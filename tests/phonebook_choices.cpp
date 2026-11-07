#include "../tools/phonebook_choices.hpp"
#include "odh/world_snapshot_codec.hpp"
#include "odh/relationship_snapshot_codec.hpp"
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;void require(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
odh::NeighborEntry record(std::int16_t id,std::int16_t family,std::int16_t gender,bool data=true){
    odh::NeighborRecord n{};n.version=4;n.neighbor_id=id;n.guid=1000+id;n.person_mode=data?1:0;
    if(data){n.person_data=std::vector<std::int16_t>(80);n.person_data->at(61)=family;n.person_data->at(65)=gender;}
    return {1,n};
}
}
int main(){try{
    auto caller=record(34,7,0);caller.record->relationships={{1,32,{}},{1,33,{-99}},{1,40,{100}},{1,41,{100}},{1,42,{100}}};
    odh::NeighborContext neighbors({0,62,{caller,record(32,7,0),record(33,8,1),record(40,8,2),record(41,0,0),record(42,8,0,false),record(43,9,0)},0});
    odh::RelationshipState relations(neighbors);odh::World world;
    require(world.create(21,odh::EntityKind::avatar,{0,88,1})&&world.define_type(21,1034)&&world.set_person_data(21,31,34),"actual caller has exact neighbor identity");
    const odh::WorldEntityReference identity{21,world.find(21)->generation};
    const auto names=[](auto id,auto)->std::optional<std::string>{return id==32?"Cassandra":"Bella";};
    const auto before=odh::encode_world_snapshot(world.snapshot());const auto rbefore=odh::encode_relationship_snapshot(relations.snapshot());
    odh::RelationshipState unknown;
    require(!odh_host::phonebook_choices(world,identity,neighbors,unknown,names).ready,"unknown relation owner is not an invented empty contact list");
    const auto offered=odh_host::phonebook_choices(world,identity,neighbors,relations,names);
    require(offered.ready&&offered.choices.size()==2&&offered.choices[0].value==32&&offered.choices[1].value==33,
        "directional contacts include own family and negative/empty scores; pets/zero family/absent person bank excluded");
    require(before==odh::encode_world_snapshot(world.snapshot())&&rbefore==odh::encode_relationship_snapshot(relations.snapshot()),"discovery leaves World and current relationships unchanged");
    const odh::Instruction set_contact{26,254,255,{0,2,6,0,7,0,0,0}};
    const odh::VmFrame current_contact{odh::routine_key(4096),0,{21,21,34},{},{43}};
    require(relations.primitive(world)(current_contact,set_contact,{}, {nullptr,1}).disposition==odh::PrimitiveDisposition::goto_true,
        "actual Relationship primitive creates current directional contact");
    const auto current=odh_host::phonebook_choices(world,identity,neighbors,relations,names);
    require(current.ready&&current.choices.size()==3&&current.choices.back().value==43,
        "contact choices use current relationship authority rather than immutable NBRS relationships");
    require(!odh_host::phonebook_choices(world,{21,identity.generation+1},neighbors,relations,names).ready,"stale caller generation refused");
    require(!odh_host::phonebook_choices(world,identity,neighbors,relations,{}).ready,"missing names never become asset filenames");
    require(!odh_host::phonebook_choices(world,identity,neighbors,relations,names,1).ready,"budget refuses entire list instead of truncating contacts");
    require(!odh_host::phonebook_choices(world,identity,neighbors,relations,names,4096,1).ready,"label budget refuses without publication");
    auto wrong_state=world.snapshot();wrong_state.entities.front().definition->guid=9999;odh::World wrong_world;
    require(wrong_world.restore_snapshot(wrong_state)&&!odh_host::phonebook_choices(wrong_world,identity,neighbors,relations,names).ready,"current caller GUID must match NBRS");
    auto broken=caller;broken.record->relationships.push_back({1,99,{0}});odh::NeighborContext missing({0,62,{broken,record(32,7,0),record(33,8,1),record(40,8,2),record(41,0,0),record(42,8,0,false)},0});
    odh::RelationshipState missing_relations(missing);
    require(!odh_host::phonebook_choices(world,identity,missing,missing_relations,names).ready,"missing linked current neighbor is explicit unsupported, not guessed away");
    auto empty=record(34,7,0);odh::NeighborContext nobody({0,62,{empty},0});odh::RelationshipState none(nobody);
    const auto no_contacts=odh_host::phonebook_choices(world,identity,nobody,none,{});
    require(no_contacts.ready&&no_contacts.choices.empty(),"known empty contact list remains a cancellable real dialog");
    std::cout<<checks<<" phonebook source-policy checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
