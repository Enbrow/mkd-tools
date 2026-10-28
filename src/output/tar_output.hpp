#pragma once

#include "MKD/output/export_result.hpp"
#include "io/output_file.hpp"
#include "io/build_directory.hpp"

#include <array>
#include <fstream>
#include <mutex>
#include <optional>
#include <unordered_set>

namespace MKD::detail
{
    class TarOutput
    {
    public:
        TarOutput(std::filesystem::path destination, bool overwrite);
        Result<void> prepare();
        Result<ExportResult> finish(ExportResult result);

        template <typename Producer>
        Result<FileWriteResult> writeBytes(const std::filesystem::path& path, Producer&& produce)
        {
            auto bytes = produce();
            if (!bytes)
                return std::unexpected(bytes.error());

            std::lock_guard lock(mutex_);
            auto header = beginMember(path, bytes->size());
            if (!header)
                return std::unexpected(header.error());

            stream_.write(reinterpret_cast<const char*>(bytes->span().data()),
                          static_cast<std::streamsize>(bytes->size()));
            position_ += bytes->size();
            return endMember(*header);
        }

        template <typename Producer>
        Result<FileWriteResult> writeStream(const std::filesystem::path& path, Producer&& produce)
        {
            std::lock_guard lock(mutex_);
            auto header = beginMember(path, std::nullopt);
            if (!header)
                return std::unexpected(header.error());

            if (auto result = produce(stream_); !result)
            {
                failure_ = result.error();
                return std::unexpected(*failure_);
            }

            return endMember(*header);
        }

    private:
        struct Member
        {
            std::streampos start;
            std::streampos sizeField;
            bool streamed;
        };

        Result<Member> beginMember(const std::filesystem::path& path, std::optional<uint64_t> size);
        Result<FileWriteResult> endMember(const Member& member);
        void writeHeader(std::string_view name, uint64_t size, char type);
        void pad(uint64_t size);
        Result<FileWriteResult> fail(std::string error);

        std::filesystem::path destination_;
        bool overwrite_;
        std::unique_ptr<BuildDirectory> staging_;
        std::array<char, 128 * 1024> buffer_{};
        std::ofstream stream_;
        uint64_t position_ = 0;
        std::mutex mutex_;
        std::unordered_set<std::filesystem::path> names_;
        std::optional<std::string> failure_;
    };
}
