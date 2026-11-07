// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ts1_curve.hpp"
#include "odh/interaction_label.hpp"
#include "odh/world.hpp"
#include <map>
#include <memory>
namespace odh {
struct AutonomyIdentity {std::int16_t id=0;std::uint64_t generation=0;};
enum class AutonomyStatus {ready,unsupported};
template<class T>struct AutonomyResult {
    AutonomyStatus status=AutonomyStatus::unsupported;
    T value{};
    std::string diagnostic;
};
template<>struct AutonomyResult<std::optional<std::string>> {
    AutonomyStatus status=AutonomyStatus::unsupported;
    std::optional<std::string> value;
    std::string diagnostic;
    std::optional<InteractionLabelOrigin> origin={};
};
struct AutonomyObject {
    AutonomyIdentity leader;
    // Null table is proven absent; legacy/undecoded table is unsupported.
    std::shared_ptr<const Ttab> table;
    bool strings_present=false;
    std::optional<std::uint32_t> disabled;
    std::optional<bool> outside;
};
struct AutonomyVariant {
    std::optional<std::string> name;
    std::int16_t param0=0;
    // Null means unmodified ads; present dictionary resets absent keys to0.
    std::optional<std::map<std::uint32_t,std::int16_t>> motive_ad_changes;
    std::optional<InteractionLabelOrigin> origin={};
};
struct AutonomyCheck {
    bool allowed=false;
    // Exact CheckTS1Action result. A check-less allowed action can be empty;
    // source then requires a TTAs table. EvaluateCheck normally adds one item.
    std::vector<AutonomyVariant> variants;
};
struct AutonomyChoice {
    AutonomyIdentity callee;
    std::uint8_t id=0; // source byte truncation of TTAB TTAIndex
    std::int16_t param0=0;
    float score=0;
    std::optional<std::string> name;
    std::optional<InteractionLabelOrigin> origin={};
};
struct AutonomyLimits {
    std::size_t entities=4096,interactions=16384,variants=65536;
};
struct FindBestActionRequest {
    World* world=nullptr;
    Vm* source_vm=nullptr;
    const VmFrame* source_frame=nullptr;
    const Instruction* source_instruction=nullptr;
    VmDispatch source_dispatch{};
    // Called first. Source FirstOrDefault(priority>currentPriority), including
    // existing queued actions. Null is a proven no-match, not unavailable state.
    std::function<AutonomyResult<std::optional<AutonomyIdentity>>(std::int16_t)> first_higher_priority;
    std::function<AutonomyResult<std::vector<AutonomyIdentity>>()> ordered_with_autonomy;
    std::function<AutonomyResult<AutonomyObject>(AutonomyIdentity)> resolve_object;
    // Auto=true, four args{1,0,0,0}, live source check effects/Temps retained.
    std::function<AutonomyResult<AutonomyCheck>(AutonomyIdentity,const Interaction&)> check_action;
    // Distinct avatar users across active queue blocks, grouped as GetUsers.
    std::function<AutonomyResult<std::int16_t>(AutonomyIdentity)> use_count;
    // Re-resolve GetAction(choice.id), args{param0,0,0,0}, priority2 and original
    // flags/permissions. False is source GetAction missing; unknown is unsupported.
    std::function<AutonomyResult<bool>(const AutonomyChoice&)> enqueue;
    std::function<AutonomyResult<std::uint64_t>(std::uint64_t)> next_random;
    // Adult/child curves read lazily AFTER the queue shortcut (false=adult).
    std::function<AutonomyResult<std::shared_ptr<const Ts1InteractionCurves>>(bool)> interaction_curves;
    // Source proof/inventory identity validation, not a spatial revision lock:
    // check effects can move entities and alter motives as in the reference.
    std::function<bool()> identities_still_match;
    AutonomyLimits limits{};
};
// TS1 FindBestAction3/65; operands have no fields. No persisted selection state:
// successful choice is represented by the existing action queue and StackObject.
PrimitiveOutcome find_best_action(const FindBestActionRequest&);
}
