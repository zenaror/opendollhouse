#include "xa_patch_catalog.hpp"
#include "odh/hit_native_controller.hpp"
#include "odh/hit_patch_registry.hpp"
#include "odh/pcm_transport.hpp"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
unsigned checks = 0;
void check(bool value, const char* why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
void word(std::string& bytes, std::uint32_t value) {
    for (unsigned i=0; i<4; ++i) bytes.push_back(static_cast<char>((value>>(8*i))&255));
}
odh::Hot hot(const std::string& text) {
    std::istringstream stream(text);
    return odh::read_hot(odh::View(stream,text.size()),nullptr,{},odh::HotReadProfile::native_entry_map);
}
struct Sink final : odh::PcmSink {
    std::deque<std::int16_t> samples;
    odh::PcmSinkFormat format() const override { return {22050,2}; }
    std::size_t queued_frames() const override { return samples.size()/2; }
    std::size_t queue(const std::int16_t* source,std::size_t frames) override {
        for (std::size_t i=0; i<frames*2; ++i) samples.push_back(source[i]);
        return frames;
    }
    void pause() override {}
    void start() override {}
    void clear() override { samples.clear(); }
    void consume() { samples.clear(); }
    std::vector<std::int16_t> queued() const { return {samples.begin(),samples.end()}; }
};
struct Counts { unsigned created=0,destroyed=0,loads=0,notes=0; };

// Test assembly only: explicit unity-Q15/center-pan PCM, header playback rate,
// no loop. It exercises ownership/replay, not native DSP or streaming choice.
class FixtureBackend final : public odh::NativeHitPatchBackend {
public:
    FixtureBackend(const odh::NativeHitPatchRef& patch, const odh_host::XaPatchCatalog& assets,
                   odh::PcmTransport& transport, Counts& counts)
        : patch_(patch),assets_(assets),transport_(transport),counts_(counts) { ++counts_.created; }
    ~FixtureBackend() noexcept override {
        try { clear_resources(); } catch (...) {}
        ++counts_.destroyed;
    }
    void load_resources() override {
        if (handle_) return;
        auto asset=assets_.load(patch_,transport_.available_pcm_bytes());
        const auto rate=asset.audio.sample_rate;
        const auto handle=transport_.load_reusable(std::move(asset.audio));
        handle_=handle;rate_=rate;++counts_.loads;
    }
    void clear_resources() override {
        if (!handle_) return;
        const auto old=handle_;handle_=0;transport_.release(old);
    }
    bool active() const override {
        if (!handle_) return false;
        const auto state=transport_.state(handle_);
        return state==odh::PcmNoteState::active || state==odh::PcmNoteState::rendered;
    }
    void note_on() override {
        load_resources();transport_.replay(handle_,{rate_,false});++counts_.notes;
    }
    void stop() override { if (handle_) transport_.stop(handle_); }
    void raw_control(std::uint8_t,std::int32_t) override {
        throw odh::FormatError("fixture has no native DSP conversion");
    }
private:
    odh::NativeHitPatchRef patch_;
    const odh_host::XaPatchCatalog& assets_;
    odh::PcmTransport& transport_;
    Counts& counts_;
    odh::PcmNoteHandle handle_=0;
    std::uint32_t rate_=0;
};
struct Endpoint {
    odh::NativeHitPatchRegistry& registry;
    odh::NativeHitPatchRegistry::BindingLease binding;
    odh::NativeHitPatchRegistry::ResourceLease resource;
    explicit Endpoint(odh::NativeHitPatchRegistry& value) : registry(value) {}
    ~Endpoint() { release(); }
    void release() noexcept { resource.reset();binding.reset(); }
    odh::NativeHitAudio audio() {
        odh::NativeHitAudio a;
        a.active=[this]{return registry.active(binding.view());};
        a.note_on=[this](std::uint8_t literal){registry.note_on(binding.view(),literal);};
        a.stop_current=[this]{registry.stop(binding.view());};
        a.release_binding=[this]{release();};
        a.pause_current=[this]{registry.pause(binding.view());};
        a.resume_current=[this]{registry.resume(binding.view());};
        a.bind_global_patch=[this](const odh::NativeHitPatchRef& patch,const odh::NativeHitState&){
            check(patch.group==1 && patch.patch->native_id26==false,
                  "fixture uses the global nonspatial Patch, not the local shadow");
            auto acquired=registry.acquire(patch.id);
            auto loaded=registry.load_resources(acquired);
            binding=std::move(acquired);resource=std::move(loaded);
            return std::int32_t{0};
        };
        // Explicit test profile ignores raw native controls, rather than
        // pretending unity Q15 is their native gain or pitch interpretation.
        a.apply_global_controls=[](const odh::NativeHitState&){};
        a.apply_global_control_write=[](std::uint8_t,const odh::NativeHitState&){};
        return a;
    }
};
}
int main(int argc,char** argv) { try {
    if (argc!=2) throw std::runtime_error("private fixture directory required");
    const auto path=(std::filesystem::path(argv[1])/"case.xa").string();
    std::string bytes("XAI\0",4);word(bytes,6);
    bytes+=std::string("\1\0\1\0",4);word(bytes,22050);word(bytes,44100);
    bytes+=std::string("\2\0\20\0",4);
    bytes.push_back(0);bytes.push_back(0x12);bytes.push_back(0x30);bytes+=std::string(12,'\0');
    std::ofstream file(path,std::ios::binary);file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));file.close();
    if (!file) throw std::runtime_error("cannot write XA fixture");
    odh_host::XaPatchCatalog assets({{path,odh_host::XaSourceKind::loose}});
    auto local=hot("[Track]\n1=0,fixture,1024,2,0,0,20,300,90\n[TrackData]\n300=16\n[Patch]\n90=0,shadow,\"missing.xa\",0,0,0,0,0\n");
    auto global=hot("[Patch]\n90=0,winner,\"case.xa\",0,0,0,0,0\n91=0,budget,\"case.xa\",0,0,0,0,0\n");
    odh::HitFile code;code.major_version=1;code.minor_version=8;code.bytes.resize(16);
    for (const int byte : {2,17,11,12}) code.bytes.push_back(static_cast<std::uint8_t>(byte));
    const std::vector<odh::HitEntryGroup> groups{{&code,&local},{&code,&global}};
    odh::HitEntryCatalog entries(groups);odh::NativeHitObjectCatalog objects(groups,entries);
    Sink sink;odh::PcmTransport transport(sink,{8,4,2,8});Counts counts;
    odh::NativeHitPatchRegistry registry(objects,[&](const odh::NativeHitPatchRef& patch){
        return std::make_unique<FixtureBackend>(patch,assets,transport,counts);
    },{2,4,4});
    Endpoint a(registry),b(registry);
    const odh::NativeHitPatchSource source{odh::NativeHitPatchSource::Scope::global_catalog,&objects};
    odh::NativeHitController first(code,local,local.tracks.at(1),16,{{1,0,0}},1024,512,a.audio(),{}, {},{}, {},source);
    odh::NativeHitController second(code,local,local.tracks.at(1),16,{{2,0,0}},1024,512,b.audio(),{}, {},{}, {},source);
    first.activate(1);second.activate(1);
    check(counts.created==1 && counts.loads==1 && transport.usage().pcm_bytes==6 &&
          transport.usage().handles==1,"two bindings charge one retained PCM allocation and handle");
    check(first.poll(1)==odh::NativeHitPoll::progressed,"first controller starts PCM and waits for consumption");
    transport.tick();
    check(sink.queued()==std::vector<std::int16_t>{4096,4096,8192,8192},"global XA decodes into exact fixture PCM");
    check(second.poll(1)==odh::NativeHitPoll::progressed,"second controller NoteOn replays shared buffer");
    transport.tick();
    check(sink.queued()==std::vector<std::int16_t>{4096,4096,8192,8192} && counts.loads==1 && counts.notes==2,
          "active replay rewinds without a second decode or allocation");
    registry.pause(a.binding.view());
    check(sink.samples.empty() && transport.usage().pcm_bytes==6 && !registry.active(b.binding.view()),
          "pause stops shared audio but retains decoded PCM");
    registry.resume(a.binding.view());
    check(counts.notes==2 && !registry.active(a.binding.view()),"resume does not replay");
    auto over_budget=registry.acquire(91);
    bool denied=false;
    try { auto ignored=registry.load_resources(over_budget); }
    catch (const odh::FormatError&) { denied=true; }
    check(denied && transport.usage().pcm_bytes==6 && counts.loads==1 &&
          registry.usage().resource_refs==2,"second generation cannot bypass aggregate PCM budget");
    over_budget.reset();
    registry.note_on(b.binding.view(),99);transport.tick();sink.consume();transport.tick();
    check(registry.active(a.binding.view()),"rendered tail remains active until consumed");
    sink.consume();transport.tick();
    check(!registry.active(a.binding.view()) && transport.usage().pcm_bytes==6,
          "consumed completion retains reusable PCM until explicit release");
    first.poll(3);second.poll(3);
    check(registry.usage().bindings==0 && registry.usage().resource_refs==0 &&
          transport.usage().pcm_bytes==0 && transport.usage().handles==0 &&
          counts.destroyed==2 && !registry.usage().cleanup_failed,
          "final controller teardown releases one shared resource and failed-generation owner");
    std::cout<<checks<<" global Patch/XA/reusable PCM fixture checks passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; } }
