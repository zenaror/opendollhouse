#include "scene_renderer.hpp"
#include "probe_view.hpp"
#ifdef ODH_HAVE_X11
#include <X11/Xlib.h>
#include <X11/keysym.h>
#endif
#include "resource.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>
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
void anchor_checks(const SceneConfig& base,const odh::AnimationHeader& ordinary,
                   const odh::WorldProjection& camera,const std::string& prefix){
    const auto number=[](std::uint32_t n){std::string out(4,'\0');for(unsigned i=0;i<4;++i)out[i]=static_cast<char>((n>>(i*8))&255);return out;};
    const auto real=[&](float value){std::uint32_t n;std::memcpy(&n,&value,4);return number(n);};
    const auto text=[](const std::string& value){return std::string(1,static_cast<char>(value.size()))+value;};
    std::vector<std::pair<std::string,std::string>> entries;
    for(const auto* name:{"rig.bcf","app.bcf","mesh.bmf","anim.bcf","POSE.cfp"}){
        std::string bytes;with_far_entry(base.animation_far,name,[&](const auto& view){
            bytes.resize(static_cast<std::size_t>(view.size()));view.read(0,bytes.data(),bytes.size());});
        entries.emplace_back(name,std::move(bytes));
    }
    auto& rig=entries[0].second;
    if(rig.size()<18||rig[8]!=1||rig[9]!=0)throw std::runtime_error("anchor test requires its synthetic one-bone rig");
    rig.resize(rig.size()-8);rig[8]=4;
    for(const auto* name:{"R_FINGER0","HEAD","PELVIS"}){
        rig+=text(name)+text("ROOT")+number(0)+real(-3)+real(6)+real(9)+
            real(0)+real(0)+real(0)+real(1)+number(1)+number(1)+number(0)+real(0)+real(0);
    }
    rig+=number(0)+number(0);
    std::string far="FAR!byAZ"+number(1)+number(0),index=number(static_cast<std::uint32_t>(entries.size()));
    for(const auto& [name,bytes]:entries){
        index+=number(static_cast<std::uint32_t>(bytes.size()))+number(static_cast<std::uint32_t>(bytes.size()))+
            number(static_cast<std::uint32_t>(far.size()))+number(static_cast<std::uint32_t>(name.size()))+name;
        far+=bytes;
    }
    const auto index_offset=number(static_cast<std::uint32_t>(far.size()));far.replace(12,4,index_offset);far+=index;
    const auto resource=prefix+"-anchors.far";{std::ofstream output(resource,std::ios::binary);output<<far;if(!output)throw std::runtime_error("anchor fixture write failed");}
    auto config=base;config.animation_far=resource;config.object_far.clear();config.object_entry.clear();config.object_id=0;
    odh::CfpPlanes planes;with_far_entry(base.animation_far,"POSE.cfp",[&](const auto& v){planes=odh::read_cfp(v,ordinary.translations,ordinary.rotations);});
    auto carry=ordinary;carry.name="carry";carry.motions[0].bone="R_FINGER0";
    auto shared=std::make_shared<const odh::CfpPlanes>(planes);unsigned resolved=0;
    SceneRenderer renderer(config,[&](const auto& selected){
        if(&selected!=&ordinary && &selected!=&carry)throw std::runtime_error("resolver lost selected header identity");
        ++resolved;return shared;
    });
    SceneState state;state.animation=&ordinary;state.frame=.5;state.avatar_tile={10,20,2};
    ScenePoseAnchors plain,held;
    renderer.render(state,camera,{},&plain);state.carry_animation=&carry;
    renderer.render(state,camera,{},&held);
    if(resolved!=3||renderer.cache_samples()!=0||renderer.cache_clips()!=0)throw std::runtime_error("secondary pose duplicated provider-owned CFP cache");
    for(unsigned slot=0;slot<3;++slot)if(!plain.avatar_slots[slot]||!held.avatar_slots[slot])throw std::runtime_error("final pose attachment bone absent");
    if(plain.avatar_slots[1]!=held.avatar_slots[1]||plain.avatar_slots[2]!=held.avatar_slots[2]||plain.avatar_slots[0]==held.avatar_slots[0])
        throw std::runtime_error("carry overlay changed unrelated attachment bones or missed right hand");
    // Ordinary ROOT x=-.5. Carry frame0 sets local right-hand x/y/z=0,
    // so facing north rotates ROOT's x to +.5. HEAD retains local(-x,y,z)=(3,6,9).
    const std::array<float,3> expected_hand{10-.5f+.5f/3,20-.5f,2};
    const std::array<float,3> expected_head{10-.5f-2.5f/3,20-.5f-3,4};
    for(unsigned axis=0;axis<3;++axis)
        if(std::abs((*held.avatar_slots[0])[axis]-expected_hand[axis])>.00002f||
           std::abs((*held.avatar_slots[1])[axis]-expected_head[axis])>.00002f)
            throw std::runtime_error("attachment export differs from same final render pose");
    auto invalid=state;invalid.frame=std::numeric_limits<double>::quiet_NaN();bool rejected=false;
    const auto original=held.avatar_slots;
    try{renderer.render(invalid,camera,{},&held);}catch(const odh::FormatError&){rejected=true;}
    if(!rejected||held.avatar_slots!=original)throw std::runtime_error("failed render published partial anchors");
    // Legacy cache also shares ordinary/carry resources rather than storing a duplicate.
    SceneRenderer cached(config);cached.render(state,camera,{},&held);
    if(cached.cache_clips()!=1||cached.cache_samples()!=14)throw std::runtime_error("carry reused CFP duplicated legacy cache");
    std::filesystem::remove(resource);
}
int main(int argc,char**argv){try{
    if(argc!=5&&argc!=6)throw std::runtime_error("driver expects animation/textures/objects/prefix");
    SceneConfig config{argv[1],"rig.bcf","rig",argv[2],argv[3],"object.iff",100,{{"app.bcf","appearance","mesh.bmf","red.bmp"}}};
    SceneRenderer renderer(config);odh::AnimationCollection clips;with_far_entry(argv[1],"anim.bcf",[&](const auto& v){clips=odh::read_bcf_animations(v);});
    SceneState state{{.5f,.5f,0},{0,0,0},0,0,&clips.animations.at(0),0};odh::WorldProjection camera;camera.width=160;camera.height=160;camera.center_tile={.5f,.5f,0};
    auto first=renderer.render(state,camera);state.frame=.5;auto second=renderer.render(state,camera);
    if(first.image.pixels==second.image.pixels)throw std::runtime_error("animated frame did not change mesh pixels");
    auto repeat=renderer.render(state,camera);if(repeat.image.pixels!=second.image.pixels||repeat.image.depth!=second.image.depth)throw std::runtime_error("cached render changed output");
    auto hidden=state;hidden.avatar_visible=false;ScenePoseAnchors hidden_anchors;
    odh::RasterOptions background;background.clear={17,29,43,255};
    const auto object_only=renderer.render(hidden,camera,background,&hidden_anchors);
    if(object_only.image.width!=camera.width||object_only.image.height!=camera.height||object_only.image.shaded_samples||
       !object_only.shaded_samples||std::any_of(hidden_anchors.avatar_slots.begin(),hidden_anchors.avatar_slots.end(),[](const auto& slot){return slot.has_value();})||
       !std::any_of(object_only.image.pixels.begin(),object_only.image.pixels.end(),[&](const auto& pixel){return pixel!=background.clear;}))
        throw std::runtime_error("off-lot avatar hid the retained scene object or emitted avatar fragments/anchors");
    auto shifted_camera=camera;shifted_camera.center_tile[0]+=.25f;
    if(renderer.render(hidden,shifted_camera,background).image.pixels==object_only.image.pixels)
        throw std::runtime_error("off-lot avatar changed scene camera projection");
    if(renderer.render(state,camera).image.pixels!=second.image.pixels)
        throw std::runtime_error("off-lot render changed subsequent visible avatar output");

    if(renderer.cache_clips()!=1||renderer.cache_samples()!=14)throw std::runtime_error("CFP cache duplicated samples");
    auto bad=clips.animations[0];++bad.translations;state.animation=&bad;bool rejected=false;try{renderer.render(state,camera);}catch(const odh::FormatError&){rejected=true;}if(!rejected||renderer.cache_samples()!=14)throw std::runtime_error("mismatched cached header accepted or cache mutated");
    state.animation=&clips.animations[0];state.frame=std::numeric_limits<double>::quiet_NaN();rejected=false;try{renderer.render(state,camera);}catch(const odh::FormatError&){rejected=true;}if(!rejected||renderer.cache_samples()!=14)throw std::runtime_error("nonfinite frame accepted");
    anchor_checks(config,clips.animations[0],camera,argv[4]);
    save(second.image,argv[4]);SceneRenderer moved(std::move(renderer));state.frame=.5;rejected=false;try{renderer.render(state,camera);}catch(const odh::FormatError&){rejected=true;}if(!rejected)throw std::runtime_error("moved renderer accepted");
    auto avatar_config=config;avatar_config.object_far.clear();avatar_config.object_entry.clear();avatar_config.object_id=0;
    SceneRenderer avatar_only(avatar_config);
    auto isolated=avatar_only.render(state,camera);
    if(avatar_only.sprite_bytes()!=0 || isolated.tested_samples!=0 || isolated.shaded_samples!=0)
        throw std::runtime_error("avatar-only renderer loaded or composed object sprites");
    save(isolated.image,std::string(argv[4])+"-avatar");
    avatar_config.object_entry="object.iff";rejected=false;
    try{SceneRenderer invalid(avatar_config);}catch(const odh::FormatError&){rejected=true;}
    if(!rejected)throw std::runtime_error("partial object selection accepted");
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
