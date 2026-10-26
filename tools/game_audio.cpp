#include "game_audio.hpp"
#include "xa_sound_factory.hpp"
#include "odh/hit_arguments.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace odh_host {
namespace {
constexpr std::size_t file_limit=131072,group_limit=64,receipt_limit=256;
constexpr std::uint64_t name_limit=4*1024*1024,metadata_limit=32*1024*1024;
std::string fold(std::string value){
    for(auto& c:value)if(c>='A' && c<='Z')c=static_cast<char>(c+('a'-'A'));
    return value;
}
std::string bounded(const std::string& value,std::size_t limit){return value.substr(0,limit);}
void quoted(std::ostream& out,const std::string& value){
    constexpr char hex[]="0123456789abcdef";
    out<<'"';for(const unsigned char c:value){
        if(c=='"'||c=='\\')out<<'\\'<<static_cast<char>(c);
        else if(c<32 || c>=127)out<<"\\u00"<<hex[c>>4]<<hex[c&15];
        else out<<static_cast<char>(c);
    }out<<'"';
}
// Observes only the prefix actually accepted by the borrowed platform sink.
// Counts delivery of nonzero PCM, never a claim that physical audio was heard.
class MeterSink final:public odh::PcmSink {
public:
    explicit MeterSink(odh::PcmSink& sink):sink_(sink){}
    odh::PcmSinkFormat format() const override{return sink_.format();}
    std::size_t queued_frames() const override{return sink_.queued_frames();}
    std::size_t queue(const std::int16_t* samples,std::size_t frames) override {
        const auto accepted=sink_.queue(samples,frames);
        if(accepted>frames)throw odh::FormatError("PCM meter: sink accepted more frames than supplied");
        accepted_frames+=accepted;
        for(std::size_t frame=0;frame<accepted;++frame){
            for(std::size_t channel=0;channel<2;++channel){
                const auto sample=static_cast<std::int32_t>(samples[frame*2+channel]);
                if(sample)++nonzero_samples;
                const auto magnitude=static_cast<std::uint32_t>(sample<0?-sample:sample);
                peak_abs_sample=std::max(peak_abs_sample,magnitude);
            }
        }
        return accepted;
    }
    void pause() override{sink_.pause();}
    void start() override{sink_.start();}
    void clear() override{sink_.clear();}
    std::uint64_t accepted_frames=0,nonzero_samples=0;
    std::uint32_t peak_abs_sample=0;
private:
    odh::PcmSink& sink_;
};
struct GroupPaths {std::vector<std::filesystem::path> hsm,hot,hit;};
struct Group {std::string hsm_path,hot_path,hit_path;odh::Hsm hsm;odh::Hot hot;odh::HitFile hit;};
template<class Reader>auto read(const std::filesystem::path& path,Reader reader){
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw odh::FormatError("cannot open sound metadata: "+path.string());
    return reader(odh::View(stream,std::filesystem::file_size(path)));
}
bool sound_directory(const std::filesystem::path& path){
    for(const auto& part:path.parent_path()){
        const auto name=fold(part.string());if(name=="sounddata" || name=="sound")return true;
    }
    return false;
}
}
struct GameAudio::Impl {
    struct Receipt {
        std::string name,reason;
        std::int16_t source=0;
        std::optional<std::uint64_t> generation;
        bool admitted=false;
        std::optional<std::size_t> group;
        std::optional<std::uint32_t> track,pc;
    };
    odh::World& world;
    std::vector<std::unique_ptr<Group>> groups;
    std::map<std::string,std::vector<std::size_t>> events;
    std::unique_ptr<XaPatchCatalog> catalog;
    std::vector<XaPatchSource> source_paths;
    MeterSink meter;
    odh::PcmTransport transport;
    odh::SoundThreadCollection threads;
    std::vector<Receipt> receipts;
    std::vector<std::string> diagnostics;
    std::uint64_t diagnostic_dropped=0,receipt_dropped=0;
    std::uint64_t scanned=0,metadata_bytes=0,name_bytes=0,skipped_groups=0,xa_sources=0;
    std::uint64_t requests=0,admitted=0,rejected=0,ticks=0,completed=0,invalid=0,faulted=0;
    std::uint64_t stopped=0,clears=0,pump_errors=0,scheduler_errors=0;
    std::uint32_t rng=0x4f444841U;
    Receipt* preparing=nullptr;
    bool pumping_stopped=false;

