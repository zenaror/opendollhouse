#include "odh/npc_avatar.hpp"
#include "odh/household_spawn.hpp"
#include <utility>

namespace odh {
namespace {
NeighborRecord inheritance_record(const NeighborRecord& source) {
    NeighborRecord result{};
    result.version=source.version;result.neighbor_id=source.neighbor_id;result.guid=source.guid;
    result.person_mode=source.person_mode;result.person_data=source.person_data;
    result.native_personal=source.native_personal;
    return result;
}
bool valid_neighbor(const NeighborRecord& n,std::uint32_t guid) {
    return n.guid==guid && (n.version==4 || n.version==10) &&
        ((n.person_mode>0)==n.person_data.has_value()) &&
        (!n.person_data || n.person_data->size()==(n.version==4?80U:256U));
}
bool valid_request(const NpcAvatarRequest& request) {
    const bool captured=request.direct_placement&&
        request.direct_placement->status==CreateObjectPlacementStatus::resolved&&
        request.direct_placement->position==request.creation.position()&&
        request.direct_placement->caller.id==request.caller&&
        request.direct_placement->caller.generation==request.caller_generation&&
        !request.direct_placement->container&&request.direct_placement->direction<=7&&
        (!request.direct_placement->destination||
         (request.direct_placement->destination->x==request.position.x&&
          request.direct_placement->destination->y==request.position.y&&
          request.direct_placement->destination->level==request.position.level&&
          request.direct_placement->destination->direction==request.position.direction));
    const bool direct=request.direct_resource_guid&&request.creation.guid==request.direct_resource_guid&&
        !request.creation.use_neighbor()&&(request.creation.flags&static_cast<std::uint8_t>(~0x3bU))==0&&captured;
    const bool neighbor=!request.direct_resource_guid&&request.creation.use_neighbor()&&
        (request.creation.flags&static_cast<std::uint8_t>(~0x3fU))==0&&captured;
    // Generic19/20 call VMContext directly, without primitive42's UseNeighbor
    // post-construction inheritance, captured placement or flag arguments.
    const bool out_of_world=!request.direct_resource_guid&&!request.direct_placement&&
        request.creation.position()==CreateObjectPosition::out_of_world&&request.creation.flags==0&&
        request.creation.guid==request.guid()&&request.constructor_neighbor&&
        request.constructor_neighbor->neighbor_id==request.requested_neighbor.neighbor_id;
    const bool pass_ids=(direct||neighbor)&&request.creation.pass_object_ids();
    return request.caller>0&&request.caller_generation&&(direct||neighbor||out_of_world)&&
        request.main_stack_object>=0&&(pass_ids||request.main_stack_object==0)&&
        (request.creation.pass_temp0()||request.main_param==(pass_ids?request.caller:0))&&
        (direct||valid_neighbor(request.requested_neighbor,request.guid()))&&
        (!request.constructor_neighbor||valid_neighbor(*request.constructor_neighbor,request.guid()));
}
bool capture_request_placement(NpcAvatarRequest& request,const VmFrame& frame,
    const Ts1Temps& temps,const World& world,std::string& diagnostic) {
    const auto* caller=world.frame_entity(frame,VmEntityRole::caller,true);
    if(!caller||caller->ui_ghost){diagnostic="avatar constructor requires its live Caller";return false;}
    request.caller=caller->id;request.caller_generation=caller->generation;
    auto plan=capture_create_object_placement(world,frame,request.creation);
    if(plan.status!=CreateObjectPlacementStatus::resolved||plan.container) {
        diagnostic="avatar requires resolved common non-container placement (status "+
            std::to_string(static_cast<unsigned>(plan.status))+")";return false;
    }
    if(plan.destination)request.position=*plan.destination;
    request.position.direction=plan.direction;
    request.direct_placement=std::move(plan);
    request.main_param=request.creation.pass_temp0()?temps[0]:
        (request.creation.pass_object_ids()?caller->id:std::int16_t{0});
    // Capture the raw source ID, not a generation-bound reference. VMEntity.Init
    // resolves it only after synchronous Init/EP8, observing deletion or ID reuse.
    if(request.creation.pass_object_ids())
        if(const auto* stack=world.frame_entity(frame,VmEntityRole::stack_object))request.main_stack_object=stack->id;
    return true;
}
bool caller_present(const World& world,const NpcAvatarRequest& request) {
    const auto* caller=world.find(request.caller);
    return caller && caller->generation==request.caller_generation && !caller->ui_ghost;
}
bool avatar_present(const World& world,std::int16_t id,std::uint64_t generation,std::uint32_t guid) {
    const auto* avatar=world.find(id);
    return avatar && avatar->kind==EntityKind::avatar && avatar->generation==generation && avatar->definition &&
        avatar->definition->guid==guid && !avatar->ui_ghost;
}
bool same_entries(const Entity& actor,const EntryPointTable& entries) {
    if(!actor.entry_points || actor.entry_points->size()!=entries.entries.size())return false;
    for(std::size_t i=0;i<entries.entries.size();++i)
        if((*actor.entry_points)[i].condition!=entries.entries[i].condition ||
           (*actor.entry_points)[i].action!=entries.entries[i].action)return false;
    return true;
}
}
NpcAvatarRequestResult resolve_npc_avatar_request(const CreateObjectInstanceRequest& creation,
    const VmFrame& frame,const Ts1Temps& temps,const NeighborContext& neighbors,const World& world) {
    NpcAvatarRequestResult result;
    if(!creation.use_neighbor() || (creation.flags&static_cast<std::uint8_t>(~0x3fU))!=0) {
        result.diagnostic="neighbor avatar requires UseNeighbor and supported non-container flags";return result;
    }
    NpcAvatarRequest request;request.creation=creation;
    if(!capture_request_placement(request,frame,temps,world,result.diagnostic))return result;
    const auto* neighbor=neighbors.by_id(frame.context.stack_object);
    if(!neighbor) {
        result.status=NpcAvatarRequestStatus::missing_neighbor;
        result.diagnostic="NPC request neighbor is absent";return result;
    }
    request.requested_neighbor=inheritance_record(*neighbor);
    if(const auto selected=neighbors.id_for_guid(neighbor->guid)) {
        if(const auto* by_guid=neighbors.by_id(*selected))request.constructor_neighbor=inheritance_record(*by_guid);
    }
    result.status=preflight_npc_avatar_creation(request,world,&result.diagnostic);
    if(result.status==NpcAvatarRequestStatus::ready)result.request=std::move(request);
    return result;
}
NpcAvatarRequestResult resolve_npc_avatar_guid_request(const CreateObjectInstanceRequest& creation,
    const VmFrame& frame,const Ts1Temps& temps,const NeighborContext& neighbors,const World& world) {
    NpcAvatarRequestResult result;
    if(!creation.guid||creation.use_neighbor()) {
        result.diagnostic="literal avatar constructor requires its resource GUID without UseNeighbor";return result;
    }
    NpcAvatarRequest request;request.creation=creation;request.direct_resource_guid=creation.guid;
    if(!capture_request_placement(request,frame,temps,world,result.diagnostic))return result;
    if(const auto nid=neighbors.id_for_guid(creation.guid))
        if(const auto* neighbor=neighbors.by_id(*nid))request.constructor_neighbor=inheritance_record(*neighbor);
    result.status=preflight_npc_avatar_creation(request,world,&result.diagnostic);
    if(result.status==NpcAvatarRequestStatus::ready)result.request=std::move(request);
    return result;
}
NpcAvatarRequestStatus preflight_npc_avatar_creation(const NpcAvatarRequest& request,
    const World& world,std::string* diagnostic) {
    const auto finish=[&](NpcAvatarRequestStatus status,const char* why){
        if(diagnostic)*diagnostic=why;
        return status;
    };
    if(!valid_request(request)||!caller_present(world,request))
        return finish(NpcAvatarRequestStatus::unsupported,"avatar constructor requires a valid supported request and its live Caller");
    if(request.direct_placement) {
        auto effective=request.creation;effective.guid=request.guid(); // UseNeighbor substitutes the NBRS GUID first.
        const auto duplicate=create_object_instance_has_duplicate(world,effective,*request.direct_placement);
        if(!duplicate)return finish(NpcAvatarRequestStatus::unsupported,"avatar NoDuplicate destination/definition is unknown");
        if(*duplicate)return finish(NpcAvatarRequestStatus::duplicate,"avatar NoDuplicate found the same GUID on its destination tile");
    }
    return finish(NpcAvatarRequestStatus::ready,"");
}
NpcAvatarRequestResult resolve_npc_avatar_out_of_world_request(std::int16_t nid,
    const VmFrame& frame,const NeighborContext& neighbors,const World& world) {
    NpcAvatarRequestResult result;
    const auto* caller=world.frame_entity(frame,VmEntityRole::caller,true);
    if(!caller||caller->ui_ghost){result.diagnostic="OOW avatar constructor needs a live caller";return result;}
    const auto* requested=neighbors.by_id(nid);
    if(!requested){result.status=NpcAvatarRequestStatus::missing_neighbor;result.diagnostic="companion token NID is absent";return result;}
    const auto constructor_nid=neighbors.id_for_guid(requested->guid);
    const auto* constructor=constructor_nid?neighbors.by_id(*constructor_nid):nullptr;
    if(!constructor){result.diagnostic="OOW avatar constructor GUID has no neighbor identity";return result;}
    NpcAvatarRequest request;
    request.creation={requested->guid,static_cast<std::uint8_t>(CreateObjectPosition::out_of_world),0,0,0};
    request.caller=caller->id;request.caller_generation=caller->generation;
    request.requested_neighbor=inheritance_record(*constructor);request.constructor_neighbor=request.requested_neighbor;
    if(!valid_request(request)){result.diagnostic="OOW avatar constructor neighbor data is unsupported";return result;}
    result.status=NpcAvatarRequestStatus::ready;result.request=std::move(request);return result;
}
bool materialize_npc_avatar(World& candidate,std::int16_t id,const NpcAvatarRequest& request,
    const ObjectDefinition& definition,const EntryPointTable& entries,std::optional<std::int16_t> initial_value) {
    if(preflight_npc_avatar_creation(request,candidate)!=NpcAvatarRequestStatus::ready || id==request.caller ||
        definition.guid!=request.guid())return false;
    if(!materialize_npc_avatar_base(candidate,id,definition,entries,initial_value))return false;
    const auto* created=candidate.find(id);if(!created)return false;
    const WorldEntityReference reference{id,created->generation};
    // Fresh cXPerson.ClassInit459797 calls base431b5e: record0 is created
    // empty by46dfaf, raw Slot14 becomes-1, and native XY/level become
    // (-16,-16,1). Base constructor43194c zeros the scalar bank including
    // Parent26. This authority belongs to this actual new constructor only;
    // materialize_npc_avatar_base is also used for restored-resource metadata
    // and deliberately does not upgrade an older archive's unknown backing.
    return candidate.declare_object_word(id,14,-1)&&
        candidate.declare_object_word(id,26,0)&&
        candidate.publish_native_slot0(reference,{})&&
        candidate.set_native_out_of_world_level(reference,1);
}
bool materialize_npc_avatar_base(World& candidate,std::int16_t id,
    const ObjectDefinition& definition,const EntryPointTable& entries,std::optional<std::int16_t> initial_value) {
    if(!definition.guid || definition.object_type!=2 || definition.is_multitile ||
        definition.master_id!=0 || !definition.num_attributes ||
        entries.entries.size()<=8 || entries.entries.size()>256)return false;
    if(!candidate.create(id,EntityKind::avatar,{*definition.num_attributes,101,3,16}) ||
        !candidate.define_type(id,definition.guid))return false;
    initialize_avatar_constructor_words(candidate,id,initial_value);
    if(!candidate.define_entry_points(id,entries.entries) || !candidate.declare_definition_word(id,9,2))return false;
    if(definition.level_offset) {
        const auto raw=*definition.level_offset;
        const auto value=static_cast<std::int16_t>(raw<32768 ? static_cast<std::int32_t>(raw) : static_cast<std::int32_t>(raw)-65536);
        if(!candidate.declare_definition_word(id,56,value))return false;
    }
    return true;
}
NpcAvatarStartupResult initialize_npc_avatar(World& world,Vm& thread,std::int16_t id,CodeOwner owner,
    const NpcAvatarRequest& request,const EntryPointTable& entries,const NpcAvatarBodyInputs& inputs,
    RandomState& random,std::optional<std::uint16_t> current_family,const NpcPositionAttempt& position,
    std::uint64_t budget,const VmPrimitive& primitive,bool scheduler_running_now,const std::function<bool()>& self_deleted) {
    NpcAvatarStartupResult result;
    const auto fail=[&](const std::string& why){result.diagnostic=why;return result;};
    const auto* actor=world.find(id);
    const bool out_of_world=request.direct_placement?
        !request.direct_placement->destination:
        request.creation.position()==CreateObjectPosition::out_of_world;
    if(!valid_request(request) || !caller_present(world,request) || !actor || actor->kind!=EntityKind::avatar ||
        !actor->definition || actor->definition->guid!=request.guid() || actor->position || actor->attachment ||
        actor->person_data.size()!=101 || actor->slots.size()!=3 || actor->motives.size()!=16 || !owner ||
        entries.entries.size()<=8 || !same_entries(*actor,entries) || (!out_of_world&&!position) || !inputs.birth_time || !budget || thread.dispatch_active() ||
        thread.status()!=VmStatus::ready || thread.run_generation()!=0)
        return fail("NPC startup requires a fresh bound VM and complete private constructor/placement inputs");
    const auto generation=actor->generation;
    if(!inputs.body || (inputs.body->avatar_type!="adult" && inputs.body->avatar_type!="child"))
        return fail("NPC startup supports resource-proven adult/child body profiles");
    try {
        // Validate body tables before any Init or RNG effects.
        (void)derive_avatar_body_descriptor(world,id,random,inputs.body,inputs.global_walk,inputs.global_swim,inputs.object_walk);
        result.stages.reserve(3);result.inheritance.reserve(2);
        for(const auto& stage:std::array<std::pair<const char*,std::uint16_t>,2>{{
            {"init",entries.entries[0].action},{"dynamic_multitile",entries.entries[8].action}}}) {
            if(stage.second<=255) {
                result.stages.push_back({stage.first,stage.second,VmStatus::ready,0,"entry absent/ID<=255 skipped by reference"});continue;
            }
            const auto remaining=result.steps<budget ? budget-result.steps : 0;
            if(!remaining)return fail("NPC synchronous startup instruction budget exhausted");
            const auto checked=thread.evaluate_entry_point_check(routine_key(stage.second),{id,id,0,owner},
                std::vector<std::int16_t>(4,0),
                remaining,scheduler_running_now,primitive,{},static_cast<bool>(self_deleted));
            result.steps+=checked.steps;
            result.stages.push_back({stage.first,stage.second,checked.status,checked.steps,checked.diagnostic});
            if(checked.started&&checked.lifecycle_deleted&&self_deleted&&self_deleted()) {
                result.completed=true;result.self_deleted=true;return result;
            }
            if(checked.lifecycle_deleted||(self_deleted&&self_deleted()))
                return fail("NPC lifecycle deletion lacks its matching typed stop/identity proof");
            if(!checked.started || (checked.status!=VmStatus::returned_true && checked.status!=VmStatus::returned_false))
                return fail("NPC startup entry did not complete synchronously: "+checked.diagnostic);
        }
        const auto main=entries.entries[1].action;
        if(main>255 && thread.has_routine(routine_key(main),owner)) {
            // ExecuteEntryPoint normalizes a null Main array too; MainParam
            // only changes word0, not the constructor's four-word extent.
            const std::vector<std::int16_t> arguments{request.main_param,0,0,0};
            // The source stores MainStackOBJ as a raw ID until Init/EP8 finish,
            // then GetObjectById returns the current indexed object or null.
            const auto* main_stack=world.find(request.main_stack_object);
            const auto stack_id=main_stack?main_stack->id:std::int16_t{0};
            if(thread.start_after_reset(routine_key(main),{id,id,stack_id,owner},arguments,true)!=VmStatus::running)
                return fail("NPC Main could not be prepared: "+thread.diagnostic());
            result.main_started=true;
        }
        result.stages.push_back({"main",main,thread.status(),0,result.main_started?"prepared without dispatch":"entry absent/ID<=255 skipped by reference"});
        if(!avatar_present(world,id,generation,request.guid()))return fail("NPC identity changed during Init");
        result.body=apply_avatar_body(world,id,random,inputs.body,inputs.global_walk,inputs.global_swim,inputs.object_walk);
        const auto attempt=[&](bool ignore)->std::optional<bool> {
            if(!caller_present(world,request) || !avatar_present(world,id,generation,request.guid()))return {};
            auto destination=request.position;
            if(ignore) {
                const auto* caller=world.find(request.caller);
                // Source ChangePosition(OUT_OF_WORLD) remains OOW; it is not
                // an unavailable geometry lookup and must reach caller cleanup.
                if(!caller->position)return std::optional<bool>{false};
                destination.x=caller->position->x;destination.y=caller->position->y;
                destination.level=caller->position->level;
            }
            if(!position)return {};
            ++result.position_attempts;
            const auto status=position(world,id,destination,request.caller,ignore);
            if(!avatar_present(world,id,generation,request.guid()))return {};
            const auto* current=world.find(id);
            if(status==NpcPositionAttemptStatus::placed)
                return !current->attachment && current->position && *current->position==destination ? std::optional<bool>{true} : std::nullopt;
            if(status==NpcPositionAttemptStatus::blocked && !current->position && !current->attachment)return false;
            return {};
        };
        std::optional<bool> first;
        if(out_of_world){
            if(!world.place_out_of_world(id,request.direct_placement?request.direct_placement->direction:0))return fail("OOW avatar NORTH placement failed");
            first=true;
        }else first=attempt(false);
        if(!first)return fail("NPC initial SetPosition(Default) is unsupported or violated its state contract");
        if(request.constructor_neighbor)
            result.inheritance.push_back(inherit_neighbor(world,id,*request.constructor_neighbor,current_family));
        const auto& birth=*inputs.birth_time;
        for(const auto& field:std::array<std::pair<std::int16_t,std::int16_t>,5>{{
            {45,birth.year},{46,birth.month},{47,birth.day},{24,birth.hour},{21,birth.minute}}})
            if(!world.declare_object_word(id,field.first,field.second))return fail("NPC birth-time publication failed");
        const auto* after_constructor=world.find(id);
        if(request.creation.position()==CreateObjectPosition::underneath_me&&
           after_constructor&&!after_constructor->position&&!after_constructor->attachment) {
            const auto retry=attempt(true);
            if(!retry)return fail("NPC UnderneathMe ignore-caller-group retry is unsupported or violated its state contract");
            if(!*retry){result.placement_blocked=true;return fail("NPC UnderneathMe placement was blocked");}
        }
        // VMContext returns the constructed group even after a failed initial
        // placement. VMCreateObjectInstance then deletes any still-OOW,
        // uncontained result for ALL positional operands except literal OOW.
        const auto* placed=world.find(id);
        if(request.creation.position()!=CreateObjectPosition::out_of_world&&placed&&
           !placed->position&&!placed->attachment) {
            result.placement_blocked=true;
            return fail("NPC non-OOW creation remained out of world; caller cleanup required");
        }
        if(request.creation.use_neighbor())result.inheritance.push_back(inherit_neighbor(world,id,request.requested_neighbor,current_family));
        result.completed=true;
    } catch(const std::exception& error) {result.diagnostic=error.what();}
    return result;
}
} // namespace odh
