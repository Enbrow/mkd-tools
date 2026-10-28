//
// kiwakiwaaにより 2026/01/17 に作成されました。
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace MKD
{
    /** Counts completed, skipped and failed files or archive members. Bytes exclude tar headers and padding. */
    struct ExportResult
    {
        size_t totalFiles = 0;
        size_t exported = 0;
        size_t skipped = 0;
        size_t failed = 0;
        uint64_t totalBytes = 0;
        std::vector<std::string> errors;

        [[nodiscard]] bool hasErrors() const noexcept { return !errors.empty(); }
        [[nodiscard]] bool isSuccess() const noexcept { return failed == 0; }

        ExportResult& operator+=(const ExportResult& other)
        {
            totalFiles += other.totalFiles;
            exported += other.exported;
            skipped += other.skipped;
            failed += other.failed;
            totalBytes += other.totalBytes;
            errors.insert(errors.end(), other.errors.begin(), other.errors.end());
            return *this;
        }
    };
}