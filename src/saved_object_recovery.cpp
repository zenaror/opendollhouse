#include "odh/saved_object_recovery.hpp"
#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <stdexcept>

namespace odh {
namespace {
using Phase=SavedRecoveryPhase;
constexpr std::uint32_t cursor_guid=0x3c08965f;
// Word67 is the existing structural slot table, preserved separately by World.
constexpr std::array<std::int16_t,8> retained_words{1,21,24,29,41,45,46,47};
bool structural_word(std::int16_t index){return index==2 || index==3 || index==11 || index==67;}
std::vector<WorldEntityReference> members(const SavedRecoveryEntity& e) {
    return e.pieces.empty()?std::vector<WorldEntityReference>{e.reference}:e.pieces;
}
bool alive(const World& w,WorldEntityReference r) {
    const auto* e=w.resolve_reference(r);return w.is_live_reference(r) && e && !e->dead;
}
bool clear_object(World& world,const SavedRecoveryEntity& descriptor,std::string& error) {
    const auto ref=descriptor.reference;
    const auto* object=world.resolve_reference(ref);
    if(!world.is_live_reference(ref) || !object){error="native clear cursor is no longer indexed with its original identity";return false;}
    std::array<std::int16_t,8> retained{};
    for(std::size_t i=0;i<retained.size();++i) {
        const auto found=object->object_words.find(retained_words[i]);
        if(found==object->object_words.end()){error="missing retained recovery word";return false;}
        retained[i]=found->second;
    }
    const auto attributes=object->attributes.size();
    // Engine indices must already have been detached by before_clear. This is
    // a raw native bank clear, not SetValue on virtual/read-only object words.
    for(std::int16_t index=0;index<72;++index)
        if(!structural_word(index) && !world.declare_object_word(ref.id,index,0)){error="cannot clear object word";return false;}
    for(std::size_t i=0;i<retained.size();++i)
        if(!world.declare_object_word(ref.id,retained_words[i],retained[i]))return false;
    const auto direction=static_cast<std::int16_t>((retained[0]%8+8)%8);
    if(!world.declare_object_word(ref.id,1,direction) ||
       !world.declare_object_word(ref.id,58,descriptor.definition_version))return false;
    VmFrame frame{};frame.context={ref.id,ref.id,0,0};
    if(!world.bind_context_live(frame.context)){error="cannot bind recovery attribute owner";return false;}
    for(std::size_t i=0;i<attributes;++i)
        if(i>32767 || !world.write(frame,0,static_cast<std::int16_t>(i),0)){
            error="cannot clear recovery attribute";return false;
        }
    // Only the eight original native temporary words are demonstrated cleared.
    // Extended registers are not silently treated as native saved words.
    std::fill_n(descriptor.thread->temps().begin(),8,0);
    return true;
}
}
bool saved_person_notification_requires_reset(WorldEntityReference target,
    const std::vector<WorldEntityReference>& active_uses,WorldEntityReference active_icon) {
    return target.id>0 && target.generation!=0 &&
        (target==active_icon || std::find(active_uses.begin(),active_uses.end(),target)!=active_uses.end());
}
SavedRecoveryResult recover_original_saved_objects(World& world,
    const std::vector<SavedRecoveryEntity>& cohort,const SavedRecoveryHost& host,bool enabled,SavedRecoveryLimits limits) {
    SavedRecoveryResult result;
    auto fail=[&](const std::string& message){result.diagnostic=message;return result;};
    if(!host.preflight || !host.supports || !host.execute || !host.world_head || !host.world_next)
        return fail("saved recovery requires actual class adapters and live world traversal authorities");
    if(cohort.size()>limits.entities)return fail("saved recovery cohort exceeds entity limit");
    std::map<WorldEntityReference,const SavedRecoveryEntity*> entities;
    for(const auto& e:cohort) {
        const auto* live=world.resolve_reference(e.reference);
        if(e.reference.id<=0 || !world.is_live_reference(e.reference) || !live || live->dead || !live->definition ||
           !e.thread || e.thread->dispatch_active() || !entities.emplace(e.reference,&e).second)
            return fail("saved recovery cohort has missing, dead, duplicate or actively executing identity");
        if((live->kind==EntityKind::avatar)!=(e.kind==SavedRecoveryClass::person))
            return fail("saved recovery class does not match live person identity");
        if((live->definition->guid==cursor_guid)!=(e.kind==SavedRecoveryClass::special_cursor))
            return fail("special cursor recovery requires the exact native GUID");
        if(e.kind!=SavedRecoveryClass::person)
            for(auto index:retained_words)if(!live->object_words.count(index))
                return fail("saved recovery requires complete retained object words");
        if(e.kind==SavedRecoveryClass::person && live->person_data.size()<=59)
            return fail("saved person recovery requires the original person bank");
    }
    // Include ALL live people: a compatible person still receives notifications.
    for(auto id=world.next_entity(0);id;id=world.next_entity(*id)) {
        const auto* e=world.find(*id);
        if(e && !e->dead && e->kind==EntityKind::avatar && !entities.count({e->id,e->generation}))
            return fail("saved recovery cohort omits a live person receiving notifications");
    }
    for(const auto& e:cohort) {
        const auto pieces=members(e);std::set<WorldEntityReference> distinct;
        if(std::find(pieces.begin(),pieces.end(),e.reference)==pieces.end())
            return fail("saved recovery group omits its original target");
        for(const auto ref:pieces) {
            if(!distinct.insert(ref).second || !entities.count(ref) || !alive(world,ref))
                return fail("saved recovery group lacks an exact live descriptor");
            if(e.native_group && entities.at(ref)->kind!=SavedRecoveryClass::object &&
                entities.at(ref)->kind!=SavedRecoveryClass::portal)
                return fail("native group contains an unsupported nonobject class");
        }
        if(!e.native_group && pieces.size()!=1)
            return fail("saved recovery multiple pieces require an actual native group adapter");
    }
    std::string diagnostic;
    try {
        if(!host.preflight(cohort,diagnostic))return fail("saved recovery preflight: "+diagnostic);
        // Build executable virtual-reset sequences independently of post-load
        // entry. Person notifications can cascade into compatible use targets.
        struct Operation {SavedRecoveryCall call;bool clear=false,native_cursor=false,probe_candidates=false;};
        std::map<WorldEntityReference,std::vector<Operation>> resets;
        std::size_t planned_operations=0;
        for(const auto& e:cohort) {
            auto& plan=resets[e.reference];
            const auto target=e.reference;
            const auto add=[&](Phase p,WorldEntityReference entity,bool full=false,bool clear=false,bool probe_candidates=false){
                if(planned_operations>=limits.planned_operations)throw std::runtime_error("saved recovery plan exceeds operation limit");
                ++planned_operations;plan.push_back({{p,entity,target,full,{}},clear,false,probe_candidates});};
            for(const auto& person:cohort)if(person.kind==SavedRecoveryClass::person && person.reference!=target)
                add(Phase::notify_person,person.reference);
            // The exact always-reset definition is Placeholder.iff, native
            // ordinary cXObject (type4/master0), not cXCursorObject. Its marker
            // selects WHEN to reset; the real shared class service owns HOW.
            if(e.kind==SavedRecoveryClass::person) {
                for(const auto phase:{Phase::person_unwind,Phase::person_visuals,Phase::person_placement,
                        Phase::restart_main,Phase::person_cleanup,Phase::person_actions})
                    add(phase,target,true);
            } else if(e.has_cleanup) {
                for(const auto phase:{Phase::cleanup,Phase::cleanup_flags,Phase::restart_main})add(phase,target);
            } else {
                auto pieces=members(e);
                if(e.native_group) {
                    // Store one phase template, not every possible receiver.
                    // Capability admission expands it below before any effects;
                    // execution still obtains every member from live links.
                    pieces={target};
                    add(Phase::capture_placement,target);
                } else for(auto ref:pieces)add(Phase::capture_placement,ref);
                if(e.kind==SavedRecoveryClass::portal)add(Phase::portal_detach,target);
                // Native422db9 owns an explicit EP10 pass only for the MT
                // virtual removal. Ordinary3206d calls4331d4 directly: that
                // consumer already executes EP10 when OBJD.MasterID is zero.
                if(e.native_group)for(auto ref:pieces)add(Phase::remove_entry,ref,false,false,true);
                for(auto ref:pieces)add(Phase::detach,ref,false,false,e.native_group);
                // Contained ordinary objects have additional detach/restore EP9;
                // placement adapters own this native structural distinction.
                for(auto ref:pieces) {
                    add(Phase::before_clear,ref,false,true,e.native_group);
                    add(Phase::restart_main,ref,false,false,e.native_group);
                }
                if(e.native_group)for(auto ref:pieces)add(Phase::restore_group,ref,false,false,e.native_group);
                for(auto ref:pieces)add(Phase::init,ref,false,false,e.native_group);
                for(auto ref:pieces)add(Phase::restore_placement,ref,false,false,e.native_group);
                for(auto ref:pieces)add(Phase::placement_entry,ref,false,false,e.native_group);
                if(e.kind==SavedRecoveryClass::portal)add(Phase::portal_restore,target);
            }
            add(Phase::refresh,target);
        }
        // Probe every actual class adapter, including compatible referenced-use
        // targets, before the first callback. A supports probe never runs BHAV.
        const auto supported=[&](const SavedRecoveryCall& call){
            diagnostic.clear();return host.supports(call,diagnostic);
        };
        const bool any_recovery=std::any_of(cohort.begin(),cohort.end(),[&](const auto& e){
            return (enabled && e.code_changed) || e.kind==SavedRecoveryClass::special_cursor;
        });
        if(any_recovery && !host.begin_person_notifications)
            return fail("saved recovery requires the actual person registration cursor");
        for(const auto& e:cohort) {
            if(!supported({Phase::post_load_lookup_flags,e.reference,e.reference,false,{}}))
                return fail("saved recovery unsupported post-load lookup ownership: "+diagnostic);
            if(e.kind!=SavedRecoveryClass::person &&
                !supported({Phase::load,e.reference,e.reference,false,{}}))
                return fail("saved recovery unsupported Load: "+diagnostic);
            if(any_recovery) {
                for(const auto& op:resets.at(e.reference)) {
                    if(op.probe_candidates) {
                        // before_clear/restart_main were interleaved per body;
                        // preserve that probe order without storing the pairs.
                        if(op.call.phase==Phase::restart_main)continue;
                        // This is an admission inventory, not membership: a
                        // callback can append any authenticated object later.
                        // Never cache supports across targets or defer it until
                        // execution, when rejecting would already leave effects.
                        for(const auto& candidate:cohort)
                            if(candidate.kind==SavedRecoveryClass::object || candidate.kind==SavedRecoveryClass::portal) {
                                auto call=op.call;call.entity=candidate.reference;
                                if(!supported(call))return fail("saved recovery unsupported phase: "+diagnostic);
                                if(call.phase==Phase::before_clear) {
                                    call.phase=Phase::restart_main;
                                    if(!supported(call))return fail("saved recovery unsupported phase: "+diagnostic);
                                }
                            }
                    } else if(!supported(op.call))return fail("saved recovery unsupported phase: "+diagnostic);
                }
                if(e.kind==SavedRecoveryClass::portal &&
                    !supported({Phase::portal_reconcile,e.reference,e.reference,false,{}}))
                    return fail("saved recovery unsupported portal reconciliation: "+diagnostic);
                if(enabled && e.code_changed && !supported({Phase::init,e.reference,e.reference,false,{}}))
                    return fail("saved recovery unsupported final Init: "+diagnostic);
            }
        }
        if(any_recovery && std::any_of(cohort.begin(),cohort.end(),[](const auto& e){return e.native_group && !e.has_cleanup;}) &&
            (!host.group_head || !host.group_next))return fail("native group recovery requires authenticated live head and next authorities");
        result.prepared=true;
        std::set<WorldEntityReference> resetting;
        std::function<bool(WorldEntityReference,bool)> reset;
        std::function<bool(Operation)> execute;
        execute=[&](Operation op){
            if(op.native_cursor) {
                if(!entities.count(op.call.entity) || !world.resolve_reference(op.call.entity)){
                    result.diagnostic="saved recovery callback lacks its exact original body";return false;
                }
            } else if(op.call.phase!=Phase::portal_reconcile &&
                (!alive(world,op.call.entity) || !alive(world,op.call.target))) {
                if(resetting.count(op.call.target) && !world.resolve_reference(op.call.target)) {
                    result.diagnostic="saved recovery lost its exact target body during a callback";return false;
                }
                return true;
            }
            const auto* dispatch_body=world.resolve_reference(op.call.entity);
            op.call.deferred_dead=dispatch_body && dispatch_body->dead;
            op.call.reset_referenced=[&](WorldEntityReference ref,std::string& error){
                if(!entities.count(ref)){error="recursive use target lacks a preflighted descriptor";return false;}
                const bool ok=reset(ref,false);if(!ok)error=result.diagnostic;return ok;
            };
            if(result.operations>=limits.executed_operations){result.diagnostic="saved recovery execution exceeds operation limit";return false;}
            result.discard_candidate=true;diagnostic.clear();
            if(!host.execute(op.call,diagnostic)){
                result.diagnostic="saved recovery phase failed: "+diagnostic;return false;
            }
            ++result.operations;
            if(op.clear && !clear_object(world,*entities.at(op.call.entity),diagnostic)) {
                result.diagnostic="saved recovery bank clear failed: "+diagnostic;return false;
            }
            if(op.call.phase==Phase::person_cleanup && alive(world,op.call.entity)) {
                for(auto index:{0,40,59})if(!world.set_person_data(op.call.entity.id,index,0)) {
                    result.diagnostic="saved person recovery could not clear directed person fields";return false;
                }
                for(auto index:{34,9})if(!world.declare_object_word(op.call.entity.id,index,0)) {
                    result.diagnostic="saved person recovery could not clear directed object fields";return false;
                }
            }
            return true;
        };
        reset=[&](WorldEntityReference target,bool final_init){
            if(!alive(world,target) || resetting.count(target))return true;
            const auto found=entities.find(target);
            if(found==entities.end()){result.diagnostic="unprepared recursive recovery identity";return false;}
            if(resetting.size()>=limits.nested_resets){result.diagnostic="saved recovery exceeds nested reset limit";return false;}
            resetting.insert(target);
            struct Guard {std::set<WorldEntityReference>& set;WorldEntityReference ref;~Guard(){set.erase(ref);}} guard{resetting,target};
            const auto& descriptor=*found->second;
            const auto group_run=[&](){
                const auto call=[&](Phase phase,WorldEntityReference entity,bool clear=false){
                    return execute({{phase,entity,target,false,{}},clear,true});
                };
                const auto query=[&](bool head,WorldEntityReference cursor,Phase phase,SavedRecoveryLink& link){
                    diagnostic.clear();
                    link=head?host.group_head(cursor,phase,diagnostic):host.group_next(cursor,phase,diagnostic);
                    if(link.status==SavedRecoveryLinkStatus::unsupported) {
                        result.diagnostic="native group traversal unavailable: "+diagnostic;return false;
                    }
                    if(link.status!=SavedRecoveryLinkStatus::member && link.status!=SavedRecoveryLinkStatus::end) {
                        result.diagnostic="native group traversal returned an invalid status";return false;
                    }
                    if(link.status==SavedRecoveryLinkStatus::member &&
                        (link.reference.id<=0 || !link.reference.generation || !entities.count(link.reference) ||
                         (entities.at(link.reference)->kind!=SavedRecoveryClass::object &&
                          entities.at(link.reference)->kind!=SavedRecoveryClass::portal))) {
                        result.diagnostic="native group traversal reached an unprepared identity";return false;
                    }
                    if(link.status==SavedRecoveryLinkStatus::end && link.reference!=WorldEntityReference{}) {
                        result.diagnostic="native group END carries an unexpected identity";return false;
                    }
                    return true;
                };
                const auto traverse=[&](SavedRecoveryLink cursor,Phase phase,bool clear_pass=false){
                    std::set<WorldEntityReference> visited;
                    while(cursor.status==SavedRecoveryLinkStatus::member) {
                        if(!visited.insert(cursor.reference).second || visited.size()>limits.entities) {
                            result.diagnostic="native group traversal cycles or exceeds entity limit";return false;
                        }
                        const auto current=cursor.reference;
                        if(!call(phase,current,clear_pass))return false;
                        if(clear_pass && !call(Phase::restart_main,current))return false;
                        // Native reads CURRENT.next only after its callback and
                        // does so even when CURRENT became dead/unlinked.
                        if(!query(false,current,phase,cursor))return false;
                    }
                    return true;
                };
                if(!call(Phase::capture_placement,target))return false;
                if(descriptor.kind==SavedRecoveryClass::portal && !call(Phase::portal_detach,target))return false;
                SavedRecoveryLink detach_head;
                if(!query(true,target,Phase::remove_entry,detach_head))return false;
                if(detach_head.status!=SavedRecoveryLinkStatus::member) {
                    result.diagnostic="native group target has no authenticated source head";return false;
                }
                if(!traverse(detach_head,Phase::remove_entry) || !traverse(detach_head,Phase::detach))return false;
                // cXObject::Reset tests the ORIGINAL target's Dead bit after
                // the complete virtual detach; another piece never vetoes it.
                if(!alive(world,target))return true;
                SavedRecoveryLink reset_head;
                if(!query(true,target,Phase::before_clear,reset_head))return false;
                if(reset_head.status!=SavedRecoveryLinkStatus::member) {
                    result.diagnostic="native reset lost its source leader";return false;
                }
                if(!traverse(reset_head,Phase::before_clear,true) ||
                   !call(Phase::restore_group,reset_head.reference) ||
                   !traverse(reset_head,Phase::init))return false;
                SavedRecoveryLink placement_head;
                if(!query(true,target,Phase::restore_placement,placement_head))return false;
                if(placement_head.status!=SavedRecoveryLinkStatus::member) {
                    result.diagnostic="native placement lost its source leader";return false;
                }
                if(!traverse(placement_head,Phase::restore_placement) ||
                   !traverse(placement_head,Phase::placement_entry))return false;
                if(descriptor.kind==SavedRecoveryClass::portal && !call(Phase::portal_restore,target))return false;
                return true;
            };
            const auto run=[&](){
                // Raw444346 captures PERSON.begin once and reads its current
                // end after every callback. This is not World/cohort order.
                {
                    diagnostic.clear();
                    auto people=host.begin_person_notifications(diagnostic);
                    if(!people || !people->at) {
                        result.diagnostic="person notification cursor unavailable: "+diagnostic;return false;
                    }
                    for(std::size_t ordinal=0;;++ordinal) {
                        if(ordinal>=limits.executed_operations) {
                            result.diagnostic="person notification traversal exceeds iteration limit";return false;
                        }
                        diagnostic.clear();const auto link=people->at(ordinal,diagnostic);
                        if(link.status==SavedRecoveryLinkStatus::end && link.reference==WorldEntityReference{})break;
                        const auto person=entities.find(link.reference);
                        if(link.status!=SavedRecoveryLinkStatus::member || person==entities.end() ||
                           person->second->kind!=SavedRecoveryClass::person) {
                            result.diagnostic="person notification cursor unavailable or unprepared: "+diagnostic;return false;
                        }
                        // Native notifier skips only exact SELF. Deferred-dead
                        // people/targets still reach the real adapter; no ID reuse.
                        if(link.reference!=target &&
                           !execute({{Phase::notify_person,link.reference,target,false,{}},false,true}))return false;
                    }
                }
                if(!alive(world,target)) {
                    result.diagnostic="post-notification reset of removed/deferred-dead target requires its native class adapter";
                    return false;
                }
                if(descriptor.native_group && !descriptor.has_cleanup) {
                    if(alive(world,target) && !group_run())return false;
                    if(final_init && !execute({{Phase::init,target,target,false,{}},false,true}))return false;
                    return execute({{Phase::refresh,target,target,false,{}},false,false});
                }
                for(const auto& op:resets.at(target)) {
                    if(op.call.phase==Phase::notify_person)continue; // capability inventory only
                    if(op.call.phase==Phase::refresh && final_init)
                        if(!execute({{Phase::init,target,target,false,{}},false,false}))return false;
                    if(!execute(op))return false;
                }
                return true;
            };
            const auto reconcile=[&](){
                if(descriptor.kind!=SavedRecoveryClass::portal)return true;
                return execute({{Phase::portal_reconcile,target,target,false,{}},false,false});
            };
            bool ok=false;
            try {ok=run();}
            catch(...) {
                // Still discard the candidate; reconciliation is topology
                // maintenance of survivors, never a claimed rollback.
                try {reconcile();}catch(...) {}
                throw;
            }
            const auto original_error=result.diagnostic;
            const bool reconciled=reconcile();
            if(!ok && !original_error.empty()) {
                if(!reconciled)result.diagnostic=original_error+"; portal reconciliation: "+result.diagnostic;
                else result.diagnostic=original_error;
            }
            return ok && reconciled;
        };
        diagnostic.clear();
        auto world_cursor=host.world_head(diagnostic);
        std::set<WorldEntityReference> visited_world;
        const auto valid_world_link=[&](const SavedRecoveryLink& link){
            if(link.status==SavedRecoveryLinkStatus::unsupported) {
                result.diagnostic="saved world traversal unavailable: "+diagnostic;return false;
            }
            if(link.status==SavedRecoveryLinkStatus::end) {
                if(link.reference==WorldEntityReference{})return true;
                result.diagnostic="saved world END carries an unexpected identity";return false;
            }
            if(link.status!=SavedRecoveryLinkStatus::member || link.reference.id<=0 ||
                !link.reference.generation || !entities.count(link.reference)) {
                result.diagnostic="saved world traversal reached an unprepared identity";return false;
            }
            return true;
        };
        if(!valid_world_link(world_cursor))return result;
        while(world_cursor.status==SavedRecoveryLinkStatus::member) {
            const auto current=world_cursor.reference;
            if(!visited_world.insert(current).second || visited_world.size()>limits.entities) {
                result.diagnostic="saved world traversal cycles or exceeds entity limit";return result;
            }
            if(!world.resolve_reference(current)) {
                result.diagnostic="saved world cursor lacks its exact original body";return result;
            }
            const auto& e=*entities.at(current);
            if(e.kind!=SavedRecoveryClass::person &&
                !execute({{Phase::load,current,current,false,{}},false,true}))return result;
            const bool requires_reset=(enabled && e.code_changed) || e.kind==SavedRecoveryClass::special_cursor;
            if(requires_reset && !alive(world,current)) {
                result.diagnostic="saved world post-load class path for a deferred-dead or removed current is unsupported";return result;
            }
            if(requires_reset)
                if(!reset(current,enabled && e.code_changed))return result;
            if(!execute({{Phase::post_load_lookup_flags,current,current,false,{}},false,true}))return result;
            // Raw 43fce8 reads CURRENT.nexte0 only AFTER the complete virtual
            // post-load call. This remains mandatory if CURRENT was unlinked
            // or deleted: the authority must authenticate its actual next.
            diagnostic.clear();world_cursor=host.world_next(current,diagnostic);
            if(!valid_world_link(world_cursor))return result;
        }
        result.completed=true;result.discard_candidate=false;return result;
    } catch(const std::exception& e) {return fail(std::string("saved recovery exception: ")+e.what());}
}
} // namespace odh
