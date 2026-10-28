//
// kiwakiwaaにより 2026/01/15 に作成されました。
//


#include "MKD/dictionary/dictionary.hpp"
#include "unicode/unicode.hpp"
#include "dictionary/package/resource_resolver.hpp"

#include <utility>

namespace MKD
{
    Dictionary::Dictionary(DictionaryContent content, DictionaryResources resources,
                           DictionarySearchConfiguration searchConfiguration)
        : directory_(std::move(resources.directory)),
          content_(std::move(content)),
          searchConfiguration_(std::move(searchConfiguration)),
          entries_(std::move(resources.entries)),
          entryOrder_(
              resources.entryOrder.value_or(EntryOrder(entries_ ? static_cast<uint32_t>(entries_->size()) : 0))),
          appendixEntryLists_(std::move(resources.appendixEntryLists)),
          graphics_(std::move(resources.graphics)),
          audio_(std::move(resources.audio)),
          stylesheet_(std::move(resources.stylesheet)),
          nightmodeStylesheet_(std::move(resources.nightmodeStylesheet)),
          fonts_(std::move(resources.fonts)),
          keystores_(std::move(resources.keystores)),
          headlines_(std::move(resources.headlines))
    {
    }


    const DictionaryPresentation& Dictionary::presentation() const noexcept
    {
        return content_.presentation;
    }

    Result<RetainedSpan> Dictionary::resource(std::string_view path) const
    {
        return detail::readDictionaryResource(directory_, graphics(), audio(), path);
    }

    const std::string& Dictionary::id() const noexcept
    {
        return content_.identifier;
    }


    const DictionaryContent& Dictionary::content() const noexcept
    {
        return content_;
    }

    const DictionarySearchConfiguration& Dictionary::searchConfiguration() const noexcept
    {
        return searchConfiguration_;
    }


    ResourceStore* Dictionary::entries() noexcept
    {
        return entries_ ? &*entries_ : nullptr;
    }


    const ResourceStore* Dictionary::entries() const noexcept
    {
        return entries_ ? &*entries_ : nullptr;
    }


    const EntryOrder& Dictionary::entryOrder() const noexcept
    {
        return entryOrder_;
    }


    bool Dictionary::hasEntries() const noexcept
    {
        return entries_.has_value();
    }


    Result<ResourceStoreItem> Dictionary::entryByIndex(const size_t index) const
    {
        if (!entries_)
            return std::unexpected("Dictionary has no entry resource store");

        return entries_->itemAt(index);
    }


    Result<ResourceStoreItem> Dictionary::entryById(const uint32_t itemId) const
    {
        if (!entries_)
            return std::unexpected("Dictionary has no entry resource store");

        return entries_->get(itemId).transform([itemId](RetainedSpan&& data)
        {
            return ResourceStoreItem{.itemId = itemId, .data = std::move(data)};
        });
    }


    Result<std::string> Dictionary::entryUtf8ByIndex(const size_t index) const
    {
        return entryByIndex(index).and_then([](const ResourceStoreItem& entry)
        {
            return entry.data.utf8().transform([](std::string_view text) { return std::string(text); });
        });
    }


    Result<std::string> Dictionary::entryUtf8ById(const uint32_t itemId) const
    {
        return entryById(itemId).and_then([](const ResourceStoreItem& entry)
        {
            return entry.data.utf8().transform([](std::string_view text) { return std::string(text); });
        });
    }


    const std::vector<AppendixEntryList>& Dictionary::appendixEntryLists() const noexcept
    {
        return appendixEntryLists_;
    }


    NamedResourceStore* Dictionary::graphics() noexcept
    {
        if (!graphics_ || graphics_->empty())
            return nullptr;

        return &*graphics_;
    }


    const NamedResourceStore* Dictionary::graphics() const noexcept
    {
        if (!graphics_ || graphics_->empty())
            return nullptr;

        return &*graphics_;
    }

    Stylesheet* Dictionary::stylesheet() noexcept
    {
        return stylesheet_ ? &*stylesheet_ : nullptr;
    }


    const Stylesheet* Dictionary::stylesheet() const noexcept
    {
        return stylesheet_ ? &*stylesheet_ : nullptr;
    }


    Stylesheet* Dictionary::nightmodeStylesheet() noexcept
    {
        return nightmodeStylesheet_ ? &*nightmodeStylesheet_ : nullptr;
    }


    const Stylesheet* Dictionary::nightmodeStylesheet() const noexcept
    {
        return nightmodeStylesheet_ ? &*nightmodeStylesheet_ : nullptr;
    }


    std::vector<Font>& Dictionary::fonts() noexcept
    {
        return fonts_;
    }


    const std::vector<Font>& Dictionary::fonts() const noexcept
    {
        return fonts_;
    }


    size_t Dictionary::resourceCount(const ResourceType type) const noexcept
    {
        switch (type)
        {
        case ResourceType::Audio:
        case ResourceType::AudioLegacy:
            return audio_ ? std::visit([](const auto& r) { return r.size(); }, *audio_) : 0;
        case ResourceType::Contents:
            return entries_ ? entries_->size() : 0;
        case ResourceType::AppendixEntries:
            {
                size_t total = 0;
                for (const auto& list : appendixEntryLists_)
                    total += list.size();
                return total;
            }
        case ResourceType::Graphics:
            return graphics_ ? graphics_->size() : 0;
        case ResourceType::Fonts:
            return fonts_.size();
        case ResourceType::Keystores:
            return keystores_.size();
        case ResourceType::Headlines:
            {
                size_t total = 0;
                for (const auto& store : headlines_)
                    total += store.size();
                return total;
            }
        }
        std::unreachable();
    }


    Result<std::string> Dictionary::headlineForEntryId(const EntryId& entryId) const
    {
        if (headlines_.empty())
            return std::unexpected("no headlines");

        for (const auto& store : headlines_)
        {
            auto components = store.findComponentsForEntryId(entryId);
            if (!components)
                return std::unexpected(components.error());
            if (*components)
                return detail::unicode::toUtf8((**components).full());
        }

        return std::unexpected("headline not found");
    }


    Result<std::string> Dictionary::sortingHeadlineForEntryId(const EntryId& entryId) const
    {
        if (headlines_.empty())
            return std::unexpected("no headlines");

        for (const auto& store : headlines_)
        {
            auto headline = store.findSortingHeadlineForEntryId(entryId);
            if (!headline)
                return std::unexpected(headline.error());
            if (*headline)
                return detail::unicode::toUtf8(**headline);
        }

        return std::unexpected("sorting headline not found");
    }


    const std::vector<Keystore>& Dictionary::keystores() const noexcept
    {
        return keystores_;
    }


    const std::variant<ResourceStore, NamedResourceStore>* Dictionary::audio() const noexcept
    {
        return audio_ ? &*audio_ : nullptr;
    }

    std::span<const HeadlineStore> Dictionary::headlines() const noexcept
    {
        return headlines_;
    }
}
