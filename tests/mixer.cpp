#include "odh/mixer.hpp"
#include <iostream>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const odh::FormatError&){failed=true;}require(failed,"expected explicit mixer rejection");}
odh::XaAudio audio(std::vector<std::int16_t> samples,std::uint32_t rate=8000,std::uint16_t channels=1){return {rate,channels,std::move(samples),0};}
std::vector<std::int16_t> render(odh::PcmMixer& mixer,std::size_t frames){std::vector<std::int16_t> out(frames*2);mixer.render(out.data(),frames);return out;}
std::vector<std::int16_t> stereo(std::initializer_list<std::int16_t> mono){std::vector<std::int16_t> out;for(auto x:mono){out.push_back(x);out.push_back(x);}return out;}
}
int main(){try{
    using namespace odh;
    {
        PcmMixer m(8000);const auto h=m.add(audio({1000,-2000,3000}));
        require(render(m,4)==stereo({1000,-2000,3000,0}),"mono unity stereo and exact finite endpoint");
        require(m.state(h)==PcmNoteState::rendered && m.usage().pcm_bytes!=0,"rendered is not backend complete; PCM remains rewindable");
        rejects([&]{m.acknowledge(1);});rejects([&]{m.commit(5);});
        m.commit(2);m.acknowledge(2);
        require(m.state(h)==PcmNoteState::rendered,"partial acceptance/consumption does not finish note");
        m.commit(2);m.acknowledge(3);
        require(m.state(h)==PcmNoteState::complete && !m.usage().pcm_bytes && m.usage().handles==1,"ack exact note end releases PCM, retains stable handle");
        rejects([&]{m.acknowledge(2);});rejects([&]{m.acknowledge(5);});
        m.stop(h);require(m.state(h)==PcmNoteState::complete,"stop completed idempotent");
        m.release(h);rejects([&]{m.state(h);});
    }
    {
        PcmMixer m(12000);m.add(audio({0,3000}));
        require(render(m,4)==stereo({0,2000,3000,0}),"rational linear upsample and last-sample clamp");
        PcmMixer down(8000);down.add(audio({1000,2000,3000},16000));
        require(render(down,3)==stereo({1000,3000,0}),"downsample endpoint uses ceil source duration");
        PcmMixer negative(12000);negative.add(audio({0,-1000}));
        require(render(negative,3)==stereo({0,-666,-1000}),"defined signed division toward zero");
        PcmMixer whole(44100),split(44100);
        whole.add(audio({-1000,1000,3000,5000},22050));split.add(audio({-1000,1000,3000,5000},22050));
        const auto expected=render(whole,9);std::vector<std::int16_t> actual;
        for(auto count:{1,3,2,3}){auto block=render(split,static_cast<std::size_t>(count));actual.insert(actual.end(),block.begin(),block.end());}
        require(actual==expected,"render block partition does not change resampling phase");
    }
    {
        PcmMixer m(8000);m.add(audio({30000,-30000}));m.add(audio({30000,-30000}));m.add(audio({-30000,30000}));
        require(render(m,2)==stereo({30000,-30000}),"sum wide before single saturation");
        PcmMixer clipped(8000);clipped.add(audio({32767,-32768}));clipped.add(audio({32767,-32768}));
        require(render(clipped,2)==stereo({32767,-32768}),"positive/negative final saturation");
        PcmMixer s(8000);s.add(audio({1000,2000,3000,4000},8000,2));
        require(render(s,2)==std::vector<std::int16_t>({1000,2000,3000,4000}),"interleaved stereo preserved");
        PcmMixer p(8000);const auto id=p.add(audio({2000,2000}));
        require(render(p,1)==stereo({2000}),"center unity controls");
        p.controls(id,{16384,32768});require(render(p,1)==std::vector<std::int16_t>({0,1000}),"half gain hard-right affects future render");
        PcmMixer left(8000);left.add(audio({2000}),{32768,-32768});
        require(render(left,1)==std::vector<std::int16_t>({2000,0}),"hard-left mono");
    }
    {
        PcmMixer m(8000,{2,1,4,4});const auto empty=m.add(audio({}));
        require(m.state(empty)==PcmNoteState::complete && !m.usage().pcm_bytes,"empty clip completes without PCM retention");
        auto input=audio({7});rejects([&]{m.add(std::move(input));});require(input.samples==std::vector<std::int16_t>{7},"record exhaustion does not move input");
        m.release(empty);const auto next=m.add(std::move(input));require(next!=empty,"handles never alias reused slots");rejects([&]{m.state(empty);});
        m.stop(next);m.stop(next);require(!m.usage().pcm_bytes && m.state(next)==PcmNoteState::stopped,"stop frees PCM exactly once");m.release(next);
        auto reserved=audio({1});reserved.samples.reserve(20);rejects([&]{m.add(std::move(reserved));});
        require(reserved.samples.size()==1 && reserved.samples.capacity()>=20,"capacity rather than size budget, rejection preserves input");
        const auto note=m.add(audio({1}));render(m,4);
        std::int16_t sentinel[4]={77,77,77,77};rejects([&]{m.render(sentinel,1);});
        require(sentinel[0]==77 && m.usage().rendered_frame==4,"pending budget rejects before writing/advancing");
        m.commit(2);m.acknowledge(1);require(m.state(note)==PcmNoteState::complete,"exact endpoint before queued silence");
        rejects([&]{m.render(sentinel,2);});m.render(sentinel,1);
        require(m.usage().rendered_frame==5,"pending capacity reclaimed by consumption");
    }
    {
        PcmMixer m(8000);const auto a=m.add(audio({1000,1000,1000,1000,1000,1000}));
        const auto b=m.add(audio({0,100,200,300,400,500}));
        require(render(m,4)==stereo({1000,1100,1200,1300}),"initial mixed cancellation fixture");
        m.commit(4);m.acknowledge(2);m.stop(a);m.rewind_pending();
        require(m.usage().rendered_frame==2 && m.usage().submitted_frame==2 && m.usage().consumed_frame==2,"rewind drops only pending horizon");
        require(render(m,2)==stereo({200,300}),"cancelled voice absent; survivor resumes exact consumed position");
        m.commit(2);m.acknowledge(4);require(m.state(b)==PcmNoteState::active,"survivor not prematurely completed");
        require(render(m,2)==stereo({400,500}),"survivor continues after replay");m.commit(2);m.acknowledge(6);
        require(m.state(b)==PcmNoteState::complete && !m.usage().pcm_bytes,"surviving PCM released at acknowledged end");
        m.release(a);m.release(b);require(!m.usage().handles,"record cleanup");
        PcmMixer fractional(12000);fractional.add(audio({0,3000,6000}));render(fractional,4);fractional.commit(4);fractional.acknowledge(1);
        fractional.rewind_pending();require(render(fractional,4)==stereo({2000,4000,6000,6000}),"rewind restores rational fractional phase");
        PcmMixer scheduled(8000);scheduled.add(audio({1,1,1,1}));render(scheduled,4);scheduled.commit(4);scheduled.acknowledge(2);
        const auto future=scheduled.add(audio({200,300}));scheduled.rewind_pending();
        require(render(scheduled,4)==stereo({1,1,200,300}),"rewind preserves start positions of notes added at prior render horizon");
        scheduled.commit(4);scheduled.acknowledge(6);require(scheduled.state(future)==PcmNoteState::complete,"future note still ends at original timestamp");
    }
    {
        PcmMixer m(8000);std::int16_t buffer[4]{};
        rejects([&]{m.render(nullptr,1);});rejects([&]{m.render(buffer,1025);});m.render(nullptr,0);m.commit(0);m.acknowledge(0);
        for(auto c:{PcmControls{32769,0},PcmControls{32768,-32769},PcmControls{32768,32769}}){rejects([&]{m.add(audio({0}),c);});}
        rejects([&]{m.state(0);});rejects([&]{m.stop(1);});rejects([&]{m.release(1);});
        rejects([&]{m.add(audio({1},7999));});rejects([&]{m.add(audio({1},192001));});
        rejects([&]{m.add(audio({1},8000,0));});rejects([&]{m.add(audio({1},8000,3));});rejects([&]{m.add(audio({1},8000,2));});
        rejects([]{PcmMixer bad(7999);});rejects([]{PcmMixer bad(192001);});
        rejects([]{PcmMixer bad(8000,{0,1,1,1});});rejects([]{PcmMixer bad(8000,{2,0,1,1});});
        rejects([]{PcmMixer bad(8000,{2,257,1,1});});rejects([]{PcmMixer bad(8000,{2,1,2,1});});
        rejects([]{PcmMixer bad(8000,{2,1,8193,8193});});rejects([]{PcmMixer bad(8000,{2,1,1,65537});});
        const auto id=m.add(audio({1000}));rejects([&]{m.controls(id,{32769,0});});
        require(render(m,1)==stereo({1000}),"invalid control update leaves note unchanged");
        rejects([&]{m.controls(999,{32768,0});});
        PcmMixer maxnotes(8000,{4096,256,1,1});
        for(unsigned n=0;n<256;++n)maxnotes.add(audio({1000}));
        require(render(maxnotes,1)==stereo({32767}),"maximum configured voices mix without intermediate overflow");
        auto input=audio({1});rejects([&]{maxnotes.add(std::move(input));});
        require(input.samples.size()==1,"full registry preserves failed source");
    }
    std::cout<<checks<<" portable PCM mixer checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
