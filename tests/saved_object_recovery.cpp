#include "odh/saved_object_recovery.hpp"
#include <algorithm>
#include <tuple>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>

namespace {
using namespace odh;
using Phase=SavedRecoveryPhase;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Fixture {
    World world;
    std::vector<SavedRecoveryEntity> cohort;
    std::vector<std::unique_ptr<Vm>> threads;
    std::vector<std::pair<Phase,std::int16_t>> events;
    std::vector<WorldEntityReference> people;
    std::map<std::int16_t,std::optional<EntityPosition>> positions;
    std::map<WorldEntityReference,WorldEntityReference> next,parent,world_links;
    WorldEntityReference world_first{};
    unsigned world_head_queries=0;
    std::vector<WorldEntityReference> world_next_queries;
    std::optional<Phase> unsupported,fail;
    bool all_preflight=false,portal_detached=false,cascade=false;
    SavedRecoveryEntity& add(std::int16_t id,SavedRecoveryClass kind=SavedRecoveryClass::object,bool changed=true) {
        std::array<std::int16_t,68> bank{};for(std::size_t i=0;i<bank.size();++i)bank[i]=static_cast<std::int16_t>(100+i);
        bank[1]=3;bank[2]=0;bank[3]=-1;bank[67]=3;
        const auto guid=kind==SavedRecoveryClass::special_cursor?0x3c08965fU:0x12340000U+id;
        require(world.create_saved_entity(id,kind==SavedRecoveryClass::person?EntityKind::avatar:EntityKind::object,
            guid,{9,8},kind==SavedRecoveryClass::person?std::vector<std::int16_t>(101,77):std::vector<std::int16_t>{},
            kind==SavedRecoveryClass::person?std::vector<std::int16_t>(16,55):std::vector<std::int16_t>{},bank,{},3),"create imported entity");
        require(world.set_position(id,{id*16,32,1,3}),"place fixture");
        threads.push_back(std::make_unique<Vm>(RoutineSet{}));threads.back()->temps().fill(44);threads.back()->temp_xl().fill(99);
        const auto* e=world.find(id);
        cohort.push_back({{id,e->generation},kind,changed,false,false,23,threads.back().get(),{}});
        if(kind==SavedRecoveryClass::person)people.push_back({id,e->generation});
        return cohort.back();
    }
    SavedRecoveryHost host() {
        // An actual mutable linked ownership fixture. These links survive
        // deferred deletion independently of World numeric-ID lookups.
        for(const auto& e:cohort){next.emplace(e.reference,WorldEntityReference{});parent.emplace(e.reference,WorldEntityReference{});}
        for(const auto& e:cohort)if(e.native_group && !e.pieces.empty())
            for(std::size_t i=0;i<e.pieces.size();++i){
                const auto ref=e.pieces[i];parent[ref]=i?e.pieces.front():WorldEntityReference{};
                next[ref]=i+1<e.pieces.size()?e.pieces[i+1]:WorldEntityReference{};
            }
        for(std::size_t i=0;i<cohort.size();++i)world_links[cohort[i].reference]=
            i+1<cohort.size()?cohort[i+1].reference:WorldEntityReference{};
        world_first=cohort.empty()?WorldEntityReference{}:cohort.front().reference;
        SavedRecoveryHost h;
        h.begin_person_notifications=[this](std::string&)->std::optional<SavedRecoveryPersonTraversal>{
            const auto* storage=people.data();
            return SavedRecoveryPersonTraversal{[this,storage](std::size_t ordinal,std::string& error){
                if(people.data()!=storage || ordinal>people.size()){
                    error="person vector storage changed or cursor passed current end";return SavedRecoveryLink{};
                }
                return ordinal==people.size()?SavedRecoveryLink{SavedRecoveryLinkStatus::end,{}}:
                    SavedRecoveryLink{SavedRecoveryLinkStatus::member,people[ordinal]};
            }};
        };
        h.world_head=[this](std::string&){++world_head_queries;return SavedRecoveryLink{
            world_first.id?SavedRecoveryLinkStatus::member:SavedRecoveryLinkStatus::end,world_first};};
        h.world_next=[this](WorldEntityReference ref,std::string& error){
            world_next_queries.push_back(ref);const auto found=world_links.find(ref);
            if(found==world_links.end()){error="removed world current has no authenticated next";return SavedRecoveryLink{};}
            return SavedRecoveryLink{found->second.id?SavedRecoveryLinkStatus::member:SavedRecoveryLinkStatus::end,found->second};
        };
        h.group_head=[this](WorldEntityReference ref,Phase,std::string& error){
            const auto found=parent.find(ref);if(found==parent.end()){error="head ownership unavailable";return SavedRecoveryLink{};}
            return SavedRecoveryLink{SavedRecoveryLinkStatus::member,found->second.id?found->second:ref};
        };
        h.group_next=[this](WorldEntityReference ref,Phase,std::string& error){
            const auto found=next.find(ref);if(found==next.end()){error="removed cursor next is not authenticated";return SavedRecoveryLink{};}
            return SavedRecoveryLink{found->second.id?SavedRecoveryLinkStatus::member:SavedRecoveryLinkStatus::end,found->second};
        };
        h.preflight=[this](const auto& descriptors,std::string&){
            require(events.empty(),"preflight precedes effects");all_preflight=descriptors.size()==cohort.size();return true;
        };
        h.supports=[this](const SavedRecoveryCall& c,std::string& error){
            require(all_preflight && events.empty(),"capability checks precede ALL effects");
            if(unsupported==c.phase){error="fixture missing capability";return false;}return true;
        };
        h.execute=[this](const SavedRecoveryCall& c,std::string& error){
            require(all_preflight,"effects require cohort preflight");events.emplace_back(c.phase,c.entity.id);
            if(c.phase==Phase::capture_placement)
                for(const auto& e:cohort)if(world.is_live_reference(e.reference))positions[e.reference.id]=world.find(e.reference.id)->position;
            if(c.phase==Phase::detach) {
                require(world.place_out_of_world(c.entity.id,3),"detach existing identity");
                require(world.declare_object_word(c.entity.id,41,901),"detach callback retained value mutation");
            }
            if(c.phase==Phase::portal_detach)portal_detached=true;
            if(c.phase==Phase::restore_placement && positions[c.entity.id])
                require(world.place_on_floor(c.entity.id,*positions[c.entity.id]),"restore existing identity");
            if(c.phase==Phase::portal_restore || c.phase==Phase::portal_reconcile)portal_detached=false;
            if(c.phase==Phase::person_unwind && cascade) {
                // Exercise the coordinator's nested object recovery, including
                // recursive notification back to this already-resetting person.
                if(!c.reset_referenced(cohort.front().reference,error))return false;
            }
            if(fail==c.phase){error="fixture callback failed after effects";return false;}
            return true;
        };
        return h;
    }
};
void object_order_and_state() {
    Fixture f;f.add(1);const auto identity=f.cohort[0].reference;const auto saved=f.world.find(1)->saved_object_banks;
    const auto result=recover_original_saved_objects(f.world,f.cohort,f.host());
    require(result.completed && result.prepared && !result.discard_candidate,"object recovery complete");
    const std::vector<std::pair<Phase,std::int16_t>> expected{
        {Phase::load,1},{Phase::capture_placement,1},{Phase::detach,1},
        {Phase::before_clear,1},{Phase::restart_main,1},{Phase::init,1},{Phase::restore_placement,1},
        {Phase::placement_entry,1},{Phase::init,1},{Phase::refresh,1},{Phase::post_load_lookup_flags,1}};
    require(f.events==expected,"native ordinary recovery order including two Init calls");
    const auto* e=f.world.resolve_reference(identity);
    require(e && e->attributes==std::vector<std::int16_t>({0,0}) && e->slots.size()==3,"identity, cleared attrs and structural slots");
    VmFrame frame{};frame.context={1,1,0,0};require(f.world.bind_context_live(frame.context),"bind exact live identity");
    require(f.world.read(frame,3,11)==1 && f.world.read(frame,3,67)==3 &&
        f.world.read(frame,3,2)==0 && f.world.read(frame,3,3)==-1,"actual World derived identity slot and containment getters");
    require(e->object_words.at(41)==901 && e->object_words.at(21)==121 && e->object_words.at(24)==124 &&
        e->object_words.at(29)==129 && e->object_words.at(45)==145 && e->object_words.at(46)==146 &&
        e->object_words.at(47)==147 && e->object_words.at(58)==23 && e->object_words.at(12)==0,"retain AFTER detach, directed reset and version");
    require(e->position==std::optional<EntityPosition>(EntityPosition{16,32,1,3}) &&
        e->saved_object_banks->object_data==saved->object_data,"placement and immutable original bank preserved");
    for(std::size_t i=0;i<8;++i)require(f.threads[0]->temps()[i]==0,"eight native temps cleared");
    require(f.threads[0]->temps()[8]==44 && f.threads[0]->temp_xl()[0]==99,"extended bank not invented as native words");
}
void cleanup_and_identity() {
    {
        Fixture f;f.add(1).has_cleanup=true;
        const auto before=*f.world.find(1);
        const auto result=recover_original_saved_objects(f.world,f.cohort,f.host());
        const std::vector<std::pair<Phase,std::int16_t>> expected{
            {Phase::load,1},{Phase::cleanup,1},{Phase::cleanup_flags,1},{Phase::restart_main,1},{Phase::init,1},{Phase::refresh,1},
            {Phase::post_load_lookup_flags,1}};
        require(result.completed && f.events==expected && f.threads[0]->temps()[0]==44 &&
            f.world.find(1)->attributes==before.attributes && f.world.find(1)->object_words.at(12)==112,
            "EP3 path retains TEMP and mutable bank rather than taking fallback clear");
    }
    {
        Fixture f;f.add(1);auto host=f.host();const auto execute=host.execute;const auto original=f.cohort[0].reference;
        host.execute=[&](const SavedRecoveryCall& call,std::string& error){
            if(!execute(call,error))return false;
            if(call.phase==Phase::detach){
                require(f.world.remove(1) && f.world.create(1,EntityKind::object,{1}),"callback deletes original and reuses numeric ID");
                require(f.world.define_type(1,0x87654321) && f.world.declare_object_word(1,41,999),"replacement identity marker");
            }
            return true;
        };
        const auto result=recover_original_saved_objects(f.world,f.cohort,host);
        require(!result.completed && result.discard_candidate && f.world.find(1)->generation!=original.generation &&
            f.world.find(1)->object_words.at(41)==999 && f.events.back().first==Phase::detach,
            "unsupported hard deletion is incomplete and ID reuse cannot mutate replacement");
    }
}
void native_removal_entry_ownership() {
    // The host detach consumer models4331d4's own MasterID-gated EP10.
    // The coordinator contributes the separate422db9 pass only for native MT
    // classes, including a cXPortal with a single physical member.
    for(const auto kind:{SavedRecoveryClass::object,SavedRecoveryClass::portal})
        for(const bool native_group:{false,true})for(const bool cleanup:{false,true})
            for(const std::int16_t master:{std::int16_t{0},std::int16_t{7}}){
                if(kind==SavedRecoveryClass::portal&&!native_group)continue;
                Fixture f;f.add(1,kind);f.cohort[0].native_group=native_group;f.cohort[0].has_cleanup=cleanup;
                require(f.world.declare_definition_word(1,10,master),"actual OBJD MasterID word available to removal consumer");
                auto host=f.host();const auto execute=host.execute;unsigned entry10=0,detaches=0,explicit_pass=0;
                host.execute=[&](const SavedRecoveryCall& call,std::string& error){
                    if(call.phase==Phase::remove_entry){++explicit_pass;++entry10;}
                    if(call.phase==Phase::detach){
                        ++detaches;VmFrame address;
                        // Scope21 addresses StackObject's definition. This is
                        // a read address, not the EP10 execution context.
                        address.context={call.entity.id,call.entity.id,call.entity.id};
                        require(f.world.bind_context_live(address.context),"native removal resolves its actual receiver");
                        const auto source_master=f.world.read(address,21,10);
                        require(source_master.has_value(),"native removal requires its actual definition MasterID");
                        if(*source_master==0)++entry10;
                    }
                    return execute(call,error);
                };
                const auto result=recover_original_saved_objects(f.world,f.cohort,host);
                require(result.completed,"source-shaped removal entry ownership completes");
                require(detaches==unsigned(!cleanup)&&explicit_pass==unsigned(!cleanup&&native_group)&&
                    entry10==(cleanup?0U:unsigned(native_group)+unsigned(master==0)),
                    "ordinary removal owns one conditional EP10; native MT adds its unconditional pass; Cleanup never removes");
            }
}
void groups_and_portals() {
    Fixture f;f.add(1,SavedRecoveryClass::portal);f.add(2,SavedRecoveryClass::portal,false);
    f.cohort[0].native_group=f.cohort[1].native_group=true;
    const std::vector<WorldEntityReference> pieces{f.cohort[0].reference,f.cohort[1].reference};
    f.cohort[0].pieces=f.cohort[1].pieces=pieces;
    const auto result=recover_original_saved_objects(f.world,f.cohort,f.host());
    auto index=[&](Phase phase,std::int16_t id){for(std::size_t i=0;i<f.events.size();++i)if(f.events[i]==std::make_pair(phase,id))return i;return f.events.size();};
    require(result.completed && !f.portal_detached &&
        index(Phase::portal_reconcile,1)<index(Phase::post_load_lookup_flags,1)&&
        index(Phase::post_load_lookup_flags,1)<index(Phase::load,2),
        "portal finally reconciliation precedes post-load lookup publication and next entity Load");
    require(index(Phase::portal_detach,1)<index(Phase::remove_entry,1) &&
        index(Phase::remove_entry,2)<index(Phase::detach,1) && index(Phase::detach,2)<index(Phase::before_clear,1),"all EP10 precede every detach and every clear");
    require(index(Phase::restart_main,2)<index(Phase::restore_group,1) &&
        index(Phase::restore_group,1)<index(Phase::init,1) && index(Phase::init,2)<index(Phase::restore_placement,1) &&
        index(Phase::restore_placement,2)<index(Phase::placement_entry,1) && index(Phase::placement_entry,2)<index(Phase::portal_restore,1),"group reset offsets Init placement and portal order");
}
void portal_finally_failure() {
    for(const bool deletion:{false,true}) {
        Fixture f;f.add(1,SavedRecoveryClass::portal);f.add(2,SavedRecoveryClass::portal,false);
        f.cohort[0].native_group=f.cohort[1].native_group=true;
        f.cohort[0].pieces=f.cohort[1].pieces={f.cohort[0].reference,f.cohort[1].reference};
        auto host=f.host();
        if(deletion) {
            const auto execute=host.execute;
            host.execute=[&,execute](const SavedRecoveryCall& call,std::string& error){
                if(!execute(call,error))return false;
                if(call.phase==Phase::remove_entry && call.entity.id==1)require(f.world.mark_dead(f.cohort[0].reference),"portal callback marks deferred-dead target");
                return true;
            };
        } else f.fail=Phase::init;
        const auto result=recover_original_saved_objects(f.world,f.cohort,host);
        bool reconciled=false;for(const auto& e:f.events)reconciled|=e.first==Phase::portal_reconcile;
        require(reconciled && !f.portal_detached,"portal finally reconciles after deletion or failure even with missing target");
        require(deletion?result.completed:(!result.completed && result.discard_candidate),"reconciliation never launders callback failure");
    }
}
void live_group_mutation() {
    const auto ids=[](const Fixture& f,Phase phase){std::vector<std::int16_t> answer;
        for(const auto& event:f.events)if(event.first==phase)answer.push_back(event.second);return answer;};
    for(const unsigned mutation:{0U,1U,2U,3U,4U,5U}) {
        Fixture f;f.add(1);f.add(2,SavedRecoveryClass::object,false);f.add(3,SavedRecoveryClass::object,false);
        f.cohort[0].native_group=f.cohort[1].native_group=true;
        const auto one=f.cohort[0].reference,two=f.cohort[1].reference,three=f.cohort[2].reference;
        f.cohort[0].pieces=f.cohort[1].pieces=mutation==1?std::vector<WorldEntityReference>{one,two}:
            std::vector<WorldEntityReference>{one,two,three};
        if(mutation!=1){f.cohort[2].native_group=true;f.cohort[2].pieces=f.cohort[0].pieces;}
        auto host=f.host();const auto execute=host.execute;
        host.execute=[&,execute](const SavedRecoveryCall& call,std::string& error){
            if(!execute(call,error))return false;
            if(call.phase==Phase::remove_entry) {
                if(mutation==0 && call.entity==one){
                    // Remove a future node before NEXT is read at current one.
                    f.next[one]=three;f.parent[two]={};f.next[two]={};
                } else if(mutation==1 && call.entity==two){
                    // Append an already preflighted singleton during EP10.
                    f.next[two]=three;f.parent[three]=one;f.next[three]={};
                } else if(mutation==2 && call.entity==two){
                    // Native unlink clears CURRENT.next. EP10 ends here;
                    // the cached-head detach pass then follows one->three.
                    f.next[one]=three;f.parent[two]={};f.next[two]={};
                } else if(mutation==3 && call.entity==two){
                    // If immediate deletion loses current links, this adapter
                    // has no authority to claim END and must decline recovery.
                    require(f.world.remove(two.id),"remove current piece");f.next.erase(two);
                } else if(mutation==4 && call.entity==one){
                    // Newly created/unprepared identity is never substituted
                    // or admitted merely because it is reachable from a group.
                    f.next[one]={99,999};
                } else if(mutation==5 && call.entity==two){
                    // A different deferred-dead piece cannot abort all passes.
                    require(f.world.mark_dead(three),"mark another piece deferred dead");
                    // World-list removal and group links are separately owned;
                    // retain the group tombstone, omit a later world visit.
                    f.world_links[two]={};
                }
            }
            return true;
        };
        const auto result=recover_original_saved_objects(f.world,f.cohort,host);
        if(mutation==3 || mutation==4) {
            require(!result.completed && result.prepared && result.discard_candidate && !result.diagnostic.empty(),
                "unknown removed-current or new-identity traversal is explicit incomplete");continue;
        }
        require(result.completed,"authenticated mutable group completes");
        if(mutation==0) {
            require(ids(f,Phase::remove_entry)==std::vector<std::int16_t>({1,3}) &&
                ids(f,Phase::detach)==std::vector<std::int16_t>({1,3}) && f.world.find(2)->attributes[0]==9,
                "future unlink is observed after callback; unrelated surviving piece is untouched");
        } else if(mutation==1) {
            require(ids(f,Phase::remove_entry)==std::vector<std::int16_t>({1,2,3}) &&
                ids(f,Phase::detach)==std::vector<std::int16_t>({1,2,3}) && f.world.find(3)->attributes[0]==0,
                "same-cohort append joins all later native passes without frozen membership");
        } else if(mutation==2) {
            require(ids(f,Phase::remove_entry)==std::vector<std::int16_t>({1,2}) &&
                ids(f,Phase::detach)==std::vector<std::int16_t>({1,3}) && f.world.find(2)->attributes[0]==9,
                "current unlink ends first pass at new NEXT, second pass restarts cached head");
        } else {
            require(ids(f,Phase::detach)==std::vector<std::int16_t>({1,2,3}) &&
                ids(f,Phase::before_clear)==std::vector<std::int16_t>({1,2,3}) &&
                ids(f,Phase::placement_entry)==std::vector<std::int16_t>({1,2,3}),
                "another deferred-dead node never triggers whole-group abandonment");
        }
    }
    {
        Fixture f;f.add(1).native_group=true;auto host=f.host();host.group_next={};
        const auto result=recover_original_saved_objects(f.world,f.cohort,host);
        require(!result.prepared && !result.completed && f.events.empty(),"missing traversal authority rejects before Load");
    }
}
void live_world_mutation() {
    for(const unsigned mutation:{0U,1U,2U,3U,4U,5U}) {
        Fixture f;f.add(30).has_cleanup=true;f.add(10).has_cleanup=true;f.add(20).has_cleanup=true;
        const auto first=f.cohort[0].reference,second=f.cohort[1].reference,third=f.cohort[2].reference;
        auto host=f.host();const auto execute=host.execute;
        host.execute=[&,execute](const SavedRecoveryCall& call,std::string& error){
            if(call.phase==Phase::load && call.deferred_dead) {
                require(call.entity==second,"deferred-dead Load retains exact future-node identity");
                error="fixture has no proved deferred-dead Load/class adapter";return false;
            }
            if(!execute(call,error))return false;
            if(call.phase==Phase::load && call.entity==first) {
                if(mutation==0) {
                    // Delete a future entity and splice its link out now.
                    require(f.world.remove(second.id),"delete future world node");f.world_links[first]=third;
                } else if(mutation==1) {
                    // Artificial host cursor mutation, not native unlink:
                    // raw44387c preserves removed.next. This only exercises
                    // the coordinator's obligation to use the supplied NEXT.
                    f.world_first=second;f.world_links[first]={};
                } else if(mutation==2) {
                    // Ordinary creation prepends before the saved pass head.
                    require(f.world.create(99,EntityKind::object,{1}) && f.world.define_type(99,0x87654321),"create prepended source node");
                    const WorldEntityReference fresh{99,f.world.find(99)->generation};
                    f.world_first=fresh;f.world_links[fresh]=first;
                } else if(mutation==3) {
                    require(f.world.remove(first.id),"delete current without link proof");f.world_links.erase(first);
                } else if(mutation==4) f.world_links[first]={99,999};
                else require(f.world.mark_dead(second),"callback marks future linked node deferred dead");
            }
            return true;
        };
        const auto result=recover_original_saved_objects(f.world,f.cohort,host);
        std::vector<std::int16_t> loads;for(const auto& e:f.events)if(e.first==Phase::load)loads.push_back(e.second);
        require(f.world_head_queries==1,"native world head is acquired once for the entire post-load pass");
        if(mutation>=3) {
            require(!result.completed && result.discard_candidate && !result.diagnostic.empty(),"unavailable removed current or unprepared world next explicitly fails");
        } else {
            require(result.completed,"authenticated native world mutation completes");
            const auto expected=mutation==0?std::vector<std::int16_t>{30,20}:mutation==1?
                std::vector<std::int16_t>{30}:std::vector<std::int16_t>{30,10,20};
            require(loads==expected,"world pass uses native source links rather than ID sort or frozen cohort");
            require(f.world_next_queries.size()==expected.size(),"NEXT follows each full entity exactly, including unlinked current");
        }
    }
    {
        Fixture f;f.add(1);auto host=f.host();host.world_next={};
        require(!recover_original_saved_objects(f.world,f.cohort,host).prepared && f.events.empty(),"missing world traversal rejects before any callback");
    }
}
void retained_compatible_postload() {
    Fixture f;const auto first=f.add(1,SavedRecoveryClass::object,false).reference;
    f.add(2,SavedRecoveryClass::object,false);auto host=f.host();const auto execute=host.execute;
    bool retained_lookup=false;
    host.execute=[&,execute](const SavedRecoveryCall& call,std::string& error){
        if(!execute(call,error))return false;
        if(call.phase==Phase::load&&call.entity==first)
            return f.world.remove_retaining_body(first);
        if(call.phase==Phase::post_load_lookup_flags&&call.entity==first)
            retained_lookup=call.deferred_dead&&f.world.resolve_reference(first);
        return true;
    };
    const auto result=recover_original_saved_objects(f.world,f.cohort,host);
    require(result.completed&&retained_lookup&&f.world_next_queries.size()==2,
        "compatible object removed in Load still publishes native lookup flags from its exact retained body before NEXT");
}
void live_person_notifications() {
    for(unsigned mutation=0;mutation<5;++mutation) {
        Fixture f;f.people.reserve(16);
        const auto target=f.add(1).reference;
        const auto a=f.add(2,SavedRecoveryClass::person,false).reference;
        const auto b=f.add(3,SavedRecoveryClass::person,false).reference;
        const auto c=f.add(4,SavedRecoveryClass::person,false).reference;
        f.people={b,a,c};
        if(mutation==2)f.people.pop_back();
        auto host=f.host();const auto execute=host.execute;
        host.execute=[&,execute](const SavedRecoveryCall& call,std::string& error){
            if(!execute(call,error))return false;
            if(call.phase==Phase::notify_person && call.target==target && call.entity==b){
                if(mutation==1)f.people.erase(f.people.begin());
                if(mutation==2)f.people.push_back(c);
                if(mutation==3)f.people.reserve(f.people.capacity()+1);
                if(mutation==4)require(f.world.mark_dead(target),"notifier may defer-delete reset target");
            }
            return true;
        };
        const auto result=recover_original_saved_objects(f.world,f.cohort,host);
        std::vector<int> notified;
        for(const auto& event:f.events)if(event.first==Phase::notify_person)notified.push_back(event.second);
        if(mutation==3)require(!result.completed&&result.discard_candidate&&notified==std::vector<int>{3},
            "person vector reallocation rejects after effects rather than inventing a valid cursor");
        else if(mutation==4)require(!result.completed&&result.discard_candidate&&notified==std::vector<int>{3,2,4},
            "target death does not suppress remaining notifications; unproved reset path then rejects explicitly");
        else {
            require(result.completed,"live person registration traversal completes");
            require(notified==(mutation==1?std::vector<int>{3,4}:std::vector<int>{3,2,4}),
                "native person order, shifted-slot skip, stable append, and deferred-dead target are preserved");
        }
    }
    Fixture f;f.add(1);f.add(2,SavedRecoveryClass::person,false);auto host=f.host();host.begin_person_notifications={};
    require(!recover_original_saved_objects(f.world,f.cohort,host).prepared&&f.events.empty(),
        "missing actual person traversal rejects before Load effects");
    {
        Fixture initially_empty;initially_empty.add(1);auto actual=initially_empty.host();const auto execute=actual.execute;
        actual.execute=[&,execute](const SavedRecoveryCall& call,std::string& error){
            if(!execute(call,error))return false;
            if(call.phase==Phase::load){
                require(initially_empty.world.create(8,EntityKind::avatar,{0,101,0,16}),"Load creates the first person");
                initially_empty.people.push_back({8,initially_empty.world.find(8)->generation});
            }
            return true;
        };
        const auto result=recover_original_saved_objects(initially_empty.world,initially_empty.cohort,actual);
        require(!result.completed&&result.discard_candidate&&result.diagnostic.find("unprepared")!=std::string::npos,
            "an initially empty person vector is still consulted after Load creates an unprepared person");
    }
}
void compact_group_admission() {
    constexpr std::int16_t count=310;
    const std::vector<Phase> phases{Phase::remove_entry,Phase::detach,Phase::before_clear,Phase::restart_main,
        Phase::restore_group,Phase::init,Phase::restore_placement,Phase::placement_entry};
    // The old stored cross product had 8*310*310 entries (>262144).
    for(const bool late_failure:{false,true}) {
        Fixture f;for(std::int16_t id=1;id<=count;++id)f.add(id).native_group=true;
        auto host=f.host();const auto supports=host.supports;
        std::map<std::tuple<Phase,std::int16_t,std::int16_t>,unsigned> probes;
        host.supports=[&](const SavedRecoveryCall& call,std::string& error){
            if(!supports(call,error))return false;
            if(std::find(phases.begin(),phases.end(),call.phase)!=phases.end())
                ++probes[{call.phase,call.entity.id,call.target.id}];
            if(late_failure&&call.phase==Phase::placement_entry&&call.entity.id==count&&call.target.id==count){
                error="last receiver/target capability missing";return false;
            }
            return true;
        };
        const auto result=recover_original_saved_objects(f.world,f.cohort,host);
        if(late_failure) {
            require(!result.prepared&&!result.completed&&!result.discard_candidate&&result.operations==0&&f.events.empty()&&f.world_head_queries==0,
                "last expanded admission probe rejects the whole cohort before any Load or cursor effects");
        } else require(result.prepared&&result.completed&&result.operations<262144,
            "310 native singleton groups fit default compact planning and execute actual live traversals");
        for(const auto phase:phases)for(std::int16_t target=1;target<=count;++target)for(std::int16_t entity=1;entity<=count;++entity)
            require(probes[{phase,entity,target}]==(phase==Phase::init&&entity==target&&(!late_failure||target<count)?2U:1U),
                "every receiver/target/phase is admitted independently; final Init remains separately checked");
    }
    for(const bool execution_limit:{false,true}) {
        Fixture f;f.add(1).native_group=true;SavedRecoveryLimits limits;
        if(execution_limit)limits.executed_operations=2;else limits.planned_operations=2;
        const auto result=recover_original_saved_objects(f.world,f.cohort,f.host(),true,limits);
        require(!result.completed&&(execution_limit?(result.prepared&&result.discard_candidate&&result.operations==2):(!result.prepared&&f.events.empty())),
            "compact templates preserve both planning and actual execution limits");
    }
}
void failures_and_person() {
    {
        Fixture f;f.add(1);SavedRecoveryLimits limits;limits.planned_operations=2;
        require(!recover_original_saved_objects(f.world,f.cohort,f.host(),true,limits).completed && f.events.empty(),
            "bounded plan rejects before effects");
    }
    require(saved_person_notification_requires_reset({7,3},{{7,3}},{}) &&
        saved_person_notification_requires_reset({7,3},{},{7,3}) &&
        !saved_person_notification_requires_reset({7,4},{{7,3}},{7,3}) &&
        !saved_person_notification_requires_reset({},{{7,3}},{7,3}),
        "native active uses or raw active icon selection, exact generation and no nil target");
    {
        Fixture f;f.add(1);f.add(2);f.unsupported=Phase::restore_placement;
        const auto result=recover_original_saved_objects(f.world,f.cohort,f.host());
        require(!result.completed && !result.prepared && !result.discard_candidate && f.events.empty(),"unsupported late phase rejects WHOLE cohort without Load");
    }
    {
        Fixture f;f.add(1);f.fail=Phase::init;
        const auto result=recover_original_saved_objects(f.world,f.cohort,f.host());
        require(!result.completed && result.prepared && result.discard_candidate && !f.world.find(1)->position,"failure explicit, effects retained, no false rollback");
    }
    {
        Fixture f;f.add(1,SavedRecoveryClass::object,false);f.add(2,SavedRecoveryClass::person);
        f.cascade=true;
        const auto before=*f.world.find(2);
        const auto result=recover_original_saved_objects(f.world,f.cohort,f.host());
        require(result.completed,"nested referenced-object recovery completes");
        const auto* after=f.world.find(2);
        require(after->motives==before.motives && after->person_data[1]==77 && after->person_data[0]==0 &&
            after->person_data[40]==0 && after->person_data[59]==0 && after->object_words.at(34)==0 &&
            after->object_words.at(9)==0 && f.threads[1]->temps()[0]==44,"person directed fields preserve motives personality TEMP");
        unsigned loads=0,notifies=0;for(const auto& event:f.events){loads+=event.first==Phase::load;notifies+=event.first==Phase::notify_person;}
        require(loads==1 && notifies==1,"no generic person Load, nested object reset never replays Load");
    }
    {
        Fixture f;f.add(1,SavedRecoveryClass::special_cursor,false);
        require(recover_original_saved_objects(f.world,f.cohort,f.host(),false).completed,"exact special GUID recovers even with global control off");
        unsigned init=0;bool shared=false,special=false;
        for(const auto& event:f.events){special|=event.first==Phase::special_cursor;init+=event.first==Phase::init;shared|=event.first==Phase::capture_placement;}
        require(!special&&shared&&init==1,"always-reset ordinary class runs its own Init but no changed-code final Init with control off");
        f.cohort[0].kind=SavedRecoveryClass::object;f.events.clear();
        require(!recover_original_saved_objects(f.world,f.cohort,f.host()).completed && f.events.empty(),"exact always-reset GUID cannot lose its independent selector marker");
    }
}
void always_reset_class_matrix(){
    for(const bool selected:{false,true})for(const bool cleanup:{false,true})
    for(const bool changed:{false,true})for(const bool enabled:{false,true}){
        Fixture f;f.add(1,selected?SavedRecoveryClass::special_cursor:SavedRecoveryClass::object,changed);
        f.cohort.front().has_cleanup=cleanup;f.add(2,SavedRecoveryClass::person,false);
        const auto ref=f.cohort.front().reference;const auto before=*f.world.find(1);
        const auto result=recover_original_saved_objects(f.world,f.cohort,f.host(),enabled);
        require(result.completed,"ordinary/always-reset, cleanup, changed and control matrix completes");
        const bool resets=selected||(changed&&enabled);
        unsigned loads=0,notifies=0,inits=0,restarts=0,captures=0,cleanups=0;bool obsolete=false;
        std::size_t notify_index=f.events.size(),reset_index=f.events.size();
        for(std::size_t i=0;i<f.events.size();++i){const auto& e=f.events[i];
            if(e.first==Phase::notify_person){++notifies;notify_index=i;}
            if(e.second!=1)continue;
            loads+=e.first==Phase::load;inits+=e.first==Phase::init;restarts+=e.first==Phase::restart_main;
            captures+=e.first==Phase::capture_placement;cleanups+=e.first==Phase::cleanup;obsolete|=e.first==Phase::special_cursor;
            if(e.first==Phase::capture_placement||e.first==Phase::cleanup)reset_index=std::min(reset_index,i);
        }
        require(loads==1&&!obsolete&&notifies==unsigned(resets)&&restarts==unsigned(resets)&&
            captures==unsigned(resets&&!cleanup)&&cleanups==unsigned(resets&&cleanup)&&
            inits==unsigned(resets&&!cleanup)+unsigned(changed&&enabled),
            "exact selector changes reset selection, never native class callbacks or extra final Init");
        require(!resets||notify_index<reset_index,"compatible person notification precedes actual ordinary reset callbacks");
        require(f.world.is_live_reference(ref)&&f.world.find(1)->position==before.position,
            "always-reset service keeps the actual original identity and placement");
        if(resets&&!cleanup)require(f.world.find(1)->object_words.at(41)==901&&f.world.find(1)->attributes[0]==0&&
            f.threads.front()->temps()[0]==0,"real ordinary reset clears banks after detach callbacks and retains their write");
        else require(f.threads.front()->temps()[0]==44,"cleanup or compatible paths never invent temporary clearing");
    }
    {
        Fixture f;f.add(1,SavedRecoveryClass::special_cursor,false);f.unsupported=Phase::restore_placement;
        const auto result=recover_original_saved_objects(f.world,f.cohort,f.host(),false);
        require(!result.prepared&&!result.completed&&!result.discard_candidate&&f.events.empty(),
            "missing shared always-reset placement capability rejects entire cohort before Load");
    }
    {
        Fixture f;f.add(1,SavedRecoveryClass::special_cursor,false);const auto original=f.cohort.front().reference;
        auto host=f.host();const auto execute=host.execute;bool replaced=false;
        host.execute=[&](const SavedRecoveryCall& call,std::string& error){
            if(!execute(call,error))return false;
            if(call.phase==Phase::detach&&!replaced){
                replaced=true;require(f.world.remove_retaining_body(original)&&f.world.create(1,EntityKind::object,{1,0,0})&&
                    f.world.define_type(1,77),"callback retains exact original and reuses numeric ID for independent object");
                VmFrame write{};write.context={1,1,1,0};require(f.world.bind_context_live(write.context)&&
                    f.world.write(write,0,0,99),"replacement has independent observable state");
            }
            return true;
        };
        const auto result=recover_original_saved_objects(f.world,f.cohort,host,false);
        require(result.completed&&replaced&&f.world.find(1)->generation!=original.generation&&
            f.world.find(1)->attributes[0]==99&&f.world.find(1)->definition->guid==77,
            "always-reset callbacks never clear, reinit or reposition an ID-reused replacement");
    }
}

}
int main(){try{object_order_and_state();cleanup_and_identity();native_removal_entry_ownership();groups_and_portals();portal_finally_failure();live_group_mutation();live_world_mutation();retained_compatible_postload();live_person_notifications();compact_group_admission();failures_and_person();always_reset_class_matrix();std::cout<<"saved recovery coordinator passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
