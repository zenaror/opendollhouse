#include "odh/vm_snapshot_codec.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template <typename Function>
void rejects(Function&& function, const char* message) {
    bool threw = false;
    try { function(); } catch (const odh::FormatError&) { threw = true; }
    require(threw, message);
}
void same(const odh::VmSnapshot& left, const odh::VmSnapshot& right) {
    require(left.status == right.status && left.diagnostic == right.diagnostic &&
        left.steps == right.steps && left.ticket_sequence == right.ticket_sequence &&
        left.run_generation == right.run_generation && left.stack.size()==right.stack.size() &&
        left.advertisement_changes==right.advertisement_changes&&
        left.suspended_pending.empty() && right.suspended_pending.empty() &&
        left.pending.has_value()==right.pending.has_value() && !left.interaction_return && !right.interaction_return &&
        !left.external_frame_return && !right.external_frame_return, "snapshot fields round-trip");
    require(left.temps == right.temps && left.temp_xl == right.temp_xl,
        "temporary registers round-trip");
    for(std::size_t i=0;i<left.stack.size();++i)
        require(left.stack[i].key.scope==right.stack[i].key.scope&&left.stack[i].key.id==right.stack[i].key.id&&
            left.stack[i].pc==right.stack[i].pc&&
            left.stack[i].context.caller==right.stack[i].context.caller&&
            left.stack[i].context.callee==right.stack[i].context.callee&&
            left.stack[i].return_mode==right.stack[i].return_mode&&
            left.stack[i].invocation_ticket==right.stack[i].invocation_ticket&&
            left.stack[i].arguments==right.stack[i].arguments&&left.stack[i].locals==right.stack[i].locals&&
            left.stack[i].exact_arguments==right.stack[i].exact_arguments&&
            left.stack[i].original_import==right.stack[i].original_import,
            "stack frame round-trips");
    if(left.pending)require(left.pending->ticket==right.pending->ticket&&
        left.pending->instruction.step==right.pending->instruction.step&&
        left.pending->instruction.key.scope==right.pending->instruction.key.scope&&
        left.pending->instruction.key.id==right.pending->instruction.key.id&&
        left.pending->context.caller==right.pending->context.caller,
        "current pending primitive round-trips");
    require(left.immediate_continuations.size()==right.immediate_continuations.size() &&
        left.immediate_return.has_value()==right.immediate_return.has_value(),
        "immediate state shape round-trips");
    for(std::size_t i=0;i<left.immediate_continuations.size();++i) {
        const auto& a=left.immediate_continuations[i];const auto& b=right.immediate_continuations[i];
        require(a.ticket==b.ticket&&a.parent_depth==b.parent_depth&&a.parent_status==b.parent_status&&
            a.parent_pending.has_value()==b.parent_pending.has_value(),"immediate continuation round-trips");
        if(a.parent_pending)require(a.parent_pending->ticket==b.parent_pending->ticket&&
            a.parent_pending->instruction.step==b.parent_pending->instruction.step&&
            a.parent_pending->instruction.key.scope==b.parent_pending->instruction.key.scope&&
            a.parent_pending->instruction.key.id==b.parent_pending->instruction.key.id&&
            a.parent_pending->context.caller==b.parent_pending->context.caller,
            "immediate parent pending round-trips");
    }
    if(left.immediate_return)require(left.immediate_return->ticket==right.immediate_return->ticket&&
        left.immediate_return->result==right.immediate_return->result&&
        left.immediate_return->code_owner==right.immediate_return->code_owner&&
        left.immediate_return->run_generation==right.immediate_return->run_generation,
        "immediate return round-trips");
}

