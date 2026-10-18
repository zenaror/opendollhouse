#include "odh/pcm_transport.hpp"
#include <algorithm>
#include <deque>
#include <iostream>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action){
    bool failed=false;try{action();}catch(const std::exception&){failed=true;}
    require(failed,"expected rejection");
}
odh::XaAudio audio(std::vector<std::int16_t> samples,std::uint32_t rate=8000){
    return {rate,1,std::move(samples),0};
}
struct FakeSink final:odh::PcmSink {
    odh::PcmSinkFormat fmt{8000,2};
    std::deque<std::int16_t> samples;
    std::size_t capacity=32,max_accept=32,consume_on_pause=0,reported_extra=0;
    int pauses=0,starts=0,clears=0,queries=0,queues=0;
    bool paused=true,throw_queue=false,throw_query=false,throw_start=false,throw_clear=false,throw_pause=false;
    std::vector<std::int16_t> accepted;
    odh::PcmSinkFormat format() const override{return fmt;}
    std::size_t queued_frames() const override {
        auto& self=const_cast<FakeSink&>(*this);++self.queries;
        if(throw_query)throw std::runtime_error("query failed");
        return samples.size()/2+reported_extra;
    }
    std::size_t queue(const std::int16_t* pcm,std::size_t frames) override {
        ++queues;
        if(throw_queue)throw std::runtime_error("queue failed");
        const auto count=std::min({frames,max_accept,capacity-samples.size()/2});
        for(std::size_t i=0;i<count*2;++i){samples.push_back(pcm[i]);accepted.push_back(pcm[i]);}
        return count;
    }
    void pause() override {
        ++pauses;if(throw_pause)throw std::runtime_error("pause failed");paused=true;
        consume(std::min(consume_on_pause,samples.size()/2));consume_on_pause=0;
    }
    void start() override {++starts;if(throw_start)throw std::runtime_error("start failed");paused=false;}
    void clear() override {++clears;if(throw_clear)throw std::runtime_error("clear failed");samples.clear();}
    void consume(std::size_t frames){
        require(frames<=samples.size()/2,"fake sink consumed beyond queue");
        for(std::size_t i=0;i<frames*2;++i)samples.pop_front();
    }
    std::vector<std::int16_t> queued() const{return {samples.begin(),samples.end()};}
};
}

