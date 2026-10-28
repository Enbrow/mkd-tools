//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#pragma once

#include "io/output_file.hpp"
#include "export_accumulator.hpp"
#include "MKD/dictionary/dictionary.hpp"
#include "MKD/output/export.hpp"

namespace MKD::detail
{
    template <typename Output>
    class ExportSession
    {
    public:
        ExportSession(const Dictionary& dictionary, Output& output,
                      const ExportOptions& options, ExportCallback progress);
        Result<ExportResult> run();

    private:
        static bool needsAudioExtension(std::string_view id);
        [[nodiscard]] bool selected(ResourceType type) const;
        [[nodiscard]] size_t fileCount(ResourceType type) const;

        [[nodiscard]] std::filesystem::path directoryFor(ResourceType type) const;
        [[nodiscard]] Result<std::filesystem::path> pathFor(ResourceType type, std::string_view name) const;

        static void record(ExportAccumulator& progress, Result<FileWriteResult> result,
                           const std::filesystem::path& path);

        void writeResources(const ResourceStore& store, ResourceType type, ExportAccumulator& progress);
        void writeResources(const NamedResourceStore& store, ResourceType type, ExportAccumulator& progress);
        template <typename Scan>
        void writeBatches(const Scan& scan, ResourceType type, ExportAccumulator& progress);
        void writeFonts(ExportAccumulator& progress);
        void writeTables(ResourceType type, ExportAccumulator& progress);
        void writeAppendices(ExportAccumulator& progress);

        void runPhase(ResourceType type, ExportAccumulator& progress);

        const Dictionary& dictionary_;
        ExportOptions options_;
        ExportCallback callback_;
        Output& output_;
    };
}
