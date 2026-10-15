#include "odh/mixer.hpp"
#include "odh/hit_thread.hpp"
#include "linux_audio.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(){try{
    const char* driver=std::getenv("SDL_AUDIODRIVER");
    if(!driver || std::string(driver)!="dummy")throw std::runtime_error("mixer audio test requires dummy");
    odh::PcmMixer mixer(8000,{1024,4,32,128});
    odh::XaAudio mono{8000,1,std::vector<std::int16_t>(56,1000),0};
    odh::XaAudio stereo{16000,2,std::vector<std::int16_t>(224),0};
    for(std::size_t n=0;n<stereo.samples.size();n+=2){stereo.samples[n]=2000;stereo.samples[n+1]=3000;}
    const auto background=mixer.add(std::move(stereo));
    odh::Hot hot;hot.patches[100]={"fixture","fixture.xa",false,false};
    odh::HitFile hit;hit.major_version=1;hit.bytes.resize(16);hit.bytes.insert(hit.bytes.end(),{5,50,100,0,0,0,2,5,11,12});
    odh::PcmNoteHandle handle=0;unsigned starts=0;
    odh::HitNoteBackend backend;
    backend.start=[&](auto)->std::optional<std::uint64_t>{if(starts++)throw std::runtime_error("note resubmitted");handle=mixer.add(std::move(mono));return handle;};
    backend.status=[&](auto id){const auto s=mixer.state(id);return s==odh::PcmNoteState::active || s==odh::PcmNoteState::rendered?odh::HitNoteStatus::active:odh::HitNoteStatus::complete;};
    std::array<std::int32_t,29> vars{};odh::HitThread thread(hit,hot,16,vars,{},backend);
    thread.tick();
    odh_host::LinuxAudio output({8000,2},{64}); // Only16frames accepted, block has32.
    std::array<std::int16_t,64> block{};std::size_t frames=0,cursor=0;
    mixer.render(block.data(),32);frames=32;
    if(block[0]!=3000 || block[1]!=4000 || mixer.state(handle)!=odh::PcmNoteState::active)
        throw std::runtime_error("mixed-rate stereo profile mismatch");
    const auto first=output.queue(block.data(),frames);mixer.commit(first);cursor=first;
    if(first!=16 || mixer.usage().consumed_frame || thread.pc()!=24)
        throw std::runtime_error("partial prefix advanced WaitSamp prematurely");
    output.start();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(thread.tick()!=odh::HitThreadTick::complete){
        if(cursor==frames && mixer.usage().active){mixer.render(block.data(),32);frames=32;cursor=0;}
        if(cursor<frames){const auto accepted=output.queue(block.data()+cursor*2,frames-cursor);mixer.commit(accepted);cursor+=accepted;}
        mixer.acknowledge(mixer.usage().submitted_frame-output.queued_frames());
        if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("mixed audio queue timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(starts!=1 || mixer.state(background)!=odh::PcmNoteState::complete || mixer.usage().pcm_bytes || output.queued_frames())
        throw std::runtime_error("mixed completion/PCM release mismatch");
    mixer.release(handle);mixer.release(background);
    std::cout<<"Mixed-rate PCM->partial SDL queue->ack->HIT WaitSamp/End passed (dummy)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
