#include "xa_sound_factory.hpp"
#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Sink final:odh::PcmSink {
    std::deque<std::int16_t> samples;
    unsigned clears=0;
    odh::PcmSinkFormat format() const override{return {8000,2};}
    std::size_t queued_frames() const override{return samples.size()/2;}
    std::size_t queue(const std::int16_t* pcm,std::size_t frames) override {
        const auto accepted=std::min(frames,2-queued_frames());
        for(std::size_t n=0;n<accepted*2;++n)samples.push_back(pcm[n]);
        return accepted;
    }
    void consume(){samples.clear();}
    void pause() override{}
    void start() override{}
    void clear() override{++clears;samples.clear();}
};
void put(std::string& bytes,std::uint32_t value,unsigned count){
    for(unsigned n=0;n<count;++n)bytes.push_back(static_cast<char>((value>>(n*8))&255));
}
void xa(const std::filesystem::path& path,std::uint8_t shift){
    std::string b("XAI\0",4);put(b,112,4);put(b,1,2);put(b,1,2);put(b,8000,4);
    put(b,16000,4);put(b,2,2);put(b,16,2);
    for(int block=0;block<2;++block){b.push_back(static_cast<char>(shift));b+=std::string(14,0x11);}
    std::ofstream stream(path,std::ios::binary);stream.write(b.data(),static_cast<std::streamsize>(b.size()));
    stream.close();require(bool(stream),"fixture XA write failed");
}
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("requires private fixture directory");
    const auto root=std::filesystem::path(argv[1]);xa(root/"fixture.xa",0);xa(root/"other.xa",1);
    odh_host::XaPatchCatalog catalog({{(root/"fixture.xa").string(),odh_host::XaSourceKind::loose},
                                    {(root/"other.xa").string(),odh_host::XaSourceKind::loose}});
    odh::HitFile hit;hit.major_version=1;hit.bytes.resize(16);hit.bytes.insert(hit.bytes.end(),{2,5,11,12});
    odh::Hot hot;hot.events["fixture"]={1,7,{}};hot.events["other"]={1,8,{}};hot.events["absent"]={1,9,{}};
    odh::HotTrack track;track.subroutine_offset=16;track.hitlist_id=0;track.patch_id=100;hot.tracks[7]=track;
    track.patch_id=200;hot.tracks[8]=track;track.patch_id=300;hot.tracks[9]=track;
    hot.patches[100]={"fixture","fixture.xa",false,false};hot.patches[200]={"other","other.xa",false,false};
    hot.patches[300]={"absent","absent.xa",false,false};
    odh::World world;world.create(1,odh::EntityKind::avatar,{0,66,0,0});world.create(2,odh::EntityKind::object);
    Sink sink;odh::PcmTransport transport(sink,{1024,8,8,32});
    std::optional<odh::HitNativeControls> native_controls;
    odh::SoundThreadCollection collection(world,[&](const auto& request,const auto& binding){
        return odh_host::make_xa_hit_sound(hit,hot,request,binding,transport,catalog,{}, {}, {},
            odh::HitJumpOperand::Literal32,odh::HitArguments{{12,0,0}},{},{},native_controls);
    });
    unsigned submissions=0;
    auto primitive=odh::sound_primitive(world,[](const auto&,auto id)->std::optional<odh::SoundEvent>{
        return odh::SoundEvent{id==1?"fixture":id==2?"other":"absent",0};
    },[&](const auto& request){++submissions;return collection.submit(request);});
    auto run=[&](std::int16_t caller,std::uint16_t event){
        const odh::Instruction play{23,254,255,{static_cast<std::uint8_t>(event),0,0,0,12,0,0,0}};
        odh::Bhav bhav{0x8002,{play},{},odh::Bhav::Metadata{0,0,0,0}};
        odh::Vm vm({{odh::routine_key(4096),bhav}},world.vm_memory(),{},primitive);
        vm.start(odh::routine_key(4096),{caller,caller,caller});
        require(vm.run(1)==odh::VmStatus::returned_true,"BHAV PlaySound did not return nonblocking true");
    };
    run(1,1);run(2,2);
    require(submissions==2 && collection.size()==2 && !transport.usage().handles,
        "factory decoded eagerly or BHAV did not submit both instances");
    require(collection.tick().active==2 && transport.usage().handles==2,"HIT notes not acquired by reusable owners");
    transport.tick();
    require(sink.samples.front()==6144 && sink.samples[1]==6144 &&
        transport.usage().submitted_frame<transport.usage().rendered_frame,"initial mix/partial queue mismatch");
    sink.samples.pop_front();sink.samples.pop_front(); // Consume exactly one frame.
    world.remove(1);
    const auto cancelled=collection.tick();
    require(cancelled.owner_invalid==1 && cancelled.active==1 && !cancelled.faulted &&
        transport.usage().handles==1 && sink.samples.empty(),"owner cancellation leaked handle or old mix");
    transport.tick();
    require(sink.samples.front()==2048 && sink.samples[1]==2048,"surviving owner replay included cancelled note");
    unsigned completed=0;
    for(unsigned n=0;n<80 && (collection.size() || transport.usage().consumed_frame<transport.usage().rendered_frame);++n){
        sink.consume();transport.tick();const auto report=collection.tick();
        require(!report.faulted && !report.owner_invalid,"surviving owner faulted");completed+=report.completed;
    }
    require(completed==1 && !collection.size() && !transport.usage().handles && !transport.usage().pcm_bytes,
        "completed factory instance retained notes/PCM");
    run(2,3);require(collection.size()==1 && !transport.usage().handles,"missing asset decoded before NoteOn");
    const auto failed=collection.tick();
    require(failed.faulted==1 && !failed.active && !transport.failed() && !transport.usage().handles,
        "deferred missing asset did not fault cleanly without failing unrelated sink");
    // A decode larger than the remaining aggregate budget fails before PCM admission.
    const auto occupied=transport.add({8000,1,std::vector<std::int16_t>(500,0),0});
    run(2,2);require(collection.tick().faulted==1 && transport.usage().handles==1,
        "loader allowance failed to enforce aggregate PCM budget");
    transport.release(occupied);
    // Native volume is available to the very first bytecode read. Copy it to
    // patch50, then write18=100: that later control write must not bind patch100.
    hot.tracks[7].arg_type=2;hot.tracks[7].volume=200;hot.tracks[7].ducking_priority=10;
    hot.tracks[7].sound_pressure_level=0;
    native_controls=odh::build_native_hit_controls(hot.tracks[7],{{12,0,0}},1024,512);
    hit.bytes.resize(16);hit.bytes.insert(hit.bytes.end(),{6,50,18,5,18,100,0,0,0,2,5,11,12});
    run(2,1);
    require(collection.size()==1 && !transport.usage().handles,"native factory executed before first tick");
    require(collection.tick().active==1 && transport.usage().handles==1,"native factory did not start one note");
    transport.tick();
    require(sink.samples.front()==2048 && sink.samples[1]==2048,
        "factory lost initial native volume or retained legacy patch18 alias");
    world.remove(2);
    require(collection.tick().owner_invalid==1 && !transport.usage().handles && !transport.usage().pcm_bytes,
        "native owner cancellation retained audio resources");
    world.create(2,odh::EntityKind::object);
    // Reject an incompatible track before any note acquisition. Previously
    // built controls cannot bypass the constructor profile gate.
    odh::SoundRequest request{};request.source=2;request.name="fixture";
    request.source_variables=odh::sound_source_variables(*world.find(2));
    bool rejected=false;
    hot.tracks[7].arg_type=1;
    try { collection.submit(request); } catch(const odh::FormatError&) { rejected=true; }
    require(rejected && !collection.size() && !transport.usage().handles && !transport.usage().pcm_bytes,
        "unsupported native factory profile did not reject with clean resources");
    hot.tracks[7].arg_type=2;
    native_controls->raw_arguments[0]=13;
    rejected=false;
    try { collection.submit(request); } catch(const odh::FormatError&) { rejected=true; }
    require(rejected && !collection.size() && !transport.usage().handles && !transport.usage().pcm_bytes,
        "late native tuple rejection retained factory resources");
    // Unset native patch chooses a real XA from the list, without inventing
    // a resource for 0x80000000 or decoding during construction.
    hot.tracks[7].patch_id=0x80000000u;hot.tracks[7].hitlist_id=1;
    hot.hitlists[1]={{100},{},{}};
    native_controls=odh::build_native_hit_controls(hot.tracks[7],{{12,0,0}},1024,512);
    hit.bytes.resize(16);hit.bytes.insert(hit.bytes.end(),{2,5,11,12});
    auto binding=*odh::SoundSourceBinding::bind(world,request);
    unsigned choices=0;
    auto instance=odh_host::make_xa_hit_sound(hit,hot,request,binding,transport,catalog,
        [&](auto,const auto& ids){++choices;require(ids==std::vector<std::uint32_t>{100},"native XA list inputs");return std::size_t{0};},
        {},{},odh::HitJumpOperand::Literal32,odh::HitArguments{{12,0,0}},{},{},native_controls);
    require(choices==1 && !transport.usage().handles && !transport.usage().pcm_bytes &&
        native_controls->locals[50-16]==0 && *hot.tracks[7].patch_id==0x80000000u,
        "native XA sentinel resolution mutated metadata or decoded eagerly");
    require(instance->tick()==odh::HitThreadTick::yielded && transport.usage().handles==1,
        "native XA sentinel did not start selected asset");
    instance.reset();
    require(!transport.usage().handles && !transport.usage().pcm_bytes,"native XA sentinel cleanup retained resources");
    std::cout<<checks<<" BHAV->XA factory->owned HIT notes->shared PCM lifecycle checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
