#include "odh/pcm_hit_owner.hpp"
#include <algorithm>
#include <deque>
#include <iostream>

namespace {
int checks=0;
void require(bool condition,const char* why){++checks;if(!condition)throw std::runtime_error(why);}
template<class F>void rejects(F action){
    bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}
    require(rejected,"expected explicit rejection");
}
odh::XaAudio audio(std::initializer_list<std::int16_t> samples){
    return {8000,1,std::vector<std::int16_t>(samples),0};
}
struct FakeSink final:odh::PcmSink {
    std::deque<std::int16_t> pcm;
    bool paused=true,throw_clear=false,throw_queue=false;
    int pauses=0,starts=0,clears=0;
    odh::PcmSinkFormat format() const override{return {8000,2};}
    std::size_t queued_frames() const override{return pcm.size()/2;}
    std::size_t queue(const std::int16_t* data,std::size_t frames) override {
        if(throw_queue)throw std::runtime_error("synthetic queue failure");
        for(std::size_t i=0;i<frames*2;++i)pcm.push_back(data[i]);
        return frames;
    }
    void pause() override{++pauses;paused=true;}
    void start() override{++starts;paused=false;}
    void clear() override{++clears;if(throw_clear)throw std::runtime_error("synthetic clear failure");pcm.clear();}
    void consume(std::size_t frames){
        require(frames<=queued_frames(),"fake consumed beyond queue");
        for(std::size_t i=0;i<frames*2;++i)pcm.pop_front();
    }
};
}

