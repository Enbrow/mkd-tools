//
// kiwakiwaaにより 2026/02/21 に作成されました。
//

#pragma once

#include "MKD/output/event.hpp"
#include "MKD/resource/resource_type.hpp"

#include <cstddef>
#include <vector>

namespace MKD
{
    enum class KeystoreExportMode : uint8_t
    {
        Forward = 1 << 0, // key → page references
        Inverse = 1 << 1, // page reference → keys
        Both = Forward | Inverse,
    };

    struct ExportOptions
    {
        std::vector<ResourceType> resources;
        /** Replaces existing files, or the complete archive for tar export. */
        bool overwriteExisting = false;
        bool createSubdirectories = true;
        bool prettyPrintXml = false;
        KeystoreExportMode keystoreExportMode = KeystoreExportMode::Inverse;
        /** Zero selects the default worker count. One runs sequentially. */
        size_t concurrency = 0;
    };
}
