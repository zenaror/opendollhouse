#include "odh/vm.hpp"

#include <iostream>
#include <sstream>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
odh::Instruction expr(unsigned op, int lhs, int rhs, unsigned ls = 8, unsigned rs = 7,
                      unsigned yes = 254, unsigned no = 255) {
    odh::Instruction i{2, static_cast<std::uint8_t>(yes), static_cast<std::uint8_t>(no), {}};
    i.operands = {static_cast<std::uint8_t>(lhs & 255), static_cast<std::uint8_t>((lhs >> 8) & 255),
                  static_cast<std::uint8_t>(rhs & 255), static_cast<std::uint8_t>((rhs >> 8) & 255),
                  0, static_cast<std::uint8_t>(op), static_cast<std::uint8_t>(ls), static_cast<std::uint8_t>(rs)};
    return i;
}
odh::Bhav routine(std::initializer_list<odh::Instruction> instructions, unsigned locals = 0,
                  unsigned args = 4) {
    return {0x8002, instructions, {}, odh::Bhav::Metadata{0, static_cast<std::uint8_t>(args),
                                       static_cast<std::uint16_t>(locals), 0}};
}
odh::RoutineKey key(unsigned id = 4096) { return odh::routine_key(static_cast<std::uint16_t>(id)); }
odh::Vm machine(odh::Bhav r) { return odh::Vm({{key(), std::move(r)}}); }
odh::Instruction call(unsigned id, unsigned yes = 254, unsigned no = 255) {
    return {static_cast<std::uint16_t>(id), static_cast<std::uint8_t>(yes),
            static_cast<std::uint8_t>(no), {255,255,255,255,255,255,255,255}};
}
}
int main() {
    try {
        // Independently specified byte header exercises format -> VM metadata.
        const std::string bytes = std::string("\x02\x80\x01\x00\x16\x02\x03\x00\x04\x00\xAA\xBB",12) +
            std::string("\x02\x00\xFE\xFF\x00\x00\x01\x00\x00\x05\x08\x07",12);
        std::istringstream stream(bytes);
        const auto parsed = odh::read_bhav(odh::View(stream, bytes.size()));
        require(parsed.metadata && parsed.metadata->arguments == 2 && parsed.metadata->locals == 3 &&
                parsed.metadata->type == 22 && parsed.metadata->tree_version == 4 && parsed.header[7] == 187,
                "BHAV header decode and unknown bytes preserved");
        auto vm = machine(parsed); vm.start(key());
        require(vm.run(1) == odh::VmStatus::returned_true && vm.temps()[0] == 1, "parsed expression executes");

        for (const auto pair : {std::pair<unsigned,int>{0,0}, {1,1}, {2,0}, {14,0}, {15,1}, {16,1}}) {
            auto m = machine(routine({expr(pair.first, -2, -1, 7, 7)})); m.start(key());
            require(m.run(1) == (pair.second ? odh::VmStatus::returned_true : odh::VmStatus::returned_false),
                    "signed comparisons");
        }
        for (const auto spec : {std::array<int,4>{3,32767,1,-32768}, {4,-32768,1,32767},
             {6,-32768,-1,-32768}, {7,-7,2,-3}, {12,-7,3,2}, {13,7,3,3},
             {18,4,2,6}, {19,7,3,4}, {9,0,16,-32768}, {10,-1,16,32767}}) {
            auto m = machine(routine({expr(static_cast<unsigned>(spec[0]),0,spec[2])}));
            m.start(key()); m.temps()[0] = static_cast<std::int16_t>(spec[1]);
            require(m.run(1) == odh::VmStatus::returned_true && m.temps()[0] == spec[3], "defined 16-bit arithmetic");
        }
        auto flag = machine(routine({expr(8,0,16)})); flag.start(key()); flag.temps()[0] = -32768;
        require(flag.run(1) == odh::VmStatus::returned_true, "high-bit flag check");
        for (const auto op : {7,12}) {
            auto m = machine(routine({expr(static_cast<unsigned>(op),0,0)})); m.start(key());
            require(m.run(1) == odh::VmStatus::fault && !m.stack().empty() && m.stack()[0].pc == 0,
                    "zero divisor faults with location preserved");
        }
        // Run slices retain state; this is an instruction budget, not a simulated tick.
        auto loop = machine(routine({expr(5,0,0,8,7,1),expr(11,0,3,8,7,1,254)})); loop.start(key());
        require(loop.run(0) == odh::VmStatus::budget_exhausted && loop.steps() == 0, "zero budget");
        require(loop.run(2) == odh::VmStatus::budget_exhausted && loop.temps()[0] == 1, "resumable loop budget");
        require(loop.run(2) == odh::VmStatus::returned_true && loop.temps()[0] == 3 && loop.steps() == 4,
                "resume neither restarts nor skips instructions");
        require(loop.run(2) == odh::VmStatus::returned_true && loop.steps() == 4, "terminal run stable");
        auto alias = machine(routine({expr(11,0,0,8,8)})); alias.start(key());
        require(alias.run(1) == odh::VmStatus::returned_false && alias.temps()[0] == 1, "rhs read after increment");

        // Nested private/global/semiglobal calls, argument substitution and local isolation.
        odh::Vm nested({{key(), routine({call(256,1), expr(2,0,0,25,7,2), expr(2,0,42)},1)},
                       {key(256), routine({expr(5,0,99,25,7,1),call(8192,2),expr(2,0,99,25,7)},1)},
                       {key(8192), routine({expr(5,0,0,8,9)},1)}});
        nested.start(key()); nested.temps()[0] = 42;
        std::vector<odh::VmTrace> trace;
        require(nested.run(20,[&](const auto& e){trace.push_back(e);}) == odh::VmStatus::returned_true,
                "nested returns and temp substitution");
        require(trace.size() == 7 && trace[2].depth == 2 && trace[3].depth == 3 && nested.temps()[0] == 42,
                "trace preserves call site/depth");
        auto literal_call = call(4097); literal_call.operands = {255,255,0,0,0,0,0,0};
        odh::Vm literal({{key(),routine({literal_call})},{key(4097),routine({expr(2,0,-1,9,7)})}});
        literal.start(key()); literal.temps()[0] = 42;
        require(literal.run(3) == odh::VmStatus::returned_true, "-1 literal when args1..3 are zero");
        odh::Vm false_call({{key(),routine({call(4097)})},{key(4097),routine({expr(2,1,2,7,7)})}});
        false_call.start(key()); require(false_call.run(3)==odh::VmStatus::returned_false,"false unwinds parent");
        auto fd = machine(routine({expr(2,0,1,7,7,254,253)})); fd.start(key());
        require(fd.run(1) == odh::VmStatus::returned_true, "FD uses sole defined edge (reference profile)");
        auto fd_error = machine(routine({expr(2,0,1,7,7,253,253)})); fd_error.start(key());
        require(fd_error.run(1) == odh::VmStatus::fault, "two FD targets fault");

        for (const auto bad : {expr(2,0,0,16,7),expr(5,0,0,7,7),expr(2,-1,0),expr(2,20,0),
                               expr(2,4,0,25,7),expr(20,0,0),expr(9,0,17)}) {
            auto m = machine(routine({bad})); m.start(key());
            require(m.run(1) == odh::VmStatus::fault && !m.diagnostic().empty(), "unsupported scope/index/operator");
        }
        auto missing = machine(routine({call(4097)})); missing.start(key());
        require(missing.run(1) == odh::VmStatus::fault && missing.stack().size()==1,"missing call not success");
        auto primitive = machine(routine({{49,254,255,{}}})); primitive.start(key());
        require(primitive.run(1) == odh::VmStatus::fault, "unknown primitive not success");
        auto invalid = machine(routine({expr(2,0,0,7,7,1)}));
        require(invalid.start(key()) == odh::VmStatus::fault, "invalid branch rejected at start");
        auto legacy_bhav = routine({expr(2,0,0,7,7)}); legacy_bhav.version=0x8001; legacy_bhav.metadata.reset();
        auto legacy = machine(legacy_bhav); require(legacy.start(key())==odh::VmStatus::fault,"legacy frame not guessed");
        auto empty = machine(routine({})); require(empty.start(key())==odh::VmStatus::fault,"empty routine not success");
        odh::Vm recursive({{key(),routine({call(4096)})}}, {}, {3,4096}); recursive.start(key());
        require(recursive.run(10)==odh::VmStatus::fault && recursive.stack().size()==3,"recursion bounded");
        odh::Vm huge({{key(),routine({expr(2,0,0,7,7)},4097)}});
        require(huge.start(key())==odh::VmStatus::fault,"frame allocation bounded");

        // World reads must be supplied; an explicit zero is distinct from missing.
        int reads = 0;
        odh::Vm world({{key(),routine({expr(2,0,0,16,7)})}},
            {[&](const auto& f,unsigned scope,int index)->std::optional<std::int16_t> {
                ++reads; require(f.context.caller==12 && f.context.stack_object==34,"world context identity");
                return scope==16 && index==0 ? std::optional<std::int16_t>(0) : std::nullopt;
            }, {}});
        world.start(key(),{12,34,34});
        require(world.run(1)==odh::VmStatus::returned_true && reads==1,"explicit zero world value");
        std::int16_t written = 0;
        odh::Vm world_write({{key(),routine({expr(5,0,17,18,7)})}},
            {{},[&](const auto&,unsigned scope,int index,std::int16_t value) {
                if (scope!=18 || index!=0) return false;
                written=value; return true;
            }});
        world_write.start(key());
        require(world_write.run(1)==odh::VmStatus::returned_true && written==17,"explicit world write");
        auto reentrant = machine(routine({expr(2,0,0,7,7)})); reentrant.start(key());
        bool rejected = false;
        try { reentrant.run(1,[&](const auto&){reentrant.start(key());}); }
        catch (const std::logic_error&) { rejected = true; }
        require(rejected && reentrant.stack().size()==1 && reentrant.steps()==0,"reentry cannot invalidate live frame");
        require(reentrant.run(1)==odh::VmStatus::returned_true,"callback exception releases execution guard");
        auto dec = machine(routine({expr(17,0,1)})); dec.start(key()); dec.temps()[0]=2;
        require(dec.run(1)==odh::VmStatus::returned_false && dec.temps()[0]==1,"decrement comparison");
        auto set_stack = machine(routine({expr(5,0,42,10,7,1),expr(2,0,42,10,7)}));
        set_stack.start(key(),{1,2,2});
        require(set_stack.run(2)==odh::VmStatus::returned_true,"stack object context read/write");
        std::cout << checks << " VM checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "VM test failed: " << e.what() << '\n'; return 1; }
}
