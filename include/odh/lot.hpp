#pragma once

#include "odh/formats.hpp"

namespace odh {

struct LotBudgetAmounts {
    std::int32_t misc_income, job_income;
    std::int32_t service_expense, food_expense, bills_expense;
    std::int32_t misc_expense, household_expense, architecture_expense;
};
struct LotBudgetDay {
    std::int32_t valid;
    // A zero Valid word serializes no amounts. Absence is not eight zeroes.
    std::optional<LotBudgetAmounts> amounts;
};
struct Simi {
    std::uint32_t reserved, version;
    // Exactly the serialized 32 or 64 signed words, without runtime defaults.
    std::vector<std::int16_t> global_data;
    std::int16_t unknown1;
    std::int32_t unknown2, unknown3, guid1, guid2, unknown4;
    std::int32_t lot_value, objects_value, architecture_value;
    std::array<LotBudgetDay, 6> budget_days;
    std::uint64_t remaining_bytes;
};
// IMIS signature; explicit native-observed versions 33,38,3e,3f,45,48,49 (hex).
// Structural decoding only. At most 64 globals and six budget records allocate;
// unknown trailing bytes (notably version 33) retain their extent. No purchase
// valuation arithmetic, simulated clock, or other runtime globals are inferred.
Simi read_simi(const View&);

} // namespace odh
