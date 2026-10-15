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
    odh::SoundThreadCollection collection(world,[&](const auto& request,const auto& binding){
        return odh_host::make_xa_hit_sound(hit,hot,request,binding,transport,catalog,{}, {}, {},
            odh::HitJumpOperand::Literal32,odh::HitArguments{{12,0,0}},{});
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
    std::cout<<checks<<" BHAV->XA factory->owned HIT notes->shared PCM lifecycle checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
