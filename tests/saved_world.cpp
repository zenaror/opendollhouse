#include "odh/saved_world.hpp"
#include <cfenv>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message) {
    ++checks; if(!value) throw std::runtime_error(message);
}
template<class F> void rejected(F&& f,const char* message) {
    ++checks; try { f(); } catch(const odh::FormatError&) { return; }
    throw std::runtime_error(message);
}
odh::ObjectDefinition definition(std::uint32_t guid,std::uint16_t master=0,std::int16_t sub=0) {
    return {138,guid,4,master,sub,master!=0,32,100};
}
odh::SavedWorldEntity record(std::uint16_t id,std::uint32_t guid) {
    odh::SavedWorldEntity r{};
    r.saved.object_id=id;r.saved.type_id=id;r.saved.guid=guid;
    auto& p=r.saved.prefix;
    p.x=id*16;p.y=id*32;p.level=1;p.object_data[1]=3;p.object_data[3]=-1;
    p.object_data[11]=static_cast<std::int16_t>(id);p.object_data[67]=124;
    p.object_data[4]=256;p.attributes={-32768,5,32767};p.temps[7]=-81;p.extras={11,22,33,44,55};
    p.footprint={7,8,9,10};p.consumed_bits=20;p.remaining_bits=24;p.block_offset=id*100;
    auto& c=r.common;c.block_offset=p.block_offset;c.start_bits=20;c.consumed_bits=30;
    c.remaining_bits=14;c.relationship_flag=-1;
    auto& t=r.tail;t.block_offset=p.block_offset;t.start_bits=30;t.consumed_bits=40;
    t.remaining_bits=4;t.remaining_data={0xa0};t.unknown_int=-33;
    r.definition=definition(guid);r.serialized_object_type=4;
    return r;
}
std::vector<odh::SavedWorldEntity> fixture() {
    auto a=record(1,101),b=record(2,102),c=record(3,103),d=record(4,104);
    a.common.slot_count=3;a.common.linked_slot=odh::ObjmSavedSlot{91,4};
    a.common.slots={{17,0},{18,2}};
    b.saved.prefix.object_data[2]=1;b.saved.prefix.object_data[3]=1;
    b.serialized_object_type=2;b.tail.person.emplace();b.tail.person->person_data.resize(80,-22);
    b.tail.person->motive_data_bits.fill(0x42c90000U); // 100.5 -> even100.
    b.tail.person->motive_data_bits[1]=0x43018000U; //129.5 ->130, no setter clamp.
    b.tail.person->motive_data_bits[2]=0xc6800000U; //-16384.
    b.tail.person->motive_data_old_bits.fill(0x7fc01234U); //old NaN retained only.
    b.tail.person->body.bytes=std::string("body\x81",5);
    b.tail.person->body.padding=0xa3;b.tail.person->body.start_byte=29;
    b.tail.person->accessory_count=1;b.tail.person->accessories={{{"hat",{},3},{"binding",0xff,8}}};
    b.tail.person->interaction_queue_count=1;b.tail.person->interaction_queue.push_back({});
    b.tail.person->interaction_queue.back().caller_id=2;b.tail.person->interaction_queue.back().target_id=1;
    b.tail.person->object_use_count=1;b.tail.person->object_uses={{1,3,42}};
    b.tail.person->motive_delta_count=1;b.tail.person->motive_deltas={{3,0x7f800000U,0x80000000U}};
    a.common.relationship_count=1;a.common.relationships={{1,2,3,{65535,32768,70000}}};
    a.common.stack_count=1;a.common.stack={{2,4096,3,1,2,{8,9},{10},-1,4}};
    c.definition=definition(103,12,0);d.definition=definition(104,12,1);
    c.tail.multitile=odh::ObjmMultitileState{0,0,0,2,0,0,0};
    d.tail.multitile=odh::ObjmMultitileState{-1,1,0,3,4,0,3};
    d.saved.prefix.x=-16;d.saved.prefix.y=-16;d.saved.prefix.level=2;
    return {d,b,a,c}; // Source order does not determine IDs or group leader.
}
std::vector<odh::DefinitionCandidate> candidates(const std::vector<odh::SavedWorldEntity>& records) {
    std::vector<odh::DefinitionCandidate> c;
    for(const auto& r:records)c.push_back({"synthetic","house.iff",static_cast<std::uint16_t>(r.saved.object_id),r.definition});
    c.push_back({"synthetic","house.iff",99,definition(105,12,-1)});return c;
}
odh::VmFrame frame(std::int16_t id) { return {odh::routine_key(4096),0,{id,id,id},{},{}}; }
}
int main() {
    try {
        for(const auto mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
            require(std::fesetround(mode)==0,"host supports rounding mode fixture");
            for(const auto& test:std::vector<std::pair<std::uint32_t,std::int16_t>>{
                {0,0},{0x80000000U,0},{1,0},{0x80000001U,0},{0x3f000000U,0},{0xbf000000U,0},
                {0x3f000001U,1},{0xbf000001U,-1},{0x3fc00000U,2},{0x40200000U,2},
                {0xbfc00000U,-2},{0xc0200000U,-2},{0x46fffe00U,32767},{0xc7000000U,-32768},
                {0xc7000080U,-32768}})
                require(odh::saved_motive_word(test.first)==test.second,"binary32 ties-even integer conversion");
            for(const auto bits:{0x7f800000U,0xff800000U,0x7fc01234U,0x46ffff00U,0x47000000U,0xc7000081U})
                rejected([&]{odh::saved_motive_word(bits);},"nonfinite/rounded overflow rejected");
        }
        std::fesetround(FE_TONEAREST);
        auto source=fixture();auto defs=candidates(source);odh::DefinitionIndex index(defs);
        auto restored=odh::make_saved_world(source,index);
        require(restored && restored->world->next_entity(0)==1 && restored->world->next_entity(3)==4,
                "all original IDs restored including source order mismatch");
        const auto& w=*restored->world;const auto* avatar=w.find(2);
        require(avatar && avatar->kind==odh::EntityKind::avatar && avatar->person_data==std::vector<std::int16_t>(80,-22),"saved avatar bank restored exactly");
        require(avatar->motives[0]==100 && avatar->motives[1]==130 && avatar->motives[2]==-16384,"motives imported without setter clamp");
        require(avatar->attachment && avatar->attachment->container==1 && avatar->attachment->slot==1 && !avatar->attachment->positioned,
                "runtime attachment uses subsequent slot pair and remains unpositioned");
        require(avatar->position==odh::EntityPosition{32,64,1,3},"attachment preserves independently saved child coordinates");
        require(!w.find(4)->position && w.find(4)->definition->master_guid==105,"out of world retained and master resolved by resource");
        require(w.find(1)->slots==std::vector<std::int16_t>({0,2}),"linked slot not confused with runtime slot zero");
        require(w.read(frame(2),3,2)==1 && w.read(frame(2),3,3)==1 && w.read(frame(2),3,11)==2 && w.read(frame(1),3,67)==2,
                "derived getters follow validated containment and actual bank sizes");
        require(w.read(frame(1),3,3)==-1 && restored->records[1].definition.object_type==4 &&
            restored->records[1].serialized_object_type==2,"unattached slot and declared OBJT kind adaptation preserved");
        require(restored->multitile_groups.size()==1 && restored->multitile_groups[0].leader==3 &&
            restored->multitile_groups[0].members==std::vector<std::int16_t>({3,4}),"multitile leader first independent of decoded order");
        require(restored->relationships->object_value(w,1,2,0)==-1 && restored->relationships->object_value(w,1,2,1)==-32768 &&
            restored->relationships->object_value(w,1,2,2)==4464,"raw relationships adapted to signed low16 in generation backend");
        require(restored->records[1].tail.person->motive_data_old_bits[0]==0x7fc01234U &&
            restored->records[1].tail.person->body.padding==0xa3 && restored->records[1].tail.person->accessories[0].binding.padding==0xff &&
            restored->records[2].saved.prefix.temps[7]==-81 && restored->records[2].tail.remaining_data==std::vector<std::uint8_t>{0xa0},
            "raw source banks, strings, temps and suffix retained");
        source[1].tail.person->body.bytes="changed";source[1].saved.prefix.attributes.clear();
        require(restored->records[1].tail.person->body.bytes==std::string("body\x81",5) && avatar->attributes.size()==3,"returned state owns all caller data");
        auto reject_change=[&](auto change,const char* message) { auto bad=fixture();change(bad);rejected([&]{odh::make_saved_world(bad,index);},message); };
        reject_change([](auto& v){v[0].saved.object_id=2;},"duplicate ID rejected");
        reject_change([](auto& v){v[0].saved.object_id=32768;},"unsigned high ID rejected");
        reject_change([](auto& v){v[0].saved.prefix.object_data[11]=1;},"ID word mismatch rejected");
        reject_change([](auto& v){v[0].saved.type_id=0;},"zero type mapping rejected");
        reject_change([](auto& v){v[0].saved.type_id=v[1].saved.type_id;},"contradictory type/GUID mapping rejected");
        reject_change([](auto& v){v[0].definition.guid=999;},"definition metadata mismatch rejected");
        reject_change([](auto& v){v[0].tail.multitile.reset();},"required multitile branch rejected");
        reject_change([](auto& v){v[1].serialized_object_type=4;},"person/OBJT branch mismatch rejected");
        reject_change([](auto& v){v[0].saved.prefix.level=3;},"invalid level rejected");
        reject_change([](auto& v){v[0].saved.prefix.y=0;},"half sentinel rejected");
        reject_change([](auto& v){v[0].saved.prefix.object_data[1]=8;},"invalid direction rejected");
        reject_change([](auto& v){v[1].common.start_bits=0;},"cursor mismatch rejected");
        reject_change([](auto& v){v[1].tail.consumed_bits=v[1].tail.start_bits;},"undecoded tail rejected");
        reject_change([](auto& v){v[1].tail.remaining_data[0]=0xa1;},"opaque suffix unused bits rejected");
        reject_change([](auto& v){v[2].common.slot_count=2;},"slot count mismatch rejected");
        reject_change([](auto& v){v[2].common.slots[0].object_id=2;},"duplicate occupied child rejected");
        reject_change([](auto& v){v[2].common.linked_slot->object_id=90;},"dangling linked slot rejected");
        reject_change([](auto& v){v[1].saved.prefix.object_data[3]=0;},"containment reciprocity rejected");
        reject_change([](auto& v){v[2].common.slots[1].object_id=0;},"orphan container rejected");
        reject_change([](auto& v){v[0].tail.multitile->parent_id=2;},"nonmultitile parent rejected");
        reject_change([](auto& v){v[3].tail.multitile->parent_id=4;},"multitile parent cycle rejected");
        reject_change([](auto& v){v[1].tail.person->motive_data_bits[4]=0x7fc01234U;},"current NaN motive rejected");
        reject_change([](auto& v){v[1].tail.person->person_data.resize(81);},"person extent rejected");
        reject_change([](auto& v){v[1].tail.person->interaction_queue[0].target_id=55;},"dormant queue dangling target rejected");
        reject_change([](auto& v){v[2].common.stack[0].local_count=2;},"frame count mismatch rejected");
        reject_change([](auto& v){v[2].common.relationships[0].target_id=55;},"relationship dangling target rejected");
        reject_change([](auto& v){v[2].common.relationships[0].value_count=4;},"relationship count mismatch rejected");
        reject_change([](auto& v){v[2].common.relationship_count=2;},"relationship serialized count rejected");
        reject_change([](auto& v){v[1].common.slot_count=2;v[1].common.linked_slot=odh::ObjmSavedSlot{0,0};v[1].common.slots={{0,1}};
            v[2].saved.prefix.object_data[2]=2;v[2].saved.prefix.object_data[3]=0;},"coherent containment cycle rejected");
        for(int which=0;which<6;++which) {
            odh::SavedWorldLimits l;
            if(which==0)l.retained_words=1;
            if(which==1)l.retained_string_bytes=1;
            if(which==2)l.retained_suffix_bytes=1;
            if(which==3)l.world.words=1;
            if(which==4)l.world.entities=3;
            if(which==5)l.relationships.values=2;
            rejected([&]{odh::make_saved_world(fixture(),index,nullptr,l);},"whole candidate budget rejected without publication");
        }
        auto ambiguous=defs;ambiguous.push_back(defs[0]);odh::DefinitionIndex duplicate(ambiguous);
        rejected([&]{odh::make_saved_world(fixture(),duplicate);},"ambiguous instance definition rejected");
        auto missing_master=defs;missing_master.pop_back();odh::DefinitionIndex partial(missing_master);
        auto pending=odh::make_saved_world(fixture(),partial);
        require(!pending->world->find(3)->definition->master_guid && !pending->multitile_groups[0].master_guid,
                "missing unique resource master remains explicit pending identity");
        require(restored->world->find(2)==avatar && restored->world->find(2)->motives[1]==130,"failed candidates do not mutate already published world");
        std::cout<<checks<<" saved-world checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
