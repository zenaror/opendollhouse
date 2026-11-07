// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/architecture_object_state.hpp"
#include <array>
#include <functional>
#include <string>

namespace odh_host {
class SessionRoomLighting;
// Exact native4b523c/4b53b3 cell operations. Native byte axis order is Y,X.
// These kernels alone do not establish a live native directional-light cache.
struct NativeLightOffsetCell {
    std::array<std::int8_t,4> offsets{{127,127,127,127}};
    unsigned count()const noexcept;
    void insert(std::int8_t delta_y,std::int8_t delta_x)noexcept;
    void remove(std::int32_t delta_y,std::int32_t delta_x)noexcept;
    std::array<std::optional<std::array<int,2>>,2> sources(int target_x,int target_y)const noexcept;
};
enum class NativeLightRemovalProfile { unknown, basic_room_color, native_directional };
struct BasicLightRemovalReceipt {
    odh::WorldEntityReference owner;
    odh::EntityPosition old_pose;
    std::uint16_t room=0;
    bool already_queued=false;
};
// Scoped adapter for the CURRENT renderer's proven FreeSO basic room colors.
// It never claims native nearest-two directional lighting. Complete current
// source ownership and actual precomputed lighting must already exist.
class BasicLightRemovalOwner {
public:
    using Ref=odh::WorldEntityReference;
    using State=std::function<odh::ArchitectureObjectState*()>;
    using Rooms=std::function<const odh::GeometricRoomMap*()>;
    using Registry=std::function<const odh::ObjectPositionRegistrySnapshot*()>;
    BasicLightRemovalOwner(odh::World&,SessionRoomLighting&,State,Rooms,Registry,std::function<bool()> authority);
    bool supports(Ref,NativeLightRemovalProfile,std::string& error)const;
    // Call BEFORE physical unregister/OOW. After mutation, the actual owner
    // must run SessionRoomLighting::process before presenting the next frame.
    std::optional<BasicLightRemovalReceipt> remove_before_oow(
        Ref,const odh::EntityPosition&,NativeLightRemovalProfile,std::string& error);
private:
    struct Evidence {odh::EntityPosition pose;std::uint16_t room;bool queued;};
    std::optional<Evidence> inspect(Ref,NativeLightRemovalProfile,std::string&)const;
    odh::World& world_;SessionRoomLighting& controller_;State state_;Rooms rooms_;Registry registry_;
    std::function<bool()> authority_;
};
}
