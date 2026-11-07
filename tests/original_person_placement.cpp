// SPDX-License-Identifier: MPL-2.0
#include "original_person_placement.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace odh;
using namespace odh_host;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void native_in_world_bounds(){
    const auto inside=[](std::int32_t x,std::int32_t y,std::int32_t size=64){
        // Invalid heading and level deliberately demonstrate the native XY-only
        // contract. This query neither places nor validates a World pose.
        return original_person_is_in_world(EntityPosition{x,y,-9,255},size);
    };
    require(inside(0,16)==false&&inside(15,16)==false&&inside(16,0)==false,
        "tile zero and fractional tile zero lie outside native interior");
    require(inside(16,16)==true&&inside(31,31)==true&&inside(62*16+15,62*16+15)==true,
        "native interior includes tiles one and size-minus-two including their low nibble");
    require(inside(63*16,16)==false&&inside(16,63*16)==false&&inside(64*16,16)==false,
        "size-minus-one border and beyond are outside");
    require(inside(2048,16)==false&&inside(16,2048)==false,
        "tile128 narrows to signed byte minus128 before bounds comparison");
    require(inside(4096+16,16)==true&&inside(-1,16)==false&&inside(-16,16)==false,
        "native signed byte wraps after arithmetic tile shift including negative coordinates");
    require(original_person_is_in_world({},64)==false,
        "absent World position represents native negative out-of-world pose");
    for(const auto size:{-1,0,1,129,1000})require(!original_person_is_in_world({},size).has_value(),
        "unsupported lot dimensions remain unknown even without a position");
    require(inside(16,16,2)==false&&inside(16,16,3)==true,
        "smallest supported lots preserve strict interior bounds");
}
struct Fixture {
    bool authority=true,contained=false,in_world=false,force_ok=true;
    std::optional<bool> skip=false;
    EntityPosition actual{171,-21,2,5};
    std::vector<std::string> events;
    std::vector<EntityPosition> queries,anchors,forced;
    std::vector<bool> restrictions;
    std::vector<Ts1PlacementStatus> query_results;
    std::vector<OriginalPositionSearchStatus> search_results;
    OriginalPersonPlacementServices services;
    Fixture(){
        services.source_still_matches=[&]{return authority;};
        services.contained_snapshot=[&]()->std::optional<bool>{events.push_back("snapshot");return contained;};
        services.remove_from_world=[&](std::string&){events.push_back("remove");contained=false;return true;};
        services.current_position=[&]()->std::optional<EntityPosition>{events.push_back("position");return actual;};
        services.query54=[&](const auto& point){
            events.push_back("query");queries.push_back(point);const auto i=queries.size()-1;
            return Ts1PlacementResult{i<query_results.size()?query_results[i]:Ts1PlacementStatus::success,-1,{},{}};
        };
        services.search=[&](const auto& start,bool same){
            events.push_back(same?"search_room":"search_any");anchors.push_back(start);restrictions.push_back(same);
            OriginalPositionSearchResult result;const auto i=anchors.size()-1;
            result.status=i<search_results.size()?search_results[i]:OriginalPositionSearchStatus::exhausted;
            if(result.status==OriginalPositionSearchStatus::found)result.position=EntityPosition{88,104,start.level,start.direction};
            return result;
        };
        services.skip_native_recovery_position=[&]{events.push_back("bit6");return skip;};
        services.is_in_world=[&]()->std::optional<bool>{events.push_back("in_world");return in_world;};
        services.lot_size=[&]()->std::optional<std::int32_t>{events.push_back("size");return 63;};
        services.force88=[&](const auto& point,std::string&){events.push_back("force");forced.push_back(point);actual=point;return force_ok;};
        services.footprint74=[&](std::string&){events.push_back("footprint");return true;};
    }
    auto run(){return place_original_person(services);}
};
void source_branch_matrix(){
    for(const bool contained:{false,true})for(const bool skip:{false,true})for(const bool in_world:{false,true}){
        Fixture f;f.contained=contained;f.skip=skip;f.in_world=in_world;const auto before=f.actual;
        const auto result=f.run();const bool fallback=!skip&&!in_world;
        std::vector<std::string> expected{"snapshot"};
        if(contained)expected.insert(expected.end(),{"remove","position","query"});
        expected.push_back("bit6");if(!skip)expected.push_back("in_world");
        if(fallback)expected.insert(expected.end(),{"remove","position","size","query","force","footprint"});
        require(result.completed&&result.fallback_executed==fallback&&f.events==expected,"native branch matrix keeps snapshot/query/bit6/IsInWorld order");
        require(result.queries==std::size_t(contained)+std::size_t(fallback)&&result.searches==0,"queries run only for contained and OOW branches");
        if(contained)require(f.queries.front()==before,"contained query reads actual pose after removal");
        if(fallback)require(f.forced==std::vector<EntityPosition>{{504,504,2,5}}&&result.forced_position==f.forced.front(),
            "fallback centers odd-size lot, erases old sub-tile nibble and preserves level/direction");
        else require(f.forced.empty()&&!result.forced_position&&f.actual==before,"contained query never publishes its candidate");
    }
}
void query_and_search_sequences(){
    for(const auto outcome:{OriginalPositionSearchStatus::found,OriginalPositionSearchStatus::exhausted}){
        Fixture f;f.query_results={Ts1PlacementStatus::blocked};
        f.search_results=outcome==OriginalPositionSearchStatus::found?
            std::vector<OriginalPositionSearchStatus>{outcome}:
            std::vector<OriginalPositionSearchStatus>{outcome,outcome};
        const auto result=f.run();require(result.completed&&result.fallback_executed&&f.forced.size()==1,"OOW fallback forces after query/search completion");
        require(f.restrictions==(outcome==OriginalPositionSearchStatus::found?std::vector<bool>{true}:std::vector<bool>{true,false}),
            "same-room search precedes unrestricted search, with early exit on found");
        const auto expected=outcome==OriginalPositionSearchStatus::found?EntityPosition{88,104,2,5}:EntityPosition{504,504,2,5};
        require(f.forced.front()==expected&&f.events.back()=="footprint","both exhausted searches still force center then update footprint");
        for(const auto& anchor:f.anchors)require(anchor==EntityPosition{504,504,2,5},"retry preserves original search anchor");
    }
    Fixture f;f.contained=true;f.query_results={Ts1PlacementStatus::blocked,Ts1PlacementStatus::success};
    f.search_results={OriginalPositionSearchStatus::found};const auto result=f.run();
    require(result.completed&&f.forced==std::vector<EntityPosition>{{504,504,2,5}}&&f.queries.back()==f.forced.front(),
        "contained search result is discarded before independent center fallback");
    Fixture ep5;ep5.contained=true;
    ep5.services.query54=[&](const auto&){ep5.events.push_back("query");ep5.skip=true;return Ts1PlacementResult{Ts1PlacementStatus::success,-1,{},{}};};
    require(ep5.run().completed&&ep5.events==std::vector<std::string>{"snapshot","remove","position","query","bit6"},
        "bit6 is read after query effects, not cached before contained probing");
}
void failures_preserve_effects(){
    Fixture unknown;unknown.contained=true;unknown.skip.reset();auto result=unknown.run();
    require(!result.completed&&result.effects_started&&unknown.events.back()=="bit6"&&unknown.forced.empty(),"unknown bit6 stops after completed contained effects");
    Fixture expired;expired.contained=true;expired.services.query54=[&](const auto&){expired.authority=false;return Ts1PlacementResult{Ts1PlacementStatus::success,-1,{},{}};};
    result=expired.run();require(!result.completed&&result.effects_started&&result.queries==1&&expired.forced.empty(),"expired query authority prevents later reads and publication");
    Fixture unsupported;unsupported.query_results={Ts1PlacementStatus::blocked};unsupported.search_results={OriginalPositionSearchStatus::unsupported};
    result=unsupported.run();require(!result.completed&&result.searches==1&&unsupported.forced.empty(),"unsupported search is distinct from exhausted and never forces");
    Fixture force;force.force_ok=false;result=force.run();
    require(!result.completed&&result.effects_started&&result.fallback_executed&&force.events.back()=="force"&&force.actual==EntityPosition{504,504,2,5},
        "partial force effects survive failure without falsely executing footprint");
    Fixture footprint;footprint.services.footprint74=[](std::string&){return false;};result=footprint.run();
    require(!result.completed&&result.forced_position&&footprint.actual==*result.forced_position,"footprint failure preserves published force pose");
    Fixture malformed;malformed.query_results={Ts1PlacementStatus::blocked};
    malformed.services.search=[](const auto&,bool){OriginalPositionSearchResult r;r.status=OriginalPositionSearchStatus::found;return r;};
    require(!malformed.run().completed&&malformed.forced.empty(),"found search without a pose is unsupported");
    for(unsigned missing=0;missing<11;++missing){Fixture f;
        if(missing==0)f.services.contained_snapshot={};if(missing==1)f.services.remove_from_world={};
        if(missing==2)f.services.current_position={};if(missing==3)f.services.query54={};
        if(missing==4)f.services.search={};if(missing==5)f.services.skip_native_recovery_position={};
        if(missing==6)f.services.is_in_world={};if(missing==7)f.services.lot_size={};
        if(missing==8)f.services.force88={};if(missing==9)f.services.footprint74={};if(missing==10)f.services.source_still_matches={};
        result=f.run();require(!result.completed&&!result.effects_started&&f.events.empty(),"incomplete service bundle rejects before effects");
    }
}
}
int main(){try{native_in_world_bounds();source_branch_matrix();query_and_search_sequences();failures_preserve_effects();
    std::cout<<"original person placement sequence checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