// Routing values include suspended replay instructions; the ordinary helper
// deliberately requires none. Compare that additional value bank exhaustively
// before applying the unchanged ordinary comparison to the remaining fields.
void same_route_values(const odh::VmSnapshot& left,const odh::VmSnapshot& right){
    const auto pending_equal=[](const odh::VmPending& a,const odh::VmPending& b){
        const auto& x=a.instruction;const auto& y=b.instruction;
        return a.ticket==b.ticket&&odh::vm_context_equal(a.context,b.context)&&
            x.step==y.step&&x.depth==y.depth&&x.key.scope==y.key.scope&&x.key.id==y.key.id&&
            x.pc==y.pc&&x.code_owner==y.code_owner&&x.instruction.opcode==y.instruction.opcode&&
            x.instruction.on_true==y.instruction.on_true&&x.instruction.on_false==y.instruction.on_false&&
            x.instruction.operands==y.instruction.operands;
    };
    require(left.suspended_pending.size()==right.suspended_pending.size()&&
        left.action_tree_known==right.action_tree_known,"routing value bank shape round-trips");
    for(std::size_t i=0;i<left.suspended_pending.size();++i)
        require(pending_equal(left.suspended_pending[i],right.suspended_pending[i]),"complete suspended routing replay round-trips");
    require(left.pending.has_value()==right.pending.has_value()&&
        (!left.pending||pending_equal(*left.pending,*right.pending)),"complete routing pending round-trips");
    require(left.stack.size()==right.stack.size(),"routing value frame count round-trips");
    for(std::size_t i=0;i<left.stack.size();++i)
        require(odh::vm_frame_equal(left.stack[i],right.stack[i]),"complete routing frame and original metadata round-trip");
    auto a=left,b=right;a.suspended_pending.clear();b.suspended_pending.clear();same(a,b);
}

