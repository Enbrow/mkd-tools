//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#include "MKD/dictionary/product_writer.hpp"
#include "writer_handles.hpp"
#include "product_build.hpp"

namespace MKD
{
    struct ProductWriter::Impl
    {
        std::shared_ptr<detail::ProductBuild> product;

        explicit Impl(std::shared_ptr<detail::ProductBuild> product) : product(std::move(product))
        {
        }
    };

    ProductWriter::ProductWriter(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
    {
    }

    ProductWriter::~ProductWriter() = default;
    ProductWriter::ProductWriter(ProductWriter&&) noexcept = default;
    ProductWriter& ProductWriter::operator=(ProductWriter&&) noexcept = default;

    Result<ProductWriter> ProductWriter::create(const std::filesystem::path& destination, DictionaryMetadata metadata,
                                                const CreationProfile profile)
    {
        return detail::ProductBuild::create(destination, std::move(metadata), profile).transform([](auto&& product)
        {
            return ProductWriter(std::make_unique<Impl>(std::move(product)));
        });
    }

    Result<DictionaryWriter> ProductWriter::addDictionary(DictionaryContent content, DictionaryWriteOptions options,
                                                          SourceContext context) const
    {
        return impl_->product->addDictionary(std::move(content), options, context).transform([](auto&& dictionary)
        {
            return DictionaryWriter(std::make_unique<DictionaryWriter::Impl>(std::move(dictionary)));
        });
    }

    Result<void> ProductWriter::addAsset(std::string_view path, std::span<const uint8_t> bytes,
                                         SourceContext context) const
    {
        return impl_->product->writeAsset(path, bytes, context);
    }

    Result<void> ProductWriter::setAppendices(AppendixMenu menu, SourceContext context) const
    {
        return impl_->product->setAppendices(std::move(menu), context);
    }

    Result<void> ProductWriter::setNavigation(NavigationIndex index, SourceContext context) const
    {
        return impl_->product->setNavigation(std::move(index), context);
    }

    Result<void> ProductWriter::setInformation(ProductInformation information, SourceContext context) const
    {
        return impl_->product->setInformation(std::move(information), context);
    }

    Result<void> ProductWriter::setIcon(std::span<const uint8_t> image, SourceContext context) const
    {
        return impl_->product->setIcon(image, context);
    }

    Result<ValidationReport> ProductWriter::preflight(ValidationOptions options) const
    {
        return impl_->product->preflight(options);
    }

    Result<void> ProductWriter::finish() const
    {
        return impl_->product->finish();
    }
}
