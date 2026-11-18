// SPDX-License-Identifier: MPL-2.0
// Authored/managed SLOT enumeration and collision contracts.
// Generated DVD OP27 routing has a separate source-derived consumer and test.
#include "simantics/engine/VMSlotEnumeration.h"
#include "simantics/engine/VMSlotParser.h"
#include "simantics/primitives/VMGotoRelativePosition.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include <iostream>
#include <vector>
#include <utility>
#include <string>
#include <cmath>
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF::Chunks;
using GoalOffset=std::pair<int,int>;
#include "pin-vectors.inc"
static int failures=0, checks=0;
static void check(bool ok,const std::string& label) { ++checks; if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';} }
static std::vector<GoalOffset> goals(const SLOTItem& s, double rotation=0) {
    if (((int)s.Rsflags & 255)==0) return {{0,0}}; // production exact-position branch
    std::vector<GoalOffset> result;
    const int max=s.MaxProximity?s.MaxProximity:s.MinProximity;
    EnumerateSlotLocations(s,max,[&](int x,int y,double distance){
        if(!SlotProximityValid(s.MinProximity,max,distance)) return;
        auto flags=VMSlotParser::GetSearchDirection(st::Vector2(10.5f,10.5f),st::Vector2(10.5f+x/16.f,10.5f+y/16.f),(float)rotation);
        if(((int)flags & (int)s.Rsflags)!=0) result.push_back({x,y});
    });
    return result;
}
static SLOTItem slot(int min,int max,int flags=255) { SLOTItem s;s.MinProximity=min;s.MaxProximity=max;s.Rsflags=(SLOTFlags)flags;return s; }
static void equal(const std::string& name,const std::vector<GoalOffset>& got,const std::vector<GoalOffset>& expected) {
    check(got==expected,name);std::cout<<name<<":";for(auto p:got)std::cout<<" ["<<p.first<<","<<p.second<<"]";std::cout<<'\n';
}
static int collisionChecks() {
    // Same resource-free constructors as interaction_checkpoint_route_contract.
    // No Content::Init, VM, world tick, profile, commercial data or RNG operation.
    using namespace FSO::SimAntics;
    using namespace FSO::SimAntics::Model;
    using namespace FSO::LotView::Model;
    using namespace FSO::Files::Formats::IFF;
    VMEntity::UseWorld=false; VMContext::UseWorld=false;
    VMContext context(nullptr);
    context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&context));
    context.Architecture()->Rooms[0].Map.assign(64,0);
    context.RoomInfo.resize(1);
    IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"collision-fixture",true);
    OBJD definition;definition.GUID=0x12345678;
    FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
    VMAvatar actor(&object),other(&object); VMGameObject permeableObject(&object,nullptr);
    Entities::VMMultitileGroup actorGroup,otherGroup,objectGroup;
    actor.MultitileGroup=&actorGroup;other.MultitileGroup=&otherGroup;permeableObject.MultitileGroup=&objectGroup;
    actor.ObjectID=1;other.ObjectID=2;permeableObject.ObjectID=3;
    const LotTilePos pos(64,64,1); const auto dir=Direction::NORTH;
    other.Footprint=other.GetObstacle(pos,dir);
    other.SetValue(VMStackObjectVariable::Flags,(int16_t)VMEntityFlags::AllowPersonIntersection);
    actor.ObjectData[(int)VMStackObjectVariable::PlacementFlags]=(int16_t)VMPlacementFlags::OnTerrain;
    context.RoomInfo[0].Entities={&other};
    const auto legacy=context.GetAvatarPlace(&actor,pos,dir);
    const auto solid=context.GetAvatarPlace(&actor,pos,dir,true);
    check(legacy.Status==VMPlacementError::Success,"legacy3arg preserves permeable-avatar placement");
    check(solid.Status==VMPlacementError::CantIntersectOtherObjects && solid.Object==&other,"explicit solid policy detects actual overlapping avatar");
    check(actor.PositionValid(pos,dir,&context).Status==VMPlacementError::Success,"PositionValid3arg retains default");
    const auto routed=actor.PositionValid(pos,dir,&context,true);
    check(routed.Status==VMPlacementError::CantIntersectOtherObjects && routed.Object==&other,"PositionValid4arg propagates solid policy");
    permeableObject.Footprint=std::make_unique<FSO::SimAntics::Model::Routing::VMObstacle>(61,61,67,67);
    permeableObject.SetValue(VMStackObjectVariable::Flags,(int16_t)VMEntityFlags::AllowPersonIntersection);
    context.RoomInfo[0].Entities={&permeableObject};
    check(context.GetAvatarPlace(&actor,pos,dir,true).Status==VMPlacementError::Success,"solid-avatar policy keeps permeable non-avatar unchanged");
    context.RoomInfo[0].Entities.clear();
    check(actor.PositionValid(pos,dir,&context,true).Status==VMPlacementError::Success,"solid policy accepts unobstructed destination");
    std::cout<<"collision_checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--collision") return collisionChecks();
    for(const auto& fixture:pinCases) {
        SLOTItem raw;raw.MinProximity=fixture.min;raw.MaxProximity=fixture.max;raw.Resolution=fixture.res;raw.Rsflags=(SLOTFlags)fixture.flags;
        size_t index=0;
        EnumerateSlotLocations(raw,raw.MaxProximity?raw.MaxProximity:raw.MinProximity,[&](int x,int y,double distance){
            check(index<fixture.count,"managed oracle callback bounds");
            if(index<fixture.count) {
                const auto& expected=fixture.raw[index];
                check(x==expected.x && y==expected.y && std::abs(distance-expected.distance)<1e-12,std::string("managed raw enumeration ")+fixture.name);
                const auto mask=VMSlotParser::GetSearchDirection(st::Vector2(10.5f,10.5f),st::Vector2(10.5f+x/16.f,10.5f+y/16.f),0);
                check((int)mask==expected.mask,std::string("managed raw mask ")+fixture.name);
            }
            ++index;
        });
        check(index==fixture.count,std::string("managed raw callback count ")+fixture.name);
    }
    for(const auto& d:pinDirections) {
        auto mask=VMSlotParser::GetSearchDirection(st::Vector2(10.5f,10.5f),st::Vector2(10.5f+d.x/16.f,10.5f+d.y/16.f),d.rotation);
        check((int)mask==d.mask,"managed direction+rotation literal");
    }
    equal("circle16",goals(slot(16,16)),{{0,16},{11,11},{16,0},{11,-11},{0,-16},{-11,-11},{-16,0},{-11,11}});
    equal("circle16-mask55",goals(slot(16,16,0x55)),{{0,16},{16,0},{0,-16},{-16,0}});
    equal("raw-max0-range",goals(slot(16,0)),{{-16,0},{0,-16},{0,16},{16,0}});
    equal("range16-24",goals(slot(16,24)),{{-16,-16},{-16,0},{-16,16},{0,-16},{0,16},{16,-16},{16,0},{16,16}});
    equal("range16-32",goals(slot(16,32)),{{-32,0},{-16,-16},{-16,0},{-16,16},{0,-32},{0,-16},{0,16},{0,32},{16,-16},{16,0},{16,16},{32,0}});
    auto boundary=slot(23,32); auto lower=goals(boundary);
    check(std::find(lower.begin(),lower.end(),GoalOffset{16,16})!=lower.end(),"lower tolerance admits (16,16), radius22.627 in [22.5,32.5]");
    boundary=slot(16,32);boundary.Resolution=3;auto upper=goals(boundary);
    check(std::find(upper.begin(),upper.end(),GoalOffset{12,30})!=upper.end(),"upper tolerance admits (12,30), radius32.311");
    check(std::find(upper.begin(),upper.end(),GoalOffset{15,30})==upper.end(),"upper tolerance rejects (15,30), radius33.541");
    check(SlotSearchRotation(512,false,1.2f)==0,"Absolute search has zero rotation");
    check(SlotSearchRotation(512,true,1.2f)==1.2f,"Absolute onPoint keeps object rotation");
    check(SlotSearchRotation(0,false,1.2f)==1.2f,"relative search keeps object rotation");
    check(SlotDestinationLevel(2,15)==3 && SlotDestinationLevel(2,-1)==1 && SlotDestinationLevel(2,0)==2,"Offset.Z levels use floor, including negative");
    int draws=0;auto draw=[&](int bound){check(bound==1024,"score RNG bound");++draws;return 512;};
    check(SlotBaseScore(0,10,16,24,draw)==2.5,"ordinary proximity score");
    check(SlotBaseScore(8192,10,16,24,draw)==.5,"RandomScoring excludes proximity bonus");
    check(draws==2,"one RNG draw per scored candidate");
    check(SlotCallerDistanceScore(0,10,6)==8 && SlotCallerDistanceScore(8192,10,6)==10,"RandomScoring excludes caller-distance penalty");
    auto weighted=slot(0,0);weighted.Standing=3;weighted.Sitting=5;
    check(SlotStandingScore(weighted,false,2)==6 && SlotStandingScore(weighted,true,2)==10,"standing/sitting weighted score");
    weighted.SnapTargetSlot=0;check(SlotStandingScore(weighted,false,2)==2 && SlotStandingScore(weighted,true,2)==2,"SnapTarget excludes standing/sitting multiplier");
    auto average=SlotFacingCenter(65536,true,st::Vector2(2,0),st::Vector2(0,2));
    check(average==st::Vector2(0,2) && std::abs(VMSlotParser::GetDirectionTo(st::Vector2(0,0),average)-M_PI)<.00001,"average group center controls actual toward facing");
    check(SlotFacingCenter(65536,false,st::Vector2(2,0),st::Vector2(0,2))==st::Vector2(2,0),"single-part facing keeps object center");
    int nominal=0;
    auto circle=slot(16,16);
    EnumerateSlotLocations(circle,16,[&](int,int,double distance){check(distance==16,"circle nominal distance");++nominal;});
    check(nominal==8,"circle callback count");
    auto square=slot(16,16);square.MaxProximity=0;square.Rsflags=(SLOTFlags)(255 | (1<<30));
    equal("square-range-raw-max0",goals(square),{{-16,-16},{-16,0},{-16,16},{0,-16},{0,16},{16,-16},{16,0},{16,16}});
    // Generated OP27 destinations use the DVD-specific consumer. Their literal
    // source vectors and production checks live in ts1_relative_position_contract.
    std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