odh::VmPending parent_wait() {
    odh::VmPending pending{};pending.ticket=9;
    pending.instruction={4,1,odh::routine_key(1),2,odh::Instruction{}};
    pending.context={4,4,4,0};return pending;
}
odh::VmSnapshot immediate_snapshot() {
    odh::VmSnapshot snapshot;snapshot.status=odh::VmStatus::running;snapshot.steps=20;
    snapshot.ticket_sequence=12;snapshot.run_generation=3;
    odh::VmFrame parent{};parent.key=odh::routine_key(1);parent.context={4,4,4,0};parent.pc=2;
    odh::VmFrame child{};child.key=odh::routine_key(2);child.context={4,8,8,0};child.pc=1;
    child.return_mode=odh::VmCallReturnMode::immediate;child.invocation_ticket=11;
    snapshot.stack={parent,child};
    snapshot.immediate_continuations.push_back({11,1,odh::VmStatus::waiting,parent_wait()});
    return snapshot;
}
void mixed_immediate_codec(){
    const auto key=[](unsigned id){return odh::routine_key(static_cast<std::uint16_t>(id));};
    const auto routine=[](std::initializer_list<odh::Instruction> code){return odh::Bhav{0x8002,code,{},odh::Bhav::Metadata{0,4,4,0}};};
    const odh::Instruction done{2,254,255,{1,0,77,0,0,5,8,7}};
    for(unsigned mode=0;mode<4;++mode){
        const bool portal=mode==1||mode==2,child_route=mode>=2;
        const odh::RoutineSet base{{key(4096),routine({{17,254,255,{}}})}};
        const odh::RoutineSet action{{key(4100),routine({{27,1,255,{}},{2,254,255,{2,0,91,0,0,5,8,7}}})},
            {key(4101),routine({{0,254,255,{}}})}};
        odh::RoutineSet privacy{{key(4102),routine({done})}};
        if(child_route)privacy.at(key(4102)).instructions={{45,1,255,{}},done};
        const odh::VmPrimitive primitive=[&](const auto&,const auto& instruction,const auto&,const auto&){
            if(instruction.opcode==17)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::call,{}, {},
                odh::VmCall{key(4100),{105,108,108,11},{},odh::VmCallReturnMode::interaction_next_tick}};
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
        };
        const auto fresh=[&](){return std::make_unique<odh::Vm>(base,odh::VmMemory{},odh::VmLimits{},primitive,
            odh::RoutineOwners{{11,action},{12,privacy}});};
        auto live=fresh();live->start(key(4096),{105,105,105,0});
        require(live->run(8)==odh::VmStatus::waiting&&live->stack().size()==2&&live->stack()[1].return_mode==odh::VmCallReturnMode::interaction_next_tick,
            "real queued user action reaches its own route before privacy immediate");
        const auto route=live->pending()->ticket;std::uint64_t sleep=0,child=0,child_sleep=0;
        if(portal){require(live->call_pending(route,{key(4101),{105,109,109,11},{}})&&live->run(8)==odh::VmStatus::waiting,
            "real route portal preserves external parent pending before immediate injection");sleep=live->pending()->ticket;}
        const auto immediate=live->inject_immediate({key(4102),{105,105,105,12},{}},[](const auto&,const auto&){return true;});
        require(immediate.has_value(),"inject privacy immediate over actual user-action or portal routing stack");
        if(child_route){
            require(live->run(8)==odh::VmStatus::waiting,"privacy action reaches its independent child route pending");child=live->pending()->ticket;
            if(mode==2){require(live->call_pending(child,{key(4101),{105,110,110,11},{}})&&live->run(8)==odh::VmStatus::waiting,
                "privacy child route may also contain an external portal wait");child_sleep=live->pending()->ticket;}
        }
        const auto original=live->snapshot();const auto bytes=odh::encode_vm_snapshot(original);
        const auto decoded=odh::decode_vm_snapshot(bytes);auto restored=fresh();
        require(odh::encode_vm_snapshot(decoded)==bytes&&restored->restore_snapshot(decoded),
            "encoded mixed user-action routing immediate and portal bytes restore into a fresh real VM");
        const auto reject=[&](const auto& mutate,const char* why){auto malformed=original;mutate(malformed);
            rejects([&]{odh::encode_vm_snapshot(malformed);},why);};
        if(portal){
            reject([](auto& value){++value.suspended_pending[0].ticket;},"mixed external wait cannot borrow another invocation ticket");
            reject([](auto& value){++value.suspended_pending[0].instruction.depth;},"mixed external wait must retain exact parent depth");
            reject([](auto& value){++value.suspended_pending[0].context.code_owner;},"mixed external wait owner must match its source frame");
            reject([](auto& value){value.suspended_pending.clear();},"mixed external frame cannot lose suspended wait roster");
        }
        reject([](auto& value){value.suspended_pending.push_back(*value.immediate_continuations[0].parent_pending);},
            "immediate parent wait cannot be duplicated into unrelated external ancestry");
        reject([](auto& value){++value.immediate_continuations[0].parent_pending->context.caller;},
            "mixed immediate parent cannot change caller");
        reject([](auto& value){value.status=odh::VmStatus::yielded;value.interaction_return=odh::VmInteractionReturn{1,true,11,value.run_generation};
            value.external_frame_return=odh::VmExternalFrameReturn{1,true,11,value.run_generation};},
            "mixed immediate stack cannot publish two unrelated return channels");
        for(auto* vm:{live.get(),restored.get()}){
            if(child_sleep){
                require(vm->complete(child_sleep,true)&&vm->run(8)==odh::VmStatus::yielded&&vm->external_frame_return(),
                    "child portal returns while original portal and immediate parent remain protected");
                const auto external_bytes=odh::encode_vm_snapshot(vm->snapshot());auto external_copy=fresh();
                require(odh::encode_vm_snapshot(odh::decode_vm_snapshot(external_bytes))==external_bytes&&
                    external_copy->restore_snapshot(odh::decode_vm_snapshot(external_bytes)),
                    "encoded child external return before ACK restores exact mixed immediate and suspended portal ancestry");
                require(vm->resume_external_frame(child),"child portal acknowledges only its own retained route pending");
            }
            if(child_route)require(vm->complete(child,true),"complete only privacy's child route");
            require(vm->run(8)==odh::VmStatus::yielded&&vm->immediate_return()&&vm->temps()[1]==77,
                "privacy immediate returns over its original user-action ancestry");
            const auto returned=odh::decode_vm_snapshot(odh::encode_vm_snapshot(vm->snapshot()));auto returned_copy=fresh();
            require(returned_copy->restore_snapshot(returned)&&vm->resume_immediate(*immediate),
                "popped immediate return retains suspended portal ancestry through encoded bytes");
            if(portal)require(vm->pending()->ticket==sleep&&vm->complete(sleep,true)&&vm->run(8)==odh::VmStatus::yielded&&
                vm->resume_external_frame(route),"original portal returns to its exact user-action route after privacy");
            require(vm->pending()->ticket==route&&vm->complete(route,true)&&vm->run(8)==odh::VmStatus::yielded&&
                vm->interaction_return()&&vm->temps()[2]==91,"original queued action resumes its authored route branch");
            const auto final=odh::decode_vm_snapshot(odh::encode_vm_snapshot(vm->snapshot()));auto final_copy=fresh();
            require(final_copy->restore_snapshot(final),"original queued user-action result also remains codec-restorable");
        }
        require(odh::encode_vm_snapshot(live->snapshot())==odh::encode_vm_snapshot(restored->snapshot()),
            "fresh encoded mid-stack continuation equals uninterrupted VM state byte for byte");
    }
    {
        const odh::RoutineSet program{
            {key(4096),routine({{27,254,255,{}}})},
            {key(4100),routine({{17,254,255,{}}})},
            {key(4101),routine({{2,254,255,{3,0,44,0,0,5,8,7}}})}};
        const odh::VmPrimitive primitive=[&](const auto&,const auto& instruction,const auto&,const auto&){
            if(instruction.opcode==17)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::call,{}, {},
                odh::VmCall{key(4101),{1,2,2,0},{},odh::VmCallReturnMode::interaction_next_tick}};
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
        };
        odh::Vm live(program,{}, {},primitive);live.start(key(4096),{1,1,1,0});
        require(live.run(8)==odh::VmStatus::waiting,"nested normal-return fixture starts with original route wait");
        const auto parent=live.pending()->ticket;
        const auto immediate=live.inject_immediate({key(4100),{1,2,2,0},{}},[](const auto&,const auto&){return true;});
        require(immediate&&live.run(8)==odh::VmStatus::yielded&&live.interaction_return()&&
            !live.immediate_return()&&live.immediate_continuations().size()==1&&live.temps()[3]==44,
            "ordinary interaction child can return while privacy immediate still protects original route");
        const auto bytes=odh::encode_vm_snapshot(live.snapshot());const auto decoded=odh::decode_vm_snapshot(bytes);
        odh::Vm restored(program,{}, {},primitive);
        require(odh::encode_vm_snapshot(decoded)==bytes&&restored.restore_snapshot(decoded)&&
            restored.resume_interaction(decoded.interaction_return->ticket)&&
            restored.immediate_continuations().size()==1&&
            restored.immediate_continuations().front().ticket==*immediate&&
            restored.immediate_continuations().front().parent_pending->ticket==parent,
            "encoded nested normal return restores and ACKs only normal child without losing immediate or original route");
    }
}

}

