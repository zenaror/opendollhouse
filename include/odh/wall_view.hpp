#pragma once
#include <cstdint>
namespace odh {
// Uniform view only: selected story1..2, lower stories up, higher hidden.
// down cuts the selected story; it does not infer proximity/room cutaway.
struct WallView {std::uint8_t level=2;bool down=false;};
}
