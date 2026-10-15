#include "bhav_audio.hpp"
#include "linux_pcm_sink.hpp"
#include "xa_sound_factory.hpp"
#include "odh/hit_arguments.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <thread>

namespace odh_host {
namespace {
template<class Reader>auto read_resource(const std::string& path,Reader reader){
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw odh::FormatError("cannot open explicit HIT resource");
    return reader(odh::View(stream,std::filesystem::file_size(path)));
}
std::string quote(const std::string& text){
    std::ostringstream out;out<<'"';
    for(const unsigned char c:text){
        if(c=='"' || c=='\\')out<<'\\'<<static_cast<char>(c);
        else if(c<32 || c>=127)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<unsigned>(c)<<std::dec;
        else out<<static_cast<char>(c);
    }
    out<<'"';return out.str();
}
}
struct BhavAudio::Impl {
    odh::World& world;
    BhavAudioConfig config;
    odh::Hsm hsm;
    odh::Hot hot;
    odh::HitFile hit;
    XaPatchCatalog catalog;
    LinuxPcmSink sink;
    odh::PcmTransport transport;
    odh::SoundThreadCollection threads;
    std::uint64_t submitted=0,rejected=0,submit_errors=0;
    std::uint64_t scheduler_ticks=0,completed=0,owner_invalid=0,faulted=0,scheduler_errors=0;
    std::uint16_t drain_requested_ms=0;
    std::uint64_t drain_elapsed_ms=0,drain_polls=0;
    bool drain_output_empty=false,drain_timed_out=false,stopped_pumping=false;
    std::string diagnostic;
    std::optional<odh::HitArguments> last_arguments;
    std::optional<BhavNativeControlReceipt> last_native_controls;

    Impl(odh::World& world_value,BhavAudioConfig config_value)
      :world(world_value),config(std::move(config_value)),
       hsm(read_resource(config.hsm_path,[](const auto& view){return odh::read_hsm(view);})),
       hot(read_resource(config.hot_path,[this](const auto& view){return odh::read_hot(view,&hsm);})),
       hit(read_resource(config.hit_path,[](const auto& view){return odh::read_hit(view);})),
       catalog(config.sources),sink(config.output_rate),transport(sink),
       threads(world,[this](const odh::SoundRequest& request,const odh::SoundSourceBinding& binding){
           auto arguments=config.arguments;
           std::optional<odh::ResolvedHitEvent> resolved;
           if(config.caller_arguments || config.native_controls)
               resolved=odh::resolve_hit_event(hit,hot,request,{},config.native_controls
                   ? odh::HitPatchMode::native_controller : odh::HitPatchMode::raw);
           if(config.caller_arguments){
               // Resolution validated the name before this bounded copy.
               auto name=request.name;
               for(auto& c:name)if(c>='A' && c<='Z')c=static_cast<char>(c-'A'+'a');
               arguments=odh::play_sound_arguments(hot.events.at(name),request.source);
           }
           last_arguments=arguments;
           std::optional<odh::HitNativeControls> native_controls;
           if(config.native_controls){
               native_controls=odh::build_native_hit_controls(*resolved->initial_track,arguments,
                   (*config.native_controls)[0],(*config.native_controls)[1]);
               BhavNativeControlReceipt receipt{};
               for(std::size_t i=0;i<receipt.size();++i)
                   receipt[i]=native_controls->locals[native_control_receipt_ids[i]-16];
               last_native_controls=receipt;
           }
           odh::HitChooser first=[](std::uint32_t,const std::vector<std::uint32_t>& choices){
               if(choices.empty())throw odh::FormatError("diagnostic chooser received empty HIT list");
               return std::size_t{0};
           };
           return make_xa_hit_sound(hit,hot,request,binding,transport,catalog,std::move(first),
               config.controls,{},config.jump,arguments,{},{},std::move(native_controls));
       }){}

