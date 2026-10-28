//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#pragma once

#include "io/output_file.hpp"

#include <filesystem>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_set>

namespace MKD::detail
{
    class DirectoryOutput
    {
    public:
        DirectoryOutput(std::filesystem::path root, bool overwrite, bool flat);
        Result<void> prepare();
        Result<void> enterDirectory(const std::filesystem::path& relativeDirectory);

        template <typename Producer>
        Result<FileWriteResult> writeFile(const std::filesystem::path& relativePath, Producer&& produce)
        {
            auto path = preparePath(relativePath);
            if (!path)
                return std::unexpected(path.error());

            if (!overwrite_)
            {
                auto existing = existingFile(*path);
                if (!existing)
                    return std::unexpected(existing.error());
                if (*existing)
                    return FileWriteResult{true, 0};
            }

            OutputFile file(*path, overwrite_);
            if (!file.isOpen())
                return openFailure(*path);
            if (auto result = produce(file); !result)
                return std::unexpected(result.error());

            return file.finish().transform([](const uint64_t bytes) { return FileWriteResult{false, bytes}; });
        }

        template <typename Producer>
        Result<FileWriteResult> writeStream(const std::filesystem::path& path, Producer&& produce)
        {
            return writeFile(path, [&](OutputFile& file) { return produce(file.stream()); });
        }

        template <typename Producer>
        Result<FileWriteResult> writeBytes(const std::filesystem::path& path, Producer&& produce)
        {
            return writeFile(path, [&](OutputFile& file) -> Result<void>
            {
                auto bytes = produce();
                if (!bytes)
                    return std::unexpected(bytes.error());

                return file.write(bytes->span());
            });
        }

    private:
        Result<void> ensureDirectory(const std::filesystem::path& relativeDirectory);
        Result<std::filesystem::path> preparePath(const std::filesystem::path& relativePath);
        static Result<bool> existingFile(const std::filesystem::path& path);
        Result<FileWriteResult> openFailure(const std::filesystem::path& path) const;

        std::filesystem::path root_;
        std::filesystem::path activeParent_;
        bool overwrite_;
        bool flat_;
        std::mutex mutex_;
        std::unordered_set<std::filesystem::path> directories_;
        std::unordered_set<std::filesystem::path> flatNames_;
    };
}
