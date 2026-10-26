#include "odh/sound.hpp"
#include "odh/playback.hpp"
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

    {
        odh::World sources;
        require(sources.create(1,odh::EntityKind::avatar,{0,66,0,0}) &&
                sources.create(2,odh::EntityKind::object,{3,0,0,0}),"create independent source banks");
        const std::vector<std::pair<std::int16_t,std::int16_t>> values{
            {2,119},{3,-11},{4,0},{5,32767},{6,-9},{7,309},
            {10,1200},{12,-32768},{15,999},{17,1009},{58,21},{65,1}};
        for(const auto& value:values) require(sources.set_person_data(1,value.first,value.second),"set selected source PersonData");
        const auto original=sources.find(1)->person_data;
        const auto vars=odh::sound_source_variables(*sources.find(1));
        std::vector<std::int32_t> expected(29);
        expected[0]=1;expected[2]=100;expected[5]=99;expected[7]=-3276;expected[11]=100;
        expected[14]=21;expected[15]=11;expected[16]=-1;expected[18]=3276;expected[20]=30;
        require(vars && vars->fields==expected && vars->profile==odh::SoundVariableProfile::simitone_avatar &&
                vars->generation==sources.find(1)->generation,"avatar fields map exactly; skills capped above only; personalities/negative signed division preserved");
        require(sources.find(1)->person_data==original,"source extraction never changes World");
        require(!odh::sound_source_variables(*sources.find(1),28) &&
                odh::sound_source_variables(*sources.find(1),29)->fields==expected,"avatar exact field budget");
        for(const auto age: {std::int16_t(-1),std::int16_t(0),std::int16_t(17),std::int16_t(18)}) {
            require(sources.set_person_data(1,58,age),"set child boundary");
            const auto fields=odh::sound_source_variables(*sources.find(1));
            require(fields && fields->fields[0]==((age<18&&age!=0)?2:1) && fields->fields[14]==age,
                    "age0 uses gender; age18 adult; negative age follows exact reference predicate");
        }
        require(sources.set_person_data(1,65,24),"set pet gender flags");
        require(odh::sound_source_variables(*sources.find(1))->fields[0]==24,"adult gender bitfield retained without filtering");
        for(std::int16_t index=0;index<3;++index) {
            auto f=frame();f.context.caller=2;
            require(sources.write(f,0,index,static_cast<std::int16_t>(index-2)),"set object attribute");
        }
        const auto object=odh::sound_source_variables(*sources.find(2),4);
        require(object && object->fields==std::vector<std::int32_t>({0,-2,-1,0}) &&
                object->profile==odh::SoundVariableProfile::simitone_object,"object attributes prefixed with zero; signs preserved");
        require(!odh::sound_source_variables(*sources.find(2),3),"object bank is rejected rather than truncated at budget");
        require(sources.create(3,odh::EntityKind::avatar,{0,65,0,0}) &&
                !odh::sound_source_variables(*sources.find(3)),"incomplete avatar bank has no invented source variables");
        require(sources.create(4,odh::EntityKind::object),"create empty object");
        require(odh::sound_source_variables(*sources.find(4),1)->fields==std::vector<std::int32_t>({0}) &&
                !odh::sound_source_variables(*sources.find(4),0),"empty object still needs prefix field budget");
        require(sources.create(5,odh::EntityKind::object,{255,0,0,0}) && sources.create(6,odh::EntityKind::object,{256,0,0,0}),"create boundary object banks");
        require(odh::sound_source_variables(*sources.find(5))->fields.size()==256 &&
                !odh::sound_source_variables(*sources.find(6)),"default per-request field bound is256");
        odh::SoundRequest captured{};
        const auto sound=odh::sound_primitive(sources,[](const auto&,auto){return std::optional<odh::SoundEvent>{{"source_fixture",0}};},
            [&](const odh::SoundRequest& request){captured=request;return true;});
        require(invoke(sound,instruction(2)).disposition==odh::PrimitiveDisposition::goto_true &&
                captured.source==2 && captured.source_variables->fields==object->fields,"StackObject source bank reaches backend");
        require(invoke(sound).disposition==odh::PrimitiveDisposition::goto_true &&
                captured.source_variables->fields[20]==30,"Caller avatar source bank reaches backend");
        const auto generation=captured.source_variables->generation;
        require(sources.set_person_data(1,7,990) && captured.source_variables->fields[20]==30,"submitted variables own their snapshot after live-bank mutation");
        require(invoke(sound).disposition==odh::PrimitiveDisposition::goto_true && captured.source_variables->fields[20]==99,
                "next submission refreshes changed bank");
        require(sources.remove(1) && sources.create(1,odh::EntityKind::avatar,{0,66,0,0}),"reuse caller ID");
        require(invoke(sound).disposition==odh::PrimitiveDisposition::goto_true &&
                captured.source_variables->generation!=generation,"snapshot stamps new entity incarnation");
        auto removing=odh::sound_primitive(sources,[&](const auto&,auto){sources.remove(1);return std::optional<odh::SoundEvent>{{"source_fixture",0}};},
            [&](const odh::SoundRequest& request){captured=request;return true;});
        require(invoke(removing).disposition==odh::PrimitiveDisposition::goto_true && captured.source_variables &&
                captured.source_variables->fields.size()==29,"resolver mutation cannot invalidate the captured source bank");
    }

    {
        odh::World animation_world;
        require(animation_world.create(1,odh::EntityKind::avatar,{0,66,0,0}),"create animation sound actor");
        require(animation_world.set_person_data(1,58,21)&&animation_world.set_person_data(1,65,1),"set animation avatar identity variables");
        odh::PlaybackEvent event{7,1,33,{{"sound","First_Event"},{"sound","ignored_second"}},
                                 animation_world.find(1)->generation,42};
        odh::SoundRequest captured{};std::string result;int submitted=0,observed_count=0;
        const auto observe=[&](const odh::SoundRequest& r,const std::string& status){captured=r;result=status;++observed_count;};
        const odh::SoundSink accept=[&](const odh::SoundRequest& r){captured=r;++submitted;return true;};
        odh::animation_sound_event(animation_world,event,accept,observe);
        require(result=="submitted"&&submitted==1&&observed_count==1&&captured.name=="First_Event",
                "animation submits first exact sound key and preserves event name spelling");
        require(captured.source==1&&captured.pan&&captured.zoom&&!captured.loop&&captured.event_id==0&&
                captured.sample_rate==0&&captured.flags==0&&captured.volume==0,
                "animation source and presentation flags use no invented BHAV metadata");
        require(captured.source_variables&&captured.source_variables->fields.size()==29&&
                captured.source_variables->fields[0]==1&&captured.source_variables->fields[14]==21&&
                captured.source_variables->generation==event.actor_generation,
                "animation snapshots complete avatar HIT variables and incarnation");
        const auto snapshot=captured.source_variables->fields;
        require(animation_world.set_person_data(1,58,17)&&captured.source_variables->fields==snapshot,
                "animation request owns variables after live bank changes");
        odh::animation_sound_event(animation_world,event,{},observe);
        require(result=="skipped_no_backend"&&submitted==1&&captured.source_variables->fields[0]==2,
                "silent animation dispatch refreshes variables without claiming audio playback");
        odh::animation_sound_event(animation_world,event,[](const auto&){return false;},observe);
        require(result=="backend_rejected","animation backend refusal is observed");
        const auto prior=observed_count;
        event.properties={{"Sound","wrong-case"},{"xevt","7"}};
        odh::animation_sound_event(animation_world,event,accept,observe);
        require(observed_count==prior&&submitted==1,"case-mismatched key is unqueried metadata");
        for(const auto& name:std::vector<std::string>{{},std::string(257,'x'),std::string("ok\0tail",7)}){
            event.properties={{"sound",name},{"sound","valid_second"}};
            odh::animation_sound_event(animation_world,event,accept,observe);
            require(result=="invalid_event"&&captured.name.empty()&&!captured.source_variables&&submitted==1,
                    "invalid first sound value rejects before copy and never falls through to duplicate");
        }
        event.properties={{"sound",std::string(256,'x')}};
        odh::animation_sound_event(animation_world,event,accept,observe);
        require(result=="submitted"&&captured.name.size()==256&&submitted==2,"exact animation event name byte budget accepted");
        event.properties={{"sound","valid"}};
        require(animation_world.remove(1),"remove emitting avatar");
        odh::animation_sound_event(animation_world,event,accept,observe);
        require(result=="missing_owner"&&!captured.source_variables&&submitted==2,"missing animation actor never reaches backend");
        require(animation_world.create(1,odh::EntityKind::avatar,{0,66,0,0}),"reuse animation actor ID");
        odh::animation_sound_event(animation_world,event,accept,observe);
        require(result=="stale_owner"&&!captured.source_variables&&submitted==2,"reused actor ID cannot receive an older animation sound");
        event.actor_generation=0;
        odh::animation_sound_event(animation_world,event,accept,observe);
        require(result=="stale_owner"&&!captured.source_variables,"legacy unstamped event is not assigned to a live actor");
        require(animation_world.remove(1)&&animation_world.create(1,odh::EntityKind::avatar,{0,65,0,0}),"create incomplete animation source bank");
        event.actor_generation=animation_world.find(1)->generation;
        odh::animation_sound_event(animation_world,event,accept,observe);
        require(result=="missing_source_variables"&&!captured.source_variables&&submitted==2,
                "incomplete animation source variables are explicitly diagnosed");
        require(animation_world.remove(1)&&animation_world.create(1,odh::EntityKind::object),"replace actor with nonavatar");
        event.actor_generation=animation_world.find(1)->generation;
        odh::animation_sound_event(animation_world,event,accept,observe);
        require(result=="missing_source_variables"&&!captured.source_variables&&submitted==2,
                "animation bridge cannot invent avatar variables for an object");
    }

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
