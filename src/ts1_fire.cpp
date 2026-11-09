#include "odh/ts1_fire.hpp"
#include <deque>
#include <set>
#include <limits>

namespace odh {
namespace {
std::int16_t short_value(std::int32_t value) {
    const auto bits=static_cast<std::uint16_t>(value);
    return static_cast<std::int16_t>(bits<32768U?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536);
}
std::int32_t tile(std::int32_t value) {
    return value>=0?value/16:static_cast<std::int32_t>((static_cast<std::int64_t>(value)-15)/16);
}
bool out_of_world(const EntityPosition& p) {
    return p.x==-32768&&p.y==-32768&&p.level==1;
}
}
PrimitiveOutcome execute_ts1_burn(const World& world,Ts1Clock& clock,const VmFrame& frame,
        const Instruction& instruction,const VmDispatch& dispatch,const Ts1FireServices& services) {
    const auto fail=[&](const std::string& why){
        return PrimitiveOutcome{PrimitiveDisposition::unsupported,
            "TS1 Burn type="+std::to_string(instruction.operands[0])+
            " caller="+std::to_string(frame.context.caller)+"/gen"+std::to_string(frame.context.caller_generation)+
            " target="+std::to_string(frame.context.stack_object)+"/gen"+std::to_string(frame.context.stack_object_generation)+
            " run="+std::to_string(dispatch.origin?dispatch.origin->run_generation():0)+
            " ticket="+std::to_string(dispatch.ticket)+" fire_percent="+std::to_string(clock.snapshot().fire_percent)+
            ": "+why};
    };
    const auto branch=[](bool made){return PrimitiveOutcome{made?PrimitiveDisposition::goto_true:PrimitiveDisposition::goto_false,{}};};
    const auto current=[&]{return services.source_current&&services.source_current()&&dispatch.origin&&
        services.instruction_lease&&services.instruction_lease->origin()==dispatch.origin&&
        services.instruction_lease->matches(frame,instruction,dispatch);};
    if(instruction.opcode!=9||!current())return fail("requires its authenticated actual source instruction lease");
    try {
        const bool spread=instruction.operands[0]!=0;
        // BurnBusyObjects (byte1) is serialized by the source operand but is
        // never consulted by Execute; it grants no busy-object override.
        if(spread){
            if(!services.random)return fail("shared random source unavailable");
            const auto draw=services.random(10000);
            if(!draw||*draw>=10000)return fail("shared random result unavailable or outside bound");
            if(!current())return fail("source changed during random draw");
            if(static_cast<std::int32_t>(*draw)>=clock.snapshot().fire_percent)return branch(false);
        }
        const auto* caller=world.frame_entity(frame,VmEntityRole::caller,true);
        if(!caller||!services.caller_in_pool)return fail("exact caller or pool service unavailable");
        const auto pool=services.caller_in_pool({caller->id,caller->generation});
        if(!pool||!current())return fail("caller pool classification unavailable");
        if(*pool)return branch(false);
        if(spread)clock.consume_fire_spread();
        // A fresh/reachable native clock is nonnegative. Type0 also performs
        // source's clamp, which is consequently an identity operation here.
        const auto* anchor=world.frame_entity(frame,VmEntityRole::stack_object);
        if(!anchor||!services.position)return fail("exact StackObject Position unavailable");
        auto initial=services.position({anchor->id,anchor->generation});
        if(!initial||!current())return fail("StackObject Position unavailable");
        if(initial->x<INT16_MIN||initial->x>INT16_MAX||initial->y<INT16_MIN||initial->y>INT16_MAX||
           initial->level<INT8_MIN||initial->level>INT8_MAX)
            return fail("StackObject Position outside source short/sbyte domain");
        if(instruction.operands[0]==1){
            if(initial->direction>7)return fail("StackObject cardinal direction unavailable");
            switch(initial->direction){
            case 0:initial->y=short_value(initial->y-16);break;
            case 2:initial->x=short_value(initial->x+16);break;
            case 4:initial->y=short_value(initial->y+16);break;
            case 6:initial->x=short_value(initial->x-16);break;
            default:break; // Odd/diagonal source directions leave Position alone.
            }
        }
        if(out_of_world(*initial))return branch(false);
        if(!services.width||!services.height||services.width>64||services.height>64||
           !services.stories||services.stories>2)return fail("bounded lot geometry unavailable");
        const auto in_bounds=[&](const EntityPosition& point){
            return point.level>=1&&point.level<=services.stories&&tile(point.x)>=0&&tile(point.y)>=0&&
                static_cast<std::uint32_t>(tile(point.x))<services.width&&
                static_cast<std::uint32_t>(tile(point.y))<services.height;
        };
        if(!in_bounds(*initial))return fail("source initial tile is outside safe lot bounds");
        if(!services.objects_at||!services.group||!services.in_use||!services.set_flags||!services.create)
            return fail("complete query/group/lifecycle/constructor services unavailable");
        const auto live=[&](WorldEntityReference ref)->const Entity*{
            const auto* body=world.find(ref.id);
            return body&&body->generation==ref.generation&&world.is_live_reference(ref)&&!body->ui_ghost?body:nullptr;
        };
        const auto query=[&](const EntityPosition& point)->std::optional<std::vector<WorldEntityReference>>{
            auto refs=services.objects_at(point);
            if(!refs||refs->size()>4096||!current())return {};
            std::int16_t previous=0;
            for(const auto ref:*refs){
                const auto* body=live(ref);
                if(!body||!body->definition||ref.id<=previous)return {};
                previous=ref.id;
            }
            return refs;
        };
        const auto has_fire=[&](const std::vector<WorldEntityReference>& refs){
            for(const auto ref:refs)if(live(ref)->definition->guid==ts1_fire_guid)return true;
            return false;
        };
        const auto construct=[&](const EntityPosition& point,std::string& error){
            EntityPosition centered{tile(point.x)*16+8,tile(point.y)*16+8,point.level,0};
            return services.create(centered,error)&&current();
        };
        std::vector<bool> visited(static_cast<std::size_t>(services.width)*services.height,false);
        std::deque<EntityPosition> queue{*initial};
        bool first=true,made=false;
        const auto budget=visited.size()+1;
        std::size_t iterations=0;
        while(!queue.empty()){
            if(++iterations>budget)return fail("source group spread exceeds bounded tile budget");
            const auto point=queue.front();queue.pop_front();
            if(out_of_world(point))continue;
            if(!in_bounds(point)||!current())return fail("queued source tile or authority unavailable");
            auto at=query(point);if(!at)return fail("source tile query is incomplete or stale");
            if(first&&!has_fire(*at)){
                made=true;
                // GetObjectsAt returns the live list. The first Create occurs
                // before foreach begins. If that list existed, its inserted
                // Fire participates in the subsequent iteration; an absent
                // bucket uses a distinct temporary empty list.
                const bool had_list=!at->empty();std::string error;
                if(!construct(*initial,error))return fail("initial Fire construction: "+error);
                if(had_list){at=query(point);if(!at)return fail("initial source list changed without query evidence");}
            }
            first=false;
            for(const auto ref:*at){
                const auto* object=live(ref);if(!object)return fail("queried object identity retired during Burn");
                const auto busy=services.in_use(ref);
                if(!busy||!current())return fail("multitile IsInUse evidence unavailable");
                object=live(ref);if(!object)return fail("queried object identity changed during IsInUse");
                if(*busy)continue;
                if(object->kind==EntityKind::avatar){
                    if(object->person_data.size()<=68)return fail("avatar ghost state unavailable");
                    if(object->person_data[68]>0)continue;
                }
                const auto group=services.group(ref);
                if(!group||group->empty()||group->size()>4096||!current())return fail("physical group unavailable");
                std::set<WorldEntityReference> unique;
                for(const auto member:*group)
                    if(!live(member)||!unique.insert(member).second)return fail("physical group identity stale or duplicated");
                if(!unique.count(ref))return fail("physical group omitted queried object");
                for(const auto member:*group){
                    const auto* part=live(member);
                    if(!part)return fail("physical group identity retired during Burn");
                    const auto pose=services.position(member);
                    if(!pose||!current())return fail("physical part Position unavailable");
                    if(pose->level!=initial->level)continue;
                    const auto flags2=part->object_words.find(40),flags=part->object_words.find(8);
                    if(flags2==part->object_words.end()||flags==part->object_words.end())return fail("physical part burn flags unavailable");
                    if(!(static_cast<std::uint16_t>(flags2->second)&32U)||
                        (static_cast<std::uint16_t>(flags->second)&2048U))continue;
                    const auto burning=static_cast<std::int16_t>(static_cast<std::uint16_t>(flags->second)|512U);
                    std::string error;
                    if(!services.set_flags(member,burning,error)||!current())return fail("Burning flag lifecycle: "+error);
                    if(!live(member))return fail("burning physical body retired during Reset");
                    const auto fire_pose=services.position(member);
                    if(!fire_pose||!current()||!in_bounds(*fire_pose))return fail("burning physical tile outside safe lot bounds");
                    const auto index=static_cast<std::size_t>(tile(fire_pose->x))+
                        static_cast<std::size_t>(tile(fire_pose->y))*services.width;
                    if(visited[index])continue;
                    const auto existing=query(*fire_pose);if(!existing)return fail("Fire duplicate query unavailable");
                    if(has_fire(*existing)){visited[index]=true;continue;}
                    queue.push_back(*fire_pose);made=true;
                    if(!construct(*fire_pose,error))return fail("group Fire construction: "+error);
                    visited[index]=true;
                }
            }
        }
        return branch(made);
    }catch(const std::exception& error){return fail(error.what());}
}
} // namespace odh
