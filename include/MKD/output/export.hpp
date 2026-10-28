//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#pragma once

#include "MKD/output/export_options.hpp"
#include "MKD/result.hpp"

#include <filesystem>

namespace MKD
{
    class Dictionary;

    /**
     * Synchronously exports the selected resources. An empty selection includes all resource types.
     * Failed, partially written files are removed.
     */
    [[nodiscard]] Result<ExportResult> exportToDirectory(
        const Dictionary& dictionary,
        const std::filesystem::path& destination,
        const ExportOptions& options = {},
        ExportCallback progress = {});

    /**
     * Exports one dictionary to an uncompressed tar archive. Counts and byte totals describe members.
     * Existing archives require overwriteExisting. Any failure discards the staged archive.
     * Progress describes processed members before publication. Member order follows worker completion.
     */
    [[nodiscard]] Result<ExportResult> exportToTar(
        const Dictionary& dictionary,
        const std::filesystem::path& destination,
        const ExportOptions& options = {},
        ExportCallback progress = {});
}
