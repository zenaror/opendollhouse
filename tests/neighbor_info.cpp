#include "odh/neighbor_info.hpp"
#include <iostream>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
odh::NeighborEntry entry(std::int16_t id,bool person=true,
    std::initializer_list<odh::NeighborRelationship> pairs={}) {
    odh::NeighborRecord r{};r.version=4;r.neighbor_id=id;r.guid=static_cast<std::uint32_t>(1000+id);
    r.person_mode=person?1:0;if(person)r.person_data=std::vector<std::int16_t>(80);
    r.relationships=pairs;return {1,r};
}
odh::Nbrs catalog(std::initializer_list<odh::NeighborEntry> entries){return {0,62,entries,0};}
void set_relation(odh::RelationshipState& state,const odh::World& world,std::int16_t source,
    std::int16_t target,std::int16_t value) {
    const auto raw=static_cast<std::uint16_t>(value);
    const odh::Instruction set{26,254,255,{0,2,6,0,7,0,
        static_cast<std::uint8_t>(raw),static_cast<std::uint8_t>(raw>>8)}};
    const odh::VmFrame frame{odh::routine_key(4096),0,{9,9,source},{},{target}};
    require(state.primitive(world)(frame,set,{}, {nullptr,1}).disposition==odh::PrimitiveDisposition::goto_true,
        "real Relationship primitive updates authoritative neighbor score");
}
}
int main(){try{
    auto main=entry(40,true,{{1,41,{50}},{1,42,{49}},{1,99,{100}},
        {1,0x1002b,{80}},{1,44,{70}}});
    (*main.record->person_data)[61]=201;(*main.record->person_data)[58]=23;
    auto friend1=entry(41,true,{{1,40,{50}}});(*friend1.record->person_data)[61]=777;
    auto friend2=entry(42,true,{{1,40,{80}}});
    auto friend3=entry(43,true,{{1,40,{90}}});
    auto oneway=entry(44,false);
    auto wrapped=entry(45);(*wrapped.record->person_data)[61]=-1;(*wrapped.record->person_data)[58]=-32768;
    odh::NeighborContext neighbors(catalog({friend3,main,oneway,friend1,wrapped,friend2}));
    odh::RelationshipState relationships(neighbors);
    odh::Family family{};family.family_number=900;family.house_number=65543;family.family_friends=65535;
    odh::Family wrong{};wrong.family_number=201;wrong.house_number=99;wrong.family_friends=8;
    odh::Family negative{};negative.family_number=1;negative.house_number=-65537;negative.family_friends=32768;
    odh::FamilyContext families({{201,family},{900,wrong},{65535,negative}});
    odh::World world;
    require(world.create(9,odh::EntityKind::avatar,{})&&world.define_type(9,main.record->guid)&&
        world.create(2,odh::EntityKind::object,{})&&world.define_type(2,main.record->guid),"live instance fixture");
    const auto query=[&](std::int16_t nid,std::int16_t field,std::optional<std::uint16_t> current=201){
        return odh::ts1_neighbor_info(neighbors,world,&families,current,&relationships,nid,field);
    };
    const std::array<std::int16_t,10> expected{9,1,23,0,0,2,7,1,0,-1};
    for(std::int16_t field=0;field<10;++field)
        require(query(40,field)==expected[static_cast<std::size_t>(field)],"complete source field0..9 mapping");
    require(query(40,1,900)==0&&query(40,1,std::nullopt)==0,
        "FAMI chunk ID, not FamilyNumber, controls belonging comparison");
    require(query(41,1,std::nullopt)==1&&query(41,1,201)==0&&query(41,6)==0&&query(41,9)==0,
        "missing FAMI record preserves null-equals-null source semantics and zero optional family values");
    require(query(44,2)==0&&query(44,1,std::nullopt)==1&&query(44,6)==0&&query(44,9)==0,
        "absent person data has no family and zero age");
    require(query(45,1,65535)==1&&query(45,6)==-1&&query(45,9)==-32768&&query(45,2)==-32768,
        "signed person family key narrows to ushort chunk ID and family scalars preserve signed low16");
    require(!query(40,1,888),"nonnull current family missing from supplied catalog is an unavailable dependency");
    for(const auto field:{std::int16_t{-32768},std::int16_t{-1},std::int16_t{10},std::int16_t{32767}})
        require(!query(40,field)&&odh::ts1_neighbor_info(neighbors,world,nullptr,{},nullptr,99,field)==0,
            "present neighbor bounds remain strict while missing neighbor zero precedes field/provider validation");
    for(const auto field:{1,6,9})require(!odh::ts1_neighbor_info(neighbors,world,nullptr,{},&relationships,40,
        static_cast<std::int16_t>(field)),"family-dependent fields require actual family provider");
    require(!odh::ts1_neighbor_info(neighbors,world,&families,201,nullptr,40,5),"friend field requires mutable relationship provider");
    require(odh::ts1_neighbor_info(neighbors,world,nullptr,{},nullptr,40,7)==1&&
        odh::ts1_neighbor_info(neighbors,world,nullptr,{},nullptr,40,3)==0,
        "source literal fields do not invent provider dependencies");
    const auto sources=relationships.source_count(),pairs=relationships.pair_count(),values=relationships.value_count();
    require(relationships.mutual_neighbor_friend_count(neighbors,40)==2&&
        relationships.source_count()==sources&&relationships.pair_count()==pairs&&relationships.value_count()==values,
        "friend query counts mutual threshold and low16 target, without storage growth");
    set_relation(relationships,world,41,40,49);require(query(40,5)==1,"reverse score dropping below50 removes friendship immediately");
    set_relation(relationships,world,41,40,50);require(query(40,5)==2,"reverse threshold50 restores friendship immediately");
    set_relation(relationships,world,40,42,50);require(query(40,5)==3&&
        neighbors.by_id(40)->relationships[1].values[0]==49,"live relationship count uses mutable matrix instead of stale NBRS");
    odh::VmMemory memory;
    memory.read=[&](const auto& frame,auto scope,auto field)->std::optional<std::int16_t>{
        if(scope!=24)return {};
        return query(frame.context.stack_object,field);
    };
    std::vector<odh::Instruction> code;
    for(std::uint8_t field=0;field<10;++field)code.push_back({2,
        static_cast<std::uint8_t>(field==9?254:field+1),255,{field,0,field,0,0,5,8,24}});
    const auto key=odh::routine_key(4096);
    odh::Vm vm({{key,odh::Bhav{0x8002,code,{},odh::Bhav::Metadata{0,4,0,0}}}},memory);
    vm.start(key,{9,9,40});
    require(vm.run(20)==odh::VmStatus::returned_true,"actual Expression reads all ten query fields in one BHAV");
    for(std::size_t field=0;field<10;++field)
        require(vm.temps()[field]==(field==5?3:expected[field]),"actual VM receives complete source query values");
    {
        odh::NeighborContext n(catalog({entry(40,false,{{1,41,{}}}),entry(41,false,{{1,40,{100}}})}));
        odh::RelationshipState r(n);
        require(!r.mutual_neighbor_friend_count(n,40),"present empty outward score is unavailable where source would throw");
    }
    {
        odh::NeighborContext n(catalog({entry(40,false,{{1,41,{50}}}),entry(41,false,{{1,40,{}}})}));
        odh::RelationshipState r(n);
        require(!r.mutual_neighbor_friend_count(n,40),"qualified friend with empty reverse score is unavailable");
    }
    {
        odh::NeighborContext n(catalog({entry(40,false,{{1,41,{49}}}),entry(41,false,{{1,40,{}}})}));
        odh::RelationshipState r(n);
        require(r.mutual_neighbor_friend_count(n,40)==0,"source short circuit does not access empty reverse score below threshold");
        require(r.mutual_neighbor_friend_count(n,99)==0,"friend helper also returns zero for missing active source neighbor");
    }
    require(world.retire(9)&&query(40,0)==0,"instance field follows live World retirement");
    std::cout<<checks<<" complete neighbor-info checks passed (native fixtures)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
