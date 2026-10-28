//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#include "encoders.hpp"
#include "tsv_writer.hpp"

#include <format>
#include <pugixml.hpp>

namespace MKD::detail
{
    Result<void> writePrettyXml(std::span<const uint8_t> bytes, std::ostream& output)
    {
        pugi::xml_document document;
        if (const auto parsed = document.load_buffer(bytes.data(), bytes.size()); !parsed)
            return std::unexpected(std::format("Invalid XML at byte {}: {}", parsed.offset, parsed.description()));

        document.save(output, "  ", pugi::format_default, pugi::encoding_utf8);
        if (!output)
            return std::unexpected("Failed to write XML output");

        return {};
    }

    Result<void> writeHeadlines(const HeadlineStore& store, std::ostream& output)
    {
        TsvWriter table(output);
        for (auto entry : store.entries())
        {
            if (!entry)
                return std::unexpected(entry.error());

            table.field(entry->entryId);
            table.field(entry->headline);
            table.field(entry->prefix);
            table.field(entry->suffix);
            if (auto row = table.endRow(); !row)
                return row;
        }
        return table.finish();
    }

    Result<void> writeAppendixEntries(const AppendixEntryList& list,
                                      std::span<const HeadlineStore> headlines, std::ostream& output)
    {
        TsvWriter table(output);
        for (const auto heading : {"entry_id", "label", "first_character", "headline"})
            table.field(heading);

        if (auto row = table.endRow(); !row)
            return row;

        for (size_t i = 0; i < list.size(); ++i)
        {
            auto entry = list.entryAt(i);
            if (!entry)
                return std::unexpected(entry.error());

            table.field(entry->entryId);
            table.field(entry->label.value_or(std::string_view{}));
            const char16_t character = entry->firstCharacter.value_or(u'\0');
            table.field(character ? std::u16string_view(&character, 1) : std::u16string_view{});
            bool found = false;
            for (const auto& store : headlines)
            {
                auto headline = store.componentsForEntryId(entry->entryId);
                if (!headline)
                    continue;

                table.field(headline->full());
                found = true;
                break;
            }

            if (!found)
                table.field(std::string_view{});
            if (auto row = table.endRow(); !row)
                return row;
        }
        return table.finish();
    }
}
