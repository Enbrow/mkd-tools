//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#include "bindings.hpp"
#include <MKD/output/export.hpp>
#include <MKD/dictionary/dictionary.hpp>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/variant.h>
#include <exception>
#include <mutex>

namespace MKD::Python
{
    using ProgressCallback = nb::typed<nb::callable, void(ExportEvent)>;

    class ExportProgress
    {
    public:
        explicit ExportProgress(std::optional<ProgressCallback> callback) : callback_(std::move(callback))
        {
        }

        void report(const ExportEvent& event)
        {
            std::lock_guard lock(mutex_);
            if (!callback_ || failure_)
                return;

            nb::gil_scoped_acquire acquire;
            try
            {
                std::visit([&](const auto& value) { (*callback_)(nb::cast(value, nb::rv_policy::copy)); }, event);
            }
            catch (...)
            {
                // Workers finish before the original Python exception is rethrown on the calling thread.
                failure_ = std::current_exception();
            }
        }

        ExportCallback callback()
        {
            if (!callback_)
                return {};

            return [this](const ExportEvent& event) { report(event); };
        }

        void rethrow() const
        {
            if (failure_) std::rethrow_exception(failure_);
        }

    private:
        std::optional<ProgressCallback> callback_;
        std::mutex mutex_;
        std::exception_ptr failure_;
    };

    template <bool Tar>
    ExportResult exportDictionary(const Dictionary& dictionary, const std::filesystem::path& destination,
                                  ExportOptions options, std::optional<ProgressCallback> callback)
    {
        ExportProgress progress(std::move(callback));
        Result<ExportResult> result;
        {
            nb::gil_scoped_release release;
            if constexpr (Tar)
                result = exportToTar(dictionary, destination, options, progress.callback());
            else
                result = exportToDirectory(dictionary, destination, options, progress.callback());
        }
        progress.rethrow();
        return unwrap(std::move(result));
    }

    void bindExportEnums(nb::module_& module)
    {
        nb::enum_<ResourceType>(module, "ResourceType")
            .value("AUDIO", ResourceType::Audio)
            .value("AUDIO_LEGACY", ResourceType::AudioLegacy)
            .value("CONTENTS", ResourceType::Contents)
            .value("APPENDIX_ENTRIES", ResourceType::AppendixEntries)
            .value("GRAPHICS", ResourceType::Graphics)
            .value("FONTS", ResourceType::Fonts)
            .value("KEYSTORES", ResourceType::Keystores)
            .value("HEADLINES", ResourceType::Headlines);
        nb::enum_<KeystoreExportMode>(module, "KeystoreExportMode")
            .value("FORWARD", KeystoreExportMode::Forward)
            .value("INVERSE", KeystoreExportMode::Inverse)
            .value("BOTH", KeystoreExportMode::Both);
    }

    void bindExportOptions(nb::module_& module)
    {
        auto type = nb::class_<ExportOptions>(module, "ExportOptions");
        type.def("__init__", [](ExportOptions* self, std::vector<ResourceType> resources, bool overwrite,
                                bool subdirectories, bool pretty, KeystoreExportMode keys, size_t concurrency)
                 {
                     new(self) ExportOptions{
                         std::move(resources), overwrite, subdirectories, pretty, keys, concurrency
                     };
                 }, nb::kw_only(), "resources"_a = std::vector<ResourceType>{}, "overwrite_existing"_a = false,
                 "create_subdirectories"_a = true, "pretty_print_xml"_a = false,
                 "keystore_export_mode"_a = KeystoreExportMode::Inverse, "concurrency"_a = 0);
        type.def_rw("resources", &ExportOptions::resources);
        type.def_rw("overwrite_existing", &ExportOptions::overwriteExisting);
        type.def_rw("create_subdirectories", &ExportOptions::createSubdirectories);
        type.def_rw("pretty_print_xml", &ExportOptions::prettyPrintXml);
        type.def_rw("keystore_export_mode", &ExportOptions::keystoreExportMode);
        type.def_rw("concurrency", &ExportOptions::concurrency);
    }

    void bindExportResult(nb::module_& module)
    {
        auto type = nb::class_<ExportResult>(module, "ExportResult");
        type.def_ro("total_files", &ExportResult::totalFiles);
        type.def_ro("exported", &ExportResult::exported);
        type.def_ro("skipped", &ExportResult::skipped);
        type.def_ro("failed", &ExportResult::failed);
        type.def_ro("total_bytes", &ExportResult::totalBytes);
        type.def_ro("errors", &ExportResult::errors);
        type.def_prop_ro("is_success", &ExportResult::isSuccess);
        type.def_prop_ro("has_errors", &ExportResult::hasErrors);
    }

    void bindExportEvents(nb::module_& module)
    {
        nb::class_<ExportBeginEvent>(module, "ExportBeginEvent")
            .def_ro("total_items", &ExportBeginEvent::totalItems);
        nb::class_<PhaseBeginEvent>(module, "PhaseBeginEvent")
            .def_ro("type", &PhaseBeginEvent::type)
            .def_ro("total_items", &PhaseBeginEvent::totalItems);
        nb::class_<ProgressEvent>(module, "ProgressEvent")
            .def_ro("type", &ProgressEvent::type)
            .def_ro("completed_items", &ProgressEvent::completedItems)
            .def_ro("total_items", &ProgressEvent::totalItems)
            .def_ro("bytes_written", &ProgressEvent::bytesWritten);
        nb::class_<PhaseEndEvent>(module, "PhaseEndEvent")
            .def_ro("type", &PhaseEndEvent::type)
            .def_ro("result", &PhaseEndEvent::result);
    }

    void bindExport(nb::module_& module)
    {
        bindExportEnums(module);
        bindExportOptions(module);
        bindExportResult(module);
        bindExportEvents(module);
        module.def("export_to_directory", &exportDictionary<false>, "dictionary"_a, "destination"_a,
                   nb::kw_only(), "options"_a = ExportOptions{}, "progress"_a = nb::none(),
                   "Export selected resources, releasing the GIL during C++ export work. "
                   "File failures are returned in the result. Progress events are owned copies. "
                   "Callbacks may run on workers. A callback exception is raised after export finishes.");
        module.def("export_to_tar", &exportDictionary<true>, "dictionary"_a, "destination"_a,
                   nb::kw_only(), "options"_a = ExportOptions{}, "progress"_a = nb::none(),
                   "Export one dictionary to an uncompressed tar archive. Any export failure raises Error. "
                   "Progress counts members before publication. Existing archives require overwrite_existing. "
                   "Callbacks and GIL handling follow export_to_directory.");
    }
}
