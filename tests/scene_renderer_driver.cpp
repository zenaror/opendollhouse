#include "scene_renderer.hpp"
#include "probe_view.hpp"
#ifdef ODH_HAVE_X11
#include <X11/Xlib.h>
#include <X11/keysym.h>
#endif
#include "resource.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
void save(const odh::RasterFrame& image,const std::string& prefix){
    std::ofstream ppm(prefix+".ppm",std::ios::binary),depth(prefix+".depth",std::ios::binary);
    ppm<<"P6\n"<<image.width<<' '<<image.height<<"\n255\n";
    for(const auto& p:image.pixels)for(unsigned c=0;c<3;++c)ppm.put(static_cast<char>(p[c]));
    for(float z:image.depth){std::uint32_t b;std::memcpy(&b,&z,4);for(unsigned c=0;c<4;++c)depth.put(static_cast<char>((b>>(8*c))&255));}
    ppm.close();depth.close();if(!ppm||!depth)throw std::runtime_error("fixture output failed");
}
int main(int argc,char**argv){try{
    if(argc!=5&&argc!=6)throw std::runtime_error("driver expects animation/textures/objects/prefix");
    SceneConfig config{argv[1],"rig.bcf","rig",argv[2],argv[3],"object.iff",100,{{"app.bcf","appearance","mesh.bmf","red.bmp"}}};
    SceneRenderer renderer(config);odh::AnimationCollection clips;with_far_entry(argv[1],"anim.bcf",[&](const auto& v){clips=odh::read_bcf_animations(v);});
    SceneState state{{.5f,.5f,0},{0,0,0},0,0,&clips.animations.at(0),0};odh::WorldProjection camera;camera.width=160;camera.height=160;camera.center_tile={.5f,.5f,0};
    auto first=renderer.render(state,camera);state.frame=.5;auto second=renderer.render(state,camera);
    if(first.image.pixels==second.image.pixels)throw std::runtime_error("animated frame did not change mesh pixels");
    auto repeat=renderer.render(state,camera);if(repeat.image.pixels!=second.image.pixels||repeat.image.depth!=second.image.depth)throw std::runtime_error("cached render changed output");
    if(renderer.cache_clips()!=1||renderer.cache_samples()!=14)throw std::runtime_error("CFP cache duplicated samples");
    auto bad=clips.animations[0];++bad.translations;state.animation=&bad;bool rejected=false;try{renderer.render(state,camera);}catch(const odh::FormatError&){rejected=true;}if(!rejected||renderer.cache_samples()!=14)throw std::runtime_error("mismatched cached header accepted or cache mutated");
    state.animation=&clips.animations[0];state.frame=std::numeric_limits<double>::quiet_NaN();rejected=false;try{renderer.render(state,camera);}catch(const odh::FormatError&){rejected=true;}if(!rejected||renderer.cache_samples()!=14)throw std::runtime_error("nonfinite frame accepted");
    save(second.image,argv[4]);SceneRenderer moved(std::move(renderer));state.frame=.5;rejected=false;try{renderer.render(state,camera);}catch(const odh::FormatError&){rejected=true;}if(!rejected)throw std::runtime_error("moved renderer accepted");
    if(moved.render(state,camera).image.pixels!=second.image.pixels)throw std::runtime_error("moved assets changed output");
    odh::World world;world.create(1,odh::EntityKind::avatar,{0,88,1});world.create(2,odh::EntityKind::object,{0,0,1});world.set_position(1,{8,8,1,0});world.set_position(2,{8,8,1,0});world.declare_object_word(1,17,0);
    const odh::Bhav routine{0x8002,{{44,254,255,{1,0,0,0,0,1,0,0}}},{},odh::Bhav::Metadata{1,2,0,0}};
    auto clip=std::make_shared<const odh::AnimationHeader>(clips.animations[0]);odh::Playback player(world,[&](const auto&,auto,auto){return clip;});
    odh::Vm vm({{odh::routine_key(4096),routine}},world.vm_memory(),{},player.primitive());player.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});vm.run(20);
    ProbeViewConfig view_config;view_config.scene=config;view_config.frames_directory=std::string(argv[4])+"-frames";view_config.stride=1;
    const odh::SlotTable slots{0,10,{},0};
    if(argc==6){
#ifdef ODH_HAVE_X11
        view_config.window=true;ProbeView controls(view_config);controls.draw(world,player,slots,0,0,true);
        std::unique_ptr<Display,decltype(&XCloseDisplay)> sender(XOpenDisplay(nullptr),&XCloseDisplay);if(!sender)throw std::runtime_error("control test display unavailable");
        unsigned long event_time=1;auto send=[&](KeySym sym,int type){XEvent e{};e.xkey.type=type;e.xkey.time=event_time++;e.xkey.display=sender.get();e.xkey.window=controls.native_id();e.xkey.root=DefaultRootWindow(sender.get());e.xkey.same_screen=True;e.xkey.keycode=XKeysymToKeycode(sender.get(),sym);XSendEvent(sender.get(),controls.native_id(),False,type==KeyPress?KeyPressMask:KeyReleaseMask,&e);XSync(sender.get(),False);};
        send(XK_p,KeyPress);controls.ready(world,player,slots,0,0);send(XK_p,KeyRelease);send(XK_space,KeyPress);
        if(controls.ready(world,player,slots,0,0)||controls.action())throw std::runtime_error("paused action became consumable");
        send(XK_p,KeyPress);if(!controls.ready(world,player,slots,0,0)||!controls.action())throw std::runtime_error("unpause did not release pending action");
        controls.clear_action();send(XK_space,KeyRelease);send(XK_space,KeyPress);controls.ready(world,player,slots,0,0);if(!controls.action())throw std::runtime_error("second released Space did not request action");
        if(vm.steps()!=1||vm.status()!=odh::VmStatus::returned_true)throw std::runtime_error("view controller mutated VM");
        send(XK_Escape,KeyPress);controls.ready(world,player,slots,0,0);if(!controls.quit())throw std::runtime_error("view close not propagated");
#else
        throw std::runtime_error("window controls backend unavailable");
#endif
    }
    ProbeView observed(view_config);observed.draw(world,player,slots,0,0,true);if(observed.failed())throw std::runtime_error(observed.diagnostic());
    auto missing=std::make_shared<odh::AnimationHeader>(clips.animations[0]);missing->xskill="missing";
    odh::Playback broken(world,[&](const auto&,auto,auto){return missing;});odh::Vm broken_vm({{odh::routine_key(4096),routine}},world.vm_memory(),{},broken.primitive());broken.bind(broken_vm);broken_vm.start(odh::routine_key(4096),{1,2,2});broken_vm.run(20);
    ProbeView failed(view_config);const auto position=*world.find(1)->position;failed.draw(world,broken,slots,0,0,true);
    if(!failed.failed()||failed.ready(world,broken,slots,0,0)||!(*world.find(1)->position==position))throw std::runtime_error("view failure mutated world or escaped fault state");
    std::ofstream fault(std::string(argv[4])+"-fault.json");fault<<failed.report();fault.close();
    std::cout<<"{\"cache_clips\":1,\"cache_samples\":14,\"vertices\":"<<moved.vertices()<<",\"triangles\":"<<moved.triangles()<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
