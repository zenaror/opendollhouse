#include "odh/saved_thread_plan.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        odh::SavedWorldEntity entity{};
        entity.saved.object_id = 105;
        entity.saved.prefix.temps = {1,2,3,4,5,6,7,8};
        odh::ObjmStackFrame base{};
        base.stack_object_id = 12; base.tree_id = 4096; base.node_id = 9;
        base.parameters = {31}; base.locals = {41,42}; base.primitive_state = 777;
        base.code_owner_obj_type = 1;
        odh::ObjmStackFrame child{};
        child.stack_object_id = 13; child.tree_id = 8192; child.node_id = 2;
        child.code_owner_obj_type = 2;
        entity.common.stack = {base, child};
        entity.tail.person.emplace();
        entity.tail.person->object_uses.push_back({13, 2, 0});

        odh::Objt types{};
        types.entries.resize(2);
        types.entries[0].guid = 0x12345678;
        types.entries[1].guid = 0x87654321;
        const auto plan = odh::make_simitone_saved_thread_plan(entity, types);
        require(plan.entity_id == 105 && plan.frames.size() == 2,
                "saved actor and both frames retained");
        require(plan.temp_registers[0] == 1 && plan.temp_registers[7] == 8 &&
                plan.temp_registers[8] == 0 && plan.temp_registers[19] == 0,
                "eight saved temps padded to twenty");
        require(plan.frames[0].routine_id == 4096 && plan.frames[0].instruction_pointer == 9 &&
                plan.frames[0].code_owner_guid == 0x12345678,
                "first frame routine, node, and owner resolved");
        require(plan.frames[0].arguments == std::vector<std::int16_t>({31,0,0,0}) &&
                plan.frames[0].locals == std::vector<std::int16_t>({41,42}),
                "arguments padded and locals preserved");
        require(plan.frames[1].code_owner_guid == 0x87654321 &&
                plan.frames[0].callee == 105 && plan.frames[1].callee == 13,
                "ObjectUses callee mapping applies from saved stack depth");
        require(plan.frames[0].saved_primitive_state == 777,
                "uninterpreted primitive state remains visible");
        require(plan.object_uses.size() == 1 &&
                plan.object_uses[0].target_id == 13 && plan.object_uses[0].stack_length == 2 &&
                plan.frames[0].return_state == odh::SimitoneSavedReturnState::unresolved,
                "object-use depth is retained and return state stays unresolved without code lookup");

        odh::RoutineSet private_code{{odh::routine_key(4096),
            odh::Bhav{0x8002,std::vector<odh::Instruction>(10,odh::Instruction{2,0xFE,0xFF,{}}),{},
                odh::Bhav::Metadata{0,4,2,0}}}};
        odh::RoutineSet semiglobal_code{{odh::routine_key(8192),
            odh::Bhav{0x8002,std::vector<odh::Instruction>(10,odh::Instruction{2,0xFE,0xFF,{}}),{},
                odh::Bhav::Metadata{0,4,2,0}}}};
        odh::RoutineOwners owner_code{{0x12345678,std::move(private_code)},
                                       {0x87654321,std::move(semiglobal_code)}};
        auto partial_owner_code=owner_code;
        odh::Vm restored_vm({}, {}, {}, {}, std::move(owner_code));
        auto normal_plan=plan;
        for (auto& frame : normal_plan.frames)
            frame.return_state=odh::SimitoneSavedReturnState::normal;
        odh::Vm no_code_vm(odh::RoutineSet{}, {}, {}, {}, {});
        bool unavailable_code_rejected=false;
        try { odh::restore_simitone_saved_thread_plan(no_code_vm,normal_plan); }
        catch (const std::invalid_argument&) { unavailable_code_rejected=true; }
        require(unavailable_code_rejected && no_code_vm.status()==odh::VmStatus::ready,
                "VM rejects saved frames whose routines are not installed");
        odh::restore_simitone_saved_thread_plan(restored_vm,normal_plan);
        require(restored_vm.status()==odh::VmStatus::dormant && restored_vm.stack().size()==2 &&
                restored_vm.stack()[0].key.id==4096 && restored_vm.stack()[0].pc==9 &&
                restored_vm.stack()[0].context.code_owner==0x12345678 &&
                restored_vm.stack()[1].key.id==8192 && restored_vm.stack()[1].pc==2 &&
                restored_vm.stack()[1].context.callee==13 &&
                restored_vm.temps()==normal_plan.temp_registers,
                "normal-return saved plan imports owner-resolved frames and temps into dormant VM");
        auto partially_mapped_plan=plan;
        partially_mapped_plan.object_uses.push_back({105,1});
        partially_mapped_plan.frames[1].return_state=odh::SimitoneSavedReturnState::normal;
        odh::Vm partially_mapped_vm({}, {}, {}, {}, std::move(partial_owner_code));
        odh::restore_simitone_saved_thread_plan(partially_mapped_vm,partially_mapped_plan);
        require(partially_mapped_vm.status()==odh::VmStatus::dormant&&
                partially_mapped_vm.stack().size()==2&&
                partially_mapped_vm.stack()[0].return_mode==odh::VmCallReturnMode::ordinary,
                "unmapped depth-one ObjectUses keeps Simitone Normal default beside an audited frame");
        bool unresolved_use_rejected=false;
        try { odh::restore_simitone_saved_thread_plan(partially_mapped_vm,plan); }
        catch (const odh::FormatError&) { unresolved_use_rejected=true; }
        require(unresolved_use_rejected&&partially_mapped_vm.stack().size()==2&&
                partially_mapped_vm.stack()[0].key.id==4096,
                "frame with unaudited caller opcode remains rejected without changing its VM");
        auto object_entity=entity;
        object_entity.saved.object_id=207;
        object_entity.tail.person.reset();
        const auto object_plan=odh::make_simitone_saved_thread_plan(object_entity,types);
        odh::RoutineOwners object_owners{{0x12345678,{{odh::routine_key(4096),
            odh::Bhav{0x8002,std::vector<odh::Instruction>(10,odh::Instruction{2,0xFE,0xFF,{}}),{},
                odh::Bhav::Metadata{0,4,2,0}}}}},
            {0x87654321,{{odh::routine_key(8192),
            odh::Bhav{0x8002,std::vector<odh::Instruction>(10,odh::Instruction{2,0xFE,0xFF,{}}),{},
                odh::Bhav::Metadata{0,4,2,0}}}}}};
        odh::Vm restored_object({}, {}, {}, {}, std::move(object_owners));
        odh::restore_simitone_saved_thread_plan(restored_object,object_plan);
        require(object_plan.object_uses.empty()&&
                object_plan.frames[0].return_state==odh::SimitoneSavedReturnState::unresolved&&
                restored_object.status()==odh::VmStatus::dormant&&restored_object.stack().size()==2&&
                restored_object.stack()[0].context.caller==207&&
                restored_object.stack()[1].context.callee==207&&
                restored_object.temps()==object_plan.temp_registers,
                "saved object thread defaults unannotated frames to Simitone Normal and remains dormant");
        {
            constexpr odh::CodeOwner host_owner=0x80000050U;
            auto source=object_plan;source.frames.resize(1);
            const auto original=source.frames.front();
            auto restored_locals=original.locals;
            restored_locals.resize(4,0); // Existing VM metadata requires max(4 arguments,2 locals).
            auto instructions=std::vector<odh::Instruction>(10,odh::Instruction{100,254,255,{}});
            instructions[9]={99,254,255,{}};
            unsigned calls=0,mappings=0;
            odh::Vm remapped({}, {}, {},
                [&](const odh::VmFrame& frame,const odh::Instruction& instruction,const odh::Ts1Temps& temps,const auto&){
                    require(instruction.opcode==99 && frame.pc==9 &&
                        frame.context.code_owner==host_owner && frame.context.caller==207 &&
                        frame.context.callee==207 && frame.context.stack_object==12 &&
                        frame.arguments==original.arguments && frame.locals==restored_locals && temps==source.temp_registers,
                        "remapped saved object executes the exact retained instruction and frame under host bank");
                    ++calls;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                },odh::RoutineOwners{{host_owner,{{odh::routine_key(4096),
                    odh::Bhav{0x8002,std::move(instructions),{},odh::Bhav::Metadata{0,4,2,0}}}}}});
            const odh::SavedThreadCodeOwnerMapper mapping=[&](std::uint32_t guid){
                require(guid==original.code_owner_guid,"mapper receives source resource GUID, not entity ID");
                ++mappings;return host_owner;};
            odh::restore_simitone_saved_thread_plan(remapped,source,mapping);
            require(remapped.status()==odh::VmStatus::dormant && remapped.stack().size()==1 &&
                remapped.stack()[0].context.code_owner==host_owner && remapped.stack()[0].pc==9 &&
                remapped.stack()[0].arguments==original.arguments && remapped.stack()[0].locals==restored_locals &&
                remapped.temps()==source.temp_registers && mappings==1,
                "host owner mapping preserves dormant saved frame data and temps");
            require(remapped.activate_dormant_state() && remapped.run(1)==odh::VmStatus::returned_true && calls==1,
                "mapped owner resolves installed host code and runs after explicit activation");
            require(source.frames[0].code_owner_guid==0x12345678 && source.frames[0].code_owner_objt_type==1 &&
                source.frames[0].saved_primitive_state==777 && source.frames[0].instruction_pointer==9 &&
                source.frames[0].arguments==original.arguments && source.frames[0].locals==original.locals &&
                source.frames[0].return_state==original.return_state && source.frames[0].action_tree==original.action_tree,
                "import and execution never rewrite source GUID/context/primitive/return provenance");
            auto duplicate_owner=source;duplicate_owner.frames.push_back(duplicate_owner.frames.front());
            odh::Vm repeated({}, {}, {}, {},odh::RoutineOwners{{host_owner,{{odh::routine_key(4096),
                odh::Bhav{0x8002,std::vector<odh::Instruction>(10,odh::Instruction{2,254,255,{}}),{},
                    odh::Bhav::Metadata{0,4,2,0}}}}}});
            mappings=0;odh::restore_simitone_saved_thread_plan(repeated,duplicate_owner,mapping);
            require(mappings==1 && repeated.stack()[0].context.code_owner==host_owner &&
                repeated.stack()[1].context.code_owner==host_owner,"one source resource GUID maps consistently across frames");
            odh::Vm untouched(odh::RoutineSet{});
            const auto unchanged=[&]{require(untouched.status()==odh::VmStatus::ready && untouched.stack().empty() &&
                untouched.run_generation()==0 && untouched.steps()==0 && !untouched.pending() &&
                untouched.temps()==odh::Ts1Temps{},"mapping rejection precedes any VM restore/publication");};
            bool rejected=false;
            try{odh::restore_simitone_saved_thread_plan(untouched,source,odh::SavedThreadCodeOwnerMapper{});}
            catch(const odh::FormatError&){rejected=true;}
            require(rejected,"absent host owner resolver rejected");unchanged();
            rejected=false;unsigned resolved=0;
            try{odh::restore_simitone_saved_thread_plan(untouched,object_plan,[&](std::uint32_t guid){
                ++resolved;return guid==0x12345678?host_owner:odh::CodeOwner{0};});}
            catch(const odh::FormatError&){rejected=true;}
            require(rejected && resolved==2,"unmapped later frame rejects whole import rather than partially restoring first frame");unchanged();
            auto zero_guid=source;zero_guid.frames[0].code_owner_guid=0;resolved=0;rejected=false;
            try{odh::restore_simitone_saved_thread_plan(untouched,zero_guid,[&](std::uint32_t){++resolved;return host_owner;});}
            catch(const odh::FormatError&){rejected=true;}
            require(rejected && resolved==0,"zero source GUID rejects before resolver or VM mutation");unchanged();
            auto invalid_return=source;invalid_return.frames[0].return_state=odh::SimitoneSavedReturnState::retry;
            rejected=false;
            try{odh::restore_simitone_saved_thread_plan(untouched,invalid_return,mapping);}
            catch(const odh::FormatError&){rejected=true;}
            require(rejected,"mapped import retains unsupported return-state guard");unchanged();
        }
        auto special_plan=normal_plan;
        special_plan.frames[1].return_state=odh::SimitoneSavedReturnState::retry;
        bool special_state_rejected=false;
        try { odh::restore_simitone_saved_thread_plan(restored_vm,special_plan); }
        catch (const odh::FormatError&) { special_state_rejected=true; }
        require(special_state_rejected && restored_vm.status()==odh::VmStatus::dormant &&
                restored_vm.stack()[1].pc==2,
                "unsupported return state is rejected without changing restored VM");

        auto missing_opcode = plan;
        try {
            odh::apply_simitone_saved_return_mapping(missing_opcode, {});
            throw std::runtime_error("missing saved caller opcode should fail");
        } catch (const odh::FormatError&) {}

        auto map_opcode = [](std::uint16_t opcode) {
            return [opcode](const odh::SimitoneSavedFramePlan&) {
                return std::optional<std::uint16_t>{opcode};
            };
        };
        for (const auto opcode : {std::uint16_t{27}, std::uint16_t{45}}) {
            auto retry = plan;
            odh::apply_simitone_saved_return_mapping(retry, map_opcode(opcode));
            require(retry.frames[1].return_state == odh::SimitoneSavedReturnState::retry &&
                    !retry.frames[1].action_tree,
                    "GotoRelative and GotoSlot restore Retry return state");
        }
        for (const auto opcode : {std::uint16_t{30}, std::uint16_t{17}}) {
            auto interaction = plan;
            interaction.frames.push_back(plan.frames[1]);
            odh::apply_simitone_saved_return_mapping(interaction, map_opcode(opcode));
            require(interaction.frames[1].return_state == odh::SimitoneSavedReturnState::interaction &&
                    interaction.frames[1].action_tree && interaction.frames[2].action_tree &&
                    interaction.frames[2].callee == 13,
                    "Gosub and IdleForInput restore interaction state through the top frame");
        }
        auto ordinary = plan;
        odh::apply_simitone_saved_return_mapping(ordinary, map_opcode(2));
        require(ordinary.frames[1].return_state == odh::SimitoneSavedReturnState::normal &&
                !ordinary.frames[1].action_tree,
                "other caller instructions restore the normal return state");
        auto absent_opcode = plan;
        try {
            odh::apply_simitone_saved_return_mapping(absent_opcode,
                [](const odh::SimitoneSavedFramePlan&) -> std::optional<std::uint16_t> { return {}; });
            throw std::runtime_error("missing caller instruction should fail");
        } catch (const odh::FormatError&) {}

        auto rejects = [&](auto mutate, const char* message) {
            auto bad = entity; mutate(bad);
            try { (void)odh::make_simitone_saved_thread_plan(bad, types); }
            catch (const odh::FormatError&) { return; }
            throw std::runtime_error(message);
        };
        rejects([](auto& e) { e.common.stack[0].code_owner_obj_type = 0; },
                "zero OBJT owner rejected");
        rejects([](auto& e) { e.saved.object_id = 65535; },
                "out-of-range actor ID rejected");
        rejects([](auto& e) { e.common.stack[0].code_owner_obj_type = 3; },
                "out-of-range OBJT owner rejected");
        rejects([](auto& e) { e.tail.person->object_uses[0].stack_length = 3; },
                "ObjectUses frame beyond saved stack rejected");

        types.entries[0].guid = 0;
        rejects([](auto&) {}, "zero GUID code owner rejected");

        auto saved_world=std::make_unique<odh::SavedWorld>();
        saved_world->world=std::make_unique<odh::World>();
        for(const auto id:{std::int16_t{101},std::int16_t{102},std::int16_t{104}}){
            require(saved_world->world->create(id,odh::EntityKind::avatar),
                    "synthetic saved session avatar exists");
            odh::SavedWorldEntity actor{};
            actor.saved.object_id=static_cast<std::uint16_t>(id);
            actor.tail.person.emplace();
            odh::ObjmStackFrame frame{};frame.tree_id=280;frame.node_id=0;
            actor.common.stack.push_back(frame);
            saved_world->records.push_back(std::move(actor));
        }
        require(saved_world->world->create(103,odh::EntityKind::object),
                "synthetic saved session non-avatar exists");
        odh::SavedWorldEntity saved_object{};saved_object.saved.object_id=103;
        odh::ObjmStackFrame object_frame{};object_frame.tree_id=280;object_frame.node_id=0;
        saved_object.common.stack.push_back(object_frame);
        saved_world->records.push_back(std::move(saved_object));
        odh::SavedWorldSession session(std::move(saved_world),3);
        auto dormant_for=[&](std::int16_t id){
            odh::RoutineSet global{{odh::routine_key(280),
                odh::Bhav{0x8002,{odh::Instruction{2,0xFE,0xFF,{}}},{},
                    odh::Bhav::Metadata{0,4,0,0}}}};
            auto vm=std::make_unique<odh::Vm>(std::move(global),
                session.world().vm_memory());
            const odh::VmFrame saved_frame{odh::routine_key(280),0,{id,id,id,0},{},{}};
            vm->restore_dormant_state({saved_frame},{});
            return vm;
        };
        auto session_rejects=[&](auto action,const char* message){
            try{action();}catch(const odh::FormatError&){return;}
            throw std::runtime_error(message);
        };
        session.retain_dormant_thread(101,dormant_for(101));
        session_rejects([&]{session.retain_dormant_thread(101,dormant_for(101));},
                        "duplicate saved avatar VM rejected");
        session_rejects([&]{session.retain_dormant_thread(999,dormant_for(999));},
                        "missing saved avatar rejected");
        session_rejects([&]{session.retain_dormant_thread(104,dormant_for(101));},
                        "wrong frame caller rejected");
        session_rejects([&]{session.retain_dormant_thread(104,std::make_unique<odh::Vm>(odh::RoutineSet{}));},
                        "ready VM rejected as saved dormant thread");
        session.retain_dormant_thread(102,dormant_for(102));
        session.retain_dormant_thread(103,dormant_for(103));
        require(session.thread_count()==3 && session.all_dormant_bindings_valid() &&
                session.dormant_thread(101) && session.dormant_thread(102) &&
                session.dormant_thread(103) &&
                session.dormant_thread(101)->status()==odh::VmStatus::dormant &&
                session.dormant_thread(101)->steps()==0 &&
                session.dormant_thread(102)->stack()[0].context.caller==102,
                "saved avatar and object VMs retained independently without execution");
        session_rejects([&]{session.retain_dormant_thread(104,dormant_for(104));},
                        "saved thread capacity enforced");
        require(session.thread_count()==3 && session.all_dormant_bindings_valid(),
                "rejected thread imports leave the valid saved VMs intact");
        require(session.world().remove(101) &&
                session.world().create(101,odh::EntityKind::avatar),
                "synthetic actor can be replaced at the same ID");
        require(!session.dormant_thread(101) && session.dormant_thread(102) &&
                session.dormant_thread(103) &&
                !session.all_dormant_bindings_valid(),
                "generation change invalidates only the replaced entity's saved VM");
        std::cout << "saved Simitone thread plan and session checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "saved thread plan test failed: " << e.what() << '\n';
        return 1;
    }
}
