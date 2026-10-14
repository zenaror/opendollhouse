#include "odh/hit_thread.hpp"
#include "odh/xa_audio.hpp"
#include "odh/sound_threads.hpp"
#include "linux_audio.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

int main() { try {
    const char* driver = std::getenv("SDL_AUDIODRIVER");
    if (!driver || std::string(driver) != "dummy")
        throw std::runtime_error("HIT audio test requires silent dummy driver");
    std::string bytes(24+30, '\0');
    bytes.replace(0,4,std::string("XAI\0",4));
    auto word = [&](std::size_t at, std::uint32_t value, unsigned count) {
        for (unsigned i=0;i<count;++i) bytes.at(at+i)=static_cast<char>((value>>(8*i))&255);
    };
    word(4,112,4); word(8,1,2); word(10,1,2); word(12,8000,4);
    word(16,16000,4); word(20,2,2); word(22,16,2);
    for (std::size_t i=24;i<bytes.size();++i) bytes[i] = (i-24)%15 ? 0x11 : 0;
    const auto input = bytes;
    odh::HitFile hit; hit.major_version=1; hit.bytes.resize(16);
    hit.bytes.insert(hit.bytes.end(),{5,50,100,0,0,0,2,5,11,12});
    odh::Hot hot; hot.patches[100]={"fixture","fixture.xa",false,false};
    std::array<std::int32_t,29> vars{};
    std::unique_ptr<odh_host::LinuxAudio> output;
    odh::XaAudio pcm{};
    unsigned starts=0;
    odh::HitNoteBackend backend;
    backend.start=[&](std::uint32_t patch)->std::optional<std::uint64_t> {
        if(patch!=100 || starts++) throw std::runtime_error("wrong/duplicate audio start");
        std::istringstream stream(bytes);
        pcm=odh::read_xa(odh::View(stream,bytes.size()));
        output=std::make_unique<odh_host::LinuxAudio>(odh_host::AudioFormat{pcm.sample_rate,pcm.channels});
        if(output->queue(pcm.samples.data(),pcm.samples.size())!=56)
            throw std::runtime_error("incomplete decoded PCM queue");
        return 17; // Opens paused: queued-but-not-started is active.
    };
    backend.status=[&](std::uint64_t handle) {
        if(handle!=17 || !output)throw std::runtime_error("unknown audio handle");
        return output->queued_frames()?odh::HitNoteStatus::active:odh::HitNoteStatus::complete;
    };
    odh::HitThread thread(hit,hot,16,vars,{},backend);
    if(thread.tick()!=odh::HitThreadTick::yielded || thread.pc()!=24 ||
       thread.state().registers[5]!=0 || output->queued_frames()!=56)
        throw std::runtime_error("queued unstarted audio did not hold WaitSamp");
    output->start();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(thread.tick()!=odh::HitThreadTick::complete) {
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("HIT audio timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(starts!=1 || thread.pc()!=26 || !thread.bytecode_ended() || thread.faulted() ||
       thread.notes_started()!=1 || output->queued_frames() || bytes!=input || pcm.samples.size()!=56)
        throw std::runtime_error("HIT decoded-audio lifecycle mismatch");
    output.reset(); // Dummy permits one open device; first probe is complete.
    // A collection must cancel queued audio even when WaitSamp cannot finish.
    struct AudioOwner:odh::SoundThreadLifetime {
        odh_host::LinuxAudio audio{{8000,1}};
        unsigned& stops;std::size_t& queued_before;bool& clean;bool stopped=false;
        AudioOwner(unsigned& s,std::size_t& q,bool& c):stops(s),queued_before(q),clean(c){}
        void stop() noexcept override {
            if(stopped)return;
            stopped=true;++stops;
            try {queued_before=audio.queued_frames();audio.pause();audio.clear();clean=audio.queued_frames()==0;}
            catch(...){clean=false;}
        }
        ~AudioOwner() override {stop();}
    };
    odh::World world;world.create(1,odh::EntityKind::avatar,{0,66,0,0});
    odh::SoundRequest request{355,0,0,0,1,"fixture",false,false,false,{}};
    request.source_variables=odh::sound_source_variables(*world.find(1));
    unsigned stops=0;std::size_t queued_before=0;bool clean=false;
    odh::SoundThreadCollection collection(world,[&](const auto&,const auto& binding){
        auto owner=std::make_unique<AudioOwner>(stops,queued_before,clean);
        auto* device=owner.get();
        odh::HitNoteBackend sound;
        sound.start=[&,device](auto)->std::optional<std::uint64_t>{
            if(device->audio.queue(pcm.samples.data(),56)!=56)throw std::runtime_error("partial cancellation fixture");
            return 1;
        };
        sound.status=[device](auto){return device->audio.queued_frames()?odh::HitNoteStatus::active:odh::HitNoteStatus::complete;};
        odh::HitSourceFieldReader reader=[binding](auto source,auto field){return binding.read(source,field);};
        auto active_thread=std::make_unique<odh::HitThread>(hit,hot,16,vars,odh::HitChooser{},sound,
            odh::HitThreadLimits{},nullptr,odh::HitJumpOperand::FreeSoHeuristic,reader);
        return std::make_unique<odh::SoundThreadInstance>(std::move(active_thread),std::move(owner));
    });
    if(!collection.submit(request) || collection.tick().active!=1)throw std::runtime_error("cancellation fixture not active");
    world.remove(1);
    const auto cancelled=collection.tick();
    if(cancelled.owner_invalid!=1 || cancelled.completed || cancelled.faulted || cancelled.active ||
       stops!=1 || queued_before!=56 || !clean)
        throw std::runtime_error("removed owner did not cancel queued SDL audio exactly once");
    std::cout<<"Synthetic XA->HIT->SDL dummy wait/end and owner cancellation passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
