//
// kiwakiwaaにより 2026/02/24 に作成されました。
//

#include "dictionary/package/paths.hpp"
#include "MKD/dictionary/dictionary_product.hpp"
#include "dictionary/package/package_format.hpp"
#include "dictionary/navigation/index_format.hpp"
#include "dictionary/package/content_info.hpp"
#include "dictionary/navigation/appendix_reader.hpp"
#include "MKD/resource/resource_loader.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace MKD
{
    namespace
    {
        std::optional<std::string> resolveLocalized(const std::optional<LocalizedString>& value)
        {
            if (!value)
                return std::nullopt;
            if (value->empty())
                return std::nullopt;
            return value->resolve();
        }

        struct ProductBase
        {
            DictionaryMetadata metadata;
            DictionaryPaths paths;
            AppendixMenu appendices;
        };

        Result<ProductBase> loadProductBase(fs::path path)
        {
            std::error_code error;
            path = fs::absolute(path, error);
            if (error) return std::unexpected(error.message());
            auto metadata = DictionaryMetadata::loadFromPath(
                path / detail::monokakido::paths::productContents / (path.stem().string() + ".json")
            );
            if (!metadata)
                return std::unexpected(std::format("Failed to load metadata: '{}'", metadata.error()));

            if (metadata->contents.empty())
                return std::unexpected("Product has no content entries");

            auto paths = DictionaryPaths::create(path);
            if (!paths)
                return std::unexpected(std::format("Failed to resolve paths: '{}'", paths.error()));

            auto appendices = detail::AppendixReader::open(path / detail::monokakido::paths::productContents);
            if (!appendices) return std::unexpected(appendices.error());

            return ProductBase{
                .metadata = std::move(*metadata),
                .paths = std::move(*paths),
                .appendices = std::move(*appendices),
            };
        }

        Result<DictionaryResources> loadResources(const ResourceLoader& loader, const DictionaryContent& contentDef)
        {
            DictionaryResources resources;
            const auto& directory = contentDef.directory;
            const auto& id = contentDef.identifier;

            auto entries = loader.loadEntries(directory, id);
            if (!entries)
                return std::unexpected(entries.error());

            if (*entries)
            {
                auto order = loader.loadEntryOrder(directory, **entries);
                if (!order)
                    return std::unexpected(order.error());
                resources.entryOrder = std::move(*order);
            }

            auto appendices = loader.loadAppendixEntryLists(directory);
            if (!appendices)
                return std::unexpected(appendices.error());

            auto audio = loader.loadAudio(directory, id);
            if (!audio)
                return std::unexpected(audio.error());

            auto graphics = loader.loadGraphics(directory);
            if (!graphics)
                return std::unexpected(graphics.error());

            auto fonts = loader.loadFonts(directory);
            if (!fonts)
                return std::unexpected(fonts.error());

            auto keystores = loader.loadKeystores(directory);
            if (!keystores)
                return std::unexpected(keystores.error());

            auto headlines = loader.loadHeadlines(directory);
            if (!headlines)
                return std::unexpected(headlines.error());

            auto styles = loader.loadStylesheets(directory);
            if (!styles)
                return std::unexpected(styles.error());

            resources.entries = std::move(*entries);
            resources.appendixEntryLists = std::move(*appendices);
            resources.audio = std::move(*audio);
            resources.graphics = std::move(*graphics);
            resources.fonts = std::move(*fonts);
            resources.keystores = std::move(*keystores);
            resources.headlines = std::move(*headlines);
            resources.stylesheet = std::move(styles->normal);
            resources.nightmodeStylesheet = std::move(styles->nightmode);
            return resources;
        }

        Result<std::vector<Dictionary>> buildDictionaries(const DictionaryMetadata& metadata,
                                                          const ResourceLoader& loader, const DictionaryPaths& paths)
        {
            std::vector<Dictionary> dictionaries;
            dictionaries.reserve(metadata.contents.size());
            const DictionarySearchConfiguration searchConfiguration{
                .productSearchClass = metadata.productSearchClass,
                .productDelegateClass = metadata.productDelegateClass,
            };

            for (const auto& contentDef : metadata.contents)
            {
                auto content = contentDef;
                auto configuration = searchConfiguration;
                const auto directory = paths.productRoot() / detail::monokakido::paths::productContents / content.
                    directory;
                if (auto info = detail::ContentInfo::load(directory, content, configuration); !info)
                    return std::unexpected(info.error());

                auto resources = loadResources(loader, content);
                if (!resources)
                    return std::unexpected(std::format("Content '{}': {}", contentDef.identifier, resources.error()));

                std::error_code error;
                resources->directory = std::filesystem::absolute(directory, error);
                if (error)
                    return std::unexpected(error.message());

                dictionaries.emplace_back(std::move(content), std::move(*resources), std::move(configuration));
            }

            return dictionaries;
        }

        std::vector<Dictionary> buildMetadataOnlyDictionaries(const DictionaryMetadata& metadata)
        {
            std::vector<Dictionary> dictionaries;
            dictionaries.reserve(metadata.contents.size());
            const DictionarySearchConfiguration searchConfiguration{
                .productSearchClass = metadata.productSearchClass,
                .productDelegateClass = metadata.productDelegateClass,
            };

            for (const auto& contentDef : metadata.contents)
            {
                dictionaries.emplace_back(contentDef, DictionaryResources{},
                                          searchConfiguration);
            }

            return dictionaries;
        }

        std::string composeSingleContentTitle(const DictionaryMetadata& metadata)
        {
            if (metadata.contents.size() != 1)
                return {};

            const auto& content = metadata.contents.front();
            const auto title = resolveLocalized(content.title);
            if (!title || title->empty())
                return {};

            const auto edition = resolveLocalized(content.edition);
            if (edition && !edition->empty())
                return *title + " " + *edition;

            return *title;
        }
    }

    Result<DictionaryProduct> DictionaryProduct::loadMetadata(const fs::path& path)
    {
        auto base = loadProductBase(path);
        if (!base)
            return std::unexpected(base.error());

        auto dictionaries = buildMetadataOnlyDictionaries(base->metadata);
        return DictionaryProduct(std::move(base->metadata), std::move(base->paths), std::move(dictionaries),
                                 std::move(base->appendices));
    }


    Result<DictionaryProduct> DictionaryProduct::openAtPath(const fs::path& path)
    {
        auto base = loadProductBase(path);
        if (!base)
            return std::unexpected(base.error());

        ResourceLoader loader(base->paths, &base->appendices);
        auto dictionaries = buildDictionaries(base->metadata, loader, base->paths);
        if (!dictionaries)
            return std::unexpected(dictionaries.error());
        return DictionaryProduct(std::move(base->metadata), std::move(base->paths), std::move(*dictionaries),
                                 std::move(base->appendices));
    }


    const Dictionary* DictionaryProduct::findContent(std::string_view contentId) const
    {
        const auto it = std::ranges::find(dictionaries_, contentId, &Dictionary::id);
        return it != std::ranges::cend(dictionaries_) ? &*it : nullptr;
    }


    Dictionary* DictionaryProduct::findContent(std::string_view contentId)
    {
        const auto it = std::ranges::find(dictionaries_, contentId, &Dictionary::id);
        return it != dictionaries_.end() ? &*it : nullptr;
    }


    const Dictionary* DictionaryProduct::findContent(uint16_t dictionaryId) const
    {
        const auto found = std::ranges::find_if(dictionaries_, [dictionaryId](const auto& dictionary)
        {
            return dictionary.content().dictionaryId == dictionaryId;
        });
        return found == dictionaries_.end() ? nullptr : &*found;
    }

    Dictionary* DictionaryProduct::findContent(uint16_t dictionaryId)
    {
        return const_cast<Dictionary*>(std::as_const(*this).findContent(dictionaryId));
    }

    Result<RetainedSpan> DictionaryProduct::resource(std::string_view relativePath) const
    {
        return detail::resolveProductPath(paths_.productRoot() / detail::monokakido::paths::productContents,
                                          relativePath).and_then([](const auto& path)
        {
            return RetainedSpan::map(path);
        });
    }

    Result<AppendixEntryList> DictionaryProduct::openAppendixList(const AppendixList& list) const
    {
        if (!list.path.ends_with(".entries"))
            return std::unexpected("Expected an appendix entry list");

        auto path = detail::resolveProductPath(paths_.productRoot() / detail::monokakido::paths::productContents,
                                               list.path);
        if (!path)
            return std::unexpected(path.error());

        const auto titles = detail::AppendixReader::listTitles(appendices_);
        const auto title = titles.find(list.path);
        return AppendixEntryList::open(*path, title == titles.end() ? "" : title->second);
    }

    Result<RetainedSpan> DictionaryProduct::openAppendixDocument(const AppendixDocument& document) const
    {
        if (!document.path.ends_with(".html") && !document.path.ends_with(".pdf"))
            return std::unexpected("Expected an HTML or PDF appendix document");

        return resource(document.path);
    }

    Result<std::optional<NavigationView>> DictionaryProduct::navigation() const
    {
        namespace nav = detail::monokakido::navigation;

        std::string relative;
        if (metadata_.appendixViewController == nav::generalController)
            relative = nav::rootPlist;
        else if (metadata_.appendixViewController == nav::kanwaController ||
            metadata_.appendixViewController == "KankenKJ2_IndexRootViewController" ||
            metadata_.appendixViewController == "Kanjikai_IndexRootViewController")
            relative = metadata_.contents.front().directory + "/" + nav::directory + "/" + nav::rootIndex;
        else if (metadata_.appendixViewController)
            return std::unexpected("Specialised navigation controller: " + *metadata_.appendixViewController);
        else
            return std::optional<NavigationView>{};

        return NavigationView::open(paths_.productRoot() / detail::monokakido::paths::productContents, {relative, {}}).
            transform([](NavigationView&& root)
            {
                return std::optional(std::move(root));
            });
    }

    std::string DictionaryProduct::displayTitle() const
    {
        if (const auto singleContentTitle = composeSingleContentTitle(metadata_); !singleContentTitle.empty())
            return singleContentTitle;
        if (const auto title = resolveLocalized(metadata_.displayName))
            return *title;
        if (const auto title = resolveLocalized(metadata_.productTitle))
            return *title;
        if (!metadata_.productIdentifier.empty())
            return metadata_.productIdentifier;
        return paths_.productRoot().stem().string();
    }


    std::optional<fs::path> DictionaryProduct::iconPath() const
    {
        const auto iconsDir = paths_.productRoot() / detail::monokakido::paths::productContents / "icons";
        if (!fs::is_directory(iconsDir))
            return std::nullopt;

        const auto iconBase = paths_.productRoot().stem().string();
        const std::array preferred = {
            iconsDir / (iconBase + "-76@3x.png"),
            iconsDir / (iconBase + "-76@2x.png"),
            iconsDir / (iconBase + "-76.png"),
            iconsDir / (iconBase + "-60@3x.png"),
            iconsDir / (iconBase + "-60@2x.png"),
            iconsDir / (iconBase + "-60.png"),
            iconsDir / (iconBase + "-29@3x.png"),
            iconsDir / (iconBase + "-29@2x.png"),
            iconsDir / (iconBase + "-29.png"),
            iconsDir / (iconBase + "-20@3x.png"),
            iconsDir / (iconBase + "-20@2x.png"),
            iconsDir / (iconBase + "-20.png"),
        };

        for (const auto& candidate : preferred)
        {
            if (fs::is_regular_file(candidate))
                return candidate;
        }

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(iconsDir, ec))
        {
            if (ec)
                break;
            if (entry.is_regular_file() && entry.path().extension() == ".png")
                return entry.path();
        }

        return std::nullopt;
    }


    DictionaryProduct::DictionaryProduct(DictionaryMetadata metadata, DictionaryPaths paths,
                                         std::vector<Dictionary> dictionaries, AppendixMenu appendices)
        : appendices_(std::move(appendices)), metadata_(std::move(metadata)), paths_(std::move(paths)),
          dictionaries_(std::move(dictionaries))
    {
    }
}
