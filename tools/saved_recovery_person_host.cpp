// SPDX-License-Identifier: MPL-2.0
#include "saved_recovery_person_host.hpp"
#include <algorithm>
#include <exception>
#include <map>
#include <set>

namespace odh_host {
struct SavedRecoveryPersonHost::State {
    SavedRecoveryPersonHostContext context;
    struct Binding {SavedRecoveryAvatarBinding actual;odh::SavedWorldEntity source;};
    std::map<odh::WorldEntityReference,Binding> bindings;
    std::map<std::pair<odh::WorldEntityReference,std::uint32_t>,SavedRecoveryAvatarOwner> owners;
    struct CachedAction {odh::WorldEntityReference person;SavedRecoveryAvatarActionSource source;};
    std::map<const odh::ObjmSavedInteraction*,CachedAction> actions;
    std::map<odh::WorldEntityReference,std::size_t> reset_progress;
    std::set<odh::WorldEntityReference> required;
    std::unique_ptr<SavedRecoveryAvatarImport> importer;
    std::unique_ptr<SavedRecoveryPersonService> people;
    bool prepared=false,consumed=false,ready=false,failed=false;
    explicit State(SavedRecoveryPersonHostContext c):context(std::move(c)){}
    bool lease() const {
        return !failed&&context.avatar_import.source_still_matches&&context.avatar_import.source_still_matches()&&
            context.person.source_still_matches&&context.person.source_still_matches();
    }
    const Binding* binding(odh::WorldEntityReference ref) const {
        if(!lease())return nullptr;
        const auto found=bindings.find(ref);if(found==bindings.end())return nullptr;
        const auto& b=found->second.actual;
        if(!context.binding_still_matches||!context.binding_still_matches(ref,b.thread,b.queue,b.controllers))return nullptr;
        return &found->second;
    }
    bool frozen_banks() const {
        if(!lease())return false;
        for(const auto& [key,code]:owners){const auto* b=binding(key.first);
            if(!b||b->actual.thread->shared_program_for_owner(code.owner)!=code.program)return false;}
        for(const auto& [raw,code]:actions){
            (void)raw;const auto* actual=binding(code.person);
            if(!actual||actual->actual.thread->shared_program_for_owner(code.source.code.owner)!=code.source.code.program)return false;
        }
        return true;
    }
};
SavedRecoveryPersonHost::SavedRecoveryPersonHost(SavedRecoveryPersonHostContext c):state_(std::make_unique<State>(std::move(c))){}
SavedRecoveryPersonHost::~SavedRecoveryPersonHost()=default;
bool SavedRecoveryPersonHost::prepare(const std::vector<SavedRecoveryAvatarBinding>& input,std::string& error){
    auto& s=*state_;
    const auto fail=[&](const std::string& why){error=why;s.failed=true;return false;};
    try{
        auto& a=s.context.avatar_import;auto& p=s.context.person;
        if(s.prepared||s.consumed||!s.lease()||!a.world||a.world!=p.world||!s.context.binding_still_matches||
           !a.resolve_owner||!a.resolve_action||!a.original_reference||!a.will_reset_before_dispatch||
           !p.supports_phase||!p.execute_phase||!p.supports_entry4||!p.skipped_entry4||!p.update_footprint||
           input.size()>a.avatars)return fail("person host lacks its complete original-load owners");
        for(const auto& b:input){
            if(!b.source||!b.source->tail.person||!b.thread||!b.queue||!b.controllers||
               !s.context.binding_still_matches(b.person,b.thread,b.queue,b.controllers)||
               !a.world->is_live_reference(b.person)||!b.controllers->bound_to(*b.thread)||&b.controllers->interactions()!=b.queue)
                return fail("person host actual owner/source identity unavailable");
            State::Binding stored{b,*b.source};
            if(!s.bindings.emplace(b.person,std::move(stored)).second)return fail("person host duplicate original person");
        }
        // Preserve every currently notifiable person, including non-residents.
        for(std::int16_t id=0;;){const auto next=a.world->next_entity(id);if(!next)break;id=*next;
            const auto* body=a.world->find(id);
            if(body->kind==odh::EntityKind::avatar&&!body->dead&&!body->ui_ghost&&
               !s.bindings.count({id,body->generation}))return fail("person host omitted a live notifiable avatar");}
        std::vector<SavedRecoveryAvatarBinding> bindings;bindings.reserve(s.bindings.size());
        for(auto& [ref,b]:s.bindings){b.actual.source=&b.source;bindings.push_back(b.actual);
            if(a.will_reset_before_dispatch(ref))s.required.insert(ref);
            for(const auto phase:{odh::SavedRecoveryPhase::person_unwind,odh::SavedRecoveryPhase::person_visuals,
                    odh::SavedRecoveryPhase::person_placement,odh::SavedRecoveryPhase::restart_main,odh::SavedRecoveryPhase::person_cleanup})
                if(!p.supports_phase(ref,phase,error))return fail(error);
        }
        auto import=a;
        import.source_still_matches=[&s]{return s.lease();};
        import.resolve_owner=[&s,resolve=a.resolve_owner](auto ref,std::uint32_t guid)->std::optional<SavedRecoveryAvatarOwner>{
            if(!s.binding(ref))return {};const auto key=std::make_pair(ref,guid);
            const auto found=s.owners.find(key);if(found!=s.owners.end())return found->second;
            const auto code=resolve(ref,guid);if(!code)return {};
            s.owners.emplace(key,*code);return code;
        };
        import.resolve_action=[&s,resolve=a.resolve_action](auto ref,auto target,const auto& raw)->std::optional<SavedRecoveryAvatarActionSource>{
            if(!s.binding(ref))return {};const auto found=s.actions.find(&raw);
            if(found!=s.actions.end())return found->second.source;
            const auto code=resolve(ref,target,raw);if(!code)return {};
            s.actions.emplace(&raw,State::CachedAction{ref,*code});return code;
        };
        s.importer=std::make_unique<SavedRecoveryAvatarImport>(std::move(import));
        if(!s.importer->prepare(bindings,error)||!s.frozen_banks())return fail(error.empty()?"person host resource/source preflight expired":error);
        s.prepared=true;return true;
    }catch(const std::exception& e){return fail(std::string("person host preparation: ")+e.what());}
}
SavedRecoveryAvatarImportResult SavedRecoveryPersonHost::commit(){
    auto& s=*state_;SavedRecoveryAvatarImportResult result;
    if(!s.prepared||s.consumed||s.failed){result.diagnostic="person host is unprepared, failed or consumed";return result;}
    s.consumed=true;
    try{
        if(!s.frozen_banks()){s.failed=true;result.diagnostic="person host frozen owners expired before hydration";return result;}
        result=s.importer->commit();if(!result.completed){s.failed=true;return result;}
        result.completed=false;result.discard_candidate=true;
        auto context=s.context.person;
        context.source_still_matches=[&s]{return s.frozen_banks();};
        context.original_reference=s.context.avatar_import.original_reference;
        context.suppress_referenced_reset=[&s](auto ref){return s.lease()?s.context.avatar_import.world->suppress_referenced_reset(ref):std::nullopt;};
        context.remove_action=[&s](auto ref,std::uint64_t uid,bool active,std::string& error){
            const auto* b=s.binding(ref);const bool ok=b&&b->actual.queue->source_recovery_remove(uid,active);
            if(!ok)error="person host actual queue removal refused";return ok;
        };
        context.set_active_icon=[&s](auto ref,std::uint64_t uid,auto icon,std::string& error){
            const auto* b=s.binding(ref);const bool ok=b&&b->actual.queue->source_recovery_set_action_icon(uid,icon);
            if(!ok)error="person host actual action icon update refused";return ok;
        };
        context.pop_frames=[&s](auto ref,std::size_t depth,std::string& error){
            const auto* b=s.binding(ref);const bool ok=b&&b->actual.thread->source_recovery_pop_to_depth(depth);
            if(!ok)error="person host actual ordered frame pop refused";return ok;
        };
        std::vector<SavedRecoveryPersonBinding> bindings;bindings.reserve(s.bindings.size());
        for(auto& [ref,b]:s.bindings){if(!s.binding(ref)){s.failed=true;result.diagnostic="person host owner changed during hydration";return result;}
            bindings.push_back({ref,b.actual.thread,b.actual.queue,b.actual.controllers,&b.source});}
        s.people=std::make_unique<SavedRecoveryPersonService>(std::move(context));
        if(!s.people->prepare(bindings,result.diagnostic)){s.failed=true;return result;}
        s.ready=true;result.completed=true;result.discard_candidate=false;return result;
    }catch(const std::exception& e){s.failed=true;result.diagnostic=std::string("person host publication: ")+e.what();return result;}
}
bool SavedRecoveryPersonHost::ready() const {return state_->ready&&state_->frozen_banks();}
bool SavedRecoveryPersonHost::dispatch_ready() const {
    if(!ready())return false;
    for(const auto ref:state_->required)if(state_->context.avatar_import.world->is_live_reference(ref))return false;
    return true;
}
bool SavedRecoveryPersonHost::supports(const odh::SavedRecoveryCall& call,std::string& error) const {
    if(!ready()||!state_->binding(call.entity)){error="person host is not hydrated or lost actual owner";return false;}
    return state_->people->supports(call,error);
}
bool SavedRecoveryPersonHost::execute(const odh::SavedRecoveryCall& call,std::string& error){
    auto& s=*state_;
    if(!supports(call,error))return false;
    try{
        const std::array<odh::SavedRecoveryPhase,6> sequence{odh::SavedRecoveryPhase::person_unwind,
            odh::SavedRecoveryPhase::person_visuals,odh::SavedRecoveryPhase::person_placement,
            odh::SavedRecoveryPhase::restart_main,odh::SavedRecoveryPhase::person_cleanup,odh::SavedRecoveryPhase::person_actions};
        const auto phase=std::find(sequence.begin(),sequence.end(),call.phase);
        if(phase!=sequence.end()){
            const auto progress=s.reset_progress[call.entity];
            if(progress>=sequence.size()||sequence[progress]!=call.phase){
                error="person host full reset phases are out of order";s.failed=true;return false;
            }
        }
        if(!s.people->execute(call,error)){s.failed=true;return false;}
        if(s.people->completed_self_reset(call.entity))s.required.erase(call.entity);
        if(phase!=sequence.end()){
            auto& progress=s.reset_progress[call.entity];++progress;
            if(progress==sequence.size()){s.required.erase(call.entity);progress=0;}
        }
        return true;
    }catch(const std::exception& e){s.failed=true;error=std::string("person host execution: ")+e.what();return false;}
}
}
