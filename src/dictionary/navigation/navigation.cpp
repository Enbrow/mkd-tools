//
// kiwakiwaaにより 2026/10/19 に作成されました。
//

#include "dictionary/package/paths.hpp"
#include "MKD/dictionary/navigation.hpp"
#include "MKD/resource/retained_span.hpp"
#include "MKD/resource/appendix_entry_list.hpp"
#include "dictionary/navigation/appendix_reader.hpp"
#include "dictionary/navigation/index_format.hpp"
#include "dictionary/package/property_list.hpp"
#include "dictionary/content/content_markup.hpp"
#include <algorithm>
#include <span>

namespace MKD
{
    namespace nav = detail::monokakido::navigation;

    // A view owns one backing store and describes one node within it. Descendants are opened on demand.
    struct NavigationView::Impl
    {
        std::filesystem::path contents;
        std::string path;

        struct Binary
        {
            RetainedSpan mapping;
            size_t offset = 4;
            uint32_t kind = 0;
            nav::Header header{};
        };

        struct Plist
        {
            std::shared_ptr<detail::PropertyList> document;
            pugi::xml_node node;
            bool inlineEntries = false;
        };

        using EntryList = std::shared_ptr<AppendixEntryList>;
        std::variant<Binary, Plist, EntryList> state = Binary{};

        // Track only the current branch. Separate branches may refer
        // to the same file or to different sections within it.
        std::vector<std::pair<std::string, size_t>> ancestors;

        static Result<NavigationView> open(const std::filesystem::path& contents, const NavigationReference& reference,
                                           std::vector<std::pair<std::string, size_t>> ancestors = {});
        Result<void> load(const std::filesystem::path& file, std::span<const uint32_t> sectionPath);
        Result<void> loadBinary(const std::filesystem::path& file, std::span<const uint32_t> sectionPath);
        static Result<NavigationView> resolve(std::shared_ptr<Impl> node);
        Result<NavigationView> child(size_t childOffset) const;
        Result<std::pair<detail::BinaryView, nav::Header>> section(size_t index) const;
        Result<NavigationViewTarget> file(std::string path, bool relativeToIndex,
                                          std::vector<uint32_t> sectionPath = {}) const;
        Result<NavigationItemView> binaryItem(size_t section, size_t index) const;
        Result<NavigationItemView> plistItem(size_t index) const;
        Result<NavigationViewTarget> plistTarget(detail::PropertyListDictionary item) const;
        Result<NavigationView> reference(detail::BinaryView data, const nav::Header& header) const;
        static Result<AppendixEntryLink> parseEntry(std::string_view text);
    };

    Result<NavigationView> NavigationView::Impl::open(const std::filesystem::path& contents,
                                                      const NavigationReference& reference,
                                                      std::vector<std::pair<std::string, size_t>> ancestors)
    {
        auto resolved = detail::resolveProductPath(contents, reference.path);
        if (!resolved)
            return std::unexpected(resolved.error());

        auto impl = std::make_shared<Impl>();
        std::error_code error;
        impl->contents = std::filesystem::weakly_canonical(contents, error);
        if (error)
            return std::unexpected(error.message());

        impl->path = resolved->lexically_relative(impl->contents).generic_string();
        impl->ancestors = std::move(ancestors);
        if (auto loaded = impl->load(*resolved, reference.sectionPath); !loaded)
            return std::unexpected(loaded.error());

        return resolve(std::move(impl));
    }

    Result<void> NavigationView::Impl::load(const std::filesystem::path& file, std::span<const uint32_t> sectionPath)
    {
        const auto extension = file.extension();
        if (extension == ".idx")
            return loadBinary(file, sectionPath);

        // Section paths select binary child headers.
        if (!sectionPath.empty())
            return std::unexpected("Section paths require a binary navigation index");

        if (extension == ".entries")
        {
            auto list = AppendixEntryList::open(file);
            if (!list)
                return std::unexpected(list.error());

            state = std::make_shared<AppendixEntryList>(std::move(*list));
        }
        else if (extension == ".plist")
        {
            auto document = detail::PropertyList::open(file, "dict");
            if (!document)
                return std::unexpected(document.error());

            auto plist = std::make_shared<detail::PropertyList>(std::move(*document));
            state = Plist{plist, plist->root()};
        }
        else
            return std::unexpected("Unknown navigation index format: " + path);

        return {};
    }

