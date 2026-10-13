#include "odh/random.hpp"
#include "odh/world.hpp"
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
odh::RoutineKey key(unsigned id = 4096) { return odh::routine_key(static_cast<std::uint16_t>(id)); }
odh::Bhav routine(std::initializer_list<odh::Instruction> instructions) {
    return {0x8002, instructions, {}, odh::Bhav::Metadata{0,4,1,0}};
}
odh::Instruction expr(unsigned op, std::int16_t lhs, std::int16_t rhs,
                      unsigned ls, unsigned rs = 7, unsigned yes = 254) {
    odh::Instruction i{2,static_cast<std::uint8_t>(yes),255,{}};
    const auto l = static_cast<std::uint16_t>(lhs), r = static_cast<std::uint16_t>(rhs);
    i.operands = {static_cast<std::uint8_t>(l),static_cast<std::uint8_t>(l >> 8),
                  static_cast<std::uint8_t>(r),static_cast<std::uint8_t>(r >> 8),
                  0,static_cast<std::uint8_t>(op),static_cast<std::uint8_t>(ls),static_cast<std::uint8_t>(rs)};
    return i;
}
odh::Instruction random(unsigned ds, std::int16_t d, unsigned rs, std::int16_t r) {
    odh::Instruction i{8,254,255,{}};
    const std::array<std::uint16_t,4> words{static_cast<std::uint16_t>(d),
        static_cast<std::uint16_t>(ds),static_cast<std::uint16_t>(r),static_cast<std::uint16_t>(rs)};
    for (std::size_t j=0;j<4;++j) {
        i.operands[2*j]=static_cast<std::uint8_t>(words[j]);
        i.operands[2*j+1]=static_cast<std::uint8_t>(words[j] >> 8);
    }
    return i;
}
void setup(odh::World& w) {
    require(w.create(1,odh::EntityKind::avatar,{0,101,0}),"caller avatar");
    require(w.create(2,odh::EntityKind::avatar,{0,101,0}),"stack avatar");
    require(w.create(3,odh::EntityKind::object),"object without person bank");
}
const odh::VmContext context{1,3,2,77};
void expressions() {
    odh::World w; setup(w);
    // Distinct code owner 77 and callee 3 must never replace caller/stack identity.
    for (const unsigned scope : {30u,31u}) {
        const auto actor=static_cast<std::int16_t>(scope==30?1:2);
        const auto other=static_cast<std::int16_t>(scope==30?2:1);
        for (const std::int16_t index : {std::int16_t{58},std::int16_t{100}}) {
            const std::int16_t reg=index==58?0:19;
            require(w.set_person_data(other,index,1234),"other avatar sentinel");
            for (const std::int16_t value : {std::int16_t{0},std::int16_t{-7},std::int16_t{32767}}) {
                odh::RoutineSet code{{key(),routine({expr(5,reg,value,scope,7,1),
                    expr(5,1,reg,8,scope,2),expr(3,reg,1,scope,7,3),
                    expr(5,2,reg,8,scope)})}};
                odh::Vm vm({},w.vm_memory(),{}, {},{{77,code}});
                vm.start(key(),context); vm.temps()[reg]=index;
                require(vm.run(4)==odh::VmStatus::returned_true,"indirect assignment/read/add execute");
                const auto expected=static_cast<std::int16_t>(value==32767?-32768:value+1);
                require(vm.temps()[1]==value && vm.temps()[2]==expected,
                        "present zero, negative and wrapped person values are readable");
                require(w.find(actor)->person_data[index]==expected &&
                        w.find(other)->person_data[index]==1234,"writes select entity and person word");
                require(w.find(actor)->person_data[reg]==0,"operand is a temp index, not a person index");
                require(vm.temps()[reg]==index,"person output preserves address temp");
            }
        }
    }
    // Same register operand 0 selects word 18, not word 0 or nested scope 18.
    w.set_person_data(1,18,-22); w.set_person_data(1,0,999);
    odh::Vm alias({{key(),routine({expr(5,0,0,31,30)})}},w.vm_memory());
    alias.start(key(),{1,3,2}); alias.temps()[0]=18;
    require(alias.run(1)==odh::VmStatus::returned_true && w.find(2)->person_data[18]==-22 &&
            w.find(2)->person_data[0]==0 && w.find(1)->person_data[0]==999,
            "same address temp resolves caller source and stack destination independently");
}
void callback_contract() {
    odh::World w; setup(w); w.set_person_data(1,58,0); w.set_person_data(2,100,-9);
    const auto base=w.vm_memory(); int reads=0,writes=0;
    odh::VmMemory observed{
        [&](const auto& f, auto scope, auto index)->std::optional<std::int16_t> {
            ++reads;
            require((scope==18 && index==58)||(scope==19 && index==100),"callback receives resolved direct read");
            return base.read(f,scope,index);
        },
        [&](const auto& f, auto scope, auto index, auto value) {
            ++writes; require(scope==19 && index==100,"callback receives resolved direct write");
            return base.write(f,scope,index,value);
        }};
    odh::Vm vm({{key(),routine({expr(2,0,0,30,7,1),expr(5,19,0,31,30)})}},observed);
    vm.start(key(),{1,3,2}); vm.temps()[0]=58; vm.temps()[19]=100;
    require(vm.run(2)==odh::VmStatus::returned_true && reads==2 && writes==1 &&
            w.find(2)->person_data[100]==0,"resolved zero remains present through normal World memory");
}
void rejects() {
    odh::World w; setup(w); w.set_person_data(1,58,71); w.set_person_data(2,58,72);
    const auto before1=w.find(1)->person_data,before2=w.find(2)->person_data;
    for (const unsigned scope : {30u,31u}) {
        for (const auto bad : {std::array<std::int16_t,2>{-1,58},{20,58},{0,-1},{19,101}}) {
            for (const bool source : {false,true}) {
                odh::Vm vm({{key(),routine({source?expr(5,1,bad[0],8,scope):expr(5,bad[0],9,scope)})}},w.vm_memory());
                vm.start(key(),{1,3,2}); vm.temps().fill(58);
                if (bad[0]>=0 && bad[0]<20) vm.temps()[bad[0]]=bad[1];
                const auto temps=vm.temps();
                require(vm.run(1)==odh::VmStatus::fault && vm.stack().size()==1 && vm.stack()[0].pc==0,
                        "invalid address faults at original instruction");
                require(vm.temps()==temps && w.find(1)->person_data==before1 &&
                        w.find(2)->person_data==before2,"invalid read or assignment preserves every person word and temp");
            }
        }
        for (const std::int16_t absent : {std::int16_t{0},std::int16_t{3},std::int16_t{99}}) {
            const odh::VmContext c{static_cast<std::int16_t>(scope==30?absent:1),3,
                                    static_cast<std::int16_t>(scope==31?absent:2)};
            for (const bool source : {false,true}) {
                odh::Vm vm({{key(),routine({source?expr(5,1,0,8,scope):expr(5,0,9,scope)})}},w.vm_memory());
                vm.start(key(),c); vm.temps()[0]=58;
                require(vm.run(1)==odh::VmStatus::fault && w.find(1)->person_data==before1 &&
                        w.find(2)->person_data==before2,"missing or non-avatar actor rejects indirect reads and writes");
            }
        }
    }
}
void child() {
    odh::World w; setup(w); w.set_person_data(1,58,11); w.set_person_data(2,58,22);
    const odh::Instruction call{4097,254,255,{255,255,255,255,255,255,255,255}};
    odh::RoutineSet code{{key(),routine({call})},{key(4097),routine({
        expr(5,1,0,8,30,1),expr(5,2,0,8,31,2),expr(5,0,-33,31)})}};
    odh::Vm vm({},w.vm_memory(),{}, {},{{77,code}});
    vm.start(key(),context); vm.temps()[0]=58;
    require(vm.run(1)==odh::VmStatus::budget_exhausted && vm.stack().size()==2 &&
            vm.stack()[1].context.caller==1 && vm.stack()[1].context.stack_object==2 &&
            vm.stack()[1].context.code_owner==77,"child retains caller, stack object and resource owner");
    require(vm.run(3)==odh::VmStatus::returned_true && vm.temps()[1]==11 && vm.temps()[2]==22 &&
            w.find(1)->person_data[58]==11 && w.find(2)->person_data[58]==-33,
            "nested expressions preserve independent identities");
}
void randomness() {
    // Seed 1 fixed arbitrary-integer oracle: next state 0x2000001,
    // masked multiplication product 0x47e4ce4b896cdd1d. No production RNG oracle.
    constexpr auto product=UINT64_C(0x47e4ce4b896cdd1d);
    for (const unsigned ds : {30u,31u}) for (const unsigned rs : {30u,31u}) {
        for (const std::int16_t range : {std::int16_t{0},std::int16_t{100},std::int16_t{-1}}) {
            odh::World w; setup(w);
            const auto source=static_cast<std::int16_t>(rs==30?1:2);
            const auto destination=static_cast<std::int16_t>(ds==30?1:2);
            w.set_person_data(source,58,range); w.set_person_data(source,0,7);
            odh::RandomState rng(1); int observations=0;
            odh::Vm vm({{key(),routine({random(ds,0,rs,0)})}},w.vm_memory(),{},
                odh::random_primitive(rng,w.vm_memory(),[&](const auto& r) {
                    ++observations; require(r.destination_scope==ds && r.destination_data==0 &&
                        r.range==static_cast<std::uint16_t>(range),"observer preserves encoded indirect operand");
                }));
            vm.start(key(),{1,3,2}); vm.temps()[0]=58;
            const auto bits=range?product%static_cast<std::uint16_t>(range):0;
            const auto expected=static_cast<std::int16_t>(bits<32768?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536);
            require(vm.run(1)==odh::VmStatus::returned_true && w.find(destination)->person_data[58]==expected &&
                    observations==1 && vm.temps()[0]==58,"RNG indirect range and destination alias use person data");
            require(rng.seed()==(range?UINT64_C(0x2000001):UINT64_C(1)),"RNG advances once except zero range");
        }
    }
    for (const unsigned scope : {30u,31u}) for (const bool source : {false,true}) {
        for (const auto bad : {std::array<std::int16_t,2>{-1,58},{20,58},{0,-1},{19,101}}) {
            odh::World w; setup(w); w.set_person_data(1,58,17); w.set_person_data(2,58,29);
            const auto a=w.find(1)->person_data,b=w.find(2)->person_data;
            odh::RandomState rng(1); int observations=0;
            odh::Vm vm({{key(),routine({source?random(8,1,scope,bad[0]):random(scope,bad[0],7,100)})}},w.vm_memory(),{},
                odh::random_primitive(rng,w.vm_memory(),[&](const auto&){++observations;}));
            vm.start(key(),{1,3,2}); vm.temps().fill(58);
            if (bad[0]>=0 && bad[0]<20) vm.temps()[bad[0]]=bad[1];
            const auto temps=vm.temps();
            require(vm.run(1)==odh::VmStatus::fault && rng.seed()==1 && observations==0 &&
                    vm.temps()==temps && w.find(1)->person_data==a && w.find(2)->person_data==b &&
                    vm.stack()[0].pc==0,"invalid indirect RNG range or destination rejects atomically");
        }
        for (const std::int16_t absent : {std::int16_t{0},std::int16_t{3},std::int16_t{99}}) {
            odh::World w; setup(w); w.set_person_data(1,58,17); w.set_person_data(2,58,29);
            const auto a=w.find(1)->person_data,b=w.find(2)->person_data;
            odh::RandomState rng(1); int observations=0;
            odh::Vm vm({{key(),routine({source?random(8,1,scope,0):random(scope,0,7,100)})}},w.vm_memory(),{},
                odh::random_primitive(rng,w.vm_memory(),[&](const auto&){++observations;}));
            vm.start(key(),{static_cast<std::int16_t>(scope==30?absent:1),3,
                            static_cast<std::int16_t>(scope==31?absent:2)}); vm.temps()[0]=58;
            const auto temps=vm.temps();
            require(vm.run(1)==odh::VmStatus::fault && rng.seed()==1 && observations==0 &&
                    vm.temps()==temps && w.find(1)->person_data==a && w.find(2)->person_data==b,
                    "RNG unavailable or non-avatar actor preserves RNG and world");
        }
    }
}
}
int main() {
    try {
        expressions(); callback_contract(); rejects(); child(); randomness();
        std::cout << checks << " person indirect checks passed (synthetic fixtures only)\n";
    } catch (const std::exception& e) {std::cerr << e.what() << '\n'; return 1;}
}
