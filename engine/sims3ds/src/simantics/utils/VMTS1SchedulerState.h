// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "files/formats/iff/chunks/OBJM.h"

namespace FSO { namespace SimAntics { namespace Engine { namespace Utils {
// ODHI through v8 did not maintain the ObjM manager word. Its old value may
// describe the foreign source rather than the native continuation being saved.
inline uint32_t TS1SavedSchedulerState(
    const Files::Formats::IFF::Chunks::OBJMInstance& instance,
    uint32_t classicVersion, uint32_t nativeVersion = 0) {
    constexpr uint32_t enabled = 0x0001ffffu;
    if (nativeVersion != 0 && nativeVersion < 9) return enabled;
    if (!instance.HasSchedulerStateTail || classicVersion < 0x36) return enabled;
    const auto word = static_cast<uint32_t>(instance.UnknownInt);
    if (classicVersion < 0x39) return (word & 0xffffu) | 0x00010000u;
    return word;
}
}}}}