void named_resource_codec(){
    for(const std::uint16_t id:{std::uint16_t{0},std::uint16_t{42},std::uint16_t{4096},std::uint16_t{65535}})
    for(const bool linked:{false,true}){
        const auto key=odh::named_routine_key(id,linked);
        const odh::Bhav body{0x8002,{{99,1,255,{}},{2,254,255,{0,0,91,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,0,0}};
        const odh::RoutineOwners owners{{77,{{key,body}}}};
        const auto wait=[](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
        odh::Vm actual({}, {}, {},wait,owners);
        require(actual.start(key,{1,2,3,77})==odh::VmStatus::running&&actual.run(1)==odh::VmStatus::waiting,
            "explicit named resource executes original ushort ID in its physical bank");
        for(const bool has_ads:{false,true}){
            auto state=actual.snapshot();if(has_ads)state.advertisement_changes.emplace();
            const auto bytes=odh::encode_vm_snapshot(state);const auto decoded=odh::decode_vm_snapshot(bytes);
            require(bytes[0]==8&&decoded.stack.front().key.scope==key.scope&&decoded.stack.front().key.id==id&&
                decoded.advertisement_changes==state.advertisement_changes&&odh::encode_vm_snapshot(decoded)==bytes,
                "v8 retains physical named namespace, raw ID and absent versus empty Thread advertisement state");
            odh::Vm fresh({}, {}, {},wait,owners);
            require(fresh.restore_snapshot(decoded)&&fresh.complete(fresh.pending()->ticket,true)&&
                fresh.run(1)==odh::VmStatus::returned_true&&fresh.temps()[0]==91,
                "fresh restored named pending resumes the exact resource body without numeric scope inference");
            auto old=bytes;old[0]=7;rejects([&]{odh::decode_vm_snapshot(old);},"old snapshot versions cannot admit named namespaces");
        }
        auto malformed=actual.snapshot();malformed.stack.front().context.code_owner=0;
        rejects([&]{odh::encode_vm_snapshot(malformed);},"named entries cannot silently bind the default global owner");
    }
    odh::VmSnapshot old;old.action_tree_known=true;const auto legacy=odh::encode_vm_snapshot(old);
    auto noncanonical=legacy;noncanonical[0]=8;noncanonical.push_back(0);
    rejects([&]{odh::decode_vm_snapshot(noncanonical);},"v8 without a named address is noncanonical");
}
void advertisement_state_codec(){
    odh::VmSnapshot empty;const auto legacy=odh::encode_vm_snapshot(empty);
    empty.advertisement_changes.emplace();const auto allocated=odh::encode_vm_snapshot(empty);
    require(allocated[0]==7&&odh::decode_vm_snapshot(allocated).advertisement_changes&&
        odh::decode_vm_snapshot(allocated).advertisement_changes->empty()&&legacy[0]==2,
        "VM v7 preserves allocated empty advertisement dictionaries distinctly from legacy null state");
    empty.advertisement_changes=std::map<std::uint32_t,std::int16_t>{{65535,0},{(1U<<16)|7,-32768},{(2U<<16)|65535,32767}};
    const auto bytes=odh::encode_vm_snapshot(empty);const auto decoded=odh::decode_vm_snapshot(bytes);
    require(decoded.advertisement_changes==empty.advertisement_changes&&odh::encode_vm_snapshot(decoded)==bytes,
        "all advertisement types, zero overrides and ushort keys round-trip byte-exactly");
    auto malformed=empty;(*malformed.advertisement_changes)[3U<<16]=0;
    rejects([&]{odh::encode_vm_snapshot(malformed);},"unknown advertisement type cannot enter saved Thread state");
    malformed.advertisement_changes.emplace();
    for(std::uint32_t n=0;n<=odh::advertisement_key_limit;++n)(*malformed.advertisement_changes)[n]=1;
    rejects([&]{odh::encode_vm_snapshot(malformed);},"oversized advertisement dictionary is rejected before encoding");
    const auto tail=bytes.size()-18-4;
    auto bad=bytes;bad[tail]=1;bad[tail+1]=64;
    rejects([&]{odh::decode_vm_snapshot(bad);},"advertisement count is bounded before allocating entries");
    bad=bytes;for(std::size_t n=0;n<4;++n)bad[tail+4+6+n]=bad[tail+4+n];
    rejects([&]{odh::decode_vm_snapshot(bad);},"duplicate advertisement wire keys cannot silently replace values");
    bad=bytes;bad.resize(bytes.size()-1);rejects([&]{odh::decode_vm_snapshot(bad);},"truncated advertisement state rejects");
    const auto key=odh::routine_key(4096);
    const odh::RoutineSet code{{key,{0x8002,{{2,1,255,{7,0,91,0,0,5,12,7}},{99,2,255,{}},
        {2,254,255,{0,0,7,0,0,5,8,12}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
    const auto wait=[](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
    odh::Vm actual(code,{}, {},wait);actual.start(key);
    require(actual.run(2)==odh::VmStatus::waiting,"normal advertisement write reaches an actual asynchronous wait");
    const auto saved=odh::encode_vm_snapshot(actual.snapshot());odh::Vm reopened(code,{}, {},wait);
    require(saved[0]==7&&reopened.restore_snapshot(odh::decode_vm_snapshot(saved))&&
        reopened.complete(reopened.pending()->ticket,true)&&reopened.run(1)==odh::VmStatus::returned_true&&reopened.temps()[0]==91,
        "ordinary write, wait, native wire, fresh restore and resumed advertisement read retain the real Thread value");
    reopened.reset_execution_state();require(reopened.advertisement_changes()==actual.advertisement_changes(),
        "source Reset retains persisted dictionary on the same Thread");
}
int main() {
    try {
        named_resource_codec();
        advertisement_state_codec();
        {
            odh::VmSnapshot state;state.status=odh::VmStatus::fault;state.run_generation=1;state.action_tree_known=true;
            odh::VmFrame frame;frame.key=odh::routine_key(4096);frame.context={1,2,42,0};
            frame.context.entity_references_bound=true;frame.context.caller_generation=11;frame.context.callee_generation=22;
            state.stack.push_back(frame);const auto bytes=odh::encode_vm_snapshot(state);const auto loaded=odh::decode_vm_snapshot(bytes);
            require(bytes[0]==6&&loaded.action_tree_known&&loaded.stack.front().context.entity_references_bound&&
                loaded.stack.front().context.stack_object==42&&!loaded.stack.front().context.stack_object_generation&&
                odh::vm_context_equal(loaded.stack.front().context,frame.context)&&odh::encode_vm_snapshot(loaded)==bytes,
                "VM v6 distinguishes bound positive nil and preserves complete source identity tags");
            auto empty=state;empty.stack.clear();empty.status=odh::VmStatus::returned_true;
            const auto empty_bytes=odh::encode_vm_snapshot(empty);
            require(empty_bytes[0]==6&&odh::decode_vm_snapshot(empty_bytes).action_tree_known&&
                odh::encode_vm_snapshot(odh::decode_vm_snapshot(empty_bytes))==empty_bytes,
                "known ActionTree state survives a returned empty VM without exact-argument frames");
            auto malformed=state;malformed.stack.front().context.entity_references_bound=false;
            rejects([&]{odh::encode_vm_snapshot(malformed);},"legacy contexts cannot carry hidden generation tags");
            malformed=state;malformed.stack.front().context.caller=0;
            rejects([&]{odh::encode_vm_snapshot(malformed);},"a generation needs a positive corresponding identity");
            malformed=state;malformed.action_tree_known=false;malformed.stack.front().action_tree=true;
            rejects([&]{odh::encode_vm_snapshot(malformed);},"unknown legacy ActionTree cannot manufacture an action tree");
            state.action_tree_known=false;state.stack.front().context={1,2,42,0};
            const auto old=odh::encode_vm_snapshot(state);require(old[0]==2&&odh::encode_vm_snapshot(odh::decode_vm_snapshot(old))==old,
                "legacy context bytes remain unchanged without a runtime admission upgrade");
        }

        mixed_immediate_codec();
        odh::VmSnapshot snapshot;
        snapshot.status = odh::VmStatus::fault;
        snapshot.diagnostic = "fixture: bounded diagnostic";
        snapshot.steps = 0x1020304050607080ull;
        snapshot.ticket_sequence = 0x8877665544332211ull;
        snapshot.run_generation = 0x0102030405060708ull;
        for (std::size_t i = 0; i < snapshot.temps.size(); ++i)
            snapshot.temps[i] = static_cast<std::int16_t>(i * 97u) - 800;
        snapshot.temp_xl = {70000, -70000};

        const auto encoded = odh::encode_vm_snapshot(snapshot);
        require(encoded[0]==2&&encoded[1]==0,
            "snapshots without immediate state retain legacy v2 encoding");
        odh::VmSnapshot next_tick;next_tick.status=odh::VmStatus::next_tick;
        odh::VmFrame next_tick_frame{};next_tick_frame.key=odh::routine_key(1);
        next_tick.stack.push_back(next_tick_frame);
        const auto next_tick_bytes=odh::encode_vm_snapshot(next_tick);
        require(next_tick_bytes[0]==3&&next_tick_bytes[1]==0,
            "next-tick-only snapshots retain legacy v3 encoding");
        same(next_tick,odh::decode_vm_snapshot(next_tick_bytes));
        same(snapshot, odh::decode_vm_snapshot(encoded));
        auto legacy = encoded;
        const auto temp_xl_offset = 2u + 1u + 4u + snapshot.diagnostic.size() + 3u * 8u + 20u * 2u;
        legacy.erase(legacy.begin() + static_cast<std::ptrdiff_t>(temp_xl_offset),
                     legacy.begin() + static_cast<std::ptrdiff_t>(temp_xl_offset + 8));
        legacy[0] = 1; legacy[1] = 0;
        const auto old_snapshot = odh::decode_vm_snapshot(legacy);
        require(old_snapshot.temp_xl == std::array<std::int32_t, 2>{},
            "version 1 snapshots restore with zeroed TempXL values");
        const auto pending_flag_offset = 2u + 1u + 4u + snapshot.diagnostic.size() +
            3u * 8u + 20u * 2u + 2u * 4u + 4u;
        for (std::size_t size = 0; size < encoded.size(); ++size) {
            auto truncated = encoded;
            truncated.resize(size);
            rejects([&] { (void)odh::decode_vm_snapshot(truncated); }, "every truncated prefix rejected");
        }
        auto changed = encoded;
        changed[0] = 5;
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "unsupported version rejected");
        changed = encoded;
        changed[2] = 0xff;
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "invalid status rejected");
        changed = encoded;
        changed[pending_flag_offset] = 2;
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "invalid pending flag rejected");
        changed = encoded;
        changed[pending_flag_offset + 5u] = 2;
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "invalid interaction flag rejected");
        changed = encoded;
        changed[pending_flag_offset + 6u] = 2;
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "invalid external return flag rejected");
        changed = encoded;
        changed.push_back(0);
        rejects([&] { (void)odh::decode_vm_snapshot(changed); }, "trailing bytes rejected");

        auto limits = odh::VmSnapshotCodecLimits{};
        limits.max_bytes = encoded.size() - 1;
        rejects([&] { (void)odh::decode_vm_snapshot(encoded, limits); }, "byte limit enforced on decode");
        limits = {};
        limits.max_diagnostic_bytes = snapshot.diagnostic.size() - 1;
        rejects([&] { (void)odh::encode_vm_snapshot(snapshot, limits); }, "diagnostic limit enforced on encode");

        odh::VmSnapshot empty;
        auto too_small = odh::VmSnapshotCodecLimits{};
        too_small.max_bytes = 81;
        rejects([&] { (void)odh::encode_vm_snapshot(empty, too_small); }, "minimum output size enforced");

        const auto immediate=immediate_snapshot();
        const auto immediate_bytes=odh::encode_vm_snapshot(immediate);
        require(immediate_bytes[0]==4&&immediate_bytes[1]==0,
            "immediate state selects VM snapshot v4");
        same(immediate,odh::decode_vm_snapshot(immediate_bytes));
        auto current_child_wait=immediate_snapshot();current_child_wait.status=odh::VmStatus::waiting;
        auto child_pending=parent_wait();child_pending.ticket=12;
        child_pending.instruction.depth=2;child_pending.instruction.key=current_child_wait.stack[1].key;
        child_pending.instruction.pc=current_child_wait.stack[1].pc;
        child_pending.context=current_child_wait.stack[1].context;
        current_child_wait.pending=child_pending;
        same(current_child_wait,odh::decode_vm_snapshot(odh::encode_vm_snapshot(current_child_wait)));
        auto nested=immediate_snapshot();
        odh::VmFrame nested_child{};nested_child.key=odh::routine_key(3);
        nested_child.context={4,9,9,0};nested_child.return_mode=odh::VmCallReturnMode::immediate;
        nested_child.invocation_ticket=12;nested.stack.push_back(nested_child);
        nested.ticket_sequence=15;
        nested.immediate_continuations[0].parent_pending.reset();
        nested.immediate_continuations[0].parent_status=odh::VmStatus::running;
        nested.immediate_continuations[0].ticket=11;
        nested.stack[1].invocation_ticket=11;
        nested.stack[1].pc=1;
        nested_child.invocation_ticket=14;nested_child.pc=2;
        nested.stack[2]=nested_child;
        auto second_pending=parent_wait();second_pending.ticket=13;
        second_pending.instruction.depth=2;
        second_pending.instruction.key=nested.stack[1].key;
        second_pending.instruction.pc=nested.stack[1].pc;
        second_pending.context=nested.stack[1].context;
        nested.immediate_continuations.push_back({14,2,odh::VmStatus::waiting,second_pending});
        same(nested,odh::decode_vm_snapshot(odh::encode_vm_snapshot(nested)));
        for(std::size_t size=0;size<immediate_bytes.size();++size) {
            auto truncated=immediate_bytes;truncated.resize(size);
            rejects([&]{(void)odh::decode_vm_snapshot(truncated);},
                "truncated immediate snapshot rejected");
        }
        auto immediate_bad=immediate_bytes;immediate_bad.push_back(0);
        rejects([&]{(void)odh::decode_vm_snapshot(immediate_bad);},
            "immediate snapshot trailing byte rejected");
        immediate_bad=immediate_bytes;immediate_bad[0]=3;
        rejects([&]{(void)odh::decode_vm_snapshot(immediate_bad);},
            "v3 rejects v4 immediate frame mode");
        immediate_bad=immediate_bytes;
        const auto continuation_count_offset=immediate_bad.size()-(4u+18u+55u+1u);
        for(std::size_t i=0;i<4;++i)immediate_bad[continuation_count_offset+i]=0xff;
        rejects([&]{(void)odh::decode_vm_snapshot(immediate_bad);},
            "oversized v4 continuation count rejected");
        immediate_bad=odh::encode_vm_snapshot(odh::VmSnapshot{});
        immediate_bad[0]=4;immediate_bad.insert(immediate_bad.end(),{0,0,0,0,0});
        rejects([&]{(void)odh::decode_vm_snapshot(immediate_bad);},
            "empty v4 extension rejected as noncanonical");
        auto bad_shape=immediate;bad_shape.immediate_return=odh::VmImmediateReturn{12,true,0,3};
        rejects([&]{(void)odh::encode_vm_snapshot(bad_shape);},
            "immediate return ticket must identify an active continuation");
        bad_shape=immediate;bad_shape.immediate_continuations[0].parent_depth=3;
        rejects([&]{(void)odh::encode_vm_snapshot(bad_shape);},
            "immediate parent depth bounded by stack");
        bad_shape=immediate;bad_shape.immediate_continuations[0].ticket=13;
        rejects([&]{(void)odh::encode_vm_snapshot(bad_shape);},
            "immediate continuation ticket bounded by sequence");
        bad_shape=immediate;bad_shape.immediate_continuations.clear();
        bad_shape.immediate_return=odh::VmImmediateReturn{12,true,0,3};
        rejects([&]{(void)odh::encode_vm_snapshot(bad_shape);},
            "immediate return without continuation rejected");
        bad_shape=immediate;bad_shape.immediate_continuations[0].parent_pending.reset();
        rejects([&]{(void)odh::encode_vm_snapshot(bad_shape);},
            "waiting parent requires its pending primitive");
        bad_shape=immediate;bad_shape.immediate_continuations.push_back(bad_shape.immediate_continuations.front());
        rejects([&]{(void)odh::encode_vm_snapshot(bad_shape);},
            "duplicate immediate continuation identity rejected");
        auto bounded=odh::VmSnapshotCodecLimits{};bounded.vm.stack_depth=1;
        rejects([&]{(void)odh::encode_vm_snapshot(immediate,bounded);},
            "immediate continuation respects configured stack bound");
        bad_shape=immediate;bad_shape.immediate_return=odh::VmImmediateReturn{11,true,0,3};
        bad_shape.immediate_continuations[0].parent_depth=1;
        bad_shape.immediate_continuations[0].parent_status=odh::VmStatus::running;
        bad_shape.immediate_continuations[0].parent_pending.reset();
        bad_shape.stack.resize(1);
        bad_shape.status=odh::VmStatus::yielded;
        same(bad_shape,odh::decode_vm_snapshot(odh::encode_vm_snapshot(bad_shape)));
        bad_shape=immediate;bad_shape.stack[1].invocation_ticket=10;
        rejects([&]{(void)odh::encode_vm_snapshot(bad_shape);},
            "immediate continuation must match the protected frame ticket");
        bad_shape=immediate;bad_shape.immediate_continuations[0].parent_pending->instruction.depth=0;
        rejects([&]{(void)odh::encode_vm_snapshot(bad_shape);},
            "immediate parent pending trace must match parent depth");

        {
            odh::VmSnapshot exact;
            exact.stack.push_back({odh::routine_key(4096),0,{1,1,2,0},{},{},odh::VmCallReturnMode::ordinary,0,true});
            exact.status=odh::VmStatus::budget_exhausted;exact.steps=1;exact.ticket_sequence=1;exact.run_generation=1;
            auto bytes=odh::encode_vm_snapshot(exact);
            require(bytes[0]==5&&bytes[1]==0,"exact argument frame selects required VM codec5");
            same(exact,odh::decode_vm_snapshot(bytes));
            const auto decoded=odh::decode_vm_snapshot(bytes);
            require(decoded.stack.front().arguments.empty()&&decoded.stack.front().exact_arguments,
                "codec5 preserves zero extent without padding");
            exact.stack.front().exact_arguments=false;
            const auto legacy=odh::encode_vm_snapshot(exact);
            require(legacy[0]==2,"ordinary frames retain previous codec2 layout");
            require(!odh::decode_vm_snapshot(legacy).stack.front().exact_arguments,
                "legacy frames decode with ordinary padded argument profile");
        }
        {
            odh::VmSnapshot original;original.run_generation=1;original.status=odh::VmStatus::running;original.action_tree_known=true;
            odh::VmFrame frame{};frame.key=odh::routine_key(4096);frame.arguments.resize(4);
            frame.original_import=odh::VmOriginalFrameState{-2147483647,odh::VmOriginalReturn::normal};original.stack.push_back(frame);
            const auto encoded=odh::encode_vm_snapshot(original);require(encoded[0]==9,"raw original primitive metadata selects VM codec9 only when needed");
            same(original,odh::decode_vm_snapshot(encoded));
            auto malformed=original;malformed.stack.front().original_import->result=odh::VmOriginalReturn::retry;
            rejects([&]{odh::encode_vm_snapshot(malformed);},"root frame cannot have an original Retry caller");
            auto values=malformed;values.status=odh::VmStatus::ready;values.run_generation=0;
            values.pending=parent_wait();values.stack.push_back(values.stack.front());
            values.suspended_pending.push_back(*values.pending);
            const auto value_bytes=odh::encode_vm_snapshot(values);
            same_route_values(values,odh::decode_vm_snapshot(value_bytes));
            odh::Vm cold({{odh::routine_key(4096),odh::Bhav{0x8002,{{27,254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}}}});
            require(!cold.restore_snapshot(odh::decode_vm_snapshot(value_bytes)),
                "routing value envelope preserves flattened original/replay metadata but never authorizes executable root Retry");
            original.stack.front().original_import.reset();const auto legacy=odh::encode_vm_snapshot(original);
            require(legacy[0]!=9,"ordinary snapshots retain their established codec selection");
        }
        std::cout << checks << " VM snapshot codec checks passed (synthetic values only)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
