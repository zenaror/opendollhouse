#include "odh/vm.hpp"
#include <iostream>

namespace {
int checks=0;
void require(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
odh::Instruction assign(int index,int value,unsigned scope=8,unsigned yes=254) {
    return {2,static_cast<std::uint8_t>(yes),255,{static_cast<std::uint8_t>(index),0,
        static_cast<std::uint8_t>(value&255),static_cast<std::uint8_t>((value>>8)&255),0,5,
        static_cast<std::uint8_t>(scope),7}};
}
odh::Instruction call(unsigned id,unsigned yes=254,unsigned no=255) {
    return {static_cast<std::uint16_t>(id),static_cast<std::uint8_t>(yes),static_cast<std::uint8_t>(no),{}};
}
odh::Bhav routine(std::initializer_list<odh::Instruction> instructions) {
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,4,4,0}};
}
odh::RoutineKey key(unsigned id) {return odh::routine_key(static_cast<std::uint16_t>(id));}
odh::RoutineSet bank(int value) {
    return {{key(4096),routine({call(256)})},{key(4097),routine({assign(0,value)})},
            {key(8192),routine({assign(1,value+100)})}};
}
odh::RoutineSet base() {
    auto b=bank(7);b.emplace(key(256),routine({call(8192,1),call(4097)}));return b;
}
template<class F>void rejects(F function,const char* message) {
    bool rejected=false;try{function();}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,message);
}
}
int main() {
    try {
        odh::RoutineOwners owners{{11,bank(11)},{22,bank(22)}};
        odh::Vm vm(base(),{},{},{},owners);
        for(auto owner:{0U,11U,22U}) {
            const int expected=owner==0?7:static_cast<int>(owner);
            vm.start(key(4096),{1,2,2,owner});std::vector<odh::CodeOwner> traced;
            require(vm.run(10,[&](const auto& t){traced.push_back(t.code_owner);})==odh::VmStatus::returned_true,
                    "same private IDs execute separately by owner");
            require(vm.temps()[0]==expected&&vm.temps()[1]==expected+100,"global resolves semiglobal/private from preserved owner");
            require(traced==std::vector<odh::CodeOwner>(5,owner),"all nested trace instructions retain code owner");
        }
        // Callee/StackObject are independent entity identities, not code banks.
        auto changed=bank(11);changed[key(4096)]=routine({assign(0,22,10,1),call(256)});
        owners[11]=changed;
        odh::Vm mutate(base(),{},{},{},owners);mutate.start(key(4096),{1,22,22,11});
        require(mutate.run(10)==odh::VmStatus::returned_true&&mutate.temps()[0]==11,"StackObject and callee do not select code owner");
        owners[33]={{key(4096),routine({call(4097)})}};
        odh::Vm missing(base(),{},{},{},owners);missing.start(key(4096),{1,2,2,33});
        require(missing.run(10)==odh::VmStatus::fault&&missing.stack().size()==1,"missing owner-private routine does not fall back to default");
        require(missing.diagnostic().find("code owner 33")!=std::string::npos,"missing resource diagnostic includes owner");
        require(missing.start(key(256),{1,2,2,99})==odh::VmStatus::fault&&missing.stack().empty(),"unknown owner rejects even global root");
        owners[33]={{key(4096),routine({call(8192)})}};
        odh::Vm no_semi(base(),{},{},{},owners);no_semi.start(key(4096),{1,2,2,33});
        require(no_semi.run(10)==odh::VmStatus::fault,"missing semiglobal does not fall back across owners");
        auto root=base();root[key(4096)]=routine({call(20,1),assign(2,99)});
        const auto invoke=[](const auto&,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome {
            return {odh::PrimitiveDisposition::call,{}, {},odh::VmCall{key(4096),{1,2,2,22},{17,18}}};
        };
        odh::Vm cross(root,{},{},invoke,owners);cross.start(key(4096),{1,1,1,0});
        require(cross.run(1)==odh::VmStatus::budget_exhausted&&cross.stack().size()==2,"explicit owner child preserves suspended parent");
        require(cross.stack()[0].pc==0&&cross.stack()[0].context.code_owner==0&&cross.stack()[1].context.code_owner==22,"parent and child with same routine ID remain distinct");
        require(cross.stack()[1].context.caller==1&&cross.stack()[1].context.callee==2&&cross.stack()[1].arguments[0]==17,"explicit invocation supplies context and arguments");
        require(cross.run(1)==odh::VmStatus::budget_exhausted&&cross.stack().size()==3&&cross.stack()[2].context.code_owner==22,"budget resume retains child global owner");
        std::vector<odh::CodeOwner> resumed;
        require(cross.run(10,[&](const auto& t){resumed.push_back(t.code_owner);})==odh::VmStatus::returned_true,"child return takes saved primitive branch and resumes parent");
        require(cross.temps()[0]==22&&cross.temps()[1]==122&&cross.temps()[2]==99&&resumed.back()==0,"return restores parent owner and shares thread temps");
        auto blocked=bank(22);blocked[key(4096)]=routine({call(17)});
        auto waiting_owners=owners;waiting_owners[22]=blocked;
        odh::Vm waiting(root,{},{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
            if(i.opcode==20)return invoke(f,i,t,d);
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
        },waiting_owners);
        waiting.start(key(4096),{1,1,1,0});
        require(waiting.run(10)==odh::VmStatus::waiting&&waiting.stack().size()==2,"cross-owner child may wait with parent intact");
        const auto ticket=waiting.pending()->ticket;
        require(waiting.pending()->context.code_owner==22&&waiting.pending()->instruction.code_owner==22,"pending context and trace carry owner");
        require(!waiting.complete(ticket+1,true)&&waiting.stack().size()==2,"stale completion leaves both owner frames intact");
        require(waiting.complete(ticket,true)&&waiting.stack().size()==1&&waiting.stack()[0].pc==1,"child completion unwinds to parent's branch");
        require(waiting.stack()[0].context.code_owner==0&&waiting.stack()[0].context.caller==1&&
                waiting.stack()[0].context.callee==1&&waiting.stack()[0].context.stack_object==1,"child context never overwrites preserved parent identity");
        require(!waiting.complete(ticket,true),"duplicate child completion cannot advance restored parent");
        require(waiting.run(1)==odh::VmStatus::returned_true&&waiting.temps()[2]==99,"parent executes once after wait return");
        auto false_root=root;false_root[key(4096)]=routine({call(20,1,2),assign(2,99),assign(2,44)});
        auto false_owners=owners;false_owners[22][key(4096)]=routine({assign(0,22,8,255)});
        odh::Vm false_child(false_root,{},{},invoke,false_owners);false_child.start(key(4096));
        require(false_child.run(10)==odh::VmStatus::returned_true&&false_child.temps()[0]==22&&false_child.temps()[2]==44,
                "child false return takes parent's false edge in restored owner");
        odh::CodeOwner read_owner=0;
        auto resource_owners=owners;resource_owners[22][key(4096)]=routine({assign(0,5,18)});
        odh::Vm resource_child(root,{{},[&](const auto& f,auto scope,auto,auto){read_owner=f.context.code_owner;return scope==18;}},
                              {},invoke,resource_owners);
        resource_child.start(key(4096));
        require(resource_child.run(10)==odh::VmStatus::returned_true&&read_owner==22,"memory callbacks receive explicit child resource owner");
        // Missing/ambiguous payloads and invalid owners fail before installing child.
        for(int variant=0;variant<5;++variant) {
            odh::Vm bad(root,{},{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
                auto o=invoke(f,i,t,d);
                if(variant==0)o.call.reset();
                if(variant==1)o.write=odh::VmFrameWrite{odh::VmFrameBank::arguments,0,77};
                if(variant==2)o.disposition=odh::PrimitiveDisposition::goto_true;
                if(variant==3)o.call->context.code_owner=99;
                if(variant==4)o.call->arguments.resize(5);
                return o;
            },owners);
            bad.start(key(4096),{1,1,1,0},{3});
            require(bad.run(1)==odh::VmStatus::fault&&bad.stack().size()==1&&bad.stack()[0].arguments[0]==3&&bad.stack()[0].pc==0,"invalid explicit call is atomic for stack and parent output");
        }
        odh::Vm limited(root,{},odh::VmLimits{1,4096,64},invoke,owners);limited.start(key(4096));
        require(limited.run(1)==odh::VmStatus::fault&&limited.stack().size()==1,"cross-owner invocation obeys existing stack bound");
        rejects([&]{odh::Vm invalid(base(),{},{},{},{{0,bank(1)}});},"default owner cannot be overwritten");
        rejects([&]{odh::Vm invalid(base(),{},{},{},{{11,{{key(256),routine({assign(0,1)})}}}});},"global bank cannot be overridden per owner");
        rejects([&]{odh::Vm invalid(base(),{},{},{},{{11,{{{odh::RoutineScope::semiglobal,4096},routine({assign(0,1)})}}}});},"owner key scope must match numeric range");
        rejects([&]{odh::Vm invalid(base(),{},odh::VmLimits{64,4096,2},{},{{11,bank(1)},{22,bank(2)}});},"owner count includes default owner");
        odh::Vm exact(base(),{},odh::VmLimits{64,4096,2},{},{{11,bank(11)}});exact.start(key(4096),{1,2,2,11});
        require(exact.run(10)==odh::VmStatus::returned_true,"owner count at configured bound works");
        // Owned program is immutable with respect to subsequent caller changes.
        auto external=bank(22);odh::Vm immutable(base(),{},{},{},{{22,external}});
        external[key(4097)]=routine({assign(0,999)});immutable.start(key(4096),{1,2,2,22});
        require(immutable.run(10)==odh::VmStatus::returned_true&&immutable.temps()[0]==22,"VM owns isolated resource copies");
        std::cout<<checks<<" owner VM checks passed (synthetic fixtures only)\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
