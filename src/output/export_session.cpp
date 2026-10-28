//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#include "export_session.hpp"
#include "encoders.hpp"
#include "tar_output.hpp"
#include "directory_output.hpp"
#include "unicode/unicode.hpp"
#include "platform/parallel.hpp"
#include "resource/rsc/resource_scan.hpp"
#include "resource/nrsc/named_resource_scan.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <bit>
#include <utility>
#include <format>
#include <unordered_set>

namespace MKD::detail
{
    template <typename Output>
    ExportSession<Output>::ExportSession(const Dictionary& dictionary, Output& output,
                                         const ExportOptions& options, ExportCallback progress)
        : dictionary_(dictionary), options_(options), callback_(std::move(progress)),
          output_(output)
    {
    }

    template <typename Output>
    bool ExportSession<Output>::needsAudioExtension(std::string_view id)
    {
        const auto filename = id.substr(id.find_last_of('/') + 1);
        return filename.find('.') == std::string_view::npos;
    }

    template <typename Output>
    bool ExportSession<Output>::selected(ResourceType type) const
    {
        return options_.resources.empty() || std::ranges::any_of(options_.resources,
                                                                 [type](const ResourceType candidate)
                                                                 {
                                                                     return candidate == type || (candidate ==
                                                                         ResourceType::AudioLegacy && type ==
                                                                         ResourceType::Audio);
                                                                 });
    }

    template <typename Output>
    size_t ExportSession<Output>::fileCount(const ResourceType type) const
    {
        switch (type)
        {
        case ResourceType::Headlines: return dictionary_.headlines().size();
        case ResourceType::Keystores:
            return dictionary_.keystores().size() * std::popcount(std::to_underlying(options_.keystoreExportMode));
        case ResourceType::AppendixEntries: return dictionary_.appendixEntryLists().size();
        default: return dictionary_.resourceCount(type);
        }
    }

    template <typename Output>
    Result<std::filesystem::path> ExportSession<Output>::pathFor(const ResourceType type, std::string_view name) const
    {
        if (unicode::firstInvalidUtf8Offset(std::span(reinterpret_cast<const uint8_t*>(name.data()), name.size())))
            return std::unexpected("Output filename is not valid UTF-8");
        const std::filesystem::path filename(std::u8string_view(
            reinterpret_cast<const char8_t*>(name.data()), name.size()));
        return directoryFor(type) / filename;
    }

    template <typename Output>
    std::filesystem::path ExportSession<Output>::directoryFor(const ResourceType type) const
    {
        if (!options_.createSubdirectories)
            return {};
        return type == ResourceType::Headlines
                   ? "Headline"
                   : type == ResourceType::Keystores
                   ? "Keystore"
                   : resourceTypeName(type);
    }

    template <typename Output>
    void ExportSession<Output>::record(ExportAccumulator& progress, Result<FileWriteResult> result,
                                       const std::filesystem::path& path)
    {
        if (!result)
        {
            const auto name = path.generic_u8string();
            const std::string_view text(reinterpret_cast<const char*>(name.data()), name.size());
            progress.recordFailure(std::format("{}: {}", text, result.error()));
        }
        else if (result->skipped)
            progress.recordSkip();
        else
            progress.recordExport(result->bytes);
    }

    template <typename Output>
    template <typename Scan>
    void ExportSession<Output>::writeBatches(const Scan& scan, const ResourceType type, ExportAccumulator& progress)
    {
        runBatches(scan.size(), options_.concurrency, [&](size_t group)
        {
            auto batch = scan.batchAt(group);
            for (size_t i = 0; i < batch.size(); ++i)
            {
                const auto id = batch.idAt(i);
                std::string filename;
                if constexpr (std::integral<decltype(id)>)
                    filename = std::format("{:06}{}", id, type == ResourceType::Contents ? ".xml" : ".aac");
                else
                {
                    filename = id;
                    if (type == ResourceType::Audio && needsAudioExtension(id))
                        filename += ".aac";
                }
                const auto path = pathFor(type, filename);
                if (!path)
                {
                    progress.recordFailure(path.error());
                    continue;
                }
                if (type == ResourceType::Contents && options_.prettyPrintXml)
                    record(progress, output_.writeStream(*path, [&](std::ostream& file) -> Result<void>
                    {
                        return batch.dataAt(i).and_then([&](const RetainedSpan& data)
                        {
                            return writePrettyXml(data.span(), file);
                        });
                    }), *path);
                else
                    record(progress, output_.writeBytes(*path, [&] { return batch.dataAt(i); }), *path);
            }
        });
    }