    Result<void> NavigationView::Impl::loadBinary(const std::filesystem::path& file,
                                                  std::span<const uint32_t> sectionPath)
    {
        auto& binary = std::get<Binary>(state);
        auto mapped = RetainedSpan::map(file);
        if (!mapped)
            return std::unexpected(mapped.error());

        binary.mapping = std::move(*mapped);
        const detail::BinaryView bytes(binary.mapping.span());
        auto indexKind = bytes.readLittle<uint32_t>(0);
        if (!indexKind)
            return std::unexpected(indexKind.error());

        // The first word identifies the presentation kind.
        binary.kind = *indexKind;

        // Validate
        for (size_t depth = 0;; ++depth)
        {
            auto data = bytes.tail(binary.offset);
            if (!data)
                return std::unexpected(data.error());

            auto parsed = nav::Header::read(*data);
            if (!parsed)
                return std::unexpected(parsed.error());

            binary.header = *parsed;
            if (depth == sectionPath.size())
                return {};

            const auto index = sectionPath[depth];
            if (binary.header.itemKind != nav::ItemKind::Section || index >= binary.header.count)
                return std::unexpected("Invalid navigation section path");

            // Each section stores offsets relative to its own header.
            // Convert the selected child to a file offset.
            binary.offset += binary.header.data + nav::headerSize * index;
        }
    }

    Result<NavigationView> NavigationView::Impl::resolve(std::shared_ptr<Impl> node)
    {
        // A file may contain several independent subtrees. Only returning to the
        // same file and header on this branch constitutes a reference cycle.
        const auto binary = std::get_if<Binary>(&node->state);
        const auto identity = std::pair{node->path, binary ? binary->offset : size_t{4}};
        if (node->ancestors.size() >= nav::maximumDepth ||
            std::ranges::find(node->ancestors, identity) != node->ancestors.end())
            return std::unexpected("Cyclic or excessively nested navigation reference");

        node->ancestors.push_back(identity);

        // Reference records are aliases rather than visible menu levels. Follow
        // the selected alias now, leaving all other branches unopened.
        if (binary && binary->header.itemKind == nav::ItemKind::Reference)
            return node->reference(*detail::BinaryView(binary->mapping.span()).tail(binary->offset), binary->header);

        return NavigationView(std::move(node));
    }

    Result<NavigationView> NavigationView::open(const std::filesystem::path& contents,
                                                const NavigationReference& reference)
    {
        return Impl::open(contents, reference);
    }

    Result<NavigationView> NavigationView::Impl::child(const size_t childOffset) const
    {
        auto& binary = std::get<Binary>(state);
        // Copy the view state. Each child keeps its own header
        // and ancestry while sharing the parent's backing storage.
        auto result = std::make_shared<Impl>(*this);
        std::get<Binary>(result->state).offset = childOffset;
        auto data = detail::BinaryView(binary.mapping.span()).tail(childOffset);
        if (!data)
            return std::unexpected(data.error());

        auto childHeader = nav::Header::read(*data);
        if (!childHeader)
            return std::unexpected(childHeader.error());

        std::get<Binary>(result->state).header = *childHeader;
        return resolve(std::move(result));
    }

    Result<std::pair<detail::BinaryView, nav::Header>> NavigationView::Impl::section(const size_t index) const
    {
        const auto& binary = std::get<Binary>(state);
        auto data = detail::BinaryView(binary.mapping.span()).tail(binary.offset);
        if (!data)
            return std::unexpected(data.error());

        // With this flag, child headers describe groups within the current
        // screen. Without it, they are selectable nodes on the next screen.
        if (binary.header.flags & nav::sections)
        {
            if (index >= binary.header.count || binary.header.itemKind != nav::ItemKind::Section)
                return std::unexpected("Navigation section is out of range");

            data = data->tail(binary.header.data + nav::headerSize * index);
            if (!data)
                return std::unexpected(data.error());

            auto sectionHeader = nav::Header::read(*data);
            if (!sectionHeader)
                return std::unexpected(sectionHeader.error());

            return std::pair{*data, *sectionHeader};
        }

        // A flat node is exposed as one unnamed section across all formats.
        if (index)
            return std::unexpected("Navigation section is out of range");

        return std::pair{*data, binary.header};
    }

    uint32_t NavigationView::kindCode() const noexcept
    {
        const auto binary = std::get_if<Impl::Binary>(&impl_->state);
        return binary ? binary->kind : 0;
    }

    size_t NavigationView::sectionCount() const noexcept
    {
        const auto binary = std::get_if<Impl::Binary>(&impl_->state);
        return binary && (binary->header.flags & nav::sections) ? binary->header.count : 1;
    }

    Result<std::string> NavigationView::sectionTitle(const size_t index) const
    {
        const auto binary = std::get_if<Impl::Binary>(&impl_->state);
        if (!binary)
        {
            if (index)
                return std::unexpected("Navigation section is out of range");
            return std::string{};
        }

        auto section = impl_->section(index);
        if (!section)
            return std::unexpected(section.error());

        // Headline-backed labels need a dictionary lookup. This string accessor
        // only returns explicit section text.
        if (!(binary->header.flags & nav::sections) || section->second.titleKind != 0)
            return std::string{};

        return nav::text(section->first, section->second.title);
    }

