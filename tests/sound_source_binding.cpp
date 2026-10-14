#include "odh/sound.hpp"
#include "odh/hit_thread.hpp"
#include <iostream>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F> void rejects(F action){bool failed=false;try{action();}catch(const odh::FormatError&){failed=true;}require(failed,"expected missing source failure");}
}
int main(){try{
    odh::World world({},odh::AttributePolicy::ts1_bounded);
    require(world.create(1,odh::EntityKind::avatar,{0,66,0,0}),"avatar created");
    require(world.set_person_data(1,7,300),"initial neatness");
    odh::SoundRequest request{355,0,0,0,1,"fixture",false,false,false,{}};
    request.source_variables=odh::sound_source_variables(*world.find(1));
    auto binding=odh::SoundSourceBinding::bind(world,request);
    require(binding && binding->valid() && binding->read(1,20)==30,"bind generation and exact field");
    require(binding->read(1,0)==0,"valid zero distinct from missing");
    for(const auto source:{-65535,65537,2,0})require(!binding->read(source,20),"source cannot wrap or alias");
    for(const auto field:{-1,29,256,2147483647})require(!binding->read(1,field),"field bounds explicit");
    auto no_snapshot=request;no_snapshot.source_variables.reset();
    require(!odh::SoundSourceBinding::bind(world,no_snapshot),"submission identity required");
    auto wrong=request;++wrong.source_variables->generation;
    require(!odh::SoundSourceBinding::bind(world,wrong),"wrong generation rejected");
    wrong=request;wrong.source_variables->profile=odh::SoundVariableProfile::simitone_object;
    require(!odh::SoundSourceBinding::bind(world,wrong),"wrong source profile rejected");
    require(!odh::SoundSourceBinding::bind(world,request,28),"required avatar fields exceed budget");
    auto copy=*binding;
    const auto snapshot=binding->refresh();
    require(world.set_person_data(1,7,570),"live bank changed");
    require(copy.read(1,20)==57 && snapshot->fields[20]==30 && request.source_variables->fields[20]==30,
            "copies refresh current bank without changing owned snapshots");
    odh::HitFile hit;hit.major_version=1;hit.bytes.resize(16);
    // NoteOn5, WaitSamp, then read source reg1/field reg2, End.
    hit.bytes.insert(hit.bytes.end(),{5,50,100,0,0,0,2,5,11,72,6,1,2,12});
    odh::Hot hot;hot.patches[100]={"fixture","fixture.xa",false,false};
    bool active=true;unsigned starts=0;
    odh::HitNoteBackend backend;
    backend.start=[&](auto)->std::optional<std::uint64_t>{++starts;return 1;};
    backend.status=[&](auto){return active?odh::HitNoteStatus::active:odh::HitNoteStatus::complete;};
    odh::HitSourceFieldReader reader=[copy](auto source,auto field){return copy.read(source,field);};
    std::array<std::int32_t,29> fields{};
    odh::HitThread live(hit,hot,16,fields,{},backend,{},nullptr,odh::HitJumpOperand::FreeSoHeuristic,reader,odh::HitArguments{{1,20,0}});
    live.tick();
    require(world.set_person_data(1,7,810),"bank changed during WaitSamp");
    active=false;live.tick();live.tick();
    require(live.state().registers[6]==81 && starts==1,"persistent HIT reads live bank after wait");
    require(live.tick()==odh::HitThreadTick::complete,"live source thread completes");
    active=true;
    odh::HitThread stale(hit,hot,16,fields,{},backend,{},nullptr,odh::HitJumpOperand::FreeSoHeuristic,reader,odh::HitArguments{{1,20,0}});
    stale.tick();active=false;stale.tick();
    require(world.remove(1) && !copy.valid() && !copy.refresh(),"removed owner invalidates copies");
    require(world.create(1,odh::EntityKind::avatar,{0,66,0,0}) && world.set_person_data(1,7,990),"ID reused");
    require(!copy.valid() && !copy.read(1,20) && !odh::SoundSourceBinding::bind(world,request),"new incarnation not adopted");
    rejects([&]{stale.tick();});require(stale.faulted(),"stale HIT source is fault, not completion");
    request.source_variables=odh::sound_source_variables(*world.find(1));
    binding=odh::SoundSourceBinding::bind(world,request);
    require(binding && binding->read(1,20)==99,"new submission binds new generation");
    require(world.replace_person_data(1,{}) && binding->valid() && !binding->refresh(),"identity may survive missing required banks");
    require(world.create(2,odh::EntityKind::object,{1,0,0,0}),"object created");
    request.source=2;request.source_variables=odh::sound_source_variables(*world.find(2));
    binding=odh::SoundSourceBinding::bind(world,request,3);
    const odh::VmFrame frame{odh::routine_key(1),0,{2,2,2},{},{}};
    require(binding && world.write(frame,0,1,23) && binding->read(2,2)==23,"live bounded object attribute growth");
    require(world.write(frame,0,2,42) && binding->valid() && !binding->refresh(),"over-budget bank not truncated");
    require(world.create(3,odh::EntityKind::object),"submission race owner created");
    bool sink_seen=false;std::string observed;
    auto primitive=odh::sound_primitive(world,
        [&](const auto&,auto)->std::optional<odh::SoundEvent>{
            require(world.remove(3) && world.create(3,odh::EntityKind::object),"resolver reuses owner ID");
            return odh::SoundEvent{"fixture",0};
        },[&](const odh::SoundRequest& submitted){
            sink_seen=true;
            require(submitted.source_variables.has_value(),"primitive supplies submission identity");
            return odh::SoundSourceBinding::bind(world,submitted).has_value();
        },[&](const auto&,const std::string& status){observed=status;});
    const odh::VmFrame submit{odh::routine_key(1),0,{3,3,3},{},{}};
    const odh::Instruction play{23,254,255,{0,0,0,0,0,0,0,0}};
    const auto outcome=primitive(submit,play,{}, {nullptr,0});
    require(sink_seen && observed=="backend_rejected" && outcome.disposition==odh::PrimitiveDisposition::goto_true,
            "stale submission rejected without changing nonblocking PlaySound behavior");
    std::cout<<checks<<" live sound source binding checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