    template <typename Output>
    void ExportSession<Output>::writeResources(const ResourceStore& store, const ResourceType type,
                                               ExportAccumulator& progress)
    {
        if (auto scan = ResourceScan::open(store); !scan)
            progress.recordFailure(scan.error(), store.size());
        else
            writeBatches(*scan, type, progress);
    }

    template <typename Output>
    void ExportSession<Output>::writeResources(const NamedResourceStore& store, const ResourceType type,
                                               ExportAccumulator& progress)
    {
        auto scan = NamedResourceScan::open(store);
        if (!scan)
        {
            progress.recordFailure(scan.error(), store.size());
            return;
        }
        if (type == ResourceType::Audio)
        {
            // Extensionless audio IDs and literal .aac IDs must not claim the same file.
            for (size_t i = 0; i < store.size(); ++i)
            {
                const auto id = store.idAt(i);
                if (!id || !id->ends_with(".aac"))
                    continue;

                const auto base = id->substr(0, id->size() - 4);
                if (needsAudioExtension(base) && store.indexOf(base))
                {
                    progress.recordFailure("Audio IDs resolve to duplicate output filenames", store.size());
                    return;
                }
            }
        }
        writeBatches(*scan, type, progress);
    }

    template <typename Output>
    void ExportSession<Output>::writeFonts(ExportAccumulator& progress)
    {
        for (const auto& font : dictionary_.fonts())
        {
            const auto path = pathFor(ResourceType::Fonts, font.filename());
            if (!path)
            {
                progress.recordFailure(path.error());
                continue;
            }
            record(progress, output_.writeStream(*path, [&](std::ostream& file) -> Result<void>
            {
                return font.writeTo(file).transform([](uint64_t)
                {
                });
            }), *path);
        }
    }

    template <typename Output>
    void ExportSession<Output>::writeTables(const ResourceType type, ExportAccumulator& progress)
    {
        if (type == ResourceType::Headlines)
        {
            for (const auto& store : dictionary_.headlines())
            {
                const auto path = pathFor(type, std::format("{}.tsv", store.filename()));
                if (!path)
                {
                    progress.recordFailure(path.error());
                    continue;
                }
                record(progress,
                       output_.writeStream(*path, [&](std::ostream& file) { return writeHeadlines(store, file); }),
                       *path);
            }
            return;
        }
        for (const auto& store : dictionary_.keystores())
        {
            for (const bool inverse : {false, true})
            {
                const auto mode = inverse ? KeystoreExportMode::Inverse : KeystoreExportMode::Forward;
                if ((std::to_underlying(options_.keystoreExportMode) & std::to_underlying(mode)) == 0)
                    continue;
                const auto path = pathFor(type, std::format("{}{}.tsv", store.filename(), inverse ? "_inverse" : ""));
                if (!path)
                {
                    progress.recordFailure(path.error());
                    continue;
                }
                record(progress, output_.writeStream(*path, [&](std::ostream& file)
                {
                    return inverse ? writeInverseKeys(store, file) : writeForwardKeys(store, file);
                }), *path);
            }
        }
    }

