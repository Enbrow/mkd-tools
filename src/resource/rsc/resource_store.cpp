//
// kiwakiwaaにより 2026/02/01 に作成されました。
//

#include "MKD/resource/resource_store.hpp"
#include "resource_store_index.hpp"
#include "resource_store_contents.hpp"
#include "resource_scan.hpp"

#include <algorithm>

#include <format>

namespace MKD
{
    struct ResourceStore::Impl
    {
        ResourceStoreIndex index;
        ResourceStoreContents data;

        Impl(ResourceStoreIndex&& index, ResourceStoreContents&& data)
            : index(std::move(index)), data(std::move(data))
        {
        }
    };


    ResourceStore::ResourceStore(std::unique_ptr<Impl> impl) noexcept
        : impl_(std::move(impl))
    {
    }


    ResourceStore::~ResourceStore() noexcept = default;
    ResourceStore::ResourceStore(ResourceStore&&) noexcept = default;
    ResourceStore& ResourceStore::operator=(ResourceStore&&) noexcept = default;


    Result<ResourceStore> ResourceStore::open(const std::filesystem::path& directoryPath, std::string_view dictId)
    {
        auto indexResult = ResourceStoreIndex::open(directoryPath);
        if (!indexResult)
            return std::unexpected(std::format("Failed to load rsc index: {}", indexResult.error()));

        auto dataResult = ResourceStoreContents::open(directoryPath, dictId, indexResult->mapVersion());
        if (!dataResult)
            return std::unexpected(std::format("Failed to load rsc data: {}", dataResult.error()));

        return ResourceStore(std::make_unique<Impl>(std::move(*indexResult), std::move(*dataResult)));
    }


    Result<RetainedSpan> ResourceStore::get(const uint32_t itemId) const
    {
        return impl_->index.findById(itemId).and_then([this](const auto& record)
        {
            return impl_->data.get(record);
        });
    }


    Result<ResourceStoreItem> ResourceStore::itemAt(const size_t index) const
    {
        return impl_->index.itemAt(index).and_then([this](const auto& item)
        {
            return impl_->data.get(item.second).transform([id = item.first](RetainedSpan&& data)
            {
                return ResourceStoreItem{id, std::move(data)};
            });
        });
    }


    Result<uint32_t> ResourceStore::idAt(const size_t index) const
    {
        return impl_->index.itemAt(index).transform([](const auto& item) { return item.first; });
    }

    Result<size_t> ResourceStore::indexOf(const uint32_t itemId) const
    {
        return impl_->index.indexOf(itemId);
    }


    size_t ResourceStore::size() const noexcept
    {
        return impl_->index.size();
    }


    bool ResourceStore::empty() const noexcept
    {
        return impl_->index.empty();
    }
}

namespace MKD::detail
{
    Result<ResourceScan> ResourceScan::open(const ResourceStore& store)
    {
        ResourceScan scan;
        scan.contents_ = &store.impl_->data;
        scan.records_.reserve(store.size());
        for (auto entry : store.impl_->index.items())
        {
            if (!entry)
                return std::unexpected(entry.error());

            scan.records_.push_back({entry->first, entry->second});
        }
        std::ranges::sort(scan.records_, {}, [](const ScanRecord& record)
        {
            return std::pair{record.location.chunkGlobalOffset, record.location.itemOffset};
        });
        for (size_t i = 0; i < scan.records_.size(); ++i)
        {
            const auto& location = scan.records_[i].location;
            if (i == 0 || location.itemOffset == 0xFFFFFFFF ||
                scan.records_[i - 1].location.itemOffset == 0xFFFFFFFF ||
                location.chunkGlobalOffset != scan.records_[i - 1].location.chunkGlobalOffset)
                scan.groups_.push_back(i);
        }
        return scan;
    }

    ResourceBatch ResourceScan::batchAt(const size_t index) const
    {
        const auto begin = groups_[index];
        const auto end = index + 1 == groups_.size() ? records_.size() : groups_[index + 1];
        return {
            *contents_, std::span(records_).subspan(begin, end - begin)
        };
    }

    ResourceBatch::ResourceBatch(const ResourceStoreContents& contents, std::span<const ScanRecord> records)
        : contents_(&contents), records_(records)
    {
    }

    Result<RetainedSpan> ResourceBatch::dataAt(const size_t index)
    {
        if (index >= records_.size())
            return std::unexpected("Batch item index out of range");

        const auto location = records_[index].location;
        if (location.itemOffset == 0xFFFFFFFF)
            return contents_->readDirectData(location.chunkGlobalOffset);

        if (error_)
            return std::unexpected(*error_);

        if (!chunk_)
        {
            auto decoded = contents_->decodeChunkAt(location.chunkGlobalOffset);
            if (!decoded)
            {
                error_ = decoded.error();
                return std::unexpected(*error_);
            }
            chunk_ = std::move(*decoded);
        }
        return ResourceStoreContents::parseItemFromChunk(*chunk_, location.itemOffset);
    }
}