    Result<size_t> NavigationView::itemCount(const size_t section) const
    {
        if (std::holds_alternative<Impl::Binary>(impl_->state))
        {
            return impl_->section(section).transform([](const auto& data) -> size_t { return data.second.count; });
        }

        if (section)
            return std::unexpected("Navigation section is out of range");

        if (const auto entries = std::get_if<Impl::EntryList>(&impl_->state))
            return (*entries)->size();

        const auto& plist = std::get<Impl::Plist>(impl_->state);
        const auto array =
            plist.inlineEntries ? plist.node : detail::PropertyListDictionary(plist.node).value("children");
        return std::distance(array.children().begin(), array.children().end());
    }

    Result<NavigationViewTarget> NavigationView::Impl::file(std::string name, bool relativeToIndex,
                                                            std::vector<uint32_t> sectionPath) const
    {
        std::optional<std::string> fragment;
        if (const auto hash = name.find('#'); hash != std::string::npos)
        {
            fragment = name.substr(hash + 1);
            name.resize(hash);
        }

        // Binary indexes resolve sibling files from their own directory.
        // General grids instead store paths from the product's Contents root.
        const auto relative =
            relativeToIndex
                ? (std::filesystem::path(path).parent_path() / name).lexically_normal().generic_string()
                : name;
        auto resolved = detail::resolveProductPath(contents, relative);
        if (!resolved)
            return std::unexpected(resolved.error());

        const auto extension = resolved->extension();
        if (extension == ".html" || extension == ".xml" || extension == ".pdf")
            return NavigationViewTarget(AppendixDocument{relative, fragment});

        auto opened = open(contents, {relative, std::move(sectionPath)}, ancestors);
        if (!opened)
            return std::unexpected(opened.error());

        return NavigationViewTarget(std::move(*opened));
    }

    Result<NavigationView> NavigationView::Impl::reference(detail::BinaryView data, const nav::Header& header) const
    {
        if (!header.count)
            return std::unexpected("Empty navigation reference");

        auto name = nav::text(data, *data.readLittle<uint32_t>(header.data));
        if (!name)
            return std::unexpected(name.error());

        // Header validation has already checked the fixed-size record table.
        const auto pathOffset = *data.readLittle<uint32_t>(header.data + 4);
        std::vector<uint32_t> sectionPath;
        if (pathOffset)
        {
            auto count = data.readLittle<uint32_t>(pathOffset);
            if (!count)
                return std::unexpected(count.error());

            auto table = data.table(static_cast<size_t>(pathOffset) + 4, *count, 4);
            if (!table)
                return std::unexpected(table.error());

            for (size_t i = 0; i < *count; ++i)
                sectionPath.push_back(*table->readLittle<uint32_t>(i * 4));
        }

        auto target = file(*name, true, std::move(sectionPath));
        if (!target)
            return std::unexpected(target.error());
        if (auto* index = std::get_if<NavigationView>(&*target))
            return std::move(*index);

        return std::unexpected("Navigation reference does not name an index");
    }

    Result<NavigationItemView> NavigationView::Impl::binaryItem(size_t sectionIndex, size_t index) const
    {
        auto& binary = std::get<Binary>(state);
        auto sectionData = section(sectionIndex);
        if (!sectionData)
            return std::unexpected(sectionData.error());

        const auto [data, recordHeader] = *sectionData;
        if (index >= recordHeader.count)
            return std::unexpected("Navigation item is out of range");

        // The selected section owns the table and its relative offsets.
        const auto record = recordHeader.data + index * nav::strides[static_cast<size_t>(recordHeader.itemKind)];
        NavigationItemView result{std::string{}, std::monostate{}};
        if (recordHeader.itemKind == nav::ItemKind::Section)
        {
            auto bytes = data.tail(record);
            auto subheader = nav::Header::read(*bytes);
            if (!subheader)
                return std::unexpected(subheader.error());

            // Some indexes borrow an article headline for the label.
            // Return that reference separately from the node's destination.
            if (subheader->titleKind == 1 && subheader->title)
            {
                auto label = nav::entry(*bytes, subheader->title);
                if (!label)
                    return std::unexpected(label.error());
                result.label = *label;
            }
            else
            {
                auto label = nav::text(*bytes, subheader->title);
                if (!label)
                    return std::unexpected(label.error());
                result.label = nav::decodeLabel(std::move(*label));
            }

            // The child header starts a new offset base within the same file.
            const auto absolute = static_cast<size_t>(bytes->bytes().data() - binary.mapping.span().data());
            auto view = child(absolute);
            if (!view)
                return std::unexpected(view.error());

            result.target = std::move(*view);
        }
        else if (recordHeader.itemKind == nav::ItemKind::Entry || recordHeader.itemKind == nav::ItemKind::LabelledEntry)
        {
            auto target = nav::entry(data, record);
            if (!target)
                return std::unexpected(target.error());

            result.target = *target;
            // A display label does not replace the item's article or subentry ID.
            if (recordHeader.itemKind == nav::ItemKind::LabelledEntry)
            {
                auto label = nav::text(data, *data.readLittle<uint32_t>(record + 8));
                if (!label)
                    return std::unexpected(label.error());
                result.label = nav::decodeLabel(std::move(*label));
            }
        }
        else if (recordHeader.itemKind == nav::ItemKind::File)
        {
            auto label = nav::text(data, *data.readLittle<uint32_t>(record));
            auto name = nav::text(data, *data.readLittle<uint32_t>(record + 4));
            if (!label)
                return std::unexpected(label.error());
            if (!name)
                return std::unexpected(name.error());

            result.label = nav::decodeLabel(std::move(*label));
            auto target = file(*name, true);
            if (!target)
                return std::unexpected(target.error());

            result.target = std::move(*target);
        }
        else
            return std::unexpected("Unexpected navigation reference table");

        return result;
    }

