#include "linux_pcm_sink.hpp"
#include "odh/sound_threads.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
void require(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
// Test-only observer. Copies accepted prefixes to inspect the next generated
// samples after cancellation; production sink retains no PCM trace.
struct ObservedSink final : odh::PcmSink {
    odh_host::LinuxPcmSink output;
    explicit ObservedSink(odh_host::LinuxAudioRuntime& runtime):output(runtime,8000,{64}){} // Only16 stereo frames.
    std::vector<std::int16_t> accepted;
    unsigned clears=0;
    odh::PcmSinkFormat format() const override{return output.format();}
    std::size_t queued_frames() const override{return output.queued_frames();}
    std::size_t queue(const std::int16_t* pcm,std::size_t frames) override {
        const auto count=output.queue(pcm,frames);
        accepted.insert(accepted.end(),pcm,pcm+count*2);return count;
    }
    void pause() override{output.pause();}
    void start() override{output.start();}
    void clear() override{++clears;output.clear();}
};
struct NoteOwner final : odh::SoundThreadLifetime {
    odh::PcmTransport& transport;
    odh::PcmNoteHandle handle=0;
    unsigned& stops;
    bool stopped=false;
    NoteOwner(odh::PcmTransport& t,unsigned& s):transport(t),stops(s){}
    void stop() noexcept override {
        if(stopped)return;
        stopped=true;++stops;
        try{if(handle)transport.release(handle);}catch(...){/* Transport latches sink faults. */}
    }
    ~NoteOwner() override{stop();}
};
}
int main(){try{
    const char* driver=std::getenv("SDL_AUDIODRIVER");
    require(driver && std::string(driver)=="dummy","transport test requires dummy output");
    odh_host::LinuxAudioRuntime runtime(odh_host::LinuxAudioRuntime::Ownership::application);
    ObservedSink sink(runtime);
    {
        odh::PcmTransport transport(sink,{4096,4,32,128});
        odh::XaAudio stereo{16000,2,std::vector<std::int16_t>(800),0};
        for(std::size_t n=0;n<stereo.samples.size();n+=2){stereo.samples[n]=2000;stereo.samples[n+1]=3000;}
        const auto background=transport.add(std::move(stereo));
        odh::Hot hot;hot.patches[100]={"fixture","fixture.xa",false,false};
        odh::HitFile hit;hit.major_version=1;hit.bytes.resize(16);
        hit.bytes.insert(hit.bytes.end(),{5,50,100,0,0,0,2,5,11,12});
        odh::World world;world.create(1,odh::EntityKind::avatar,{0,66,0,0});
        odh::SoundRequest request{355,0,0,0,1,"fixture",false,false,false,{}};
        request.source_variables=odh::sound_source_variables(*world.find(1));
        unsigned stops=0,starts=0;
        odh::PcmNoteHandle cancelled_handle=0;
        odh::SoundThreadCollection collection(world,[&](const auto&,const auto& binding){
            auto owner=std::make_unique<NoteOwner>(transport,stops);
            auto* note=owner.get();
            odh::HitNoteBackend backend;
            backend.start=[&,note](auto)->std::optional<std::uint64_t>{
                require(starts++==0,"duplicate foreground note");
                note->handle=transport.add({8000,1,std::vector<std::int16_t>(400,1000),0});
                cancelled_handle=note->handle;return note->handle;
            };
            backend.status=[&](auto handle){
                const auto state=transport.state(handle);
                return state==odh::PcmNoteState::active || state==odh::PcmNoteState::rendered?
                    odh::HitNoteStatus::active:odh::HitNoteStatus::complete;
            };
            std::array<std::int32_t,29> vars{};
            auto thread=std::make_unique<odh::HitThread>(hit,hot,16,vars,odh::HitChooser{},backend,
                odh::HitThreadLimits{},nullptr,odh::HitJumpOperand::FreeSoHeuristic,
                [binding](auto source,auto field){return binding.read(source,field);});
            return std::make_unique<odh::SoundThreadInstance>(std::move(thread),std::move(owner));
        });
        require(collection.submit(request) && collection.tick().active==1,"foreground not active");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        auto tick=[&]{transport.tick();require(!transport.failed(),"transport fault");
            require(std::chrono::steady_clock::now()<deadline,"dummy transport timeout");};
        tick();
        require(!sink.accepted.empty() && sink.accepted[0]==3000 && sink.accepted[1]==4000,
            "initial two-note stereo mix incorrect");
        require(transport.usage().submitted_frame<transport.usage().rendered_frame,
            "fixture did not retain partial prefix");
        while(transport.usage().consumed_frame<16){std::this_thread::sleep_for(std::chrono::milliseconds(1));tick();}
        world.remove(1);
        const auto report=collection.tick();
        require(report.owner_invalid==1 && !report.active && !report.completed && !report.faulted && stops==1,
            "invalid owner did not cancel exactly once");
        const auto usage=transport.usage();
        require(usage.handles==1 && usage.rendered_frame==usage.consumed_frame &&
            usage.submitted_frame==usage.consumed_frame && sink.queued_frames()==0 && sink.clears==1,
            "cancellation did not discard retained and queued prefixes");
        bool stale_rejected=false;
        try{transport.state(cancelled_handle);}catch(const odh::FormatError&){stale_rejected=true;}
        require(stale_rejected,"cancelled handle still valid");
        sink.accepted.clear();tick();
        require(sink.accepted.size()>=2 && sink.accepted[0]==2000 && sink.accepted[1]==3000,
            "survivor replay included stale foreground PCM");
        while(transport.state(background)!=odh::PcmNoteState::complete ||
              transport.usage().submitted_frame!=transport.usage().rendered_frame || sink.queued_frames()){
            std::this_thread::sleep_for(std::chrono::milliseconds(1));tick();
        }
        transport.release(background);
        require(!transport.usage().handles && !transport.usage().pcm_bytes && starts==1 && stops==1,
            "survivor did not finish on shared device or resources leaked logically");
    }
    require(sink.queued_frames()==0,"transport destruction left queued PCM");
    std::cout<<"PCM transport->partial SDL dummy queue->World owner cancellation->survivor completion passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