    template <typename Output>
    void ExportSession<Output>::writeAppendices(ExportAccumulator& progress)
    {
        std::unordered_set<std::string> names;
        for (const auto& list : dictionary_.appendixEntryLists())
        {
            std::string base = list.title().empty()
                                   ? std::filesystem::path(list.filename()).stem().string()
                                   : std::string(list.title());
            for (auto& character : base)
                if (static_cast<unsigned char>(character) < 0x20 || std::string_view("<>:\"/\\|?*").contains(character))
                    character = '_';

            while (!base.empty() && (base.back() == '.' || base.back() == ' '))
                base.pop_back();

            if (base.empty())
                base = "entries";

            auto name = base;
            for (size_t ordinal = 2; !names.insert(name).second; ++ordinal)
                name = std::format("{}-{}", base, ordinal);

            const auto path = pathFor(ResourceType::AppendixEntries, name + ".tsv");
            if (!path)
            {
                progress.recordFailure(path.error());
                continue;
            }
            record(progress, output_.writeStream(*path, [&](std::ostream& file)
            {
                return writeAppendixEntries(list, dictionary_.headlines(), file);
            }), *path);
        }
    }

    template <typename Output>
    void ExportSession<Output>::runPhase(ResourceType type, ExportAccumulator& progress)
    {
        switch (type)
        {
        case ResourceType::Contents: writeResources(*dictionary_.entries(), type, progress);
            break;
        case ResourceType::Graphics: writeResources(*dictionary_.graphics(), type, progress);
            break;
        case ResourceType::Audio:
            std::visit([&](const auto& store) { writeResources(store, type, progress); }, *dictionary_.audio());
            break;
        case ResourceType::Fonts: writeFonts(progress);
            break;
        case ResourceType::Headlines:
        case ResourceType::Keystores: writeTables(type, progress);
            break;
        case ResourceType::AppendixEntries: writeAppendices(progress);
            break;
        case ResourceType::AudioLegacy: std::unreachable();
        }
    }

    template <typename Output>
    Result<ExportResult> ExportSession<Output>::run()
    {
        if (std::to_underlying(options_.keystoreExportMode) < 1 ||
            std::to_underlying(options_.keystoreExportMode) > std::to_underlying(KeystoreExportMode::Both))
            return std::unexpected("Invalid keystore export mode");

        constexpr std::array phases{
            ResourceType::Audio, ResourceType::Contents, ResourceType::AppendixEntries,
            ResourceType::Fonts, ResourceType::Graphics, ResourceType::Headlines, ResourceType::Keystores
        };
        for (const auto type : options_.resources)
            if (std::ranges::find(phases, type) == phases.end() && type != ResourceType::AudioLegacy)
                return std::unexpected("Invalid export resource selection");

        if (auto prepared = output_.prepare(); !prepared)
            return std::unexpected(prepared.error());

        size_t total = 0;
        for (const auto type : phases)
            if (selected(type))
                total += fileCount(type);

        if (callback_)
            callback_(ExportBeginEvent{total});

        ExportResult combined;
        for (const auto type : phases)
        {
            const auto count = selected(type) ? fileCount(type) : 0;
            if (count == 0)
                continue;
            if (callback_)
                callback_(PhaseBeginEvent{type, count});

            ExportAccumulator progress(count, type, callback_);
            Result<void> prepared;
            if constexpr (requires { output_.enterDirectory(directoryFor(type)); })
                prepared = output_.enterDirectory(directoryFor(type));
            if (!prepared)
                progress.recordFailure(std::format("{}: {}", directoryFor(type).string(), prepared.error()), count);
            else
                runPhase(type, progress);

            auto result = progress.finalize();
            if (callback_)
                callback_(PhaseEndEvent{type, result});
            combined += result;
        }
        if constexpr (requires { output_.finish(std::move(combined)); })
            return output_.finish(std::move(combined));
        else
            return combined;
    }
}

namespace MKD
{
    Result<ExportResult> exportToDirectory(const Dictionary& dictionary, const std::filesystem::path& destination,
                                           const ExportOptions& options, ExportCallback progress)
    {
        detail::DirectoryOutput output(destination, options.overwriteExisting, !options.createSubdirectories);
        return detail::ExportSession(dictionary, output, options, std::move(progress)).run();
    }

    Result<ExportResult> exportToTar(const Dictionary& dictionary, const std::filesystem::path& destination,
                                     const ExportOptions& options, ExportCallback progress)
    {
        detail::TarOutput output(destination, options.overwriteExisting);
        return detail::ExportSession(dictionary, output, options, std::move(progress)).run();
    }
}
