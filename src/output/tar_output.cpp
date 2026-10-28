#include "tar_output.hpp"
#include "unicode/unicode.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <span>

namespace MKD::detail
{
    namespace
    {
        constexpr uint64_t maxUstarSize = 077777777777ULL;

        std::string paxRecord(std::string_view key, std::string_view value)
        {
            const auto body = std::format(" {}={}\n", key, value);
            size_t length = body.size() + 1;
            while (true)
            {
                auto digits = std::to_string(length);
                if (digits.size() + body.size() == length)
                    return digits + body;

                length = digits.size() + body.size();
            }
        }

        void octal(std::span<char> field, const uint64_t value)
        {
            std::ranges::fill(field, '0');
            field.back() = '\0';
            char digits[24];
            const auto result = std::to_chars(digits, digits + sizeof(digits), value, 8);
            std::copy(digits, result.ptr, field.end() - 1 - (result.ptr - digits));
        }
    }

    TarOutput::TarOutput(std::filesystem::path destination, bool overwrite)
        : destination_(std::move(destination)), overwrite_(overwrite)
    {
    }

    Result<void> TarOutput::prepare()
    {
        if (destination_.empty())
            return std::unexpected("Export destination is empty");

        auto staging = BuildDirectory::create(destination_, overwrite_);
        if (!staging)
            return std::unexpected(staging.error());

        staging_ = std::move(*staging);
        stream_.rdbuf()->pubsetbuf(buffer_.data(), buffer_.size());
        stream_.open(staging_->path() / "archive.tar", std::ios::binary | std::ios::out | std::ios::noreplace);
        if (!stream_)
            return std::unexpected("Cannot create staged tar archive");

        return {};
    }

    void TarOutput::writeHeader(std::string_view name, const uint64_t size, const char type)
    {
        std::array<char, 512> header{};
        const auto fields = std::span(header);
        const auto nameField = fields.subspan<0, 100>();
        const auto modeField = fields.subspan<100, 8>();
        const auto ownerField = fields.subspan<108, 8>();
        const auto groupField = fields.subspan<116, 8>();
        const auto sizeField = fields.subspan<124, 12>();
        const auto timeField = fields.subspan<136, 12>();
        const auto checksumField = fields.subspan<148, 8>();
        const auto typeField = fields.subspan<156, 1>();
        const auto magicField = fields.subspan<257, 6>();
        const auto versionField = fields.subspan<263, 2>();

        std::ranges::copy(name, nameField.begin());
        octal(modeField, 0644);
        octal(ownerField, 0);
        octal(groupField, 0);
        octal(sizeField, size);
        octal(timeField, 0);
        typeField.front() = type;
        std::ranges::copy(std::string_view("ustar"), magicField.begin());
        std::ranges::fill(versionField, '0');

        // The checksum is calculated with its own field filled with spaces.
        std::ranges::fill(checksumField, ' ');
        unsigned checksum = 0;
        for (const unsigned char byte : header)
            checksum += byte;

        octal(checksumField.first<7>(), checksum);
        checksumField.back() = ' ';
        stream_.write(header.data(), header.size());
        position_ += header.size();
    }

    void TarOutput::pad(const uint64_t size)
    {
        constexpr std::array<char, 512> zeros{};
        const auto padding = (512 - size % 512) % 512;
        stream_.write(zeros.data(), padding);
        position_ += padding;
    }

    Result<TarOutput::Member> TarOutput::beginMember(const std::filesystem::path& path, std::optional<uint64_t> size)
    {
        if (failure_)
            return std::unexpected(*failure_);

        if (auto valid = validateRelativeOutputPath(path); !valid)
            return std::unexpected(valid.error());

        const auto utf8 = path.generic_u8string();
        const std::string name(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        if (unicode::firstInvalidUtf8Offset(std::span(reinterpret_cast<const uint8_t*>(name.data()), name.size())))
            return std::unexpected("Output filename is not valid UTF-8");

        if (!names_.insert(path).second)
            return std::unexpected("Multiple resources resolve to the same output path");

        std::string extended;
        const bool extendedName =
            name.size() > 100 || std::ranges::any_of(name, [](unsigned char c) { return c >= 128; });
        if (extendedName)
            extended += paxRecord("path", name);

        std::streampos sizeField = -1;
        if (!size || *size > maxUstarSize)
        {
            // Reserve a fixed-width decimal size so streamed members need no second pass.
            const auto record = paxRecord("size", std::format("{:020}", size.value_or(0)));
            sizeField = std::streampos(position_ + 512 + extended.size() + record.find("size=") + 5);
            extended += record;
        }

        if (!extended.empty())
        {
            writeHeader("PaxHeader", extended.size(), 'x');
            stream_.write(extended.data(), static_cast<std::streamsize>(extended.size()));
            position_ += extended.size();
            pad(extended.size());
        }

        writeHeader(extendedName ? "member" : name, size && *size <= maxUstarSize ? *size : 0, '0');
        const auto start = std::streampos(position_);
        if (!stream_ || start < 0)
        {
            failure_ = "Failed to write tar member header";
            return std::unexpected(*failure_);
        }

        return Member{start, sizeField, !size};
    }

    Result<FileWriteResult> TarOutput::fail(std::string error)
    {
        if (!failure_)
            failure_ = std::move(error);

        return std::unexpected(*failure_);
    }

    Result<FileWriteResult> TarOutput::endMember(const Member& member)
    {
        const auto end = member.streamed ? stream_.tellp() : std::streampos(position_);
        if (!stream_ || end < member.start)
            return fail("Failed to write tar member data");

        const auto bytes = static_cast<uint64_t>(end - member.start);
        position_ = static_cast<uint64_t>(end);
        if (member.streamed)
        {
            stream_.seekp(member.sizeField);
            const auto size = std::format("{:020}", bytes);
            stream_.write(size.data(), size.size());
            stream_.seekp(end);
        }

        pad(bytes);
        if (!stream_)
            return fail("Failed to finish tar member");

        return FileWriteResult{false, bytes};
    }

    Result<ExportResult> TarOutput::finish(ExportResult result)
    {
        if (result.hasErrors() || result.failed || failure_)
            return std::unexpected(result.errors.empty()
                                       ? failure_.value_or("Tar export failed")
                                       : result.errors.front());

        constexpr std::array<char, 1024> zeros{};
        stream_.write(zeros.data(), zeros.size());
        stream_.close();
        if (!stream_)
            return std::unexpected("Failed to close tar archive");

        return staging_->publishFile("archive.tar").transform([&]
        {
            return std::move(result);
        });
    }
}
