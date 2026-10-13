#pragma once
#include "odh/animation.hpp"
#include "odh/vm.hpp"
#include <array>

namespace odh {
using LotZoning = std::map<std::int16_t,std::int16_t>;
// Explicit US language group, native residential/community labels only.
// Other labels and duplicate lot IDs are rejected instead of assigned a zone.
LotZoning read_lot_zoning(const StringTable&);
struct LotZoneResult {std::int16_t input,value;bool fallback;};
class LotContext {
public:
    // Preserve only serialized static globals. Clock-derived and entity/runtime
    // initialization fields remain unsupported until their backends exist.
    LotContext(std::int16_t selected_lot,const std::vector<std::int16_t>& serialized,
               std::optional<LotZoning> zoning = {});
    std::optional<std::int16_t> read(std::int16_t index) const;
    bool write(std::int16_t index,std::int16_t value);
    std::optional<LotZoneResult> zone(std::int16_t input) const;
    VmPrimitive primitive(std::function<void(const LotZoneResult&)> observed = {}) const;
private:
    std::array<std::optional<std::int16_t>,38> globals_{};
    std::optional<LotZoning> zoning_;
};
}
