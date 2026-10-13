#include "odh/vm.hpp"
#include <iostream>

namespace {
int checks=0;
void require(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
odh::RoutineKey key(unsigned id) {return odh::routine_key(static_cast<std::uint16_t>(id));}
odh::Instruction op(unsigned id,unsigned yes=254,unsigned no=255) {
    return {static_cast<std::uint16_t>(id),static_cast<std::uint8_t>(yes),static_cast<std::uint8_t>(no),{}};
}
odh::Bhav routine(std::initializer_list<odh::Instruction> instructions) {
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,4,4,0}};
}
odh::PrimitiveOutcome action(unsigned id=4097,odh::VmCallReturnMode mode=odh::VmCallReturnMode::interaction_next_tick) {
    return {odh::PrimitiveDisposition::call,{}, {},odh::VmCall{key(id),{1,22,23,7},{99},mode}};
}
}
int main() {
    try {
        for(bool child_result:{true,false}) {
            unsigned pushes=0;
            std::uint64_t invocation=0;
            odh::Vm* live=nullptr;
            bool resume_guarded=false;
            const auto parent=routine({op(17,1,2),op(18),op(19)});
            // The global child uses its interaction owner's resources and may
            // unwind ordinary nested routines before reaching the action root.
            odh::RoutineSet base{{key(4096),parent},{key(256),routine({op(8192)})}};
            odh::RoutineOwners owners{{7,{{key(4097),routine({op(256)})},
                {key(8192),routine({op(20,child_result?254:255,255)})}}}};
            odh::Vm nested(base, {}, {},
                [&](const auto& frame,const auto& i,const auto&,const auto& dispatch) {
                    try{live->resume_interaction(dispatch.ticket);}catch(const std::logic_error&){resume_guarded=true;}
                    if(i.opcode==17 && pushes++==0) {
                        invocation=dispatch.ticket;
                        auto out=action();out.write=odh::VmFrameWrite{odh::VmFrameBank::arguments,0,
                            static_cast<std::int16_t>(frame.arguments[0]-1)};
                        return out;
                    }
                    require(i.opcode!=19,"interaction result never branches parent");
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                },owners);
            live=&nested;
            require(nested.start(key(4096),{1,2,3,0},{10})==odh::VmStatus::running,"start interaction parent");
            require(!nested.resume_interaction(1),"no boundary cannot resume");
            require(nested.run(1)==odh::VmStatus::budget_exhausted && nested.stack().size()==2,"action push obeys budget");
            require(nested.stack()[0].arguments[0]==9 && nested.stack()[0].pc==0,"call output published before child executes");
            require(nested.stack()[1].invocation_ticket==invocation,"action frame keeps dispatch ticket");
            require(nested.run(1)==odh::VmStatus::budget_exhausted && nested.stack().size()==3,"ordinary global child permitted");
            require(nested.run(10)==odh::VmStatus::yielded && nested.stack().size()==1,"interaction pop stops instruction run");
            require(resume_guarded && nested.interaction_return() &&
                nested.interaction_return()->ticket==invocation && nested.interaction_return()->result==child_result &&
                nested.interaction_return()->code_owner==7 && nested.interaction_return()->run_generation==nested.run_generation(),
                "return event carries root result owner generation and callback resume guarded");
            const auto event=*nested.interaction_return();
            const auto steps=nested.steps();
            require(nested.run(50)==odh::VmStatus::yielded && nested.steps()==steps,"yielded run never redispatches");
            require(nested.stack()[0].pc==0 && nested.stack()[0].context.callee==2 && nested.stack()[0].context.stack_object==3 &&
                nested.stack()[0].context.code_owner==0 && nested.stack()[0].arguments[0]==9,"same parent instruction and context survive");
            require(!nested.complete(invocation,true) && !nested.resume_interaction(invocation+1),"service completion and wrong resume rejected");
            require(nested.resume_interaction(invocation) && !nested.interaction_return() &&
                nested.status()==odh::VmStatus::running && !nested.resume_interaction(invocation),"matching resume consumed once");
            require(nested.run(10)==odh::VmStatus::returned_true && pushes==2,"parent reexecutes same instruction then true edge");
            nested.start(key(4096));
            require(nested.run_generation()>event.run_generation && !nested.resume_interaction(event.ticket) &&
                !nested.interaction_return(),"restart clears boundary and rejects old generation");
        }
        // WAIT completion may directly unwind the interaction root, but never
        // consumes the invocation ticket or branches its preserved parent.
        std::uint64_t invocation=0;
        odh::Vm waiting({{key(4096),routine({op(17)})}}, {}, {},
            [&](const auto&,const auto& i,const auto&,const auto& dispatch) {
                if(i.opcode==17) {invocation=dispatch.ticket;return action();}
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
            },{{7,{{key(4097),routine({op(20)})}}}});
        waiting.start(key(4096));
        require(waiting.run(10)==odh::VmStatus::waiting,"interaction child supports WAIT");
        const auto service=waiting.pending()->ticket;
        require(service!=invocation && !waiting.resume_interaction(invocation),"WAIT and invocation tickets distinct");
        require(waiting.complete(service,false) && waiting.status()==odh::VmStatus::yielded &&
            waiting.interaction_return()->ticket==invocation && !waiting.interaction_return()->result &&
            waiting.stack()[0].pc==0 && !waiting.pending(),"WAIT false publishes action boundary without parent branch");
        require(!waiting.complete(service,true),"duplicate WAIT completion rejected at boundary");
        const auto old_generation=waiting.run_generation();
        require(waiting.start(key(9999))==odh::VmStatus::fault && !waiting.interaction_return() &&
            waiting.run_generation()>old_generation && !waiting.resume_interaction(invocation),"faulted start also invalidates event");

        for(unsigned variant=0;variant<4;++variant) {
            odh::Vm invalid({{key(4096),routine({op(17)})}}, {}, {},
                [&](const auto&,const auto& i,const auto&,const auto&) {
                    auto out=action();out.write=odh::VmFrameWrite{odh::VmFrameBank::arguments,0,55};
                    if(variant==0)out.call->context.code_owner=99;
                    if(variant==1)out.write->index=4;
                    if(variant==2)out.call->return_mode=static_cast<odh::VmCallReturnMode>(99);
                    if(variant==3 && i.opcode==20)out.call->key=key(4098);
                    return out;
                },{{7,{{key(4097),routine({op(20)})},{key(4098),routine({op(21)})}}}});
            invalid.start(key(4096),{}, {10});
            require(invalid.run(variant==3?10:1)==odh::VmStatus::fault,"invalid call or nested interaction faults");
            if(variant<3)require(invalid.stack().size()==1 && invalid.stack()[0].arguments[0]==10 &&
                invalid.stack()[0].pc==0,"invalid child and output leave parent atomic");
            else require(invalid.stack().size()==2 && invalid.stack()[1].arguments[0]==99 &&
                !invalid.interaction_return(),"nested action rejected before frame output or push");
        }
        // Default explicit calls preserve the original branch-return behavior.
        odh::Vm ordinary({{key(4096),routine({op(17,254,255)})}}, {}, {},
            [](const auto&,const auto&,const auto&,const auto&) {return action(4097,odh::VmCallReturnMode::ordinary);},
            {{7,{{key(4097),routine({{2,255,255,{0,0,0,0,0,2,7,7}}})}}}});
        ordinary.start(key(4096));
        require(ordinary.run(10)==odh::VmStatus::returned_false && !ordinary.interaction_return(),"ordinary child false still branches caller");
        odh::Vm* retry_live=nullptr;
        bool retry_guarded=false;
        unsigned retry_dispatches=0;
        odh::Vm retry({}, {}, {},
            [&](const auto&,const auto&,const auto&,const auto& dispatch) {
                ++retry_dispatches;
                try{retry_live->retry_pending(dispatch.ticket);}catch(const std::logic_error&){retry_guarded=true;}
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
            },{{7,{{key(4096),routine({op(17)})}}}});
        retry_live=&retry;retry.start(key(4096),{1,2,3,7},{10});retry.run(1);
        const auto ticket=retry.pending()->ticket;
        const auto steps=retry.steps();
        require(!retry.retry_pending(ticket+1,odh::VmFrameWrite{odh::VmFrameBank::arguments,0,55}) &&
            !retry.retry_pending(ticket,odh::VmFrameWrite{odh::VmFrameBank::arguments,4,55}) &&
            retry.pending()->ticket==ticket && retry.stack()[0].arguments[0]==10,"stale and invalid retry leave wait/output unchanged");
        require(retry.retry_pending(ticket,odh::VmFrameWrite{odh::VmFrameBank::arguments,0,9}) &&
            retry.status()==odh::VmStatus::running && !retry.pending() && retry.steps()==steps &&
            retry.stack()[0].pc==0 && retry.stack()[0].context.code_owner==7 && retry.stack()[0].arguments[0]==9,
            "retry keeps same owner PC and updates frame without executing");
        require(!retry.retry_pending(ticket),"retry ticket consumed once");
        require(retry.run(1)==odh::VmStatus::waiting && retry_dispatches==2 && retry_guarded &&
            retry.steps()==steps+1 && retry.pending()->ticket>ticket,"retry redispatches only on next run with new ticket");
        const auto stale=retry.pending()->ticket;retry.start(key(4096),{1,2,3,7});
        require(!retry.retry_pending(stale),"restart invalidates service retry ticket");
        require(std::string(odh::vm_status_name(odh::VmStatus::yielded))=="yielded","yielded status name");
        std::cout<<checks<<" interaction VM checks passed (synthetic fixtures only)\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
