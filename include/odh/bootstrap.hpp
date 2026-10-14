#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace odh {
// Explicit FreeSO TS1 Init/IFF-activation profile, not expansion detection or
// saved VMMarshal restoration. Only three runtime globals are provided; a
// separate lot/clock/entity backend must supply other global indices.
class Ts1BootstrapGlobals {
public:
    static constexpr std::array<std::int16_t,3> indices{17,20,25};
    static constexpr std::array<std::int16_t,3> initial{4,255,4};
    std::optional<std::int16_t> read(std::int16_t index) const;
    bool write(std::int16_t index,std::int16_t value);
private:
    std::array<std::int16_t,3> values_=initial;
};
}
