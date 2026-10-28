//
// kiwakiwaaにより 2026/10/17 に作成されました。
//

#pragma once

#include "MKD/result.hpp"
#include "MKD/resource/appendix_entry_list.hpp"
#include "MKD/resource/headline_store.hpp"
#include "MKD/resource/keystore.hpp"

#include <ostream>
#include <span>

namespace MKD::detail
{
    Result<void> writePrettyXml(std::span<const uint8_t> bytes, std::ostream& output);
    Result<void> writeHeadlines(const HeadlineStore& store, std::ostream& output);
    Result<void> writeForwardKeys(const Keystore& store, std::ostream& output);
    Result<void> writeInverseKeys(const Keystore& store, std::ostream& output);
    Result<void> writeAppendixEntries(const AppendixEntryList& list,
                                      std::span<const HeadlineStore> headlines, std::ostream& output);
}
