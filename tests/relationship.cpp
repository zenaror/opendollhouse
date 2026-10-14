#include "odh/relationship.hpp"
#include <iostream>
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
}
int main() {
    try {semantics();variables_and_atomicity();objects_and_limits();actual_vm();std::cout<<"relationship: "<<checks<<" checks passed\n";return 0;}
    catch (const std::exception& e) {std::cerr<<"relationship: "<<e.what()<<'\n';return 1;}
}
