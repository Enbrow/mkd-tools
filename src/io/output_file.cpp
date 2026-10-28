//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#include "output_file.hpp"

#include <algorithm>
#include <limits>

namespace MKD::detail
{
    Result<void> validateRelativeOutputPath(const std::filesystem::path& path)
    {
        const auto text = path.generic_u8string();
        if (text.empty() || path.is_absolute() || path.has_root_name() || text.find(u8'\\') != std::u8string::npos ||
            text.find(u8'\0') != std::u8string::npos)
            return std::unexpected("Invalid relative output path");

        for (const auto& part : path)
            if (part.empty() || part == "." || part == "..")
                return std::unexpected("Invalid relative output path component");

        return {};
    }

    Result<void> OutputFile::writeAll(const std::filesystem::path& path, std::span<const uint8_t> bytes)
    {
        OutputFile file(path, false);
        if (!file.isOpen())
            return std::unexpected("Cannot create output file: " + path.string());

        if (auto result = file.write(bytes); !result)
            return result;

        return file.finish().transform([](uint64_t)
        {
        });
    }

    OutputFile::OutputFile(const std::filesystem::path& path, bool overwrite)
        : path_(path), stream_(path, std::ios::binary | std::ios::out |
                               (overwrite ? std::ios::trunc : std::ios::noreplace))
    {
    }

    OutputFile::~OutputFile()
    {
        if (finished_ || !stream_.is_open())
            return;

        stream_.close();
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    bool OutputFile::isOpen() const noexcept { return stream_.is_open(); }

    std::ostream& OutputFile::stream() noexcept
    {
        streamed_ = true;
        return stream_;
    }

    Result<void> OutputFile::write(std::span<const uint8_t> bytes)
    {
        while (!bytes.empty())
        {
            const auto count = std::min(bytes.size(), static_cast<size_t>(std::numeric_limits<std::streamsize>::max()));
            stream_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(count));
            if (!stream_)
                return std::unexpected("Failed to write output bytes");

            bytes_ += count;
            bytes = bytes.subspan(count);
        }
        return {};
    }

    Result<uint64_t> OutputFile::finish()
    {
        stream_.flush();
        if (streamed_)
        {
            const auto position = stream_.tellp();
            if (position < 0)
                return std::unexpected("Failed to determine output size");
            bytes_ = static_cast<uint64_t>(position);
        }

        if (!stream_)
            return std::unexpected("Failed to flush output file");

        stream_.close();
        if (!stream_)
        {
            std::error_code error;
            std::filesystem::remove(path_, error);
            return std::unexpected("Failed to close output file");
        }

        finished_ = true;
        return bytes_;
    }
}
