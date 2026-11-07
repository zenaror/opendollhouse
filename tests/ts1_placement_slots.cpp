#include "odh/ts1_placement_slots.hpp"
#include "odh/source_direction.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::uint32_t bits(float value){std::uint32_t word;std::memcpy(&word,&value,sizeof word);return word;}
odh::SlotRecord slot(){odh::SlotRecord r{};r.type=3;r.standing=1;r.snap_target_slot=-1;r.facing=-2;r.resolution=16;r.proximity=std::array<std::int32_t,3>{0,0,0};return r;}
struct Fixture{
    odh::World world;odh::Ts1SlotLocationsServices services;odh::Ts1PlacementLocations result;
    std::vector<char> order;std::size_t draws=0,checks=0,destinations=0;bool outside=false,wall=false,door=false,conflict=false,unknown=false;
    odh::Ts1PlacementStatus collision=odh::Ts1PlacementStatus::success;std::int16_t blocker=0;
    Fixture(){
        for(std::int16_t id=1;id<=4;++id)require(world.create(id,id==1||id==4?odh::EntityKind::avatar:odh::EntityKind::object,id==1||id==4?odh::EntityLayout{0,88,3,16}:odh::EntityLayout{0,0,3})&&
            world.set_position(id,{id==1?120:id==2?160:id==3?192:208,160,1,0}),"SLOT fixture entity");
        services.group=[&](auto target)->std::optional<std::vector<odh::Ts1PlacementIdentity>>{return std::vector<odh::Ts1PlacementIdentity>{target,{3,world.find(3)->generation}};};
        services.room_at=[&](const auto&)->std::optional<std::uint16_t>{order.push_back('r');if(unknown)return {};return 1;};
        services.out_of_bounds=[&](const auto&)->std::optional<bool>{order.push_back('b');return outside;};
        services.raycast_wall=[&](auto,auto,auto,auto,auto)->std::optional<bool>{order.push_back('w');return wall;};
        services.position_valid=[&](const auto& inv,auto,const auto&,auto direction,auto flags){require(inv.source_still_matches()&&direction==0&&flags==9,"SLOT PositionValid uses actual source and north/flags9");
            order.push_back('p');++checks;return odh::Ts1PlacementResult{collision,collision==odh::Ts1PlacementStatus::success?std::int16_t{-1}:std::int16_t{10},
                {blocker,blocker?world.find(blocker)->generation:0},{}};};
        services.chair_entry_point26=[](auto)->std::optional<bool>{return true;};
        services.architectural_door_at=[&](const auto&)->std::optional<bool>{order.push_back('d');return door;};
        services.destination_conflict=[&](const auto& inv,auto,const auto&)->std::optional<bool>{require(inv.source_still_matches(),"SLOT destination query uses original request");++destinations;return conflict;};
        services.random=[&](const auto& inv,auto bound)->std::optional<std::uint64_t>{require(inv.source_still_matches()&&bound==1024,"SLOT RNG is same real source");order.push_back('g');++draws;return 512;};
    }
    odh::Ts1PlacementLocations run(const odh::SlotRecord& record,odh::Ts1SlotLocationsLimits limits={}){
        odh::Vm* active=nullptr;odh::Vm vm({{odh::routine_key(4096),{0x8002,{{46,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}},world.vm_memory(),{},
            [&](const auto& f,const auto& i,const auto&,const auto& d){
                odh::Ts1PlacementInvocation inv{f,i,d,[&]{return active->matches_active_primitive(f,i,d);}};
                result=odh::ts1_find_slot_locations(world,inv,record,{2,world.find(2)->generation},{1,world.find(1)->generation},services,limits);
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            });active=&vm;vm.start(odh::routine_key(4096),{1,2,2,0});require(vm.run(1)==odh::VmStatus::returned_true,"real SLOT request completes");return result;
    }
};
void diagnostic_observation(){
    Fixture baseline,observed;auto r=slot();r.flags=255;r.proximity=std::array<std::int32_t,3>{16,16,16};
    odh::Ts1SlotDiagnostics diagnostic;unsigned reports=0;
    const auto install=[&](Fixture& fixture){fixture.services.diagnostics=[&](const auto& invocation,const auto& raw,auto target,auto caller,const auto& value){
        ++reports;require(invocation.source_still_matches()&&target.id==2&&caller.id==1&&
            target.generation&&caller.generation&&raw.type==3,"observer receives actual source identity and authored slot");diagnostic=value;};};
    install(observed);
    const auto before=baseline.run(r),after=observed.run(r);
    require(before.supported&&after.supported&&baseline.order==observed.order&&baseline.draws==observed.draws&&
        baseline.checks==observed.checks&&before.goals.size()==after.goals.size()&&reports==1,
        "diagnostics do not repeat providers, change RNG or alter phase order");
    for(std::size_t n=0;n<before.goals.size();++n)require(before.goals[n].position==after.goals[n].position&&
        before.goals[n].radians==after.goals[n].radians&&before.goals[n].score==after.goals[n].score,
        "observed goals retain exact authored geometry and scoring");
    require(diagnostic.profile_decoded&&diagnostic.effective_flags==255&&diagnostic.minimum==16&&diagnostic.maximum==16&&
        diagnostic.enumerated==8&&diagnostic.verified==8&&diagnostic.accepted==8&&diagnostic.position_valid_calls==8,
        "source circle profile and successful candidate counts are retained once");
    Fixture bounds;bounds.outside=true;install(bounds);reports=0;
    require(bounds.run(slot()).goals.empty()&&reports==1&&diagnostic.bounds_rejected==1&&!diagnostic.position_valid_calls,
        "bounds rejection precedes every later provider");
    Fixture walls;walls.wall=true;install(walls);
    require(walls.run(slot()).goals.empty()&&diagnostic.wall_rejected==1&&!diagnostic.position_valid_calls,
        "wall rejection records its existing verification stage");
    Fixture rooms;install(rooms);unsigned room_reads=0;
    rooms.services.room_at=[&](const auto&)->std::optional<std::uint16_t>{return ++room_reads==1?1:2;};
    require(rooms.run(r).goals.empty()&&diagnostic.room_rejected==8&&!diagnostic.verified&&!rooms.draws,
        "room rejection records actual candidates without introducing scoring or checks");
    Fixture direction;install(direction);auto directed=r;directed.flags=1;
    require(direction.run(directed).supported&&diagnostic.direction_rejected>0&&
        diagnostic.enumerated==diagnostic.direction_rejected+diagnostic.verified,
        "direction-mask rejection is separate from verification failures");
    Fixture collision;install(collision);collision.collision=odh::Ts1PlacementStatus::blocked;collision.blocker=3;
    require(collision.run(slot()).goals.empty()&&diagnostic.position_blocked==1&&diagnostic.object_rejected==1&&
        diagnostic.samples[0].placement_error==10&&diagnostic.samples[0].blocker.id==3&&
        diagnostic.samples[0].blocker.generation==collision.world.find(3)->generation,
        "PositionValid rejection retains source error and exact blocker generation");
    Fixture chair;install(chair);chair.collision=odh::Ts1PlacementStatus::blocked;chair.blocker=3;
    auto seated=slot();seated.standing=0;seated.sitting=1;seated.facing=-3;
    require(chair.run(seated).goals.size()==1&&diagnostic.chair_candidates==1&&!diagnostic.object_rejected,
        "usable chairs are not reported as rejected objects");
    Fixture conflict;install(conflict);conflict.conflict=true;
    require(conflict.run(slot()).goals.size()==1&&diagnostic.destination_conflicts==1&&diagnostic.accepted==1,
        "source destination conflict lowers score rather than rejecting a valid goal");
    Fixture unsupported;install(unsupported);unsupported.collision=odh::Ts1PlacementStatus::unsupported;reports=0;
    require(!unsupported.run(slot()).supported&&reports==1&&diagnostic.position_unsupported==1&&
        !diagnostic.supported&&!diagnostic.diagnostic.empty()&&diagnostic.diagnostic.size()<=256,
        "unsupported execution publishes one bounded diagnostic without manufacturing goals");
    Fixture cap;install(cap);unsigned failures=0;
    cap.services.position_valid=[&](const auto&,auto,const auto&,auto,auto){return odh::Ts1PlacementResult{
        odh::Ts1PlacementStatus::blocked,static_cast<std::int16_t>(100+failures++),{3,cap.world.find(3)->generation},{}};};
    auto many=slot();many.flags=255|(1U<<29);many.proximity=std::array<std::int32_t,3>{0,64,32};
    require(cap.run(many).supported&&diagnostic.sample_count==diagnostic.samples.size()&&diagnostic.samples_dropped>0,
        "many distinct observed failures retain only the fixed sample budget with explicit truncation");
    Fixture throwing;
    throwing.services.diagnostics=[](const auto&,const auto&,auto,auto,const auto&){throw std::runtime_error("reporting only");};
    require(throwing.run(slot()).goals.size()==1,"reporting failure cannot change the authored simulation result");
}
void exact_and_offsets(){
    Fixture direct;const auto exact=direct.run(slot());require(exact.supported&&exact.goals.size()==1&&exact.goals[0].position.x==160&&
        exact.goals[0].position.y==160&&!direct.draws&&direct.checks==1,"zero-offset exact source SLOT has one verified location without RNG");
    for(const auto facing:{-3,-2,-1,0,1,2,3,4,5,6,7}){Fixture f;auto r=slot();r.facing=facing;
        const auto result=f.run(r);require(result.supported&&result.goals.size()==1&&result.goals[0].face_anywhere==(facing==-3),"all source facing modes are preserved");}
    Fixture average;require(average.world.set_position(2,{160,160,1,2}),"rotate object east");auto r=slot();r.flags=65536;r.offset_bits={bits(16),0,bits(15)};
    const auto result=average.run(r);require(result.supported&&result.goals.size()==1&&result.goals[0].position.x==176&&
        result.goals[0].position.y==176&&result.goals[0].position.level==2,"average physical group, rotated XY and Z story offset share source geometry");
    for(unsigned direction=0;direction<8;++direction){Fixture oow;
        require(oow.world.place_out_of_world(2,static_cast<std::uint8_t>(direction)),"OOW target heading");
        auto record=slot();record.flags=4096;record.facing=0;
        // A real offset brings the OOW sentinel into valid lot coordinates;
        // rotation itself still comes from the retained OOW object direction.
        const float source_product=static_cast<float>(direction)*static_cast<float>(3.14159265358979323846/4);
        double source_angle=source_product;
        if(source_angle>3.14159265358979323846)source_angle-=3.14159265358979323846*2;
        const float theta=static_cast<float>(source_angle);
        require(bits(odh::source_game_object_radians(static_cast<std::uint8_t>(direction)))==bits(theta),
            "all object directions preserve source FLOAT multiply then DOUBLE normalization bits");
        const float c=static_cast<float>(std::cos(theta)),s=static_cast<float>(std::sin(theta));
        record.offset_bits={bits(32784.0f*c+32784.0f*s),bits(-32784.0f*s+32784.0f*c),0};
        const auto result=oow.run(record);
        double expected=theta;
        expected=std::fmod(expected,3.14159265358979323846*2);if(expected<0)expected+=3.14159265358979323846*2;
        if(expected>3.14159265358979323846)expected-=3.14159265358979323846*2;
        require(result.supported&&result.goals.size()==1&&std::abs(result.goals[0].radians-expected)<1e-5,
            "SLOT OOW GetValue Direction uses all native notches without log2 reinterpretation");}
    Fixture directional;r=slot();r.flags=4096;r.facing=3;
    const auto snapped=directional.run(r);require(snapped.supported&&snapped.goals.size()==1&&std::abs(snapped.goals[0].radians-2.3561945f)<1e-6f,
        "SnapToDirection uses facing bit and continuous angle");
}
void range_and_phase(){
    Fixture circle;auto r=slot();r.flags=255;r.proximity=std::array<std::int32_t,3>{16,16,16};
    const auto result=circle.run(r);require(result.supported&&result.goals.size()==8&&circle.draws==8&&circle.checks==8,"circle enumerates all8 source points and RNG once per candidate");
    require(circle.order.size()>5&&circle.order[0]=='r'&&circle.order[1]=='r'&&circle.order[2]=='g'&&circle.order[3]=='b'&&
        circle.order[4]=='w'&&circle.order[5]=='p',"room filter precedes RNG, then bounds/wall and effectful PositionValid");
    Fixture range;r.proximity=std::array<std::int32_t,3>{0,16,16};r.resolution=16;r.flags=255|(1U<<30)|(1U<<29);
    const auto grid=range.run(r);require(grid.supported&&!grid.goals.empty()&&range.draws==0,"square and equal-proximity profiles do not fabricate random draws");
    Fixture rejected;rejected.outside=true;r.flags=255;r.proximity=std::array<std::int32_t,3>{16,16,16};
    const auto empty=rejected.run(r);require(empty.supported&&empty.goals.empty()&&empty.no_valid_goals&&rejected.draws==8&&!rejected.checks,
        "out-of-bounds candidates retain preceding score RNG but never run PositionValid");
    Fixture wall;wall.wall=true;r=slot();const auto blocked=wall.run(r);
    require(blocked.supported&&blocked.goals.empty()&&blocked.no_valid_goals&&!blocked.blocker.id,"WallInWay cannot outrank initial NoValidGoals");
}
void chairs_and_failure(){
    Fixture only;auto r=slot();r.standing=0;r.sitting=5;r.facing=-3;only.collision=odh::Ts1PlacementStatus::blocked;only.blocker=3;
    const auto sitting=only.run(r);require(sitting.supported&&sitting.goals.size()==1&&sitting.goals[0].chair.id==3,"known EP26 blocker becomes actual sitting candidate");
    Fixture missing;missing.collision=odh::Ts1PlacementStatus::blocked;missing.blocker=3;const auto failed=missing.run(slot());
    require(failed.supported&&failed.goals.empty()&&!failed.no_valid_goals&&failed.blocker.id==3,"solid object sets highest source failure and blocker");
    Fixture no_chair;r=slot();r.standing=0;r.sitting=1;const auto empty=no_chair.run(r);
    require(empty.supported&&empty.goals.empty()&&!empty.no_valid_goals,"OnlySit starts with NoChair, not NoValidGoals");
    Fixture ignored_error;ignored_error.collision=odh::Ts1PlacementStatus::blocked;
    require(ignored_error.run(slot()).goals.size()==1,"source ignores non-object PositionValid rejection when no blocker entity exists");
    Fixture avatar;avatar.collision=odh::Ts1PlacementStatus::blocked;avatar.blocker=4;
    const auto penalized=avatar.run(slot());require(penalized.supported&&penalized.goals.size()==1&&!avatar.destinations,
        "avatar blocker penalizes candidate and skips redundant destination consultation");
    Fixture mixed;r=slot();r.flags=255;r.proximity=std::array<std::int32_t,3>{16,16,16};
    mixed.services.position_valid=[&](const auto&,auto,const auto&,auto,auto){
        ++mixed.checks;return mixed.checks==1?odh::Ts1PlacementResult{odh::Ts1PlacementStatus::blocked,10,{3,mixed.world.find(3)->generation},{}}:
            odh::Ts1PlacementResult{odh::Ts1PlacementStatus::success,-1,{}, {}};};
    const auto recovered=mixed.run(r);
    require(recovered.supported&&!recovered.goals.empty()&&recovered.failure_code==0&&!recovered.blocker.id,
        "successful ranked goals clear earlier rejected-candidate failure metadata");
}
void containment_and_unknown(){
    Fixture f;auto r=slot();r.snap_target_slot=0;f.services.position_valid={};f.services.raycast_wall={};f.services.destination_conflict={};f.services.architectural_door_at={};
    const auto contained=f.run(r);require(contained.supported&&contained.goals.size()==1&&!f.checks,"SnapTargetSlot bypasses geometric placement checks exactly as source");
    Fixture unknown;unknown.unknown=true;const auto result=unknown.run(slot());require(!result.supported&&!unknown.draws&&!unknown.checks,"unknown room does not become room0 or empty candidate set");
    Fixture budget;r=slot();r.flags=255;r.proximity=std::array<std::int32_t,3>{0,1024,16};r.resolution=1;
    const auto exceeded=budget.run(r,{16,4096});require(!exceeded.supported&&!budget.draws,"bounded excessive enumeration rejects before RNG");
    auto height=slot();height.height=2;require(odh::ts1_storage_slot_height(height)==2.5f,"source SLOT height2 keeps fractional offset");
    height.height=7;require(odh::ts1_storage_slot_height(height)==7,"source sitting height7");
    height.height=5;height.offset_bits[2]=bits(3.25f);require(odh::ts1_storage_slot_height(height)==3.25f,"nonstandard height5 uses raw Z");
}
}
int main(){try{diagnostic_observation();exact_and_offsets();range_and_phase();chairs_and_failure();containment_and_unknown();std::cout<<"TS1 shared SLOT parser matrix passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
