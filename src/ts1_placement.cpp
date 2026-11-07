// SPDX-License-Identifier: MPL-2.0
// TS1 VMSnap, VMFindLocationFor, VMDrop; pinned FreeSO7e76c1960252ddd033d8937d5edb47d326f5b69c.
#include "odh/ts1_placement.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>
namespace odh {
namespace {
constexpr double pi=3.14159265358979323846;
PrimitiveOutcome bad(std::string why){return {PrimitiveDisposition::unsupported,"TS1 placement: "+std::move(why)};}
PrimitiveOutcome branch(bool yes){return {yes?PrimitiveDisposition::goto_true:PrimitiveDisposition::goto_false,{}};}
std::uint16_t word(const Instruction& i,unsigned offset){return static_cast<std::uint16_t>(i.operands[offset]|(i.operands[offset+1]<<8));}
std::int16_t wrap16(std::int64_t value){const auto bits=static_cast<std::uint16_t>(value);return bits<32768?static_cast<std::int16_t>(bits):static_cast<std::int16_t>(static_cast<int>(bits)-65536);}
std::int16_t wrap8(std::int32_t value){const auto bits=static_cast<std::uint8_t>(value);return bits<128?bits:static_cast<std::int16_t>(static_cast<int>(bits)-256);}
std::int32_t tile(std::int32_t value){return value>=0?value/16:-static_cast<std::int32_t>((-static_cast<std::int64_t>(value)+15)/16);}
EntityPosition big_tile(std::int32_t x,std::int32_t y,std::int16_t level,std::uint8_t direction){return {wrap16(static_cast<std::int64_t>(wrap16(x))*16+8),wrap16(static_cast<std::int64_t>(wrap16(y))*16+8),level,direction};}
Ts1PlacementIdentity identity(const World& world,std::int16_t id){const auto* entity=world.find(id);return {id,entity?entity->generation:0};}
bool live(const World& world,Ts1PlacementIdentity object){const auto* entity=world.find(object.id);return entity&&object.generation&&entity->generation==object.generation;}
std::optional<EntityPosition> source_position(const World& world,std::int16_t id){
    const auto* entity=world.find(id);if(!entity)return {};
    if(entity->position){const auto p=*entity->position;
        if(p.x<-32768||p.x>32767||p.y<-32768||p.y>32767||p.level<-128||p.level>127||p.direction>7)return {};
        return p;}
    VmFrame frame;frame.context.caller=id;const auto direction=world.read(frame,3,1);
    if(!direction||*direction<0||*direction>7)return {};
    return EntityPosition{-32768,-32768,1,static_cast<std::uint8_t>(*direction)};
}
bool result_valid(const World& world,const Ts1PlacementResult& result){
    return result.blocker.id?live(world,result.blocker):result.blocker.generation==0;
}
PrimitiveOutcome result_branch(const World& world,const Ts1PlacementResult& result){
    if(!result_valid(world,result)||result.status==Ts1PlacementStatus::unsupported)return bad(result.diagnostic.empty()?"placement capability unavailable or blocker changed":result.diagnostic);
    return branch(result.status==Ts1PlacementStatus::success);
}
std::uint8_t notch(float radians){
    const double modulus=static_cast<float>(pi)*2.0f;
    double a=std::fmod(static_cast<double>(radians),modulus);if(a<0)a+=modulus;
    const auto raw=a/(pi/4),low=std::floor(raw),fraction=raw-low;
    auto rounded=static_cast<unsigned>(low);if(fraction>0.5||(fraction==0.5&&(rounded&1U)))++rounded;
    return static_cast<std::uint8_t>(rounded%8);
}
}
Ts1PlacementResult ts1_find_location_radial(const Ts1RadialPlacementServices& services,bool prefer_non_empty){
    const auto bad=[](const char* why){return Ts1PlacementResult{Ts1PlacementStatus::unsupported,-1,{},why};};
    if(!services.source_still_matches||!services.reference_position||!services.place||
       (prefer_non_empty&&!services.tile_occupied)||!services.source_still_matches())return bad("radial source placement capability unavailable");
    std::vector<EntityPosition> deferred;deferred.reserve(1524);
    std::optional<Ts1PlacementResult> terminal;
    const auto evaluate=[&](EntityPosition point){
        if(terminal)return;
        if(!services.source_still_matches()){terminal=bad("radial source identity changed");return;}
        if(prefer_non_empty){const auto occupied=services.tile_occupied(point);
            if(!occupied||!services.source_still_matches()){terminal=bad("radial occupancy unavailable");return;}
            if(*occupied){deferred.push_back(point);return;}}
        auto result=services.place(point);
        if(!services.source_still_matches())result=bad("radial source changed during placement");
        if(result.status!=Ts1PlacementStatus::blocked)terminal=std::move(result);
    };
    for(int radius=0;radius<10&&!terminal;++radius){
        const auto ref=services.reference_position();
        if(!ref||!services.source_still_matches())return bad("radial reference position unavailable");
        if(radius==0){for(unsigned n=0;n<4&&!terminal;++n)evaluate({ref->x,ref->y,ref->level,static_cast<std::uint8_t>(n*2)});}
        else{
            for(int x=-radius;x<=radius&&!terminal;++x)for(int j=0;j<8&&!terminal;++j)
                evaluate(big_tile(tile(ref->x)+x,tile(ref->y)+((j%2)*2-1)*radius,ref->level,static_cast<std::uint8_t>((j/2)*2)));
            for(int y=1-radius;y<radius&&!terminal;++y)for(int j=0;j<8&&!terminal;++j)
                evaluate(big_tile(tile(ref->x)+((j%2)*2-1)*radius,tile(ref->y)+y,ref->level,static_cast<std::uint8_t>((j/2)*2)));
        }
    }
    if(terminal)return *terminal;
    for(const auto point:deferred){
        if(!services.source_still_matches())return bad("radial deferred source identity changed");
        auto result=services.place(point);
        if(!services.source_still_matches())return bad("radial deferred source changed during placement");
        if(result.status!=Ts1PlacementStatus::blocked)return result;
    }
    return {Ts1PlacementStatus::blocked,7,{},{}};
}
std::optional<float> ts1_storage_slot_height(const SlotRecord& slot){
    if(!slot.height)return {};
    if(*slot.height==5){float z;const auto bits=slot.offset_bits[2];std::memcpy(&z,&bits,sizeof z);return std::isfinite(z)?std::optional<float>{z}:std::nullopt;}
    constexpr float offsets[]={0,2.5f,4,4,0,0,7,4,0};
    return *slot.height>=1&&*slot.height<=9?std::optional<float>{offsets[*slot.height-1]}:std::nullopt;
}
Ts1PlacementResult Ts1Placement::transfer(const Ts1PlacementInvocation& invocation,Ts1PlacementIdentity container,
    std::size_t slot,Ts1PlacementIdentity item){
    if(!invocation.source_still_matches||!invocation.source_still_matches()||!live(world_,container)||!live(world_,item)||!services_.transfer)
        return {Ts1PlacementStatus::unsupported,-1,{},"slot transfer identity or capability unavailable"};
    auto result=services_.transfer(invocation,container,slot,item);
    if(!invocation.source_still_matches()||!live(world_,container)||!live(world_,item)||!result_valid(world_,result))
        return {Ts1PlacementStatus::unsupported,-1,{},"slot transfer source identity changed"};
    return result;
}
VmPrimitive Ts1Placement::primitive(){return [this](const VmFrame& frame,const Instruction& instruction,
    const Ts1Temps&,const VmDispatch& dispatch)->PrimitiveOutcome{
    if(instruction.opcode!=5&&instruction.opcode!=16&&instruction.opcode!=46)return {};
    if(executing_||!dispatch.origin||!dispatch.origin->matches_active_primitive(frame,instruction,dispatch))return bad("actual ordinary primitive dispatch required");
    const auto caller=identity(world_,frame.context.caller);if(!live(world_,caller))return bad("Caller identity unavailable");
    const auto still=[&,caller]{return live(world_,caller)&&dispatch.origin->matches_active_primitive(frame,instruction,dispatch);};
    const Ts1PlacementInvocation invocation{frame,instruction,dispatch,still};
    struct Guard {bool& value;explicit Guard(bool& v):value(v){v=true;}~Guard(){value=false;}} guard(executing_);
    try{
        if(instruction.opcode==5)return drop(invocation);
        if(instruction.opcode==16)return find_location(invocation);
        return snap(invocation);
    }catch(const std::exception& error){return bad(error.what());}
};}
PrimitiveOutcome Ts1Placement::drop(const Ts1PlacementInvocation& invocation){
    const auto caller=identity(world_,invocation.frame.context.caller);
    const auto* actor=world_.find(caller.id);
    const auto count=world_.read(invocation.frame,3,67);if(!count)return bad("Caller total slots unavailable");
    if(*count==0)return branch(false);
    const auto hand=world_.read(invocation.frame,20,0);
    if(!hand)return bad("Caller hand-slot facts unavailable");
    if(!*hand)return branch(false);
    const auto item=identity(world_,*hand);if(!live(world_,item))return bad("held object identity unavailable");
    const auto position=source_position(world_,caller.id);if(!position||!actor)return bad("Caller source position/direction unavailable");
    if(!services_.place)return bad("default group placement unavailable");
    constexpr int dx[]={0,16,16,16,0,-16,-16,-16},dy[]={-16,-16,0,16,16,16,0,-16};
    const auto base=big_tile(tile(position->x),tile(position->y),position->level,position->direction);
    for(unsigned i=0;i<8;++i){
        const auto j=(i&1U)?7-i/2:i/2;const auto n=(j+position->direction)%8;
        const EntityPosition destination{wrap16(base.x+dx[n]),wrap16(base.y+dy[n]),base.level,position->direction};
        const auto result=services_.place(invocation,item,destination,0);
        if(!invocation.source_still_matches()||!live(world_,item))return bad("Drop source identity changed during placement");
        if(result.status==Ts1PlacementStatus::unsupported||!result_valid(world_,result))return result_branch(world_,result);
        if(result.status==Ts1PlacementStatus::success)return branch(true);
    }
    return branch(false);
}
PrimitiveOutcome Ts1Placement::find_location(const Ts1PlacementInvocation& invocation){
    const auto& instruction=invocation.instruction;const auto& frame=invocation.frame;
    const auto mode=instruction.operands[0];
    const auto target=identity(world_,frame.context.stack_object);
    std::int16_t reference_id=frame.context.caller;
    if(instruction.operands[2]&1U){
        if(instruction.operands[1]>=frame.locals.size())return bad("reference Local index is outside actual frame");
        reference_id=frame.locals[instruction.operands[1]];
    }
    if(mode>5)return branch(false);
    if(!live(world_,target)||!services_.place)return bad("FindLocationFor target/placement unavailable");
    const auto reference=identity(world_,reference_id);
    const bool uses_reference=mode==0||mode==3||mode==4||mode==5;
    if(uses_reference&&!live(world_,reference))return bad("FindLocationFor reference is unavailable");
    const auto flags=static_cast<std::uint8_t>(1U|((instruction.operands[2]&4U)?2U:0U));
    const bool prefer_non_empty=(instruction.operands[2]&2U)==0;
    const auto place=[&](std::optional<EntityPosition> destination,std::uint8_t request_flags)->Ts1PlacementResult{
        if(!invocation.source_still_matches()||!live(world_,target)||(uses_reference&&!live(world_,reference)))return {Ts1PlacementStatus::unsupported,-1,{},"FindLocationFor source identity changed"};
        auto result=services_.place(invocation,target,destination,request_flags);
        if(!invocation.source_still_matches()||!live(world_,target)||(uses_reference&&!live(world_,reference)))return {Ts1PlacementStatus::unsupported,-1,{},"FindLocationFor placement changed its source identity"};
        return result;
    };
    if(mode==1){const auto result=place({},flags);if(result.status==Ts1PlacementStatus::unsupported||!result_valid(world_,result))return result_branch(world_,result);return branch(true);}
    if(mode==2){
        const auto caller=source_position(world_,frame.context.caller),callee=source_position(world_,frame.context.callee);
        if(!caller||!callee)return bad("smoke midpoint Caller/Callee position unavailable");
        // Source LotTilePos operators narrow AFTER addition, then integer divide.
        EntityPosition point{wrap16(wrap16(callee->x+caller->x)/2-8),wrap16(wrap16(callee->y+caller->y)/2-8),
            wrap8(wrap8(callee->level+caller->level)/2),0};
        return result_branch(world_,place(point,0));
    }
    if(mode==5){
        if(!services_.lot_size||!services_.random)return bad("random placement geometry/RNG unavailable");
        const auto size=services_.lot_size(invocation);if(!size||size->first<=2||size->second<=2)return bad("random placement requires source interior dimensions");
        for(unsigned i=0;i<100;++i){
            const auto x=services_.random(invocation,static_cast<std::uint64_t>(size->first-2));
            if(!x||*x>=static_cast<std::uint64_t>(size->first-2)||!invocation.source_still_matches())return bad("random X source unavailable");
            const auto y=services_.random(invocation,static_cast<std::uint64_t>(size->second-2));
            const auto ref=source_position(world_,reference.id);
            if(!y||*y>=static_cast<std::uint64_t>(size->second-2)||!ref||!invocation.source_still_matches())return bad("random Y/reference source unavailable");
            const auto result=place(big_tile(static_cast<std::int32_t>(*x+1),static_cast<std::int32_t>(*y+1),ref->level,0),flags);
            if(result.status!=Ts1PlacementStatus::blocked||!result_valid(world_,result))return result_branch(world_,result);
        }
        return branch(false);
    }
    if(mode==0){
        Ts1RadialPlacementServices radial;
        radial.source_still_matches=[&]{return invocation.source_still_matches()&&live(world_,target)&&live(world_,reference);};
        radial.reference_position=[&]{return source_position(world_,reference.id);};
        radial.place=[&](EntityPosition point){auto result=place(point,flags);
            if(!result_valid(world_,result))return Ts1PlacementResult{Ts1PlacementStatus::unsupported,-1,{},"radial blocker identity changed"};
            return result;};
        if(services_.tile_occupied)radial.tile_occupied=[&](EntityPosition point){return services_.tile_occupied(invocation,point);};
        return result_branch(world_,ts1_find_location_radial(radial,prefer_non_empty));
    }
    if(mode>5)return branch(false);
    if(prefer_non_empty&&!services_.tile_occupied)return bad("source tile occupancy service unavailable");
    std::vector<EntityPosition> deferred;deferred.reserve(1524);
    std::optional<PrimitiveOutcome> terminal;
    const auto evaluate=[&](EntityPosition position){
        if(terminal)return;
        if(prefer_non_empty){const auto occupied=services_.tile_occupied(invocation,position);
            if(!occupied||!invocation.source_still_matches()){terminal=bad("source occupancy facts unavailable");return;}
            // Preserve source quirk: occupied tiles are DEFERRED despite the flag name.
            if(*occupied){deferred.push_back(position);return;}}
        const auto result=place(position,flags);
        if(result.status!=Ts1PlacementStatus::blocked||!result_valid(world_,result))terminal=result_branch(world_,result);
    };
    if(mode==3||mode==4){
        const auto initial=source_position(world_,reference.id);if(!initial)return bad("vector reference direction unavailable");
        const auto direction=static_cast<std::uint8_t>((initial->direction+(mode==4?2:0))%8);
        constexpr int dx[]={0,-16,-16,-16,0,16,16,16},dy[]={16,16,0,-16,-16,-16,0,16};
        for(int i=0;i<32&&!terminal;++i){
            auto ref=source_position(world_,reference.id);if(!ref)return bad("vector reference position unavailable");
            evaluate({wrap16(ref->x+dx[direction]*(i/2)),wrap16(ref->y+dy[direction]*(i/2)),ref->level,direction});
            if((i&1)&&!terminal){ref=source_position(world_,reference.id);if(!ref)return bad("vector reference position unavailable");
                evaluate({wrap16(ref->x-dx[direction]*(i/2)),wrap16(ref->y-dy[direction]*(i/2)),ref->level,direction});}
        }
    }
    if(terminal)return *terminal;
    for(const auto position:deferred){const auto result=place(position,flags);
        if(result.status!=Ts1PlacementStatus::blocked||!result_valid(world_,result))return result_branch(world_,result);}
    return branch(false);
}
PrimitiveOutcome Ts1Placement::snap(const Ts1PlacementInvocation& invocation){
    const auto& frame=invocation.frame;const auto& instruction=invocation.instruction;
    const auto caller=identity(world_,frame.context.caller),target=identity(world_,frame.context.stack_object);
    const auto mode=word(instruction,2);std::size_t index=word(instruction,0);
    if(mode>4)return branch(false);
    if(!live(world_,target))return bad("Snap StackObject identity unavailable");
    if(mode==1)return result_branch(world_,transfer(invocation,target,0,caller));
    SlotRecord slot{};slot.type=3;slot.standing=1;slot.snap_target_slot=-1;slot.facing=-2;slot.resolution=16;
    if(mode==2){slot.flags=1;slot.proximity=std::array<std::int32_t,3>{16,0,0};slot.max_size=100;slot.height=0;slot.gradient_bits=0;slot.unknown_i10=0;}
    else if(mode==0||mode==3||mode==4){
        if(mode==0){if(index>=frame.arguments.size())return bad("Snap StackVariable index outside actual Args");
            if(frame.arguments[index]<0)return bad("Snap StackVariable resolves a negative SLOT index");
            index=static_cast<std::size_t>(frame.arguments[index]);}
        if(!services_.slots)return bad("Snap SLOT resource service unavailable");
        const auto lookup=services_.slots(invocation,target,mode==4);
        if(!invocation.source_still_matches()||!live(world_,target))return bad("Snap source identity changed during SLOT lookup");
        if(lookup.status==Ts1SlotLookupStatus::missing)return mode==4?branch(false):bad("private SLOT table is absent");
        if(lookup.status!=Ts1SlotLookupStatus::available||!lookup.table)return bad(lookup.diagnostic.empty()?"Snap SLOT resource unknown":lookup.diagnostic);
        const auto group=std::find_if(lookup.table->records.begin(),lookup.table->records.end(),[](const auto& record){return record.type==3;});
        if(group==lookup.table->records.end())return mode==4?branch(false):bad("private SLOT table has no routing group");
        const auto* selected=slot_of_type(*lookup.table,3,index);if(!selected)return branch(false);slot=*selected;
    }else return branch(false);
    if(!services_.locations)return bad("shared VMSlotParser service unavailable");
    const auto locations=services_.locations(invocation,slot,target,caller);
    if(!invocation.source_still_matches()||!live(world_,target))return bad("Snap source identity changed during candidate selection");
    if(!locations.supported)return bad(locations.diagnostic.empty()?"Snap SLOT parser capability unavailable":locations.diagnostic);
    if(slot.snap_target_slot>=0){
        const auto result=transfer(invocation,target,static_cast<std::size_t>(slot.snap_target_slot),caller);
        if(result.status!=Ts1PlacementStatus::success)return result_branch(world_,result);
        if(!locations.goals.empty()&&(slot.flags&4096U)&&!world_.set_radian_direction(caller.id,locations.goals.front().radians))return bad("contained Snap direction publication failed");
        return branch(true);
    }
    const auto failure=[&](bool no_goals,Ts1PlacementIdentity blocker)->PrimitiveOutcome{
        if((blocker.id&&!live(world_,blocker))||!world_.read(frame,3,54)||(no_goals&&!world_.read(frame,3,52)))return bad("Snap source failure banks/blocker unavailable");
        if(no_goals&&!world_.write(frame,3,52,2))return bad("Snap PrimitiveResult publication rejected");
        if(!world_.write(frame,3,54,blocker.id))return bad("Snap PrimitiveResultID publication rejected");
        return branch(false);
    };
    if(locations.goals.empty())return failure(locations.no_valid_goals,locations.blocker);
    if(!services_.place)return bad("Snap final placement service unavailable");
    const auto& goal=locations.goals.front();if(!std::isfinite(goal.radians))return bad("Snap selected nonfinite direction");
    auto destination=goal.position;
    const auto* actor=world_.find(caller.id);if(!actor)return bad("Snap Caller was removed");
    const float radians=(slot.flags&4096U)?goal.radians:actor->radian_direction;
    destination.direction=notch(radians);
    if(caller.id==target.id&&actor->kind==EntityKind::object){
        const auto current=source_position(world_,caller.id);
        if(!current)return bad("self Snap source position unavailable");
        if(current->x!=destination.x||current->y!=destination.y||current->level!=destination.level)
            if(!services_.moved_self||!services_.moved_self(invocation,caller))return bad("Snap MovedSelf group capability unavailable");
    }
    const auto result=services_.place(invocation,caller,destination,0);
    if(!invocation.source_still_matches()||!live(world_,caller)||!result_valid(world_,result))return bad("Snap final placement source/blocker identity changed");
    if(result.status==Ts1PlacementStatus::unsupported)return result_branch(world_,result);
    if(result.status==Ts1PlacementStatus::blocked){
        const auto* blocker=world_.find(result.blocker.id);
        if((instruction.operands[4]&2U)&&blocker&&blocker->kind==EntityKind::avatar)
            if(!services_.shoo||!services_.shoo(invocation,result.blocker))return bad("Snap Shoo queue/creation service unavailable");
        return failure(result.placement_error==0,result.blocker);
    }
    actor=world_.find(caller.id);
    if(actor->kind==EntityKind::avatar&&!world_.set_radian_direction(caller.id,radians))return bad("Snap continuous facing publication rejected");
    return branch(true);
}
}
