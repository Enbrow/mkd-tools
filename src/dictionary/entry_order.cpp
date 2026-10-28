//
// kiwakiwaaにより 2026/10/18 に作成されました。
//

#include "MKD/dictionary/entry_order.hpp"
#include "binary/binary_view.hpp"
#include "MKD/resource/resource_store.hpp"

#include <limits>

namespace MKD
{
    EntryOrder::EntryOrder(const uint32_t pageCount) noexcept
        : count_(pageCount), maximum_(pageCount)
    {
    }

    Result<EntryOrder> EntryOrder::open(const std::filesystem::path& path, const ResourceStore& entries)
    {
        if (entries.size() > std::numeric_limits<uint32_t>::max())
            return std::unexpected("Page count exceeds the entry order format");

        const auto pageCount = static_cast<uint32_t>(entries.size());
        EntryOrder order(pageCount);
        std::error_code error;
        const auto status = std::filesystem::status(path, error);
        if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status)))
            return order;
        if (error)
            return std::unexpected("Cannot inspect entry order: " + error.message());

        auto data = RetainedSpan::map(path);
        if (!data)
            return std::unexpected("Cannot map entry order: " + data.error());
        if (data->size() < 8)
            return std::unexpected("Entry order header is truncated");

        const detail::BinaryView bytes(data->span());
        const auto count = *bytes.readLittle<uint32_t>(0);
        order.maximum_ = *bytes.readLittle<uint32_t>(4);
        if (count != pageCount)
            return std::unexpected("Entry order count does not match the page store");
        if (uint64_t{8} + uint64_t{4} * (uint64_t{count} + order.maximum_) != data->size())
            return std::unexpected("Entry order tables have an invalid size");

        order.data_ = std::move(*data);
        if (auto valid = order.validate(); !valid)
            return std::unexpected(valid.error());

        for (size_t i = 0; i < entries.size(); ++i)
        {
            auto id = entries.idAt(i);
            if (!id)
                return std::unexpected(id.error());
            if (!order.indexOf(*id))
                return std::unexpected("Entry order does not contain every stored page");
        }
        return order;
    }

    Result<void> EntryOrder::validate() const
    {
        for (size_t i = 0; i < count_; ++i)
        {
            const auto id = page(i);
            if (id == 0 || id > maximum_ || position(id) != i)
                return std::unexpected("Entry order contains an invalid or repeated page");
        }

        for (uint64_t id = 1; id <= maximum_; ++id)
        {
            const auto index = position(static_cast<uint32_t>(id));
            if (index == std::numeric_limits<uint32_t>::max())
                continue;
            if (index >= count_ || page(index) != id)
                return std::unexpected("Entry order contains an invalid page position");
        }
        return {};
    }

    size_t EntryOrder::size() const noexcept { return count_; }
    bool EntryOrder::empty() const noexcept { return count_ == 0; }

    uint32_t EntryOrder::position(const uint32_t pageId) const
    {
        if (data_.empty())
            return pageId - 1;

        return *detail::BinaryView(data_.span()).readLittle<uint32_t>(8 + size_t{4} * (pageId - 1));
    }

    uint32_t EntryOrder::page(const size_t index) const
    {
        if (data_.empty())
            return static_cast<uint32_t>(index + 1);

        return *detail::BinaryView(data_.span()).readLittle<uint32_t>(8 + size_t{4} * (size_t{maximum_} + index));
    }

    Result<uint32_t> EntryOrder::pageAt(const size_t index) const
    {
        if (index >= count_)
            return std::unexpected("Entry order index is out of range");
        return page(index);
    }

    Result<size_t> EntryOrder::indexOf(const uint32_t pageId) const
    {
        if (pageId == 0 || pageId > maximum_)
            return std::unexpected("Page is not in the entry order");

        const auto index = position(pageId);
        if (index >= count_)
            return std::unexpected("Page is not in the entry order");

        return index;
    }

    Result<std::optional<uint32_t>> EntryOrder::previous(const uint32_t pageId) const
    {
        return indexOf(pageId).transform([this](const size_t index) -> std::optional<uint32_t>
        {
            if (index == 0)
                return std::nullopt;

            return page(index - 1);
        });
    }

    Result<std::optional<uint32_t>> EntryOrder::next(const uint32_t pageId) const
    {
        return indexOf(pageId).transform([this](const size_t index) -> std::optional<uint32_t>
        {
            if (index + 1 == count_)
                return std::nullopt;

            return page(index + 1);
        });
    }
}