    Impl(odh::World& value,const std::filesystem::path& root,odh::PcmSink& sink)
      :world(value),meter(sink),transport(meter),threads(world,[this](const auto& request,const auto& binding){
          return make(request,binding);
      }){
        receipts.reserve(receipt_limit);diagnostics.reserve(receipt_limit);
        discover(root);
    }
    void diagnostic(const std::string& message){
        if(diagnostics.size()<receipt_limit)diagnostics.push_back(bounded(message,1024));
        else ++diagnostic_dropped;
    }
    void discover(const std::filesystem::path& root){
        std::map<std::string,GroupPaths> pairs;
        std::vector<XaPatchSource> sources;
        try{
            const auto absolute=std::filesystem::absolute(root);
            if(!std::filesystem::is_directory(absolute))throw odh::FormatError("sound game root is not a directory");
            // Never follow directory symlinks. Explicitly skip file symlinks too,
            // so discovery stays inside the selected copied game-data tree.
            for(const auto& entry:std::filesystem::recursive_directory_iterator(absolute)){
                if(++scanned>file_limit)throw odh::FormatError("sound discovery file/entry budget exceeded");
                if(entry.is_symlink() || !entry.is_regular_file())continue;
                const auto path=entry.path();const auto text=path.string();
                if(text.size()>4096 || text.size()>name_limit-name_bytes)
                    throw odh::FormatError("sound discovery filename budget exceeded");
                name_bytes+=text.size();
                const auto extension=fold(path.extension().string());
                if(extension==".hsm" || extension==".hot" || extension==".hit"){
                    const auto key=path.parent_path().string()+"/"+fold(path.stem().string());
                    auto& pair=pairs[key];
                    if(extension==".hsm")pair.hsm.push_back(path);
                    else if(extension==".hot")pair.hot.push_back(path);
                    else pair.hit.push_back(path);
                    if(pairs.size()>group_limit)throw odh::FormatError("sound metadata group discovery budget exceeded");
                }
                if(extension==".xa" || (extension==".far" && sound_directory(path))){
                    if(sources.size()>=8192)throw odh::FormatError("sound XA source budget exceeded");
                    sources.push_back({text,extension==".far"?XaSourceKind::far:XaSourceKind::loose});
                }
            }
        }catch(const std::exception& error){
            diagnostic(std::string("sound discovery refused: ")+error.what());
            // An incomplete scan cannot establish unique metadata or XA names.
            return;
        }
        for(const auto& item:pairs){
            const auto& paths=item.second;
            if(paths.hot.empty())continue;
            if(paths.hot.size()!=1 || paths.hsm.size()!=1 || paths.hit.size()!=1){
                ++skipped_groups;diagnostic("skipped sound group with missing/ambiguous companions: "+item.first);continue;
            }
            try{
                for(const auto& path:{paths.hsm.front(),paths.hot.front(),paths.hit.front()}){
                    const auto bytes=std::filesystem::file_size(path);
                    if(bytes>metadata_limit-metadata_bytes)throw odh::FormatError("aggregate sound metadata budget exceeded");
                    metadata_bytes+=bytes;
                }
                auto group=std::make_unique<Group>();
                group->hsm_path=paths.hsm.front().string();group->hot_path=paths.hot.front().string();group->hit_path=paths.hit.front().string();
                group->hsm=read(paths.hsm.front(),[](const auto& view){return odh::read_hsm(view);});
                group->hot=read(paths.hot.front(),[&](const auto& view){return odh::read_hot(view,&group->hsm);});
                group->hit=read(paths.hit.front(),[](const auto& view){return odh::read_hit(view);});
                groups.push_back(std::move(group));
            }catch(const std::exception& error){++skipped_groups;diagnostic("skipped sound group "+item.first+": "+error.what());}
        }
        if(groups.empty())diagnostic("no complete supported HSM/HOT/HIT sound group loaded");
        // Publish the complete event index only after all group parsing succeeds.
        // Allocation failure escapes startup rather than leaving dangling indices.
        std::map<std::string,std::vector<std::size_t>> event_index;
        std::uint64_t event_rows=0,event_bytes=0;
        bool event_budget_failed=false;
        for(std::size_t index=0;index<groups.size() && !event_budget_failed;++index){
            for(const auto& event:groups[index]->hot.events){
                if(++event_rows>65536 || event.first.size()>2*1024*1024-event_bytes){
                    event_budget_failed=true;break;
                }
                event_bytes+=event.first.size();
                if(event.first.size()>256){diagnostic("sound event name exceeds runtime budget in "+groups[index]->hot_path);continue;}
                event_index[fold(event.first)].push_back(index);
            }
        }
        if(event_budget_failed)diagnostic("sound event index refused: aggregate 65536 rows/2 MiB name budget exceeded");
        else events.swap(event_index);
        std::sort(sources.begin(),sources.end(),[](const auto& a,const auto& b){return a.path<b.path;});
        xa_sources=sources.size();source_paths=sources;
        if(sources.empty()){diagnostic("no Sound/SoundData FAR or loose XA sources discovered");return;}
        try{
            XaPatchCatalogLimits limits;limits.sources=8192;limits.name_bytes=name_limit;
            catalog=std::make_unique<XaPatchCatalog>(sources,limits);
        }catch(const std::exception& error){diagnostic(std::string("sound XA catalog unavailable: ")+error.what());}
    }
    std::unique_ptr<odh::SoundThreadInstance> make(const odh::SoundRequest& request,const odh::SoundSourceBinding& binding){
        const auto found=events.find(fold(request.name));
        if(found==events.end())throw odh::FormatError("sound event absent from discovered HOT groups");
        if(found->second.size()!=1)throw odh::FormatError("sound event ambiguous across HOT groups");
        const auto index=found->second.front();const auto& group=*groups[index];
        if(preparing)preparing->group=index;
        const auto resolved=odh::resolve_hit_event(group.hit,group.hot,request);
        if(preparing){preparing->track=resolved.track_id;preparing->pc=resolved.start_pc;}
        if(!catalog)throw odh::FormatError("sound XA catalog unavailable");
        const auto arguments=odh::play_sound_arguments(group.hot.events.at(fold(request.name)),request.source);
        odh::HitChooser chooser=[this](std::uint32_t,const std::vector<std::uint32_t>& choices){
            if(choices.empty())throw odh::FormatError("sound chooser received empty list");
            // Separate deterministic stream: never consumes gameplay random state.
            rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
            return static_cast<std::size_t>(rng)%choices.size();
        };
        return make_xa_hit_sound(group.hit,group.hot,request,binding,transport,*catalog,
            std::move(chooser),{}, {},odh::HitJumpOperand::Literal32,arguments,{}, {}, {},
            odh::PcmSequenceGroupProfile::simitone_instance);
    }
};
GameAudio::GameAudio(odh::World& world,const std::filesystem::path& root,odh::PcmSink& sink)
    :impl_(std::make_unique<Impl>(world,root,sink)){}
