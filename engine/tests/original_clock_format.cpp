/* SPDX-License-Identifier: MPL-2.0 */
#include "app/ui/OriginalClock.h"
#include <iostream>

int main() {
    struct Case { unsigned hour, minute; std::uint8_t language; const char* expected; };
    // Expected strings follow the original locale service's panel call, not
    // translated labels or the host operating system's time preferences.
    const Case cases[] = {
        {0, 0, 14, " 0:00"}, {6, 52, 14, " 6:52"},
        {12, 0, 14, "12:00"}, {13, 5, 14, "13:05"}, {23, 59, 14, "23:59"},
        {0, 5, 1, "AM 12:05"}, {12, 0, 1, "PM 12:00"}, {13, 5, 1, "PM  1:05"},
        {6, 52, 2, "AM  6:52"}, {23, 59, 19, "PM 11:59"},
        {13, 5, 3, "13:05"}, {13, 5, 15, "13:05"}, {13, 5, 20, "13:05"}
    };
    for (const auto& c : cases) {
        const auto actual = simapp::original::FormatClock(c.hour, c.minute, c.language);
        if (actual != c.expected) {
            std::cerr << "clock language " << unsigned(c.language) << ": [" << actual
                      << "] instead of [" << c.expected << "]\n";
            return 1;
        }
    }
    return 0;
}
