// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/model/VMDialogText.h"
#include <optional>
#include <string>
namespace FSO { namespace SimAntics { namespace Engine { class VMStackFrame; } } }
namespace FSO { namespace Content { namespace TS1 {
// Adapt the original portable text rules to the active engine's owned resources.
std::optional<SimAntics::Model::VMDialogText> ResolveCareerDialog(
    SimAntics::Engine::VMStackFrame*, const std::string&,
    const std::vector<uint16_t>&, std::string& error);
}}}
