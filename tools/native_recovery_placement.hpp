// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "native_floor_heads.hpp"
#include "object_trade_session_adapter.hpp"
#include <memory>

namespace odh_host {
// Binds native hierarchy kernels to the SAME candidate World, spatial registry
// and existing one-shot placement phases. No VM, resource loader or second lot.
class NativeRecoveryPlacement {
public:
    NativeRecoveryPlacement(SessionObjectPositionContext,SessionObjectPositionServices,
        std::shared_ptr<NativeFloorHeads>);
    bool unlink(odh::WorldEntityReference,std::string&);
    bool restore(odh::WorldEntityReference,std::optional<odh::EntityPosition>,
        std::optional<odh::WorldEntityReference>,std::size_t,
        std::optional<odh::EntityPosition> native_saved_pose,std::string&);
private:
    SessionObjectPositionContext context_;
    SessionObjectPositionServices services_;
    std::shared_ptr<NativeFloorHeads> floors_;
    bool current(odh::WorldEntityReference)const;
    bool phase(odh::WorldEntityReference,std::optional<odh::EntityPosition>,SessionPositionPhase,
        std::optional<std::uint8_t>,const std::function<bool()>&);
    bool physical(odh::WorldEntityReference root,odh::WorldEntityReference child,
        std::optional<odh::WorldEntityReference>,std::size_t,const std::function<bool()>&);
    bool level(odh::WorldEntityReference,std::int16_t,std::string&);
};
}
