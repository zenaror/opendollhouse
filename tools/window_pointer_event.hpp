// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
namespace odh_host {
enum class WindowPointerKind { press, motion, release };
struct WindowPointerEvent {
    WindowPointerKind kind=WindowPointerKind::motion;
    std::int32_t x=0,y=0;
    bool shift=false,control=false;
};
}