GameAudio::~GameAudio()=default;
bool GameAudio::submit(const odh::SoundRequest& request){
    auto& a=*impl_;++a.requests;
    Impl::Receipt receipt;receipt.name=bounded(request.name,256);receipt.source=request.source;
    if(request.source_variables)receipt.generation=request.source_variables->generation;
    try{
        if(request.name.empty() || request.name.size()>256 || request.name.find('\0')!=std::string::npos)
            throw odh::FormatError("invalid or over-budget sound event name");
        if(request.loop)throw odh::FormatError("loop sound outside single-owner nonlooping profile");
        a.preparing=&receipt;
        receipt.admitted=a.threads.submit(request);
        a.preparing=nullptr;
        receipt.reason=receipt.admitted?"admitted":"source binding unavailable or thread capacity exceeded";
    }catch(const std::exception& error){a.preparing=nullptr;receipt.reason=bounded(error.what(),1024);}
    const bool result=receipt.admitted;
    if(result)++a.admitted;else ++a.rejected;
    if(a.receipts.size()<receipt_limit)a.receipts.push_back(std::move(receipt));else ++a.receipt_dropped;
    return result;
}
void GameAudio::simulation_tick(){
    auto& a=*impl_;++a.ticks;
    try{
        const auto report=a.threads.tick();a.completed+=report.completed;a.invalid+=report.owner_invalid;a.faulted+=report.faulted;
        for(const auto& failure:report.failures)
            a.diagnostic("HIT source="+std::to_string(failure.source)+" generation="+std::to_string(failure.generation)+": "+failure.diagnostic);
    }catch(const std::exception& error){++a.scheduler_errors;a.diagnostic(std::string("sound scheduler: ")+error.what());}
    pump();
}
void GameAudio::pump(){
    auto& a=*impl_;if(a.pumping_stopped || a.transport.failed())return;
    try{a.transport.tick();}catch(const std::exception& error){++a.pump_errors;a.pumping_stopped=true;a.diagnostic(std::string("PCM pump: ")+error.what());}
}
void GameAudio::stop_owner(std::int16_t source,std::uint64_t generation){
    auto& a=*impl_;
    try{a.stopped+=a.threads.stop_owner(source,generation);}
    catch(const std::exception& error){++a.scheduler_errors;a.diagnostic(std::string("sound stop owner: ")+error.what());}
}
void GameAudio::clear(){
    auto& a=*impl_;
    try{a.threads.clear();++a.clears;}
    catch(const std::exception& error){++a.scheduler_errors;a.diagnostic(std::string("sound clear: ")+error.what());}
}
bool GameAudio::output_empty() const {
    const auto usage=impl_->transport.usage();
    return !usage.active && usage.rendered_frame==usage.submitted_frame && usage.submitted_frame==usage.consumed_frame;
}
void GameAudio::emit_json(std::ostream& out) const {
    const auto& a=*impl_;const auto pcm=a.transport.usage();
    out<<"{\"profile\":\"single_owner_nonlooping_HIT_XA\",\"controls\":\"centered_unity_Q15_no_native_unit_conversion\","
       <<"\"sharing\":\"no_global_track_or_patch_sharing\",\"HIT\":\"bounded_selection_WaitSamp_End_no_arbitrary_bytecode\","
       <<"\"sequence_groups\":\"simitone_instance0_stop_own_notes_others_noop\",\"jump\":\"literal32\",\"chooser\":\"separate_deterministic_xorshift32\",\"audio_snapshot\":false,"
       <<"\"limits\":{\"threads\":64,\"notes_per_thread\":64,\"pcm_bytes\":4194304,\"receipts\":256,\"event_rows\":65536,\"event_name_bytes\":2097152},"
       <<"\"discovery\":{\"scanned\":"<<a.scanned<<",\"name_bytes\":"<<a.name_bytes<<",\"metadata_bytes\":"<<a.metadata_bytes
       <<",\"skipped_groups\":"<<a.skipped_groups<<",\"xa_sources\":"<<a.xa_sources<<",\"groups\":[";
    for(std::size_t i=0;i<a.groups.size();++i){if(i)out<<',';const auto& g=*a.groups[i];out<<"{\"hsm\":";quoted(out,g.hsm_path);
        out<<",\"hot\":";quoted(out,g.hot_path);out<<",\"hit\":";quoted(out,g.hit_path);out<<'}';}
    out<<"],\"sources\":[";
    for(std::size_t i=0;i<a.source_paths.size();++i){if(i)out<<',';out<<"{\"kind\":";
        quoted(out,a.source_paths[i].kind==XaSourceKind::far?"far":"xa");out<<",\"path\":";quoted(out,a.source_paths[i].path);out<<'}';}
    out<<"]},\"requests\":{\"total\":"<<a.requests<<",\"admitted\":"<<a.admitted<<",\"rejected\":"<<a.rejected
       <<",\"receipts_dropped\":"<<a.receipt_dropped<<"},\"threads\":{\"active\":"<<a.threads.size()<<",\"completed\":"<<a.completed
       <<",\"owner_invalid\":"<<a.invalid<<",\"faulted\":"<<a.faulted<<",\"stopped\":"<<a.stopped<<",\"clears\":"<<a.clears
       <<",\"ticks\":"<<a.ticks<<",\"scheduler_errors\":"<<a.scheduler_errors<<"},\"pcm\":{\"bytes\":"<<pcm.pcm_bytes
       <<",\"handles\":"<<pcm.handles<<",\"active\":"<<pcm.active<<",\"rendered\":"<<pcm.rendered_frame<<",\"submitted\":"<<pcm.submitted_frame
       <<",\"consumed\":"<<pcm.consumed_frame<<",\"accepted_frames\":"<<a.meter.accepted_frames
       <<",\"nonzero_samples\":"<<a.meter.nonzero_samples<<",\"peak_abs_sample\":"<<a.meter.peak_abs_sample
       <<",\"audibility_verified\":false,\"failed\":"<<(a.transport.failed()?"true":"false")<<",\"pump_errors\":"<<a.pump_errors
       <<",\"output_empty\":"<<(output_empty()?"true":"false")<<"},\"receipts\":[";
    for(std::size_t i=0;i<a.receipts.size();++i){if(i)out<<',';const auto& r=a.receipts[i];out<<"{\"name\":";quoted(out,r.name);
        out<<",\"source\":"<<r.source<<",\"generation\":";if(r.generation)out<<*r.generation;else out<<"null";
        out<<",\"admitted\":"<<(r.admitted?"true":"false")<<",\"reason\":";quoted(out,r.reason);
        out<<",\"group\":";if(r.group)out<<*r.group;else out<<"null";
        out<<",\"track\":";if(r.track)out<<*r.track;else out<<"null";
        out<<",\"pc\":";if(r.pc)out<<*r.pc;else out<<"null";out<<'}';}
    out<<"],\"diagnostics_dropped\":"<<a.diagnostic_dropped<<",\"diagnostics\":[";
    for(std::size_t i=0;i<a.diagnostics.size();++i){if(i)out<<',';quoted(out,a.diagnostics[i]);}out<<"]}";
}
}
