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

    // Simitone's TS1 PurchaseValue: lot + objects + 70% of architecture.
    // Uses wide intermediates and rejects values outside the serialized int32 range.
    std::int32_t purchase_value() const;
};
// IMIS signature; explicit native-observed versions 33,38,3e,3f,45,48,49 (hex).
// Reads bounded resource structure; purchase_value() applies the separately
// evidenced Simitone TS1 valuation formula. At most 64 globals and six budget
// records allocate; unknown trailing bytes (notably version 33) retain extent.
Simi read_simi(const View&);

} // namespace odh
