#include "../tools/source_container_drop.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
using Ref=odh::WorldEntityReference;
using Status=odh::Ts1PlacementStatus;
struct Child {Ref ref;bool positioned;std::uint8_t direction;std::int16_t hidden;};
struct Fixture {
    Ref parent{10,100};bool active=true,anchor=false,rewrite_later=false;
    std::vector<Ref> slots{{20,200},{30,300},{40,400}};
    std::map<std::int16_t,Child> children{{20,{{20,200},true,6,2}},
        {30,{{30,300},true,2,0}},{40,{{40,400},false,4,-1}}};
    std::vector<std::string> events;unsigned extents=0,finds=0,unhides=0;
    bool fail_search=false;
    Child* get(Ref ref){auto it=children.find(ref.id);return it!=children.end()&&it->second.ref.generation==ref.generation?&it->second:nullptr;}
    odh_host::SourceContainerDropServices services(){
        odh_host::SourceContainerDropServices result;result.parent=parent;
        result.source_still_matches=[this]{return active;};
        result.total_slots=[this]()->std::optional<std::size_t>{++extents;return slots.size();};
        result.child=[this](std::size_t slot)->std::optional<Ref>{events.push_back("slot"+std::to_string(slot));return slot<slots.size()?std::optional<Ref>{slots[slot]}:std::nullopt;};
        result.copy_position=[this](Ref ref){auto* child=get(ref);if(!child)return false;events.push_back("copy"+std::to_string(ref.id));anchor=child->positioned;return true;};
        result.direction=[this](Ref ref)->std::optional<std::uint8_t>{auto* child=get(ref);if(!child)return {};events.push_back("direction"+std::to_string(ref.id));return child->direction;};
        result.out_of_world=[this](Ref ref,std::uint8_t direction){auto* child=get(ref);
            if(!child||child->direction!=direction)return odh::Ts1PlacementResult{Status::unsupported,-1,{},"wrong own direction"};
            events.push_back("oow"+std::to_string(ref.id));child->positioned=false;
            for(auto& slot:slots)if(slot.id==ref.id&&slot.generation==ref.generation){slot={};break;}
            if(ref.id==20&&rewrite_later){children[30]={{30,301},true,7,0};slots[1]={30,301};}
            return odh::Ts1PlacementResult{Status::success,0,{},{}};
        };
        result.hidden=[this](Ref ref)->std::optional<std::int16_t>{auto* child=get(ref);if(!child)return {};events.push_back("hidden"+std::to_string(ref.id));return child->hidden;};
        result.unhide=[this](Ref ref){auto* child=get(ref);if(!child)return false;++unhides;events.push_back("unhide"+std::to_string(ref.id));child->hidden=0;return true;};
        result.anchor_positioned=[this]()->std::optional<bool>{events.push_back("anchor");return anchor;};
        result.find_location=[this](Ref ref){++finds;events.push_back("find"+std::to_string(ref.id));return odh::Ts1PlacementResult{
            fail_search?Status::unsupported:Status::blocked,5,{},fail_search?"native dependency unavailable":"legitimate no place"};};
        return result;
    }
};
}
int main(){try{
    Fixture f;f.rewrite_later=true;
    const auto done=odh_host::source_container_drop(f.services());
    require(done.completed&&f.extents==1&&f.finds==2&&f.unhides==1,"source slot extent once, ordinary false ignored, OOW anchor skips search, only positive Hidden clears");
    require(f.children[30].ref.generation==301&&!f.children[30].positioned&&f.children[30].direction==7,"later GetSlot sees replacement generation and preserves its own direction");
    require(f.children[20].hidden==0&&f.children[40].hidden==-1,"forced drop unhides positive values only");
    const std::vector<std::string> first{"slot0","copy20","direction20","oow20","hidden20","unhide20","anchor","find20"};
    require(f.events.size()>=first.size()&&std::equal(first.begin(),first.end(),f.events.begin()),"parent copies raw child pose before own direction/OOW and search follows Hidden plus current anchor");
    for(const auto& slot:f.slots)require(slot.id==0,"all source slots detach in source order");
    Fixture unavailable;unavailable.fail_search=true;
    const auto failed=odh_host::source_container_drop(unavailable.services());
    require(!failed.completed&&failed.diagnostic.find("parent=10:100 child=20:200 slot=0 stage=FindLocationFor")!=std::string::npos,"failure names parent/child generations, slot and exact stage");
    require(!unavailable.children[20].positioned&&unavailable.children[20].hidden==0&&unavailable.slots[0].id==0,"unsupported search preserves earlier actual OOW/Hidden/detach effects");
    Fixture expired;expired.active=false;
    require(!odh_host::source_container_drop(expired.services()).completed&&expired.extents==0,"expired source cannot begin drop or inspect slots");
    Fixture reread;auto services=reread.services();
    services.out_of_world=[&](Ref ref,std::uint8_t direction){(void)direction;reread.children[ref.id].positioned=false;reread.anchor=false;return odh::Ts1PlacementResult{Status::success,0,{},{}};};
    require(odh_host::source_container_drop(services).completed&&reread.finds==0,"search condition re-reads parent Position after child lifecycle effects");
    std::cout<<"source_container_drop: passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"source_container_drop: "<<error.what()<<'\n';return 1;}}
