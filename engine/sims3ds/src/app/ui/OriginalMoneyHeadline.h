// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "app/Canvas.h"
#include "app/front/FrontStrings.h"

namespace simapp { namespace original {

struct MoneyHeadline {
    std::string text;
    std::uint32_t color;
};

// TS1 object scripts encode three kinds of overhead text in one signed value.
// Only the script operand uses these ranges. A large ordinary building debit
// must still display its full cost.
inline MoneyHeadline FormatMoneyHeadline(std::int32_t value, bool fromSim, int alpha) {
    if (fromSim && value < -20000)
        return {std::to_string(-20000 - value), RGBA(255, 0, 255, alpha)};
    if (fromSim && value < -10000)
        return {std::to_string(-10000 - value), RGBA(255, 203, 23, alpha)};
    auto text = front::Simoleons(value);
    if (!fromSim && value > 0) text.insert(0, "+");
    return {text, value < 0 ? RGBA(255, 80, 70, alpha) : RGBA(90, 255, 90, alpha)};
}

}}
