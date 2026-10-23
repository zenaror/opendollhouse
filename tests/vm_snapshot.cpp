#include "odh/vm.hpp"
#include <cstdlib>
#include <iostream>
#include <new>
#include <sstream>

namespace { int allocation_countdown = -1; }
void* operator new(std::size_t size) {
    if (allocation_countdown == 0) { allocation_countdown = -1; throw std::bad_alloc(); }
    if (allocation_countdown > 0) --allocation_countdown;
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
namespace {
int checks = 0;
void require(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
odh::RoutineKey key(unsigned id) { return odh::routine_key(id); }
odh::Instruction op(unsigned id, unsigned yes = 254, unsigned no = 255) {
    return {static_cast<std::uint16_t>(id), static_cast<std::uint8_t>(yes), static_cast<std::uint8_t>(no), {}};
}
odh::Bhav routine(std::initializer_list<odh::Instruction> code) {
    return {0x8002, code, {}, odh::Bhav::Metadata{0, 4, 4, 0}};
}
void context(std::ostream& out, const odh::VmContext& c) { out << c.caller << ',' << c.callee << ',' << c.stack_object << ',' << c.code_owner << ';'; }
void trace(std::ostream& out, const odh::VmTrace& t) {
    out << t.step << ',' << t.depth << ',' << int(t.key.scope) << ',' << t.key.id << ',' << t.pc << ',' << t.code_owner << ','
        << t.instruction.opcode << ',' << int(t.instruction.on_true) << ',' << int(t.instruction.on_false) << ',';
    for (auto v : t.instruction.operands) out << int(v) << ',';
}
std::string values(const odh::VmSnapshot& s) {
    std::ostringstream out;
    out << int(s.status) << ';' << s.steps << ';' << s.run_generation << ';' << s.ticket_sequence << ';' << s.diagnostic.size() << ':' << s.diagnostic << ';';
    for (auto v : s.temps) out << v << ',';
    out << ';' << s.stack.size() << ';';
    for (const auto& f : s.stack) {
        out << int(f.key.scope) << ',' << f.key.id << ',' << f.pc << ',' << int(f.return_mode) << ',' << f.invocation_ticket << ';';
        context(out, f.context);
        out << f.arguments.size() << ':'; for (auto v : f.arguments) out << v << ',';
        out << f.locals.size() << ':'; for (auto v : f.locals) out << v << ',';
    }
    const auto wait = [&](const odh::VmPending& p) { out << p.ticket << ';'; trace(out, p.instruction); context(out, p.context); };
    out << ';' << bool(s.pending) << ';'; if (s.pending) wait(*s.pending);
    out << ';' << s.suspended_pending.size() << ';'; for (const auto& p : s.suspended_pending) wait(p);
    const auto event = [&](const auto& e) { out << e.ticket << ',' << e.result << ',' << e.code_owner << ',' << e.run_generation << ';'; };
    out << bool(s.interaction_return) << ';'; if (s.interaction_return) event(*s.interaction_return);
    out << bool(s.external_frame_return) << ';'; if (s.external_frame_return) event(*s.external_frame_return);
    return out.str();
}
void same(const odh::Vm& a, const odh::Vm& b) { require(values(a.snapshot()) == values(b.snapshot()), "all VM values match"); }
odh::PrimitiveOutcome service(const odh::VmFrame& f, const odh::Instruction& i,
                              const std::array<std::int16_t,20>&, const odh::VmDispatch& d) {
    require(d.origin && d.ticket, "dispatch has target VM and nonzero ticket");
    if (i.opcode == 10) return {odh::PrimitiveDisposition::wait, {}};
    if (i.opcode == 14) return {odh::PrimitiveDisposition::goto_false, {}};
    if (i.opcode == 12 && f.arguments[0] == 0) {
        odh::PrimitiveOutcome result{odh::PrimitiveDisposition::call, {}};
        result.call = odh::VmCall{key(4097), {3,4,5,7}, {}, odh::VmCallReturnMode::interaction_next_tick};
        result.write = odh::VmFrameWrite{odh::VmFrameBank::arguments, 0, 1};
        return result;
    }
    return {odh::PrimitiveDisposition::goto_true, {}};
}
odh::RoutineSet bank() {
    return {{key(4096), routine({op(4097,1), op(11)})},
            {key(4097), routine({op(10,1), op(11)})},
            {key(4098), routine({op(12,1), op(11)})},
            {key(4099), routine({op(13)})},
            {key(4100), routine({op(10,1), op(10,2), op(11)})},
            {key(4101), routine({op(14)})}};
}
odh::RoutineOwners owners() { return {{7, {{key(4097), routine({op(10,1), op(11)})}}}}; }
std::unique_ptr<odh::Vm> restored(const odh::Vm& source) {
    auto result = std::make_unique<odh::Vm>(bank(), odh::VmMemory{}, odh::VmLimits{}, service, owners());
    require(result->restore_snapshot(source.snapshot()), "restore valid snapshot"); same(source, *result); return result;
}
void run_same(odh::Vm& a, odh::Vm& b, std::uint64_t budget) {
    std::ostringstream left, right;
    a.run(budget, [&](const auto& t) { trace(left,t); }); b.run(budget, [&](const auto& t) { trace(right,t); });
    require(left.str() == right.str(), "continuous and restored traces match"); same(a,b);
}
}
int main() {
    try {
        odh::Vm source(bank(), {}, {}, service, owners());
        auto ready = restored(source);
        source.start(key(4096), {1,2,3}, {17,-8}); source.temps()[19] = -321;
        auto running = restored(source); run_same(source, *running, 1);
        auto child = restored(source); run_same(source, *child, 1);
        require(source.status() == odh::VmStatus::waiting && source.stack().size() == 2, "ordinary child WAIT fixture");
        auto wait = restored(source); const auto ticket = source.pending()->ticket;
        require(!wait->complete(ticket+1, true), "wrong completion ticket rejected");
        require(source.complete(ticket,true,odh::VmFrameWrite{odh::VmFrameBank::locals,2,77}) &&
            wait->complete(ticket,true,odh::VmFrameWrite{odh::VmFrameBank::locals,2,77}), "matching completion preserved");
        run_same(source,*wait,20); auto finished = restored(source);
        source.start(key(4100)); source.run(1);
        const auto first_ticket = source.pending()->ticket;
        auto retry = restored(source);
        require(source.retry_pending(first_ticket) && retry->retry_pending(first_ticket), "retry retained");
        run_same(source,*retry,1);
        require(source.pending()->ticket > first_ticket, "ticket sequence continues after restore");
        const auto parent_ticket = source.pending()->ticket;
        require(source.call_pending(parent_ticket, {key(4097),{6,7,8,7},{}}), "schedule external child");
        auto suspended = restored(source); run_same(source,*suspended,1);
        require(source.snapshot().suspended_pending.size()==1, "parent wait retained while child waits");
        const auto child_ticket = source.pending()->ticket;
        require(child_ticket > parent_ticket, "child has independent ticket");
        auto child_wait = restored(source);
        // A second external child gives two suspended waits at distinct depths.
        require(source.call_pending(child_ticket,{key(4099),{}, {}}) && child_wait->call_pending(child_ticket,{key(4099),{}, {}}), "nested external child");
        auto twice_suspended = restored(source); run_same(source,*twice_suspended,1);
        require(source.status()==odh::VmStatus::yielded && source.external_frame_return(), "external return yields to waiting child");
        auto yielded = restored(source);
        require(source.resume_external_frame(child_ticket) && yielded->resume_external_frame(child_ticket), "external yield boundary restored");
        require(source.complete(child_ticket,true) && yielded->complete(child_ticket,true), "child wait completion restored");
        run_same(source,*yielded,10);
        auto outer_yield = restored(source);
        require(source.resume_external_frame(parent_ticket) && outer_yield->resume_external_frame(parent_ticket), "parent yield boundary restored");
        require(source.complete(parent_ticket,true) && outer_yield->complete(parent_ticket,true), "parent completion restored");
        run_same(source,*outer_yield,1);
        require(source.pending()->ticket > child_ticket, "next dispatch ticket preserved");
        source.start(key(4098)); source.run(1); auto interaction_child=restored(source);
        run_same(source,*interaction_child,1);
        require(source.complete(source.pending()->ticket,true), "interaction child WAIT completion");
        source.run(10); require(source.status()==odh::VmStatus::yielded && source.interaction_return(), "interaction return fixture");
        auto interaction_yield=restored(source); const auto interaction_ticket=source.interaction_return()->ticket;
        require(source.resume_interaction(interaction_ticket) && interaction_yield->resume_interaction(interaction_ticket), "interaction boundary restored");
        run_same(source,*interaction_yield,10);
        source.start(key(999)); require(source.status()==odh::VmStatus::fault, "missing routine fault"); auto fault=restored(source);
        odh::Vm unsupported(bank()); unsupported.start(key(4099)); unsupported.run(1);
        odh::Vm fault_target(bank()); require(fault_target.restore_snapshot(unsupported.snapshot()), "diagnostic fault restored"); same(unsupported,fault_target);
        odh::Vm dormant(bank(),{},{},service,owners());
        dormant.restore_dormant_state({{key(4099),0,{},{1,2,3,4},{1,2,3,4}}},{});
        auto dormant_copy=restored(dormant);
        require(dormant.activate_dormant_state() && dormant_copy->activate_dormant_state(), "dormant activation preserved"); run_same(dormant,*dormant_copy,1);
        source.start(key(4100)); source.run(1); source.call_pending(source.pending()->ticket,{key(4097),{1,2,3,7},{}}); source.run(1);
        const auto snapshot=source.snapshot();
        require(snapshot.pending && !snapshot.suspended_pending.empty(),
            "allocation failure fixture includes pending and suspended waits");
        odh::Vm false_return(bank(),{},{},service,owners()); false_return.start(key(4101)); false_return.run(1);
        require(false_return.status()==odh::VmStatus::returned_false,"false return fixture"); auto false_copy=restored(false_return);
        auto invalid_event=false_return.snapshot();
        invalid_event.interaction_return=odh::VmInteractionReturn{1,true,0,invalid_event.run_generation};
        odh::Vm event_target(bank(),{},{},service,owners());
        require(!event_target.restore_snapshot(invalid_event),"terminal state rejects return event");

        const auto reject=[&](const odh::VmSnapshot& bad, odh::VmLimits limits=odh::VmLimits{}) {
            odh::Vm target(bank(),{},limits,service,owners()); const auto before=values(target.snapshot());
            require(!target.restore_snapshot(bad) && values(target.snapshot())==before,"invalid snapshot leaves fresh VM unchanged");
        };
        auto bad=snapshot; bad.stack[0].pc=999; reject(bad);
        bad=snapshot; bad.stack.back().key.scope=static_cast<odh::RoutineScope>(99); reject(bad);
        bad=snapshot; bad.stack.back().context.code_owner=99; reject(bad);
        bad=snapshot; bad.stack.back().arguments.clear(); reject(bad);
        bad=snapshot; bad.stack.back().return_mode=static_cast<odh::VmCallReturnMode>(99); reject(bad);
        bad=snapshot; bad.stack.back().invocation_ticket=bad.ticket_sequence+1; reject(bad);
        bad=snapshot; bad.pending->ticket=0; reject(bad);
        bad=snapshot; bad.pending->ticket=bad.ticket_sequence+1; reject(bad);
        bad=snapshot; bad.stack.back().invocation_ticket=0; reject(bad);
        bad=snapshot; bad.suspended_pending[0].instruction.instruction.opcode=11; reject(bad);
        bad=snapshot; bad.status=odh::VmStatus::yielded;
        bad.external_frame_return=odh::VmExternalFrameReturn{bad.pending->ticket,true,7,bad.run_generation+1}; reject(bad);
        bad=snapshot; bad.status=odh::VmStatus::yielded;
        bad.external_frame_return=odh::VmExternalFrameReturn{bad.pending->ticket+1,true,7,bad.run_generation}; reject(bad);

        bad=snapshot; bad.pending->ticket=bad.suspended_pending[0].ticket; reject(bad);
        bad=snapshot; bad.pending->instruction.instruction.operands[1]=12; reject(bad);
        bad=snapshot; bad.pending->instruction.depth=1; reject(bad);
        bad=snapshot; bad.pending->instruction.step=bad.steps; reject(bad);
        bad=snapshot; bad.pending->context.stack_object++; reject(bad);
        bad=snapshot; bad.suspended_pending.clear(); reject(bad);
        bad=snapshot; bad.suspended_pending[0].instruction.depth=2; reject(bad);
        bad=snapshot; bad.run_generation=0; reject(bad);
        bad=snapshot; bad.status=static_cast<odh::VmStatus>(99); reject(bad);
        bad=snapshot; bad.status=odh::VmStatus::running; reject(bad);
        reject(snapshot, {1,4096,64}); reject(snapshot, {64,3,64});
        const auto used=values(source.snapshot()); require(!source.restore_snapshot(snapshot) && values(source.snapshot())==used,"used destination unchanged");
        odh::Vm dirty(bank(),{},{},service,owners()); dirty.temps()[0]=1;
        require(!dirty.restore_snapshot(snapshot) && dirty.temps()[0]==1,"dirty ready destination rejected");
        auto independent=snapshot; independent.stack[0].arguments[0]=123; independent.temps[0]=456;
        require(values(source.snapshot())==used,"snapshot values own their storage");
        odh::Vm allocation_target(bank(),{},{},service,owners()); const auto initial=values(allocation_target.snapshot());
        bool complete=false;
        for(int fail=0;fail<100 && !complete;++fail) {
            allocation_countdown=fail; bool threw=false;
            try { complete=allocation_target.restore_snapshot(snapshot); } catch(const std::bad_alloc&) { threw=true; }
            allocation_countdown=-1;
            if(!complete) require(threw && values(allocation_target.snapshot())==initial,"every allocation failure is atomic");
        }
        require(complete && values(allocation_target.snapshot())==values(snapshot),"allocation sweep eventually publishes full state");
        bool capture_guard=false,restore_guard=false;
        odh::Vm guarded(bank(),{},{},[&](const auto&,const auto&,const auto&,const auto& dispatch) {
            try { dispatch.origin->snapshot(); } catch(const std::logic_error&) { capture_guard=true; }
            try { const_cast<odh::Vm*>(dispatch.origin)->restore_snapshot(snapshot); } catch(const std::logic_error&) { restore_guard=true; }
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        });
        guarded.start(key(4099)); guarded.run(1);
        require(capture_guard && restore_guard,"callbacks cannot capture or restore in-flight state");
        std::cout << checks << " VM snapshot checks passed (synthetic fixtures only)\n";
    } catch(const std::exception& e) { allocation_countdown=-1; std::cerr << e.what() << '\n'; return 1; }
}
