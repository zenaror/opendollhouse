#pragma once
#include "odh/animation.hpp"

namespace odh {
// Read one explicit catalog price position from a decoded STR# table. Uses
// TS1 language-set selection: only an absent requested group falls back to US;
// a missing index/empty value in a present group is an error. Codes0/1 are US.
// Selected text is at most64 ASCII bytes, with optional surrounding ASCII
// whitespace and one sign followed by decimal digits. No locale/currency rules.
// Throws FormatError on missing, undecoded, malformed or out-of-int32 data.
// This neither chooses resource IDs nor builds a complete price provider.
std::int32_t architecture_price_from_strings(const StringTable&,std::size_t index,
    std::uint8_t language=1);
} // namespace odh
