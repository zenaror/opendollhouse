/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace simapp { namespace original {

// The original panel requests uppercase, prefixed markers from its locale
// service. Only language selectors 1, 2 and 19 use the 12-hour branch.
// Other selectors preserve the simulation's 0..23 hour without a marker;
// translated UIText AM/PM labels do not determine this display policy.
inline std::string FormatClock(unsigned hour, unsigned minute, std::uint8_t language) {
    char text[32];
    if (language == 1 || language == 2 || language == 19) {
        unsigned display = hour % 12;
        if (!display) display = 12;
        std::snprintf(text, sizeof text, "%s %2u:%.2u", hour < 12 ? "AM" : "PM", display, minute);
    } else {
        std::snprintf(text, sizeof text, "%2u:%.2u", hour, minute);
    }
    return text;
}

}}
