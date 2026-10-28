//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#include "MKD/dictionary/dictionary_writer.hpp"
#include "writer_handles.hpp"

namespace MKD
{
    DictionaryWriter::DictionaryWriter(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
    {
    }

    DictionaryWriter::~DictionaryWriter() = default;
    DictionaryWriter::DictionaryWriter(DictionaryWriter&&) noexcept = default;
    DictionaryWriter& DictionaryWriter::operator=(DictionaryWriter&&) noexcept = default;

    Result<EntryWriter> DictionaryWriter::createEntry(const std::optional<uint32_t> pageId, SourceContext context) const
    {
        return impl_->dictionary->reserveEntry(pageId, context).transform([&](uint32_t page)
        {
            return EntryWriter(std::make_unique<EntryWriter::Impl>(EntryWriter::Impl{
                impl_->dictionary, page, std::move(context)
            }));
        });
    }

    Result<void> DictionaryWriter::addPage(const uint32_t pageId, std::string_view xhtml, SourceContext context) const
    {
        return impl_->dictionary->addPage(pageId, xhtml, context);
    }

    Result<void> DictionaryWriter::addHeadline(HeadlineComponents headline, const HeadlineRole role,
                                               SourceContext context) const
    {
        return impl_->dictionary->addHeadline(std::move(headline), role, context);
    }

    Result<void> DictionaryWriter::addSearchKey(const SearchScope scope, std::string_view key, const EntryId target,
                                                const SearchKeyOptions options, SourceContext context) const
    {
        return impl_->dictionary->addSearchKey(scope, key, target, options, context);
    }

    Result<GaijiResource> DictionaryWriter::addGaiji(std::string_view filename, std::span<const uint8_t> bytes,
                                                     SourceContext context) const
    {
        return impl_->dictionary->addGaiji(filename, bytes, context);
    }

    Result<void> DictionaryWriter::setPresentation(DictionaryPresentation presentation, SourceContext context) const
    {
        return impl_->dictionary->setPresentation(std::move(presentation), context);
    }

    Result<void> DictionaryWriter::addGraphic(std::string_view name, std::span<const uint8_t> bytes,
                                              SourceContext context) const
    {
        return impl_->dictionary->writeMedia(name, bytes, detail::MediaKind::Graphic, context);
    }

    Result<void> DictionaryWriter::addAudio(std::string_view name, std::span<const uint8_t> bytes,
                                            SourceContext context) const
    {
        return impl_->dictionary->writeMedia(name, bytes, detail::MediaKind::Audio, context);
    }

    Result<void> DictionaryWriter::addAsset(std::string_view path, std::span<const uint8_t> bytes,
                                            SourceContext context) const
    {
        return impl_->dictionary->writeAsset(path, bytes, context);
    }

    Result<void> DictionaryWriter::addFont(std::string_view filename, std::span<const uint8_t> bytes,
                                           SourceContext context) const
    {
        return impl_->dictionary->addFont(filename, bytes, context);
    }

    Result<AppendixDocument> DictionaryWriter::addAppendix(std::string_view relative, std::string_view title,
                                                           SourceContext context) const
    {
        return impl_->dictionary->addAppendixDocument(relative, title, context);
    }

    Result<AppendixList> DictionaryWriter::addAppendix(std::string_view name, std::string_view title,
                                                       std::span<const AppendixEntry> entries,
                                                       const AppendixListWriteOptions options,
                                                       SourceContext context) const
    {
        return impl_->dictionary->addAppendixList(name, title, entries, options, context);
    }

    Result<void> DictionaryWriter::setEntryOrder(std::span<const uint32_t> pages, SourceContext context) const
    {
        return impl_->dictionary->setEntryOrder(pages, context);
    }

    Result<ValidationReport> DictionaryWriter::preflight(ValidationOptions options) const
    {
        return impl_->dictionary->preflight(options);
    }

    Result<void> DictionaryWriter::finish() const
    {
        return impl_->dictionary->finish();
    }
}