    void note_error(const std::exception& error){
        if(diagnostic.empty())diagnostic=error.what();
        if(diagnostic.size()>1024)diagnostic.resize(1024);
    }
    bool output_empty() const {
        const auto usage=transport.usage();
        return !usage.active && usage.rendered_frame==usage.submitted_frame &&
               usage.submitted_frame==usage.consumed_frame;
    }
    void pump_transport(){
        if(stopped_pumping || transport.failed())return;
        try{transport.tick();}
        catch(const std::exception& error){++scheduler_errors;note_error(error);stopped_pumping=true;}
    }
};
BhavAudio::BhavAudio(odh::World& world,BhavAudioConfig config)
    :impl_(std::make_unique<Impl>(world,std::move(config))){}
BhavAudio::~BhavAudio()=default;
bool BhavAudio::submit(const odh::SoundRequest& request){
    impl_->last_arguments.reset();
    impl_->last_native_controls.reset();
    try{
        if(impl_->threads.submit(request)){++impl_->submitted;return true;}
        impl_->last_native_controls.reset();
        ++impl_->rejected;return false;
    }catch(const std::exception& error){
        impl_->last_native_controls.reset();
        ++impl_->rejected;++impl_->submit_errors;impl_->note_error(error);return false;
    }
}
std::optional<odh::HitArguments> BhavAudio::last_arguments() const {return impl_->last_arguments;}
std::optional<BhavNativeControlReceipt> BhavAudio::last_native_controls() const {return impl_->last_native_controls;}
void BhavAudio::simulation_tick(){
    ++impl_->scheduler_ticks;
    try{
        const auto report=impl_->threads.tick();
        impl_->completed+=report.completed;
        impl_->owner_invalid+=report.owner_invalid;
        impl_->faulted+=report.faulted;
    }catch(const std::exception& error){++impl_->scheduler_errors;impl_->note_error(error);}
    impl_->pump_transport();
}
void BhavAudio::drain(std::uint16_t milliseconds){
    impl_->drain_requested_ms=milliseconds;
    impl_->drain_output_empty=impl_->output_empty();
    if(!milliseconds)return;
    const auto start=std::chrono::steady_clock::now();
    const auto deadline=start+std::chrono::milliseconds(milliseconds);
    // The wall deadline and a poll cap both bound this host-only phase.
    for(std::uint64_t poll=0;poll<5001;++poll){
        if(impl_->output_empty() || impl_->stopped_pumping || impl_->transport.failed())break;
        if(std::chrono::steady_clock::now()>=deadline)break;
        impl_->pump_transport();++impl_->drain_polls;
        if(!impl_->output_empty())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    impl_->drain_elapsed_ms=static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());
    impl_->drain_output_empty=impl_->output_empty();
    impl_->drain_timed_out=!impl_->drain_output_empty && !impl_->transport.failed() && !impl_->stopped_pumping;
}
bool BhavAudio::unhealthy() const {
    const auto& a=*impl_;
    return a.rejected || a.submit_errors || a.faulted || a.scheduler_errors ||
           a.transport.failed() || a.drain_timed_out;
}
void BhavAudio::emit_json(std::ostream& out,std::uint64_t simulation_ticks) const {
    const auto& a=*impl_;const auto usage=a.transport.usage();
    const char* playback_state=a.transport.failed() || a.scheduler_errors || a.faulted ||
                               a.rejected || a.submit_errors ? "faulted" :
                               a.drain_timed_out ? "drain_timeout" :
                               a.drain_output_empty && a.threads.size() ? "pcm_drained_hit_pending" :
                               a.drain_output_empty ? "complete" : "pending_output";
    out<<"{\"profile\":{\"hsm\":"<<quote(a.config.hsm_path)
       <<",\"hot\":"<<quote(a.config.hot_path)<<",\"hit\":"<<quote(a.config.hit_path)
       <<",\"argument_mode\":"<<quote(a.config.caller_arguments?"play_sound_caller":"explicit_tuple")
       <<",\"args\":";
    if(a.config.caller_arguments)out<<"null";
    else out<<'['<<a.config.arguments[0]<<','<<a.config.arguments[1]<<','<<a.config.arguments[2]<<']';
    if(a.config.native_controls)out<<",\"native_controls\":{\"profile\":\"fresh_argstyle2_partial\",\"extvol\":"
        <<(*a.config.native_controls)[0]<<",\"pan\":"<<(*a.config.native_controls)[1]<<'}';
    out<<",\"jump\":"<<quote(a.config.jump==odh::HitJumpOperand::Literal32?"literal32":"free-so")
       <<",\"chooser\":\"first\",\"gain_q15\":"<<a.config.controls.gain_q15
       <<",\"pan_q15\":"<<a.config.controls.pan_q15<<",\"rate\":"<<a.config.output_rate
       <<",\"sources\":[";
    for(std::size_t i=0;i<a.config.sources.size();++i){
        if(i)out<<',';
        const auto& source=a.config.sources[i];
        out<<"{\"kind\":"<<quote(source.kind==XaSourceKind::far?"far":"xa")
           <<",\"path\":"<<quote(source.path)<<'}';
    }
    out<<"]},\"requests\":{\"submitted\":"<<a.submitted<<",\"rejected\":"<<a.rejected
       <<",\"submit_errors\":"<<a.submit_errors<<"},\"threads\":{\"completed\":"<<a.completed
       <<",\"owner_invalid\":"<<a.owner_invalid<<",\"faulted\":"<<a.faulted
       <<",\"active\":"<<a.threads.size()<<"},\"pcm\":{\"rendered\":"<<usage.rendered_frame
       <<",\"submitted\":"<<usage.submitted_frame<<",\"consumed\":"<<usage.consumed_frame
       <<",\"failed\":"<<(a.transport.failed()?"true":"false")
       <<"},\"playback_state\":"<<quote(playback_state)
       <<",\"simulation_ticks\":"<<simulation_ticks<<",\"scheduler_ticks\":"<<a.scheduler_ticks
       <<",\"scheduler_errors\":"<<a.scheduler_errors
       <<",\"host_drain\":{\"scope\":\"pcm_only\",\"requested\":"<<(a.drain_requested_ms?"true":"false")
       <<",\"requested_ms\":"<<a.drain_requested_ms
       <<",\"elapsed_ms\":"<<a.drain_elapsed_ms<<",\"polls\":"<<a.drain_polls
       <<",\"output_empty\":"<<(a.drain_output_empty?"true":"false")
       <<",\"timed_out\":"<<(a.drain_timed_out?"true":"false")
       <<"},\"diagnostic\":"<<quote(a.diagnostic)<<'}';
}
}
