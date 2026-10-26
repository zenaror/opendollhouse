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
odh::Vm output_machine(odh::PrimitiveOutcome outcome) {
    return odh::Vm({{key(), routine({{1,1,2,{}},expr(2,1,1,7,7),expr(2,1,2,7,7)},1)}},
        {}, {}, [outcome](const auto&,const auto&,const auto&,const auto&) { return outcome; });
}
void rejects_outputs(odh::PrimitiveOutcome outcome) {
    auto vm = output_machine(outcome);
    vm.start(key(),{11,22,33},{17});
    for (std::size_t i=0;i<vm.temps().size();++i) vm.temps()[i]=static_cast<std::int16_t>(100+i);
    const auto before=vm.temps();
    const auto arguments=vm.stack()[0].arguments;
    const auto locals=vm.stack()[0].locals;
    require(vm.run(1)==odh::VmStatus::fault,"invalid paired output faults");
    require(vm.temps()==before,"invalid paired output preserves every temp");
    require(vm.stack().size()==1 && vm.stack()[0].pc==0 &&
            vm.stack()[0].arguments==arguments && vm.stack()[0].locals==locals,
            "invalid paired output preserves frame and branch");
    require(vm.stack()[0].context.caller==11 && vm.stack()[0].context.callee==22 &&
            vm.stack()[0].context.stack_object==33 && vm.stack()[0].context.code_owner==0,
            "invalid paired output preserves all context identities");
    require(!vm.pending(),"invalid paired output does not publish wait or child");
}
}
int main() {
    try {
        {
            odh::Vm owners{{{key(),routine({expr(2,0,0)})},
                            {key(256),routine({expr(2,0,0)})}}, {}, {64,4096,3}};
            require(owners.has_routine(key(),0)&&!owners.has_routine(key(4097),0)&&
                    !owners.has_routine(key(),11)&&!owners.has_routine(key(256),11),
                    "routine availability rejects missing roots and missing owners, even for globals");
            require(owners.install_owner(11,{{key(4097),routine({expr(2,0,0)})}})&&
                    owners.has_routine(key(4097),11)&&owners.has_routine(key(256),11)&&
                    !owners.has_routine(key(),11)&&!owners.has_routine(key(4097),0),
                    "routine availability resolves owner-local roots and shared globals without default fallback");
            owners.start(key());const auto before=owners.snapshot();
            require(owners.has_routine(key(4097),11)&&!owners.has_routine(key(),99)&&
                    owners.status()==before.status&&owners.steps()==before.steps&&
                    owners.run_generation()==before.run_generation&&owners.stack().size()==before.stack.size()&&
                    owners.stack().front().pc==before.stack.front().pc&&
                    !owners.install_owner(11,{{key(),routine({expr(2,0,0)})}}),
                    "availability queries preserve active VM state and installed owners remain immutable");
        }

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

        std::optional<std::uint16_t> read_flag;
        odh::VmMemory sprite_memory;
        sprite_memory.dynamic_sprite_flag=[&](const odh::VmFrame&,std::uint16_t flag)->std::optional<bool>{
            read_flag=flag;return flag==5;
        };
        odh::Vm sprite_read({{key(),routine({expr(2,0,1,27,7)})}},sprite_memory);
        sprite_read.start(key());sprite_read.temps()[0]=5;
        require(sprite_read.run(1)==odh::VmStatus::returned_true&&read_flag==5,
                "scope 27 reads the sprite flag selected by the Temp register");
        std::optional<std::pair<std::uint16_t,bool>> written_flag;
        sprite_memory.set_dynamic_sprite_flag=[&](const odh::VmFrame&,std::uint16_t flag,bool enabled){
            written_flag=std::make_pair(flag,enabled);return true;
        };
        odh::Vm sprite_write({{key(),routine({expr(5,0,1,27,7)})}},sprite_memory);
        sprite_write.start(key());sprite_write.temps()[0]=69;
        require(sprite_write.run(1)==odh::VmStatus::returned_true&&written_flag&&
                written_flag->first==69&&written_flag->second,
                "scope 27 writes the selected sprite flag using the Temp register value");

        auto restored = machine(routine({expr(2,0,37,9,7)},2));
        odh::VmFrame saved_frame{key(),0,{12,13,14,0},{37},{44},
                                 odh::VmCallReturnMode::ordinary,0};
        std::array<std::int16_t,20> saved_temps{};
        saved_temps[0]=77;
        restored.restore_dormant_state({saved_frame},saved_temps);
        require(restored.status()==odh::VmStatus::dormant && restored.steps()==0 &&
                restored.stack().size()==1 && restored.stack()[0].pc==0 &&
                restored.stack()[0].arguments.size()==4 && restored.stack()[0].arguments[0]==37 &&
                restored.stack()[0].locals.size()==4 && restored.stack()[0].locals[0]==44 &&
                restored.temps()==saved_temps && restored.stack()[0].context.caller==12 &&
                restored.stack()[0].context.callee==13 && restored.stack()[0].context.stack_object==14,
                "saved VM state loads exactly and stays dormant with padded variables");
        require(restored.run(1)==odh::VmStatus::dormant && restored.steps()==0,
                "dormant VM does not execute before explicit activation");
        std::vector<odh::VmTrace> restored_trace;
        require(restored.activate_dormant_state() &&
                restored.run(1,[&](const auto& trace){restored_trace.push_back(trace);})==odh::VmStatus::returned_true &&
                restored_trace.size()==1 && restored_trace[0].key.id==key().id && restored_trace[0].pc==0,
                "explicit activation begins at the saved routine and address");

        odh::Vm restored_stack({{key(),routine({call(4097,1),expr(2,0,37,9,7)})},
                                {key(4097),routine({expr(2,0,37,9,7)})}});
        auto saved_parent=saved_frame; saved_parent.arguments={5}; saved_parent.locals={55};
        auto saved_child=saved_frame; saved_child.key=key(4097); saved_child.arguments={37}; saved_child.locals={66};
        restored_stack.restore_dormant_state({saved_parent,saved_child},saved_temps);
        std::vector<odh::VmTrace> stack_trace;
        require(restored_stack.activate_dormant_state() &&
                restored_stack.run(1,[&](const auto& trace){stack_trace.push_back(trace);})==odh::VmStatus::budget_exhausted &&
                stack_trace.size()==1 && stack_trace[0].key.id==4097 && stack_trace[0].pc==0 &&
                restored_stack.stack().size()==1 && restored_stack.stack()[0].key.id==4096 &&
                restored_stack.stack()[0].pc==1,
                "restored two-frame stack runs its saved top frame then returns to its saved parent");

        std::optional<odh::VmContext> temporary_effect_context;
        odh::Vm temporary({{key(),routine({expr(2,0,37,9,7)})},
                           {key(4097),routine({expr(5,1,0,8,9)})},
                           {key(4098),routine({expr(16,0,0,9,7)})},
                           {key(4099),routine({expr(5,2,7,8,7,1),expr(2,0,0,7,7)})},
                           {key(4100),routine({{49,254,255,{}}})}}, {}, {},
            [&](const auto& frame,const auto& instruction,const auto&,const auto&) {
                if(instruction.opcode!=49)return odh::PrimitiveOutcome{};
                temporary_effect_context=frame.context;
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            });
        temporary.restore_dormant_state({saved_frame},saved_temps);
        temporary.temp_xl()[0]=123456;
        const auto suspended_main=temporary.snapshot();
        const auto child_true=temporary.run_subroutine_in_temporary_stack(
            key(4097),{14,14,14,0},{55,66,77,88},8);
        require(child_true.started&&child_true.status==odh::VmStatus::returned_true&&
                child_true.steps==1&&child_true.diagnostic.empty(),
                "temporary named tree reports synchronous true and its bounded step count");
        require(temporary.status()==suspended_main.status&&temporary.steps()==suspended_main.steps&&
                temporary.stack().size()==suspended_main.stack.size()&&
                temporary.stack()[0].pc==suspended_main.stack[0].pc&&
                temporary.stack()[0].arguments==suspended_main.stack[0].arguments&&
                temporary.stack()[0].locals==suspended_main.stack[0].locals&&
                temporary.stack()[0].context.caller==suspended_main.stack[0].context.caller&&
                temporary.stack()[0].context.callee==suspended_main.stack[0].context.callee&&
                temporary.stack()[0].context.stack_object==suspended_main.stack[0].context.stack_object&&
                temporary.run_generation()==suspended_main.run_generation,
                "temporary named tree restores the suspended Main frame and status exactly");
        require(temporary.temps()[0]==saved_temps[0]&&temporary.temps()[1]==55&&
                temporary.temp_xl()[0]==123456,
                "temporary named tree receives caller arguments while retaining target thread registers");

        const odh::Instruction sleep_wait{0,254,255,{0,0,0,0,0,0,0,0}};
        odh::Vm sleeping_main({{key(),routine({sleep_wait})},
                               {key(4097),routine({expr(2,0,0,7,7)})}}, {}, {},
            [](const auto&,const auto& instruction,const auto&,const auto&){
                return instruction.opcode==0?odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}}:
                                             odh::PrimitiveOutcome{};
            });
        sleeping_main.restore_dormant_state({saved_frame},saved_temps);
        require(sleeping_main.activate_dormant_state()&&
                sleeping_main.run(1)==odh::VmStatus::waiting&&sleeping_main.pending()&&
                sleeping_main.pending()->instruction.instruction.opcode==0,
                "restored Main reaches its scheduled Sleep wait");
        const auto sleeping_main_before=sleeping_main.snapshot();
        const auto child_during_sleep=sleeping_main.run_subroutine_in_temporary_stack(
            key(4097),{14,14,14,0},{},8);
        require(child_during_sleep.started&&child_during_sleep.status==odh::VmStatus::returned_true&&
                sleeping_main.status()==odh::VmStatus::waiting&&sleeping_main.pending()&&
                sleeping_main.pending()->ticket==sleeping_main_before.pending->ticket&&
                sleeping_main.stack().size()==sleeping_main_before.stack.size()&&
                sleeping_main.stack()[0].pc==sleeping_main_before.stack[0].pc&&
                sleeping_main.steps()==sleeping_main_before.steps,
                "temporary named tree runs over an idle Sleep and restores its pending Main wait");

        const odh::Instruction other_wait{36,254,255,{}};
        odh::Vm non_sleeping_main({{key(),routine({other_wait})},
                                   {key(4097),routine({expr(2,0,0,7,7)})}}, {}, {},
            [](const auto&,const auto& instruction,const auto&,const auto&){
                return instruction.opcode==36?odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}}:
                                              odh::PrimitiveOutcome{};
            });
        non_sleeping_main.restore_dormant_state({saved_frame},saved_temps);
        require(non_sleeping_main.activate_dormant_state()&&
                non_sleeping_main.run(1)==odh::VmStatus::waiting,
                "non-sleeping Main fixture enters its private dialog wait");
        const auto rejected_during_dialog=non_sleeping_main.run_subroutine_in_temporary_stack(
            key(4097),{14,14,14,0},{},8);
        require(!rejected_during_dialog.started&&non_sleeping_main.status()==odh::VmStatus::waiting&&
                non_sleeping_main.pending()&&non_sleeping_main.pending()->instruction.instruction.opcode==36,
                "temporary named tree does not hide a player-facing dialog wait");

        const auto child_false=temporary.run_subroutine_in_temporary_stack(
            key(4098),{14,14,14,0},{0,0,0,0},8);
        require(child_false.started&&child_false.status==odh::VmStatus::returned_false&&
                temporary.status()==suspended_main.status&&temporary.stack()[0].pc==suspended_main.stack[0].pc,
                "temporary named tree returns false without advancing Main");
        const auto child_notify=temporary.run_subroutine_in_temporary_stack(
            key(4100),{31,32,33,0},{},8);
        require(child_notify.started&&child_notify.status==odh::VmStatus::returned_true&&
                temporary_effect_context&&temporary_effect_context->caller==31&&
                temporary_effect_context->callee==32&&temporary_effect_context->stack_object==33,
                "temporary named tree sends opcode effects through the target thread context");
        const auto child_budget=temporary.run_subroutine_in_temporary_stack(
            key(4099),{14,14,14,0},{},1);
        require(child_budget.started&&child_budget.status==odh::VmStatus::budget_exhausted&&
                child_budget.steps==1&&temporary.status()==suspended_main.status&&
                temporary.stack().size()==suspended_main.stack.size()&&temporary.stack()[0].pc==0&&
                temporary.temps()[2]==7,
                "budgeted child restores Main even when its own tree does not finish");
        const auto missing_temporary=temporary.run_subroutine_in_temporary_stack(
            key(4101),{14,14,14,0},{},8);
        require(!missing_temporary.started&&missing_temporary.status==odh::VmStatus::fault&&
                temporary.status()==suspended_main.status&&temporary.stack()[0].pc==0,
                "missing temporary routine leaves the suspended Main untouched");

        odh::RoutineOwners restored_owners{{42,{{key(),routine({expr(2,0,37,9,7)})}}}};
        odh::Vm restored_owner({{key(0),routine({expr(2,0,0)})}}, {}, {}, {},
                               std::move(restored_owners));
        auto owner_frame=saved_frame; owner_frame.context.code_owner=42;
        restored_owner.restore_dormant_state({owner_frame},saved_temps);
        require(restored_owner.status()==odh::VmStatus::dormant &&
                restored_owner.stack()[0].context.code_owner==42,
                "restore resolves a private routine through its installed code owner");

        auto atomic_restore=machine(routine({expr(2,0,1)}));
        atomic_restore.start(key());
        const auto old_status=atomic_restore.status();
        const auto old_stack=atomic_restore.stack();
        const auto old_temps=atomic_restore.temps();
        const auto old_generation=atomic_restore.run_generation();
        auto bad_frame=saved_frame; bad_frame.pc=99;
        bool restore_rejected=false;
        try { atomic_restore.restore_dormant_state({bad_frame},saved_temps); }
        catch (const std::invalid_argument&) { restore_rejected=true; }
        require(restore_rejected && atomic_restore.status()==old_status &&
                atomic_restore.stack().size()==old_stack.size() &&
                atomic_restore.stack()[0].pc==old_stack[0].pc &&
                atomic_restore.temps()==old_temps && atomic_restore.run_generation()==old_generation,
                "invalid restore rejects atomically and preserves active VM state");
        odh::Vm restore_over_limit({{key(),routine({expr(2,0,1)},4097)}}, {}, {64,4096,64});
        bool metadata_limit_rejected=false;
        try { restore_over_limit.restore_dormant_state({saved_frame},saved_temps); }
        catch (const std::invalid_argument&) { metadata_limit_rejected=true; }
        require(metadata_limit_rejected && restore_over_limit.status()==odh::VmStatus::ready &&
                restore_over_limit.stack().empty(),
                "restore rejects metadata that would exceed the frame variable limit");

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
            require(m.run(1) == odh::VmStatus::returned_true && m.stack().empty() &&
                    m.temps()[0] == (op == 7 ? -1 : 0),
                    "pinned TS1 zero divisor result");
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

        for (const auto bad : {expr(2,0,0,16,7),expr(2,-1,0),expr(2,20,0),
                               expr(2,4,0,25,7),expr(21,0,0)}) {
            auto m = machine(routine({bad})); m.start(key());
            require(m.run(1) == odh::VmStatus::fault && !m.diagnostic().empty(), "unsupported scope/index/operator");
        }
        auto read_only_literal = machine(routine({expr(5,0,0,7,7)}));
        read_only_literal.start(key());
        require(read_only_literal.run(1) == odh::VmStatus::returned_false,
                "literal setter is a known rejection");
        auto square_root = machine(routine({expr(20,0,81)})); square_root.start(key());
        require(square_root.run(1) == odh::VmStatus::returned_true && square_root.temps()[0] == 9,
                "pinned TS1 sqrt operator");
        auto wide_flag = machine(routine({expr(9,0,17)})); wide_flag.start(key());
        require(wide_flag.run(1) == odh::VmStatus::returned_true && wide_flag.temps()[0] == 0,
                "flag mask is 32-bit before narrow store");
        auto missing = machine(routine({call(4097)})); missing.start(key());
        require(missing.run(1) == odh::VmStatus::fault && missing.stack().size()==1,"missing call not success");
        auto primitive = machine(routine({{49,254,255,{}}})); primitive.start(key());
        require(primitive.run(1) == odh::VmStatus::fault, "unknown primitive not success");
        auto invalid = machine(routine({expr(2,0,0,7,7,1)}));
        require(invalid.start(key()) == odh::VmStatus::fault, "invalid branch rejected at start");
        auto legacy_bhav = routine({expr(2,0,0,7,7)}); legacy_bhav.version=0x8001; legacy_bhav.metadata.reset();
        auto legacy = machine(legacy_bhav);
        require(legacy.start(key())==odh::VmStatus::running && legacy.stack()[0].arguments.size()==4 &&
                legacy.stack()[0].locals.size()==4,"legacy frame uses bounded four-argument/no-local profile");
        require(legacy.run(1)==odh::VmStatus::returned_true,"8001 BHAV executes under legacy frame profile");
        auto called_legacy_bhav=routine({expr(2,0,42,9,7)});called_legacy_bhav.version=0x8001;called_legacy_bhav.metadata.reset();
        odh::Vm legacy_call({{key(),routine({call(4097)})},{key(4097),called_legacy_bhav}});
        legacy_call.start(key());legacy_call.temps()[0]=42;
        require(legacy_call.run(3)==odh::VmStatus::returned_true,"8002 caller can invoke a four-argument 8001 BHAV");
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

        // Primitive outputs publish as one validated synchronous operation. The second
        // temp is applied last when both outputs address the same register.
        for (const auto disposition : {odh::PrimitiveDisposition::goto_true,
                                       odh::PrimitiveDisposition::goto_false}) {
            for (const bool aliased : {false,true}) {
                odh::PrimitiveOutcome output{disposition,{}};
                output.write=odh::VmFrameWrite{odh::VmFrameBank::arguments,0,99};
                output.temp_write=odh::VmTempWrite{0,0};
                output.second_temp_write=odh::VmTempWrite{aliased ? 0u : 19u,-32768};
                output.stack_object_write=odh::VmStackObjectWrite{-42};
                auto vm=output_machine(output); vm.start(key(),{11,22,33},{17});
                vm.temps().fill(73);
                auto expected=vm.temps(); expected[0]=0;
                expected[aliased ? 0 : 19]=-32768;
                require(vm.run(1)==odh::VmStatus::budget_exhausted && vm.temps()==expected &&
                        vm.stack()[0].arguments[0]==99 && vm.stack()[0].context.stack_object==-42 &&
                        vm.stack()[0].context.caller==11 && vm.stack()[0].context.callee==22,
                        "paired output publishes frame, signed temps and stack object together");
                require(vm.stack()[0].pc==(disposition==odh::PrimitiveDisposition::goto_true ? 1 : 2),
                        "paired output takes requested branch");
                require(vm.run(1)==(disposition==odh::PrimitiveDisposition::goto_true ?
                        odh::VmStatus::returned_true : odh::VmStatus::returned_false),
                        "paired output preserves true and false returns");
            }
        }
        odh::PrimitiveOutcome paired{odh::PrimitiveDisposition::goto_true,{}};
        paired.write=odh::VmFrameWrite{odh::VmFrameBank::locals,0,99};
        paired.temp_write=odh::VmTempWrite{20,9};
        paired.second_temp_write=odh::VmTempWrite{19,-7};
        paired.stack_object_write=odh::VmStackObjectWrite{-42};
        rejects_outputs(paired); // Invalid first temp must not publish a valid second temp.
        paired.temp_write=odh::VmTempWrite{0,9};
        paired.second_temp_write=odh::VmTempWrite{20,-7};
        rejects_outputs(paired); // Invalid second temp must not publish the first or frame/context.
        paired.second_temp_write=odh::VmTempWrite{19,-7};
        paired.write=odh::VmFrameWrite{odh::VmFrameBank::locals,4,99};
        rejects_outputs(paired);
        paired.write=odh::VmFrameWrite{static_cast<odh::VmFrameBank>(99),0,99};
        rejects_outputs(paired);
        // Exercise second_temp_write independently, so rejection cannot be caused
        // by the established first-temp or stack-object disposition checks.
        paired.temp_write.reset(); paired.stack_object_write.reset();
        paired.write=odh::VmFrameWrite{odh::VmFrameBank::arguments,0,99};
        for (const auto disposition : {odh::PrimitiveDisposition::wait,
                                       odh::PrimitiveDisposition::call,
                                       odh::PrimitiveDisposition::unsupported}) {
            paired.disposition=disposition;
            if (disposition==odh::PrimitiveDisposition::call) paired.call=odh::VmCall{key(),{},{}};
            else paired.call.reset();
            rejects_outputs(paired);
        }
        paired.disposition=odh::PrimitiveDisposition::goto_true; paired.call.reset();
        auto second_only=output_machine(paired); second_only.start(key(),{11,22,33},{17});
        second_only.temps()[0]=42;
        require(second_only.run(1)==odh::VmStatus::budget_exhausted &&
                second_only.temps()[0]==42 && second_only.temps()[19]==-7 &&
                second_only.stack()[0].arguments[0]==99,
                "second temp can publish without first temp");

        const auto service_wait=[](std::uint16_t opcode) {
            return odh::Instruction{opcode,1,2,{0,0,0,0,0,0,0,0}};
        };
        odh::RoutineSet external_routines{
            {key(),routine({service_wait(45),expr(0,1,0,7,7),expr(2,0,0,7,7,254,253)})},
            {key(4097),routine({service_wait(46),expr(0,1,0,7,7),expr(2,0,0,7,7,254,253)})},
            {key(4098),routine({expr(0,1,0,7,7)})}
        };
        odh::Vm external(std::move(external_routines),{}, {},
            [](const auto&,const auto& instruction,const auto&,const auto&) {
                if(instruction.opcode==45||instruction.opcode==46)
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
                return odh::PrimitiveOutcome{};
            });
        external.start(key());
        require(external.run(1)==odh::VmStatus::waiting&&external.pending(),
                "service parent waits before executing an external child BHAV");
        const auto parent_ticket=external.pending()->ticket;
        require(!external.call_pending(parent_ticket+1,{key(4097),{},{}})&&
                !external.call_pending(parent_ticket,{key(4099),{},{}})&&
                external.status()==odh::VmStatus::waiting&&external.pending()->ticket==parent_ticket&&
                external.stack().size()==1,
                "stale ticket and missing child routine preserve the suspended parent atomically");
        require(external.call_pending(parent_ticket,{key(4097),{},{}})&&
                external.status()==odh::VmStatus::running&&external.stack().size()==2&&!external.pending(),
                "external child call suspends the parent ticket without re-dispatching its BHAV");
        require(external.run(1)==odh::VmStatus::waiting&&external.pending(),
                "external child can own a separate service wait");
        const auto child_ticket=external.pending()->ticket;
        require(child_ticket!=parent_ticket&&external.call_pending(child_ticket,{key(4098),{},{}}),
                "nested external frame suspends the child wait independently");
        require(external.run(1)==odh::VmStatus::yielded&&external.external_frame_return()&&
                external.external_frame_return()->ticket==child_ticket&&
                external.external_frame_return()->result&&
                external.external_frame_return()->code_owner==0&&
                external.external_frame_return()->run_generation==external.run_generation()&&
                external.pending()&&external.pending()->ticket==child_ticket,
                "nested BHAV return publishes identity/result and restores the exact child ticket");
        require(external.resume_external_frame(child_ticket)&&external.status()==odh::VmStatus::waiting&&
                external.complete(child_ticket,true),
                "service resumes after nested return and completes the waiting child BHAV");
        require(external.run(2)==odh::VmStatus::yielded&&external.external_frame_return()&&
                external.external_frame_return()->ticket==parent_ticket&&
                external.external_frame_return()->result&&
                external.pending()&&external.pending()->ticket==parent_ticket,
                "outer portal BHAV return restores the original route service wait");
        require(external.resume_external_frame(parent_ticket)&&external.status()==odh::VmStatus::waiting,
                "outer service can resume after its portal BHAV returns");
        require(external.complete(parent_ticket,true),
                "outer route service completes the exact parent ticket");
        require(external.run(1)==odh::VmStatus::returned_true&&external.stack().empty(),
                "root BHAV follows the completed route result without re-dispatch");
        std::cout << checks << " VM checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "VM test failed: " << e.what() << '\n'; return 1; }
}
