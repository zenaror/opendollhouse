#include "odh/world.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace { int allocation_countdown=-1; }
void* operator new(std::size_t size) {
    if(allocation_countdown==0) { allocation_countdown=-1; throw std::bad_alloc(); }
    if(allocation_countdown>0) --allocation_countdown;
    if(auto* p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
namespace {
int checks=0;
void require(bool value,const char* message) {
    ++checks;
    if(!value) throw std::runtime_error(message);
}
bool same(const odh::Entity& a,const odh::Entity& b) {
    if(a.id!=b.id || a.kind!=b.kind || a.attributes!=b.attributes ||
       a.person_data!=b.person_data || a.slots!=b.slots || a.object_words!=b.object_words ||
       !(a.position==b.position) || a.generation!=b.generation || a.motives!=b.motives ||
       a.definition_words!=b.definition_words || a.unattached_slot!=b.unattached_slot ||
       a.static_footprint!=b.static_footprint || a.ui_ghost!=b.ui_ghost ||
       a.kill_timeout!=b.kill_timeout || a.radian_direction!=b.radian_direction ||
       bool(a.attachment)!=bool(b.attachment) || bool(a.definition)!=bool(b.definition) ||
       bool(a.entry_points)!=bool(b.entry_points) ||
       bool(a.saved_object_banks)!=bool(b.saved_object_banks)) return false;
    if(a.attachment && (a.attachment->container!=b.attachment->container ||
       a.attachment->slot!=b.attachment->slot || a.attachment->positioned!=b.attachment->positioned)) return false;
    if(a.definition && (a.definition->guid!=b.definition->guid ||
       a.definition->master_guid!=b.definition->master_guid)) return false;
    if(a.entry_points) {
        if(a.entry_points->size()!=b.entry_points->size()) return false;
        for(std::size_t i=0;i<a.entry_points->size();++i)
            if((*a.entry_points)[i].condition!=(*b.entry_points)[i].condition ||
               (*a.entry_points)[i].action!=(*b.entry_points)[i].action) return false;
    }
    return !a.saved_object_banks || (a.saved_object_banks->object_data==b.saved_object_banks->object_data &&
                                   a.saved_object_banks->extras==b.saved_object_banks->extras);
}
bool same(const odh::WorldSnapshot& a,const odh::WorldSnapshot& b) {
    if(a.word_count!=b.word_count || a.generation_sequence!=b.generation_sequence ||
       a.spatial_revision!=b.spatial_revision || a.attribute_policy!=b.attribute_policy ||
       a.entities.size()!=b.entities.size()) return false;
    for(std::size_t i=0;i<a.entities.size();++i) if(!same(a.entities[i],b.entities[i])) return false;
    return true;
}
}
int main() {
    try {
        odh::World source({},odh::AttributePolicy::ts1_bounded);
        std::array<std::int16_t,68> raw{}; raw[1]=7; raw[3]=19;
        std::array<std::int16_t,5> extras{-1,2,-3,4,-5};
        require(source.create(9,odh::EntityKind::object) && source.remove(9),"generation history gap");
        require(source.create_saved_entity(1,odh::EntityKind::avatar,123,{1,-2},{3,-4},{-300,200},
                raw,extras,1,456) && source.create(2,odh::EntityKind::object,{2,0,1}) &&
                source.create(3,odh::EntityKind::object,{1}),"fixture entities");
        require(source.define_type(2,789) && source.define_entry_points(1,{{12,65535},{0,4}}) &&
                source.define_entry_points(2,{}) && source.declare_definition_word(1,255,-7) &&
                source.declare_object_word(3,78,-8),"definitions, absent and empty entry tables");
        odh::VmFrame frame{odh::routine_key(4096),0,{1,1,1},{},{}};
        require(source.write(frame,0,4,-55) && source.write(frame,3,0,999) &&
                source.set_person_data(1,0,91) && source.set_motive(1,0,-25) &&
                source.set_avatar_kill_timeout(1,72),"mutable banks differ from original baseline");
        require(source.set_position(1,{16,32,2,7}) && source.set_position(2,{48,64,1,3}) &&
                source.place_in_slot(1,0,2) && source.attach(2,0,3) &&
                source.set_radian_direction(1,-0.3f) && source.set_static_footprint(1,false) &&
                source.set_ui_ghost(3,true),"positioned and unpositioned containment and flags");
        auto snapshot=source.snapshot();
        odh::World restored;
        require(restored.restore_snapshot(snapshot) && same(snapshot,restored.snapshot()),"all fields round trip exactly");
        require(restored.read(frame,0,10)==0 && restored.create(4,odh::EntityKind::object) &&
                restored.find(4)->generation==snapshot.generation_sequence+1,"policy and next generation restored");
        require(restored.place_on_floor(2,{80,96,1,2}) && restored.find(3)->attachment &&
                !restored.find(2)->attachment,"restored containment remains operational");
        snapshot.entities[0].attributes[0]=321;
        require(source.find(1)->attributes[0]==1 && restored.find(1)->attributes[0]==1,"owned independent snapshots");
        snapshot=source.snapshot();
        const auto before=restored.snapshot();
        require(!restored.restore_snapshot(snapshot) && same(before,restored.snapshot()),"nonempty destination unchanged");
        odh::World published;
        require(published.restore_snapshot(source.snapshot()),"publish fixture baseline");
        auto bound_memory=published.vm_memory();
        auto candidate_snapshot=snapshot;
        const auto published_address=&published;
        require(published.publish_snapshot(candidate_snapshot) && &published==published_address &&
                same(candidate_snapshot,published.snapshot()),"candidate snapshot publishes without replacing World identity");
        require(bound_memory.read(frame,0,0)==1,
                "VM read callback bound before publication observes candidate state");
        require(bound_memory.write(frame,0,2,222) && published.find(1)->attributes[2]==222,
                "VM write callback bound before publication updates candidate state");
        const auto published_before_reject=published.snapshot();
        auto invalid_candidate=candidate_snapshot;
        ++invalid_candidate.word_count;
        require(!published.publish_snapshot(invalid_candidate) &&
                same(published_before_reject,published.snapshot()),
                "invalid candidate publication leaves populated World unchanged");
        auto stale_candidate=odh::WorldSnapshot{};
        stale_candidate.generation_sequence=published_before_reject.generation_sequence-1;
        stale_candidate.spatial_revision=published_before_reject.spatial_revision;
        require(!published.publish_snapshot(stale_candidate) &&
                same(published_before_reject,published.snapshot()),
                "older candidate generation history leaves populated World unchanged");
        stale_candidate.generation_sequence=published_before_reject.generation_sequence;
        stale_candidate.spatial_revision=published_before_reject.spatial_revision-1;
        require(!published.publish_snapshot(stale_candidate) &&
                same(published_before_reject,published.snapshot()),
                "older candidate position history leaves populated World unchanged");
        auto changed_identity=candidate_snapshot;
        changed_identity.generation_sequence=published_before_reject.generation_sequence+1;
        changed_identity.entities[0].generation=changed_identity.generation_sequence;
        require(!published.publish_snapshot(changed_identity) &&
                same(published_before_reject,published.snapshot()),
                "existing entity cannot change identity during publication");
        auto recycled_identity=candidate_snapshot;
        recycled_identity.entities.push_back(odh::Entity{});
        recycled_identity.entities.back().id=9;
        recycled_identity.entities.back().kind=odh::EntityKind::object;
        recycled_identity.entities.back().generation=1;
        recycled_identity.spatial_revision++;
        require(!published.publish_snapshot(recycled_identity) &&
                same(published_before_reject,published.snapshot()),
                "removed historical identity cannot return as a new entity");
        odh::World publish_failures;
        require(publish_failures.restore_snapshot(source.snapshot()),"publication allocation baseline");
        const auto publication_baseline=publish_failures.snapshot();
        bool publication_completed=false;
        for(int allocation=0;allocation<200;++allocation) {
            allocation_countdown=allocation;
            bool threw=false;
            try { publication_completed=publish_failures.publish_snapshot(candidate_snapshot); }
            catch(const std::bad_alloc&) { threw=true; }
            allocation_countdown=-1;
            if(publication_completed) break;
            require(threw && same(publication_baseline,publish_failures.snapshot()),
                    "allocation failure leaves published World unchanged");
        }
        require(publication_completed && same(candidate_snapshot,publish_failures.snapshot()),
                "all candidate publication allocations eventually succeed");
        odh::World reused_empty;
        require(reused_empty.create(10,odh::EntityKind::object) && reused_empty.remove(10),
                "reused empty destination has prior identities");
        const auto reused_before=reused_empty.snapshot();
        require(!reused_empty.restore_snapshot(snapshot) && same(reused_before,reused_empty.snapshot()),
                "reused empty destination cannot recycle generation or spatial history");
        const auto reject=[&](const odh::WorldSnapshot& bad) {
            odh::World empty;
            const auto initial=empty.snapshot();
            require(!empty.restore_snapshot(bad) && same(initial,empty.snapshot()),"invalid snapshot fails atomically");
        };
        auto bad=snapshot; bad.word_count++; reject(bad);
        bad=snapshot; bad.entities[1].id=1; reject(bad);
        bad=snapshot; bad.entities[1].generation=bad.entities[0].generation; reject(bad);
        bad=snapshot; bad.generation_sequence=0; reject(bad);
        bad=snapshot; bad.entities[0].generation=0; reject(bad);
        bad=snapshot; bad.attribute_policy=static_cast<odh::AttributePolicy>(99); reject(bad);
        bad=snapshot; bad.entities[0].radian_direction=std::numeric_limits<float>::infinity(); reject(bad);
        bad=snapshot; bad.entities[0].object_words[11]=1; ++bad.word_count; reject(bad);
        bad=snapshot; bad.entities[0].position->direction=8; reject(bad);
        bad=snapshot; bad.entities[1].attachment->container=99; reject(bad);
        bad=snapshot; bad.entities[1].attachment->slot=99; reject(bad);
        bad=snapshot; bad.entities[0].slots[0]=0; reject(bad);
        bad=snapshot; bad.entities[1].position->x++; reject(bad);
        bad=snapshot; bad.entities[2].slots.push_back(1); ++bad.word_count;
        bad.entities[0].attachment=odh::Attachment{3,0,false}; reject(bad);
        bad=snapshot; bad.entities[1].entry_points.reset(); bad.entities[1].definition.reset();
        bad.entities[1].entry_points=std::vector<odh::EntryPoint>{}; reject(bad);
        odh::World short_words({4096,snapshot.word_count-1,4096}), short_bank({4096,65536,67}),
                   short_entities({2,65536,4096});
        require(!short_words.restore_snapshot(snapshot) && !short_bank.restore_snapshot(snapshot) &&
                !short_entities.restore_snapshot(snapshot),"word, bank and entity budgets enforced");
        odh::World exact({3,snapshot.word_count,68});
        require(exact.restore_snapshot(snapshot) && exact.word_count()==snapshot.word_count,"exact budget succeeds");
        odh::World failures;
        const auto initial_failure_state=failures.snapshot();
        bool completed=false;
        for(int allocation=0;allocation<200;++allocation) {
            allocation_countdown=allocation;
            bool threw=false;
            try { completed=failures.restore_snapshot(snapshot); } catch(const std::bad_alloc&) { threw=true; }
            allocation_countdown=-1;
            if(completed) break;
            require(threw && same(initial_failure_state,failures.snapshot()),
                    "allocation failure leaves all destination state unchanged");
        }
        require(completed && same(snapshot,failures.snapshot()),"all staged allocations eventually succeed");
        odh::World empty_source;
        require(empty_source.create(1,odh::EntityKind::object) && empty_source.remove(1),"empty history fixture");
        auto empty_snapshot=empty_source.snapshot();
        empty_snapshot.generation_sequence=std::numeric_limits<std::uint64_t>::max();
        empty_snapshot.spatial_revision=std::numeric_limits<std::uint64_t>::max();
        odh::World empty_destination;
        require(empty_destination.restore_snapshot(empty_snapshot) && same(empty_snapshot,empty_destination.snapshot()) &&
                !empty_destination.create(1,odh::EntityKind::object),"empty world retains exhausted generations and saturated revision");
        odh::World permissive,permissive_restore;
        require(permissive.create(1,odh::EntityKind::object) && permissive.define_type(1,0,0) &&
                permissive.set_position(1,{0,0,1,0}) && permissive.declare_object_word(1,1,-99) &&
                permissive_restore.restore_snapshot(permissive.snapshot()) &&
                same(permissive.snapshot(),permissive_restore.snapshot()),
                "restore accepts unusual values reachable through existing declaration APIs");
        std::cout<<checks<<" World snapshot checks passed (synthetic entity state only)\n";
    } catch(const std::exception& e) {
        allocation_countdown=-1;
        std::cerr<<"World snapshot test failed: "<<e.what()<<'\n'; return 1;
    }
}
