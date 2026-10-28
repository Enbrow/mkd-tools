//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#include "encoders.hpp"
#include "tsv_writer.hpp"

#include <format>

namespace MKD::detail
{
    Result<void> writeForwardKeys(const Keystore& store, std::ostream& output)
    {
        TsvWriter table(output);
        for (auto entry : store.entries(KeystoreIndex::Prefix))
        {
            if (!entry)
                return std::unexpected(entry.error());

            auto ids = store.entryIdsAt(KeystoreIndex::Prefix, entry->index);
            if (!ids)
                return std::unexpected(ids.error());

            table.field(entry->key);
            for (const auto id : *ids)
                table.field(std::format("{}-{}", id.pageId, id.itemId));

            if (auto row = table.endRow(); !row)
                return row;
        }
        return table.finish();
    }

    Result<void> writeInverseKeys(const Keystore& store, std::ostream& output)
    {
        auto ids = store.indexedEntryIds();
        if (!ids)
            return std::unexpected(ids.error());

        TsvWriter table(output);
        for (const auto id : *ids)
        {
            auto keys = store.keysForEntry(id);
            if (!keys)
                return std::unexpected(keys.error());

            table.field(id);
            for (const auto key : *keys)
                table.field(key);

            if (auto row = table.endRow(); !row)
                return row;
        }
        return table.finish();
    }
}
