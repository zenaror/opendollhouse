// SPDX-License-Identifier: MPL-2.0
// Pinned FreeSO VMSlotParser; native implementation retaining source phase order.
#include "odh/ts1_placement_slots.hpp"
#include "odh/source_direction.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace odh {
namespace {
constexpr double pi=3.141592653589793238462643383279502884;
float f(float v){volatile float value=v;return value;}
float bits_float(std::uint32_t bits){float v;std::memcpy(&v,&bits,sizeof v);return v;}
double even(double x){const auto low=std::floor(x),frac=x-low;return frac>0.5||(frac==0.5&&std::fmod(std::abs(low),2.0)==1.0)?low+1:low;}
std::int16_t narrow(double x){if(!std::isfinite(x)||x<-2147483648.0||x>2147483647.0)throw std::runtime_error("SLOT coordinate outside source numeric domain");
    const auto bits=static_cast<std::uint16_t>(static_cast<std::int64_t>(even(x)));return bits<32768?static_cast<std::int16_t>(bits):static_cast<std::int16_t>(static_cast<int>(bits)-65536);}
std::int32_t wrap32(std::int64_t value){const auto bits=static_cast<std::uint32_t>(value);
    return bits<=2147483647U?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(static_cast<std::int64_t>(bits)-4294967296LL);}
std::int16_t narrow_level(std::int64_t level){const auto bits=static_cast<std::uint8_t>(level);return bits<128?bits:static_cast<std::int16_t>(static_cast<int>(bits)-256);}
std::int32_t tile(std::int32_t x){return x>=0?x/16:-static_cast<std::int32_t>((-static_cast<std::int64_t>(x)+15)/16);}
bool live(const World& world,Ts1PlacementIdentity identity){const auto* e=world.find(identity.id);return e&&identity.generation&&e->generation==identity.generation;}
EntityPosition source_position(const World& world,Ts1PlacementIdentity identity){
    const auto* e=world.resolve_reference({identity.id,identity.generation});if(!e)throw std::runtime_error("SLOT entity identity changed");
    if(!e->position)return {-32768,-32768,1,0};
    const auto p=*e->position;if(p.x<-32768||p.x>32767||p.y<-32768||p.y>32767||p.level<-128||p.level>127||p.direction>7)
        throw std::runtime_error("SLOT position outside source short/sbyte domain");
    return p;
}
float angle(const World& world,Ts1PlacementIdentity identity){
    const auto result=source_entity_radian_direction(world,identity.id,identity.generation);
    if(!result)throw std::runtime_error("SLOT angle identity/direction unknown");return *result;
}
std::int32_t distance(EntityPosition a,EntityPosition b){const auto dx=static_cast<std::int64_t>(a.x)-b.x,dy=static_cast<std::int64_t>(a.y)-b.y;
    const auto square=dx*dx+dy*dy;if(square>std::numeric_limits<std::int32_t>::max())throw std::runtime_error("SLOT distance exceeds source int32 squared domain");
    return static_cast<std::int32_t>(std::sqrt(static_cast<double>(square)));}
std::uint8_t search(float cx,float cy,float x,float y,float rot){
    double d=std::atan2(std::floor(x)-std::floor(cx),std::floor(cy)-std::floor(y))*(180/pi)-rot*(180/pi);
    d=std::fmod(d,360);if(d<0)d+=360;if(d>180)d-=360;
    const double err=-0.1f;std::uint8_t mask=0;
    if(d>=-45-err&&d<=45+err)mask|=1;
    if(d>=0-err&&d<=90+err)mask|=2;
    if(d>=45-err&&d<=135+err)mask|=4;
    if((d>=90-err&&d<=180+err)||d<=-180+err)mask|=8;
    if(d>=135-err||d<=-135+err)mask|=16;
    if((d>=-180-err&&d<=-90+err)||d>=180-err)mask|=32;
    if(d>=-135-err&&d<=-45+err)mask|=64;
    if(d>=-90-err&&d<=0+err)mask|=128;
    return mask;
}
}
const char* ts1_slot_diagnostic_reason_name(Ts1SlotDiagnosticReason reason) noexcept {
    switch(reason){
    case Ts1SlotDiagnosticReason::bounds:return "bounds";
    case Ts1SlotDiagnosticReason::room:return "room";
    case Ts1SlotDiagnosticReason::direction:return "direction";
    case Ts1SlotDiagnosticReason::wall:return "wall";
    case Ts1SlotDiagnosticReason::position_blocked:return "position_blocked";
    case Ts1SlotDiagnosticReason::position_unsupported:return "position_unsupported";
    case Ts1SlotDiagnosticReason::object:return "object";
    case Ts1SlotDiagnosticReason::chair_candidate:return "chair_candidate";
    case Ts1SlotDiagnosticReason::chair_angle:return "chair_angle";
    case Ts1SlotDiagnosticReason::chair_required:return "chair_required";
    case Ts1SlotDiagnosticReason::avatar_penalty:return "avatar_penalty";
    case Ts1SlotDiagnosticReason::door_penalty:return "door_penalty";
    case Ts1SlotDiagnosticReason::destination_conflict:return "destination_conflict";
    }
    return "unknown";
}
Ts1PlacementLocations ts1_find_slot_locations(const World& world,const Ts1PlacementInvocation& invocation,
    const SlotRecord& slot,Ts1PlacementIdentity target,Ts1PlacementIdentity caller,
    const Ts1SlotLocationsServices& services,Ts1SlotLocationsLimits limits){
    Ts1PlacementLocations result;Ts1SlotDiagnostics diagnostic;
    const auto count=[&](std::uint64_t& value){if(value==UINT64_MAX)diagnostic.counters_saturated=true;else ++value;};
    const auto sample=[&](Ts1SlotDiagnosticReason reason,std::optional<EntityPosition> position={},
        std::int16_t error=-1,Ts1PlacementIdentity blocker={}){
        for(std::size_t n=0;n<diagnostic.sample_count;++n){auto& existing=diagnostic.samples[n];
            if(existing.reason==reason&&existing.placement_error==error&&existing.blocker.id==blocker.id&&existing.blocker.generation==blocker.generation){count(existing.occurrences);return;}}
        if(diagnostic.sample_count==diagnostic.samples.size()){count(diagnostic.samples_dropped);return;}
        diagnostic.samples[diagnostic.sample_count++]={reason,position,error,blocker,1};
    };
    try{
        // The source parser reads its target's retained entity reference;
        // only the caller receives PositionValid checks and must remain live.
        const auto valid=[&]{return invocation.source_still_matches&&invocation.source_still_matches()&&
            world.resolve_reference({target.id,target.generation})&&live(world,caller);};
        if(!valid()||!services.out_of_bounds||!services.room_at||!limits.candidates||!limits.group_members)
            throw std::runtime_error("SLOT source identities/geometry capability unavailable");
        const auto raw=slot.proximity.value_or(std::array<std::int32_t,3>{0,0,0});
        const auto minimum=raw[0],maximum=raw[1]?raw[1]:minimum,desired=raw[2]?raw[2]:minimum;
        const auto facing=slot.facing.value_or(-2),resolution=slot.resolution.value_or(16);
        diagnostic.minimum=minimum;diagnostic.maximum=maximum;diagnostic.desired=desired;
        diagnostic.facing=facing;diagnostic.resolution=resolution;diagnostic.effective_flags=slot.flags;
        if(facing<-3||facing>7||minimum<0||maximum<0||resolution<=0)throw std::runtime_error("SLOT authored numeric fields invalid");
        const float ox=bits_float(slot.offset_bits[0]),oy=bits_float(slot.offset_bits[1]),oz=bits_float(slot.offset_bits[2]);
        if(!std::isfinite(ox)||!std::isfinite(oy)||!std::isfinite(oz))throw std::runtime_error("SLOT offset is not finite");
        auto flags=slot.flags;
        const bool only_sit=slot.sitting>0&&slot.standing==0;
        int failure=only_sit?1:0; // NoValidGoals, NoChair, Person, Object priorities.
        result.no_valid_goals=!only_sit;
        const auto fail=[&](int priority,Ts1PlacementIdentity blocker){if(priority>failure){failure=priority;result.no_valid_goals=false;result.blocker=blocker;}};
        const auto target_start=source_position(world,target);diagnostic.target_start=target_start;
        const auto center=[&](){
            const auto p=source_position(world,target);std::pair<float,float> c{f(p.x/16.0f),f(p.y/16.0f)};
            if(flags&65536U){
                if(!services.group)throw std::runtime_error("SLOT average group authority unavailable");
                const auto group=services.group(target);if(!group||group->empty()||group->size()>limits.group_members)throw std::runtime_error("SLOT average group is unknown or over budget");
                if(group->size()>1){c={0,0};for(const auto member:*group){const auto point=source_position(world,member);c.first=f(c.first+f(point.x/16.0f));c.second=f(c.second+f(point.y/16.0f));}
                    c.first=f(c.first/static_cast<float>(group->size()));c.second=f(c.second/static_cast<float>(group->size()));}
            }
            return c;
        };
        const auto object_center=center();
        const bool on_point=(flags&255U)==0||ox!=0||oy!=0||oz!=0;
        const float rotation=(flags&512U)&&!on_point?0:angle(world,target);
        const float cosine=static_cast<float>(std::cos(rotation)),sine=static_cast<float>(std::sin(rotation));
        const float rx=f(f(ox*cosine)-f(oy*sine)),ry=f(f(ox*sine)+f(oy*cosine));
        const float cx=f(object_center.first+f(rx/16.0f)),cy=f(object_center.second+f(ry/16.0f));
        const auto level=narrow_level(static_cast<std::int64_t>(target_start.level)+static_cast<std::int64_t>(std::floor(oz/15.0f)));
        diagnostic.center_x=cx;diagnostic.center_y=cy;diagnostic.level=level;diagnostic.profile_decoded=true;
        const auto target_room=services.room_at(target_start);if(!target_room||!valid())throw std::runtime_error("SLOT target room unknown");
        std::size_t considered=0;
        const auto verify=[&](float x,float y,std::uint32_t entry,double score,std::optional<float> explicit_facing){
            count(diagnostic.verified);
            if(++considered>limits.candidates)throw std::runtime_error("SLOT candidate budget exceeded");
            if(!valid())throw std::runtime_error("SLOT identity changed before candidate verification");
            EntityPosition point{narrow(f(x*16)),narrow(f(y*16)),level,0};
            const auto outside=services.out_of_bounds(point);if(!outside||!valid())throw std::runtime_error("SLOT bounds unknown");if(*outside){count(diagnostic.bounds_rejected);sample(Ts1SlotDiagnosticReason::bounds,point);return;}
            if(!(flags&8192U))score-=distance(point,source_position(world,caller))/3.0;
            if(slot.snap_target_slot<0){
                if(!services.raycast_wall)throw std::runtime_error("SLOT wall raycast capability unavailable");
                const auto obj=source_position(world,target);
                const auto wall=services.raycast_wall(static_cast<std::int32_t>(x),static_cast<std::int32_t>(y),tile(obj.x),tile(obj.y),level);
                if(!wall||!valid())throw std::runtime_error("SLOT wall raycast stage="+std::string(wall?"source-authority":"provider-unknown")+
                    " from="+std::to_string(static_cast<std::int32_t>(x))+","+std::to_string(static_cast<std::int32_t>(y))+
                    " to="+std::to_string(tile(obj.x))+","+std::to_string(tile(obj.y))+" level="+std::to_string(level)+
                    " caller="+std::to_string(caller.id)+":"+std::to_string(caller.generation)+
                    " target="+std::to_string(target.id)+":"+std::to_string(target.generation)+
                    " owner="+std::to_string(invocation.frame.context.code_owner)+" routine="+std::to_string(invocation.frame.key.id)+
                    " pc="+std::to_string(invocation.frame.pc)+" opcode="+std::to_string(invocation.instruction.opcode));
                if(*wall){count(diagnostic.wall_rejected);sample(Ts1SlotDiagnosticReason::wall,point);return;} // WallInWay has priority -1: source SetFail does not replace initial failure.
            }
            Ts1PlacementGoal goal;goal.position=point;goal.entry_flags=static_cast<std::uint8_t>(entry&255U);
            if(explicit_facing)goal.radians=*explicit_facing;
            else{
                const auto live_center=center();
                if(facing==-2)goal.radians=static_cast<float>(std::atan2(f(live_center.first-x),-f(live_center.second-y)));
                else if(facing==-1)goal.radians=static_cast<float>(std::atan2(f(x-live_center.first),-f(y-live_center.second)));
                else if(facing==-3){goal.face_anywhere=true;goal.radians=0;}
                else{const auto obj=source_position(world,target);const auto direction=(facing+obj.direction)%8;
                    goal.radians=static_cast<float>((direction>4?direction:direction-8)*(pi/4));}
            }
            if(slot.snap_target_slot<0){
                if(!services.position_valid||!services.architectural_door_at||!services.destination_conflict)
                    throw std::runtime_error("SLOT placement/destination capability unavailable");
                count(diagnostic.position_valid_calls);
                const auto solid=services.position_valid(invocation,caller,point,0,9);
                if(solid.status==Ts1PlacementStatus::blocked){count(diagnostic.position_blocked);sample(Ts1SlotDiagnosticReason::position_blocked,point,solid.placement_error,solid.blocker);}
                if(solid.status==Ts1PlacementStatus::unsupported){count(diagnostic.position_unsupported);sample(Ts1SlotDiagnosticReason::position_unsupported,point,solid.placement_error,solid.blocker);}
                if(!valid()||solid.status==Ts1PlacementStatus::unsupported)throw std::runtime_error(solid.diagnostic.empty()?"SLOT PositionValid unsupported":solid.diagnostic);
                bool avatar_in_way=false;
                if(solid.status!=Ts1PlacementStatus::success&&solid.blocker.id){
                    const auto* blocker=world.find(solid.blocker.id);
                    if(!blocker||blocker->generation!=solid.blocker.generation)throw std::runtime_error("SLOT blocker identity unknown");
                    if(blocker->kind==EntityKind::object){
                        bool chair=false;if(slot.sitting>0){if(!services.chair_entry_point26)throw std::runtime_error("SLOT chair EP26 capability unavailable");
                            const auto available=services.chair_entry_point26(solid.blocker);if(!available)throw std::runtime_error("SLOT chair EP26 unknown");chair=*available;}
                        if(chair){goal.chair=solid.blocker;count(diagnostic.chair_candidates);sample(Ts1SlotDiagnosticReason::chair_candidate,point,solid.placement_error,solid.blocker);}
                        else{count(diagnostic.object_rejected);sample(Ts1SlotDiagnosticReason::object,point,solid.placement_error,solid.blocker);fail(3,solid.blocker);return;}
                    }else {avatar_in_way=true;count(diagnostic.avatar_penalties);sample(Ts1SlotDiagnosticReason::avatar_penalty,point,solid.placement_error,solid.blocker);}
                }
                const auto door=services.architectural_door_at(point);if(!door||!valid())throw std::runtime_error("SLOT architectural door query unknown");if(*door){count(diagnostic.door_penalties);sample(Ts1SlotDiagnosticReason::door_penalty,point);}avatar_in_way|=*door;
                if(goal.chair.id){const auto chair_angle=angle(world,goal.chair);
                    auto difference=std::fmod(static_cast<double>(chair_angle-goal.radians)+pi,pi*2);if(difference<0)difference+=pi*2;difference-=pi;
                    if(std::abs(difference)>pi/4){count(diagnostic.chair_angle_rejected);sample(Ts1SlotDiagnosticReason::chair_angle,point,-1,goal.chair);return;}
                }else if(only_sit){count(diagnostic.chair_required_rejected);sample(Ts1SlotDiagnosticReason::chair_required,point);return;}
                score*=goal.chair.id?slot.sitting:slot.standing;
                if(!avatar_in_way){const auto conflict=services.destination_conflict(invocation,caller,goal);
                    if(!conflict||!valid())throw std::runtime_error("SLOT destination intersection unknown");if(*conflict){count(diagnostic.destination_conflicts);sample(Ts1SlotDiagnosticReason::destination_conflict,point);}avatar_in_way=*conflict;}
                if(avatar_in_way)score/=100000;
            }
            goal.score=score;result.goals.push_back(goal);count(diagnostic.accepted);
        };
        if(flags&4096U){
            if(facing>=0)flags|=1U<<static_cast<unsigned>(facing);else if(!(flags&255U))flags|=1;
            diagnostic.effective_flags=flags;
            const double mask=flags&255U;
            auto direction=std::fmod(rotation+even(std::log2(mask))*(pi/4),pi*2);if(direction<0)direction+=pi*2;if(direction>pi)direction-=pi*2;
            count(diagnostic.enumerated);verify(cx,cy,flags,std::numeric_limits<double>::max(),static_cast<float>(direction));
        }else if(on_point){count(diagnostic.enumerated);verify(cx,cy,flags,std::numeric_limits<double>::max(),{});}
        else{
            const auto spread=std::max(wrap32(static_cast<std::int64_t>(desired)-minimum),wrap32(static_cast<std::int64_t>(maximum)-desired));
            const auto travel=wrap32(static_cast<std::int64_t>(distance(target_start,source_position(world,caller)))+maximum)/3;
            const double max_score=wrap32(static_cast<std::int64_t>(wrap32(static_cast<std::int64_t>(spread)+travel))+2);
            const auto enumerate=[&](std::int32_t dx,std::int32_t dy,double range){
                count(diagnostic.enumerated);
                const float x=f(cx+f(dx/16.0f)),y=f(cy+f(dy/16.0f));
                if(range<minimum-0.5||range>maximum+0.5){count(diagnostic.proximity_rejected);return;}
                if(!(flags&2048U)){const EntityPosition room_point{narrow(f(x*16)),narrow(f(y*16)),source_position(world,target).level,0};
                    const auto room=services.room_at(room_point);
                    if(!room||!valid())throw std::runtime_error("SLOT candidate room unknown");
                    if(*room!=*target_room){count(diagnostic.room_rejected);sample(Ts1SlotDiagnosticReason::room,room_point);return;}}
                const auto entry=static_cast<std::uint32_t>(search(cx,cy,x,y,rotation))&flags;if(!entry){count(diagnostic.direction_rejected);sample(Ts1SlotDiagnosticReason::direction);return;}
                double score=max_score;
                if(!(flags&(1U<<29))){if(!services.random)throw std::runtime_error("SLOT score RNG unavailable");
                    const auto random=services.random(invocation,1024);if(!random||*random>=1024||!valid())throw std::runtime_error("SLOT score RNG unknown");
                    score=static_cast<float>(*random)/1024.0f;
                    if(!(flags&8192U))score+=max_score-std::abs(desired-range);}
                verify(x,y,entry,score,{});
            };
            if(raw[1]==raw[0]){
                if(limits.candidates<8)throw std::runtime_error("SLOT circle exceeds candidate budget");
                for(unsigned i=0;i<8;++i){const double a=i*pi/4;enumerate(static_cast<std::int32_t>(even(std::sin(a)*raw[0])),static_cast<std::int32_t>(even(std::cos(a)*raw[0])),raw[0]);}
            }else{
                const auto bound=(maximum/resolution)*resolution;
                const auto width=static_cast<std::uint64_t>(bound/resolution)*2+1;
                if(width>limits.candidates||width*width>limits.candidates)throw std::runtime_error("SLOT range exceeds candidate budget");
                for(std::int64_t x=-static_cast<std::int64_t>(bound);x<=bound;x+=resolution)
                    for(std::int64_t y=-static_cast<std::int64_t>(bound);y<=bound;y+=resolution){
                        const double range=(flags&(1U<<30))?static_cast<double>(std::max(std::abs(x),std::abs(y))):std::sqrt(static_cast<double>(x*x+y*y));
                        enumerate(static_cast<std::int32_t>(x),static_cast<std::int32_t>(y),range);}
            }
            std::stable_sort(result.goals.begin(),result.goals.end(),[](const auto& a,const auto& b){return a.score>b.score;});
        }
        if(!result.goals.empty()){result.no_valid_goals=false;result.blocker={};}
        if(!valid())throw std::runtime_error("SLOT source identity changed after selection");
        result.failure_code=!result.goals.empty()?0:failure==0?7:failure==1?10:failure==2?13:8;
        result.supported=true;
    }catch(const std::exception& error){result.supported=false;result.diagnostic=error.what();}
    diagnostic.supported=result.supported;diagnostic.no_valid_goals=result.no_valid_goals;
    diagnostic.failure_code=result.failure_code;diagnostic.blocker=result.blocker;
    if(services.diagnostics){try{
        diagnostic.diagnostic=result.diagnostic.substr(0,256);
        services.diagnostics(invocation,slot,target,caller,diagnostic);
    }catch(...){/* Observational reporting cannot change authored outcomes. */}}
    return result;
}
}
