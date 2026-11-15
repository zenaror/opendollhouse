// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/VM.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/entities/VMEntity.h"

namespace FSO { namespace SimAntics { namespace Engine {
inline VMEntity* PersistedActionIcon(VM& vm, const VMQueuedAction& action) {
    auto icon = action.IconOwner();
    // The VM retains retired entities until shutdown, but can reuse their IDs.
    // A missing portrait uses the normal null -> callee fallback on restore.
    if (!icon || icon->Dead || icon->GhostImage || vm.GetObjectById(icon->ObjectID) != icon) return nullptr;
    return icon;
}
}}}
