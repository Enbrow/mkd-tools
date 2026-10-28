//
// kiwakiwaaにより 2026/01/16 に作成されました。
//

#include "MKD/resource/named_resource_store.hpp"
#include "named_resource_store_index.hpp"
#include "named_resource_store_contents.hpp"
#include "named_resource_scan.hpp"

#include <algorithm>

#include <format>

namespace MKD
{
    struct NamedResourceStore::Impl
    {
        NamedResourceStoreIndex index;
        NamedResourceStoreContents data;

        Impl(NamedResourceStoreIndex&& index, NamedResourceStoreContents&& data)
            : index(std::move(index)), data(std::move(data))
        {
        }
    };


    NamedResourceStore::NamedResourceStore(std::unique_ptr<Impl> impl) noexcept
        : impl_(std::move(impl))
    {
    }


    NamedResourceStore::~NamedResourceStore() = default;
    NamedResourceStore::NamedResourceStore(NamedResourceStore&&) noexcept = default;
    NamedResourceStore& NamedResourceStore::operator=(NamedResourceStore&&) noexcept = default;


    Result<NamedResourceStore> NamedResourceStore::open(const std::filesystem::path& directoryPath)
    {
        auto indexResult = NamedResourceStoreIndex::open(directoryPath);
        if (!indexResult)
            return std::unexpected(std::format("Failed to load nrsc index: {}", indexResult.error()));

        auto dataResult = NamedResourceStoreContents::open(directoryPath);
        if (!dataResult)
            return std::unexpected(std::format("Failed to load nrsc data: {}", dataResult.error()));

        return NamedResourceStore{std::make_unique<Impl>(std::move(*indexResult), std::move(*dataResult))};
    }


    Result<RetainedSpan> NamedResourceStore::get(std::string_view id) const
    {
        return impl_->index.findById(id).and_then([this](const auto& record)
        {
            return impl_->data.get(record);
        });
    }


    Result<NamedResourceStoreItem> NamedResourceStore::itemAt(const size_t index) const
    {
        return impl_->index.itemAt(index).and_then([this](const auto& item)
        {
            return impl_->data.get(item.second).transform([id = item.first](RetainedSpan&& data)
            {
                return NamedResourceStoreItem{std::string(id), std::move(data)};
            });
        });
    }


    size_t NamedResourceStore::size() const noexcept
    {
        return impl_->index.size();
    }


    bool NamedResourceStore::empty() const noexcept
    {
        return impl_->index.empty();
    }


    Result<std::string_view> NamedResourceStore::idAt(size_t index) const
    {
        return impl_->index.idAt(index);
    }

    Result<size_t> NamedResourceStore::indexOf(std::string_view id) const
    {
        return impl_->index.indexOf(id);
    }
}

namespace MKD::detail
{
    Result<NamedResourceScan> NamedResourceScan::open(const NamedResourceStore& store)
    {
        NamedResourceScan scan;
        scan.contents_ = &store.impl_->data;
        scan.records_.reserve(store.size());
        for (auto item : store.impl_->index.items())
        {
            if (!item)
                return std::unexpected(item.error());

            scan.records_.push_back({item->first, item->second});
        }
        std::ranges::sort(scan.records_, {}, [](const NamedScanRecord& record)
        {
            return std::pair{record.location.fileSequence, record.location.fileOffset};
        });
        return scan;
    }

    size_t NamedResourceScan::size() const noexcept
    {
        return (records_.size() + batchSize - 1) / batchSize;
    }

    NamedResourceBatch NamedResourceScan::batchAt(const size_t index) const
    {
        const auto begin = index * batchSize;
        return {
            *contents_, std::span(records_).subspan(begin, std::min(batchSize, records_.size() - begin))
        };
    }

    NamedResourceBatch::NamedResourceBatch(const NamedResourceStoreContents& contents,
                                           std::span<const NamedScanRecord> records)
        : contents_(&contents), records_(records)
    {
    }

    Result<RetainedSpan> NamedResourceBatch::dataAt(const size_t index) const
    {
        if (index >= records_.size())
            return std::unexpected("Batch item index out of range");
        return contents_->get(records_[index].location);
    }
}
