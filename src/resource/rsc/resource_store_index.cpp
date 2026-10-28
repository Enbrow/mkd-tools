//
// kiwakiwaaにより 2026/01/19 に作成されました。
//

#include "resource_store_index.hpp"
#include "resource/detail/resource_files.hpp"

#include <algorithm>
#include <format>

namespace MKD
{
    Result<ResourceStoreIndex::Map> ResourceStoreIndex::readMap(detail::BinaryView data)
    {
        auto header = data.slice(0, 8);
        if (!header)
            return std::unexpected("Map file too small for header");

        const auto version = *header->readLittle<uint32_t>(0);
        if (version > 1)
            return std::unexpected(std::format("Unsupported map version: {}", version));

        auto locations = data.table(8, *header->readLittle<uint32_t>(4), 8);
        if (!locations)
            return std::unexpected(locations.error());

        return Map{.version = version, .locations = *locations};
    }

    Result<detail::BinaryView> ResourceStoreIndex::readIdIndex(detail::BinaryView data)
    {
        auto header = data.slice(0, 8);
        if (!header)
            return std::unexpected("Index file too small for header");

        const auto count = *header->readLittle<uint32_t>(0);
        auto table = data.table(8, count, 8);
        if (!table)
            return std::unexpected(table.error());

        for (size_t i = 1; i < count; ++i)
            if (*table->readLittle<uint32_t>((i - 1) * 8) >= *table->readLittle<uint32_t>(i * 8))
                return std::unexpected("RSC index IDs must be unique and sorted");

        return table;
    }

    Result<std::optional<MappedFile>> ResourceStoreIndex::openIndex(const std::filesystem::path& directory)
    {
        auto path = detail::findResourceFile(directory, ".idx");
        if (!path)
            return std::unexpected(path.error());

        if (!*path)
            return std::optional<MappedFile>{};

        auto file = MappedFile::open(**path);
        if (!file)
            return std::unexpected(file.error());

        return std::optional(std::move(*file));
    }

    Result<void> ResourceStoreIndex::validateMappings(detail::BinaryView locations, detail::BinaryView index)
    {
        const size_t count = locations.bytes().size() / 8;
        for (size_t offset = 0; offset < index.bytes().size(); offset += 8)
            if (*index.readLittle<uint32_t>(offset + 4) >= count)
                return std::unexpected(
                    std::format("Invalid map index for item {}", *index.readLittle<uint32_t>(offset)));
        return {};
    }

    Result<ResourceStoreIndex> ResourceStoreIndex::open(const std::filesystem::path& directory)
    {
        auto path = detail::findResourceFile(directory, ".map");
        if (!path)
            return std::unexpected(path.error());

        if (!*path)
            return std::unexpected(std::format("No .map file in '{}'", directory.string()));

        auto file = MappedFile::open(**path);
        if (!file)
            return std::unexpected(file.error());

        auto map = readMap(detail::BinaryView(file->data()));
        if (!map)
            return std::unexpected(map.error());

        auto indexFile = openIndex(directory);
        if (!indexFile)
            return std::unexpected(indexFile.error());

        ResourceStoreIndex result;
        result.version_ = map->version;
        result.locations_ = map->locations;
        result.mapFile_ = std::move(*file);
        result.indexFile_ = std::move(*indexFile);
        if (result.indexFile_)
        {
            auto index = readIdIndex(detail::BinaryView(result.indexFile_->data()));
            if (!index)
                return std::unexpected(index.error());
            if (auto valid = validateMappings(result.locations_, *index); !valid)
                return std::unexpected(valid.error());
            result.index_ = *index;
        }
        return result;
    }

    uint32_t ResourceStoreIndex::idAt(size_t index) const
    {
        return indexFile_ ? *index_.readLittle<uint32_t>(index * 8) : static_cast<uint32_t>(index);
    }

    Result<size_t> ResourceStoreIndex::indexOf(uint32_t id) const
    {
        if (!indexFile_)
        {
            if (id >= size())
                return std::unexpected(std::format("Item ID {} not found", id));
            return id;
        }
        const auto positions = std::views::iota(size_t{0}, size());
        const auto it = std::ranges::lower_bound(positions, id, {}, [this](size_t i) { return idAt(i); });
        if (it == positions.end() || idAt(*it) != id)
            return std::unexpected(std::format("Item ID {} not found", id));
        return *it;
    }

    Result<ResourceStoreMapRecord> ResourceStoreIndex::findById(const uint32_t id) const
    {
        return indexOf(id).transform([this](const size_t position) { return itemAt(position)->second; });
    }

    Result<std::pair<uint32_t, ResourceStoreMapRecord>> ResourceStoreIndex::itemAt(size_t index) const
    {
        if (index >= size())
            return std::unexpected(std::format("Index {} out of range (size: {})", index, size()));

        const size_t location = indexFile_ ? *index_.readLittle<uint32_t>(index * 8 + 4) : index;
        return std::pair{
            idAt(index), ResourceStoreMapRecord{
                .chunkGlobalOffset = *locations_.readLittle<uint32_t>(location * 8),
                .itemOffset = *locations_.readLittle<uint32_t>(location * 8 + 4),
            }
        };
    }
}
