#pragma once

#include "odh/vm.hpp"

#include <functional>
#include <optional>

namespace odh {

using Ts1FamilyBudgetReader = std::function<std::optional<std::int32_t>(const VmFrame&)>;
using Ts1FamilyBudgetWriter = std::function<bool(const VmFrame&, std::int32_t)>;

// TS1 opcode 25 (Budget). The host supplies the active family's value and a
// writer that persists it. Missing family state follows the reference's zero
// default. Unsupported operand scopes or unavailable writes fail closed.
VmPrimitive ts1_budget_primitive(const VmMemory&, Ts1FamilyBudgetReader,
                                 Ts1FamilyBudgetWriter = {});

} // namespace odh
