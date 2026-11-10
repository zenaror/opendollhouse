// SPDX-License-Identifier: MPL-2.0
#include "odh/slot_height.hpp"
#include "object_trade_session_adapter.hpp"
#include "lot_position_load.hpp"
#include "odh/dynamic_object_groups.hpp"
#include "session_refresh.hpp"
#include "resource.hpp"
#include "saved_portal.hpp"
#include "session_generic_resource_path.hpp"
#include "linux_session_archive_store.hpp"
#include "odh/world_room_info.hpp"
#include "odh/avatar_containment.hpp"
#include "odh/installed_lot_limits.hpp"
#include "odh/room_score.hpp"
#include "odh/route_geometry.hpp"
#include "odh/seated_session_continuation.hpp"
#include "odh/ts1_budget.hpp"
#include "odh/random.hpp"
#include "odh/object_type.hpp"
#include "odh/object_value_registry.hpp"
#include "odh/entity_next.hpp"
#include "odh/distance_to.hpp"
#include "odh/direction_to.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace odh_host {
bool publish_source_individual_position(odh::World& world,odh::WorldEntityReference target,
    const std::optional<odh::EntityPosition>& destination,std::uint8_t direction,
    const std::function<bool()>& current,std::string* diagnostic){
    const auto reject=[&](const char* reason){if(diagnostic)*diagnostic=reason;return false;};
    if(diagnostic)diagnostic->clear();
    if(!current)return reject("source authority callback absent before scalar");
    if(!current())return reject("source authority expired before scalar");
    if(direction>7)return reject("direction outside 0..7");
    if(!world.is_live_reference(target))return reject("target generation unavailable before scalar");
    const bool native_known=world.find(target.id)->native_slot0.has_value();
    if(!world.place_individual_on_floor(target.id,destination,direction))
        return reject("World::place_individual_on_floor refused scalar publication");
    if(!current())return reject("source authority expired after scalar");
    if(!world.is_live_reference(target))return reject("target generation unavailable after scalar");
    const auto* actual=world.find(target.id);
    if(actual->native_slot0.has_value()!=native_known)return reject("native slot0 knownness changed after scalar");
    // FreeSO7e76 LotTilePos.cs128 defines the OOW sentinel with Level1;
    // VMEntity.SetIndivPosition copies that complete position. Native431b5e
    // and433ef5 likewise publish(-16,-16,1). Generic World null remains
    // geometry-only; this source consumer supplies the additional evidence.
    if(destination)return true;
    return world.set_native_out_of_world_level(target,1)||reject("native out-of-world level publication refused");
}
namespace {
using namespace odh;
void require(bool yes,const char* message){if(!yes)throw FormatError(message);}
std::int16_t signed_word(std::uint16_t value){return value<=32767?static_cast<std::int16_t>(value):static_cast<std::int16_t>(static_cast<std::int32_t>(value)-65536);}
std::optional<std::int16_t> word(const World& world,std::int16_t id,std::int16_t index){
    const auto* e=world.find(id);if(!e)return {};const auto v=e->object_words.find(index);
    return v==e->object_words.end()?std::nullopt:std::optional<std::int16_t>{v->second};
}
template<class F>void read_definition(const DefinitionCandidate& definition,F&& read){
    if(definition.asset.empty())with_iff_file(definition.source,std::forward<F>(read));
    else with_far_entry(definition.source,definition.asset,std::forward<F>(read));
}
bool same_lease(const SessionFamilyLease& a,const SessionFamilyLease& b){
    return a.neighborhood_identity==b.neighborhood_identity&&a.family_identity==b.family_identity&&
        a.occupancy_epoch==b.occupancy_epoch&&a.family_chunk_id==b.family_chunk_id;
}
}

ObjectTradeResourceLibrary::ObjectTradeResourceLibrary(std::filesystem::path root,const odh::DefinitionIndex& definitions,
        std::filesystem::path global_archive,std::string global_entry)
    :root_(std::filesystem::canonical(root)),global_archive_(std::filesystem::canonical(global_archive)),
     global_entry_(std::move(global_entry)),definitions_(definitions){
    (void)generic_session_source_key(root_,global_archive_.string());
    const auto bank=load_saved_global_code_bank(global_archive_.string(),global_entry_);
    globals_=std::make_shared<const odh::VmProgram>(bank.routines);
    global_tuning_=load_saved_tuning_tables(global_archive_.string(),global_entry_);
}
const ObjectTradeLoadedResource* ObjectTradeResourceLibrary::find(std::uint32_t guid) const noexcept {
    const auto it=objects_.find(guid);return it==objects_.end()?nullptr:&it->second;
}
const ObjectTradeLoadedResource& ObjectTradeResourceLibrary::creation_resource(std::uint32_t guid,const odh::Vm* source_vm){
    if(const auto* cached=find(guid))return *cached; // No admission, I/O or cache mutation.
    if(source_vm&&source_vm->dispatch_active())
        throw odh::FormatError("creation resource admission requires a dispatch-free boundary");
    return load(guid);
}
const ObjectTradeLoadedResource& ObjectTradeResourceLibrary::load(std::uint32_t guid){
    if(const auto* cached=find(guid))return *cached;
    const auto* definition=definitions_.unique(guid);
    require(definition!=nullptr,"trade definition is absent or ambiguous");
    require(objects_.size()<4096,"trade resource cache exceeds bounds");
    ObjectTradeLoadedResource result;
    read_definition(*definition,[&](const odh::View& view){
        result.code.runtime=odh::read_object_runtime_resources(view,*definition,{},
            definition->definition.master_id!=0&&definition->definition.sub_index!=-1);
        std::optional<odh::View> objd;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){if(chunk.type=="OBJD"&&chunk.id==definition->chunk_id){
            require(!objd,"duplicate trade OBJD");objd=chunk.payload;}});
        require(objd.has_value(),"trade selected OBJD is absent");
        result.definition_words=odh::read_object_definition_words(*objd);
        // Optional capability from complete supported raw fields; an ordinary
        // object does not acquire invented style from a truncated metadata tail.
        const auto version=objd->le32(0);
        if((version==136&&objd->size()>=94)||(version==138&&objd->size()>=164)){
            const auto raw=odh::read_objd_wall_appearance(*objd);
            if(raw.guid==guid&&raw.wall_style<256){
                odh::WallAppearanceStyle style{raw.wall_style,0,{}};
                if(raw.wall_style<=21)result.wall_style=style;
                else if(raw.sprite_base){style.resource_guid=guid;
                    for(unsigned n=0;n<6;++n)style.sprites[n]=static_cast<std::uint16_t>(*raw.sprite_base+n);
                    result.wall_style=style;}
            }
        }
        if(result.definition_words.size()>63)result.code.lead_object=static_cast<std::uint16_t>(result.definition_words[63]);
        result.tuning.private_tables=read_saved_tuning_tables(view);
    });
    const auto& words=result.definition_words;
    require(words.size()>69,"trade catalog fields are incomplete");
    const auto raw=[&](std::size_t index){return static_cast<std::uint16_t>(words.at(index));};
    const auto& d=result.code.runtime.definition;
    // TS1ObjectProvider73-96: exact catalog eligibility and unsigned OBJD.Price.
    const bool listed=(raw(40)>0||raw(69)>0)&&raw(16)==0&&(d.is_multitile||raw(4)>0)&&
        (d.master_id==0||d.sub_index==-1);
    result.catalog={listed?odh::ObjectTradeCatalogState::present:odh::ObjectTradeCatalogState::missing,guid,listed?raw(18):0U};
    result.code.origin={0,0,guid,generic_session_source_key(root_,definition->source),definition->asset,definition->chunk_id};
    if(result.code.runtime.semiglobal_name){
        const auto& name=*result.code.runtime.semiglobal_name;
        require(!name.empty()&&name.find_first_of("/\\:.\0",0,5)==std::string::npos,"trade GLOB is not a flat exact resource name");
        const auto bank=load_saved_semiglobal_code_bank(global_archive_.string(),name+".iff");
        result.code.linked_routines=bank.routines;
        result.code.linked_semiglobal=odh::SessionLinkedSemiglobal{bank.name,generic_session_source_key(root_,global_archive_.string()),bank.entry};
        result.tuning.semiglobal_tables=load_saved_tuning_tables(global_archive_.string(),bank.entry);
        result.tuning.semiglobal_state=odh::TuningSemiState::loaded;
    }else result.tuning.semiglobal_state=odh::TuningSemiState::absent;
    std::size_t instructions=0;
    for(const auto* bank:{&result.code.runtime.private_routines,&result.code.linked_routines})
        for(const auto& routine:*bank)instructions+=routine.second.instructions.size();
    require(instructions<=2097152-instructions_,"trade loaded instructions exceed bounds");
    auto inserted=objects_.emplace(guid,std::move(result));instructions_+=instructions;return inserted.first->second;
}
void ObjectTradeResourceLibrary::preload(const odh::WorldSnapshot& snapshot){
    for(const auto& e:snapshot.entities)if(e.kind==odh::EntityKind::object&&e.definition)(void)load(e.definition->guid);
}
odh::ObjectTradeResource ObjectTradeResourceLibrary::command_resource(std::uint32_t guid){
    const auto& record=load(guid);
    if(record.code.runtime.definition.master_id&&record.code.runtime.definition.sub_index==-1)return load_group(guid);
    auto result=record.code;result.global_routines=globals_->routines();return result;
}

odh::ObjectTradeResource ObjectTradeResourceLibrary::load_group(std::uint32_t requested_guid){
    const auto* requested=definitions_.unique(requested_guid);
    require(requested,"trade group requested definition is absent or ambiguous");
    std::optional<ObjectGroupConstructionPlan> plan;
    read_definition(*requested,[&](const View& view){plan=plan_object_group_construction(view,*requested,definitions_);});
    require(plan&&plan->parts.size()<=64&&plan->base_index<plan->parts.size(),"trade group construction plan is unavailable");
    auto result=load(requested_guid).code;result.global_routines=globals_->routines();
    auto group=std::make_shared<ObjectTradeGroupResources>();group->requested=result.origin;
    group->multitile=plan->multitile;group->base_index=plan->base_index;
    for(const auto& part:plan->parts){
        require(part.candidate,"trade group part definition unavailable");
        auto resource=load(part.candidate->definition.guid).code;resource.global_routines=globals_->routines();
        if(plan->multitile){resource.runtime.master_guid=requested_guid;resource.origin.master_guid=requested_guid;}
        const auto& words=load(part.candidate->definition.guid).definition_words;
        require(words.size()>63,"trade group lead metadata unavailable");
        group->parts.push_back({std::move(resource),part.offset_x,part.offset_y,part.offset_level,
            static_cast<std::uint16_t>(words[63])});
    }
    result.group=std::move(group);return result;
}

std::vector<std::int16_t> allocate_object_trade_group_ids(const WorldSnapshot& world,
        const SessionObjectHistory& history,std::size_t part_count){
    require(history.complete&&part_count&&part_count<=64,"purchase group allocation needs complete bounded identity history");
    std::set<std::int16_t> unavailable;
    for(const auto& e:world.entities)require(e.id>0&&unavailable.insert(e.id).second,"purchase source identity cohort invalid");
    unavailable.insert(history.reserved_entity_ids.begin(),history.reserved_entity_ids.end());
    std::vector<std::int16_t> result;std::int32_t next=1;
    for(std::size_t n=0;n<part_count;++n){while(next<=32767&&unavailable.count(static_cast<std::int16_t>(next)))++next;
        require(next<=32767,"purchase complete group exhausts object identities");result.push_back(static_cast<std::int16_t>(next++));}
    return result;
}

