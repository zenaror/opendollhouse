#pragma once
#include <cstdint>
namespace odh {
// FreeSO TS1 gesture values; visual state, not PersonData words 22/23.
enum class HandGesture : std::uint8_t {idle=0,pointing=1,fist=2,none=3};
struct AvatarHands {HandGesture left=HandGesture::idle,right=HandGesture::idle;};
inline bool valid_hand_gesture(HandGesture value){return static_cast<std::uint8_t>(value)<=3;}
}
