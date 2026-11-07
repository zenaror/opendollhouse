// SPDX-License-Identifier: MPL-2.0
#include "live_panel_snapshot.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
using namespace odh;using namespace odh_host;
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
WorldEntityReference avatar(World& world,std::int16_t id,std::size_t words){
    require(world.create(id,EntityKind::avatar,{0,words,0,0}),"create an actual avatar person-data bank");
    return {id,world.find(id)->generation};
}
RelationshipSnapshot matrix(){
    RelationshipSnapshot result;
    result.sources={
        {RelationshipMatrix::neighbor,40,0,{
            {-7,{10,900,20}}, {41,{75,123,-25}}, {42,{-90}}, {43,{}},
            {44,{66}}, {32768,{99}}, {70000,{88}}}},
        {RelationshipMatrix::neighbor,41,0,{{40,{-88,0,99}},{45,{50}}}}
    };
    for(const auto& source:result.sources)for(const auto& pair:source.pairs){++result.pair_count;result.value_count+=pair.values.size();}
    return result;
}
void no_person(const LivePanelSnapshot& snapshot){
    require(!snapshot.has_selected_person&&!snapshot.career&&!snapshot.relationships&&!snapshot.interests,
        "invalid selection does not expose another person's career or relations");
    require(std::none_of(snapshot.skills.begin(),snapshot.skills.end(),[](const auto& v){return v.has_value();})&&
        std::none_of(snapshot.personality.begin(),snapshot.personality.end(),[](const auto& v){return v.has_value();}),
        "invalid selection leaves person values absent, not fabricated zeroes");
}
}
int main(){try{
    World world;const auto selected=avatar(world,1,88);
    // Distinct values prove the UI's source order, signed preservation, and the
    // distinction between actual zero and an unavailable person-data word.
    const std::array<std::int16_t,6> skills{0,123,1000,-5,1250,32767};
    const std::array<std::int16_t,5> personality{710,620,330,540,250};
    for(std::size_t n=0;n<skills.size();++n)require(world.set_person_data(1,static_cast<std::int16_t>(live_skill_indices[n]),skills[n]),"set real skill word");
    for(std::size_t n=0;n<personality.size();++n)require(world.set_person_data(1,static_cast<std::int16_t>(live_personality_indices[n]),personality[n]),"set real personality word");
    require(world.set_person_data(1,31,40)&&world.set_person_data(1,56,9)&&
        world.set_person_data(1,57,4)&&world.set_person_data(1,63,-27),"set source identity and career words");
    RelationshipState relationships;require(relationships.restore_snapshot(matrix()),"restore real directional relationship authority");
    const std::array<std::pair<std::int16_t,std::int16_t>,15> interest_words{{
        {46,0},{47,175},{48,299},{49,1000},{50,-125},{51,1120},{52,630},{53,740},
        {13,815},{14,920},{16,105},{20,210},{26,315},{54,420},{55,525}}};
    for(const auto& word:interest_words)require(world.set_person_data(1,word.first,word.second),"set actual legacy and expansion interest words");
    require(world.set_person_data(1,58,27),"set actual adult age for original interest selection");
    const auto source_words=world.find(1)->person_data;
    const auto pairs_before=relationships.pair_count(),values_before=relationships.value_count();
    std::size_t career_calls=0;
    LivePanelBindings bindings;
    bindings.career=[&](std::int16_t job,std::int16_t level)->std::optional<LiveCareerInfo>{
        ++career_calls;require(job==9&&level==4,"career resolver receives actual type and promotion level");
        LiveCareerInfo result;result.employed=true;result.title="Resolved title";result.salary=155;
        result.start_hour=9;result.end_hour=17;result.performance=99;result.required_skills[0]=600;return result;
    };
    const auto raw=prepare_live_panel_snapshot(world,selected,nullptr,bindings);
    require(raw.has_selected_person&&career_calls==1&&raw.career&&raw.career->title=="Resolved title"&&
        raw.career->salary==155&&raw.career->performance==-27&&raw.career->required_skills[0]==600,
        "real career metadata is retained while performance comes from the selected person");
    for(std::size_t n=0;n<skills.size();++n)require(raw.skills[n]==skills[n],"skill words are neither reordered nor clamped in the snapshot");
    for(std::size_t n=0;n<personality.size();++n)require(raw.personality[n]==personality[n],"five personality values use their TS1 indices");
    require(raw.interests&&raw.interests->age==27&&raw.interests->raws==
        std::array<std::int16_t,19>{0,175,299,1000,-125,1120,630,740,0,175,299,1000,815,920,105,210,315,420,525},
        "original nineteen-topic extraction includes expansion aliases and retains raw signed values without flattening them to bars");
    require(!raw.relationships,"missing relationship authority is distinct from an empty list");

    std::vector<std::pair<std::int16_t,bool>> requests;
    std::vector<std::int16_t> rendered_faces;
    auto face=std::make_shared<BitmapImage>();face->width=face->height=1;face->bit_count=24;face->pixels={{200,170,150,255}};
    bindings.first_portrait=1;bindings.portrait_count=2;
    bindings.neighbor=[&](std::int16_t id,bool visible)->std::optional<LiveRelationshipInfo>{
        requests.emplace_back(id,visible);
        if(id!=40&&id!=-7&&id!=41&&id!=42&&id!=43)return std::nullopt;
        LiveRelationshipInfo person;person.neighbor_id=id;person.name="Actual neighbor "+std::to_string(id);
        if(visible){rendered_faces.push_back(id);person.portrait=face;}
        return person;
    };
    const auto directional=prepare_live_panel_snapshot(world,selected,&relationships,bindings);
    require(directional.relationships&&directional.relationships->size()==4,"only admitted signed neighbor identities appear, never truncated wide keys");
    require(!requests.empty()&&requests.front()==std::make_pair(std::int16_t{40},false),"selected identity is validated without requesting its portrait");
    const auto& people=*directional.relationships;
    require(people[0].neighbor_id==-7&&people[0].daily==10&&people[0].long_term==20,
        "signed neighbor identities preserve their own directional values");
    require(people[1].neighbor_id==41&&people[1].daily==75&&people[1].long_term==-25,
        "selected-to-target scores use offsets0 and2, not offset1 or reverse scores");
    require(people[2].neighbor_id==42&&people[2].daily==-90&&!people[2].long_term,
        "a short relationship record leaves its absent lifetime score unavailable");
    require(people[3].neighbor_id==43&&!people[3].daily&&!people[3].long_term,
        "an admitted empty record is retained without manufacturing either score");
    require(rendered_faces==std::vector<std::int16_t>{41,42}&&!people[0].portrait&&
        people[1].portrait==face&&people[2].portrait==face&&!people[3].portrait,
        "only the two visible accepted neighbors request and own portraits");
    require(std::none_of(requests.begin(),requests.end(),[](const auto& entry){return entry.first==std::numeric_limits<std::int16_t>::min();}),
        "wide relationship key32768 is rejected before signed-short wrapping or neighbor resolution");
    require(world.find(1)->person_data==source_words&&relationships.pair_count()==pairs_before&&
        relationships.value_count()==values_before&&relationships.value(41,40,0)==-88,
        "UI extraction leaves actual person data, reverse relationships and matrix allocation counts untouched");

    rendered_faces.clear();requests.clear();bindings.first_portrait=std::numeric_limits<std::size_t>::max();bindings.portrait_count=2;
    const auto offscreen=prepare_live_panel_snapshot(world,selected,&relationships,bindings);
    require(offscreen.relationships&&offscreen.relationships->size()==4&&rendered_faces.empty(),
        "out-of-range portrait window cannot overflow into visible work or discard metadata");
    bindings.first_portrait=0;bindings.portrait_count=0;rendered_faces.clear();
    require(prepare_live_panel_snapshot(world,selected,&relationships,bindings).relationships->size()==4&&rendered_faces.empty(),
        "zero portrait budget still exposes actual relationship metadata without face work");
    auto no_admission=bindings;no_admission.neighbor={};
    require(!prepare_live_panel_snapshot(world,selected,&relationships,no_admission).relationships,
        "missing neighbor admission service does not fabricate an empty relationship authority");
    auto reject_source=bindings;reject_source.neighbor=[](std::int16_t,bool)->std::optional<LiveRelationshipInfo>{return std::nullopt;};
    require(!prepare_live_panel_snapshot(world,selected,&relationships,reject_source).relationships,
        "unresolved selected neighbor does not masquerade as a known person with no relationships");

    // Invalid generation, object selection and a dead avatar must stop before
    // invoking host metadata or portrait providers.
    const auto calls_before=career_calls;requests.clear();
    no_person(prepare_live_panel_snapshot(world,{1,selected.generation+1},&relationships,bindings));
    no_person(prepare_live_panel_snapshot(world,{99,1},&relationships,bindings));
    require(world.create(2,EntityKind::object,{88,0,0,0}),"create a legitimate object with attributes rather than a forbidden person-data bank");
    no_person(prepare_live_panel_snapshot(world,{2,world.find(2)->generation},&relationships,bindings));
    const auto dead=avatar(world,3,88);require(world.mark_dead(dead),"mark actual avatar dead");
    no_person(prepare_live_panel_snapshot(world,dead,&relationships,bindings));
    require(career_calls==calls_before&&requests.empty(),"invalid selections never call metadata providers");

    const auto short_person=avatar(world,4,4);
    require(world.set_person_data(4,2,-12)&&world.set_person_data(4,3,420),"populate only present short-bank traits");
    const auto partial=prepare_live_panel_snapshot(world,short_person,&relationships,bindings);
    require(partial.has_selected_person&&!partial.career&&!partial.relationships&&!partial.interests&&
        !partial.personality[0]&&!partial.personality[1]&&partial.personality[2]==420&&
        !partial.personality[3]&&partial.personality[4]==-12&&!partial.skills[0],
        "short person-data bank preserves available traits and absence of unavailable words");
    const auto age_missing=avatar(world,6,58);
    require(!prepare_live_panel_snapshot(world,age_missing,nullptr).interests,
        "complete interest value banks without declared age cannot choose an invented adult or child topic set");
    const auto child=avatar(world,7,59);require(world.set_person_data(7,58,7)&&world.set_person_data(7,46,830),"set actual child age and shared travel/toys field");
    const auto child_snapshot=prepare_live_panel_snapshot(world,child,nullptr);
    require(child_snapshot.interests&&child_snapshot.interests->age==7&&
        child_snapshot.interests->raws[0]==830&&child_snapshot.interests->raws[8]==830,
        "child toy and adult travel labels intentionally share the same preserved original field");
    const auto unemployed=avatar(world,5,58);
    const auto unemployed_snapshot=prepare_live_panel_snapshot(world,unemployed,nullptr,bindings);
    require(unemployed_snapshot.career&&!unemployed_snapshot.career->employed&&
        !unemployed_snapshot.career->salary&&!unemployed_snapshot.career->performance&&career_calls==calls_before,
        "actual job0 is unemployed without a fabricated salary, absent performance or resolver call");
    require(world.set_person_data(5,56,9),"set a nonzero career requiring a host resolver");
    LivePanelBindings unavailable;unavailable.career=[](std::int16_t,std::int16_t)->std::optional<LiveCareerInfo>{return std::nullopt;};
    require(!prepare_live_panel_snapshot(world,unemployed,nullptr,unavailable).career&&
        !prepare_live_panel_snapshot(world,unemployed,nullptr).career,
        "missing or unresolved career remains unavailable rather than being mislabeled unemployed");
    require(world.set_person_data(1,31,43),"select a known neighbor with no outgoing source");
    const auto empty=prepare_live_panel_snapshot(world,selected,&relationships,bindings);
    require(empty.relationships&&empty.relationships->empty(),"known identity with no outgoing pairs yields a genuinely empty list");
    std::cout<<checks<<" live panel snapshot checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
