//
// kiwakiwaaにより 2026/10/16 に作成されました。
//

#include "MKD/resource/retained_span.hpp"
#include "binary/binary_view.hpp"
#include "unicode/unicode.hpp"
#include "MKD/platform/mmap_file.hpp"

#include <format>
#include <utility>

namespace MKD
{
    RetainedSpan::RetainedSpan(RetainedSpan&& other) noexcept
        : owner_(std::move(other.owner_)), span_(std::exchange(other.span_, {}))
    {
    }

    RetainedSpan& RetainedSpan::operator=(RetainedSpan&& other) noexcept
    {
        if (this != &other)
        {
            owner_ = std::move(other.owner_);
            span_ = std::exchange(other.span_, {});
        }
        return *this;
    }

    RetainedSpan::RetainedSpan(std::vector<uint8_t> bytes)
        : RetainedSpan(std::make_shared<const std::vector<uint8_t>>(std::move(bytes)))
    {
    }

    RetainedSpan::RetainedSpan(std::shared_ptr<const std::vector<uint8_t>> buffer)
        : owner_(buffer), span_(buffer ? std::span(*buffer) : std::span<const uint8_t>{})
    {
    }

    RetainedSpan::RetainedSpan(std::shared_ptr<const void> owner, std::span<const uint8_t> bytes)
        : owner_(std::move(owner)), span_(bytes)
    {
    }

    RetainedSpan RetainedSpan::copy(std::span<const uint8_t> bytes)
    {
        return RetainedSpan(std::vector(bytes.begin(), bytes.end()));
    }

    Result<RetainedSpan> RetainedSpan::map(const std::filesystem::path& path)
    {
        return MappedFile::open(path).transform([](MappedFile&& mapping)
        {
            auto owner = std::make_shared<MappedFile>(std::move(mapping));
            const auto bytes = owner->data();
            return RetainedSpan(std::move(owner), bytes);
        });
    }

    Result<RetainedSpan> RetainedSpan::slice(const size_t offset, const size_t length) const
    {
        return detail::BinaryView(span_).slice(offset, length).transform([this](const detail::BinaryView& view)
        {
            return RetainedSpan(owner_, view.bytes());
        });
    }

    Result<std::string_view> RetainedSpan::utf8() const
    {
        if (empty())
            return std::string_view{};
        if (const auto invalid = detail::unicode::firstInvalidUtf8Offset(span_))
            return std::unexpected(std::format("Invalid UTF-8 sequence at byte offset {}", *invalid));
        return std::string_view(reinterpret_cast<const char*>(data()), size());
    }
}
