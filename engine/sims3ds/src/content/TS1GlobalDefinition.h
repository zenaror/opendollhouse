/*
 * This Source Code Form is subject to the terms of the Mozilla Public License,
 * v. 2.0. If a copy of the MPL was not distributed with this file, You can
 * obtain one at http://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "files/formats/iff/chunks/OBJD.h"
#include <cstdint>
#include <optional>

namespace FSO { namespace Content {

// DVD Sims.exe 0x442bdf considers nonzero-Global OBJD types 7 and 12 for its
// global-definition bootstrap. Keep this separate from the raw script-visible
// OBJD.Global field and from any archive enumeration/order policy.
inline constexpr bool IsTS1GlobalDefinition(
    FSO::Files::Formats::IFF::Chunks::OBJDType objectType,
    std::optional<uint16_t> global)
{
    using FSO::Files::Formats::IFF::Chunks::OBJDType;
    if (!global || *global == 0) return false;
    return objectType == OBJDType::SimType || objectType == OBJDType::GiftToken;
}

inline bool IsTS1GlobalDefinition(const FSO::Files::Formats::IFF::Chunks::OBJD& obj)
{
    return IsTS1GlobalDefinition(obj.ObjectType, obj.Global);
}

}} // namespace FSO::Content