    Result<AppendixEntryLink> NavigationView::Impl::parseEntry(std::string_view text)
    {
        // General grids accept page-item IDs with an optional decimal dictionary
        // suffix. Keep zero when the file leaves dictionary selection implicit.
        const auto last = text.rfind('-');
        uint16_t dictionaryId = 0;
        if (last != std::string_view::npos && text.find('-') != last)
        {
            uint32_t value = 0;
            const auto id = text.substr(last + 1);
            const auto parsed = std::from_chars(id.data(), id.data() + id.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size() || value > UINT16_MAX)
                return std::unexpected("Invalid navigation dictionary ID");

            dictionaryId = static_cast<uint16_t>(value);
            text = text.substr(0, last);
        }

        auto id = detail::EntryReference::parse(text);
        if (!id)
            return std::unexpected("Invalid navigation entry ID");

        return AppendixEntryLink{*id, dictionaryId};
    }

    Result<NavigationViewTarget> NavigationView::Impl::plistTarget(const detail::PropertyListDictionary item) const
    {
        if (const auto fileName = item.text("file"))
            return file(*fileName, false);

        if (const auto entry = item.text("entry"))
        {
            auto id = parseEntry(*entry);
            if (!id)
                return std::unexpected(id.error());

            return NavigationViewTarget(*id);
        }

        const auto array = item.value("entries");
        if (!array)
            return NavigationViewTarget(std::monostate{});

        if (std::string_view(array.name()) != "array")
            return std::unexpected("Invalid navigation entry array");

        // Inline arrays are another view into this document.
        auto result = std::make_shared<Impl>(*this);
        auto& plist = std::get<Plist>(result->state);
        plist.node = array;
        plist.inlineEntries = true;
        return NavigationViewTarget(NavigationView(std::move(result)));
    }

    Result<NavigationItemView> NavigationView::Impl::plistItem(const size_t index) const
    {
        const auto& plist = std::get<Plist>(state);
        if (plist.inlineEntries)
        {
            auto node = plist.node.first_child();
            for (size_t i = 0; i < index; ++i)
                node = node.next_sibling();

            auto id = parseEntry(node.child_value());
            if (!id)
                return std::unexpected(id.error());

            return NavigationItemView{std::string{}, *id};
        }

        auto node = detail::PropertyListDictionary(plist.node).value("children").first_child();
        for (size_t i = 0; i < index; ++i)
            node = node.next_sibling();

        const detail::PropertyListDictionary item(node);
        auto target = plistTarget(item);
        if (!target)
            return std::unexpected(target.error());

        return NavigationItemView{nav::decodeLabel(item.text("title").value_or("")), std::move(*target)};
    }

    Result<NavigationItemView> NavigationView::itemAt(const size_t section, const size_t index) const
    {
        auto count = itemCount(section);
        if (!count)
            return std::unexpected(count.error());

        if (index >= *count)
            return std::unexpected("Navigation item is out of range");

        if (std::holds_alternative<Impl::Binary>(impl_->state))
            return impl_->binaryItem(section, index);

        // Entry-list records borrow their labels from mapped storage.
        if (const auto entries = std::get_if<Impl::EntryList>(&impl_->state))
        {
            auto entry = (*entries)->entryAt(index);
            if (!entry)
                return std::unexpected(entry.error());

            return NavigationItemView{
                std::string(entry->label.value_or("")),
                AppendixEntryLink{entry->entryId, entry->dictionaryId}
            };
        }
        return impl_->plistItem(index);
    }
}
