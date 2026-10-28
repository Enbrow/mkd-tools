//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#pragma once

#include "MKD/result.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>

namespace MKD::detail
{
    struct FileWriteResult
    {
        bool skipped = false;
        uint64_t bytes = 0;
    };

    [[nodiscard]] Result<void> validateRelativeOutputPath(const std::filesystem::path& path);

    class OutputFile
    {
    public:
        static Result<void> writeAll(const std::filesystem::path& path, std::span<const uint8_t> bytes);

        OutputFile(const std::filesystem::path& path, bool overwrite);
        ~OutputFile();
        OutputFile(const OutputFile&) = delete;
        OutputFile& operator=(const OutputFile&) = delete;

        [[nodiscard]] bool isOpen() const noexcept;
        Result<void> write(std::span<const uint8_t> bytes);
        std::ostream& stream() noexcept;
        Result<uint64_t> finish();

    private:
        std::filesystem::path path_;
        std::ofstream stream_;
        bool finished_ = false;
        bool streamed_ = false;
        uint64_t bytes_ = 0;
    };
}
