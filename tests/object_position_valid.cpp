// SPDX-License-Identifier: MPL-2.0
#include "odh/object_position_valid.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
using Status=odh::ObjectPositionValidStatus;
using Result=odh::ObjectPositionValidResult;
using Check=odh::ObjectPositionEntryPoint5Request;
struct Fixture {
    odh::World world;odh::ObjectPositionArchitecture architecture;
    odh::ObjectPositionIntersections overlaps;
    odh::ObjectPositionValidServices services;
    std::vector<std::pair<int,int>> calls;
    std::function<std::optional<bool>(const Check&)> answer;
    std::function<bool()> authority_copy;
    bool authorized=true;int architecture_calls=0,query_calls=0;float observed_heading=-1;
    std::int32_t height=2;
    Fixture(){
        for(std::int16_t id=1;id<=4;++id){
            require(world.create(id,odh::EntityKind::object,{0,0,2})&&world.set_position(id,{20,20,1,0}),"construct ordinary source placement objects");
            require(world.define_type(id,0x1000u+static_cast<std::uint32_t>(id)),"declare ordinary source object definition");
            std::vector<odh::EntryPoint> entry_points(27);entry_points[5].action=500;
            require(world.define_entry_points(id,entry_points),"bind source object entry points");
            for(const auto pair:std::vector<std::pair<std::int16_t,std::int16_t>>{{1,0},{4,1},{8,0},{13,128},{23,1},{27,5},{28,6},{42,3},{63,0}})
                require(world.declare_object_word(id,pair.first,pair.second),"declare known source object word");
        }
        word(1,1,3);
        architecture.level_offset=0;overlaps.footprint_present=true;overlaps.query.status=odh::RoomInfoObstacleIndexStatus::complete;
        services.architecture=[&](auto,const auto&){++architecture_calls;return std::optional<odh::ObjectPositionArchitecture>{architecture};};
        services.intersections=[&](auto,const auto&,auto){++query_calls;observed_heading=static_cast<float>(world.find(1)->object_words.at(1));return std::optional<odh::ObjectPositionIntersections>{overlaps};};
        services.same_group=[](auto,auto)->std::optional<bool>{return false;};
        services.ignore_intersection=[](auto,auto)->std::optional<bool>{return false;};
        services.ghost_image=[](auto)->std::optional<bool>{return false;};
        services.slot0_height=[&](auto)->std::optional<std::int32_t>{return height;};
        services.entry_point5=[&](const Check& q)->std::optional<bool>{
            require(q.source_still_matches&&q.source_still_matches()&&q.action==500&&
                q.arguments==std::array<std::int16_t,4>{q.other.id,q.ghost,0,0},"EP5 receives exact live source capability and bilateral arguments");
            require(world.find(1)->object_words.at(1)==0,"live GetObjPlace EP5 observes temporary NORTH");
            authority_copy=q.source_still_matches;calls.emplace_back(q.tested.id,q.other.id);
            return answer?answer(q):std::optional<bool>{false};
        };
    }
    void word(int id,int index,std::int16_t value){odh::VmFrame frame;frame.context.caller=static_cast<std::int16_t>(id);
        require(world.write(frame,3,static_cast<std::int16_t>(index),value),"write already-known live object word");}
    void hits(std::initializer_list<int> ids){overlaps.query.intersections.clear();for(auto id:ids)overlaps.query.intersections.push_back({id,0,{0,0,8,8},odh::RoomInfoObstacleCollection::static_set});}
    Result run(std::uint8_t flags=1,std::uint8_t direction=0,odh::EntityPosition position={24,24,1,0}){
        odh::ObjectPositionValid valid(world,services);
        const auto* e=world.find(1);const auto original=e->object_words.at(1);const auto generation=e->generation;
        const auto result=valid.evaluate({{1,generation},position,direction,flags,[&]{return authorized;}});
        e=world.find(1);if(e&&e->generation==generation){const auto notches=e->object_words.at(23);
            require(e->object_words.at(1)==(notches>1?original-original%notches:original),"PositionValid restores old object direction through live RotationNotches");}
        return result;
    }
};
void already_published_native_heading(){
    Fixture f;f.hits({2});f.word(1,23,4);
    const odh::WorldEntityReference ref{1,f.world.find(1)->generation};
    require(f.world.publish_native_object_heading(ref,1),"native group publishes unquantized member heading");
    unsigned publications=0,checks=0;
    f.services.publish_heading=[&](auto,auto,const auto&){++publications;return false;};
    f.services.entry_point5=[&](const Check&)->std::optional<bool>{
        ++checks;require(f.world.publish_native_object_heading(ref,5),"authored EP5 effect changes actual native heading");return false;
    };
    odh::ObjectPositionValid valid(f.world,f.services);
    const auto result=valid.evaluate({{1,ref.generation},{24,24,1,1},1,1,[&]{return f.authorized;},true});
    require(result.status==Status::blocked&&checks==2&&publications==0&&
        f.world.find(1)->object_words.at(1)==5&&f.world.find(1)->position->direction==5,
        "native checks preserve authored heading changes without temporary quantization/restoration");
    const auto mismatch=valid.evaluate({{1,ref.generation},{24,24,1,3},3,1,[&]{return f.authorized;},true});
    require(mismatch.status==Status::unsupported&&checks==2&&publications==0,
        "already-published mode rejects a heading not owned by the actual object");
}
void temporary_heading_publication(){
    {
        Fixture f;f.hits({2});f.word(1,23,2);bool active=false;std::vector<std::uint8_t> headings;
        std::function<bool()> retained;
        f.services.publish_heading=[&](auto id,std::uint8_t direction,const std::function<bool()>& fixed){
            require(!active&&id.id==1&&id.generation==f.world.find(1)->generation,"temporary setter scope owns the exact source generation");
            active=true;headings.push_back(direction);retained=fixed;
            const bool completed=fixed();active=false;
            require(completed&&f.world.find(1)->object_words.at(1)==direction&&f.world.find(1)->position->direction==direction,
                "heading publication receives and publishes the effective quantized direction");
            return completed;
        };
        const auto intersections=f.services.intersections;
        f.services.intersections=[&](auto id,const auto& point,auto direction){require(!active,"RoomInfo checks remain outside heading setter scope");return intersections(id,point,direction);};
        f.answer=[&](const Check&)->std::optional<bool>{require(!active,"actual EP5 callbacks are outside temporary publication scope");return false;};
        require(f.run().status==Status::blocked&&headings==std::vector<std::uint8_t>{0,2}&&f.calls.size()==2&&!active,
            "temporary orientation and mandatory restoration each publish once around one bilateral check");
        require(retained&&!retained()&&f.world.find(1)->object_words.at(1)==2,
            "retained temporary setters expire without publishing another heading");
    }
    for(unsigned mode=0;mode<4;++mode){
        Fixture f;unsigned publications=0;require(f.world.declare_object_word(1,60,0),"declare durable heading wrapper fixture effect");
        f.services.publish_heading=[&](auto,std::uint8_t,const std::function<bool()>& fixed){
            ++publications;
            if(publications>1)return fixed(); // Restoration must still be attempted.
            f.word(1,60,77);
            if(mode==0)return true; // Omitted fixed setter cannot claim success.
            const bool completed=fixed();
            if(mode==1){require(!fixed(),"temporary heading setter cannot execute twice");return true;}
            if(mode==2)return false;
            if(mode==3)throw std::runtime_error("heading wrapper failed after publication");
            return completed;
        };
        require(f.run().status==Status::unsupported&&publications==2&&f.query_calls==0&&
            f.world.find(1)->object_words.at(60)==77&&f.world.find(1)->object_words.at(1)==3,
            "rejected/skipped/repeated/throwing orientation preserves effects and still restores source heading");
    }
    {
        Fixture f;f.hits({2});unsigned publications=0;
        f.services.publish_heading=[&](auto,std::uint8_t,const std::function<bool()>& fixed){++publications;return fixed();};
        f.answer=[](const Check&)->std::optional<bool>{throw std::runtime_error("authored EP5 unavailable");};
        require(f.run().status==Status::unsupported&&publications==2&&f.world.find(1)->object_words.at(1)==3,
            "throwing EP5 still restores heading through its separate actual publication scope");
    }
    {
        Fixture f;unsigned publications=0;
        f.services.publish_heading=[&](auto,std::uint8_t,const std::function<bool()>& fixed){
            return ++publications==1?fixed():false;
        };
        odh::ObjectPositionValid valid(f.world,f.services);const auto generation=f.world.find(1)->generation;
        const auto result=valid.evaluate({{1,generation},{24,24,1,0},0,1,[&]{return f.authorized;}});
        require(result.status==Status::unsupported&&publications==2&&f.world.find(1)->object_words.at(1)==0,
            "refused restoration is reported unsupported without an unguarded fallback write");
    }
    {
        Fixture f;unsigned publications=0;f.services.publish_heading=[&](auto,std::uint8_t,const std::function<bool()>& fixed){++publications;return fixed();};
        require(f.run(4).status==Status::success&&publications==0,"AllowIntersection does not invent temporary heading work");
    }
}
void architecture_rules(){
    Fixture f;f.architecture.out_of_bounds=true;require(f.run().placement_error==0&&f.query_calls==0,"bounds reject before overlap");
    f.architecture.out_of_bounds=false;f.architecture.user_out_of_bounds=true;
    require(f.run(2).placement_error==0&&f.run(0).status==Status::success,"user build bounds only apply when requested");
    f.architecture.user_out_of_bounds=false;f.word(1,13,0);f.architecture.wall_segments=16;
    require(f.run(4).placement_error==6,"AllowIntersection cannot bypass a diagonal wall rejection");
    f.architecture.wall_segments=0;f.word(1,13,64);require(f.run().placement_error==22,"required diagonal wall must exist");
    f.word(1,13,1);f.architecture.wall_segments=2;require(f.run(1,0).status==Status::success&&
        f.run(1,2).placement_error==5&&f.run(1,1).status==Status::success,
        "wall rotation matches cardinal DirectionToWallOff and source diagonal default0");
    f.word(1,63,2);f.architecture.occupied_walls=2;require(f.run().placement_error==21,"exclusive attachment requires unused wall");
    f.word(1,63,0);f.word(1,13,256);require(f.run().placement_error==6,"forbidden rotated wall rejects");
    f.word(1,13,128);f.architecture.wall_segments=0;f.architecture.occupied_walls=0;
    f.architecture.precise_floor=65535;require(f.run().placement_error==28,"ordinary object cannot place on pool without permission");
    f.word(1,42,4096);require(f.run().status==Status::success,"RequirePool permits actual pool");
    f.architecture.precise_floor=1;require(f.run().placement_error==30,"RequirePool rejects ordinary floor before other floor flags");
    f.architecture.precise_floor=65534;f.word(1,42,3);require(f.run().placement_error==28,"water requires explicit permission");
    f.word(1,42,8192);f.architecture.precise_floor=1;require(f.run().placement_error==29,"RequireWater rejects ordinary floor");
    f.architecture.precise_floor=0;f.word(1,42,1);require(f.run().placement_error==19,"bare first-level terrain needs OnTerrain");
    f.word(1,42,2);require(f.run(1,0,{24,24,2,0}).placement_error==17,"upstairs missing floor rejects absent InAir and zero LevelOffset");
    f.architecture.level_offset=1;require(f.run(1,0,{24,24,2,0}).status==Status::success,"nonzero OBJD LevelOffset permits source upstairs hole");
    f.architecture.precise_floor=1;require(f.run().placement_error==18,"solid floor still requires OnFloor or nonzero offset WITH InAir");
    f.word(1,42,512);require(f.run().status==Status::success,"InAir plus nonzero offset permits solid floor");
    f.word(1,42,3);f.architecture.sloped=true;require(f.run().placement_error==16,"OnFloor without OnSlope rejects slope");
    f.word(1,42,259);require(f.run().status==Status::success,"OnSlope permits source sloped terrain");
    const auto count=f.architecture_calls;require(f.run(1,0,{-32768,-32768,1,0}).status==Status::success&&f.architecture_calls==count,
        "canonical source OUT_OF_WORLD bypasses architecture rather than fabricating floor placement");
}
void ordered_ep5_and_ignored_rules(){
    Fixture f;f.hits({3,2});const auto last=f.run();require(last.placement_error==10&&last.object.id==3&&
        f.calls==std::vector<std::pair<int,int>>{{1,2},{2,1},{1,3},{3,1}},"source combined static/dynamic owners sort before bilateral EP5; last solid blocker wins");
    require(f.authority_copy&&!f.authority_copy(),"retained EP5 capability expires after PositionValid returns");
    Fixture exempt;exempt.hits({2});exempt.answer=[](const Check& q)->std::optional<bool>{return q.tested.id==1;};
    require(exempt.run().status==Status::success&&exempt.calls==std::vector<std::pair<int,int>>{{1,2}},"target true short-circuits counterpart EP5");
    Fixture blocker_flags;blocker_flags.hits({2});blocker_flags.word(2,8,16);
    require(blocker_flags.run().placement_error==10&&blocker_flags.calls.size()==2,"GetObjPlace does not borrow avatar placement's blocker-flag exemption");
    Fixture ghost;ghost.hits({2});ghost.services.ghost_image=[](auto id)->std::optional<bool>{return id.id==2;};
    ghost.answer=[](const Check& q)->std::optional<bool>{require(q.ghost==1,"source ghost1 propagates bilaterally");return false;};
    require(ghost.run().placement_error==10,"ghost flag is a check argument, not blanket permission");
    Fixture ignored;ignored.hits({2});ignored.services.ignore_intersection=[](auto,auto)->std::optional<bool>{return true;};
    require(ignored.run().status==Status::success&&ignored.calls.empty(),"IgnoreIntersection member skips EP5");
    Fixture group;group.hits({2});group.services.same_group=[](auto,auto)->std::optional<bool>{return true;};
    require(group.run().status==Status::success&&group.calls.empty(),"same multitile group skips EP5");
    Fixture avatars;require(avatars.world.remove(2)&&avatars.world.create(2,odh::EntityKind::avatar,{0,88,0}),"create avatar overlap");
    avatars.hits({2});avatars.word(1,8,16);
    require(avatars.run(8).status==Status::success&&avatars.calls.empty(),"GetObjPlace ignores AllAvatarsSolid, unlike GetAvatarPlace");
    Fixture notches;notches.hits({2});notches.answer=[&](const Check&)->std::optional<bool>{notches.word(1,23,2);return false;};
    require(notches.run().placement_error==10&&notches.world.find(1)->object_words.at(1)==2,
        "restoring source old RadianDirection re-applies RotationNotches changed by EP5");
}
void support_rules(){
    Fixture f;f.hits({2,3});f.word(1,4,2);
    f.answer=[](const Check& q)->std::optional<bool>{require(q.other.id!=3&&q.tested.id!=3,"valid support returns before later overlapping obstacles");return false;};
    auto accepted=f.run(1);require(accepted.status==Status::success&&accepted.support_slot&&accepted.object.id==2,
        "allowed empty slot height and strictly greater strength select support slot0");
    require(f.run(0).placement_error==10,"without AcceptSlots even successful support is final overlap rejection");
    f.answer={};f.hits({2});f.word(2,28,5);require(f.run(1).placement_error==13&&f.run(0).placement_error==10,
        "equal support weight fails strictly and PositionValid overrides detail without AcceptSlots");
    f.word(2,28,6);f.height=1;f.word(1,4,8);require(f.run().placement_error==37,"no-floor counter requirement retains CounterHeight error");
    f.word(1,4,4);f.height=8;require(f.run().placement_error==44,"disallowed end-table height preserves computer-specific source error");
    f.height=1;require(f.run().placement_error==23,"other no-floor height mismatch retains HeightNotAllowed");
    f.word(1,4,3);f.height=3;require(f.run().placement_error==10,"floor-allowed height mismatch retains original overlap error");
    f.word(1,4,2);f.height=2;require(f.world.attach(2,0,4)&&f.run().placement_error==10,"occupied slot cannot be selected as support");
    Fixture no_footprint;no_footprint.word(1,4,2);no_footprint.overlaps.footprint_present=false;
    require(no_footprint.run().placement_error==23,"null footprint does not invent floor permission for a surface-only object");
    Fixture live;live.word(1,4,2);live.hits({2});
    live.answer=[&](const Check& q)->std::optional<bool>{if(q.tested.id==1)live.word(1,27,100);return false;};
    require(live.run().status==Status::success&&live.world.find(1)->object_words.at(27)==100,
        "GetObjPlace caches target weight before checks but preserves check mutations");
}
void unknown_and_effects(){
    Fixture missing;missing.hits({2});missing.services.entry_point5={};
    require(missing.run().status==Status::unsupported,"unavailable EP5 is not false or empty placement");
    Fixture absent;absent.services.intersections={};require(absent.run().status==Status::unsupported,"missing footprint capability is not absent footprint");
    Fixture reentry;reentry.hits({2});odh::ObjectPositionValid nested(reentry.world,reentry.services);
    odh::ObjectPositionValidRequest nested_request{{1,reentry.world.find(1)->generation},{24,24,1,0},0,1,[]{return true;}};
    reentry.answer=[&](const Check&)->std::optional<bool>{require(nested.evaluate(nested_request).status==Status::unsupported,
        "live EP5 cannot reenter the same placement evaluator");return false;};
    require(nested.evaluate(nested_request).placement_error==10&&reentry.query_calls==1&&
        reentry.world.find(1)->object_words.at(1)==3&&reentry.world.find(1)->position==std::optional<odh::EntityPosition>{{20,20,1,3}},
        "object placement evaluates only and retains original physical position after checks");
    Fixture unauthorized;odh::ObjectPositionValid valid(unauthorized.world,unauthorized.services);
    odh::ObjectPositionValidRequest request{{1,unauthorized.world.find(1)->generation},{24,24,1,0},0,1,[]{return true;}};
    require(valid.evaluate({request.target,request.candidate,0,1,{}}).status==Status::unsupported,"missing source capability refuses before effects");
    Fixture throwing;throwing.hits({2});throwing.answer=[&](const Check&)->std::optional<bool>{throwing.word(1,27,42);throw std::runtime_error("live check fault");};
    require(throwing.run().status==Status::unsupported&&throwing.world.find(1)->object_words.at(27)==42,
        "throwing check retains real World effects and restores only temporary orientation");
    Fixture replaced;replaced.hits({2});replaced.answer=[&](const Check&)->std::optional<bool>{
        require(replaced.world.remove(1)&&replaced.world.create(1,odh::EntityKind::object)&&replaced.world.declare_object_word(1,1,6),"recycle target during EP5");return false;};
    require(replaced.run().status==Status::unsupported&&replaced.world.find(1)->object_words.at(1)==6,
        "orientation restoration never clobbers a replacement generation");
    Fixture callbacks;callbacks.hits({2});callbacks.services.same_group=[&](auto,auto)->std::optional<bool>{
        require(callbacks.world.remove(2)&&callbacks.world.create(2,odh::EntityKind::object),"recycle group candidate");return true;};
    require(callbacks.run().status==Status::unsupported,"same-group callback cannot skip generation revalidation");
}
}
int main(){try{already_published_native_heading();temporary_heading_publication();architecture_rules();ordered_ep5_and_ignored_rules();support_rules();unknown_and_effects();
    std::cout<<"object PositionValid checks: "<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
