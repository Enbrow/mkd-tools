//
// kiwakiwaaにより 2026/01/16 に作成されました。
//

#include "named_resource_store_index.hpp"
#include "binary/binary_view.hpp"
#include "resource/detail/resource_files.hpp"

#include <algorithm>
#include <format>

namespace MKD
{
    Result<NamedResourceStoreIndex> NamedResourceStoreIndex::open(const std::filesystem::path& directory)
    {
        auto path = detail::findResourceFile(directory, ".nidx");
        if (!path)
            return std::unexpected(path.error());

        if (!*path)
            return std::unexpected(std::format("No .nidx file in '{}'", directory.string()));

        auto file = MappedFile::open(**path);
        if (!file)
            return std::unexpected(file.error());

        return read(std::move(*file));
    }

    Result<NamedResourceStoreIndex::Layout> NamedResourceStoreIndex::readLayout(detail::BinaryView data)
    {
        auto header = data.slice(0, 8);
        if (!header)
            return std::unexpected("Named resource index too small for header");

        if (*header->readLittle<uint32_t>(0) != 0)
            return std::unexpected("Invalid named resource index header");

        const auto count = *header->readLittle<uint32_t>(4);
        auto records = data.table(8, count, 16);
        if (!records)
            return std::unexpected(records.error());

        const size_t stringsOffset = 8 + records->bytes().size();
        return Layout{.records = *records, .strings = *data.tail(stringsOffset), .stringsOffset = stringsOffset};
    }

    Result<NamedResourceStoreIndexRecord> NamedResourceStoreIndex::readRecord(detail::BinaryView data)
    {
        const auto format = *data.readLittle<uint16_t>(0);
        if (format > 1)
            return std::unexpected(std::format("Unsupported named resource compression format: {}", format));

        return NamedResourceStoreIndexRecord{
            .format = format,
            .fileSequence = *data.readLittle<uint16_t>(2),
            .idStringOffset = *data.readLittle<uint32_t>(4),
            .fileOffset = *data.readLittle<uint32_t>(8),
            .length = *data.readLittle<uint32_t>(12),
        };
    }

    Result<std::string_view> NamedResourceStoreIndex::readId(const Layout& layout, const uint32_t offset)
    {
        if (offset < layout.stringsOffset)
            return std::unexpected("Resource ID offset precedes the string table");

        const size_t relative = offset - layout.stringsOffset;
        const auto strings = layout.strings.bytes();
        if (relative > strings.size())
            return std::unexpected("Resource ID offset exceeds the string table");

        if (relative > 0 && strings[relative - 1] != 0)
            return std::unexpected("Resource ID offset is not at a string boundary");

        return layout.strings.nullTerminatedString(relative);
    }

    Result<void> NamedResourceStoreIndex::validateRecords(const Layout& layout)
    {
        const size_t count = layout.records.bytes().size() / 16;
        std::optional<std::string_view> previousId;
        for (size_t i = 0; i < count; ++i)
        {
            auto record = readRecord(*layout.records.slice(i * 16, 16));
            if (!record)
                return std::unexpected(record.error());
            auto id = readId(layout, record->idStringOffset);
            if (!id)
                return std::unexpected(id.error());
            if (previousId && *previousId >= *id)
                return std::unexpected("Named resource IDs must be unique and sorted");
            previousId = *id;
        }
        return {};
    }

    NamedResourceStoreIndex::NamedResourceStoreIndex(MappedFile file, const Layout& layout)
        : file_(std::move(file)), records_(layout.records), strings_(layout.strings),
          stringsOffset_(layout.stringsOffset)
    {
    }

    Result<NamedResourceStoreIndex> NamedResourceStoreIndex::read(MappedFile file)
    {
        auto layout = readLayout(detail::BinaryView(file.data()));
        if (!layout)
            return std::unexpected(layout.error());

        if (auto valid = validateRecords(*layout); !valid)
            return std::unexpected(valid.error());

        return NamedResourceStoreIndex(std::move(file), *layout);
    }

    std::string_view NamedResourceStoreIndex::idFor(const NamedResourceStoreIndexRecord& record) const
    {
        return *strings_.nullTerminatedString(record.idStringOffset - stringsOffset_);
    }

    Result<size_t> NamedResourceStoreIndex::indexOf(std::string_view id) const
    {
        const auto positions = std::views::iota(size_t{0}, size());
        const auto it = std::ranges::lower_bound(positions, id, {}, [this](const size_t i) { return *idAt(i); });
        if (it == positions.end() || *idAt(*it) != id)
            return std::unexpected(std::format("Resource not found: {}", id));
        return *it;
    }

    Result<NamedResourceStoreIndexRecord> NamedResourceStoreIndex::findById(std::string_view id) const
    {
        return indexOf(id).and_then([this](const size_t index)
        {
            return readRecord(*records_.slice(index * 16, 16));
        });
    }

    Result<std::pair<std::string_view, NamedResourceStoreIndexRecord>> NamedResourceStoreIndex::itemAt(
        size_t index) const
    {
        if (index >= size())
            return std::unexpected(std::format("Index {} out of range (size: {})", index, size()));

        const auto record = *readRecord(*records_.slice(index * 16, 16));
        return std::pair{idFor(record), record};
    }

    Result<std::string_view> NamedResourceStoreIndex::idAt(size_t index) const
    {
        return itemAt(index).transform([](const auto& item) { return item.first; });
    }
}
