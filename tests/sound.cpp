#include "odh/sound.hpp"
#include <iostream>
#include <sstream>

namespace {
int checks=0;
void require(bool value,const char* why) {++checks;if(!value)throw std::runtime_error(why);}
odh::SoundEvent parse(const std::string& bytes,std::size_t limit=65536) {
    std::istringstream input(bytes);odh::View view(input,bytes.size());
    return odh::read_fwav(view,limit);
}
void rejected(const std::string& bytes,std::size_t limit=65536) {
    bool failed=false;try {parse(bytes,limit);}catch(const odh::FormatError&) {failed=true;}
    require(failed,"unterminated/over-limit FWAV rejected");
}
odh::Instruction instruction(std::uint8_t flags=0) {
    return {23,254,255,{0x34,0x12,0x80,0xBB,flags,219,0xA5,0x5A}};
}
odh::VmFrame frame() {return {odh::routine_key(4096),0,{1,3,2},{},{}};}
odh::PrimitiveOutcome invoke(const odh::VmPrimitive& primitive,const odh::Instruction& i=instruction()) {
    return primitive(frame(),i,{}, {nullptr,0});
}
}
int main() {try {
    const auto empty=parse(std::string(1,'\0'),0);
    require(empty.name.empty() && empty.remaining_bytes==0,"empty bounded event");
    const std::string raw("a\xFF\x80\0tail\0",9);
    auto event=parse(raw,3);
    require(event.name==raw.substr(0,3) && event.remaining_bytes==5,"raw bytes and tail count retained");
    {std::istringstream input("prefix"+raw+"suffix");odh::View all(input,21);
     auto payload=all.subview(6,raw.size());event=odh::read_fwav(payload,3);
     require(event.name==raw.substr(0,3) && event.remaining_bytes==5 && payload.u8(4)=='t',"bounded view preserves unread tail");}
    for(std::size_t n=0;n<4;++n) rejected(raw.substr(0,n));
    rejected(raw,2);rejected("a",0);
    const std::string maximum(65536,'x');
    require(parse(maximum+std::string(1,'\0')).name==maximum,"default maximum accepted with terminator");
    rejected(maximum+std::string("x\0",2));

    odh::World world;world.create(1,odh::EntityKind::avatar);world.create(2,odh::EntityKind::object);
    int resolves=0,submissions=0,observations=0;
    odh::SoundRequest last{};std::string status;
    odh::SoundResolver resolver=[&](const odh::VmFrame& f,std::uint16_t id) -> std::optional<odh::SoundEvent> {
        ++resolves;
        require(f.context.caller==1 && f.context.callee==3 && f.context.stack_object==2 && id==0x1234,
                "resolver receives original code-owner frame and event ID");
        return odh::SoundEvent{"test_event",7};
    };
    const auto observed=[&](const odh::SoundRequest& request,const std::string& s) {
        ++observations;last=request;status=s;
    };
    auto primitive=odh::sound_primitive(world,resolver,[&](const odh::SoundRequest& request) {
        ++submissions;last=request;return true;
    },observed);
    for(unsigned flags=0;flags<64;++flags) {
        require(invoke(primitive,instruction(static_cast<std::uint8_t>(flags))).disposition==odh::PrimitiveDisposition::goto_true,"sound submission branches true");
        require(status=="submitted" && observations==submissions && submissions==resolves,"each submission observed once");
        require(last.source==((flags&2)?2:1) && last.pan==!(flags&8) && last.zoom==!(flags&4) && last.loop==bool(flags&1),
                "source, pan, zoom and loop flags");
        require(last.event_id==0x1234 && last.sample_rate==48000 && last.volume==219 && last.flags==flags && last.name=="test_event",
                "raw metadata including AutoVary and SimSpeedAffects preserved; tail ignored");
    }
    auto piano=odh::sound_primitive(world,[](const auto&,auto) {return std::optional<odh::SoundEvent>{{"piano_play",0}};}, {},observed);
    require(invoke(piano).disposition==odh::PrimitiveDisposition::goto_true && last.loop && status=="skipped_no_backend","piano forces looping without backend");
    auto declined=odh::sound_primitive(world,resolver,[](const auto&) {return false;},observed);
    require(invoke(declined).disposition==odh::PrimitiveDisposition::goto_true && status=="backend_rejected","backend rejection still branches true");
    auto missing=odh::sound_primitive(world,[](const auto&,auto) {return std::optional<odh::SoundEvent>{};},
        [&](const auto&) {throw std::runtime_error("missing resource submitted");return true;},observed);
    require(invoke(missing).disposition==odh::PrimitiveDisposition::goto_true && status=="missing_resource" && last.name.empty(),"missing resource observed without submission");
    auto no_resolver=odh::sound_primitive(world,{}, {},observed);
    require(invoke(no_resolver).disposition==odh::PrimitiveDisposition::goto_true && status=="missing_resource","absent resolver explicitly missing");
    world.remove(2);const auto prior=resolves;
    require(invoke(primitive,instruction(2)).disposition==odh::PrimitiveDisposition::goto_true && status=="missing_owner" && last.name.empty() && resolves==prior,
            "missing selected stack owner skips resolver/backend and is observed");
    require(invoke(primitive).disposition==odh::PrimitiveDisposition::goto_true && status=="submitted","existing caller works when stack owner absent");
    world.remove(1);
    require(invoke(primitive).disposition==odh::PrimitiveDisposition::goto_true && status=="missing_owner","missing caller owner branches true");
    const auto before=observations;
    for(const auto flags : {64,128,255})
        require(invoke(primitive,instruction(flags)).disposition==odh::PrimitiveDisposition::unsupported,"unknown high flag bits unsupported");
    auto other=instruction();other.opcode=22;
    require(invoke(primitive,other).disposition==odh::PrimitiveDisposition::unsupported && observations==before,"other opcode and unsupported flags do not submit");

    // A second instruction modifies a register, proving sound failures and absent
    // backends cannot suspend or prevent the VM's following instruction.
    world.create(1,odh::EntityKind::avatar);
    for(int mode=0;mode<5;++mode) {
        const auto key=odh::routine_key(4096);
        auto sound=instruction(mode==4?2:0);sound.on_true=1;
        odh::RoutineSet routines{{key,{0x8002,{sound,{2,254,255,{0,0,42,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
        odh::SoundResolver selected=mode==3 ? odh::SoundResolver{} : resolver;
        odh::SoundSink backend=mode==0 ? odh::SoundSink{[](const auto&) {return true;}} :
            mode==1 ? odh::SoundSink{[](const auto&) {return false;}} : odh::SoundSink{};
        odh::Vm vm(routines,{}, {},odh::sound_primitive(world,selected,backend,observed));
        vm.start(key,{1,3,2});
        require(vm.run(1)==odh::VmStatus::budget_exhausted && !vm.pending() && vm.stack().back().pc==1,"PlaySound advances immediately without pending service");
        require(vm.run(1)==odh::VmStatus::returned_true && vm.temps()[0]==42 && vm.steps()==2,"next instruction executes after all normal sound outcomes");
    }
    std::cout<<checks<<" sound checks passed (synthetic fixtures; no audio playback)\n";return 0;
}catch(const std::exception& e) {std::cerr<<"Sound test failed: "<<e.what()<<'\n';return 1;}}
