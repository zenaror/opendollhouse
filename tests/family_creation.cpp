#include "odh/family_overlay.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& operation,const char* message){
    try{operation();}catch(const odh::FormatError&){return;}
    throw std::runtime_error(message);
}
struct Fixture {
    std::array<std::uint8_t,32> identity{};
    std::vector<odh::FamilyEntry> families;
    odh::Nbrs neighbors{55,63,{},0};
    Fixture(){
        identity[0]=0xa7;
        for(std::uint16_t id=0;id<2;++id){
            const auto guid=std::uint32_t(1000+id);
            odh::Family family{};family.family_number=4+id;
            family.budget=20000;family.member_guids={guid};
            families.push_back({id,family});
            odh::NeighborRecord neighbor{};
            neighbor.version=10;neighbor.unknown3=9;neighbor.person_mode=9;
            neighbor.name="iffname";neighbor.unknown_neg_one=-1;
            neighbor.person_data=std::vector<std::int16_t>(256);
            neighbor.neighbor_id=static_cast<std::int16_t>(1+id);neighbor.guid=guid;
            neighbors.entries.push_back({1,neighbor});
        }
    }
    odh::FamilyOverlayCreation create(const odh::FamilyOverlay* overlay,std::uint32_t guid=4000)const{
        return odh::make_simitone_family_creation(families,neighbors,identity,overlay,
            "Next family",{{guid,std::vector<std::int16_t>(88)}});
    }
    odh::FamilyOverlay played_then_moved_out(const odh::FamilyOverlay* initial,std::uint16_t id)const{
        auto overlay=odh::plan_family_managed_move_in_overlay(families,identity,initial,id,7,1000);
        std::array<std::uint8_t,32> checkpoint{};checkpoint[0]=0x68;
        overlay=odh::plan_family_checkpoint_overlay(families,identity,&overlay,id,7,1,18000,checkpoint);
        return odh::plan_family_managed_move_out_overlay(families,identity,&overlay,id);
    }
};
void deleted_original_reservations(bool played){
    Fixture f;
    std::optional<odh::FamilyOverlay> history;
    if(played)history=f.played_then_moved_out(nullptr,1);
    const auto removed=odh::plan_family_delete_overlay(f.families,f.neighbors,f.identity,
        history?&*history:nullptr,1);
    const auto roundtrip=odh::decode_family_overlay(odh::encode_family_overlay(removed));
    const auto visible=odh::apply_family_overlay(f.families,f.identity,roundtrip);
    require(visible.size()==1&&visible.front().chunk_id==0,"deleted original must be absent");
    const auto next=f.create(&roundtrip);
    require(next.family_chunk_id==2&&next.members.front().neighbor_id==3,
        "deleted original family and neighbor IDs must remain reserved");
    const auto appended=odh::plan_family_creation_overlay(f.families,f.neighbors,f.identity,&roundtrip,next);
    const auto families=odh::apply_family_overlay(f.families,f.identity,appended);
    require(families.back().family.family_number==6,
        "deleted highest original family number must remain reserved");
    rejects([&]{(void)f.create(&roundtrip,1001);},"deleted original GUID cannot be reused");
}
void archived_creation_reservations(bool played){
    Fixture f;
    const auto first=f.create(nullptr,3000);
    auto overlay=odh::plan_family_creation_overlay(f.families,f.neighbors,f.identity,nullptr,first);
    if(played)overlay=f.played_then_moved_out(&overlay,first.family_chunk_id);
    const auto removed=odh::plan_family_archive_overlay(f.families,f.neighbors,f.identity,
        &overlay,first.family_chunk_id);
    const auto roundtrip=odh::decode_family_overlay(odh::encode_family_overlay(removed));
    require(odh::apply_family_overlay(f.families,f.identity,roundtrip).size()==2,
        "archived creation must be absent");
    const auto next=f.create(&roundtrip);
    require(next.family_chunk_id==3&&next.members.front().neighbor_id==4,
        "archived creation family and neighbor IDs must remain reserved");
    const auto appended=odh::plan_family_creation_overlay(f.families,f.neighbors,f.identity,&roundtrip,next);
    const auto families=odh::apply_family_overlay(f.families,f.identity,appended);
    require(families.back().family.family_number==7,
        "archived highest created family number must remain reserved");
    rejects([&]{(void)f.create(&roundtrip,3000);},"archived created GUID cannot be reused");
}
void no_visible_family_can_still_create(){
    Fixture f;
    auto removed=odh::plan_family_delete_overlay(f.families,f.neighbors,f.identity,nullptr,0);
    removed=odh::plan_family_delete_overlay(f.families,f.neighbors,f.identity,&removed,1);
    require(odh::apply_family_overlay(f.families,f.identity,removed).empty(),
        "fixture must have no visible families");
    const auto next=f.create(&removed);
    require(next.family_chunk_id==2&&next.members.front().neighbor_id==3,
        "empty visible neighborhood must allocate after reserved original identities");
    const auto appended=odh::plan_family_creation_overlay(f.families,f.neighbors,f.identity,&removed,next);
    require(odh::apply_family_overlay(f.families,f.identity,appended).size()==1,
        "new family can be published after deleting every visible family");
}
void removed_exhausted_family_number_stays_exhausted(){
    Fixture f;f.families.back().family.family_number=std::numeric_limits<std::int32_t>::max();
    const auto removed=odh::plan_family_delete_overlay(f.families,f.neighbors,f.identity,nullptr,1);
    rejects([&]{(void)f.create(&removed);},"removed maximum family number must not wrap or be reused");
}
}
int main(){try{
    deleted_original_reservations(false);deleted_original_reservations(true);
    archived_creation_reservations(false);archived_creation_reservations(true);
    no_visible_family_can_still_create();removed_exhausted_family_number_stays_exhausted();
    std::cout<<"Family allocation preserves removed household identities\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
