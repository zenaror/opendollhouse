// SPDX-License-Identifier: MPL-2.0
// Pinned FreeSO7e76: VMNetBuyObjectCmd.TryPlace/Verify/Execute,
// VMNetDeleteObjectCmd.Execute, VMGameObject.PrePositionChange,
// VMEntity.Delete, VMMultitileGroup.Price, VMTS1GlobalLinkStub.
#include "odh/object_trade_transaction.hpp"
#include "odh/seated_session_continuation.hpp"
#include "odh/object_value_registry.hpp"
#include "odh/dynamic_object_groups.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>

namespace odh {
namespace {
struct Unsupported : std::runtime_error {using std::runtime_error::runtime_error;};
void need(bool value,const char* why){if(!value)throw Unsupported(why);}
bool same_origin(const SessionCreatedObject& a,const SessionCreatedObject& b){
    return a.guid==b.guid&&a.source==b.source&&a.asset==b.asset&&a.definition_chunk_id==b.definition_chunk_id;
}
void resource_profile(const ObjectTradeResource& resource){
    const auto& r=resource.runtime;
    need(resource.origin.guid&&resource.origin.guid==r.definition.guid&&!resource.origin.source.empty()&&
         resource.origin.definition_chunk_id,"trade needs exact resource provenance");
    need(r.construction.status==ObjectConstructionStatus::ready&&
         (!r.definition.master_id||r.definition.sub_index!=-1)&&r.definition.build_mode_type&&*r.definition.build_mode_type==0,
         "trade needs resolved ordinary physical definitions, not abstract masters");
    need(r.definition.level_offset&&*r.definition.level_offset==0,
         "trade EP11 architectural floor wrapper is not integrated");
    need(r.entry_points.entries.size()>12,"trade source entry-point table is incomplete");
    // Delete publishes real World.Dead before both source PrePositionChange
    // passes. Exact wall-use services suppress EP6 under that same state;
    // ordinary live placement retains its authored EP6 checks.
    need(r.semiglobal_name.has_value()==resource.linked_semiglobal.has_value()&&
         (!r.semiglobal_name||*r.semiglobal_name==resource.linked_semiglobal->name)&&
         (resource.linked_semiglobal||resource.linked_routines.empty()),"trade exact GLOB provenance unavailable");
    for(const auto& item:resource.global_routines)need(item.first.scope==RoutineScope::global,"trade global bank has non-global routine");
    for(const auto& item:resource.linked_routines)need(item.first.scope==RoutineScope::semiglobal,"trade linked bank has non-semiglobal routine");
    for(const auto& item:r.private_routines)need(item.first.scope==RoutineScope::private_object,"trade private bank has wrong scope");
}
void leaf_topology(const World& world,ObjectTradeIdentity target,bool allow_container=false){
    const auto* e=world.find(target.id);
    need(e&&e->generation==target.generation&&e->kind==EntityKind::object&&!e->ui_ghost&&(allow_container||!e->attachment),
         "trade target identity/topology changed");
    need(std::none_of(e->slots.begin(),e->slots.end(),[](auto id){return id!=0;}),
         "trade contained-object deletion is not integrated");
    if(e->attachment){
        const auto parent=world.attachment_parent({target.id,target.generation});
        const auto* container=parent?world.resolve_reference(*parent):nullptr;
        need(container&&container->kind==EntityKind::object&&!container->ui_ghost&&
             e->attachment->slot<container->slots.size()&&container->slots[e->attachment->slot]==target.id,
             "trade source container identity/reciprocity unavailable");
    }
}
void empty_topology(const World& world,ObjectTradeIdentity target){leaf_topology(world,target);}
void no_execution_reference(const SessionBundle& bundle,std::int16_t id,const std::set<std::int16_t>& retiring={}){
    const auto context=[&](const VmContext& c){return c.caller==id||c.callee==id||c.stack_object==id;};
    const auto vm=[&](const VmSnapshot& v){
        for(const auto& frame:v.stack)need(!context(frame.context),"trade target is referenced by a live VM frame");
        if(v.pending)need(!context(v.pending->context),"trade target is pending in a VM");
        for(const auto& p:v.suspended_pending)need(!context(p.context),"trade target is suspended in a VM");
    };
    const auto avatar=[&](const SessionRuntimeEntity& r){
        vm(r.vm);
        for(const auto& action:r.controllers.interactions.actions)
            need(!context(action.call.context),"trade target is referenced by an interaction queue");
        // The existing session validator checks every remaining route, seating,
        // resource owner and headline link again before candidate publication.
    };
    for(const auto& r:bundle.runtime_entities)avatar(r);
    for(const auto& r:bundle.npc_avatars)avatar(r.runtime);
    for(const auto& r:bundle.object_threads)if(r.id!=id&&!retiring.count(r.id))vm(r.vm);
    for(const auto& r:bundle.deferred_removals)need(r.id!=id,"trade target already has deferred removal");
}
class Candidate {
public:
    SessionBundle bundle;
    World world;
    RelationshipState relations;
    RandomState random;
    std::optional<RuntimeGlobals> globals;
    std::optional<Ts1Clock> clock;
    std::unique_ptr<ObjectValueRegistry> values;
    bool actorless=false,source_had_groups=false;
    Candidate(const SessionBundle& source,const SessionBundleLimits& limits,bool no_family=false)
        :bundle(source),world(limits.world.world,source.world.attribute_policy),
         relations(source.relationships.limits),random(source.host.random_seed){
        actorless=no_family;source_had_groups=source.object_groups.has_value();
        if(actorless)validate_actorless_object_trade_state(actorless_object_trade_from_components(source),limits);
        else (void)encode_session_bundle(source,limits);
        need(source.generic_execution&&source.object_history&&source.object_history->complete&&
             (actorless?!source.host.family_budget.has_value():source.host.family_budget.has_value()),
             "trade needs complete generic history and correct CurrentFamily authority");
        need(world.restore_snapshot(source.world)&&relations.restore_snapshot(source.relationships),"trade candidate restore rejected");
        need(world.install_reserved_entity_ids(source.object_history->reserved_entity_ids),"trade reservations cannot be reconstructed");
        if(source.runtime_globals){
            globals.emplace(RuntimeGlobals::from_legacy(LotContext::Snapshot{}));
            need(globals->restore(*source.runtime_globals),"trade runtime global snapshot invalid");
        }
        if(source.simulation){clock.emplace();need(clock->restore(source.simulation->clock),"trade simulation clock invalid");}
        if(!bundle.object_trade)bundle.object_trade=SessionObjectTradeLedger{};
        values=std::make_unique<ObjectValueRegistry>(world,*bundle.object_trade);
        need(values->migrate_legacy_singletons()&&values->bind(),"trade candidate value registry could not bind");
    }
    void finish(ObjectTradeTransactionResult& result,const SessionBundleLimits& limits,
                const ObjectTradeRuntimeBinding& binding,bool refused){
        need(binding.command_still_matches&&binding.command_still_matches(),"trade command/source version changed");
        bundle.world=world.snapshot();bundle.relationships=relations.snapshot();bundle.host.random_seed=random.seed();
        if(globals)bundle.runtime_globals=globals->snapshot();
        if(binding.capture_architecture_state){
            auto captured=binding.capture_architecture_state(bundle.world);
            need(!bundle.architecture_object_state||captured.object_state.has_value(),
                "trade final event capture dropped known architecture owner");
            need(!bundle.architecture_object_support||captured.object_support||captured.object_state,
                "trade final event capture dropped known support owner");
            need(!captured.object_support||!captured.object_state,"trade final event capture has competing owners");
            bundle.architecture_object_support=std::move(captured.object_support);
            bundle.architecture_object_state=std::move(captured.object_state);
            need(binding.command_still_matches&&binding.command_still_matches(),
                "trade source changed during final event capture");
        }
        // Binding a dynamic-capable command needs an owned empty workspace,
        // but a static command must not manufacture a persisted OGRP section.
        // All source/candidate callbacks have completed at this publication
        // boundary; the optional owner is no longer consulted afterwards.
        if(!source_had_groups&&bundle.object_groups&&bundle.object_groups->groups.empty()&&
           bundle.object_groups->retired_native_members.empty())bundle.object_groups.reset();
        std::vector<std::uint8_t> bytes;
        if(actorless){
            const auto state=actorless_object_trade_from_components(bundle);
            bytes=encode_actorless_object_trade_state(state,limits);
            (void)decode_actorless_object_trade_state(bytes,&state.resources,limits);
        }else{
            bytes=encode_session_bundle(bundle,limits);
            (void)decode_session_bundle(bytes,bundle.resources,limits);
        }
        // The registry borrows this exact ledger. Unbind before moving its
        // owner into the result; no candidate callbacks run after publication.
        values.reset();
        result.archive=std::move(bytes);result.candidate=std::move(bundle);
        result.status=refused?ObjectTradeTransactionStatus::prepared_authored_refusal:ObjectTradeTransactionStatus::prepared_success;
    }
};
RoutineOwners owners(const ObjectTradeResource& r,CodeOwner owner){
    auto bank=r.runtime.private_routines;bank.insert(r.linked_routines.begin(),r.linked_routines.end());
    return {{owner,std::move(bank)}};
}
std::shared_ptr<DynamicObjectGroups> bind_dynamic(Candidate& c,const ObjectTradeRuntimeBinding& binding){
    if(!binding.on_group_state_bound)return {};
    if(!c.bundle.object_groups)c.bundle.object_groups=ObjectGroupSnapshot{};
    auto dynamic=binding.on_group_state_bound(*c.bundle.object_groups,*c.values);
    need(static_cast<bool>(dynamic),"trade candidate dynamic group binding failed");return dynamic;
}
void authority(const ObjectTradeRuntimeBinding& b){need(b.command_still_matches&&b.command_still_matches(),"trade command authority unavailable");}
void singleton(const ObjectTradeRuntimeBinding& b,ObjectTradeIdentity target){
    authority(b);need(static_cast<bool>(b.position_change.admit_single_ordinary),"trade ordinary lifecycle proof unavailable");
    const auto ordinary=b.position_change.admit_single_ordinary({target.id,target.generation});
    need(ordinary&&*ordinary,"trade ordinary lifecycle profile is unknown or unsupported");authority(b);
}
SessionResourceCodeOwner resource_owner(const ObjectTradeResource& r,CodeOwner owner){
    const auto banks=owners(r,owner);const auto& bank=banks.begin()->second;
    SessionResourceCodeOwner result;result.owner=owner;result.resource_guid=r.origin.guid;
    result.source=r.origin.source;result.asset=r.origin.asset;result.definition_chunk_id=r.origin.definition_chunk_id;
    result.tree_table_id=r.runtime.definition.tree_table_id;
    // Complete-bank manifests use an anchor key, not a claim of a callable Main.
    // The empty-bank anchor follows the generic host's private-bank convention.
    result.root_routine=bank.empty()?4096:bank.begin()->first.id;
    result.bank_kind=r.linked_semiglobal?SessionCodeBankKind::private_and_semiglobal:SessionCodeBankKind::complete_private;
    result.identity=owner==r.origin.guid?SessionCodeOwnerIdentity::resource_guid:SessionCodeOwnerIdentity::process;
    result.linked_semiglobal=r.linked_semiglobal;result.routine_fingerprint=session_loaded_bank_fingerprint(bank);
    return result;
}
std::array<std::uint8_t,32> global_program_fingerprint(const RoutineSet& routines){
    // Owner-bank fingerprints deliberately reject globals. Use a separate
    // domain and exact numeric global keyspace, without reclassifying keys or
    // relaxing the owner-bank authenticator shared with session restoration.
    Sha256 hash;static constexpr char domain[]="ODH-TRADE-GLOBAL-PROGRAM-V1";
    hash.update(domain,sizeof(domain));
    const auto integer=[&](std::uint64_t value,unsigned width){
        std::array<std::uint8_t,8> bytes{};
        for(unsigned n=0;n<width;++n)bytes[n]=static_cast<std::uint8_t>(value>>(n*8));
        hash.update(bytes.data(),width);
    };
    integer(routines.size(),8);
    for(const auto& item:routines){
        const auto& key=item.first;const auto& code=item.second;
        need(key.scope==RoutineScope::global&&routine_key_valid(key),"trade global program has invalid global keys");
        integer(key.id,2);integer(code.version,2);hash.update(code.header.data(),code.header.size());
        integer(code.metadata?1:0,1);
        if(code.metadata){const auto& m=*code.metadata;
            integer(m.type,1);integer(m.arguments,1);integer(m.locals,2);integer(m.tree_version,2);
        }
        integer(code.instructions.size(),8);
        for(const auto& instruction:code.instructions){
            integer(instruction.opcode,2);integer(instruction.on_true,1);integer(instruction.on_false,1);
            hash.update(instruction.operands.data(),instruction.operands.size());
        }
    }
    return hash.finish();
}
ObjectTradeSelectedPrograms selected_programs(const ObjectTradeResource& resource,
        const SessionObjectThread& source,const ObjectTradeRuntimeBinding& binding){
    ObjectTradeSelectedPrograms programs;
    if(binding.selected_programs)programs=*binding.selected_programs;
    else{
        programs.globals=std::make_shared<const VmProgram>(resource.global_routines);
        for(auto& bank:owners(resource,source.primary_owner))
            programs.owners.emplace(bank.first,std::make_shared<const VmProgram>(std::move(bank.second)));
    }
    need(programs.globals&&global_program_fingerprint(programs.globals->routines())==
        global_program_fingerprint(resource.global_routines),"trade selected global program differs from exact resource");
    need(programs.owners.size()==source.resource_owners.size(),"trade selected program collection does not cover captured owners");
    for(const auto& manifest:source.resource_owners){
        const auto bank=programs.owners.find(manifest.owner);
        need(manifest.owner&&bank!=programs.owners.end()&&bank->second&&
            session_loaded_bank_fingerprint(bank->second->routines())==manifest.routine_fingerprint,
            "trade selected immutable owner differs from captured provenance");
    }
    need(programs.owners.count(source.primary_owner)!=0,"trade selected primary program is unavailable");
    return programs;
}
void entry(Vm& thread,const ObjectTradeResource& resource,CodeOwner owner,std::int16_t id,std::uint16_t ep,
           std::uint64_t& budget,const ObjectTradeRuntimeBinding& b,const VmPrimitive& primitive,
           const SharedRoutineOwners* installed=nullptr){
    authority(b);const auto action=resource.runtime.entry_points.entries.at(ep).action;
    if(action<=255||!thread.has_routine(routine_key(action),owner))return;
    // Net commands run BEFORE InternalTick. EvaluateCheck clones the actual
    // entity thread's Temps/XL for EACH entry; check writes never become that
    // thread's registers, while World, budget and RNG effects remain shared.
    // Entry checks remain strict, but retain the authenticated installed bank
    // collection of the actual selected Thread. Its restore policy never leaks
    // into synchronous checks or freshly purchased Threads.
    auto globals=thread.shared_program_for_owner(0);SharedRoutineOwners banks;
    if(installed)banks=*installed;
    else if(b.selected_programs)banks=b.selected_programs->owners;
    else for(auto& bank:owners(resource,owner))banks.emplace(bank.first,std::make_shared<const VmProgram>(std::move(bank.second)));
    Vm check(std::move(globals),ShareVmProgramTag{},b.memory,{},primitive,std::move(banks),ShareVmOwnerProgramsTag{});
    need(check.start(routine_key(action),{id,id,0,owner},{0,0,0,0},true)==VmStatus::running,
         "trade exact entry frame could not start");
    const auto run=check.run_synchronous_check_with_registers(thread,false,budget,primitive);
    if(!run.started||(run.status!=VmStatus::returned_true&&run.status!=VmStatus::returned_false)){
        std::string reason="trade synchronous entry did not complete EP="+std::to_string(ep)+" action="+std::to_string(action)+
            " entity="+std::to_string(id)+" owner="+std::to_string(owner)+" status="+vm_status_name(run.status)+
            " started="+std::to_string(run.started)+" steps="+std::to_string(run.steps)+" diagnostic="+run.diagnostic;
        for(const auto& frame:check.stack())reason+=" frame="+std::to_string(frame.key.id)+"@"+std::to_string(frame.pc)+
            "/owner="+std::to_string(frame.context.code_owner)+"/caller="+std::to_string(frame.context.caller)+"/SO="+std::to_string(frame.context.stack_object);
        throw Unsupported(reason);
    }
    need(run.steps<=budget,"trade entry exceeded instruction budget");budget-=run.steps;authority(b);
}
void publish_object_thread(Candidate& c,ObjectTradeIdentity target,Vm& vm,const ObjectTradeResource& r){
    need(c.bundle.scheduler.entities.size()<c.bundle.scheduler.entity_limit,"trade scheduler capacity unavailable");
    SessionObjectThread state;state.id=target.id;state.generation=target.generation;state.guid=r.origin.guid;
    state.primary_owner=r.origin.guid;state.vm=vm.snapshot();state.resource_owners.push_back(resource_owner(r,state.primary_owner));
    Sleep sleep(c.world);need(sleep.bind(vm),"trade object sleep binding failed");
    SleepSnapshot clock;clock.actor=SleepActorSnapshot{target.id,target.generation,vm.run_generation()};
    clock.clock=c.bundle.host.tick;clock.last_tick=c.bundle.host.tick;
    need(sleep.restore_snapshot(clock),"trade object thread clock invalid");state.sleep=sleep.snapshot();
    c.bundle.object_threads.push_back(std::move(state));
    std::sort(c.bundle.object_threads.begin(),c.bundle.object_threads.end(),[](const auto& a,const auto& z){return a.id<z.id;});
    // Same hook profile as restore_runtime_object: before, no after, interrupt.
    c.bundle.scheduler.entities.push_back({target.id,vm.run_generation(),vm.steps(),vm.status(),true,false,true});
    std::sort(c.bundle.scheduler.entities.begin(),c.bundle.scheduler.entities.end(),[](const auto& a,const auto& z){return a.id<z.id;});
}
void remove_thread(Candidate& c,std::int16_t id){
    auto& scheduled=c.bundle.scheduler.entities;
    const auto found=std::find_if(scheduled.begin(),scheduled.end(),[&](const auto& e){return e.id==id;});
    if(found!=scheduled.end()){
        // EntityVmScheduler::remove preserves its next-successor budget cursor.
        if(c.bundle.scheduler.budget_cursor==id){
            auto next=std::next(found);if(next==scheduled.end())next=scheduled.begin();
            if(next==found)c.bundle.scheduler.budget_cursor.reset();else c.bundle.scheduler.budget_cursor=next->id;
        }
        scheduled.erase(found);
    }
    auto& threads=c.bundle.object_threads;
    threads.erase(std::remove_if(threads.begin(),threads.end(),[&](const auto& t){return t.id==id;}),threads.end());
}
void retire(Candidate& c,ObjectTradeIdentity target,const ObjectTradeRuntimeBinding& b,bool physical_member=false){
    const auto admit=[&]{
        if(!physical_member){singleton(b,target);return;}
        authority(b);need(static_cast<bool>(b.position_change.admit_group_member),"trade group member deletion proof unavailable");
        const auto yes=b.position_change.admit_group_member({target.id,target.generation});
        need(yes&&*yes,"trade exact physical deletion member is unavailable");authority(b);
    };
    leaf_topology(c.world,target,true);admit();
    need(c.world.mark_dead({target.id,target.generation}),"trade source Delete could not publish Dead before cleanup");
    const auto definition=c.world.find(target.id)->definition;
    ObjectPositionValid placement(c.world,b.placement);
    auto lifecycle=b.position_change;
    if(physical_member){const auto ordinary=lifecycle.admit_single_ordinary,member=lifecycle.admit_group_member;
        lifecycle.admit_single_ordinary=[ordinary,member,target](ObjectPositionIdentity id)->std::optional<bool>{
            return id.id==target.id&&id.generation==target.generation?member(id):ordinary?ordinary(id):std::nullopt;
        };
    }
    ObjectPositionChange change(c.world,placement,std::move(lifecycle));
    // VMEntity.Delete invokes PrePositionChange, then RemoveObjectInstance
    // invokes it again. The first pass detaches a supported item and returns;
    // the second handles its now-unattached wall/support registration.
    for(unsigned pass=0;pass<2;++pass){
        struct Lease {bool active=true,called=false,accepted=false,repeated=false;std::function<bool()> kernel;};
        auto lease=std::make_shared<Lease>();
        lease->kernel=[&]{return change.pre_position_change({target.id,target.generation},true,b.command_still_matches);};
        struct End {std::shared_ptr<Lease> lease;~End(){lease->active=false;lease->kernel={};}} end{lease};
        const std::function<bool()> operation=[lease]{
            if(!lease->active)return false;
            if(lease->called){lease->repeated=true;return false;}
            lease->called=true;lease->accepted=lease->kernel();return lease->accepted;
        };
        const bool completed=b.position_change.lifecycle_phase?
            b.position_change.lifecycle_phase({target.id,target.generation},ObjectPositionChangePhase::pre,operation):operation();
        lease->active=false;lease->kernel={};
        need(completed&&lease->called&&lease->accepted&&!lease->repeated,"trade deletion PrePositionChange incomplete");
        authority(b);empty_topology(c.world,target);admit();
    }
    const auto origin=std::find_if(c.bundle.created_objects.begin(),c.bundle.created_objects.end(),[&](const auto& o){return o.id==target.id;});
    if(origin!=c.bundle.created_objects.end()){
        c.bundle.object_history->retired_created_objects.push_back(*origin);c.bundle.created_objects.erase(origin);
        // Reusable-ID histories retain every constructor generation. Numeric
        // ID alone is not an ordering key: an unstable sort can move the new
        // generation before an earlier retirement of that same ID.
        std::sort(c.bundle.object_history->retired_created_objects.begin(),c.bundle.object_history->retired_created_objects.end(),
            [](const auto& a,const auto& z){return std::tie(a.id,a.generation)<std::tie(z.id,z.generation);});
    }else{
        need(definition.has_value(),"trade original definition missing");
        c.bundle.removed_original_objects.push_back({target.id,target.generation,definition->guid});
        std::sort(c.bundle.removed_original_objects.begin(),c.bundle.removed_original_objects.end(),[](const auto& a,const auto& z){return a.id<z.id;});
    }
    need(c.relations.retire_object_identity(target.id,target.generation)&&c.world.retire(target.id),"trade native retirement rejected");
    c.bundle.object_history->reserved_entity_ids=c.world.reserved_entity_ids();
    remove_thread(c,target.id);
    need(c.values->retire_member(target),"trade retired object's value group could not retire");
}
bool same_identity(ObjectTradeIdentity a,ObjectTradeIdentity b){return a.id==b.id&&a.generation==b.generation;}
ObjectTradeGroupResources group_resources(const ObjectTradeResource& selected,bool purchasing){
    ObjectTradeGroupResources plan;
    if(selected.group)plan=*selected.group;
    else{plan.requested=selected.origin;auto leaf=selected;leaf.group.reset();
        plan.parts.push_back({std::move(leaf),0,0,0,selected.lead_object});}
    need(!plan.parts.empty()&&plan.parts.size()<=64&&plan.base_index<plan.parts.size()&&
        (plan.live_association||plan.multitile||plan.parts.size()==1),"trade complete ordered group plan is invalid or over budget");
    need(!purchasing||(!plan.live_association&&!plan.imported&&plan.requested&&plan.requested->guid==selected.origin.guid),
        "trade purchase requires exact requested construction definition");
    need(!plan.imported||!plan.requested,"trade imported group cannot invent a common construction master");
    const auto globals=global_program_fingerprint(plan.parts.front().resource.global_routines);
    for(const auto& part:plan.parts){
        need(!part.resource.group,"trade group part recursively contains another group plan");
        resource_profile(part.resource);
        need(global_program_fingerprint(part.resource.global_routines)==globals,"trade group global resources differ between parts");
        if(purchasing&&plan.multitile)
            need(part.resource.runtime.master_guid==std::optional<std::uint32_t>{plan.requested->guid}&&
                part.resource.origin.master_guid==part.resource.runtime.master_guid,
                "trade construction part does not name its exact requested master");
    }
    return plan;
}
ObjectGroup physical_group(const ObjectTradeGroupResources& plan,const std::vector<ObjectTradeIdentity>& identities){
    need(identities.size()==plan.parts.size(),"trade physical identity count differs from its complete plan");
    ObjectGroup result{plan.requested?plan.requested->guid:0,plan.multitile,identities.at(plan.base_index).id,{}};
    if(plan.live_association)result.kind=ObjectGroupKind::live_association;
    for(std::size_t n=0;n<identities.size();++n){const auto& id=identities[n];const auto& part=plan.parts[n];
        result.members.push_back({id.id,id.generation,part.offset_x,part.offset_y,part.offset_level});}
    return result;
}
std::vector<ObjectTradeIdentity> authenticate_existing_group(const SessionBundle& source,const World& world,
        const SessionObjectValueGroup& values,const ObjectTradeGroupResources& plan,ObjectTradeIdentity clicked){
    need(values.members.size()==plan.parts.size()&&values.multitile==plan.multitile&&values.base,
        "trade complete group plan missing; singleton plan cannot cover authenticated value group");
    std::vector<ObjectTradeIdentity> ids;bool found_clicked=false;
    for(std::size_t n=0;n<plan.parts.size();++n){const auto& part=plan.parts[n];const auto& member=values.members[n];
        const ObjectTradeIdentity id{member.object.id,member.object.generation};const auto* entity=world.find(id.id);
        need(entity&&entity->generation==id.generation&&entity->kind==EntityKind::object&&!entity->dead&&!entity->ui_ghost&&
            entity->definition&&entity->definition->guid==part.resource.origin.guid&&
            entity->definition->master_guid==member.object.master_guid&&part.resource.origin.master_guid==member.object.master_guid&&
            part.resource.runtime.master_guid==member.object.master_guid&&same_origin(member.object,part.resource.origin)&&
            member.offset_x==part.offset_x&&member.offset_y==part.offset_y&&member.offset_level==part.offset_level&&
            (!part.lead_object||member.lead_object==part.lead_object),"trade physical part identity/resource/offset differs from its authenticated value member");
        if(plan.parts.size()>1||plan.imported)need(part.resource.origin.id==id.id&&part.resource.origin.generation==id.generation,
            "trade existing physical part lacks its exact source identity");
        found_clicked|=same_identity(clicked,id);ids.push_back(id);
    }
    need(found_clicked&&same_identity(*values.base,ids[plan.base_index]),"trade clicked target or physical base differs from complete group");
    // Mutable price_origin is deliberately not a physical/master identifier.
    const auto constructed=source.object_groups?std::find_if(source.object_groups->groups.begin(),source.object_groups->groups.end(),
        [&](const auto& group){return group.base_id==values.base->id;}):std::vector<ObjectGroup>::const_iterator{};
    if(!plan.imported&&!plan.live_association&&plan.parts.size()>1){
        need(plan.requested&&source.object_groups&&constructed!=source.object_groups->groups.end(),"trade constructed group lacks OGRP authority");
        const auto expected=physical_group(plan,ids);
        need(constructed->definition_guid==expected.definition_guid&&constructed->multitile==expected.multitile&&
            constructed->members.size()==expected.members.size(),"trade constructed physical manifest differs from OGRP");
        for(std::size_t n=0;n<expected.members.size();++n){const auto& a=constructed->members[n];const auto& b=expected.members[n];
            need(a.id==b.id&&a.generation==b.generation&&a.offset_x==b.offset_x&&a.offset_y==b.offset_y&&a.offset_level==b.offset_level,
                "trade constructed physical member differs from OGRP");}
    }
    if(plan.live_association){
        need(source.object_groups&&constructed!=source.object_groups->groups.end()&&constructed->kind==ObjectGroupKind::live_association&&
            constructed->multitile==plan.multitile&&constructed->members.size()==ids.size(),"trade live association lacks exact OGRP authority");
        for(std::size_t n=0;n<ids.size();++n){const auto&m=constructed->members[n];const auto&p=plan.parts[n];
            need(m.id==ids[n].id&&m.generation==ids[n].generation&&m.offset_x==p.offset_x&&m.offset_y==p.offset_y&&m.offset_level==p.offset_level&&m.origin&&
                m.origin->guid==p.resource.origin.guid&&m.origin->master_guid==p.resource.origin.master_guid&&m.origin->source==p.resource.origin.source&&
                m.origin->asset==p.resource.origin.asset&&m.origin->definition_chunk_id==p.resource.origin.definition_chunk_id,"trade live association member provenance differs");}
    }
    return ids;
}
struct PartThread {
    ObjectTradeIdentity identity;CodeOwner owner=0;ObjectTradeSelectedPrograms programs;
    std::unique_ptr<Vm> vm;
};
std::vector<PartThread> bind_part_threads(Candidate& candidate,const SessionBundle& source,const ObjectTradeGroupResources& plan,
        const std::vector<ObjectTradeIdentity>& ids,ObjectTradeRuntimeBinding& binding,const VmPrimitive& primitive,bool fresh){
    std::vector<PartThread> result;result.reserve(ids.size());
    if(binding.on_group_materialized)binding.on_group_materialized(physical_group(plan,ids));
    need(ids.size()==1||static_cast<bool>(binding.on_member_thread_bound),"trade complete group VM binding capability unavailable");
    for(std::size_t n=0;n<ids.size();++n){const auto& resource=plan.parts[n].resource;PartThread part;part.identity=ids[n];
        auto policy=VmSavedRoutinePolicy::require_present;const SessionObjectThread* saved=nullptr;
        if(fresh){part.owner=resource.origin.guid;part.programs.globals=std::make_shared<const VmProgram>(resource.global_routines);
            for(auto& bank:owners(resource,part.owner))part.programs.owners.emplace(bank.first,std::make_shared<const VmProgram>(std::move(bank.second)));
        }else{
            const auto thread=std::find_if(source.object_threads.begin(),source.object_threads.end(),[&](const auto& t){return t.id==ids[n].id&&t.generation==ids[n].generation;});
            need(thread!=source.object_threads.end(),"trade complete source group has a member without its exact Thread");saved=&*thread;part.owner=thread->primary_owner;
            const auto expected=resource_owner(resource,part.owner);
            const auto provenance=std::find_if(thread->resource_owners.begin(),thread->resource_owners.end(),[&](const auto& bank){return bank.owner==part.owner;});
            need(provenance!=thread->resource_owners.end()&&provenance->resource_guid==expected.resource_guid&&
                provenance->source==expected.source&&provenance->asset==expected.asset&&provenance->definition_chunk_id==expected.definition_chunk_id&&
                provenance->bank_kind==expected.bank_kind&&provenance->linked_semiglobal==expected.linked_semiglobal&&
                provenance->routine_fingerprint==expected.routine_fingerprint,"trade group member primary immutable resource provenance differs");
            auto local=binding;
            if(binding.member_programs){const auto admission=binding.member_programs(ids[n]);need(admission.has_value(),"trade source member programs/admission unavailable");
                local.selected_programs=admission->programs;policy=admission->saved_routine_policy;}
            else{need(ids.size()==1,"trade group member-specific saved programs unavailable");policy=binding.selected_saved_routine_policy;}
            part.programs=selected_programs(resource,*thread,local);
        }
        part.vm=std::make_unique<Vm>(part.programs.globals,ShareVmProgramTag{},binding.memory,VmLimits{},primitive,part.programs.owners,ShareVmOwnerProgramsTag{});
        if(saved)need(part.vm->restore_snapshot(saved->vm,policy),"trade source member Thread snapshot could not restore");
        result.push_back(std::move(part));
        auto& actual=result.back();
        if(binding.on_member_thread_bound)binding.on_member_thread_bound(actual.identity,actual.owner,*actual.vm);
        else if(binding.on_thread_bound)binding.on_thread_bound(*actual.vm);
    }
    return result;
}
void user_placement(const ObjectTradeRuntimeBinding& binding,const ObjectPositionIdentity target,
        std::vector<PartThread>& threads,const ObjectTradeGroupResources& plan,std::uint64_t& budget,const VmPrimitive& primitive){
    const auto actual=binding.position_change.group?binding.position_change.group(target):std::nullopt;
    if(binding.execute_bound_entry&&actual){for(const auto&m:actual->members)
        need(binding.execute_bound_entry({m.id,m.generation},11),"trade user placement entry incomplete on resulting association");
    }else{
        for(std::size_t n=0;n<threads.size();++n){auto&part=threads[n];
            if(actual&&std::none_of(actual->members.begin(),actual->members.end(),[&](const auto&m){return m.id==part.identity.id&&m.generation==part.identity.generation;}))continue;
            entry(*part.vm,plan.parts[n].resource,part.owner,part.identity.id,11,budget,binding,primitive,&part.programs.owners);}
        if(actual)for(const auto&m:actual->members)need(std::any_of(threads.begin(),threads.end(),[&](const auto&t){return t.identity.id==m.id&&t.identity.generation==m.generation;}),"trade resulting association member lacks user placement execution");
    }
}
void unchanged_group(const World& world,const ObjectTradeGroupResources& plan,const std::vector<ObjectTradeIdentity>& ids){
    for(std::size_t n=0;n<ids.size();++n){const auto* e=world.find(ids[n].id);
        need(e&&e->generation==ids[n].generation&&!e->dead&&!e->ui_ghost&&e->definition&&
            e->definition->guid==plan.parts[n].resource.origin.guid,
            "trade group membership changed during authored lifecycle; transition is not represented");
    }
}
void erase_physical_group(Candidate& c,const std::vector<ObjectTradeIdentity>& ids){
    if(!c.bundle.object_groups)return;
    auto& groups=c.bundle.object_groups->groups;
    groups.erase(std::remove_if(groups.begin(),groups.end(),[&](const auto& group){return std::any_of(group.members.begin(),group.members.end(),
        [&](const auto& member){return std::any_of(ids.begin(),ids.end(),[&](auto id){return id.id==member.id&&id.generation==member.generation;});});}),groups.end());
}
void constructor_cohort_profile(const SessionBundle& source,const ObjectTradeGroupResources& plan,
        NativeObjectGroupConstructionProfile profile){
    need(profile==NativeObjectGroupConstructionProfile::unknown_legacy||profile==NativeObjectGroupConstructionProfile::original_native,
        "trade native constructor profile is unsupported");
    std::set<std::uint32_t> native_guids;
    bool any_native=false,any_ordinary=false;
    for(const auto& part:plan.parts){const auto fields=native_object_group_constructor_fields(part.resource.runtime.definition);
        any_native|=fields.has_value();any_ordinary|=!fields.has_value();if(fields)native_guids.insert(part.resource.origin.guid);}
    if(!any_native)return;
    if(profile==NativeObjectGroupConstructionProfile::original_native)
        need(!any_ordinary,"trade native constructor cannot mix ordinary and MT bodies in one native chain");
    // Dynamic adjacency only combines matching GUIDs. An unrelated ordinary
    // body has no MT fields to infer. A compatible peer requires the SAME
    // explicit profile, before constructor/Init effects in this candidate.
    for(const auto& entity:source.world.entities){
        if(entity.dead||entity.ui_ghost||entity.kind!=EntityKind::object||!entity.definition||!native_guids.count(entity.definition->guid))continue;
        const ObjectGroup* found=nullptr;
        if(source.object_groups)for(const auto& group:source.object_groups->groups)
            for(const auto& member:group.members)if(member.id==entity.id&&member.generation==entity.generation)found=&group;
        const bool known=found&&found->native_head.has_value();
        need(known==(profile==NativeObjectGroupConstructionProfile::original_native),
            "trade native constructor profile would mix known and unknown compatible source groups");
    }
}
ObjectTradeTransactionResult failure(const std::exception& e){ObjectTradeTransactionResult r;r.diagnostic=e.what();return r;}
} // namespace

ObjectTradeTransactionResult prepare_ts1_object_purchase_impl(const SessionBundle& source,const ObjectPurchaseRequest& request,
    const ObjectTradeResource& resource,const ObjectTradeBindingFactory& factory,SessionBundleLimits limits,std::uint64_t budget,bool actorless){
    try{
        need(!source.build_buy_permission||source.build_buy_permission->enabled,"Build/Buy is disabled by the current lot simulation");
        const auto plan=group_resources(resource,true);need(static_cast<bool>(factory),"trade binding factory unavailable");
        need(request.position.direction<=7&&request.position.x>=INT16_MIN&&request.position.x<=INT16_MAX&&
            request.position.y>=INT16_MIN&&request.position.y<=INT16_MAX&&request.position.level>=INT8_MIN&&request.position.level<=INT8_MAX,
            "trade purchase destination exceeds native source representation");
        const auto price=ts1_object_purchase_value(plan.requested->guid,request.catalog);
        if(price.decision.status!=ObjectTradeStatus::allowed){ObjectTradeTransactionResult r;r.decision=price.decision;return r;}
        Candidate c(source,limits,actorless);const auto balance=ts1_object_trade_balance({true,c.bundle.host.family_budget},*price.amount,false);
        if(balance.decision.status!=ObjectTradeStatus::allowed){ObjectTradeTransactionResult r;r.decision=balance.decision;
            r.status=balance.decision.status==ObjectTradeStatus::rejected?ObjectTradeTransactionStatus::rejected_preflight:ObjectTradeTransactionStatus::unsupported;return r;}
        auto ids=request.new_ids;if(ids.empty())ids.push_back(request.new_id);
        need(ids.size()==plan.parts.size()&&ids.front()==request.new_id,"trade new identities do not cover ordered group plan");
        std::set<std::int16_t> unique;
        for(const auto id:ids)need(id>0&&unique.insert(id).second&&c.world.entity_id_available(id),"trade group new identity unavailable or repeated");
        need(!c.bundle.scheduler.faulted&&c.bundle.scheduler.entities.size()<=c.bundle.scheduler.entity_limit&&
            ids.size()<=c.bundle.scheduler.entity_limit-c.bundle.scheduler.entities.size(),"trade group scheduler capacity unavailable");
        auto binding=factory(c.world,c.relations,c.bundle.host,c.random,c.globals?&*c.globals:nullptr,c.clock?&*c.clock:nullptr,resource);authority(binding);
        need(!binding.selected_programs,"trade fresh purchase cannot use selected source programs");
        need(static_cast<bool>(binding.primitive_factory),"trade synchronous primitive factory unavailable");
        constructor_cohort_profile(source,plan,binding.native_group_construction);
        const bool native_constructor=binding.native_group_construction==NativeObjectGroupConstructionProfile::original_native&&
            native_object_group_constructor_fields(plan.parts.front().resource.runtime.definition).has_value();
        need(!native_constructor||static_cast<bool>(binding.on_group_state_bound),"trade native constructor needs the actual mutable group owner");
        c.bundle.host.family_budget=balance.budget_after;
        std::vector<ObjectTradeIdentity> identities;identities.reserve(ids.size());
        SessionObjectValueGroup value_group;value_group.multitile=plan.multitile;
        value_group.initial_price=*price.amount;
        value_group.price_origin={SessionObjectPriceSource::purchase,plan.requested->guid,plan.requested->source,
            plan.requested->asset,plan.requested->definition_chunk_id,{},{}};
        const auto purchase_price_origin=value_group.price_origin;
        for(std::size_t n=0;n<ids.size();++n){const auto& part=plan.parts[n];
            need(materialize_runtime_object(c.world,ids[n],part.resource.runtime)==ObjectRuntimeCreateStatus::created,
                "trade group member materialization failed");
            const ObjectTradeIdentity identity{ids[n],c.world.find(ids[n])->generation};identities.push_back(identity);
            VmFrame heading;heading.context.caller=identity.id;
            const bool heading_published=native_constructor?
                c.world.publish_native_object_heading({identity.id,identity.generation},request.position.direction):
                c.world.write(heading,3,1,request.position.direction);
            need(heading_published,"trade source construction direction unavailable");
            auto origin=part.resource.origin;origin.id=identity.id;origin.generation=identity.generation;
            c.bundle.created_objects.push_back(origin);
            SessionObjectValueMember member;member.object=origin;member.offset_x=part.offset_x;member.offset_y=part.offset_y;
            member.offset_level=part.offset_level;member.lead_object=part.lead_object;
            if(!member.lead_object){const auto* created=c.world.find(identity.id);const auto lead=created->definition_words.find(63);
                if(lead!=created->definition_words.end())member.lead_object=static_cast<std::uint16_t>(lead->second);}
            member.wear_known=true;member.wear=std::uint16_t{0};member.disabled=std::uint8_t{0};
            value_group.members.push_back(std::move(member));
        }
        std::sort(c.bundle.created_objects.begin(),c.bundle.created_objects.end(),[](const auto& x,const auto& y){return x.id<y.id;});
        value_group.base=identities[plan.base_index];
        need(c.values->admit(std::move(value_group)),"trade complete purchase value group admission failed");
        const auto physical=physical_group(plan,identities);const auto base=identities[plan.base_index];
        if(resource.group||native_constructor){auto groups=c.bundle.object_groups.value_or(ObjectGroupSnapshot{});
            groups.groups.push_back(physical);c.bundle.object_groups=capture_object_group_snapshot(std::move(groups),c.world.snapshot(),limits.object_groups);}
        if(binding.on_instruction_budget)binding.on_instruction_budget(budget);
        const auto dynamic=bind_dynamic(c,binding);
        if(native_constructor){
            need(static_cast<bool>(dynamic),"trade native constructor group owner is unavailable");
            std::vector<NativeObjectGroupSourceMember> rows;rows.reserve(identities.size());
            for(std::size_t n=0;n<identities.size();++n){const auto fields=native_object_group_constructor_fields(plan.parts[n].resource.runtime.definition);
                need(fields.has_value(),"trade native constructor member fields are unavailable");
                rows.push_back({{identities[n].id,identities[n].generation},fields->authored,fields->current});}
            // 440c2a/440ca1: actual first-created body is parent, not BaseObject.
            // 4231a8 recomposes current triples before the first script Init0.
            const WorldEntityReference head{identities.front().id,identities.front().generation};
            need(dynamic->bind_native_created_group({head.id,head.generation},head,rows),"trade actual native constructor field admission failed");
            need(dynamic->recompute_current_offsets(head),"trade actual native constructor offset recomposition failed");
        }
        const auto primitive=binding.primitive_factory(binding.memory);
        auto threads=bind_part_threads(c,source,plan,identities,binding,primitive,true);
        for(std::size_t n=0;n<threads.size();++n){auto& part=threads[n];const auto& code=plan.parts[n].resource;
            if(c.world.lockout_count_clock())need(c.world.admit_lockout_count_state(part.identity.id,part.identity.generation,true,0),
                "trade new member source Thread history unavailable");
            entry(*part.vm,code,part.owner,part.identity.id,0,budget,binding,primitive,&part.programs.owners);
            unchanged_group(c.world,plan,identities);
            entry(*part.vm,code,part.owner,part.identity.id,8,budget,binding,primitive,&part.programs.owners);
            unchanged_group(c.world,plan,identities);
            const auto main=code.runtime.entry_points.entries[1].action;
            if(main>255&&part.vm->has_routine(routine_key(main),part.owner))
                need(part.vm->start(routine_key(main),{part.identity.id,part.identity.id,0,part.owner},{0,0,0,0},true)==VmStatus::running,
                    "trade member Main could not be staged");
        }
        ObjectPositionValid placement(c.world,binding.placement);ObjectPositionChange change(c.world,placement,binding.position_change);
        // VMContext.CreateObjectInstance completes the constructor's own OOW
        // ChangePosition and birth publication before the user placement attempt.
        const auto initial=change.apply_existing_group({{base.id,base.generation},{-32768,-32768,1,request.position.direction},
            request.position.direction,0,binding.command_still_matches});
        need(initial.status==ObjectPositionChangeStatus::success,"trade group initial out-of-world lifecycle unsupported");
        if(c.clock){const auto born=c.clock->snapshot();for(const auto id:identities)
            for(const auto& field:std::array<std::pair<std::int16_t,std::int16_t>,5>{{{45,static_cast<std::int16_t>(born.year)},
                {46,born.month},{47,born.day},{24,born.hour},{21,born.minute}}})
                need(c.world.declare_object_word(id.id,field.first,field.second),"trade group birth-time publication failed");}
        unchanged_group(c.world,plan,identities);
        const auto moved=change.apply_existing_group({{base.id,base.generation},request.position,request.position.direction,3,binding.command_still_matches});
        need(moved.status!=ObjectPositionChangeStatus::unsupported,"trade group placement lifecycle unsupported");
        const bool placed_before_user_entry=c.world.find(base.id)->position.has_value();
        user_placement(binding,{base.id,base.generation},threads,plan,budget,primitive);
        unchanged_group(c.world,plan,identities);
        const bool refused=!c.world.find(base.id)->position;
        if(refused){
            erase_physical_group(c,identities);
            for(const auto id:identities)retire(c,id,binding,plan.multitile||identities.size()>1);
            if(request.catalog.state==ObjectTradeCatalogState::present){const auto refund=ts1_object_trade_balance({true,c.bundle.host.family_budget},*price.amount,true);
                if(refund.decision.status==ObjectTradeStatus::allowed)c.bundle.host.family_budget=refund.budget_after;}
        }else{
            need(c.values->set_purchase_price(base,*price.amount,purchase_price_origin),"trade final successful purchase price could not publish");
            for(std::size_t n=0;n<threads.size();++n)publish_object_thread(c,threads[n].identity,*threads[n].vm,plan.parts[n].resource);
        }
        ObjectTradeTransactionResult result;c.finish(result,limits,binding,refused);
        if(refused)result.diagnostic=std::string("purchase authored refusal: placement=")+
            (moved.status==ObjectPositionChangeStatus::success?"success":"blocked")+
            " error="+std::to_string(moved.placement.placement_error)+
            " blocker="+std::to_string(moved.placement.object.id)+
            " generation="+std::to_string(moved.placement.object.generation)+
            " in_world_before_user_entry="+(placed_before_user_entry?"true":"false")+
            (moved.diagnostic.empty()?"":"; "+moved.diagnostic)+
            (moved.placement.diagnostic.empty()?"":"; "+moved.placement.diagnostic);
        return result;
    }catch(const std::exception& e){return failure(e);}
}

ObjectTradeTransactionResult prepare_ts1_object_sale_impl(const SessionBundle& source,const ObjectSaleRequest& request,
    const ObjectTradeResource& resource,const ObjectTradeBindingFactory& factory,SessionBundleLimits limits,std::uint64_t budget,bool actorless){
    try{
        need(!source.build_buy_permission||source.build_buy_permission->enabled,"Build/Buy is disabled by the current lot simulation");
        const auto plan=group_resources(resource,false);need(static_cast<bool>(factory),"trade binding factory unavailable");
        for(const auto& part:plan.parts)resource_profile(part.resource);
        Candidate c(source,limits,actorless);const auto* values=c.values->query(request.target);
        need(values,"trade sale has no authenticated shared-value group");
        const auto identities=authenticate_existing_group(source,c.world,*values,plan,request.target);
        const auto clicked=std::find_if(identities.begin(),identities.end(),[&](auto id){return same_identity(id,request.target);});
        const auto clicked_index=static_cast<std::size_t>(clicked-identities.begin());
        need(same_origin(resource.origin,plan.parts[clicked_index].resource.origin),"trade clicked command resource differs from physical selected member");
        // VMEntity.Delete(CleanupAll) dispatches only this selected member for
        // DynamicMultitile. Its association/split lifecycle must not be replaced
        // by static whole-group deletion.
        const bool selected_only=identities.size()>1&&plan.parts[clicked_index].resource.runtime.entry_points.entries[8].action>=256;
        const auto retiring=selected_only?std::vector<ObjectTradeIdentity>{request.target}:identities;
        std::set<std::int16_t> deleting;for(const auto id:retiring)deleting.insert(id.id);
        for(const auto id:retiring){leaf_topology(c.world,id,true);no_execution_reference(source,id.id,deleting);}
        const auto refund=c.values->refund(request.target);need(refund.amount.has_value(),"trade refund is unknown");
        const auto group_id=values->group_id,revision=values->revision;
        const auto disabled=values->members[clicked_index].disabled?values->members[clicked_index].disabled:request.selected_disabled;
        ObjectTradePermissionInput permission{c.world,request.target,request.groups,request.avatar_threads,request.avatar_threads_complete,
            request.buildable,request.move_objects,true,disabled};
        const auto allowed=ts1_object_trade_permission(permission);
        if(allowed.status!=ObjectTradeStatus::allowed){ObjectTradeTransactionResult result;result.decision=allowed;
            result.status=allowed.status==ObjectTradeStatus::rejected?ObjectTradeTransactionStatus::rejected_preflight:ObjectTradeTransactionStatus::unsupported;return result;}
        for(const auto id:retiring){const auto thread=std::find_if(source.object_threads.begin(),source.object_threads.end(),[&](const auto& t){return t.id==id.id;});
            need(thread!=source.object_threads.end(),"trade group source Thread is absent");const auto& seed=thread->vm;
            need(seed.suspended_pending.empty()&&!seed.interaction_return&&!seed.external_frame_return&&seed.immediate_continuations.empty()&&
                !seed.immediate_return&&(!seed.pending||(thread->sleep.job&&seed.pending->instruction.instruction.opcode==0&&
                    thread->sleep.job->ticket==seed.pending->ticket)),"trade owned asynchronous cleanup is outside Main/Sleep profile");}
        auto binding=factory(c.world,c.relations,c.bundle.host,c.random,c.globals?&*c.globals:nullptr,c.clock?&*c.clock:nullptr,resource);authority(binding);
        need(static_cast<bool>(binding.primitive_factory),"trade synchronous primitive factory unavailable");
        if(binding.on_instruction_budget)binding.on_instruction_budget(budget);
        const auto dynamic=bind_dynamic(c,binding);
        const auto primitive=binding.primitive_factory(binding.memory);
        auto threads=bind_part_threads(c,source,plan,identities,binding,primitive,false);
        const auto* current=c.values->query(request.target);
        need(current&&current->group_id==group_id&&current->revision==revision,"trade shared value changed during lifecycle admission");
        auto& selected=threads[clicked_index];
        entry(*selected.vm,plan.parts[clicked_index].resource,selected.owner,request.target.id,12,budget,binding,primitive,&selected.programs.owners);
        unchanged_group(c.world,plan,identities);
        if(selected_only){need(dynamic&&dynamic->ensure({request.target.id,request.target.generation}),"trade dynamic sale association authority unavailable");}
        else erase_physical_group(c,identities);
        for(const auto id:retiring){retire(c,id,binding,plan.multitile||identities.size()>1||plan.live_association);
            if(selected_only)need(dynamic->erase_after_retire({id.id,id.generation}),"trade dynamic sale physical retirement incomplete");}
        if(selected_only&&!plan.multitile)for(const auto id:identities)if(!same_identity(id,request.target))
            need(dynamic->update({id.id,id.generation},false),"trade dynamic sale survivor flags incomplete");
        const auto after=ts1_object_trade_balance({true,c.bundle.host.family_budget},*refund.amount,true);
        if(after.decision.status==ObjectTradeStatus::allowed)c.bundle.host.family_budget=after.budget_after;
        ObjectTradeTransactionResult result;c.finish(result,limits,binding,false);return result;
    }catch(const std::exception& e){return failure(e);}
}
ObjectTradeTransactionResult prepare_ts1_object_move_impl(const SessionBundle& source,const ObjectMoveRequest& request,
    const ObjectTradeResource& resource,const ObjectTradeBindingFactory& factory,SessionBundleLimits limits,std::uint64_t budget,bool actorless){
    try{
        need(!source.build_buy_permission||source.build_buy_permission->enabled,"Build/Buy is disabled by the current lot simulation");
        const auto plan=group_resources(resource,false);need(static_cast<bool>(factory),"trade binding factory unavailable");
        Candidate c(source,limits,actorless);const auto target=request.admission.target;const auto* values=c.values->query(target);
        need(values,"trade movement has no authenticated shared-value/physical group");
        const auto identities=authenticate_existing_group(source,c.world,*values,plan,target);
        const auto clicked=std::find_if(identities.begin(),identities.end(),[&](auto id){return same_identity(id,target);});
        need(same_origin(resource.origin,plan.parts[static_cast<std::size_t>(clicked-identities.begin())].resource.origin),
            "trade movement command resource differs from clicked physical member");
        // Pinned VMNetMoveObjectCmd TS1 performs SetPosition after identity
        // lookup. IsUserMovable belongs to its !TS1 branch and to the original
        // UI's separate BaseObject filter, not a clicked-piece server filter.
        // Keep complete resource/identity/geometry/event authentication here.
        auto binding=factory(c.world,c.relations,c.bundle.host,c.random,c.globals?&*c.globals:nullptr,c.clock?&*c.clock:nullptr,resource);
        authority(binding);need(static_cast<bool>(binding.primitive_factory),"object move primitive factory missing");
        if(binding.on_instruction_budget)binding.on_instruction_budget(budget);
        const auto dynamic=bind_dynamic(c,binding);
        const auto primitive=binding.primitive_factory(binding.memory);
        auto threads=bind_part_threads(c,source,plan,identities,binding,primitive,false);
        ObjectPositionValid placement(c.world,binding.placement);ObjectPositionChange change(c.world,placement,binding.position_change);
        const auto moved=change.apply_existing({{target.id,target.generation},request.position,request.position.direction,3,binding.command_still_matches});
        need(moved.status!=ObjectPositionChangeStatus::unsupported,"object group move lifecycle unsupported");
        if(moved.status==ObjectPositionChangeStatus::success){
            user_placement(binding,{target.id,target.generation},threads,plan,budget,primitive);
            unchanged_group(c.world,plan,identities);
        }
        ObjectTradeTransactionResult result;c.finish(result,limits,binding,moved.status==ObjectPositionChangeStatus::blocked);return result;
    }catch(const std::exception& e){return failure(e);}
}

namespace {
ActorlessObjectTradeResult actorless_result(ObjectTradeTransactionResult result) {
    ActorlessObjectTradeResult out;out.status=result.status;out.decision=result.decision;
    out.diagnostic=std::move(result.diagnostic);out.archive=std::move(result.archive);
    if(result.candidate)out.candidate=actorless_object_trade_from_components(*result.candidate);
    return out;
}
}

ObjectTradeTransactionResult prepare_ts1_object_purchase(const SessionBundle& source,const ObjectPurchaseRequest& request,
    const ObjectTradeResource& resource,const ObjectTradeBindingFactory& factory,SessionBundleLimits limits,std::uint64_t budget) {
    return prepare_ts1_object_purchase_impl(source,request,resource,factory,limits,budget,false);
}
ActorlessObjectTradeResult prepare_ts1_actorless_object_purchase(const ActorlessObjectTradeState& source,const ObjectPurchaseRequest& request,
    const ObjectTradeResource& resource,const ObjectTradeBindingFactory& factory,SessionBundleLimits limits,std::uint64_t budget) {
    return actorless_result(prepare_ts1_object_purchase_impl(actorless_object_trade_components(source),request,resource,factory,limits,budget,true));
}

ObjectTradeTransactionResult prepare_ts1_object_sale(const SessionBundle& source,const ObjectSaleRequest& request,
    const ObjectTradeResource& resource,const ObjectTradeBindingFactory& factory,SessionBundleLimits limits,std::uint64_t budget) {
    return prepare_ts1_object_sale_impl(source,request,resource,factory,limits,budget,false);
}
ActorlessObjectTradeResult prepare_ts1_actorless_object_sale(const ActorlessObjectTradeState& source,const ObjectSaleRequest& request,
    const ObjectTradeResource& resource,const ObjectTradeBindingFactory& factory,SessionBundleLimits limits,std::uint64_t budget) {
    return actorless_result(prepare_ts1_object_sale_impl(actorless_object_trade_components(source),request,resource,factory,limits,budget,true));
}

ObjectTradeTransactionResult prepare_ts1_object_move(const SessionBundle& source,const ObjectMoveRequest& request,
    const ObjectTradeResource& resource,const ObjectTradeBindingFactory& factory,SessionBundleLimits limits,std::uint64_t budget) {
    return prepare_ts1_object_move_impl(source,request,resource,factory,limits,budget,false);
}
ActorlessObjectTradeResult prepare_ts1_actorless_object_move(const ActorlessObjectTradeState& source,const ObjectMoveRequest& request,
    const ObjectTradeResource& resource,const ObjectTradeBindingFactory& factory,SessionBundleLimits limits,std::uint64_t budget) {
    return actorless_result(prepare_ts1_object_move_impl(actorless_object_trade_components(source),request,resource,factory,limits,budget,true));
}
} // namespace odh
