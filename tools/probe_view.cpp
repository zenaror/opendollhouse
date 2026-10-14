#include "probe_view.hpp"
#include "json.hpp"
#ifdef ODH_HAVE_X11
#include "linux_window.hpp"
#endif
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

struct ProbeView::Impl {
    ProbeViewConfig config;
    SceneRenderer renderer;
    odh::WorldProjection camera;
    odh::RasterOptions options;
    std::vector<std::string> records;
    bool stopped=false,requested=false,paused=false;
    std::string error;
    std::uint64_t readbacks=0,last_tick=0;
    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now();
#ifdef ODH_HAVE_X11
    std::unique_ptr<odh_host::LinuxWindow> window;
#endif
    explicit Impl(ProbeViewConfig c):config(std::move(c)),renderer(config.scene){
        if(!config.window)config.automatic=true;
        if(!config.stride||config.stride>4096)throw odh::FormatError("view stride outside1..4096");
        if(config.frames_directory.empty()&&!config.window)throw odh::FormatError("view requires frame directory or window");
        if(!config.frames_directory.empty())std::filesystem::create_directories(config.frames_directory);
        options.lighting=true;camera.center_tile={4,4,0};
#ifdef ODH_HAVE_X11
        if(config.window)window=std::make_unique<odh_host::LinuxWindow>(camera.width,camera.height,"OpenDollhouse: Space sit/stand; P pause; arrows camera; Escape exit");
#else
        if(config.window)throw odh::FormatError("X11 view backend not built; configure ODH_X11 and X11 development files");
#endif
    }
    void draw(const odh::World& world,const odh::Playback& playback,const odh::SlotTable& slots,std::uint64_t tick,std::size_t phase,bool force){
        last_tick=tick;
        if(stopped||!error.empty()||(!force&&tick%config.stride!=0))return;
        if(records.size()>=8192)throw odh::FormatError("view frame observation budget exceeded");
        const auto resolver=[&](const odh::Entity& parent)->const odh::SlotTable*{return parent.id==2?&slots:nullptr;};
        const auto avatar=odh::graphics_entity(world,1,resolver),object=odh::graphics_entity(world,2,resolver);
        const auto sample=playback.sample();
        if(sample&&(sample->actor!=avatar.id||sample->actor_generation!=avatar.generation))throw odh::FormatError("view playback identity does not match avatar");
        SceneState state{avatar.tile_position,object.tile_position,avatar.direction,object.direction,
                         sample?sample->clip.get():nullptr,sample?sample->frame:0};
        state.hands=playback.hands().value_or(odh::AvatarHands{});
        auto rendered=renderer.render(state,camera,options);
        const auto* actor=world.find(1);const auto* obj=world.find(2);
        if(!actor||!obj||!actor->position||!obj->position||actor->person_data.empty())throw odh::FormatError("view world state missing");
        std::string path;
        if(!config.frames_directory.empty()){
            std::ostringstream name;name<<"frame-"<<std::setw(4)<<std::setfill('0')<<records.size()<<"-tick-"<<std::setw(4)<<tick<<".ppm";
            path=(std::filesystem::path(config.frames_directory)/name.str()).string();
            std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out)throw odh::FormatError("cannot open view frame output");
            out<<"P6\n"<<rendered.image.width<<' '<<rendered.image.height<<"\n255\n";
            for(const auto& p:rendered.image.pixels){const char rgb[]={static_cast<char>(p[0]),static_cast<char>(p[1]),static_cast<char>(p[2])};out.write(rgb,3);}out.close();if(!out)throw odh::FormatError("cannot write view frame output");
        }
#ifdef ODH_HAVE_X11
        if(window){
            window->set_title("OpenDollhouse tick "+std::to_string(tick)+" posture "+std::to_string(actor->person_data[0])+" | Space sit/stand, P pause, arrows camera, Esc exit");
            window->present(rendered.image);
            if(config.readback){if(window->readback()!=rendered.image.pixels)throw odh::FormatError("own X11 window readback differs from framebuffer");++readbacks;}
        }
#endif
        std::ostringstream out;out<<std::setprecision(9)<<"{\"tick\":"<<tick<<",\"phase\":"<<phase<<",\"actor_generation\":"<<avatar.generation<<",\"object_generation\":"<<object.generation<<",\"avatar_raw\":["<<actor->position->x<<','<<actor->position->y<<','<<actor->position->level<<','<<unsigned(avatar.direction)<<"],\"object_raw\":["<<obj->position->x<<','<<obj->position->y<<','<<obj->position->level<<','<<unsigned(object.direction)<<"],\"avatar_tile\":["<<avatar.tile_position[0]<<','<<avatar.tile_position[1]<<','<<avatar.tile_position[2]<<"],\"object_tile\":["<<object.tile_position[0]<<','<<object.tile_position[1]<<','<<object.tile_position[2]<<"],\"container\":"<<avatar.container<<",\"posture\":"<<actor->person_data[0]<<",\"comfort\":";
        if(actor->motives.size()>6)out<<actor->motives[6];else out<<"null";
        out<<",\"rotation\":"<<unsigned(camera.rotation)<<",\"zoom\":"<<unsigned(camera.zoom)<<",\"animation\":";
        if(sample)out<<"{\"name\":"<<json_bytes(sample->clip->name)<<",\"xskill\":"<<json_bytes(sample->clip->xskill)<<",\"vm_generation\":"<<sample->vm_generation<<",\"frame\":"<<sample->frame<<",\"frames\":"<<sample->frames<<",\"loop\":"<<(sample->loop?"true":"false")<<",\"end\":"<<(sample->end?"true":"false")<<'}';else out<<"null";
        out<<",\"cfp_cache_clips\":"<<renderer.cache_clips()<<",\"cfp_cache_samples\":"<<renderer.cache_samples()<<",\"sprite_shaded\":"<<rendered.shaded_samples<<",\"sprite_occluded\":"<<rendered.occluded_samples<<",\"output\":";
        if(path.empty())out<<"null";else out<<json_bytes(path);out<<'}';records.push_back(out.str());
    }
};
ProbeView::ProbeView(ProbeViewConfig c):impl_(std::make_unique<Impl>(std::move(c))){}
ProbeView::~ProbeView()=default;
void ProbeView::draw(const odh::World& w,const odh::Playback& p,const odh::SlotTable& s,std::uint64_t t,std::size_t phase,bool force){try{impl_->draw(w,p,s,t,phase,force);}catch(const std::exception& e){impl_->error=e.what();}}
bool ProbeView::ready(const odh::World& w,const odh::Playback& p,const odh::SlotTable& s,std::uint64_t t,std::size_t phase){
    auto& i=*impl_;if(!i.error.empty())return false;try{
#ifdef ODH_HAVE_X11
    if(i.window){const auto input=i.window->poll();i.stopped=i.stopped||input.quit;i.requested=i.requested||input.action;if(input.pause)i.paused=!i.paused;
        i.camera.rotation=static_cast<std::uint8_t>((int(i.camera.rotation)+input.rotation%4+4)%4);
        const auto zoom=int(i.camera.zoom)+input.zoom;i.camera.zoom=static_cast<std::uint8_t>(zoom<1?1:zoom>3?3:zoom);
        if(input.rotation||input.zoom||input.redraw)i.draw(w,p,s,t,phase,true);
        if(i.stopped)return false;
        if(i.paused){std::this_thread::sleep_for(std::chrono::milliseconds(10));i.deadline=std::chrono::steady_clock::now();return false;}
        // Fixed preview clock25Hz; late rendering delays wall time rather than
        // skipping or merging simulation ticks. Original timing not established.
        i.deadline+=std::chrono::milliseconds(40);std::this_thread::sleep_until(i.deadline);
    }
#endif
    return !i.stopped;
    }catch(const std::exception& e){i.error=e.what();return false;}
}
bool ProbeView::failed()const{return !impl_->error.empty();}
const std::string& ProbeView::diagnostic()const{return impl_->error;}
unsigned long ProbeView::native_id()const{
#ifdef ODH_HAVE_X11
    return impl_->window?impl_->window->native_id():0;
#else
    return 0;
#endif
}
bool ProbeView::quit()const{return impl_->stopped;}
bool ProbeView::action()const{return impl_->requested&&!impl_->paused;}
void ProbeView::clear_action(){impl_->requested=false;}
bool ProbeView::manual()const{return impl_->config.window&&!impl_->config.automatic;}
std::string ProbeView::report()const{
    const auto& i=*impl_;std::ostringstream o;o<<"{\"world_connected\":true,\"playback_connected\":true,\"original_calibrated\":false,\"fault\":"<<(!i.error.empty()?"true":"false")<<",\"diagnostic\":"<<json_bytes(i.error)<<",\"window\":"<<(i.config.window?"true":"false")<<",\"automatic\":"<<(i.config.automatic?"true":"false")<<",\"quit\":"<<(i.stopped?"true":"false")<<",\"clock_profile\":"<<json_bytes(i.config.window?"preview25Hz_no_tick_drop":"headless_no_wall_clock_pacing")<<",\"tick_limit\":4096,\"tick_limit_reached\":"<<(i.last_tick>=4096?"true":"false")<<",\"stride\":"<<i.config.stride<<",\"readbacks_equal\":"<<i.readbacks<<",\"vertices\":"<<i.renderer.vertices()<<",\"triangles\":"<<i.renderer.triangles()<<",\"texture_bytes\":"<<i.renderer.texture_bytes()<<",\"sprite_cache_bytes\":"<<i.renderer.sprite_bytes()<<",\"cfp_cache_clips\":"<<i.renderer.cache_clips()<<",\"cfp_cache_samples\":"<<i.renderer.cache_samples()<<",\"frames\":[";
    for(std::size_t n=0;n<i.records.size();++n){if(n)o<<',';o<<i.records[n];}o<<"]}";return o.str();
}