namespace {
bool same_physical_group(const ObjectGroup& a,const ObjectGroup& b){
    if(a.kind!=b.kind||a.definition_guid!=b.definition_guid||a.multitile!=b.multitile||a.base_id!=b.base_id||a.members.size()!=b.members.size())return false;
    for(std::size_t n=0;n<a.members.size();++n){const auto& x=a.members[n];const auto& y=b.members[n];
        if(x.id!=y.id||x.generation!=y.generation||x.offset_x!=y.offset_x||x.offset_y!=y.offset_y||x.offset_level!=y.offset_level)return false;}
    return true;
}
const SessionObjectValueGroup* value_group_for(const SessionBundle& source,ObjectPositionIdentity id){
    if(!source.object_trade||source.object_trade->version!=2)return nullptr;
    for(const auto& group:source.object_trade->groups)for(const auto& member:group.members)
        if(member.object.id==id.id&&member.object.generation==id.generation)return &group;
    return nullptr;
}
bool same_resource_origin(const SessionCreatedObject& a,const SessionCreatedObject& b){
    return a.guid==b.guid&&a.source==b.source&&a.asset==b.asset&&a.definition_chunk_id==b.definition_chunk_id;
}
bool exact_group_members(const ObjectGroup& physical,const ObjectTradeGroup& membership){
    if(!membership.complete||physical.members.size()!=membership.members.size())return false;
    std::set<std::pair<std::int16_t,std::uint64_t>> expected,actual;
    for(const auto& m:physical.members)if(!expected.emplace(m.id,m.generation).second)return false;
    for(const auto& m:membership.members)if(!actual.emplace(m.id,m.generation).second)return false;
    return expected==actual;
}
ObjectTradeResource existing_group_resource(ObjectTradeResourceLibrary& resources,const SessionBundle& source,
        const ObjectTradeSessionHooks& hooks,const std::vector<ObjectTradeGroup>& memberships,ObjectPositionIdentity target){
    const auto* values=value_group_for(source,target);
    const auto entity=std::find_if(source.world.entities.begin(),source.world.entities.end(),[&](const auto& e){return e.id==target.id;});
    require(entity!=source.world.entities.end()&&entity->generation==target.generation&&entity->definition,"trade group target source identity unavailable");
    auto result=resources.command_resource(entity->definition->guid);
    if(!values)return result;
    require(values->base&&values->members.size()<=64,"trade complete source group base unavailable");
    const ObjectGroup* constructed=nullptr;
    if(source.object_groups)for(const auto& group:source.object_groups->groups)for(const auto& member:group.members)
        if(member.id==target.id&&member.generation==target.generation){require(!constructed,"trade source group duplicated");constructed=&group;}
    std::optional<ObjectGroup> physical;
    std::shared_ptr<ObjectTradeGroupResources> group;
    if(constructed){
        if(constructed->kind==ObjectGroupKind::live_association){group=std::make_shared<ObjectTradeGroupResources>();group->live_association=true;group->multitile=constructed->multitile;}
        else{auto requested=resources.load_group(constructed->definition_guid);group=std::make_shared<ObjectTradeGroupResources>(*requested.group);}
        physical=*constructed;
    }else{
        if(!values->multitile&&values->members.size()==1)return result;
        require(static_cast<bool>(hooks.physical_group),"imported trade group needs original ordered physical manifest");
        physical=hooks.physical_group(target);
        require(physical&&physical->definition_guid==0,"imported trade group needs individual source provenance, not a common master");
        group=std::make_shared<ObjectTradeGroupResources>();group->imported=true;group->multitile=values->multitile;
    }
    require(physical&&physical->multitile==values->multitile&&physical->members.size()==values->members.size()&&
        physical->base_id==values->base->id,"trade physical/value group base or extent differs");
    require(std::any_of(memberships.begin(),memberships.end(),[&](const auto& membership){return exact_group_members(*physical,membership);}),
        "trade ordered group does not match actual physical membership");
    if(constructed&&!group->live_association)require(group->parts.size()==values->members.size()&&physical->base_id==physical->members.at(group->base_index).id,
        "trade constructed group differs from exact source plan");
    for(std::size_t n=0;n<values->members.size();++n){const auto& value=values->members[n];const auto& member=physical->members[n];
        const auto& origin=value.object;
        const auto live=std::find_if(source.world.entities.begin(),source.world.entities.end(),[&](const auto& e){return e.id==member.id;});
        require(origin.id==member.id&&origin.generation==member.generation&&value.offset_x==member.offset_x&&
            value.offset_y==member.offset_y&&value.offset_level==member.offset_level&&live!=source.world.entities.end()&&
            live->generation==member.generation&&live->definition&&live->definition->guid==origin.guid&&
            live->definition->master_guid==origin.master_guid,"trade group member differs from physical/value source");
        auto part=resources.command_resource(origin.guid);const auto& words=resources.load(origin.guid).definition_words;
        require(same_resource_origin(part.origin,origin)&&words.size()>63&&value.lead_object==std::optional<std::uint16_t>{static_cast<std::uint16_t>(words[63])},
            "trade group individual resource/lead provenance differs");
        part.origin=origin;part.runtime.master_guid=origin.master_guid;
        if(constructed&&!group->live_association){const auto& planned=group->parts[n];require(same_resource_origin(planned.resource.origin,origin)&&
            planned.resource.origin.master_guid==origin.master_guid&&planned.offset_x==member.offset_x&&
            planned.offset_y==member.offset_y&&planned.offset_level==member.offset_level,"trade dynamic group order differs from construction source");
            group->parts[n].resource=std::move(part);
        }else group->parts.push_back({std::move(part),member.offset_x,member.offset_y,member.offset_level,value.lead_object});
        if(member.id==physical->base_id)group->base_index=n;
    }
    require(group->base_index<group->parts.size()&&group->parts[group->base_index].resource.origin.id==physical->base_id,
        "trade original source base member absent");
    result.group=std::move(group);return result;
}

struct PositionBinding {
    SessionObjectPositionContext ctx;
    std::string lifecycle_diagnostic;
    std::set<std::pair<std::int16_t,std::uint64_t>> unregistered;
    explicit PositionBinding(SessionObjectPositionContext c):ctx(std::move(c)){}
    bool current() const{return ctx.source_still_matches&&ctx.source_still_matches();}
    const Entity* entity(ObjectPositionIdentity id) const {const auto* e=ctx.world->find(id.id);
        return current()&&e&&e->generation==id.generation&&!e->ui_ghost?e:nullptr;}
    bool architectural(ObjectPositionIdentity id) const {
        const auto* e=entity(id);const auto flags=word(*ctx.world,id.id,40);
        if(!e||!e->definition||!flags||!(std::uint16_t(*flags)&0xc000U)||!ctx.architecture_objects||
            !ctx.position_registry||!ctx.position_registry->complete)return false;
        const auto* record=ctx.resources->find(e->definition->guid);
        return record&&record->wall_style.has_value()&&current();
    }
    std::optional<ObjectGroup> group(ObjectPositionIdentity id) const {
        if(!entity(id)||!ctx.group)return {};
        auto group=ctx.group(id);
        if(!current()||!group||group->members.empty()||group->members.size()>64||
           (group->kind==ObjectGroupKind::constructed&&!group->multitile&&group->members.size()!=1))return {};
        const bool live=group->kind==ObjectGroupKind::live_association;
        if(live&&(!ctx.authenticate_live_group||!ctx.authenticate_live_group(*group)))return {};
        const bool imported=!live&&group->definition_guid==0;
        if(imported&&(!ctx.authenticate_imported_group||!ctx.authenticate_imported_group(*group)||!current()))return {};
        bool base=false,found=false;std::set<std::int16_t> seen;
        for(const auto& member:group->members){
            const auto* e=entity({member.id,member.generation});
            if(!e||e->kind!=EntityKind::object||!e->definition||!seen.insert(member.id).second)return {};
            if(!imported&&!live&&(group->multitile?e->definition->master_guid!=std::optional<std::uint32_t>{group->definition_guid}:
                (e->definition->guid!=group->definition_guid||e->definition->master_guid)))return {};
            base|=member.id==group->base_id;found|=member.id==id.id&&member.generation==id.generation;
        }
        return base&&found?group:std::nullopt;
    }
    bool publish_slot(ObjectPositionIdentity item,ObjectPositionIdentity container,std::size_t slot){
        const auto physical=[&]{return entity(item)&&entity(container)&&ctx.world->attach(container.id,slot,item.id)&&
            ctx.world->refresh_contained_position(item.id);};
        return ctx.slot_phase?ctx.slot_phase(item,container,slot,SessionSlotTransferPhase::publish,physical):physical();
    }
    std::optional<std::uint16_t> room(const EntityPosition& p) const{
        if(p.x<0||p.y<0||p.level<1||p.level>5||p.x/16>=static_cast<std::int32_t>(ctx.prepared->width)||p.y/16>=static_cast<std::int32_t>(ctx.prepared->height))return 0;
        if(p.level>2)return {};
        return geometric_room_at_runtime(*ctx.rooms,p.x,p.y,p.level);
    }
    std::unique_ptr<ObjectPositionRegistry> registry() const {
        if(!ctx.position_registry||!ctx.position_registry->complete)return {};
        ObjectPositionRegistryServices services;services.definitions=&ctx.resources->definitions();
        services.group=[this](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{
            if(ctx.registration_group)return current()?ctx.registration_group(id):std::nullopt;
            if(ctx.group){const auto physical=group(id);if(!physical)return {};
                std::vector<ObjectPositionIdentity> result;for(const auto& member:physical->members)result.push_back({member.id,member.generation});return result;}
            for(const auto& group:ctx.groups)for(const auto& member:group.members)if(member.id==id.id&&member.generation==id.generation){
                if(!group.complete)return {};std::vector<ObjectPositionIdentity> result;
                for(const auto& item:group.members)result.push_back({item.id,item.generation});return result;
            }return {};
        };
        services.routing_slot_count=[this](auto id)->std::optional<std::size_t>{
            const auto* e=entity(id);if(!e||!e->definition)return {};const auto* record=ctx.resources->find(e->definition->guid);
            if(!record)return {};const auto& slots=record->code.runtime.slots;
            return slots?std::count_if(slots->records.begin(),slots->records.end(),[](const auto& slot){return slot.type==3;}):0;
        };
        services.moved_self=[this](auto id){return known_object_moved_self(*ctx.position_registry,id);};
        services.retained_moves_often=[this](auto id)->std::optional<bool>{
            if(!current()||!ctx.retired_container||!ctx.world->retired_container_active(*ctx.retired_container))return {};
            const auto reference=ctx.retired_container->parent();
            if(reference.id!=id.id||reference.generation!=id.generation)return {};
            const auto* parent=ctx.world->resolve_reference(reference);if(!parent)return {};
            if(parent->kind==EntityKind::avatar)return true;
            if(parent->kind!=EntityKind::object||parent->attachment||!parent->definition)return {};
            const auto* record=ctx.resources->find(parent->definition->guid);if(!record)return {};
            const auto& slots=record->code.runtime.slots;
            if(slots&&std::count_if(slots->records.begin(),slots->records.end(),[](const auto& slot){return slot.type==3;})>20)return true;
            return ctx.retired_parent_moved_self;
        };
        return std::make_unique<ObjectPositionRegistry>(*ctx.world,*ctx.prepared,*ctx.rooms,std::move(services),*ctx.position_registry);
    }
    std::optional<bool> execute(ObjectPositionIdentity tested,std::uint16_t ep,std::int16_t stack,
            const std::vector<std::int16_t>& args){
        const auto unavailable=[&](const char* why)->std::optional<bool>{
            lifecycle_diagnostic="entry execution entity="+std::to_string(tested.id)+" ep="+std::to_string(ep)+" "+why;return {};
        };
        const auto* e=entity(tested);if(!e||!e->entry_points||ep>=e->entry_points->size())return unavailable("identity/table unavailable");
        const auto action=e->entry_points->at(ep).action;if(action<=255)return false;
        auto* thread=ctx.thread_for?ctx.thread_for(tested.id):nullptr;
        const auto owner=ctx.owner_for?ctx.owner_for(tested.id):std::nullopt;
        if(!thread||!owner||!ctx.primitive_factory)return unavailable("actual thread/owner/primitive unavailable");
        const auto globals=thread->shared_program_for_owner(0),bank=thread->shared_program_for_owner(*owner);
        if(!globals||!bank)return unavailable("actual installed bank unavailable");
        const auto key=routine_key(action);if(!thread->has_routine(key,*owner))return false;
        const auto run_generation=thread->run_generation();
        const auto primitive=ctx.primitive_factory(ctx.memory);
        auto banks=ctx.owner_programs?ctx.owner_programs(tested.id):SharedRoutineOwners{{*owner,bank}};
        if(!banks.count(*owner)||banks.at(*owner)!=bank)return unavailable("full entry owner authority differs");
        Vm check(globals,ShareVmProgramTag{},ctx.memory,{},primitive,std::move(banks),ShareVmOwnerProgramsTag{});
        if(check.start(key,{tested.id,tested.id,stack,*owner},args,true)!=VmStatus::running)return unavailable("entry start failed");
        const auto limit=ctx.instruction_budget?*ctx.instruction_budget:10000;
        const auto result=check.run_synchronous_check_with_registers(*thread,ctx.share_check_registers,limit,primitive);
        if(ctx.instruction_budget){if(result.steps>*ctx.instruction_budget)return unavailable("shared entry budget exceeded");*ctx.instruction_budget-=result.steps;}
        e=entity(tested);
        if(!e||!e->entry_points||e->entry_points->at(ep).action!=action||thread->run_generation()!=run_generation||
           !result.started||(result.status!=VmStatus::returned_true&&result.status!=VmStatus::returned_false)){
            lifecycle_diagnostic="entry execution entity="+std::to_string(tested.id)+" ep="+std::to_string(ep)+
                " action="+std::to_string(action)+" owner="+std::to_string(*owner)+
                " started="+std::to_string(result.started)+" status="+vm_status_name(result.status)+
                " detail="+check.diagnostic();return {};
        }
        return result.status==VmStatus::returned_true;
    }
};
bool run_position_publication(const std::shared_ptr<PositionBinding>& state,ObjectPositionIdentity id,
        const std::optional<EntityPosition>& desired,std::uint8_t direction,std::function<bool()> kernel){
    struct Lease {bool active=true,called=false,accepted=false,repeated=false;std::function<bool()> kernel;};
    auto lease=std::make_shared<Lease>();lease->kernel=std::move(kernel);
    struct End {std::shared_ptr<Lease>& lease;~End(){lease->active=false;lease->kernel={};}}end{lease};
    const std::function<bool()> physical=[lease]{
        if(!lease->active)return false;
        if(lease->called){lease->repeated=true;return false;}
        lease->called=true;lease->accepted=lease->kernel();return lease->accepted;
    };
    const bool completed=state->ctx.position_phase?state->ctx.position_phase(id,desired,
        SessionPositionPhase::publish,direction,physical):physical();
    return completed&&lease->called&&lease->accepted&&!lease->repeated&&state->entity(id);
}
}
SessionObjectPositionServices make_session_object_position_services(SessionObjectPositionContext context){
    require(context.world&&context.resources&&context.prepared&&context.architecture&&context.rooms&&context.source_still_matches,
        "object placement needs exact World and prepared geometry");
    require(context.prepared->width==context.rooms->width&&context.prepared->height==context.rooms->height&&
        context.prepared->width==context.architecture->width&&context.prepared->height==context.architecture->height,
        "object placement geometry dimensions disagree");
    if(context.architecture_objects){
        auto& a=*context.architecture_objects;const auto cells=std::size_t(context.prepared->width)*context.prepared->height;
        require(a.object_support.width==context.prepared->width&&a.object_support.height==context.prepared->height,
            "placement AOBJ dimensions differ from prepared source");
        require(!context.object_support||context.object_support==&a.object_support,"placement support is not same AOBJ owner");
        require(!context.position_registry||(a.position_registry&&context.position_registry==&*a.position_registry),
            "placement registry is not same AOBJ owner");
        for(const auto& level:a.occupied_walls){require(level.size()==cells,"placement occupied wall extent mismatch");
            for(const auto mask:level)require((mask&~15U)==0,"placement occupied wall mask invalid");}
    }
    auto state=std::make_shared<PositionBinding>(std::move(context));
    SessionObjectPositionServices result;
    auto& valid=result.placement;auto& change=result.change;
    change.detach_container=state->ctx.detach_container;
    valid.publish_heading=[state](auto id,std::uint8_t direction,const std::function<bool()>& setter){
        const auto* e=state->entity(id);if(!e||e->kind!=EntityKind::object||direction>7)return false;
        auto pose=e->position;if(pose)pose->direction=direction;
        // Only the fixed orientation write owns this scope. Object PositionValid
        // executes its intersection queries and real EP5 after it has closed.
        return run_position_publication(state,id,pose,direction,setter);
    };
    result.record_moved_self=[state](auto id,bool value){
        return state->ctx.position_registry&&state->ctx.position_registry->complete&&record_object_moved_self(*state->ctx.position_registry,*state->ctx.world,id,value,state->ctx.source_still_matches);
    };
    result.static_footprint_changed=[state](auto id,bool value){
        if(state->ctx.position_registry&&!state->ctx.position_registry->complete)return false;
        auto registry=state->registry();if(!registry)return false;
        const bool accepted=registry->static_footprint_changed(id,value,state->ctx.source_still_matches);
        *state->ctx.position_registry=registry->snapshot();return accepted;
    };
    result.update_footprint=[state](auto id){
        if(state->ctx.position_registry&&!state->ctx.position_registry->complete)return false;
        auto registry=state->registry();if(!registry)return false;
        const bool accepted=registry->update_footprint(id,state->ctx.source_still_matches);
        *state->ctx.position_registry=registry->snapshot();return accepted;
    };
    result.execute_entry=[state](auto id,std::uint16_t ep,std::int16_t stack,const std::vector<std::int16_t>& args){
        return state->execute(id,ep,stack,args);
    };
    valid.architecture=[state](auto target,const auto& pos)->std::optional<ObjectPositionArchitecture>{
        const auto* e=state->entity(target);if(!e||!e->definition)return {};
        const auto& ctx=state->ctx;const auto& p=*ctx.prepared;ObjectPositionArchitecture a;
        a.out_of_bounds=pos.x<0||pos.y<0||pos.level<1||pos.level>5||pos.x/16>=static_cast<std::int32_t>(p.width)||pos.y/16>=static_cast<std::int32_t>(p.height);
        if(a.out_of_bounds)return a;if(pos.level>2)return {};
        const auto x=static_cast<std::uint32_t>(pos.x/16),y=static_cast<std::uint32_t>(pos.y/16);
        const auto at=std::size_t(y)*p.width+x;
        if(at>=p.buildable.size()||at>=p.levels[pos.level-1].walls.size()||!p.floor_identity_unambiguous())return {};
        a.user_out_of_bounds=!p.buildable[at];a.wall_segments=p.levels[pos.level-1].walls[at].segments;
        const auto floor=prepared_precise_floor(p,pos.x,pos.y,pos.level);if(!floor)return {};a.precise_floor=*floor;
        const auto wall=word(*ctx.world,target.id,13),exclusive=word(*ctx.world,target.id,63);
        if(!wall||!exclusive)return {};
        if(ctx.architecture_objects){const auto& owner=*ctx.architecture_objects;
            const auto& occupied=owner.occupied_walls[std::size_t(pos.level-1)];
            if(owner.object_support.width!=p.width||owner.object_support.height!=p.height||
               occupied.size()!=std::size_t(p.width)*p.height||at>=occupied.size()||(occupied[at]&~15U))return {};
            a.occupied_walls=occupied[at];
        }else if((*exclusive&2)&&(*wall&15))return {}; // Actual legacy unknown, not an empty grid.
        const auto& raw=*ctx.architecture;if(!raw.altitude||raw.altitude->size()!=p.buildable.size())return {};
        const auto h=[&](auto xx,auto yy){return raw.altitude->at(std::size_t(yy)*raw.width+xx)[1];};
        a.sloped=h(x,y)!=h((x+1)%raw.width,y)||h(x,y)!=h((x+1)%raw.width,(y+1)%raw.height)||h(x,y)!=h(x,(y+1)%raw.height);
        const auto* def=ctx.resources->definitions().unique(e->definition->guid);if(!def)return {};
        if(def->definition.level_offset)a.level_offset=static_cast<std::int16_t>(*def->definition.level_offset);return a;
    };
    valid.intersections=[state](auto target,auto pos,auto direction)->std::optional<ObjectPositionIntersections>{
        const auto* e=state->entity(target);if(!e||!e->definition)return {};
        const auto* def=state->ctx.resources->definitions().unique(e->definition->guid);const auto flags=word(*state->ctx.world,target.id,8);
        if(!def||!flags)return {};pos.direction=direction;
        const auto shape=object_collision_shape(def->definition,pos,static_cast<std::uint16_t>(*flags),false);
        if(shape.state==CollisionShapeState::unsupported)return {};
        ObjectPositionIntersections out;out.footprint_present=shape.state==CollisionShapeState::present;if(!out.footprint_present)return out;
        const auto room=state->room(pos);if(!room)return {};
        if(state->ctx.position_registry&&state->ctx.position_registry->complete){auto registry=state->registry();out.query=registry->query(*room,shape.box,target.id);}
        else out.query=query_world_room_info(*state->ctx.world,state->ctx.resources->definitions(),[state](auto p){return state->room(p);},
            [state](auto id,auto generation)->std::optional<std::int32_t>{const auto* v=state->ctx.world->find(id);
                return v&&v->generation==generation&&v->kind==EntityKind::avatar?v->kill_timeout:std::nullopt;},*room,shape.box,target.id);
        auto& hits=out.query.intersections;
        hits.erase(std::remove_if(hits.begin(),hits.end(),[state](const auto& hit){const auto* v=state->ctx.world->find(static_cast<std::int16_t>(hit.owner_id));
            return v&&state->unregistered.count({v->id,v->generation});}),hits.end());return out;
    };
    valid.same_group=[state](auto a,auto b)->std::optional<bool>{if(!state->entity(a)||!state->entity(b))return {};
        if(a.id==b.id)return true;
        if(state->ctx.group){const auto group=state->group(a);if(!group)return {};
            return std::any_of(group->members.begin(),group->members.end(),[&](const auto& member){return member.id==b.id&&member.generation==b.generation;});}
        for(const auto& group:state->ctx.groups){bool found=false;for(const auto& member:group.members)if(member.id==a.id&&member.generation==a.generation)found=true;
            if(found){if(!group.complete)return {};for(const auto& member:group.members)if(member.id==b.id&&member.generation==b.generation)return true;return false;}}
        return {};};
    valid.ignore_intersection=[state](auto a,auto b)->std::optional<bool>{return state->entity(a)&&state->entity(b)?std::optional<bool>{false}:std::nullopt;};
    valid.ghost_image=[state](auto id)->std::optional<bool>{return state->entity(id)?std::optional<bool>{false}:std::nullopt;};
    valid.slot0_height=[state](auto id)->std::optional<std::int32_t>{
        const auto* e=state->entity(id);if(!e)return {};
        // VMAvatar overrides this query independently of object SLOT resources.
        if(e->kind==EntityKind::avatar)return 5;
        if(e->kind!=EntityKind::object||!e->definition)return {};
        const auto* r=state->ctx.resources->find(e->definition->guid);if(!r)return {};
        const auto extent=logical_containment_slots(*e);
        if(!r->code.runtime.slots)return extent?std::nullopt:std::optional<std::int32_t>{-1};
        const auto& table=*r->code.runtime.slots;
        if(table.version<4||table.version>10)return {};
        const auto count=static_cast<std::size_t>(std::count_if(table.records.begin(),table.records.end(),
            [](const auto& slot){return slot.type==0;}));
        if(count!=extent)return {}; // Contained and its source storage bank must agree.
        if(!extent)return -1;
        const auto* slot=slot_of_type(table,0,0);
        return slot?source_slot_height(table,*slot):std::nullopt;
    };
    valid.entry_point5=[state](const auto& q)->std::optional<bool>{if(!q.source_still_matches||!q.source_still_matches())return {};
        const auto* e=state->entity(q.tested);if(!e||!e->entry_points||e->entry_points->size()<=5||e->entry_points->at(5).action!=q.action)return {};
        return state->execute(q.tested,5,q.other.id,std::vector<std::int16_t>(q.arguments.begin(),q.arguments.end()));};
    change.story_count=[]()->std::optional<std::int16_t>{return 5;};
    change.room_at=[state](const auto& p){return state->current()?state->room(p):std::nullopt;};
    change.admit_single_ordinary=[state](auto id)->std::optional<bool>{const auto* e=state->entity(id);if(!e||!e->definition)return {};
        const auto* def=state->ctx.resources->definitions().unique(e->definition->guid);const auto flags=word(*state->ctx.world,id.id,40),light=word(*state->ctx.world,id.id,51),category=word(*state->ctx.world,id.id,59);
        if(!def||!flags||!light||!category||!e->entry_points||e->entry_points->size()<=15)return {};
        if(e->kind!=EntityKind::object||def->definition.is_multitile||def->definition.master_id||e->definition->master_guid||
           ((static_cast<std::uint16_t>(*flags)&0xc000U)&&!state->architectural(id))||*light||*category==8||
           (e->entry_points->at(15).action&&!state->architectural(id)))return false;
        for(const auto& group:state->ctx.groups)for(const auto& member:group.members)if(member.id==id.id&&member.generation==id.generation)
            return group.complete&&group.members.size()==1;
        // A fresh constructor proves its own singleton; an unrecorded older
        // identity is not promoted merely from OBJD.MasterID.
        return {};};
    if(state->ctx.group)change.group=[state](auto id){return state->group(id);};
    if(state->ctx.authenticate_imported_group)change.authenticate_imported_group=[state](const auto& group){
        return state->current()&&state->ctx.authenticate_imported_group(group)&&state->current();
    };
    if(state->ctx.group&&state->ctx.admit_group_member)change.admit_group_member=[state](auto id)->std::optional<bool>{
        const auto group=state->group(id);const auto* e=state->entity(id);
        if(!group||!e||!e->definition)return {};
        const auto* def=state->ctx.resources->definitions().unique(e->definition->guid);
        const auto flags=word(*state->ctx.world,id.id,40),light=word(*state->ctx.world,id.id,51),category=word(*state->ctx.world,id.id,59);
        if(!def||!flags||!light||!category||!e->entry_points||e->entry_points->size()<=15)return {};
        if((def->definition.master_id&&def->definition.sub_index==-1)||
           ((static_cast<std::uint16_t>(*flags)&0xc000U)&&!state->architectural(id))||*light||*category==8||
           (e->entry_points->at(15).action&&!state->architectural(id)))return false;
        const auto admitted=state->ctx.admit_group_member(id);
        return state->entity(id)&&state->group(id)?admitted:std::nullopt;
    };
    change.unregister_position=[state](auto id,bool room_change){const auto* e=state->entity(id);if(!e)return false;
        if(state->ctx.position_registry&&!state->ctx.position_registry->complete)return false;
        if(auto registry=state->registry()){
            const bool accepted=registry->unregister_position(id,room_change,state->ctx.source_still_matches);
            *state->ctx.position_registry=registry->snapshot();if(!accepted){state->lifecycle_diagnostic="registry unregister rejected";return false;}
        }
        state->unregistered.insert({id.id,id.generation});return state->current();};
    change.register_position=[state](auto id,std::uint16_t room,bool room_change){const auto* e=state->entity(id);if(!e)return false;
        if(state->ctx.position_registry&&!state->ctx.position_registry->complete)return false;
        if(auto registry=state->registry()){
            const bool accepted=registry->register_position(id,room,room_change,state->ctx.source_still_matches);
            *state->ctx.position_registry=registry->snapshot();if(!accepted){state->lifecycle_diagnostic="registry register rejected";return false;}
        }
        const auto actual=e->position?state->room(*e->position):std::optional<std::uint16_t>{0};
        if(!actual||*actual!=room||word(*state->ctx.world,id.id,29)!=static_cast<std::int16_t>(static_cast<std::int32_t>(room)-1)){
            state->lifecycle_diagnostic="registered geometric room/Room29 mismatch";return false;
        }
        if(e->position&&!calculate_room_score(*state->ctx.rooms,room_score_contributions(*state->ctx.world),e->position->x,e->position->y,e->position->level)){
            state->lifecycle_diagnostic="registered room score unavailable";return false;
        }
        state->unregistered.erase({id.id,id.generation});return state->current();};
    change.place_in_slot0=[state](auto target,auto support){return state->publish_slot(target,support,0);};
    change.restore_container=[state](auto target,auto container,std::size_t slot){
        const auto* c=state->entity(container);const auto* t=state->entity(target);
        if(!c||!t||t->attachment||slot>=c->slots.size()||c->slots[slot])return false;
        // Source PlaceInSlot(cleanOld=false) sets physical XYZ, preserving
        // target direction and delaying recursive registration to its caller.
        return state->publish_slot(target,container,slot);
    };
    change.wall_use=[state](auto id,bool,bool exclusive){const auto* e=state->entity(id);if(!e||!e->position||!e->entry_points||e->entry_points->size()<=6)return false;
        const auto flags=word(*state->ctx.world,id.id,13),direction=word(*state->ctx.world,id.id,1);if(!flags||!direction)return false;
        const auto& p=*state->ctx.prepared;const auto pos=*e->position;if(pos.level<1||pos.level>2)return false;
        const auto at=std::size_t(pos.y/16)*p.width+static_cast<std::size_t>(pos.x/16);if(at>=p.levels[pos.level-1].walls.size())return false;
        const auto rotate=[](std::uint32_t v,unsigned by){return ((v<<(4-by))&15U)|((v&15U)>>by);};
        const auto quarter=(*direction%2)==0?*direction/2:0;const auto rotation=static_cast<unsigned>((8-(quarter+1))%4);
        const auto used=rotate(static_cast<std::uint16_t>(*flags)%15U,rotation);
        if(!e->dead&&e->entry_points->at(6).action){const auto main=rotate(p.levels[pos.level-1].walls[at].segments,(4-rotation)%4);std::uint16_t adjacency=0;
            if(main&1)adjacency|=4;if(main&2)adjacency|=2;if(main&4)adjacency|=8;if(main&8)adjacency|=1;
            if(!state->ctx.world->declare_object_word(id.id,5,static_cast<std::int16_t>(adjacency))||!state->execute(id,6,id.id,{0,0,0,0}).has_value())return false;}
        return (!used||!exclusive)&&state->current();};
    if(state->ctx.architecture_objects){
        LotPositionLoadContext walls;walls.world=state->ctx.world;walls.prepared=state->ctx.prepared;
        const auto weak=std::weak_ptr<PositionBinding>(state);
        walls.source_still_matches=[weak]{const auto s=weak.lock();return s&&s->current();};
        walls.resolved_wall_style=[weak](auto id)->std::optional<WallAppearanceStyle>{
            const auto s=weak.lock();const auto* e=s?s->entity(id):nullptr;if(!e||!e->definition)return {};
            const auto* r=s->ctx.resources->find(e->definition->guid);return r?r->wall_style:std::nullopt;};
        walls.execute_entry=[weak](auto id,std::uint16_t ep,std::int16_t stack,const std::array<std::int16_t,4>& args){
            const auto s=weak.lock();return s&&s->execute(id,ep,stack,{args.begin(),args.end()}).has_value();};
        const auto consumer=bind_lot_position_wall_consumers(std::move(walls),*state->ctx.architecture_objects);
        change.wall_use=[state,consumer](auto id,bool set,bool exclusive){return state->current()&&consumer.wall_use(id,set,exclusive);};
        change.architectural_wall_style_change=[state,consumer](auto id,bool set){
            return state->current()&&consumer.architectural_wall_style_change(id,set)&&
                (!state->ctx.wall_appearance_changed||state->ctx.wall_appearance_changed())&&state->current();};
    }
    change.object_supported=[state](auto id,const auto& position,bool set){
        auto* support=state->ctx.object_support;
        if(!support||support->width!=state->ctx.prepared->width||support->height!=state->ctx.prepared->height)return false;
        return set_architecture_object_supported(*support,*state->ctx.world,id,position,set,state->ctx.source_still_matches);
    };
    change.dynamic_multitile=[state](auto id){if(state->ctx.dynamic_group_update)return state->ctx.dynamic_group_update(id);const auto* e=state->entity(id);if(!e||!e->position)return false;
        for(std::int16_t cursor=0;;){const auto next=state->ctx.world->next_entity(cursor);if(!next)break;cursor=*next;
            const auto* other=state->ctx.world->find(cursor);if(cursor==id.id||!other->definition||!e->definition||other->definition->guid!=e->definition->guid||!other->position)continue;
            const auto tile=[](std::int32_t x){return x>=0?x/16:-((-x+15)/16);};
            const auto dx=std::abs(tile(other->position->x-e->position->x)),dy=std::abs(tile(other->position->y-e->position->y));
            if(dx<2&&dy<2&&dx+dy<2)return false;
        }
        return state->execute(id,8,0,{0,0,0,0}).has_value();};
    change.entry_point9=[state](auto id){
        return state->ctx.native_entry_point9?state->current()&&state->ctx.native_entry_point9(id)&&state->current():
            state->execute(id,9,0,{0,0,0,0}).has_value();
    };
    change.authenticate_live_group=state->ctx.authenticate_live_group;
    change.split_dynamic_group=state->ctx.split_dynamic_group;
    change.update_dynamic_group_flags=state->ctx.update_dynamic_group_flags;
    change.begin_native_rotation=state->ctx.begin_native_rotation;
    change.rollback_native_rotation=state->ctx.rollback_native_rotation;
    change.native_current_offset=state->ctx.native_current_offset;
    change.native_group_head=state->ctx.native_group_head;change.native_group_next=state->ctx.native_group_next;
    change.lifecycle_phase=[state](auto id,ObjectPositionChangePhase phase,const std::function<bool()>& operation){
        const auto* e=state->entity(id);if(!e)return false;
        const auto current_pose=e->position;
        // Post reads the actual root pose after a support slot or restoration;
        // that physical pose is not necessarily the requested floor candidate.
        return state->ctx.position_phase?state->ctx.position_phase(id,current_pose,
            phase==ObjectPositionChangePhase::pre?SessionPositionPhase::pre:SessionPositionPhase::post,{},operation):operation();
    };
    change.publish_position=[state](auto id,const std::optional<EntityPosition>& desired,std::uint8_t direction){
        const auto* e=state->entity(id);if(!e||e->kind!=EntityKind::object||direction>7)return false;
        const auto physical=[state,id,desired,direction]{
            if(!state->entity(id))return false;
            // This frame is only a World register address; all execution
            // authority comes from the real source context captured above.
            VmFrame address;address.context.caller=id.id;
            const auto group=state->ctx.group?state->ctx.group(id):std::nullopt;
            const bool heading=group&&group->native_head?
                state->ctx.world->publish_native_object_heading({id.id,id.generation},direction):state->ctx.world->write(address,3,1,direction);
            return heading&&state->entity(id)&&publish_source_individual_position(*state->ctx.world,
                {id.id,id.generation},desired,direction,[state,id]{return state->entity(id)!=nullptr;});
        };
        return run_position_publication(state,id,desired,direction,physical);
    };
    const auto placement=result.placement;
    auto lifecycle=result.change;
    // Delete/slot lifecycle acts on one physical member, unlike floor movement.
    // Only this private copy accepts a certified group member as its root/child.
    const auto single=lifecycle.admit_single_ordinary,member=lifecycle.admit_group_member;
    if(member)lifecycle.admit_single_ordinary=[single,member](auto id)->std::optional<bool>{
        const auto ordinary=single?single(id):std::nullopt;
        return ordinary&&*ordinary?ordinary:member(id);
    };
    result.lifecycle_diagnostic=[state]{return state->lifecycle_diagnostic;};
    result.before_slot_transfer=[state,placement,lifecycle](auto id,bool room_change){
        state->lifecycle_diagnostic.clear();
        ObjectPositionValid valid(*state->ctx.world,placement);ObjectPositionChange change(*state->ctx.world,valid,lifecycle);
        const bool accepted=change.pre_position_change(id,room_change,state->ctx.source_still_matches);
        if(!accepted)state->lifecycle_diagnostic=change.diagnostic()+" "+state->lifecycle_diagnostic;
        return accepted;
    };
    result.after_slot_transfer=[state,placement,lifecycle](auto id,bool room_change){
        state->lifecycle_diagnostic.clear();
        ObjectPositionValid valid(*state->ctx.world,placement);ObjectPositionChange change(*state->ctx.world,valid,lifecycle);
        const bool accepted=change.position_change(id,room_change,state->ctx.source_still_matches);
        if(!accepted)state->lifecycle_diagnostic=change.diagnostic()+" "+state->lifecycle_diagnostic;
        return accepted;
    };
    const auto pre=result.before_slot_transfer,post=result.after_slot_transfer;
    result.transfer_to_slot=[state,pre,post](auto item,auto container,std::size_t slot)->SessionSlotTransferResult{
        const auto reject=[](const std::string& why){return SessionSlotTransferResult{false,false,why};};
        const auto valid=[&]{return state->entity(item)&&state->entity(container);};
        if(!valid())return reject("slot source identity unavailable");
        VmFrame frame;frame.context.caller=container.id;
        if(slot>32767)return reject("slot exceeds signed source index");
        const auto occupant=state->ctx.world->read(frame,20,static_cast<std::int16_t>(slot));
        if(!occupant)return reject("slot extent unavailable");
        if(*occupant==item.id)return {true,true,{}};
        if(*occupant)return {true,false,{}};
        auto plan=state->ctx.world->prepare_move_to_slot(container.id,slot,item.id);
        if(!plan)return reject("slot ancestry/budget preflight unavailable");
        const auto phase=[&](SessionSlotTransferPhase stage,const std::function<bool()>& operation){
            return state->ctx.slot_phase?state->ctx.slot_phase(item,container,slot,stage,operation):operation();
        };
        if(!phase(SessionSlotTransferPhase::pre,[&]{return pre(item,true);})||!valid())
            return reject("slot PrePositionChange: "+state->lifecycle_diagnostic);
        // Source PlaceInSlot publishes root Container/slot and root XYZ only.
        // Child old poses remain registered until recursive PositionChange.
        if(!state->publish_slot(item,container,slot)||!valid())
            return reject("slot scalar attachment/position publication failed");
        if(!phase(SessionSlotTransferPhase::post,[&]{return post(item,true);})||!valid())
            return reject("slot PositionChange: "+state->lifecycle_diagnostic);
        return {true,true,{}};
    };
    return result;
}

namespace {
struct ThreadSeed {std::int16_t id;std::uint64_t generation;VmSnapshot state;ObjectTradeThreadPrograms programs;};
std::string register_owner_restore_failure(const ThreadSeed& seed){
    std::string detail="candidate real register owner could not restore entity="+std::to_string(seed.id)+
        " status="+std::to_string(static_cast<unsigned>(seed.state.status))+" policy="+
        std::to_string(static_cast<unsigned>(seed.programs.saved_routine_policy))+" depth="+std::to_string(seed.state.stack.size());
    for(std::size_t n=0;n<seed.state.stack.size();++n){const auto& frame=seed.state.stack[n];
        const auto bank=frame.key.scope==RoutineScope::global||frame.context.code_owner==0?seed.programs.globals:
            (seed.programs.owners.count(frame.context.code_owner)?seed.programs.owners.at(frame.context.code_owner):nullptr);
        const auto found=bank?bank->routines().find(frame.key):RoutineSet::const_iterator{};
        detail+=" frame["+std::to_string(n)+"]="+std::to_string(frame.key.id)+"@"+std::to_string(frame.pc)+
            " owner="+std::to_string(frame.context.code_owner);
        if(!bank)detail+=" bank_missing";
        else if(found==bank->routines().end())detail+=" routine_missing";
        else{detail+=" instructions="+std::to_string(found->second.instructions.size());
            if(frame.pc>=found->second.instructions.size())detail+=" pc_out_of_range";
            detail+=" arguments="+std::to_string(frame.arguments.size())+" locals="+std::to_string(frame.locals.size());
        }
    }
    return detail;
}

// The same ownership contract as the live host: notifications are broader
// than pose changes; only a fixed Pre/publish/Post capability owns a pose delta.
// Init/EP5/EP11 never inherit that capability merely by running in a candidate.
class CandidateRegistryGuard {
    struct Pose {
        std::uint64_t generation;EntityKind kind;std::optional<EntityType> definition;
        std::optional<EntityPosition> position;std::optional<Attachment> attachment;
        std::optional<std::int16_t> heading,flags,architectural_flags;std::uint16_t portal_action;float radians;bool stat;
    };
    World& world_;ObjectPositionRegistrySnapshot& registry_;std::function<bool()> authority_;
    const ArchitectureObjectState* architecture_=nullptr;
    std::map<std::int16_t,Pose> poses_;
    std::function<bool(std::int16_t,const Pose&,const Entity&)> owned_;
    std::set<std::int16_t> pending_footprints_;
    static bool link(const std::optional<Attachment>& a,const std::optional<Attachment>& b){
        return a.has_value()==b.has_value()&&(!a||(a->container==b->container&&a->slot==b->slot&&a->positioned==b->positioned));
    }
    Pose pose(const Entity& e)const{return {e.generation,e.kind,e.definition,e.position,e.attachment,
        word(world_,e.id,1),word(world_,e.id,8),word(world_,e.id,40),
        static_cast<std::uint16_t>(e.entry_points&&e.entry_points->size()>15?e.entry_points->at(15).action:0),
        e.radian_direction,e.static_footprint};}
    bool referenced(ObjectPositionIdentity owner)const{
        const auto same=[&](auto id){return id.id==owner.id&&id.generation==owner.generation;};
        for(const auto& r:registry_.rooms){
            if(std::any_of(r.entities.begin(),r.entities.end(),same))return true;
            for(const auto* values:{&r.static_obstacles,&r.dynamic_obstacles})
                if(std::any_of(values->begin(),values->end(),[&](const auto& v){return same(v.owner);}))return true;
            for(const auto* values:{&r.portals,&r.window_portals})
                if(std::any_of(values->begin(),values->end(),[&](const auto& v){return same(v.owner);}))return true;
        }
        for(const auto& tile:registry_.tiles)if(std::any_of(tile.entities.begin(),tile.entities.end(),same))return true;
        return false;
    }
    bool known_nonspatial_architectural_change(const Pose& before,const Entity& e)const{
        // Init may establish its wall class before the first source Post. This
        // is not permission to change a registered door/window's collections.
        if(!authority_||!authority_()||!architecture_||!architecture_->position_registry||
           &*architecture_->position_registry!=&registry_||before.position||before.attachment||
           e.kind!=EntityKind::object||e.dead)return false;
        std::string why;
        if(!object_position_known_absent_oow(registry_,e,why))return false;
        // A stale style/door owner is a real architectural contribution even
        // when a broken source has omitted its ordinary registry collections.
        for(const auto& face:architecture_->wall_appearance.faces)
            if((face.owner==e.id&&face.generation==e.generation)||
               (face.door_owner==e.id&&face.door_generation==e.generation))return false;
        return true;
    }
public:
    CandidateRegistryGuard(World& world,ObjectPositionRegistrySnapshot& registry,std::function<bool()> authority,
            const ArchitectureObjectState* architecture)
        :world_(world),registry_(registry),authority_(std::move(authority)),architecture_(architecture){
        for(std::int16_t id=0;;){const auto next=world_.next_entity(id);if(!next)break;id=*next;poses_.emplace(id,pose(*world_.find(id)));}
    }
    void observe(){
        bool changed=false;std::set<std::int16_t> present;
        for(std::int16_t id=0;;){const auto next=world_.next_entity(id);if(!next)break;id=*next;
            const auto& e=*world_.find(id);present.insert(id);const auto old=poses_.find(id);
            if(old==poses_.end()){changed=true;}
            else {
                const auto& before=old->second;
                const bool identity=before.generation==e.generation&&before.kind==e.kind&&
                    before.definition.has_value()==e.definition.has_value()&&(!e.definition||
                    (before.definition->guid==e.definition->guid&&before.definition->master_guid==e.definition->master_guid));
                const bool moved=!(before.position==e.position)||!link(before.attachment,e.attachment)||
                    before.heading!=word(world_,id,1)||before.radians!=e.radian_direction;
                if(!identity||(moved&&(!owned_||!owned_(id,before,e))))changed=true;
                const auto flags2=word(world_,id,40);
                if(before.architectural_flags.has_value()!=flags2.has_value())changed=true;
                else if(flags2&&((std::uint16_t(*before.architectural_flags)^std::uint16_t(*flags2))&0xc000U)&&
                    !(identity&&!moved&&known_nonspatial_architectural_change(before,e)))changed=true;
                if(before.portal_action!=(e.entry_points&&e.entry_points->size()>15?e.entry_points->at(15).action:0))changed=true;
                // ObjectData8 publishes before its synchronous UpdateFootprint
                // callback. Keep its pending receipt separate from pose authority.
                const auto flags=word(world_,id,8);
                if(before.flags.has_value()!=flags.has_value())changed=true;
                else if(flags&&((std::uint16_t(*before.flags)^std::uint16_t(*flags))&4U))pending_footprints_.insert(id);
                // Flags assignment deliberately changes StaticFootprint without
                // moving the source's existing obstacle collection. The World
                // ObstacleStatic callback separately owns collection migration.
                if(before.stat!=e.static_footprint){
                    const bool flags_assignment=flags&&e.static_footprint==((std::uint16_t(*flags)&0x8000U)==0);
                    if(!flags_assignment)pending_footprints_.insert(id);
                }
            }
            poses_.insert_or_assign(id,pose(e));
        }
        for(auto it=poses_.begin();it!=poses_.end();)if(!present.count(it->first)){
            if(referenced({it->first,it->second.generation}))changed=true;
            pending_footprints_.erase(it->first);it=poses_.erase(it);
        }else ++it;
        if(changed)registry_.complete=false;
    }
    void footprint_receipt(ObjectPositionIdentity id,bool accepted){
        const auto* e=world_.find(id.id);
        if(!accepted||!e||e->generation!=id.generation)registry_.complete=false;
        else pending_footprints_.erase(id.id);
    }
    bool phase(ObjectPositionIdentity item,std::optional<ObjectPositionIdentity> container,std::size_t slot,
               SessionPositionPhase phase,const std::optional<EntityPosition>& desired,
               std::optional<std::uint8_t> direction,const std::function<bool()>& operation){
        if(!authority_()||!registry_.complete||!operation||
           (!container&&phase==SessionPositionPhase::publish&&(!direction||*direction>7||
                (desired&&desired->direction!=*direction))))return false;
        std::map<std::int16_t,Pose> nodes;
        std::function<bool(std::int16_t)> capture=[&](auto id){
            const auto* e=world_.find(id);if(!e||e->ui_ghost||nodes.size()>=4096||nodes.count(id))return false;
            const auto state=pose(*e);if(!state.heading||*state.heading<0||*state.heading>7)return false;
            nodes.emplace(id,state);
            for(std::size_t n=0;n<e->slots.size();++n)if(e->slots[n]){
                const auto* child=world_.find(e->slots[n]);if(!child||!child->attachment||
                    child->attachment->container!=id||child->attachment->slot!=n||!capture(child->id))return false;
            }return true;
        };
        if(!capture(item.id)||nodes.at(item.id).generation!=item.generation)return false;
        auto previous=std::move(owned_);
        struct End {CandidateRegistryGuard& self;decltype(owned_) previous;bool completed=false;
            ~End(){self.owned_=std::move(previous);if(!completed)self.registry_.complete=false;}}end{*this,std::move(previous)};
        owned_=[&,item,container,slot,phase,desired,direction](auto id,const Pose& before,const Entity& actual){
            if(!authority_())return false;const auto found=nodes.find(id);
            if(found==nodes.end()||found->second.generation!=before.generation||actual.generation!=before.generation)return false;
            const auto& known=found->second;
            const auto* parent=container?world_.find(container->id):nullptr;
            if(container&&(!parent||parent->generation!=container->generation||!(parent->position==desired)))return false;
            if(actual.radian_direction!=known.radians)return false; // Ordinary objects retain raw radians.
            if(phase==SessionPositionPhase::pre)return id==item.id&&known.attachment&&!actual.attachment&&
                actual.position==known.position&&word(world_,id,1)==known.heading;
            auto predicted=desired;if(predicted&&(container||id!=item.id))predicted->direction=static_cast<std::uint8_t>(*known.heading);
            if(phase==SessionPositionPhase::publish){
                if(id!=item.id)return false;
                if(container)return actual.attachment&&actual.attachment->container==container->id&&actual.attachment->slot==slot&&
                    (actual.position==known.position||actual.position==predicted)&&word(world_,id,1)==known.heading;
                const auto same_xyz=[](const auto& a,const auto& b){return a&&b&&a->x==b->x&&a->y==b->y&&a->level==b->level;};
                const bool heading_only=same_xyz(actual.position,known.position)&&actual.position->direction==*direction;
                const auto heading=word(world_,id,1);
                return !actual.attachment&&(actual.position==known.position||heading_only||actual.position==predicted)&&
                    heading&&(heading==known.heading||*heading==*direction);
            }
            if(word(world_,id,1)!=known.heading||!link(known.attachment,actual.attachment)||!(actual.position==predicted))return false;
            if(id==item.id&&!container)return true;
            const auto* actual_parent=actual.attachment?world_.find(actual.attachment->container):nullptr;
            return actual_parent&&actual.attachment->slot<actual_parent->slots.size()&&actual_parent->slots[actual.attachment->slot]==id&&
                (id==item.id||nodes.count(actual_parent->id));
        };
        end.completed=operation()&&authority_()&&registry_.complete&&pending_footprints_.empty();return end.completed;
    }
    void invalidate(){registry_.complete=false;}
    bool complete()const{return registry_.complete&&pending_footprints_.empty();}
};

struct CandidateBinding {
    ObjectTradeCandidateContext context;
    ArchitectureObjectState* architecture_objects=nullptr;
    const SessionBundle& source;
    std::vector<ObjectTradeGroup> groups;
    const PreparedArchitecture& prepared;const SavedArchitecture& architecture;const GeometricRoomMap& rooms;
    const ObjectTradeSessionHooks& hooks;
    std::int16_t selected_id;CodeOwner selected_owner;Vm* selected_thread=nullptr;
    std::map<std::int16_t,Vm*> bound_threads;
    std::map<std::pair<std::int16_t,std::uint64_t>,ObjectTradeMemberAdmission> member_admissions;
    std::shared_ptr<const ObjectTradeGroupResources> trade_plan;
    std::optional<ObjectGroup> active_group;
    std::vector<ObjectGroup> source_physical;
    bool positions_initialized=false;
    std::uint64_t* instruction_budget=nullptr;
    ObjectGroupSnapshot* owned_groups=nullptr;ObjectValueRegistry* values=nullptr;
    std::weak_ptr<DynamicObjectGroups> dynamic_groups;
    std::map<std::int16_t,std::unique_ptr<Vm>> threads;
    std::map<std::int16_t,CodeOwner> primary;
    std::map<CodeOwner,std::uint32_t> owner_guids;
    VmMemory memory;
    std::shared_ptr<CandidateRegistryGuard> registry_guard;
    SessionObjectPositionServices positions;
    std::function<bool()> authority;
    CandidateBinding(World& world,RelationshipState& relationships,SessionHostState& host,RandomState& random,
            RuntimeGlobals* globals,const Ts1Clock* clock,ObjectTradeResourceLibrary& resources,const SessionBundle& source_,
            const PreparedArchitecture& p,const SavedArchitecture& a,const GeometricRoomMap& r,const ObjectTradeSessionHooks& h,
            std::vector<ObjectTradeGroup> g,std::int16_t selected,CodeOwner owner,std::function<bool()> proof)
        :context{world,relationships,host,random,globals,clock,resources,{}},source(source_),groups(std::move(g)),
         prepared(p),architecture(a),rooms(r),hooks(h),selected_id(selected),selected_owner(owner),authority(std::move(proof)){
        context.thread_for=[this](auto id)->Vm*{const auto bound=bound_threads.find(id);
            if(bound!=bound_threads.end())return bound->second;
            if(id==selected_id&&selected_thread)return selected_thread;
            const auto found=threads.find(id);return found==threads.end()?nullptr:found->second.get();};
        memory=world.vm_memory();const auto read=memory.read;const auto write=memory.write;
        memory.job_data=hooks.job_data;
        memory.read=[this,read](const VmFrame& frame,std::uint8_t scope,std::int16_t index)->std::optional<std::int16_t>{
            if(scope==6){
                if(context.globals)return context.globals->read(index,context.clock);
                if(context.clock){const auto value=context.clock->global(index);if(value)return value;}
                const auto entry=std::find_if(context.host.global_values.begin(),context.host.global_values.end(),[&](const auto& v){return v.index==index;});
                return entry==context.host.global_values.end()?std::nullopt:std::optional<std::int16_t>{entry->value};
            }
            if(scope==26){const auto location=BconTuning::address(index);const TuningTables* tables=&context.resources.global_tuning();
                if(location.scope!=RoutineScope::global){const auto guid=owner_guids.find(frame.context.code_owner);if(guid==owner_guids.end())return {};
                    const auto* resource=context.resources.find(guid->second);if(!resource)return {};
                    tables=location.scope==RoutineScope::semiglobal&&resource->tuning.semiglobal_state==TuningSemiState::loaded?
                        &resource->tuning.semiglobal_tables:&resource->tuning.private_tables;}
                const auto table=tables->find(location.table);
                // Exact archives were loaded before candidate entry. Authored
                // absence in a loaded bank has the source GetTuning zero value.
                if(table==tables->end()||location.index>=table->second.constants.size())return 0;
                return signed_word(table->second.constants[location.index]);
            }
            return read(frame,scope,index);
        };
        memory.write=[this,write](const VmFrame& frame,std::uint8_t scope,std::int16_t index,std::int16_t value){
            if(scope!=6)return write(frame,scope,index,value);
            if(context.globals)return context.globals->write(index,value);
            const auto entry=std::find_if(context.host.global_values.begin(),context.host.global_values.end(),[&](const auto& v){return v.index==index;});
            if(entry==context.host.global_values.end())return false;entry->value=value;return true;
        };
        memory.room_by_temp0=[this](const VmFrame&,auto temp,auto index){return room_by_temp0_data(rooms,temp,index);};
        world.set_definition_word_resolver([this](const Entity& e,std::int16_t index)->std::optional<std::int16_t>{
            if(!e.definition||index<0)return {};const auto* resource=context.resources.find(e.definition->guid);
            if(!resource||static_cast<std::size_t>(index)>=resource->definition_words.size())return {};
            return resource->definition_words[static_cast<std::size_t>(index)];
        });
        require(world.set_room_resolver([this](const EntityPosition& p){return geometric_room_at_runtime(rooms,p.x,p.y,p.level);}),"candidate room resolver rejected");
    }
    std::optional<ObjectGroup> physical(ObjectPositionIdentity id)const{
        const auto contains=[&](const ObjectGroup& g){return std::any_of(g.members.begin(),g.members.end(),[&](const auto& m){return m.id==id.id&&m.generation==id.generation;});};
        if(owned_groups)for(const auto& g:owned_groups->groups)if(contains(g))return g;
        if(active_group&&contains(*active_group))return active_group;
        for(const auto& g:source_physical)if(contains(g))return g;
        const auto*e=context.world.find(id.id);const auto*v=values?values->query({id.id,id.generation}):nullptr;
        if(e&&e->generation==id.generation&&e->definition&&!e->definition->master_guid&&v&&v->members.size()==1){const auto&m=v->members.front();
            return ObjectGroup{e->definition->guid,false,id.id,{{id.id,id.generation,m.offset_x,m.offset_y,m.offset_level}}};}
        return {};
    }
    bool authenticate_imported(const ObjectGroup& group)const{
        if(group.definition_guid||!authority())return false;
        const ObjectGroup* expected=nullptr;
        if(active_group&&trade_plan&&trade_plan->imported&&same_physical_group(group,*active_group))expected=&*active_group;
        if(!expected)for(const auto& candidate:source_physical)if(!candidate.definition_guid&&same_physical_group(group,candidate)){expected=&candidate;break;}
        if(!expected)return false;
        for(const auto& member:expected->members){
            const auto source_entity=std::find_if(source.world.entities.begin(),source.world.entities.end(),[&](const auto& e){return e.id==member.id;});
            const auto* live=context.world.find(member.id);
            if(source_entity==source.world.entities.end()||source_entity->generation!=member.generation||!source_entity->definition||
               !live||live->generation!=member.generation||!live->definition||live->definition->guid!=source_entity->definition->guid||
               live->definition->master_guid!=source_entity->definition->master_guid)return false;
            const auto* values=value_group_for(source,{member.id,member.generation});if(!values)return false;
            const auto value=std::find_if(values->members.begin(),values->members.end(),[&](const auto& v){return v.object.id==member.id&&v.object.generation==member.generation;});
            const auto* resource=context.resources.find(live->definition->guid);
            if(value==values->members.end()||!resource||!same_resource_origin(value->object,resource->code.origin)||
               value->object.master_guid!=live->definition->master_guid||value->offset_x!=member.offset_x||
               value->offset_y!=member.offset_y||value->offset_level!=member.offset_level)return false;
        }
        return authority();
    }
    bool authenticate_live(const ObjectGroup& group)const{
        if(!authority()||!owned_groups||!values||group.kind!=ObjectGroupKind::live_association)return false;
        const auto saved=std::find_if(owned_groups->groups.begin(),owned_groups->groups.end(),[&](const auto&g){return g.base_id==group.base_id;});
        if(saved==owned_groups->groups.end()||!same_physical_group(group,*saved))return false;
        const auto*v=values->query({group.base_id,context.world.find(group.base_id)?context.world.find(group.base_id)->generation:0});
        if(!v||v->members.size()!=group.members.size()||!v->base||v->base->id!=group.base_id)return false;
        for(std::size_t n=0;n<group.members.size();++n){const auto&m=group.members[n];const auto&source=v->members[n];
            const auto*e=context.world.find(m.id);const auto*r=e&&e->definition?context.resources.find(e->definition->guid):nullptr;
            if(!e||e->generation!=m.generation||!m.origin||!r||!same_resource_origin(source.object,r->code.origin)||
                source.object.id!=m.id||source.object.generation!=m.generation||source.offset_x!=m.offset_x||source.offset_y!=m.offset_y||source.offset_level!=m.offset_level||
                m.origin->guid!=source.object.guid||m.origin->master_guid!=source.object.master_guid||m.origin->source!=source.object.source||m.origin->asset!=source.object.asset||m.origin->definition_chunk_id!=source.object.definition_chunk_id)return false;
        }
        return true;
    }
    std::optional<bool> admit_member(ObjectPositionIdentity id)const{
        if(!authority()||!physical(id))return {};
        const auto*e=context.world.find(id.id);const auto*r=e&&e->definition?context.resources.find(e->definition->guid):nullptr;
        if(!e||e->generation!=id.generation||e->kind!=EntityKind::object||e->ui_ghost||!e->definition||!r||
           !e->entry_points||e->entry_points->size()<=15)return {};
        const auto flags=word(context.world,id.id,40),light=word(context.world,id.id,51),category=word(context.world,id.id,59);
        if(!flags||!light||!category)return {};
        const bool walls=architecture_objects&&architecture_objects->position_registry&&
            architecture_objects->position_registry->complete&&r->wall_style&&(std::uint16_t(*flags)&0xc000U);
        return (!(static_cast<std::uint16_t>(*flags)&0xc000U)||walls)&&!*light&&*category!=8&&
            (!e->entry_points->at(15).action||walls);
    }
    void groups_changed(){
        if(!owned_groups)return;std::set<std::pair<std::int16_t,std::uint64_t>> owned;
        for(const auto&g:owned_groups->groups)for(const auto&m:g.members)owned.emplace(m.id,m.generation);
        groups.erase(std::remove_if(groups.begin(),groups.end(),[&](const auto&g){return std::any_of(g.members.begin(),g.members.end(),[&](const auto&m){return owned.count({m.id,m.generation});});}),groups.end());
        for(const auto&g:owned_groups->groups){ObjectTradeGroup membership;membership.complete=true;for(const auto&m:g.members)membership.members.push_back({m.id,m.generation});groups.push_back(std::move(membership));}
    }
    VmPrimitive primitive(const VmMemory& stable_memory){
        auto rng=random_primitive(context.random,stable_memory);
        auto budget=ts1_budget_primitive(stable_memory,[this](const VmFrame&){return context.host.family_budget;},
            [this](const VmFrame&,std::int32_t value){if(!context.host.family_budget)return false;context.host.family_budget=value;return true;});
        auto type=object_type_primitive(context.world);
        auto distance=get_distance_to_primitive(context.world,stable_memory);
        auto direction=get_direction_to_primitive(context.world,stable_memory);
        // The transaction owns this restored relationship matrix. Both TS1
        // and legacy opcodes must use the same candidate World/register memory;
        // never forward relationship effects into the live session owner.
        auto relationships=context.relationships.primitive(context.world,stable_memory);
        auto refresh=session_refresh_primitive(context.world,&rooms);
        auto extra=hooks.extra_primitive_factory?hooks.extra_primitive_factory(context,stable_memory):VmPrimitive{};
        return [this,rng=std::move(rng),budget=std::move(budget),type=std::move(type),relationships=std::move(relationships),distance=std::move(distance),direction=std::move(direction),refresh=std::move(refresh),&stable_memory,extra=std::move(extra)]
            (const VmFrame& frame,const Instruction& instruction,const Ts1Temps& temps,const VmDispatch& dispatch)->PrimitiveOutcome{
            if(!authority())return {PrimitiveDisposition::unsupported,"object command authority expired"};
            switch(instruction.opcode){case 7:return refresh(frame,instruction,temps,dispatch);case 8:return rng(frame,instruction,temps,dispatch);case 25:return budget(frame,instruction,temps,dispatch);
                case 31:{std::vector<std::vector<std::int16_t>> group_ids;
                    for(const auto& group:groups){std::vector<std::int16_t> ids;for(const auto& member:group.members)ids.push_back(member.id);group_ids.push_back(std::move(ids));}
                    return entity_next_primitive(context.world,stable_memory,{},std::move(group_ids))(frame,instruction,temps,dispatch);}
                case 11:return distance(frame,instruction,temps,dispatch);
                case 12:return direction(frame,instruction,temps,dispatch);
                case 24:case 26:return relationships(frame,instruction,temps,dispatch);
                case 32:return type(frame,instruction,temps,dispatch);default:break;}
            return extra?extra(frame,instruction,temps,dispatch):PrimitiveOutcome{PrimitiveDisposition::unsupported,
                "candidate object command requires native primitive "+std::to_string(instruction.opcode)};
        };
    }
};
// A transaction may preserve a complete event ledger, never manufacture one
// from final coordinates. This verifies the real events against the candidate;
// no entries, completion flags or loading scripts are written here.
void require_coherent_candidate_registry(const SessionBundle& source,const WorldSnapshot& world,
        const std::optional<ArchitectureObjectState>& objects,const GeometricRoomMap& rooms){
    if(!source.architecture_object_state||!source.architecture_object_state->position_registry||
       !source.architecture_object_state->position_registry->complete)return;
    require(objects&&objects->position_registry,
        "object command lost actual position registry");
    const auto& registry=*objects->position_registry;
    require(registry.complete,"object command left actual position registry incomplete");
    validate_object_position_registry_snapshot(registry);
    std::map<std::int16_t,const Entity*> live;
    for(const auto& e:world.entities)if(!e.ui_ghost)live.emplace(e.id,&e);
    const auto entity=[&](ObjectPositionIdentity id)->const Entity&{
        const auto at=live.find(id.id);require(at!=live.end()&&at->second->generation==id.generation,
            "object command registry retains a stale owner");return *at->second;
    };
    std::map<std::int16_t,unsigned> tiles,room_members,bindings;
    for(const auto& tile:registry.tiles)for(const auto id:tile.entities){const auto& e=entity(id);
        const auto offset=e.position?(e.position->level-1)*std::int32_t(registry.width*registry.height)+
            (e.position->y/16)*std::int32_t(registry.width)+e.position->x/16:-1;
        require(tile.offset==offset,"object command registry tile differs from actual pose");++tiles[id.id];
    }
    for(const auto& room:registry.rooms){
        for(const auto id:room.entities){const auto& e=entity(id);
            const auto actual=e.position?geometric_room_at_runtime(rooms,e.position->x,e.position->y,e.position->level):std::optional<std::uint16_t>{0};
            require(actual&&*actual==room.id,"object command registry room differs from actual pose");++room_members[id.id];
        }
        for(const auto* collection:{&room.static_obstacles,&room.dynamic_obstacles})for(const auto& obstacle:*collection){
            (void)entity(obstacle.owner);
            const auto binding=std::find_if(registry.footprints.begin(),registry.footprints.end(),[&](const auto& value){
                return value.owner.id==obstacle.owner.id&&value.owner.generation==obstacle.owner.generation&&
                    value.present&&value.instance==obstacle.instance&&value.box==obstacle.box&&value.room==room.id;
            });require(binding!=registry.footprints.end(),"object command obstacle lacks actual footprint binding");
        }
        for(const auto* collection:{&room.portals,&room.window_portals})for(const auto& portal:*collection)(void)entity(portal.owner);
    }
    for(const auto& binding:registry.footprints){(void)entity(binding.owner);++bindings[binding.owner.id];}
    for(const auto& motion:registry.motion)(void)entity(motion.owner);
    for(const auto& row:live){
        if(tiles[row.first]==1&&room_members[row.first]==1&&bindings[row.first]==1)continue;
        std::string reason;
        if(tiles[row.first]==0&&room_members[row.first]==0&&
           object_position_known_absent_oow(registry,*row.second,reason))continue;
        const auto& body=*row.second;
        throw FormatError("object command actual registry does not cover its complete live cohort: id="+
            std::to_string(body.id)+" generation="+std::to_string(body.generation)+
            " tiles="+std::to_string(tiles[row.first])+" rooms="+std::to_string(room_members[row.first])+
            " footprints="+std::to_string(bindings[row.first])+" pose="+(body.position?"in-world":"OOW")+
            " native_level="+(body.native_out_of_world_level?std::to_string(*body.native_out_of_world_level):"unknown")+
            (reason.empty()?"":"; "+reason));
    }
    if(source.architecture_object_state->lighting)
        require(objects->lighting.has_value(),"object command lost actual lighting owner");
}

ObjectTradeBindingFactory candidate_factory(ObjectTradeResourceLibrary& resources,const SessionBundle& source,
        const PreparedArchitecture& prepared,const SavedArchitecture& architecture,const GeometricRoomMap& rooms,
        const ObjectTradeSessionHooks& hooks,const std::vector<ObjectTradeGroup>& groups,const std::vector<ThreadSeed>& seeds,
        std::int16_t selected,CodeOwner owner,std::uint32_t guid,std::function<bool()> authority,
        ArchitectureObjectSupport* candidate_support,ObjectPositionRegistrySnapshot* candidate_registry,ArchitectureObjectState* candidate_objects){
    std::vector<ObjectGroup> source_physical;
    for(const auto& membership:groups){
        if(!membership.complete||membership.members.empty())continue;
        const auto first=membership.members.front();std::optional<ObjectGroup> physical;
        if(source.object_groups)for(const auto& group:source.object_groups->groups)
            if(std::any_of(group.members.begin(),group.members.end(),[&](const auto& m){return m.id==first.id&&m.generation==first.generation;}))physical=group;
        if(!physical&&hooks.physical_group)physical=hooks.physical_group({first.id,first.generation});
        if(!physical&&membership.members.size()==1){
            const auto e=std::find_if(source.world.entities.begin(),source.world.entities.end(),[&](const auto& item){return item.id==first.id&&item.generation==first.generation;});
            if(e!=source.world.entities.end()&&e->definition&&!e->definition->master_guid)
                physical=ObjectGroup{e->definition->guid,false,first.id,{{first.id,first.generation,0,0,0}}};
        }
        if(physical){require(exact_group_members(*physical,membership),"candidate physical manifest differs from source cohort");source_physical.push_back(std::move(*physical));}
    }
    return [&,groups,seeds,source_physical,selected,owner,guid,authority,candidate_support,candidate_registry,candidate_objects](World& world,RelationshipState& relationships,SessionHostState& host,RandomState& random,
            RuntimeGlobals* globals,const Ts1Clock* clock,const ObjectTradeResource& command_resource)->ObjectTradeRuntimeBinding{
        auto state=std::make_shared<CandidateBinding>(world,relationships,host,random,globals,clock,resources,source,
            prepared,architecture,rooms,hooks,groups,selected,owner,authority);
        state->owner_guids[owner]=guid;state->primary[selected]=owner;
        state->trade_plan=command_resource.group;state->source_physical=source_physical;
        auto selected_policy=VmSavedRoutinePolicy::require_present;
        std::optional<ObjectTradeSelectedPrograms> selected_programs;
        for(const auto& seed:seeds){
            const auto* e=world.find(seed.id);require(e&&e->generation==seed.generation,"candidate thread identity differs from capture");
            require(seed.programs.globals&&seed.programs.primary_owner,"candidate thread programs are unavailable");
            require(seed.programs.saved_routine_policy==VmSavedRoutinePolicy::require_present||
                (seed.programs.saved_routine_policy==VmSavedRoutinePolicy::defer_missing_or_pc_fault&&e->kind==EntityKind::object),
                "deferred source restore policy requires an authenticated imported object Thread");
            if(e->kind==EntityKind::object)require(state->member_admissions.emplace(std::make_pair(seed.id,seed.generation),
                ObjectTradeMemberAdmission{{seed.programs.globals,seed.programs.owners},seed.programs.saved_routine_policy}).second,
                "candidate source member admission duplicated");
            for(const auto& record:source.runtime_entities)if(record.id==seed.id)for(const auto& bank:record.resource_owners)state->owner_guids[bank.owner]=bank.resource_guid;
            for(const auto& record:source.object_threads)if(record.id==seed.id)for(const auto& bank:record.resource_owners)state->owner_guids[bank.owner]=bank.resource_guid;
            if(e->definition)state->owner_guids[seed.programs.primary_owner]=e->definition->guid;
            state->primary[seed.id]=seed.programs.primary_owner;
            if(seed.id==selected){
                require(seed.programs.primary_owner==owner&&e->kind==EntityKind::object,
                    "selected source Thread owner differs from command authority");
                selected_policy=seed.programs.saved_routine_policy;
                selected_programs=ObjectTradeSelectedPrograms{seed.programs.globals,seed.programs.owners};continue;
            }
            auto primitive=state->primitive(state->memory);
            auto vm=std::make_unique<Vm>(seed.programs.globals,ShareVmProgramTag{},state->memory,VmLimits{},std::move(primitive),seed.programs.owners,ShareVmOwnerProgramsTag{});
            if(!vm->restore_snapshot(seed.state,seed.programs.saved_routine_policy))throw FormatError(register_owner_restore_failure(seed));
            state->threads.emplace(seed.id,std::move(vm));
        }
        state->architecture_objects=candidate_objects;
        ObjectTradeRuntimeBinding binding;binding.memory=state->memory;
        binding.native_group_construction=hooks.native_group_construction;
        binding.selected_saved_routine_policy=selected_policy;
        binding.selected_programs=std::move(selected_programs);
        binding.command_still_matches=[state]{return state->authority()&&(!state->registry_guard||state->registry_guard->complete());};
        binding.primitive_factory=[state](const VmMemory& memory){return state->primitive(memory);};
        binding.capture_architecture_state=[state,candidate_support,candidate_registry,candidate_objects](const WorldSnapshot& world){
            ObjectTradeArchitectureCapture captured;
            captured.object_support=state->source.architecture_object_support;
            captured.object_state=candidate_objects?std::optional<ArchitectureObjectState>{*candidate_objects}:state->source.architecture_object_state;
            if(captured.object_state){
                // Capture the SAME event-owned candidate AOBJ: wall use/style,
                // support and registry. Unrelated colors/deferred light receipts
                // stay recorded; visual identities are reconciled below without
                // rerunning fresh-load position constructors.
                if(candidate_support)captured.object_state->object_support=*candidate_support;
                if(candidate_registry)captured.object_state->position_registry=*candidate_registry;
                if(captured.object_state->lighting){
                    // Capture classifications from this candidate's actual
                    // object fields after position/lifecycle events. New or
                    // moved identities must be drawable before the first tick
                    // of a fresh process. This does not drain lighting requests
                    // or recalculate/replace the last committed room colors.
                    auto& lighting=*captured.object_state->lighting;
                    const auto live=[&](ObjectPositionIdentity id){const auto* e=state->context.world.find(id.id);
                        return e&&e->generation==id.generation&&!e->dead&&!e->ui_ghost;};
                    lighting.visual_rooms.erase(std::remove_if(lighting.visual_rooms.begin(),lighting.visual_rooms.end(),
                        [&](const auto& visual){return !live(visual.owner);}),lighting.visual_rooms.end());
                    for(const auto& e:world.entities){
                        if(e.dead||e.ui_ghost)continue;
                        const auto before=std::find_if(state->source.world.entities.begin(),state->source.world.entities.end(),
                            [&](const auto& old){return old.id==e.id&&old.generation==e.generation;});
                        const bool position_changed=before!=state->source.world.entities.end()&&
                            (!(before->position==e.position)||bool(before->attachment)!=bool(e.attachment)||
                             (e.attachment&&(before->attachment->container!=e.attachment->container||
                                 before->attachment->slot!=e.attachment->slot||before->attachment->positioned!=e.attachment->positioned)));
                        if(position_changed||!room_lighting_cached_visual_room(lighting,{e.id,e.generation}))
                            require(refresh_room_lighting_visual(lighting,state->context.world,{e.id,e.generation},0),
                                "candidate object visual room classification unavailable");
                    }
                }
                if(captured.object_state->position_registry){
                    auto& registry=*captured.object_state->position_registry;
                    const auto absent=[&](const auto& value){return std::none_of(world.entities.begin(),world.entities.end(),[&](const auto& e){
                        return e.id==value.owner.id&&e.generation==value.owner.generation;
                    });};
                    // Only constructor metadata is retired here, after the real
                    // unregister operations removed RoomInfo/query/obstacles.
                    // Coherence below rejects any remaining stale event refs.
                    registry.motion.erase(std::remove_if(registry.motion.begin(),registry.motion.end(),absent),registry.motion.end());
                    registry.footprints.erase(std::remove_if(registry.footprints.begin(),registry.footprints.end(),absent),registry.footprints.end());
                }
            }else if(captured.object_support&&candidate_support)captured.object_support=*candidate_support;
            require_coherent_candidate_registry(state->source,world,captured.object_state,state->rooms);
            return captured;
        };
        const auto initialize_positions=[state,&resources,&prepared,&architecture,&rooms,candidate_support,candidate_registry,candidate_objects,selected,guid]{
            if(state->positions_initialized)return;
            if(!state->active_group&&std::none_of(state->source.world.entities.begin(),state->source.world.entities.end(),[&](const auto& e){return e.id==selected;})){
                const auto* e=state->context.world.find(selected);require(e&&e->definition&&e->definition->guid==guid,"new singleton lacks actual constructor identity");
                state->groups.push_back({true,{{selected,e->generation}}});
                state->source_physical.push_back({guid,false,selected,{{selected,e->generation,0,0,0}}});
                if(candidate_registry){
                    require(record_object_footprint_absent(*candidate_registry,state->context.world,{selected,e->generation},state->authority),"fresh constructor footprint state unavailable");
                    require(record_object_moved_self(*candidate_registry,state->context.world,{selected,e->generation},false,state->authority),"fresh constructor motion state unavailable");
                }
            }
            SessionObjectPositionContext ctx;ctx.world=&state->context.world;ctx.resources=&resources;
            ctx.object_support=candidate_support;ctx.architecture_objects=candidate_objects;
            ctx.position_registry=candidate_registry;
            ctx.prepared=&prepared;ctx.architecture=&architecture;ctx.rooms=&rooms;ctx.groups=state->groups;
            ctx.thread_for=state->context.thread_for;const auto weak=std::weak_ptr<CandidateBinding>(state);
            ctx.owner_for=[weak](auto id)->std::optional<CodeOwner>{
                const auto live=weak.lock();if(!live)return {};
                const auto it=live->primary.find(id);return it==live->primary.end()?std::nullopt:std::optional<CodeOwner>{it->second};};
            ctx.instruction_budget=state->instruction_budget;
            ctx.owner_programs=[weak](auto id){const auto s=weak.lock();if(!s)return SharedRoutineOwners{};const auto*e=s->context.world.find(id);if(!e)return SharedRoutineOwners{};
                const auto admission=s->member_admissions.find({id,e->generation});if(admission!=s->member_admissions.end())return admission->second.programs.owners;
                const auto owner=s->primary.find(id);auto*thread=s->context.thread_for(id);if(owner==s->primary.end()||!thread)return SharedRoutineOwners{};
                return SharedRoutineOwners{{owner->second,thread->shared_program_for_owner(owner->second)}};};
            ctx.memory=state->memory;ctx.primitive_factory=[weak](const VmMemory& memory){const auto live=weak.lock();return live?live->primitive(memory):VmPrimitive{};};
            ctx.source_still_matches=state->authority;
            ctx.registration_group=[weak](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{
                const auto live=weak.lock();if(!live||!live->authority())return {};
                for(const auto& group:live->groups)if(std::any_of(group.members.begin(),group.members.end(),[&](const auto& m){return m.id==id.id&&m.generation==id.generation;})){
                    if(!group.complete)return {};std::vector<ObjectPositionIdentity> result;
                    for(const auto& member:group.members){const auto* e=live->context.world.find(member.id);
                        if(!e||e->generation!=member.generation)return {};result.push_back({member.id,member.generation});}
                    return result;
                }return {};
            };
            ctx.group=[weak](auto id)->std::optional<ObjectGroup>{const auto live=weak.lock();return live&&live->authority()?live->physical(id):std::nullopt;};
            ctx.authenticate_imported_group=[weak](const auto& group){const auto live=weak.lock();return live&&live->authenticate_imported(group);};
            ctx.authenticate_live_group=[weak](const auto&group){const auto live=weak.lock();return live&&live->authenticate_live(group);};
            ctx.dynamic_group_update=[weak](auto id){const auto live=weak.lock();const auto d=live?live->dynamic_groups.lock():nullptr;return d&&d->update(id);};
            ctx.split_dynamic_group=[weak](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{const auto live=weak.lock();const auto d=live?live->dynamic_groups.lock():nullptr;return d?d->split_for_move(id):std::nullopt;};
            ctx.update_dynamic_group_flags=[weak](auto id){const auto live=weak.lock();const auto d=live?live->dynamic_groups.lock():nullptr;return d&&d->update(id,false);};
            ctx.begin_native_rotation=[weak](auto id,auto heading)->std::optional<NativeObjectGroupRotation>{const auto live=weak.lock();const auto d=live?live->dynamic_groups.lock():nullptr;return d?d->begin_native_rotation(id,heading):std::nullopt;};
            ctx.rollback_native_rotation=[weak](const auto& rotation){const auto live=weak.lock();const auto d=live?live->dynamic_groups.lock():nullptr;return d&&d->rollback_native_rotation(rotation);};
            ctx.native_current_offset=[weak](auto id)->std::optional<NativeObjectGroupOffset>{const auto live=weak.lock();const auto d=live?live->dynamic_groups.lock():nullptr;return d?d->native_offset(id):std::nullopt;};
            ctx.native_group_head=[weak](auto id){const auto live=weak.lock();const auto d=live?live->dynamic_groups.lock():nullptr;return d?d->group_head(id):NativeObjectGroupLink{};};
            ctx.native_group_next=[weak](auto id){const auto live=weak.lock();const auto d=live?live->dynamic_groups.lock():nullptr;return d?d->group_next(id):NativeObjectGroupLink{};};
            ctx.admit_group_member=[weak](auto id)->std::optional<bool>{const auto live=weak.lock();return live?live->admit_member(id):std::nullopt;};
            if(candidate_registry){
                state->registry_guard=std::make_shared<CandidateRegistryGuard>(state->context.world,*candidate_registry,state->authority,candidate_objects);
                const auto guard=state->registry_guard;
                ctx.position_phase=[guard](auto id,const auto& desired,auto phase,auto direction,const auto& operation){
                    return guard->phase(id,{},0,phase,desired,direction,operation);
                };
                ctx.slot_phase=[guard,weak](auto item,auto container,std::size_t slot,auto phase,const auto& operation){
                    const auto live=weak.lock();const auto* parent=live?live->context.world.find(container.id):nullptr;
                    return parent&&parent->generation==container.generation&&
                        guard->phase(item,container,slot,phase,parent->position,{},operation);
                };
            }
            state->positions=make_session_object_position_services(std::move(ctx));
            require(state->context.world.set_spatial_change_observer([weak](World& emitter,std::uint64_t){
                const auto live=weak.lock();if(!live||&emitter!=&live->context.world||!live->registry_guard)return;
                try{live->registry_guard->observe();}catch(...){
                    if(live->source.architecture_object_state&&live->source.architecture_object_state->position_registry)
                        live->registry_guard->invalidate();
                }
            }),"candidate spatial publication guard failed");
            require(state->context.world.set_footprint_observer([weak](World& emitter,std::int16_t id,std::uint64_t generation){
                const auto live=weak.lock();if(!live||&emitter!=&live->context.world)return false;
                if(!live->source.architecture_object_state||!live->source.architecture_object_state->position_registry)return true;
                const bool accepted=live->positions.update_footprint&&live->positions.update_footprint({id,generation});
                if(live->registry_guard)live->registry_guard->footprint_receipt({id,generation},accepted);return accepted;
            }),"candidate footprint shape binding failed");
            require(state->context.world.set_static_footprint_observer([weak](World& emitter,std::int16_t id,std::uint64_t generation,bool,bool value){
                const auto live=weak.lock();if(!live||&emitter!=&live->context.world)return false;
                if(!live->positions.static_footprint_changed)return false;
                // This candidate has no event ledger in legacy sessions.
                if(!live->source.architecture_object_state||!live->source.architecture_object_state->position_registry)return true;
                const bool accepted=live->positions.static_footprint_changed({id,generation},value);
                if(live->registry_guard)live->registry_guard->footprint_receipt({id,generation},accepted);return accepted;
            }),"candidate static footprint event binding failed");
            state->positions_initialized=true;
        };
        if(state->trade_plan)binding.on_group_materialized=[state,candidate_registry](const ObjectGroup& physical){
            require(state->authority()&&!state->active_group&&state->trade_plan,"candidate group already bound or lacks source plan");
            const auto& plan=*state->trade_plan;
            require(physical.members.size()==plan.parts.size()&&plan.base_index<plan.parts.size()&&physical.multitile==plan.multitile&&
                physical.base_id==physical.members[plan.base_index].id&&physical.definition_guid==(plan.requested?plan.requested->guid:0),
                "candidate materialized group differs from source plan");
            ObjectTradeGroup membership;membership.complete=true;
            bool fresh=false;
            for(std::size_t n=0;n<physical.members.size();++n){const auto& member=physical.members[n];const auto& part=plan.parts[n];
                const auto* e=state->context.world.find(member.id);
                require(e&&e->generation==member.generation&&e->definition&&e->definition->guid==part.resource.origin.guid&&
                    e->definition->master_guid==part.resource.origin.master_guid&&member.offset_x==part.offset_x&&
                    member.offset_y==part.offset_y&&member.offset_level==part.offset_level,"candidate group actual member differs from exact part resource");
                if(part.resource.origin.id)require(part.resource.origin.id==member.id&&part.resource.origin.generation==member.generation,"candidate group reused source member identity");
                membership.members.push_back({member.id,member.generation});
                const bool existing=std::any_of(state->source.world.entities.begin(),state->source.world.entities.end(),[&](const auto& old){return old.id==member.id&&old.generation==member.generation;});
                fresh|=!existing;
                if(existing)require(state->primary.count(member.id),"source group member Thread programs unavailable");
                else{state->primary[member.id]=part.resource.origin.guid;state->owner_guids[part.resource.origin.guid]=part.resource.origin.guid;}
                if(!existing&&candidate_registry){
                    require(record_object_footprint_absent(*candidate_registry,state->context.world,{member.id,member.generation},state->authority),"fresh group member footprint state unavailable");
                    require(record_object_moved_self(*candidate_registry,state->context.world,{member.id,member.generation},false,state->authority),"fresh group member motion state unavailable");
                }
            }
            if(fresh){
                for(const auto& member:physical.members)require(std::none_of(state->source.world.entities.begin(),state->source.world.entities.end(),[&](const auto& old){return old.id==member.id;}),"candidate group mixes fresh and source members");
                const ObjectTradeGroup* already=nullptr;
                for(const auto& group:state->groups){
                    const bool overlaps=std::any_of(group.members.begin(),group.members.end(),[&](const auto& a){return std::any_of(membership.members.begin(),membership.members.end(),[&](const auto& b){return a.id==b.id;});});
                    if(!overlaps)continue;
                    require(!already&&group.complete&&group.members.size()==membership.members.size(),"candidate fresh group overlaps partial/conflicting membership");
                    for(std::size_t i=0;i<group.members.size();++i)require(group.members[i].id==membership.members[i].id&&group.members[i].generation==membership.members[i].generation,
                        "candidate fresh group order/identity differs from already-owned native group");
                    already=&group;
                }
                // Actual native metadata admission may have published this same
                // owner index before Init. Do not insert a second membership.
                if(!already)state->groups.push_back(std::move(membership));
            }
            state->active_group=physical;
        };
        binding.member_programs=[state](ObjectTradeIdentity id)->std::optional<ObjectTradeMemberAdmission>{
            if(!state->authority())return {};
            const auto admission=state->member_admissions.find({id.id,id.generation});
            return admission==state->member_admissions.end()?std::nullopt:std::optional<ObjectTradeMemberAdmission>{admission->second};
        };
        binding.on_member_thread_bound=[state,initialize_positions](ObjectTradeIdentity id,CodeOwner actual_owner,Vm& actual){
            const auto* e=state->context.world.find(id.id);
            require(state->authority()&&e&&e->generation==id.generation&&actual_owner&&state->primary.count(id.id)&&
                state->primary.at(id.id)==actual_owner&&!state->bound_threads.count(id.id),"candidate member Thread identity or binding unavailable");
            if(state->active_group)require(std::any_of(state->active_group->members.begin(),state->active_group->members.end(),
                [&](const auto& m){return m.id==id.id&&m.generation==id.generation;}),"candidate bound Thread is outside command group");
            state->bound_threads.emplace(id.id,&actual);state->primary[id.id]=actual_owner;
            if(e->definition)state->owner_guids[actual_owner]=e->definition->guid;
            initialize_positions();
        };
        binding.on_thread_bound=[state,initialize_positions,selected](Vm& actual){
            require(!state->selected_thread&&!state->bound_threads.count(selected),"candidate selected Thread rebound");
            state->selected_thread=&actual;state->bound_threads.emplace(selected,&actual);initialize_positions();
        };
        binding.placement.architecture=[state](auto a,const auto& b){return state->positions.placement.architecture(a,b);};
        binding.placement.intersections=[state](auto a,auto b,auto c){return state->positions.placement.intersections(a,b,c);};
        binding.placement.same_group=[state](auto a,auto b){return state->positions.placement.same_group(a,b);};
        binding.placement.ignore_intersection=[state](auto a,auto b){return state->positions.placement.ignore_intersection(a,b);};
        binding.placement.ghost_image=[state](auto a){return state->positions.placement.ghost_image(a);};
        binding.placement.slot0_height=[state](auto a){return state->positions.placement.slot0_height(a);};
        binding.placement.entry_point5=[state](const auto& a){return state->positions.placement.entry_point5(a);};
        binding.placement.publish_heading=[state](auto id,auto direction,const auto& setter){
            return state->positions.placement.publish_heading(id,direction,setter);
        };
        binding.on_instruction_budget=[state](std::uint64_t& budget){state->instruction_budget=&budget;};
        binding.execute_bound_entry=[state](auto id,std::uint16_t ep){return state->positions.execute_entry&&state->positions.execute_entry(id,ep,0,{0,0,0,0}).has_value();};
        binding.on_group_state_bound=[state](ObjectGroupSnapshot& groups,ObjectValueRegistry& values){
            state->owned_groups=&groups;state->values=&values;const auto weak=std::weak_ptr<CandidateBinding>(state);
            DynamicObjectGroupServices services;services.authority=state->authority;
            services.source_group=[weak](auto id)->std::optional<ObjectGroup>{const auto s=weak.lock();return s?s->physical(id):std::nullopt;};
            services.origin=[weak](auto id)->std::optional<ObjectGroupMemberOrigin>{const auto s=weak.lock();if(!s||!s->values)return {};const auto*g=s->values->query({id.id,id.generation});if(!g)return {};
                const auto m=std::find_if(g->members.begin(),g->members.end(),[&](const auto&m){return m.object.id==id.id&&m.object.generation==id.generation;});if(m==g->members.end())return {};
                const bool native=std::any_of(s->source.created_objects.begin(),s->source.created_objects.end(),[&](const auto&o){return o.id==id.id&&o.generation==id.generation;})||
                    std::none_of(s->source.world.entities.begin(),s->source.world.entities.end(),[&](const auto&e){return e.id==id.id&&e.generation==id.generation;});
                const auto&o=m->object;return ObjectGroupMemberOrigin{native?ObjectGroupMemberSource::native_created:ObjectGroupMemberSource::original_import,o.guid,o.master_guid,o.source,o.asset,o.definition_chunk_id};};
            services.definition_offset=[weak](auto id)->std::optional<ObjectGroupMember>{const auto s=weak.lock();const auto*e=s?s->context.world.find(id.id):nullptr;const auto*r=e&&e->definition?s->context.resources.find(e->definition->guid):nullptr;if(!r||!r->code.runtime.definition.level_offset)return {};
                const auto sub=static_cast<std::uint16_t>(r->code.runtime.definition.sub_index);const auto signed_byte=[](unsigned n){return n<128?int(n):int(n)-256;};
                return ObjectGroupMember{id.id,id.generation,static_cast<std::int16_t>(signed_byte(sub>>8)*16),static_cast<std::int16_t>(signed_byte(sub&255)*16),static_cast<std::int16_t>(*r->code.runtime.definition.level_offset)};};
            services.execute_entry8=[weak](auto id,std::int16_t flags){const auto s=weak.lock();return s&&s->positions.execute_entry&&s->positions.execute_entry(id,8,0,{flags,0,0,0}).has_value();};
            services.changed=[weak]{const auto s=weak.lock();if(s)s->groups_changed();};
            services.publish_direction=[weak](auto id,std::uint8_t direction){const auto s=weak.lock();if(!s||!s->positions.placement.publish_heading)return false;
                return s->positions.placement.publish_heading(id,direction,[s,id,direction]{VmFrame frame;frame.context.caller=id.id;return s->context.world.write(frame,3,1,direction);});};
            services.publish_native_direction=[weak](auto id,std::uint8_t direction){const auto s=weak.lock();if(!s||!s->positions.placement.publish_heading)return false;
                return s->positions.placement.publish_heading({id.id,id.generation},direction,[s,id,direction]{return s->context.world.publish_native_object_heading(id,direction);});};
            auto dynamic=std::make_shared<DynamicObjectGroups>(state->context.world,values,groups,std::move(services));state->dynamic_groups=dynamic;return dynamic;
        };
        binding.position_change.story_count=[state]{return state->positions.change.story_count();};
        binding.position_change.admit_single_ordinary=[state](auto a){return state->positions.change.admit_single_ordinary(a);};
        binding.position_change.group=[state](auto id){return state->authority()?state->physical(id):std::nullopt;};
        binding.position_change.authenticate_imported_group=[state](const auto& physical){return state->authenticate_imported(physical);};
        binding.position_change.authenticate_live_group=[state](const auto& physical){return state->authenticate_live(physical);};
        binding.position_change.admit_group_member=[state](auto id){return state->admit_member(id);};
        binding.position_change.room_at=[state](const auto& a){return state->positions.change.room_at(a);};
        binding.position_change.unregister_position=[state](auto a,auto b){return state->positions.change.unregister_position(a,b);};
        binding.position_change.register_position=[state](auto a,auto b,auto c){return state->positions.change.register_position(a,b,c);};
        binding.position_change.place_in_slot0=[state](auto a,auto b){return state->positions.change.place_in_slot0(a,b);};
        binding.position_change.restore_container=[state](auto a,auto b,auto slot){return state->positions.change.restore_container(a,b,slot);};
        binding.position_change.wall_use=[state](auto a,auto b,auto c){return state->positions.change.wall_use(a,b,c);};
        binding.position_change.architectural_wall_style_change=[state](auto id,bool set){
            return state->positions.change.architectural_wall_style_change&&state->positions.change.architectural_wall_style_change(id,set);};
        binding.position_change.object_supported=[state](auto a,const auto& position,bool set){
            return state->positions.change.object_supported&&state->positions.change.object_supported(a,position,set);
        };
        binding.position_change.dynamic_multitile=[state](auto a){return state->positions.change.dynamic_multitile(a);};
        binding.position_change.split_dynamic_group=[state](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{const auto d=state->dynamic_groups.lock();return d?d->split_for_move(id):std::nullopt;};
        binding.position_change.update_dynamic_group_flags=[state](auto id){const auto d=state->dynamic_groups.lock();return d&&d->update(id,false);};
        binding.position_change.begin_native_rotation=[state](auto id,auto heading)->std::optional<NativeObjectGroupRotation>{const auto d=state->dynamic_groups.lock();return d?d->begin_native_rotation(id,heading):std::nullopt;};
        binding.position_change.rollback_native_rotation=[state](const auto& rotation){const auto d=state->dynamic_groups.lock();return d&&d->rollback_native_rotation(rotation);};
        binding.position_change.native_current_offset=[state](auto id)->std::optional<NativeObjectGroupOffset>{const auto d=state->dynamic_groups.lock();return d?d->native_offset(id):std::nullopt;};
        binding.position_change.native_group_head=[state](auto id){const auto d=state->dynamic_groups.lock();return d?d->group_head(id):NativeObjectGroupLink{};};
        binding.position_change.native_group_next=[state](auto id){const auto d=state->dynamic_groups.lock();return d?d->group_next(id):NativeObjectGroupLink{};};
        binding.position_change.entry_point9=[state](auto a){return state->positions.change.entry_point9(a);};
        binding.position_change.publish_position=[state](auto id,const auto& desired,auto direction){
            return state->positions.change.publish_position(id,desired,direction);
        };
        binding.position_change.lifecycle_phase=[state](auto id,auto phase,const auto& operation){
            return state->positions.change.lifecycle_phase(id,phase,operation);
        };
        return binding;
    };
}
}

ObjectTradeResource resolve_object_trade_source_group(ObjectTradeResourceLibrary& resources,
        const SessionBundle& source,const ObjectTradeSessionHooks& hooks,const std::vector<ObjectTradeGroup>& groups,
        ObjectTradeIdentity target){
    return existing_group_resource(resources,source,hooks,groups,{target.id,target.generation});
}

ObjectTradeBindingFactory make_object_trade_candidate_factory(ObjectTradeResourceLibrary& resources,
    const SessionBundle& source,const PreparedArchitecture& prepared,const SavedArchitecture& architecture,
    const GeometricRoomMap& rooms,const ObjectTradeSessionHooks& hooks,const std::vector<ObjectTradeGroup>& groups,
    std::int16_t selected,CodeOwner owner,std::uint32_t guid,std::function<bool()> authority,
    ArchitectureObjectSupport* support,ObjectPositionRegistrySnapshot* registry,ArchitectureObjectState* objects) {
    std::vector<ThreadSeed> seeds;
    const auto add=[&](std::int16_t id,const VmSnapshot& vm){
        const auto e=std::find_if(source.world.entities.begin(),source.world.entities.end(),[&](const auto& row){return row.id==id;});
        require(e!=source.world.entities.end()&&hooks.thread_programs,"candidate Thread lacks World/program authority");
        seeds.push_back({id,e->generation,vm,hooks.thread_programs(id,source)});
    };
    for(const auto& thread:source.runtime_entities)add(thread.id,thread.vm);
    for(const auto& thread:source.npc_avatars)add(thread.runtime.id,thread.runtime.vm);
    for(const auto& thread:source.object_threads)add(thread.id,thread.vm);
    return candidate_factory(resources,source,prepared,architecture,rooms,hooks,groups,seeds,selected,owner,guid,
        std::move(authority),support,registry,objects);
}

ObjectTradeSessionAdapter::ObjectTradeSessionAdapter(ObjectTradeResourceLibrary& resources,FamilySessionJournal& journal,
        const odh::PreparedArchitecture& prepared,const odh::SavedArchitecture& architecture,const odh::GeometricRoomMap& rooms,
        ObjectTradeSessionHooks hooks)
    :resources_(resources),journal_(journal),prepared_(prepared),architecture_(architecture),rooms_(rooms),hooks_(std::move(hooks)){}

ObjectTradeSessionResult ObjectTradeSessionAdapter::execute(const ObjectTradeSessionCommand& command){
    ObjectTradeSessionResult result;
    if(command.kind==ObjectTradeSessionCommandKind::cancel){result.state=ObjectTradeSessionState::cancelled;return result;}
    if(executing_){result.diagnostic="object command reentry";return result;}
    struct Guard{bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(executing_);
    bool published=false;
    try{
        require(hooks_.capture_current&&hooks_.validate_reopen&&hooks_.thread_programs&&hooks_.physical_groups,"object session hooks are incomplete");
        journal_.verify_current();const auto source=hooks_.capture_current();
        require(!source.build_buy_permission||source.build_buy_permission->enabled,"Build/Buy is disabled by the current lot simulation");
        require(source.family_lease&&same_lease(*source.family_lease,journal_.lease()),"object command occupancy lease differs");
        const auto original=encode_session_bundle(source);const auto original_digest=sha256(original.data(),original.size());
        require(!source.architecture_object_state||!source.architecture_object_state->position_registry||
            source.architecture_object_state->position_registry->complete,
            "object command source actual position registry is incomplete");
        resources_.preload(source.world);const auto groups=hooks_.physical_groups();
        std::int16_t selected=command.target.id;std::uint32_t guid=command.guid;CodeOwner owner=guid;
        if(command.kind!=ObjectTradeSessionCommandKind::purchase){const auto it=std::find_if(source.world.entities.begin(),source.world.entities.end(),[&](const auto& e){return e.id==selected;});
            require(it!=source.world.entities.end()&&it->generation==command.target.generation&&it->definition,"object command target is stale");guid=it->definition->guid;
            const auto thread=std::find_if(source.object_threads.begin(),source.object_threads.end(),[&](const auto& t){return t.id==selected;});
            require(thread!=source.object_threads.end(),"object command target lacks actual Thread");owner=thread->primary_owner;
        }else{
            std::set<std::int16_t> unavailable;for(const auto& e:source.world.entities)unavailable.insert(e.id);
            require(source.object_history.has_value(),"object command allocation history unknown");
            unavailable.insert(source.object_history->reserved_entity_ids.begin(),source.object_history->reserved_entity_ids.end());
            std::int32_t id=1;while(id<=32767&&unavailable.count(static_cast<std::int16_t>(id)))++id;
            require(id<=32767,"object command ID space exhausted");selected=static_cast<std::int16_t>(id);owner=guid;
        }
        auto resource=command.kind==ObjectTradeSessionCommandKind::purchase?resources_.load_group(guid):
            resolve_object_trade_source_group(resources_,source,hooks_,groups,command.target);
        const auto* selected_record=resources_.find(guid);require(selected_record,"object command resource unavailable");
        std::vector<std::int16_t> purchase_ids;
        if(command.kind==ObjectTradeSessionCommandKind::purchase){
            require(resource.group&&!resource.group->parts.empty()&&resource.group->parts.size()<=64,"purchase group allocation extent unavailable");
            purchase_ids=allocate_object_trade_group_ids(source.world,*source.object_history,resource.group->parts.size());
            require(purchase_ids.front()==selected,"purchase batch allocator changed its first identity");
        }
        std::vector<ThreadSeed> seeds;
        const auto add=[&](std::int16_t id,const VmSnapshot& vm){const auto e=std::find_if(source.world.entities.begin(),source.world.entities.end(),[&](const auto& x){return x.id==id;});
            require(e!=source.world.entities.end(),"captured Thread is not in World");seeds.push_back({id,e->generation,vm,hooks_.thread_programs(id,source)});};
        for(const auto& r:source.runtime_entities)add(r.id,r.vm);for(const auto& r:source.npc_avatars)add(r.runtime.id,r.runtime.vm);
        for(const auto& r:source.object_threads)add(r.id,r.vm);
        auto alive=std::make_shared<bool>(true);struct End{std::shared_ptr<bool> alive;~End(){*alive=false;}} end{alive};
        const auto authority=[weak=std::weak_ptr<bool>(alive)]{const auto lease=weak.lock();return lease&&*lease;};
        auto candidate_support=source.architecture_object_support;
        auto candidate_objects=source.architecture_object_state;
        auto* support=candidate_objects?&candidate_objects->object_support:candidate_support?&*candidate_support:nullptr;
        auto* registry=candidate_objects&&candidate_objects->position_registry?&*candidate_objects->position_registry:nullptr;
        const auto factory=candidate_factory(resources_,source,prepared_,architecture_,rooms_,hooks_,groups,seeds,selected,owner,guid,authority,support,registry,candidate_objects?&*candidate_objects:nullptr);
        ObjectTradeTransactionResult prepared;
        if(command.kind==ObjectTradeSessionCommandKind::purchase)
            prepared=prepare_ts1_object_purchase(source,{selected,selected_record->catalog,command.position,purchase_ids},resource,factory);
        else{
            World permission_world(odh::installed_lot_world_limits());require(permission_world.restore_snapshot(source.world),"permission snapshot invalid");
            std::vector<std::unique_ptr<Vm>> avatars;ObjectSaleRequest request;request.target=command.target;request.groups=groups;
            request.move_objects=command.move_objects;request.avatar_threads_complete=true;
            request.buildable=ObjectTradeBuildableArea{prepared_.width,prepared_.height,5,0,0,static_cast<std::int32_t>(prepared_.width),static_cast<std::int32_t>(prepared_.height),std::vector<bool>(prepared_.buildable.begin(),prepared_.buildable.end())};
            for(const auto& seed:seeds){const auto* e=permission_world.find(seed.id);if(e->kind!=EntityKind::avatar)continue;
                auto vm=std::make_unique<Vm>(seed.programs.globals,ShareVmProgramTag{},VmMemory{},VmLimits{},VmPrimitive{},seed.programs.owners,ShareVmOwnerProgramsTag{});
                if(!vm->restore_snapshot(seed.state,seed.programs.saved_routine_policy))throw FormatError(register_owner_restore_failure(seed));
                request.avatar_threads.push_back({{seed.id,seed.generation},vm.get()});avatars.push_back(std::move(vm));
            }
            // Fresh TS1 imports/constructors own runtime Disabled=0 provenance.
            // It is independent of OD43 and OBJD.Disabled raw16. Retain the
            // existing fresh-created proof for legacy transaction checkpoints.
            if(std::any_of(source.created_objects.begin(),source.created_objects.end(),[&](const auto& o){return o.id==selected&&o.generation==command.target.generation;}))request.selected_disabled=0;
            if(source.object_trade&&source.object_trade->version==2)
                for(const auto& group:source.object_trade->groups)for(const auto& member:group.members)
                    if(member.object.id==selected&&member.object.generation==command.target.generation&&member.disabled)
                        request.selected_disabled=member.disabled;
            if(command.kind==ObjectTradeSessionCommandKind::sell)prepared=prepare_ts1_object_sale(source,request,resource,factory);
            else prepared=prepare_ts1_object_move(source,{request,command.position},resource,factory);
        }
        result.transaction=prepared.status;
        result.diagnostic=prepared.diagnostic;
        if(!prepared.candidate){result.state=prepared.status==ObjectTradeTransactionStatus::rejected_preflight?ObjectTradeSessionState::rejected:ObjectTradeSessionState::unsupported;
            result.diagnostic=prepared.diagnostic;return result;}
        // The kernel's capture_architecture_state already transferred the
        // final actual support/registry and visual classifications before its
        // FIRST archive validation. Do not overwrite that owner with the older
        // adapter staging copy: it lacks reconciled visual identities and can
        // resurrect retired constructor metadata.
        // The transaction owns its live value ledger. Architecture capture
        // must not restore source.object_trade over Init/EP11/retirement writes.
        if(prepared.candidate->object_trade)
            validate_object_value_groups(*prepared.candidate->object_trade,prepared.candidate->world);
        require_coherent_candidate_registry(source,prepared.candidate->world,prepared.candidate->architecture_object_state,rooms_);
        require(encode_session_bundle(*prepared.candidate)==prepared.archive,
            "object transaction changed after its final architecture/value capture");
        hooks_.validate_reopen(*prepared.candidate,prepared.archive);
        const auto current=encode_session_bundle(hooks_.capture_current());
        require(sha256(current.data(),current.size())==original_digest,"object command live session changed during preparation");
        journal_.verify_current();result.archive=prepared.archive;
        result.checkpoint=journal_.publish(std::move(*prepared.candidate));published=true;
        result.state=ObjectTradeSessionState::published_reload_required;result.durability_uncertain=journal_.durability_uncertain();
        const auto reread=encode_session_archive(read_session_archive(result.checkpoint));
        require(reread==result.archive,"published object checkpoint bytes differ");journal_.validate_restore(reread);
        return result;
    }catch(const std::exception& error){result.diagnostic=error.what();
        result.state=published?ObjectTradeSessionState::published_reload_required:ObjectTradeSessionState::unsupported;return result;}
}
} // namespace odh_host
