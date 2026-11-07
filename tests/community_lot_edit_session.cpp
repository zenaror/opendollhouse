// SPDX-License-Identifier: MPL-2.0
#include "community_lot_edit_session.hpp"
#include "odh/sha256.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F operation,const char* message){
    try{operation();}catch(const std::exception&){return;}
    throw std::runtime_error(message);
}
std::vector<std::uint8_t> wire(const odh_host::CommunityLotDocument& document){
    return odh::encode_session_archive(odh_host::encode_community_lot_document(document));
}
odh_host::CommunityLotDocument fixture(){
    odh_host::CommunityLotDocument d;d.identity.neighborhood=3;d.identity.mode=4;d.identity.house=58;
    d.identity.neighborhood_sha256.fill(1);d.identity.lot_sha256.fill(2);
    auto& p=d.architecture.prepared;p.width=p.height=2;p.buildable.assign(4,1);
    for(auto& level:p.levels){level.floors.assign(4,0);level.walls.resize(4);}
    return d;
}
struct MemoryStore {
    std::optional<odh_host::CommunityLotCheckpointHead> head;
    std::vector<std::uint8_t> bytes;
    std::size_t attempts=0,writes=0;
    odh_host::CommunityLotEditPublication publish(
            const std::optional<odh_host::CommunityLotCheckpointHead>& expected,
            const odh_host::CommunityLotDocument& doc){
        ++attempts;if(expected!=head)throw std::runtime_error("checkpoint conflict");
        auto candidate=wire(doc);const auto digest=odh::sha256(candidate.data(),candidate.size());
        bytes=std::move(candidate);head=digest;++writes;
        return {digest,"private/architecture.odhl"};
    }
};
}
int main(){try{
    const auto original=fixture();const auto original_wire=wire(original);
    MemoryStore store;
    odh_host::CommunityLotEditSession edit(original,{},[&](const auto& expected,const auto& state){return store.publish(expected,state);});
    require(!edit.dirty()&&!edit.expected_head()&&!edit.save()&&store.attempts==0,
        "clean original does not create a private file or call publisher");
    auto first=original.architecture;first.prepared.levels[0].floors[0]=300;
    first.materials={{odh::ArchitectureResourceFamily::floor,300,"test-first.flr"}};
    edit.stage_architecture(first);
    require(edit.dirty()&&store.attempts==0&&wire(edit.baseline())==original_wire,
        "architecture gesture stages without publishing or changing saved baseline");
    edit.discard();
    require(!edit.dirty()&&wire(edit.current())==original_wire&&store.writes==0,
        "discard restores original exact bytes without private file");
    edit.stage_architecture(first);edit.stage(original);
    require(!edit.dirty()&&!edit.save()&&store.attempts==0,
        "returning to exact baseline is a clean no-op");
    auto forged=original;++forged.identity.house;
    rejects([&]{edit.stage(forged);},"draft cannot change authenticated lot identity");
    auto missing_material=original;missing_material.architecture.prepared.levels[0].floors[0]=300;
    rejects([&]{edit.stage(missing_material);},"unbound dynamic material cannot enter the draft");
    auto invalid=original;invalid.architecture.prepared.width=0;
    rejects([&]{edit.stage(invalid);},"invalid architecture cannot replace current draft");
    require(!edit.dirty()&&wire(edit.current())==original_wire,
        "rejected draft leaves baseline and current state unchanged");
    edit.stage_architecture(first);const auto first_wire=wire(edit.current());
    const auto saved=edit.save();
    require(saved&&!edit.dirty()&&edit.expected_head()==store.head&&store.writes==1&&
        store.bytes==first_wire&&wire(edit.baseline())==first_wire,
        "explicit save commits complete staged document and advances baseline/head");
    require(!edit.save()&&store.attempts==1,
        "second clean save leaves durable bytes and head untouched");
    auto second=first;second.prepared.levels[0].floors[1]=301;
    second.materials.push_back({odh::ArchitectureResourceFamily::floor,301,"test-second.flr"});edit.stage_architecture(second);
    const auto pending=wire(edit.current());const auto expected=edit.expected_head();
    auto external=original;external.architecture.prepared.levels[1].floors[3]=302;
    external.architecture.materials={{odh::ArchitectureResourceFamily::floor,302,"test-external.flr"}};
    (void)store.publish(store.head,external);const auto external_wire=store.bytes;
    rejects([&]{(void)edit.save();},"stale session cannot overwrite another writer");
    require(edit.dirty()&&wire(edit.current())==pending&&wire(edit.baseline())==first_wire&&
        edit.expected_head()==expected&&store.bytes==external_wire&&store.writes==2,
        "save conflict preserves draft and prior baseline without rebasing or overwrite");
    edit.discard();
    require(!edit.dirty()&&wire(edit.current())==first_wire&&store.bytes==external_wire&&
        edit.expected_head()==expected&&!edit.save(),
        "discard restores local saved baseline and never replaces external checkpoint");
    const auto reopened=odh_host::decode_community_lot_document(odh::decode_session_archive(store.bytes));
    odh_host::CommunityLotEditSession fresh(reopened,store.head,[&](const auto& h,const auto& d){return store.publish(h,d);});
    fresh.stage_architecture(second);require(fresh.save().has_value()&&store.writes==3,
        "fresh session can save after explicitly reloading changed authority");
    odh_host::CommunityLotEditSession failed(original,{},[](const auto&,const auto&)->odh_host::CommunityLotEditPublication{throw std::runtime_error("I/O failure");});
    failed.stage_architecture(first);rejects([&]{failed.save();},"publisher failure propagates");
    require(failed.dirty()&&wire(failed.current())==first_wire&&!failed.expected_head()&&wire(failed.baseline())==original_wire,
        "failed publication preserves all unsaved state");
    failed.discard();require(!failed.dirty()&&wire(failed.current())==original_wire,
        "publication failure releases guard for deliberate discard");
    MemoryStore reentry_store;odh_host::CommunityLotEditSession* active=nullptr;
    odh_host::CommunityLotEditSession reentry(original,{},[&](const auto& h,const auto& d){
        rejects([&]{active->stage(original);},"publisher cannot replace staged document");
        rejects([&]{active->stage_architecture(original.architecture);},"publisher cannot stage architecture");
        rejects([&]{active->discard();},"publisher cannot discard pending state");
        rejects([&]{(void)active->save();},"publisher cannot recursively publish");
        return reentry_store.publish(h,d);
    });
    active=&reentry;reentry.stage_architecture(first);
    require(reentry.save().has_value()&&!reentry.dirty()&&wire(reentry.current())==first_wire&&
        wire(reentry.baseline())==first_wire&&reentry_store.writes==1,
        "reentrant mutation attempts cannot mark a different draft clean");
    std::cout<<"community edit staging/save/discard checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
