// SPDX-License-Identifier: MPL-2.0
#include "app/ui/OriginalMoneyHeadline.h"
#include <cstdint>
#include <limits>
#include <iostream>

int main() {
    using simapp::original::FormatMoneyHeadline;
    using simapp::front::Simoleons;
    const int alpha = 137;
    struct Case { int raw; bool fromSim; std::string text; };
    const Case cases[] = {
        {15, true, Simoleons(15)},  // Fridge shows its price separately from the debit.
        {-15, true, Simoleons(-15)}, {-10000, true, Simoleons(-10000)},
        {-10001, true, "1"}, {-20000, true, "10000"},
        {-20035, true, "35"}, {std::numeric_limits<std::int32_t>::min(), true, "2147463648"},
        {-20035, false, Simoleons(-20035)}, {15, false, "+" + Simoleons(15)}
    };
    for (const auto& sample : cases) {
        if (FormatMoneyHeadline(sample.raw, sample.fromSim, alpha).text != sample.text) {
            std::cerr << "Unexpected overhead text for " << sample.raw << '\n';
            return 1;
        }
    }
    if (FormatMoneyHeadline(-20035, true, alpha).color != simapp::RGBA(255, 0, 255, alpha) ||
        FormatMoneyHeadline(-10035, true, alpha).color != simapp::RGBA(255, 203, 23, alpha)) return 1;
    return 0;
}
