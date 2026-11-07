#include "odh/relationship.hpp"
#include "odh/relationship_snapshot_codec.hpp"
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <new>

namespace { bool fail_allocation=false;int allocation_countdown=-1; }
void* operator new(std::size_t size) {
    if (fail_allocation) {fail_allocation=false;throw std::bad_alloc();}
    if (allocation_countdown==0) {allocation_countdown=-1;throw std::bad_alloc();}
    if (allocation_countdown>0) --allocation_countdown;
    if (auto p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new(std::size_t size,const std::nothrow_t&) noexcept {
    try {return ::operator new(size);}catch (...) {return nullptr;}
}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);}
namespace {
int checks=0;
void require(bool value,const char* message) {++checks;if (!value) throw std::runtime_error(message);}
odh::NeighborEntry entry(std::int16_t id,std::initializer_list<odh::NeighborRelationship> relations={}) {
    odh::NeighborRecord r{};r.version=4;r.neighbor_id=id;r.relationships=relations;
    return {1,r};
}
odh::Nbrs catalog(std::initializer_list<odh::NeighborEntry> entries) {return {0,62,entries,0};}
odh::VmFrame frame(std::int16_t stack=12) {return {odh::routine_key(4096),0,{101,202,stack,99},{-7,52,9},{-7,91,3}};}
odh::Instruction instruction(unsigned mode=0,unsigned set=0,unsigned index=0,unsigned scope=8,std::int16_t data=0,unsigned extra=2,unsigned local=0) {
    const auto d=static_cast<std::uint16_t>(data);
    return {26,254,255,{static_cast<std::uint8_t>(index),static_cast<std::uint8_t>(mode),
        static_cast<std::uint8_t>(extra|((set&1)<<2)|((set&2)<<4)),static_cast<std::uint8_t>(local),
        static_cast<std::uint8_t>(scope),static_cast<std::uint8_t>(scope>>8),static_cast<std::uint8_t>(d),static_cast<std::uint8_t>(d>>8)}};
}
odh::Instruction legacy(unsigned mode,unsigned set,unsigned index=0,unsigned argument=1,unsigned flags=2) {
    return {24,254,255,{static_cast<std::uint8_t>(set),static_cast<std::uint8_t>(index),static_cast<std::uint8_t>(argument),
        static_cast<std::uint8_t>(mode),static_cast<std::uint8_t>(flags),0xFF,0xFF,0xFF}};
}
odh::PrimitiveOutcome dispatch(const odh::VmPrimitive& p,const odh::Instruction& i,const odh::VmFrame& f=frame(),const std::array<std::int16_t,20>& t={}) {
    return p(f,i,t,{nullptr,17});
}
bool yes(const odh::PrimitiveOutcome& o) {return o.disposition==odh::PrimitiveDisposition::goto_true;}
bool unsupported(const odh::PrimitiveOutcome& o) {return o.disposition==odh::PrimitiveDisposition::unsupported && !o.diagnostic.empty();}
void avatar(odh::World& w,std::int16_t id,std::int16_t nid) {
    require(w.create(id,odh::EntityKind::avatar,{0,88,0}),"create avatar");
    require(w.set_person_data(id,31,nid),"set neighbor identity");
}
void neighbor_admission() {
    using M=odh::RelationshipMatrix;
    odh::RelationshipSnapshot saved;saved.sources={{M::neighbor,11,0,{{12,{55,-1,77}}}},
        {M::object,3,90,{{12,{88}}}}};saved.pair_count=2;saved.value_count=4;
    odh::RelationshipState state;require(state.restore_snapshot(saved),"restore old lot neighbor/object relationship owner");
    const auto old=odh::encode_relationship_snapshot(state.snapshot());
    odh::NeighborContext current(catalog({entry(11,{{1,12,{999}}}),entry(3),entry(4,{{1,70000,{0x1234FFFF,65536}}})}));
    require(state.admit_missing_neighbor_sources(current),"admit actual new catalog neighbors to an existing owner");
    require(state.source_count()==4&&state.pair_count()==3&&state.value_count()==6&&
        state.value(11,12,0)==55&&state.value(11,12,1)==-1&&state.value(4,70000,0)==-1&&state.value(4,70000,1)==0,
        "new sources preserve existing progress, raw target keys and source low16 values");
    auto admitted=state.snapshot();
    auto preserved=admitted;preserved.sources.erase(std::remove_if(preserved.sources.begin(),preserved.sources.end(),
        [](const auto& v){return v.matrix==M::neighbor&&(v.id==3||v.id==4);}),preserved.sources.end());
    preserved.pair_count=saved.pair_count;preserved.value_count=saved.value_count;
    require(odh::encode_relationship_snapshot(preserved)==old,"all existing source generations, pairs and tails remain exact");
    const auto bytes=odh::encode_relationship_snapshot(admitted);
    require(state.admit_missing_neighbor_sources(current)&&odh::encode_relationship_snapshot(state.snapshot())==bytes,
        "repeated catalog admission is byte-identical and never re-seeds existing pairs");
    odh::RelationshipState reopened;require(reopened.restore_snapshot(odh::decode_relationship_snapshot(bytes)),"admitted source identities persist in existing RELA codec");
    odh::World world;avatar(world,101,3);auto primitive=reopened.primitive(world);
    require(yes(dispatch(primitive,instruction())),"known-empty admitted neighbor serves its first actual relationship operation");
    require(world.set_person_data(101,31,99),"select actual unknown neighbor source for negative gate");
    require(unsupported(dispatch(primitive,instruction())),"unregistered neighbor remains unsupported; no primitive fallback source creation");
    auto invalid=saved;++invalid.pair_count;odh::RelationshipState failed;
    require(!failed.restore_snapshot(invalid)&&!failed.admit_missing_neighbor_sources(current)&&failed.source_count()==0,
        "failed restore does not establish authority for later neighbor admission");
    for(unsigned kind=0;kind<3;++kind){
        auto limited=saved;
        if(kind==0)limited.limits.sources=2;
        if(kind==1)limited.limits.pairs=2;
        if(kind==2)limited.limits.values=4;
        odh::RelationshipState bounded;require(bounded.restore_snapshot(limited),"restore actual bounded relationship source");
        const auto before=odh::encode_relationship_snapshot(bounded.snapshot());
        require(!bounded.admit_missing_neighbor_sources(current)&&odh::encode_relationship_snapshot(bounded.snapshot())==before,
            "admission overflow rejects the whole batch without changing existing relationships");
    }
    odh::NeighborContext duplicate(catalog({entry(3,{{1,12,{1}},{1,12,{2}}})}));
    odh::RelationshipState unchanged;require(unchanged.restore_snapshot(saved),"restore duplicate/exception test source");
    require(!unchanged.admit_missing_neighbor_sources(duplicate)&&odh::encode_relationship_snapshot(unchanged.snapshot())==old,
        "duplicate new relationship targets leave the entire old state unchanged");
    bool threw=false;fail_allocation=true;
    try{(void)unchanged.admit_missing_neighbor_sources(current);}catch(const std::bad_alloc&){threw=true;}
    require(threw&&odh::encode_relationship_snapshot(unchanged.snapshot())==old,"allocation failure cannot publish a partial neighbor batch");
    require(unchanged.retire_neighbor_identity(11),"apply explicit neighbor retirement before an updated effective catalog");
    odh::NeighborContext filtered(catalog({entry(3),entry(4)}));
    require(unchanged.admit_missing_neighbor_sources(filtered)&&!unchanged.value(11,12,0),
        "effective catalog admission does not reintroduce a logically deleted neighbor");
}
void semantics() {
    auto tail=entry(44,{{1,70000,std::vector<std::int32_t>(300,0x1234FFFF)}});
    odh::NeighborContext n(catalog({entry(11,{{1,12,{55,0,65535,65536,-65537,32768,2147483647}}}),entry(12,{{1,11,{66}},{1,-7,{77}}}),entry(-7,{{1,12,{88}}}),tail}));
    odh::RelationshipState state(n);odh::World w;avatar(w,101,11);
    std::vector<odh::RelationshipRecord> records;
    auto p=state.primitive(w,{},[&](const auto& r){records.push_back(r);});
    for (unsigned mode=0;mode<4;++mode) {
        auto o=dispatch(p,instruction(mode));
        require(yes(o)&&o.temp_write&&o.temp_write->value==static_cast<std::int16_t>(55+11*mode),"four neighbor directions");
        require(records.back().context.code_owner==99 && records.back().mode==mode && records.back().diff_multiplier==1,"observer context and multiplier");
    }
    require(state.source_count()==4&&state.pair_count()==5&&state.value_count()==310,"import counts include preserved tails");
    const auto selected_pairs=state.neighbor_pairs(44);
    require(std::any_of(selected_pairs.begin(),selected_pairs.end(),[](const auto& pair){
        return pair.target==70000&&pair.values.size()==300&&pair.values[299]==-1;
    }),"selected-neighbor UI query preserves raw targets and tails without copying other matrices");
    require(state.neighbor_pairs(32700).empty()&&state.source_count()==4&&state.pair_count()==5&&state.value_count()==310,
        "querying an absent neighbor never creates a relationship source or zero pair");
    require(state.value(44,70000,299)==-1&&!state.value(44,4464,299),"preserve raw int32 keys and unaddressable tails");
    for (const auto& pair : {std::pair<unsigned,std::int16_t>{2,-1},{3,0},{4,-1},{5,-32768},{6,-1}})
        require(state.value(11,12,pair.first)==pair.second,"defined signed low16 conversion");
    require(yes(dispatch(p,instruction(0,1,0,7,140)))&&state.value(11,12,0)==100,"write clamp positive");
    require(yes(dispatch(p,instruction(0,1,0,7,55,130)))&&state.value(11,12,0)==55,"TSO persist bit ignored in TS1 profile");
    require(yes(dispatch(p,instruction(0,1,0,7,-140)))&&state.value(11,12,0)==-100,"write clamp negative");
    require(yes(dispatch(p,instruction(0,1,0,7,32767,66)))&&state.value(11,12,0)==32767,"write unclamped");
    require(yes(dispatch(p,instruction(0,2,0,7,1)))&&state.value(11,12,0)==-100,"increment wraps before clamping");
    require(yes(dispatch(p,instruction(0,1,0,7,-32768,66)))&&yes(dispatch(p,instruction(0,2,0,7,-1,66)))&&state.value(11,12,0)==32767,"unclamped negative wrap");
    require(n.by_id(11)->relationships[0].values[0]==55 && n.by_id(44)->relationships[0].values[299]==0x1234FFFF,"original snapshot untouched");
    const auto before=state.value_count();
    auto grow=dispatch(p,instruction(0,0,255));
    require(yes(grow)&&grow.temp_write->value==0&&state.value_count()==before+249,"read grows to byte index255");
    require(records.back().grown&&!records.back().created,"growth observer");
    require(yes(dispatch(p,instruction(0,3,3,65535,-32768,10),frame(-99)))&&state.value(11,-99,3)==0,"modern no-op ignores wide scope and reserved bit3, allows missing signed target");
    require(!records.back().value_before&&!records.back().value_after&&records.back().created,"no-op does not read values");
    const auto size=state.value_count(), pairs=state.pair_count();
    require(dispatch(p,instruction(0,0,5,65535,0,3),frame(-77)).disposition==odh::PrimitiveDisposition::goto_false,"FailSmall missing pair precedes invalid scope");
    require(dispatch(p,instruction(0,0,255,7,0,3),frame(-99)).disposition==odh::PrimitiveDisposition::goto_false,"FailSmall short list");
    require(state.value_count()==size&&state.pair_count()==pairs,"FailSmall no mutations");
    require(unsupported(dispatch(p,instruction(1,0,0,8,0,3),frame(-77))),"missing neighbor source unsupported not false");
    require(unsupported(dispatch(p,instruction(2,0,0,8,0,2,3))),"bad direction local selector");
    require(unsupported(dispatch(p,instruction(4))),"bad mode unsupported");
    require(unsupported(dispatch(p,instruction(0,0,0,256))),"uint16 scope not truncated");
    require(unsupported(dispatch(p,instruction(0),odh::VmFrame{})),"neighbor caller unavailable");
    auto lf=frame();lf.locals[0]=123;
    require(yes(dispatch(p,legacy(2,1),lf))&&state.value(12,-7,0)==52,"legacy direction uses args0, source value args1");
    require(yes(dispatch(p,legacy(3,0),lf))&&records.back().source==-7,"legacy reverse args selector");
    require(yes(dispatch(p,legacy(0,255,7,255),frame(-88)))&&state.value(11,-88,7)==0&&!records.back().value_after,"legacy raw GetSet>=3 ensure-only");
    require(dispatch(p,odh::Instruction{99,0,0,{}}).disposition==odh::PrimitiveDisposition::unsupported,"unrelated opcode unhandled");
}
void variables_and_atomicity() {
    odh::NeighborContext n(catalog({entry(11,{{1,12,{73}}}),entry(12)}));
    odh::RelationshipState state(n);odh::World w;avatar(w,101,11);avatar(w,12,12);
    auto p=state.primitive(w,w.vm_memory());
    std::array<std::int16_t,20> t{};t[2]=91;t[3]=31;
    for (const auto& pair : {std::pair<unsigned,std::int16_t>{8,91},{9,52},{10,12},{25,91}}) {
        const auto i=static_cast<std::int16_t>(pair.first==10?32767:1);
        if (pair.first==8)t[1]=91;
        require(yes(dispatch(p,instruction(0,1,0,pair.first,i),frame(),t))&&state.value(11,12,0)==pair.second,"VM input banks and stack ignores index");
    }
    t[0]=1;
    require(yes(dispatch(p,instruction(0,1,0,40,0),frame(),t))&&state.value(11,12,0)==91,
        "relationship input resolves LocalByTemp through the active frame");
    const auto indirect_output=dispatch(p,instruction(0,0,0,40,0),frame(),t);
    require(yes(indirect_output)&&indirect_output.write&&
        indirect_output.write->bank==odh::VmFrameBank::locals&&indirect_output.write->index==1&&
        indirect_output.write->value==91,"relationship LocalByTemp output remains a staged frame write");
    require(yes(dispatch(p,instruction(0,1,0,30,3),frame(),t))&&state.value(11,12,0)==11,"indirect my person data input");
    require(yes(dispatch(p,instruction(0,1,0,31,3),frame(),t))&&state.value(11,12,0)==12,"indirect stack person data input");
    auto o=dispatch(p,instruction(0,0,0,9,1));require(yes(o)&&o.write&&o.write->bank==odh::VmFrameBank::arguments&&o.write->index==1,"argument output staged");
    o=dispatch(p,instruction(0,0,0,25,1));require(yes(o)&&o.write->bank==odh::VmFrameBank::locals,"local output staged");
    o=dispatch(p,instruction(0,0,0,10,-1));require(yes(o)&&o.stack_object_write->value==12,"stack identity output staged");
    require(yes(dispatch(p,instruction(0,0,0,30,3),frame(),t))&&w.find(101)->person_data[31]==12,"indirect output external memory");
    require(w.set_person_data(101,31,11),"restore caller nid");
    unsigned reads=0,writes=0;bool accepted=false;
    odh::VmMemory external{
        [&](const odh::VmFrame& f,std::uint8_t scope,std::int16_t index)->std::optional<std::int16_t> {
            ++reads;require(f.context.callee==202&&scope==26&&index==-32768,"external memory preserves context/signed variable");return 20;
        },
        [&](const odh::VmFrame&,std::uint8_t scope,std::int16_t index,std::int16_t value) {
            ++writes;require(scope==19&&index==31&&value==0,"resolved external output gets zero after growth");return accepted;
        }};
    auto ep=state.primitive(w,external);
    require(yes(dispatch(ep,instruction(0,2,0,26,-32768)))&&state.value(11,12,0)==32&&reads==1,"external tuning scope read");
    const auto values=state.value_count(), pairs=state.pair_count();
    require(unsupported(dispatch(ep,instruction(0,0,4,31,3),frame(-3),t))&&writes==1,"external rejected output unsupported");
    require(state.value_count()==values&&state.pair_count()==pairs&&!state.value(11,-3,0),"external rejected output leaves matrix unchanged");
    accepted=true;require(yes(dispatch(ep,instruction(0,0,4,31,3),frame(-3),t))&&state.value(11,-3,4)==0,"external accepted output publishes growth");
    const auto size=state.value_count();
    for (const auto i : {instruction(0,0,5,8,20),instruction(0,0,5,9,-1),instruction(0,0,5,25,3),instruction(0,0,5,7,1),instruction(0,1,5,8,20),instruction(0,0,5,30,20)})
        require(unsupported(dispatch(p,i,frame(-5),t))&&!state.value(11,-5,0)&&state.value_count()==size,"invalid memory fails without allocation/publication");
    auto no_input=state.primitive(w);require(unsupported(dispatch(no_input,instruction(0,1,5,26,0),frame(-5))),"unavailable external input");
    const auto allocation_frame=frame(-4), existing_frame=frame();
    const auto allocation_instruction=instruction(0,0,4,31,3);
    const auto write_count=writes;fail_allocation=true;bool threw=false;
    try {dispatch(ep,allocation_instruction,allocation_frame,t);}catch (const std::bad_alloc&) {threw=true;}
    fail_allocation=false;
    require(threw&&writes==write_count&&!state.value(11,-4,0)&&state.value_count()==size,"allocation exception before external writer leaves matrix unchanged");
    bool completed=false;
    for (int allocation=0;allocation<64;++allocation) {
        const auto old_writes=writes;allocation_countdown=allocation;threw=false;
        try {completed=yes(dispatch(ep,allocation_instruction,allocation_frame,t));}catch (const std::bad_alloc&) {threw=true;}
        allocation_countdown=-1;
        if (completed) break;
        require(threw&&writes==old_writes&&!state.value(11,-4,0)&&state.value_count()==size,"each creation allocation failure is atomic before external writer");
    }
    require(completed&&state.value(11,-4,4)==0,"creation eventually succeeds past injected allocations");
    fail_allocation=true;o=dispatch(no_input,instruction(),existing_frame);
    require(yes(o)&&fail_allocation,"existing read avoids relationship storage allocation");fail_allocation=false;
    fail_allocation=true;threw=false;
    try {dispatch(no_input,instruction(0,1,0,7,99),existing_frame);}catch(const std::bad_alloc&) {threw=true;}
    fail_allocation=false;require(threw&&state.value(11,12,0)==32,"existing write allocation failure preserves value");
}
void objects_and_limits() {
    odh::NeighborContext n(catalog({entry(11),entry(12)}));odh::RelationshipState state(n);odh::World w;
    avatar(w,101,11);avatar(w,102,12);require(w.create(103,odh::EntityKind::object),"create object");
    odh::RelationshipRecord record;auto p=state.primitive(w,{},[&](const auto& r){record=r;});
    require(yes(dispatch(p,instruction(0,1,0,7,45,0),frame(102)))&&state.value(11,12,0)==45&&record.matrix==odh::RelationshipMatrix::neighbor,"avatar IDs auto-select neighbor matrix");
    require(yes(dispatch(p,instruction(0,1,0,7,56,0),frame(103)))&&state.object_value(w,101,103,0)==56&&record.matrix==odh::RelationshipMatrix::object,"object fallback matrix");
    const auto count=state.source_count();
    require(yes(dispatch(p,instruction(0,0,255,65535,0,0),frame(-32768)))&&record.skipped_entities&&state.source_count()==count,"missing actual entity true no-op before memory/budget");
    require(w.remove(101),"remove avatar");avatar(w,101,11);
    require(!state.object_value(w,101,103,0)&&yes(dispatch(p,instruction(0,0,0,8,0,0),frame(103)))&&state.object_value(w,101,103,0)==0,"object source generation prevents identity aliasing");
    require(state.source_count()==count+1,"old object generations remain budgeted");
    auto rejects=[&](odh::NeighborContext& source,odh::RelationshipLimits limit) {
        bool rejected=false;try {odh::RelationshipState candidate(source,limit);}catch (const odh::FormatError&) {rejected=true;}
        require(rejected,"constructor limits reject");
    };
    rejects(n,{1,10,10});
    odh::NeighborContext populated(catalog({entry(11,{{1,12,{1,2}}})}));
    rejects(populated,{1,0,10});rejects(populated,{1,1,1});
    odh::NeighborContext duplicated(catalog({entry(11,{{1,12,{1}},{1,12,{2}}})}));rejects(duplicated,{});
    for (const auto limit : {odh::RelationshipLimits{2,0,10},odh::RelationshipLimits{2,1,0}}) {
        odh::RelationshipState bounded(n,limit);auto bp=bounded.primitive(w);
        require(unsupported(dispatch(bp,instruction()))&&bounded.pair_count()==0&&bounded.value_count()==0,"growth pair/value budget atomic");
    }
    odh::RelationshipState bounded(n,{2,10,10});auto bp=bounded.primitive(w);
    require(unsupported(dispatch(bp,instruction(0,1,0,7,1,0),frame(103)))&&bounded.source_count()==2&&bounded.pair_count()==0,"object source budget atomic");
    odh::RelationshipState full(populated,{1,1,2});auto fp=full.primitive(w);
    require(yes(dispatch(fp,instruction(0,1,0,7,2)))&&full.value(11,12,0)==2,"existing value write permitted at exact budgets");
    require(unsupported(dispatch(fp,instruction(0,0,2)))&&full.value_count()==2&&full.value(11,12,1)==2,"existing pair growth budget atomic");
}
void source_delete_and_body_gc(){
    odh::World world;require(world.create(1,odh::EntityKind::object)&&world.create(2,odh::EntityKind::object)&&
        world.create(3,odh::EntityKind::object),"create actual source relationship identities");
    const auto first=world.find(1)->generation,second=world.find(2)->generation,third=world.find(3)->generation;
    odh::RelationshipSnapshot saved;saved.limits={16,32,64};
    saved.sources={{odh::RelationshipMatrix::object,1,first,{{1,{8}},{2,{7}}}},
        {odh::RelationshipMatrix::object,2,second,{{1,{9}},{3,{13}}}}};
    saved.pair_count=4;saved.value_count=4;
    odh::RelationshipState state;require(state.restore_snapshot(saved),"restore generation-owned outgoing maps");
    const auto lookup=[](const odh::RelationshipSnapshot& snapshot,int id,std::uint64_t generation,int target)->std::optional<std::int16_t>{
        for(const auto& source:snapshot.sources)if(source.matrix==odh::RelationshipMatrix::object&&source.id==id&&source.generation==generation)
            for(const auto& pair:source.pairs)if(pair.target==target&&!pair.values.empty())return pair.values.front();
        return {};
    };
    require(world.mark_dead({1,first})&&world.find(1)&&world.reference_is_dead({1,first})&&
        state.remove_inbound_object_references(world,1,first)&&world.remove_retaining_body({1,first}),
        "end-frame Dead is visible before dictionary removal and source inbound cleanup");
    auto after=state.snapshot();require(!lookup(after,1,first,1)&&!lookup(after,2,second,1)&&lookup(after,1,first,2)==std::optional<std::int16_t>{7},
        "Delete removes self-target inbound but preserves the dead body's outgoing map to other objects");
    require(world.create(1,odh::EntityKind::object)&&world.mark_dead({2,second})&&
        state.remove_inbound_object_references(world,2,second)&&world.remove_retaining_body({2,second})&&world.create(2,odh::EntityKind::object),
        "delete another body after the first numeric ID was reused");
    after=state.snapshot();require(lookup(after,1,first,2)==std::optional<std::int16_t>{7}&&lookup(after,2,second,3)==std::optional<std::int16_t>{13},
        "inbound cleanup consults exact indexed generations and never clears another retained source");
    after.sources.push_back({odh::RelationshipMatrix::object,3,third,{{2,{44}}}});++after.pair_count;++after.value_count;
    odh::RelationshipState fresh;require(fresh.restore_snapshot(after),"create new inbound relation to the replacement numeric ID");
    std::vector<odh::WorldEntityReference> collected;
    require(world.collect_retained_bodies({{1,first}},&collected)&&collected==std::vector<odh::WorldEntityReference>{{2,second}}&&
        fresh.forget_object_source(2,second),"body GC reports and forgets only the collected old source");
    const auto final=fresh.snapshot();require(!lookup(final,2,second,3)&&lookup(final,3,third,2)==std::optional<std::int16_t>{44}&&
        lookup(final,1,first,2)==std::optional<std::int16_t>{7},"GC never repeats numeric inbound cleanup against a replacement");
}
void retire_object_identity() {
    odh::RelationshipSnapshot saved;
    saved.limits={16,32,64};
    saved.sources={
        {odh::RelationshipMatrix::neighbor,11,0,{{101,{7}}}},
        {odh::RelationshipMatrix::object,101,1,{{201,{10,11}},{102,{}}}},
        {odh::RelationshipMatrix::object,101,2,{{202,{20}}}},
        {odh::RelationshipMatrix::object,202,1,{{101,{30,31}},{102,{40}}}},
        {odh::RelationshipMatrix::object,203,1,{{101,{}}}}
    };
    saved.pair_count=7;saved.value_count=7;
    odh::RelationshipState state;
    require(state.restore_snapshot(saved),"restore mixed relationship generations for retirement");
    require(!state.retire_object_identity(0,1)&&!state.retire_object_identity(101,0),
        "invalid retirement identities reject");
    require(state.source_count()==5&&state.pair_count()==7&&state.value_count()==7,
        "invalid retirement leaves all counts unchanged");
    require(state.retire_object_identity(101,1),"retire exact object relationship identity");
    const auto after=state.snapshot();
    require(state.source_count()==4&&state.pair_count()==3&&state.value_count()==3,
        "retirement reclaims source and inbound pair/value budgets including empty pairs");
    require(state.value(11,101,0)==7,"neighbor matrix remains unchanged");
    require(after.sources.size()==4&&after.sources[1].id==101&&after.sources[1].generation==2&&
        after.sources[1].pairs.size()==1&&after.sources[1].pairs[0].target==202,
        "obsolete other-generation source remains intact");
    require(after.sources[2].id==202&&after.sources[2].pairs.size()==1&&
        after.sources[2].pairs[0].target==102&&after.sources[2].pairs[0].values==std::vector<std::int16_t>{40},
        "unrelated outgoing pair survives while inbound retired target is removed");
    require(after.sources[3].id==203&&after.sources[3].pairs.empty(),
        "empty inbound pair is removed but its unrelated source remains");
    const auto repeated=state.retire_object_identity(101,1);
    const auto repeated_snapshot=state.snapshot();
    require(repeated&&repeated_snapshot.sources.size()==after.sources.size()&&
        repeated_snapshot.pair_count==after.pair_count&&repeated_snapshot.value_count==after.value_count&&
        state.pair_count()==3&&state.value_count()==3,"repeated retirement is idempotent");
    odh::RelationshipState restored;
    require(restored.restore_snapshot(after)&&restored.snapshot().pair_count==3&&
        restored.snapshot().value_count==3&&restored.source_count()==4,
        "post-retirement snapshot restores with consistent counts");
}
void actual_vm() {
    odh::NeighborContext n(catalog({entry(11,{{1,12,{55}}}),entry(12)}));
    odh::RelationshipState state(n);odh::World w;avatar(w,101,11);
    const auto key=odh::routine_key(4096);
    for (const auto scope : {8U,9U,25U,10U}) {
        odh::Bhav routine{0x8002,{instruction(0,0,0,scope)}, {},odh::Bhav::Metadata{0,2,0,0}};
        odh::Vm vm({{key,routine}},{},{},state.primitive(w));
        require(vm.start(key,{101,202,12,0},{0,0})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::returned_true,"actual VM accepts staged memory/context outputs");
        if (scope==8) require(vm.temps()[0]==55,"actual VM temp read result");
    }
    odh::Bhav routine{0x8002,{legacy(0,1)}, {},odh::Bhav::Metadata{0,2,0,0}};
    odh::Vm vm({{key,routine}},{},{},state.primitive(w));
    require(vm.start(key,{101,202,12,0},{0,92})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::returned_true&&state.value(11,12,0)==92,"actual legacy VM mutation");
}
void retire_neighbor_identity() {
    using M=odh::RelationshipMatrix;
    odh::RelationshipSnapshot snapshot;
    snapshot.sources={{M::neighbor,41,0,{{42,{60,70}},{43,{90}}}},
        {M::neighbor,42,0,{{41,{10,20}},{43,{30}}}},
        {M::object,41,1,{{41,{50}}}}};
    snapshot.pair_count=5;snapshot.value_count=7;
    odh::RelationshipState state;
    require(state.restore_snapshot(snapshot),"restore saved neighbor and object relations");
    require(!state.retire_neighbor_identity(0)&&!state.retire_neighbor_identity(-1),
        "invalid retired neighbor IDs reject");
    require(state.retire_neighbor_identity(41),"retire deleted neighbor");
    const auto cleaned=state.snapshot();
    require(cleaned.sources.size()==2&&cleaned.pair_count==2&&cleaned.value_count==2,
        "retirement counts remove outgoing and inbound neighbor values");
    require(!state.value(41,42,0)&&!state.value(42,41,0)&&state.value(42,43,0)==30,
        "deleted Sim references removed while unrelated relationship progress survives");
    const auto object=std::find_if(cleaned.sources.begin(),cleaned.sources.end(),[](const auto& src){return src.matrix==M::object;});
    require(object!=cleaned.sources.end()&&object->pairs.size()==1&&object->pairs[0].target==41&&object->pairs[0].values[0]==50,
        "numeric object identity does not become a deleted neighbor identity");
    require(state.retire_neighbor_identity(41)&&state.pair_count()==2&&state.value_count()==2,
        "neighbor retirement is idempotent");
    odh::RelationshipState reopened;
    require(reopened.restore_snapshot(cleaned)&&reopened.value(42,43,0)==30,
        "cleaned snapshot reopens with surviving progress");
}
}
int main() {
    try {neighbor_admission();source_delete_and_body_gc();semantics();variables_and_atomicity();objects_and_limits();retire_object_identity();retire_neighbor_identity();actual_vm();std::cout<<"relationship: "<<checks<<" checks passed\n";return 0;}
    catch (const std::exception& e) {std::cerr<<"relationship: "<<e.what()<<'\n';return 1;}
}