int main(){try{
    using namespace odh;
    {
        FakeSink sink;PcmTransport transport(sink,{1024,4,2,4});
        std::vector<std::uint64_t> allowances;
        std::vector<std::uint32_t> patches;
        PcmHitOwner owner(transport,[&](std::uint32_t patch,std::uint64_t allowance){
            patches.push_back(patch);allowances.push_back(allowance);
            return audio({10,20});
        },{},2);
        int kills=0;auto backend=owner.backend([&](std::uint8_t group){
            require(group==7,"explicit group passed through");++kills;
        });
        const auto first=backend.start(101);const auto second=backend.start(102);
        require(first && second && *first!=*second && patches==std::vector<std::uint32_t>({101,102}),
                "successful starts allocate distinct persistent handles");
        require(allowances.size()==2 && allowances[0]==1024 && allowances[1]<allowances[0],
                "loader receives available PCM allowance before each admission");
        require(!backend.start(103) && patches.size()==2,"full lifetime note budget refuses before loader");
        require(backend.status(*first)==HitNoteStatus::active &&
                backend.status(*second)==HitNoteStatus::active,"new notes active for HIT");
        backend.kill_group(7);require(kills==1,"only explicit group callback executes");
        transport.tick();
        require(backend.status(*first)==HitNoteStatus::active &&
                transport.state(*first)==PcmNoteState::rendered,
                "rendered note remains HIT active until consumed");
        sink.consume(1);transport.tick();
        require(backend.status(*first)==HitNoteStatus::active,
                "partial consumption does not acknowledge note endpoint");
        sink.consume(1);transport.tick();
        require(backend.status(*first)==HitNoteStatus::complete &&
                backend.status(*second)==HitNoteStatus::complete,
                "fully consumed notes map to HIT complete");
        require(!backend.start(104) && patches.size()==2,
                "completed note does not recycle lifetime admission slot");
        owner.stop();owner.stop();
        require(!transport.usage().handles && !transport.usage().pcm_bytes &&
                !owner.cleanup_failed(),"idempotent stop releases every completed note");
        require(!backend.start(105) && patches.size()==2,"start after stop never invokes loader");
        require(backend.status(*first)==HitNoteStatus::failed &&
                backend.status(*second)==HitNoteStatus::failed,"owned status after stop fails deterministically");
        rejects([&]{backend.status(999);});
        rejects([&]{backend.kill_group(7);});
        require(kills==1,"group effect after stop is never invoked");
    }
    {
        FakeSink sink;PcmTransport transport(sink,{1024,4,2,4});
        int loads=0;
        PcmHitOwner a(transport,[&](auto patch,auto){
            ++loads;if(patch==77)throw FormatError("unsupported patch");return audio({100});
        },{},2);
        PcmHitOwner b(transport,[](auto,auto){return audio({200});});
        auto first=a.backend();auto other=b.backend();
        require(!first.kill_group,"no group callback remains absent for opcode 69 fault contract");
        rejects([&]{first.start(77);});
        require(loads==1 && !transport.usage().handles,"loader error propagates without phantom handle");
        const auto owned=first.start(1);const auto foreign=other.start(2);
        require(owned && foreign,"both owners started a note");
        rejects([&]{first.status(*foreign);});
        rejects([&]{other.status(*owned);});
        require(first.status(*owned)==HitNoteStatus::active,"owned handle still queryable");
        transport.stop(*owned);
        require(first.status(*owned)==HitNoteStatus::complete,
                "explicitly stopped note maps to HIT complete while owner remains live");
        a.stop();
        require(transport.usage().handles==1 && other.status(*foreign)==HitNoteStatus::active,
                "one owner stops without closing or releasing another owner's note");
        b.stop();require(!transport.usage().handles,"other owner cleans up independently");
    }
    {
        FakeSink sink;PcmTransport transport(sink,{1024,6,2,4});
        unsigned loads=0;
        PcmHitOwner owner(transport,[&](auto patch,auto){
            ++loads;const auto sample=static_cast<std::int16_t>(patch);return audio({sample,sample});
        },{},3);
        PcmHitOwner other(transport,[](auto,auto){return audio({300,300});});
        auto backend=owner.backend({},PcmSequenceGroupProfile::simitone_instance);
        auto foreign_backend=other.backend();
        rejects([&]{owner.backend([](std::uint8_t){},PcmSequenceGroupProfile::simitone_instance);});
        rejects([&]{owner.backend({},static_cast<PcmSequenceGroupProfile>(99));});
        backend.kill_group(0); // Valid before the first note and does not retire owner.
        const auto first=backend.start(100),second=backend.start(200),foreign=foreign_backend.start(1);
        require(first && second && foreign,"instance group fixture admits independent owned notes");
        transport.tick();
        require(!sink.pcm.empty() && sink.pcm.front()==600,"fixture mixed owned and foreign active notes");
        const auto clears=sink.clears;
        backend.kill_group(7);
        require(sink.clears==clears && backend.status(*first)==HitNoteStatus::active &&
                backend.status(*second)==HitNoteStatus::active,"nonzero Simitone group is a no-op");
        backend.kill_group(0);
        require(backend.status(*first)==HitNoteStatus::complete && backend.status(*second)==HitNoteStatus::complete,
                "instance group stops all own notes while retaining queryable handles");
        require(foreign_backend.status(*foreign)==HitNoteStatus::active && transport.usage().handles==3,
                "instance group preserves foreign owner and does not release note handles");
        transport.tick();
        require(!sink.pcm.empty() && sink.pcm.front()==300,"pending cancelled samples discarded and foreign PCM preserved");
        const auto later=backend.start(400);
        require(later && backend.status(*later)==HitNoteStatus::active && loads==3,
                "instance group permits a new note after cancellation");
        backend.kill_group(0);
        require(backend.status(*later)==HitNoteStatus::complete && !backend.start(500) && loads==3,
                "instance cancellation retains cumulative lifetime note budget");
        owner.stop();
        require(transport.usage().handles==1 && foreign_backend.status(*foreign)==HitNoteStatus::active,
                "final instance owner teardown releases its handles only");
        rejects([&]{backend.kill_group(0);});
    }
    {
        FakeSink sink;PcmTransport transport(sink,{1024,4,2,4});
        PcmHitOwner owner(transport,[](auto,auto){return audio({1,2,3});});
        int group_effects=0;
        auto backend=owner.backend([&](std::uint8_t){++group_effects;});
        const auto handle=backend.start(1);
        require(handle.has_value(),"failure fixture starts note");
        sink.throw_queue=true;rejects([&]{transport.tick();});
        require(transport.failed() && backend.status(*handle)==HitNoteStatus::failed,
                "transport failure maps owned note to HIT failed");
        rejects([&]{backend.kill_group(7);});
        require(group_effects==0,"failed transport blocks explicit group side effect");
        owner.stop();
        require(!owner.cleanup_failed() && !transport.usage().handles &&
                !transport.usage().pcm_bytes,"owner releases note after latched transport fault");
    }
    {
        FakeSink sink;PcmTransport transport(sink,{1024,4,2,4});
        PcmHitOwner owner(transport,[](auto,auto){return audio({1,2,3});}, {},2);
        auto backend=owner.backend();const auto a=backend.start(1),b=backend.start(2);
        transport.tick();sink.throw_clear=true;
        owner.stop();
        require(a && b && owner.cleanup_failed() && transport.failed() &&
                !transport.usage().handles && !transport.usage().pcm_bytes,
                "failed first flush records cleanup error but releases every owned note");
        require(backend.status(*a)==HitNoteStatus::failed &&
                backend.status(*b)==HitNoteStatus::failed,"failed cleanup cannot masquerade as completion");
    }
    {
        FakeSink sink;PcmTransport transport(sink);
        HitPcmLoader empty;
        rejects([&]{PcmHitOwner bad(transport,empty);});
        rejects([&]{PcmHitOwner bad(transport,[](auto,auto){return audio({1});},{},0);});
        rejects([&]{PcmHitOwner bad(transport,[](auto,auto){return audio({1});},{},257);});
        rejects([&]{PcmHitOwner bad(transport,[](auto,auto){return audio({1});},{32769,0});});
        require(!transport.usage().handles,"invalid owner construction has no note effects");
    }
    {
        FakeSink sink;PcmTransport transport(sink,{1024,4,2,4});
        HitNoteBackend backend;
        int loads=0;
        PcmHitOwner owner(transport,[&](auto,auto){
            ++loads;rejects([&]{backend.start(2);});return audio({1});
        },{},1);
        backend=owner.backend();
        const auto handle=backend.start(1);
        require(handle && loads==1 && transport.usage().handles==1,
                "reentrant start rejected before loader and lifetime slot stays bounded");
        owner.stop();
    }
    {
        FakeSink sink;PcmTransport transport(sink,{1024,4,2,4});
        PcmHitOwner* owner_ptr=nullptr;
        PcmHitOwner owner(transport,[&](auto,auto){
            owner_ptr->stop();return audio({1});
        });
        owner_ptr=&owner;
        auto backend=owner.backend();
        require(!backend.start(1) && !transport.usage().handles,
                "stop during loader prevents late note admission");
    }
    std::cout<<checks<<" portable PCM HIT owner checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
