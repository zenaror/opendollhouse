#include "odh/property_survivors.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
template<class F> void rejects(F call,const char* text) {
    try{call();}catch(const odh::FormatError&){return;}
    throw std::runtime_error(text);
}
odh::DefinitionCandidate definition(std::uint32_t guid,std::uint16_t type=4,
    std::uint16_t master=0,std::int16_t sub=0) {
    odh::ObjectDefinition d{};d.version=138;d.guid=guid;d.object_type=type;
    d.master_id=master;d.sub_index=sub;d.is_multitile=master!=0;d.build_mode_type=0;
    return {"objects.far","objects.iff",static_cast<std::uint16_t>(guid),d};
}
void object(odh::World& world,std::int16_t id,std::uint32_t guid,
    std::optional<std::uint32_t> master={},std::size_t slots=0,odh::EntityKind kind=odh::EntityKind::object) {
    check(world.create(id,kind,{1,0,slots,0}) && world.define_type(id,guid,master),"fixture entity");
    check(world.declare_object_word(id,43,16),"fixture retained flag");
}
}
int main(){try {
    using namespace odh;
    const DefinitionIndex index({definition(1),definition(10,4,7,0),definition(11,4,7,1),definition(12,4,7,-1),
        definition(20,4,8,0),definition(21,4,8,1),definition(22,4,8,-1),definition(30,2)});
    WorldLimits wl;wl.reserved_entity_ids={100};World world(wl);
    object(world,1,1,{},1);object(world,2,10,12);object(world,3,11,12,1);
    object(world,4,20,22);object(world,5,21,22,1);object(world,6,1);object(world,7,1,{},1);
    object(world,8,1);
    check(world.attach(1,0,2) && world.attach(3,0,4) && world.attach(5,0,6) && world.attach(7,0,8),"fixture containment");
    const std::vector<SavedMultitileGroup> groups{{1,{}, {1}},{2,12,{3,2}},{4,22,{4,5}}, {6,{}, {6}},
        {7,{}, {7}},{8,{}, {8}}};
    RelationshipState relationships;
    check(relationships.import_object_relationships(world,{{1,{{7,{1}}}},{7,{{1,{2}},{8,{3,4,5}},{7,{}}}}}),"fixture relations");
    auto rs=relationships.snapshot();
    rs.sources.push_back({RelationshipMatrix::neighbor,1,0,{{65537,{9,10}},{1,{11}}}});
    rs.pair_count+=2;rs.value_count+=3;
    RelationshipState with_neighbors;check(with_neighbors.restore_snapshot(rs),"fixture NBRS");
    const auto before=world.snapshot();
    auto result=prepare_property_survivors(world,with_neighbors,index,groups,true,{1},{});
    check(result.removed_ids==std::vector<std::int16_t>({1,2,3,4,5,6}),"closure crosses contained children and complete groups repeatedly");
    check(result.world->snapshot().entities.size()==2 && result.world->find(8)->attachment->container==7,
        "unrelated complete containment survives");
    check(result.world->find(7)->object_words==world.find(7)->object_words &&
        result.world->find(7)->attributes==world.find(7)->attributes &&
        result.world->find(7)->generation==world.find(7)->generation,"survivor durable words and identity preserved");
    check(result.groups.size()==2 && result.groups[0].leader==7 && result.groups[1].leader==8,"whole survivor partition");
    check(result.world->reserved_entity_ids()==std::vector<std::int16_t>({1,2,3,4,5,6,100}),"union reservations");
    check(result.world->snapshot().generation_sequence==before.generation_sequence,"generation ceiling preserved");
    const auto filtered=result.relationships->snapshot();
    check(filtered.sources.size()==2 && filtered.pair_count==4 && filtered.value_count==6,
        "deleted object sources and exact targets pruned, counts rebuilt");
    check(result.relationships->value(1,65537,0)==9 && result.relationships->value(1,1,0)==11,
        "neighbor full-width and matching numeric keys untouched");
    check(result.relationships->object_value(*result.world,7,8,2)==5 &&
        !result.relationships->object_value(*result.world,7,1,0),"survivor relationship tails preserved");
    check(world.snapshot().entities.size()==before.entities.size() && world.snapshot().spatial_revision==before.spatial_revision &&
        world.find(2)->attachment->container==1 && with_neighbors.snapshot().pair_count==rs.pair_count,
        "source World and relationships unchanged");
    auto all=prepare_property_survivors(world,with_neighbors,index,groups,true,{1,7},{});
    check(all.world->snapshot().entities.empty() && all.world->snapshot().generation_sequence==before.generation_sequence,
        "empty property preserves generation ceiling");
    check(all.world->create(9,EntityKind::object) && all.world->find(9)->generation>before.generation_sequence &&
        !all.world->create(1,EntityKind::object),"retired IDs never reused; fresh generation grows");
    auto none=prepare_property_survivors(world,with_neighbors,index,groups,true,{},{});
    check(none.removed_ids.empty() && none.world->snapshot().entities.size()==8,"empty selection is structural identity");
    rejects([&]{prepare_property_survivors(world,with_neighbors,index,groups,false,{1},{});},"incomplete inventory accepted");
    rejects([&]{prepare_property_survivors(world,with_neighbors,index,groups,true,{1,1},{});},"duplicate seed accepted");
    rejects([&]{prepare_property_survivors(world,with_neighbors,index,groups,true,{99},{});},"missing seed accepted");
    auto bad=groups;bad.pop_back();
    rejects([&]{prepare_property_survivors(world,with_neighbors,index,bad,true,{1},{});},"incomplete partition accepted");
    bad=groups;bad.push_back(groups.front());
    rejects([&]{prepare_property_survivors(world,with_neighbors,index,bad,true,{1},{});},"duplicate member accepted");
    bad=groups;bad[1].master_guid=22;
    rejects([&]{prepare_property_survivors(world,with_neighbors,index,bad,true,{1},{});},"bad master accepted");
    SavedWorldLimits small;small.world.entities=2;
    rejects([&]{prepare_property_survivors(world,with_neighbors,index,groups,true,{1}, {},small);},"deletion hides oversized source");
    SavedWorldLimits conflicting;conflicting.world.reserved_entity_ids={7};
    rejects([&]{prepare_property_survivors(world,with_neighbors,index,groups,true,{1},{},conflicting);},"live reservation accepted");
    auto stale=rs;stale.sources.front().generation+=100;
    RelationshipState stale_state;check(stale_state.restore_snapshot(stale),"fixture stale source");
    rejects([&]{prepare_property_survivors(world,stale_state,index,groups,true,{1},{});},"deleted stale source concealed");
    auto wide=rs;
    for(auto& entry:wide.sources)if(entry.matrix==RelationshipMatrix::object) {entry.pairs[0].target=65535;break;}
    RelationshipState wide_state;check(wide_state.restore_snapshot(wide),"fixture wide target");
    rejects([&]{prepare_property_survivors(world,wide_state,index,groups,true,{1},{});},"noncanonical object target accepted");

    World actors;object(actors,1,1,{},1);object(actors,2,30);
    check(actors.attach(1,0,2),"fixture actor as contained child");
    const std::vector<SavedMultitileGroup> ag{{1,{}, {1}},{2,{}, {2}}};
    RelationshipState empty;
    rejects([&]{prepare_property_survivors(actors,empty,index,ag,true,{1},{});},"cascade deletes unauthorized OBJD actor");
    const auto* actor=actors.find(2);
    auto authorized=prepare_property_survivors(actors,empty,index,ag,true,{1},{{2,actor->generation,30}});
    check(authorized.world->snapshot().entities.empty(),"exact departing identity authorizes structural cascade");
    rejects([&]{prepare_property_survivors(actors,empty,index,ag,true,{1},{{2,actor->generation+1,30}});},"stale actor approval accepted");
    rejects([&]{prepare_property_survivors(actors,empty,index,ag,true,{},{});},"retained actor silently activated");
    rejects([&]{prepare_property_survivors(actors,empty,index,ag,true,{1},{{1,actors.find(1)->generation,1}});},"nonactor approval accepted");
    World disguised;object(disguised,1,1,{},0,EntityKind::avatar);
    rejects([&]{prepare_property_survivors(disguised,empty,index,{{1,{}, {1}}},true,{1},{});},"native avatar bypassed actor boundary");
    auto ghost=world.snapshot();ghost.entities.front().ui_ghost=true;
    World ghosts(wl);check(ghosts.restore_snapshot(ghost),"fixture ghost");
    rejects([&]{prepare_property_survivors(ghosts,empty,index,groups,true,{1},{});},"UI ghost hidden by deletion");
    WorldLimits full_reservations;
    for(std::int16_t id=100;id<4196;++id)full_reservations.reserved_entity_ids.push_back(id);
    World reserved_world(full_reservations);object(reserved_world,1,1);
    rejects([&]{prepare_property_survivors(reserved_world,empty,index,{{1,{}, {1}}},true,{1},{});},
        "retirement overflowed the reservation cap");
    check(reserved_world.find(1) && reserved_world.reserved_entity_ids().size()==4096,"reservation failure leaves source intact");
    World imported;
    std::array<std::int16_t,68> words{};std::array<std::int16_t,5> extras{};
    words[2]=123;words[3]=2;words[11]=77;words[43]=16;words[12]=2;extras[0]=2;
    check(imported.create_saved_entity(1,EntityKind::object,1,{2},{},{},words,extras,1),"fixture imported parent");
    words[2]=1;words[3]=0;words[11]=88;
    check(imported.create_saved_entity(2,EntityKind::object,1,{1},{},{},words,extras,0) && imported.attach(1,0,2),
        "fixture imported child");
    auto imported_result=prepare_property_survivors(imported,empty,index,{{1,{}, {1}},{2,{}, {2}}},true,{2},{});
    const auto& normalized=imported_result.world->find(1)->saved_object_banks->object_data;
    check(normalized[2]==0 && normalized[3]==-1 && normalized[11]==1,"derived bank mirrors normalized in private copy");
    check(normalized[12]==2 && imported_result.world->find(1)->attributes[0]==2 &&
        imported_result.world->find(1)->saved_object_banks->extras[0]==2,"arbitrary words equal to deleted ID preserved");
    check(imported.find(1)->saved_object_banks->object_data[2]==123 &&
        imported.find(1)->saved_object_banks->object_data[11]==77 && imported.find(1)->slots[0]==2,
        "source saved banks remain immutable");
    std::cout<<"property survivors tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
