#include "odh/resident_selection.hpp"
#include "odh/formats.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main() {
    require(odh::advance_resident_selection(0, -1, 3) == 2, "previous wraps to last resident");
    require(odh::advance_resident_selection(2, 1, 3) == 0, "next wraps to first resident");
    require(odh::advance_resident_selection(4, 0, 3) == 1, "current index is bounded");
    require(odh::advance_resident_selection(1, std::numeric_limits<int>::min(), 5) == 3,
            "minimum negative delta is handled without overflow");
    require(odh::resident_selection_row(10, 0, 4, 12) == 10, "first visible row maps to page start");
    require(odh::resident_selection_row(10, 1, 4, 12) == 11, "last resident on partial page is selectable");
    require(!odh::resident_selection_row(10, 2, 4, 12), "rows beyond resident count are rejected");
    require(!odh::resident_selection_row(4, 0, 0, 12), "empty page is rejected");
    bool rejected = false;
    try { (void)odh::advance_resident_selection(0, 1, 0); }
    catch (const odh::FormatError&) { rejected = true; }
    require(rejected, "empty household is rejected");
    std::cout << "resident selection checks passed\n";
}