int main(){try{
    using namespace odh;
    {
        FakeSink sink;sink.fmt.channels=1;
        rejects([&]{PcmTransport bad(sink);});
        require(!sink.pauses && !sink.clears,"invalid channel format has no sink effects");
        sink.fmt={7999,2};rejects([&]{PcmTransport bad(sink);});
        require(!sink.pauses,"invalid rate before sink effects");
        sink.fmt={8000,2};rejects([&]{PcmTransport bad(sink,{0,1,1,1});});
        require(!sink.pauses,"invalid limits before sink effects");
        sink.samples.push_back(1);sink.samples.push_back(1);
        rejects([&]{PcmTransport bad(sink);});
        require(!sink.pauses && !sink.clears && sink.queued()==std::vector<std::int16_t>{1,1},
                "preexisting queue rejected without discarding caller audio");
    }
    {
        FakeSink sink;sink.capacity=2;sink.max_accept=1;
        PcmTransport transport(sink,{1024,4,4,8});
        const auto note=transport.add(audio({10,20,30,40,50,60}));
        require(sink.pauses==1 && !sink.starts,"constructor leaves empty sink paused");
        transport.tick();require(sink.queued()==std::vector<std::int16_t>({10,10}) &&
                                 transport.usage().submitted_frame==1 && sink.starts==1,"first partial prefix starts sink");
        transport.tick();require(transport.usage().submitted_frame==2,"second prefix commits once");
        transport.tick();require(transport.usage().submitted_frame==2 &&
                                 transport.usage().rendered_frame==4,"zero acceptance retains same block");
        sink.consume(1);transport.tick();
        require(transport.usage().consumed_frame==1 && transport.usage().submitted_frame==3 &&
                sink.queued()==std::vector<std::int16_t>({20,20,30,30}),"consumption opens room for retained prefix");
        sink.consume(2);transport.tick();
        require(transport.usage().consumed_frame==3 && transport.usage().submitted_frame==4 &&
                sink.queued()==std::vector<std::int16_t>({40,40}),"all first-block prefixes submitted in order");
        sink.consume(1);transport.tick();
        require(transport.state(note)==PcmNoteState::rendered &&
                sink.queued()==std::vector<std::int16_t>({50,50}),"second block renders only while note active");
        sink.consume(1);transport.tick();require(transport.state(note)==PcmNoteState::rendered,
                "rendered note remains pending before endpoint consumption");
        sink.consume(1);transport.tick();
        require(transport.state(note)==PcmNoteState::complete && !transport.usage().pcm_bytes,
                "acknowledged endpoint releases note despite trailing padding");
        for(int i=0;i<8 && (transport.usage().submitted_frame<8 || sink.queued_frames());++i){
            if(sink.queued_frames())sink.consume(sink.queued_frames());
            transport.tick();
        }
        require(transport.usage().submitted_frame==8 && transport.usage().consumed_frame==8,
                "already-rendered padding drains without generating new silence");
        const auto rendered=transport.usage().rendered_frame;transport.tick();
        require(transport.usage().rendered_frame==rendered,"inactive transport does not render");
    }
    {
        FakeSink sink;PcmTransport transport(sink,{1024,4,4,8});
        const auto victim=transport.add(audio({1000,1000,1000,1000,1000,1000}));
        const auto survivor=transport.add(audio({0,100,200,300,400,500}));
        transport.tick();sink.consume(2);sink.consume_on_pause=1;
        transport.stop(victim);
        require(sink.paused && sink.clears==1 && !sink.queued_frames() &&
                transport.usage().consumed_frame==3 && transport.usage().submitted_frame==3 &&
                transport.usage().rendered_frame==3,"pause-boundary consumption acknowledged before clear and rewind");
        require(transport.state(victim)==PcmNoteState::stopped,"victim stopped after clearing shared queue");
        transport.tick();
        require(sink.queued().front()==300 && sink.queued()[1]==300 && sink.starts==2,
                "survivor restarts at consumed timeline without cancelled samples");
        sink.consume(sink.queued_frames());transport.tick();
        require(transport.state(survivor)==PcmNoteState::complete,"survivor finishes at original endpoint");
        transport.release(victim);transport.release(survivor);
        require(!transport.usage().handles,"stopped and completed handles released");
    }
    {
        FakeSink sink;sink.capacity=2;sink.max_accept=2;
        PcmTransport transport(sink,{1024,4,4,8});
        const auto victim=transport.add(audio({1000,1000,1000,1000,1000,1000}));
        const auto survivor=transport.add(audio({0,100,200,300,400,500}));
        transport.tick();
        require(transport.usage().rendered_frame==4 && transport.usage().submitted_frame==2,
                "cancellation fixture holds both accepted queue and unaccepted suffix");
        sink.consume(1);transport.stop(victim);
        require(transport.usage().rendered_frame==1 && transport.usage().submitted_frame==1 &&
                transport.usage().consumed_frame==1 && sink.queued().empty(),
                "flush discards accepted tail and retained suffix together");
        const auto clears=sink.clears;transport.stop(victim);
        require(sink.clears==clears,"repeated stop does not flush surviving note");
        transport.tick();
        require(sink.queued()==std::vector<std::int16_t>({100,100,200,200}),
                "survivor restarts with exact first two frames, no stale victim prefix");
        sink.consume(1);transport.tick();
        require(sink.queued()==std::vector<std::int16_t>({200,200,300,300}),
                "survivor's retained suffix follows regenerated prefix");
        sink.consume(2);transport.tick();
        require(sink.queued()==std::vector<std::int16_t>({400,400}),
                "survivor phase remains exact through full regenerated block");
        transport.release(victim);transport.release(survivor);
    }
    {
        FakeSink sink;sink.fmt.sample_rate=12000;
        PcmTransport transport(sink,{1024,4,4,8});
        const auto victim=transport.add(audio({100,100,100,100}));
        const auto survivor=transport.add(audio({0,3000,6000}));
        transport.tick();sink.consume(1);transport.release(victim);
        require(transport.usage().consumed_frame==1 && transport.usage().rendered_frame==1,
                "release pending note rewinds fractional survivor");
        transport.tick();require(sink.queued().front()==2000 && sink.queued()[1]==2000,
                "rational phase recomputed at consumed frame");
        rejects([&]{transport.state(victim);});
        transport.release(survivor);
    }
    {
        FakeSink sink;PcmTransport transport(sink,{1024,4,4,8});
        const auto note=transport.add(audio({2000,2000,2000,2000}));
        const auto before=sink.clears;
        rejects([&]{transport.controls(note,{32769,0});});
        rejects([&]{transport.controls(999,{32768,0});});
        rejects([&]{transport.stop(999);});
        require(sink.clears==before && !transport.failed(),
                "invalid controls/handle have no sink effects or failure latch");
        transport.tick();sink.consume(1);
        transport.controls(note,{16384,32768});
        require(sink.clears==before+1 && transport.usage().consumed_frame==1,
                "control update flushes old pending mix at consumed position");
        transport.tick();require(sink.queued()[0]==0 && sink.queued()[1]==1000,
                "new controls apply to regenerated pending frames");
        sink.consume(sink.queued_frames());transport.tick();
        require(transport.state(note)==PcmNoteState::complete,"control fixture completes");
        const auto clears=sink.clears;transport.stop(note);transport.stop(note);transport.release(note);
        require(sink.clears==clears,"completed stop/release are no-op for shared queue");
    }
    {
        FakeSink sink;PcmTransport transport(sink,{1024,4,4,8});
        const auto note=transport.add(audio({1,2,3,4,5}));
        sink.throw_queue=true;rejects([&]{transport.tick();});
        require(transport.failed() && sink.paused && sink.clears==1,
                "queue failure latches fail-closed state and clears sink");
        sink.throw_queue=false;rejects([&]{transport.tick();});
        require(!sink.starts,"failed transport cannot resume stale output");
        transport.release(note);require(!transport.usage().handles,"failed transport permits note cleanup");
    }
    {
        FakeSink sink;
        PcmTransport transport(sink,{1024,4,4,8});
        sink.reported_extra=1;
        rejects([&]{transport.tick();});
        require(transport.failed() && sink.paused && sink.clears==1,
                "external queue frames violate exclusive ownership and fail closed");
    }
    {
        FakeSink sink;PcmTransport transport(sink,{1024,4,4,8});
        transport.add(audio({1,2}));sink.throw_start=true;
        rejects([&]{transport.tick();});
        require(transport.failed() && sink.paused && sink.queued().empty(),
                "start failure clears accepted PCM and forbids replay");
    }
    {
        FakeSink sink;PcmTransport transport(sink,{1024,4,4,8});
        const auto note=transport.add(audio({1,2,3,4,5}));
        transport.tick();sink.throw_clear=true;
        rejects([&]{transport.release(note);});
        require(transport.failed() && sink.paused && !transport.usage().handles &&
                !transport.usage().pcm_bytes,
                "clear failure latches failed state and releases note on first attempt");
        rejects([&]{transport.state(note);});
        rejects([&]{transport.tick();});
        sink.throw_clear=false;
    }
    for(const bool query_failure:{false,true}){
        FakeSink sink;PcmTransport transport(sink,{1024,4,4,8});
        const auto note=transport.add(audio({1,2,3,4,5}));
        transport.tick();
        if(query_failure)sink.throw_query=true;else sink.throw_pause=true;
        rejects([&]{transport.stop(note);});
        require(transport.failed() && transport.state(note)==PcmNoteState::stopped &&
                !transport.usage().pcm_bytes && sink.queued().empty(),
                "pause/query failure still stops target and clears source PCM");
        rejects([&]{transport.tick();});
        sink.throw_query=false;sink.throw_pause=false;
        transport.release(note);
    }
    {
        FakeSink sink;PcmTransport transport(sink,{32,2,4,8});
        auto pcm=audio({1000,2000,3000,4000,5000,6000});
        const auto shared=transport.load_reusable(std::move(pcm));
        const auto survivor=transport.add(audio({10,20,30,40,50,60}));
        require(pcm.samples.empty() && transport.state(shared)==PcmNoteState::stopped &&
                transport.usage().pcm_bytes==24,"reusable source admitted once beside other note");
        transport.replay(shared,{8000,false});transport.tick();sink.consume(2);
        transport.replay(shared,{8000,false});
        require(sink.queued().empty() && transport.usage().consumed_frame==2 &&
                transport.usage().pcm_bytes==24,"active replay clears pending shared mix without copying PCM");
        transport.tick();
        require(sink.queued()==std::vector<std::int16_t>({1030,1030,2040,2040,3050,3050,4060,4060}),
                "replayed handle starts at zero while survivor continues at consumed frame");
        sink.consume(1);transport.stop(shared);
        require(transport.state(shared)==PcmNoteState::stopped &&
                transport.usage().pcm_bytes==24,"stop retains reusable resource and survivor");
        transport.tick();
        require(sink.queued()[0]==40 && sink.queued()[1]==40,
                "survivor continues after shared handle stop");
        transport.release(shared);transport.release(survivor);
        require(!transport.usage().pcm_bytes && !transport.usage().handles,
                "release frees shared and one-shot resources");
    }
    {
        FakeSink sink;sink.capacity=2;sink.max_accept=2;
        PcmTransport transport(sink,{32,2,4,8});
        const auto shared=transport.load_reusable(audio({100,200,300,400}));
        const auto survivor=transport.add(audio({1,2,3,4}));
        transport.replay(shared,{8000,false});transport.tick();
        require(transport.usage().rendered_frame==4 && transport.usage().submitted_frame==2,
                "replay fixture has queued prefix and retained suffix");
        sink.consume(1);transport.replay(shared,{8000,false});
        require(transport.usage().rendered_frame==1 &&
                transport.usage().submitted_frame==1 &&
                transport.usage().consumed_frame==1 && sink.queued().empty(),
                "replay drops accepted tail and retained suffix together");
        transport.tick();
        require(sink.queued()==std::vector<std::int16_t>({102,102,203,203}),
                "replay after partial queue preserves survivor phase");
        transport.release(shared);transport.release(survivor);
    }
    {
        FakeSink sink;PcmTransport transport(sink,{32,2,4,8});
        const auto shared=transport.load_reusable(audio({0,1000,2000,3000,4000}));
        const auto clears=sink.clears;
        rejects([&]{transport.replay(shared,{99,false});});
        rejects([&]{transport.replay(shared,{192001,false});});
        rejects([&]{transport.set_frequency(shared,0);});
        rejects([&]{transport.replay(999,{8000,false});});
        require(sink.clears==clears && !transport.failed() &&
                transport.state(shared)==PcmNoteState::stopped,
                "invalid replay/frequency/handle reject before sink I/O");
        transport.replay(shared,{4000,false});transport.tick();sink.consume(2);
        transport.set_frequency(shared,8000);
        require(sink.queued().empty() && transport.usage().consumed_frame==2,
                "rate change flushes rendered tail at old-rate consumed phase");
        transport.tick();
        require(sink.queued()==std::vector<std::int16_t>({1000,1000,2000,2000,3000,3000,4000,4000}),
                "transport rate change preserves current source position");
        sink.consume(4);transport.tick();
        require(transport.state(shared)==PcmNoteState::complete &&
                transport.usage().pcm_bytes==10,
                "acknowledged changed-rate completion retains reusable PCM");
        transport.release(shared);
    }
    {
        FakeSink sink;PcmTransport transport(sink,{2,1,4,8});
        auto oversized=audio({1,2});
        rejects([&]{transport.load_reusable(std::move(oversized));});
        require(oversized.samples==std::vector<std::int16_t>({1,2}) &&
                !transport.usage().handles,"reusable budget rejection leaves source untouched");
        const auto shared=transport.load_reusable(audio({7}));
        transport.replay(shared,{8000,true});transport.tick();
        require(transport.state(shared)==PcmNoteState::active &&
                transport.usage().pcm_bytes==2,"loop retains one bounded PCM resource");
        sink.throw_pause=true;rejects([&]{transport.replay(shared,{8000,true});});
        require(transport.failed(),"replay sink failure latches fail-closed transport");
        sink.throw_pause=false;transport.release(shared);
        require(!transport.usage().handles && !transport.usage().pcm_bytes,
                "failed replay still permits resource cleanup");
    }
    std::cout<<checks<<" portable PCM transport checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
