#include "odh/dialog.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool v,const char* m){++checks;if(!v)throw std::runtime_error(m);}
using Bytes=std::array<std::uint8_t,8>;
odh::RoutineSet program(std::uint16_t opcode,Bytes bytes){return {{odh::routine_key(4096),odh::Bhav{0x8002,{{opcode,254,255,bytes}}, {},odh::Bhav::Metadata{0,0,0,0}}}};}
struct Fixture {
    odh::World world;unsigned shown=0;odh::DialogStringScope seen=odh::DialogStringScope::private_resource;
    odh::Vm* owner=nullptr;bool frontend=true,replace_prepare=false,replace_show=false;std::vector<odh::DialogChoice> choices;
    odh::DialogController dialogs;
    Fixture():dialogs(world,services()){
        require(world.create(1,odh::EntityKind::avatar,{})&&world.create(2,odh::EntityKind::object,{}),"dialog fixture");
    }
    odh::DialogServices services(){return {
        [&](const odh::Vm& vm)->std::optional<odh::WorldEntityReference>{if(&vm!=owner)return {};return odh::WorldEntityReference{1,world.find(1)->generation};},
        [&](auto scope,const auto&,auto,std::string&)->std::optional<odh::DialogPrompt>{seen=scope;if(replace_prepare){require(world.remove(1)&&world.create(1,odh::EntityKind::avatar,{}),"prepare retained effect");}return odh::DialogPrompt{"title","message","","Yes","No","Cancel",choices};},
        [&](const auto&,bool,std::string& why){++shown;if(replace_show){require(world.remove(1)&&world.create(1,odh::EntityKind::avatar,{}),"show retained effect");}if(!frontend)why="unavailable frontend";return frontend;},
        [&](auto ref,const auto& text,std::string&){return world.set_name(ref,text);}};}
    odh::VmContext context(){odh::VmContext c{1,1,2};require(world.bind_context_live(c),"bind dialog roles");return c;}
};
}
int main(){try{
    for(auto opcode:{36,38,39})for(unsigned type:{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}){
        Fixture f;f.choices={{"first",type==10?34:40000}};Bytes bytes{};bytes[5]=static_cast<std::uint8_t>(type);bytes[7]=32;
        odh::Vm vm{program(static_cast<std::uint16_t>(opcode),bytes),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        vm.start(odh::routine_key(4096),f.context());require(vm.run(1)==odh::VmStatus::waiting&&f.dialogs.pending(),"all common kinds establish genuine wait");
        const auto ticket=f.dialogs.pending()->ticket;
        require(f.seen==(opcode==36?odh::DialogStringScope::private_resource:opcode==38?odh::DialogStringScope::global_resource:odh::DialogStringScope::semiglobal_resource),"all3 dialog resource scopes are explicit");
        const auto saved=*f.dialogs.pending();const auto bytes_saved=odh::encode_dialog_state(saved);
        const auto decoded=odh::decode_dialog_state(bytes_saved);
        require(odh::encode_dialog_state(decoded)==bytes_saved&&odh::dialog_matches_vm_snapshot(decoded,vm.snapshot()),"pending prompt/typedowner/frame roundtrip");
        std::string diagnostic;
        require(!f.dialogs.respond(ticket+1,{0,""},diagnostic)&&vm.status()==odh::VmStatus::waiting,"stale ticket has no effects");
        odh::DialogResponse response{0,""};if(type==1||type==2)response.code=2;
        if(type==3)response.text=std::string(40,'x');
        if(odh::dialog_selector_type(static_cast<std::uint8_t>(type))){response.text=type==10?"34":"40000";response.code=1;}
        require(f.dialogs.respond(ticket,response,diagnostic),"actual response consumes its pending instruction");
        require(vm.status()==((type==1||type==2)?odh::VmStatus::returned_false:odh::VmStatus::returned_true),"source response selects authored branch");
        if(type==2)require(vm.temps()[1]==1,"cancel differs from no via selectedTemp");
        if(type==10)require(vm.temps()[0]==34&&vm.temps()[1]==0,"PhoneBook always publishes offered positive NID into Temp0");
        else if(odh::dialog_selector_type(static_cast<std::uint8_t>(type)))require(vm.temps()[1]==-25536,"numeric source signedshort casts wrap");
        if(type==3)require(f.world.find(2)->name_override==std::string(32,'x'),"text entry writes truncated actual object name");
        require(!f.dialogs.respond(ticket,response,diagnostic),"duplicate response rejected");
    }
    {
        Fixture f;Bytes b{};b[5]=1;b[7]=1;odh::Vm vm{program(38,b),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        vm.start(odh::routine_key(4096),f.context());require(vm.run(1)==odh::VmStatus::returned_true&&f.shown==1&&!f.dialogs.pending(),"Continue displays but creates no blocking state");
    }
    {
        Fixture f;Bytes b{};b[5]=5;b[7]=16|32;f.choices={{"max",2147483647}};
        odh::Vm vm{program(39,b),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        vm.start(odh::routine_key(4096),f.context());require(vm.run(1)==odh::VmStatus::waiting,"XL wait");
        std::string error;const auto t=f.dialogs.pending()->ticket;
        require(!f.dialogs.respond(t,{0,"17"},error)&&vm.temp_xl()[1]==0,"no invented catalog option");
        require(f.dialogs.respond(t,{0," +2147483647 "},error)&&vm.temp_xl()[1]==2147483647,"XL response retains int32 and selected index");
    }
    {
        Fixture f;Bytes b{};b[5]=9;odh::Vm vm{program(36,b),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        vm.start(odh::routine_key(4096),f.context());require(vm.run(1)==odh::VmStatus::waiting,"pet cancel wait");std::string e;
        require(f.dialogs.respond(f.dialogs.pending()->ticket,{0,"999"},e)&&vm.status()==odh::VmStatus::returned_false,"pet cancellation needs no invented item");
    }
    {
        Fixture f;Bytes b{};odh::Vm vm{program(36,b),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        vm.start(odh::routine_key(4096),f.context());require(vm.run(1)==odh::VmStatus::waiting,"restore wait");
        const auto state=*f.dialogs.pending();const auto snap=vm.snapshot();f.dialogs.cancel_for(&vm);
        odh::Vm resumed{program(36,b),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&resumed;
        require(resumed.restore_snapshot(snap),"restore actualVM");std::string e;
        require(f.dialogs.restore(state,resumed,e),"fresh controller binds exactwait");
        require(f.dialogs.respond(state.ticket,{0,{}},e)&&resumed.status()==odh::VmStatus::returned_true,"fresh pending resumes once");
        auto forged=state;forged.thread_owner.generation++;
        require(!f.dialogs.restore(forged,resumed,e),"replacement thread generation rejected");
        auto raw=odh::encode_dialog_state(state);raw.push_back(0);bool rejected=false;
        try{(void)odh::decode_dialog_state(raw);}catch(const odh::FormatError&){rejected=true;}require(rejected,"trailing state bytes rejected");
    }
    {
        Fixture f;Bytes b{};odh::Vm vm{program(36,b),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        vm.start(odh::routine_key(4096),f.context());require(vm.run(1)==odh::VmStatus::waiting,"global firstwait");
        odh::Vm other{program(39,b),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};
        other.start(odh::routine_key(4096),f.context());require(other.run(1)==odh::VmStatus::next_tick&&f.shown==1,"global blocking slot yields contenders without another dialog");
    }
    {
        Fixture f;Bytes b{};f.frontend=false;odh::Vm vm{program(36,b),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        vm.start(odh::routine_key(4096),f.context());require(vm.run(1)==odh::VmStatus::fault&&!f.dialogs.pending(),"missing presentation is explicit unsupported");
        auto primitive=odh::dialog_primitive(f.dialogs);odh::VmFrame frame{odh::routine_key(4096),0,f.context()};
        require(primitive(frame,{36,254,255,{}},{},{}).disposition==odh::PrimitiveDisposition::unsupported,"fabricated dispatch refused");
    }
    {
        Fixture f;f.choices={{"negative",-1}};Bytes b{};b[5]=5;
        odh::Vm vm{program(36,b),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        vm.start(odh::routine_key(4096),f.context());require(vm.run(1)==odh::VmStatus::waiting,"numeric malformed input wait");std::string e;
        require(f.dialogs.respond(f.dialogs.pending()->ticket,{0,"+-1"},e)&&vm.status()==odh::VmStatus::returned_false&&vm.temps()[0]==0,"doubledsign is parsefalse without register effects");
    }
    {
        Fixture f;Bytes b{};odh::Vm vm{program(36,b),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        auto c=f.context();c.caller=2;c.caller_generation=f.world.find(2)->generation;
        vm.start(odh::routine_key(4096),c);require(vm.run(1)==odh::VmStatus::waiting,"Caller differs from actualThread");
        const auto state=*f.dialogs.pending();require(state.thread_owner.id==1&&state.source.context.caller==2,"thread identity never inferred from Caller");
        f.dialogs.cancel_for(&vm);auto foreign=state;foreign.thread_owner={2,f.world.find(2)->generation};std::string e;
        require(!f.dialogs.restore(foreign,vm,e)&&f.dialogs.restore(state,vm,e),"sameframe cannot bind another live Thread owner");
        auto malformed=state;malformed.input=std::string(1,static_cast<char>(0xff));
        require(!odh::dialog_state_valid(malformed),"wire input cannot bypass UTF8 draft admission");
        require(f.dialogs.update_draft(state.ticket,"typed",0)&&f.dialogs.pending()->input=="typed","draft belongs to resumable controller state");
    }
    {
        Fixture f;Bytes b{};b[7]=1;odh::Vm vm{program(36,b),f.world.vm_memory()};
        const auto c=f.context();const auto result=vm.evaluate_check_at_safe_point(odh::routine_key(4096),c,{},4,odh::dialog_primitive(f.dialogs));
        require(result.status==odh::VmStatus::returned_true&&f.shown==1&&!f.dialogs.pending(),"Continue check displays synchronously without claiming a persistent scratchThread owner");
        b[7]=0;odh::Vm blocked{program(36,b),f.world.vm_memory()};
        const auto failed=blocked.evaluate_check_at_safe_point(odh::routine_key(4096),c,{},4,odh::dialog_primitive(f.dialogs));
        require(failed.status!=odh::VmStatus::returned_true&&f.shown==1,"blocking scratchcheck has no fabricated durable dialog");
    }
    for(bool prepare:{false,true}){
        Fixture f;f.replace_prepare=prepare;f.replace_show=!prepare;Bytes b{};
        odh::Vm vm{program(36,b),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        const auto generation=f.world.find(1)->generation;
        vm.start(odh::routine_key(4096),f.context());
        require(vm.run(1)==odh::VmStatus::fault&&!f.dialogs.pending()&&f.world.find(1)->generation!=generation,"callback owner replacement preserves real effects but cannot publish stale wait");
        require(f.shown==(prepare?0U:1U),"resource mutation rejects before show; show mutation rejects after its real effect");
    }
    require(odh::expand_dialog_text("$$Local:1 $Temp:0 ",[&](auto token,const auto& indexes)->std::optional<std::string>{
        if(token=="Local:"&&indexes[0]==1)return "99";
        if(token=="Temp:"&&indexes[0]==0)return "7";
        return {};
    })=="$99 7 ","portable formatter uses explicit source variables");
    require(odh::expand_dialog_text("$2000. $! $Future:0007 $",{})=="$2000. $! $Future:0007 $",
        "native literal dollars remain exact, including currency and trailing dollars");
    require(odh::expand_dialog_text("$$Temp:0 / $$Param:1 / $$Me / $$Local:0",[](auto token,const auto&)->std::optional<std::string>{
        if(token=="Temp:")return "7";if(token=="Param:")return "9";
        if(token=="Me")return "name";if(token=="Local:")return "155";return {};
    })=="$7 / $9 / $name / $155","doubled dollars compose with every known substitution prefix");
    require(odh::expand_dialog_text("$Me\r\n\r\n$2000.",[](auto,const auto&)->std::optional<std::string>{return "name";})=="name\r\n\r\n$2000.",
        "plain original paragraph breaks and literal currency coexist with substitution");
    {bool rejected=false;try{(void)odh::expand_dialog_text("$Temp:0",{});}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"known commands still require their actual substitution backend");}
    for(const auto* token:{"Job:","JobDesc:","JobOffer:","TimeLocal:"}){
        const auto source=std::string("$")+token+"4:5:0007";
        require(odh::expand_dialog_text(source,[&](auto actual,const auto& indexes)->std::optional<std::string>{
            require(actual==token&&indexes==std::vector<std::uint16_t>{4,5},"native career selector extent");
            return "resolved";
        })=="resolved:0007","native career parser preserves unconsumed suffix bytes");
    }
    for(const unsigned flags:{0U,16U,32U,48U})for(const bool cancel:{false,true}){
        Fixture f;f.choices={{"Actual contact",34},{"Boundary contact",32767},{"Malformed out-of-range offer",40000}};
        Bytes bytes{};bytes[5]=10;bytes[7]=flags;
        odh::Vm vm{program(36,bytes),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        vm.start(odh::routine_key(4096),f.context());require(vm.run(1)==odh::VmStatus::waiting,"phonebook genuinely blocks its actual source thread");
        for(std::size_t i=0;i<vm.temps().size();++i)vm.temps()[i]=static_cast<std::int16_t>(101+i);
        vm.temp_xl()[0]=123456;vm.temp_xl()[1]=-654321;
        const auto before=vm.snapshot();const auto encoded=odh::encode_dialog_state(*f.dialogs.pending());
        const auto pending=odh::decode_dialog_state(encoded);
        require(pending.prompt.choices.size()==3&&pending.prompt.choices[0].value==34,"offered contacts persist without invented identity");
        std::string why;const auto ticket=f.dialogs.pending()->ticket;
        for(const auto& response:std::vector<odh::DialogResponse>{{1,"35"},{0,"0"},{0,""},{0,"+-1"},{0,"-32769"},{1,"32768"},{1,"40000"},{1,"-1"}}){
            require(!f.dialogs.respond(ticket,response,why)&&!why.empty()&&vm.status()==odh::VmStatus::waiting,
                "invalid/unoffered PhoneBook results refuse while retaining the original wait");
            const auto after=vm.snapshot();require(after.temps==before.temps&&after.temp_xl==before.temp_xl&&
                odh::encode_dialog_state(*f.dialogs.pending())==encoded,"refused PhoneBook results preserve all registers and complete pending state");
        }
        require(!f.dialogs.respond(ticket+1,{0,"-1"},why)&&vm.status()==odh::VmStatus::waiting,"PhoneBook cancel cannot bypass ticket authority");
        require(f.dialogs.respond(ticket,{static_cast<std::uint8_t>(cancel?0:1),cancel?"-1":"34"},why)&&
            vm.status()==(cancel?odh::VmStatus::returned_false:odh::VmStatus::returned_true),
            "native PhoneBook negative returns FALSE and offered positive returns TRUE");
        auto expected=before.temps;if(!cancel)expected[0]=34;
        require(vm.temps()==expected&&vm.temp_xl()==before.temp_xl,
            "all four flags preserve every Temp/XL on cancel and publish only positive Temp0 independent of NumericEntry flags");
        require(!f.dialogs.pending()&&!f.dialogs.respond(ticket,{0,"-1"},why),"PhoneBook response consumes its pending instruction exactly once");
    }
    {
        Fixture f;Bytes bytes{};bytes[5]=10;odh::Vm vm{program(36,bytes),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        vm.start(odh::routine_key(4096),f.context());require(vm.run(1)==odh::VmStatus::waiting,"known empty phonebook still blocks for a real decision");
        vm.temps()[0]=77;vm.temp_xl()[0]=123456;std::string why;
        require(f.dialogs.respond(f.dialogs.pending()->ticket,{0,"-1"},why)&&vm.status()==odh::VmStatus::returned_false&&
            vm.temps()[0]==77&&vm.temp_xl()[0]==123456,"empty PhoneBook cancellation returns FALSE without inventing a contact or changing registers");
    }
    {
        Fixture f;Bytes bytes{};bytes[5]=5;odh::Vm vm{program(36,bytes),f.world.vm_memory(),{},odh::dialog_primitive(f.dialogs)};f.owner=&vm;
        vm.start(odh::routine_key(4096),f.context());require(vm.run(1)==odh::VmStatus::waiting,"ordinary destination selector genuine wait");
        std::string why;require(!f.dialogs.respond(f.dialogs.pending()->ticket,{0,"-1"},why)&&vm.status()==odh::VmStatus::waiting,"phonebook sentinel never bypasses other selector catalogs");
    }
    std::cout<<checks<<" dialog checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"dialog: "<<e.what()<<'\n';return 1;}}
