#include "scene_renderer.hpp"
#include "original_portrait.hpp"
#include "odh/special_effect.hpp"
#include "special_effect_view.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
int checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("scene hand gesture assertion failed at check "+std::to_string(checks));}
template<class F> void rejects(F f){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}check(caught);}
void word(std::string& b,std::uint32_t n){for(int i=0;i<4;++i)b.push_back(static_cast<char>((n>>(i*8))&255));}
void scalar(std::string& b,float n){std::uint32_t bits;std::memcpy(&bits,&n,4);word(b,bits);}
void text(std::string& b,const std::string& s){b.push_back(static_cast<char>(s.size()));b+=s;}
using Entry=std::pair<std::string,std::string>;
void far(const std::filesystem::path& p,const std::vector<Entry>& entries){
    std::string b="FAR!byAZ";word(b,1);word(b,0);std::vector<std::uint32_t> offsets;
    for(const auto& e:entries){offsets.push_back(static_cast<std::uint32_t>(b.size()));b+=e.second;}
    auto offset=static_cast<std::uint32_t>(b.size());for(int i=0;i<4;++i)b[12+i]=static_cast<char>((offset>>(i*8))&255);
    word(b,static_cast<std::uint32_t>(entries.size()));
    for(std::size_t i=0;i<entries.size();++i){auto size=static_cast<std::uint32_t>(entries[i].second.size());word(b,size);word(b,size);word(b,offsets[i]);word(b,static_cast<std::uint32_t>(entries[i].first.size()));b+=entries[i].first;}
    std::ofstream out(p,std::ios::binary);out.write(b.data(),static_cast<std::streamsize>(b.size()));
}
std::string rig(bool include_head=true,bool include_pelvis=false){
    std::string b;word(b,1);text(b,"rig");word(b,(include_head?2:1)+(include_pelvis?1:0));
    const auto bone=[&](const char* name,const char* parent,float y,float x=0.f){
        text(b,name);text(b,parent);word(b,0);
        for(float n:{x,y,0.f,0.f,0.f,0.f,1.f})scalar(b,n);
        for(auto n:{1U,1U,0U})word(b,n);scalar(b,0);scalar(b,0);
    };
    bone("ROOT","NULL",0.f);if(include_head)bone("HEAD","ROOT",1.2f);
    if(include_pelvis)bone("PELVIS","ROOT",2.4f,1.5f);
    word(b,0);word(b,0);return b;
}
std::string appearance(const std::string& name,const std::string& bone="ROOT"){
    std::string b;word(b,0);word(b,1);text(b,name);word(b,0);word(b,0);word(b,1);text(b,bone);text(b,name);word(b,0);word(b,0);word(b,0);return b;
}
std::string mesh(const std::string& name,float x,const std::string& bone="ROOT",float scale=1.f,float y_offset=0.f){
    std::string b;text(b,name);text(b,"x");word(b,1);text(b,bone);
    for(auto n:{1U,0U,1U,2U,1U,0U,0U,3U,0U,0U,3U})word(b,n);
    for(int i=0;i<6;++i)scalar(b,.25f);word(b,0);word(b,3);
    for(const auto& position:std::array<std::array<float,3>,3>{{{x-.7f*scale,(2+y_offset)*scale,0},{x+.7f*scale,(2+y_offset)*scale,0},{x,(4+y_offset)*scale,0}}}){
        for(float n:position)scalar(b,n);for(float n:{0.f,1.f,0.f})scalar(b,n);
    }
    return b;
}
std::string bitmap(std::uint8_t color){
    std::string b="BM";word(b,58);word(b,0);word(b,54);word(b,40);word(b,1);word(b,1);
    b.push_back(1);b.push_back(0);b.push_back(24);b.push_back(0);word(b,0);word(b,4);
    for(int i=0;i<4;++i)word(b,0);b.push_back(static_cast<char>(color));b.push_back(40);b.push_back(static_cast<char>(200-color));b.push_back(0);return b;
}
struct Temp{
    std::filesystem::path path;
    Temp(){path=std::filesystem::temp_directory_path()/("odh-scene-hands-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
}
int main(){try{
    Temp dir;const auto a=dir.path/"animations.far",t=dir.path/"textures.far";
    std::vector<Entry> animations{{"rig.bcf",rig()}},textures;
    SceneConfig config;config.animation_far=a.string();config.textures_far=t.string();config.skeleton_entry="rig.bcf";config.skeleton_name="rig";
    for(std::size_t i=0;i<7;++i){
        const auto name="part"+std::to_string(i);animations.push_back({name+".bcf",appearance(name)});
        animations.push_back({name+".bmf",mesh(name,i==0?0.f:(i%2==1?-2.5f:2.5f))});
        textures.push_back({name+".bmp",bitmap(static_cast<std::uint8_t>(i*25))});
        ScenePart part{name+".bcf",name,name+".bmf",name+".bmp"};
        if(i)part.hand=SceneHandPart{(i%2)==0,static_cast<odh::HandGesture>((i-1)/2)};
        config.parts.push_back(part);
    }
    animations.push_back({"head.bcf",appearance("head","HEAD")});
    animations.push_back({"head.bmf",mesh("head",0.f,"HEAD",.4f,-3.f)});
    textures.push_back({"head.bmp",bitmap(230)});
    config.parts.push_back({"head.bcf","head","head.bmf","head.bmp"});
    far(a,animations);far(t,textures);SceneRenderer renderer(config);
    odh::WorldProjection camera;camera.width=160;camera.height=160;
    SceneState state;const auto defaults=renderer.render(state,camera);
    check(defaults.image.shaded_samples>0);
    // Off-lot visibility hides body, hand variants, accessories and pose
    // anchors without substituting an origin or resolving unavailable clips.
    auto hidden=state;hidden.avatar_visible=false;hidden.head_only=true;
    odh::AnimationHeader unavailable;unavailable.name="unavailable-offlot";unavailable.xskill="missing";
    hidden.animation=&unavailable;hidden.carry_animation=&unavailable;
    hidden.animation_layers={{&unavailable,0,1}};hidden.owner_id=123;
    odh::RasterOptions background;background.clear={17,29,43,255};background.record_owner_ids=true;
    ScenePoseAnchors hidden_anchors;hidden_anchors.avatar_slots[0]=std::array<float,3>{1,2,3};
    const auto clips_before_hidden=renderer.cache_clips();const auto samples_before_hidden=renderer.cache_samples();
    const auto offlot=renderer.render(hidden,camera,background,&hidden_anchors);
    check(offlot.image.width==camera.width&&offlot.image.height==camera.height&&offlot.image.shaded_samples==0);
    check(std::all_of(offlot.image.pixels.begin(),offlot.image.pixels.end(),[&](const auto& pixel){return pixel==background.clear;}));
    check(std::all_of(offlot.image.depth.begin(),offlot.image.depth.end(),[](float z){return std::isinf(z)&&z>0;}));
    check(std::all_of(offlot.image.owner_ids.begin(),offlot.image.owner_ids.end(),[](auto owner){return owner==0;}));
    check(std::all_of(hidden_anchors.avatar_slots.begin(),hidden_anchors.avatar_slots.end(),[](const auto& slot){return !slot;}));
    check(renderer.pose_anchors(hidden).avatar_slots==hidden_anchors.avatar_slots);
    check(renderer.cache_clips()==clips_before_hidden&&renderer.cache_samples()==samples_before_hidden);
    const auto visible_again=renderer.render(state,camera);
    check(visible_again.image.pixels==defaults.image.pixels&&visible_again.image.depth==defaults.image.depth&&
        visible_again.image.shaded_samples==defaults.image.shaded_samples);

    const auto resident_vertices=renderer.vertices(),resident_triangles=renderer.triangles(),resident_textures=renderer.texture_bytes();
    check(resident_vertices==24&&resident_triangles==8&&resident_textures==32);
    SceneState head_state;head_state.head_only=true;
    const auto head_only=renderer.render(head_state,camera);
    check(head_only.image.shaded_samples>0);
    check(head_only.image.pixels!=defaults.image.pixels&&
          head_only.image.shaded_samples<defaults.image.shaded_samples);
    auto head_control_config=config;head_control_config.parts={config.parts.back()};
    SceneRenderer head_control(head_control_config);
    const auto head_control_frame=head_control.render(SceneState{},camera);
    check(head_only.image.pixels==head_control_frame.image.pixels&&
          head_only.image.depth==head_control_frame.image.depth);
    const auto portrait=renderer.render_head_portrait(SceneState{});
    const auto portrait_repeat=renderer.render_head_portrait(SceneState{});
    check(portrait.image.width==74&&portrait.image.height==74&&portrait.image.shaded_samples>0);
    check(portrait.image.pixels==portrait_repeat.image.pixels&&portrait.image.depth==portrait_repeat.image.depth);
    check(std::any_of(portrait.image.depth.begin(),portrait.image.depth.end(),
        [](float depth){return std::isfinite(depth)&&depth>0.0f;}));
    auto moved_avatar=SceneState{};moved_avatar.avatar_tile={8,-3,5};moved_avatar.avatar_direction=7;
    const auto moved_portrait=renderer.render_head_portrait(moved_avatar);
    check(portrait.image.pixels==moved_portrait.image.pixels&&portrait.image.depth==moved_portrait.image.depth);
    check(renderer.render(SceneState{},camera).image.pixels==defaults.image.pixels);
    auto invalid_portrait_camera=HeadPortraitCamera{};invalid_portrait_camera.zoom=0;
    rejects([&]{renderer.render_head_portrait(SceneState{},invalid_portrait_camera);});
    auto near_clipping_camera=HeadPortraitCamera{};near_clipping_camera.near_plane=13.4;
    const auto clipped_portrait=renderer.render_head_portrait(SceneState{},near_clipping_camera);
    check(clipped_portrait.image.width==74&&clipped_portrait.image.height==74&&
          clipped_portrait.image.shaded_samples>0);
    check(clipped_portrait.image.pixels!=portrait.image.pixels);
    // The action-menu render gets its own native resolution and orbit; later
    // HUD renders must retain the original projection and pose.
    const auto pie_camera=original_pie_portrait_camera({160,120},{220,120});
    const auto pie_portrait=renderer.render_head_portrait(SceneState{},pie_camera);
    check(pie_portrait.image.width==200&&pie_portrait.image.height==200&&pie_portrait.image.shaded_samples>0);
    const auto neutral_pie=renderer.render_head_portrait(SceneState{},original_pie_portrait_camera({160,120},{160,120}));
    check(neutral_pie.image.pixels!=pie_portrait.image.pixels);
    check(renderer.render_head_portrait(SceneState{}).image.pixels==portrait.image.pixels);
    for(const auto invalid_size:{0U,15U,513U,65536U}){
        auto invalid=pie_camera;invalid.viewport_size=invalid_size;
        rejects([&]{renderer.render_head_portrait(SceneState{},invalid);});
    }
    for(unsigned left=0;left<4;++left)for(unsigned right=0;right<4;++right){
        state.hands={static_cast<odh::HandGesture>(left),static_cast<odh::HandGesture>(right)};
        auto subset=config;subset.parts.clear();
        for(auto part:config.parts){
            if(part.hand&&part.hand->gesture!=(part.hand->right?state.hands.right:state.hands.left))continue;
            part.hand.reset();subset.parts.push_back(part);
        }
        SceneRenderer control(subset);auto expected=control.render(SceneState{},camera),actual=renderer.render(state,camera);
        check(actual.image.pixels==expected.image.pixels&&actual.image.depth==expected.image.depth);
        check(actual.image.tested_samples==expected.image.tested_samples&&actual.image.shaded_samples==expected.image.shaded_samples);
        check(renderer.vertices()==resident_vertices&&renderer.triangles()==resident_triangles&&renderer.texture_bytes()==resident_textures);
        check(renderer.cache_samples()==0&&renderer.cache_clips()==0);
        if(left==3&&right==3)check(actual.image.pixels!=defaults.image.pixels&&actual.image.shaded_samples<defaults.image.shaded_samples);
    }
    // Untagged legacy parts remain visible even with both gestures None.
    auto legacy=config;for(auto& part:legacy.parts)part.hand.reset();SceneRenderer always(legacy);
    check(always.render(state,camera).image.pixels==always.render(SceneState{},camera).image.pixels);
    const auto no_head_archive=dir.path/"no-head.far";
    far(no_head_archive,{{"rig.bcf",rig(false)},{"part0.bcf",appearance("part0")},
                         {"part0.bmf",mesh("part0",0.f)}});
    SceneConfig no_head_config;no_head_config.animation_far=no_head_archive.string();
    no_head_config.textures_far=t.string();no_head_config.skeleton_entry="rig.bcf";
    no_head_config.skeleton_name="rig";no_head_config.parts.push_back(config.parts.front());
    SceneRenderer no_head_renderer(no_head_config);
    auto invalid_head_state=SceneState{};invalid_head_state.head_only=true;
    rejects([&]{no_head_renderer.render(invalid_head_state,camera);});
    // Camera facts use the current draw's full pose sampler, without rasterizing
    // or borrowing the previous frame's bone cache.
    const auto pelvis_archive=dir.path/"pelvis.far";
    far(pelvis_archive,{{"rig.bcf",rig(true,true)},{"part0.bcf",appearance("part0")},
                         {"part0.bmf",mesh("part0",0.f)}});
    auto pelvis_config=no_head_config;pelvis_config.animation_far=pelvis_archive.string();
    SceneRenderer pelvis_renderer(pelvis_config);SceneState pelvis_state;
    pelvis_state.avatar_tile={8.f,9.f,1.f};pelvis_state.avatar_radians=0.6f;
    const auto before_pose_samples=pelvis_renderer.cache_samples();
    const auto facts=pelvis_renderer.pose_anchors(pelvis_state);
    ScenePoseAnchors drawn_facts;
    (void)pelvis_renderer.render(pelvis_state,camera,{},&drawn_facts);
    check(facts.avatar_slots==drawn_facts.avatar_slots&&facts.avatar_slots[2].has_value());
    check(pelvis_renderer.cache_samples()==before_pose_samples);
    // Display banks affect drawing, while functional queries keep the actual
    // indexed skeleton. Ghost tint/alpha never remove or move source bones.
    {odh::World display_world;check(display_world.create(7,odh::EntityKind::avatar,{0,88,0}));
     check(display_world.declare_object_word(7,34,1)&&display_world.set_person_data(7,68,1)&&display_world.set_person_data(7,74,1));
     auto hidden_draw=pelvis_state;apply_avatar_display(hidden_draw,*display_world.find(7));
     check(!hidden_draw.avatar_visible&&pelvis_renderer.render(hidden_draw,camera).image.shaded_samples==0);
     auto query=hidden_draw;query.avatar_visible=true;const auto hidden_bones=pelvis_renderer.pose_anchors(query);
     check(hidden_bones.avatar_slots==facts.avatar_slots&&hidden_bones.neck_matrix==facts.neck_matrix);
     check(hidden_bones.headline_target==facts.headline_target);
    }
    // A hidden draw must not slide the source VisualNormal. A bone query is
    // also a read-only operation, even when passed a later presentation epoch.
    {SceneRenderer normals(pelvis_config);SceneState align;align.avatar_tile=camera.center_tile;
     align.display_flags=4096;align.ground_normal=std::array<float,3>{.6f,.8f,0};align.simulation_tick=0;
     const auto flat=normals.render(align,camera).image;check(flat.shaded_samples>0);
     align.avatar_visible=false;align.simulation_tick=1;check(normals.render(align,camera).image.shaded_samples==0);
     align.avatar_visible=true;align.simulation_tick=2;const auto first_visible=normals.render(align,camera).image;
     check(first_visible.depth==flat.depth&&first_visible.pixels==flat.pixels);
     align.simulation_tick=3;const auto tilted=normals.render(align,camera).image;check(tilted.depth!=flat.depth);
     align.simulation_tick=100;const auto query1=normals.pose_anchors(align),query2=normals.pose_anchors(align);
     check(query1.avatar_slots==query2.avatar_slots&&query1.neck_matrix==query2.neck_matrix);
     align.simulation_tick=4;(void)normals.render(align,camera); // Query did not consume epoch100.
    }
    // CenterTo adds back the slot-only half-tile XY offset; vertical bone
    // height remains composed. A logical avatar origin is not the pelvis.
    const auto focus=*facts.avatar_slots[2];
    check(focus[2]>pelvis_state.avatar_tile[2]&&focus[0]+.5f!=pelvis_state.avatar_tile[0]);
    pelvis_state.avatar_tile[0]+=3.f;
    const auto moved_facts=pelvis_renderer.pose_anchors(pelvis_state);
    check(moved_facts.avatar_slots[2]&&(*moved_facts.avatar_slots[2])[0]>focus[0]+2.99f);
    check(!no_head_renderer.pose_anchors(SceneState{}).avatar_slots[2]);
    // One CPU integration path: authored opcode35 -> actual VM proof -> current
    // animated pelvis -> instant view intent -> camera. No window is involved.
    odh::World focus_world;
    check(focus_world.create(1,odh::EntityKind::avatar,{})&&
        focus_world.set_position(1,{176,144,1,0}));
    const auto logical_before=*focus_world.find(1)->position;
    const odh::WorldEntityReference focus_identity{1,focus_world.find(1)->generation};
    odh_host::SpecialEffectView integrated_view;
    std::array<float,3> integrated_pelvis{};unsigned focus_calls=0;
    auto effect=odh::special_effect_primitive(focus_world,{true,
        [&](const odh::SpecialEffectFocusRequest& request){
            ++focus_calls;check(request.target==focus_identity&&request.source_still_matches());
            const auto current_pose=pelvis_renderer.pose_anchors(pelvis_state);
            check(current_pose.avatar_slots[2].has_value());
            integrated_pelvis=*current_pose.avatar_slots[2];
            integrated_pelvis[0]+=.5f;integrated_pelvis[1]+=.5f;
            std::string reason;
            const auto completed=integrated_view.center_to(request.target,
                {focus_identity,1,1,integrated_pelvis},&reason);
            check(request.source_still_matches());
            return odh::SpecialEffectFocusResult{completed,reason};
        }});
    odh::RoutineSet focus_routines{{odh::routine_key(4096),odh::Bhav{0x8002,
        {{35,254,255,{10,0,1,2,8,0,0,0}}},{},odh::Bhav::Metadata{0,0,0,0}}}};
    odh::Vm focus_vm{std::move(focus_routines),focus_world.vm_memory(),{},std::move(effect)};
    focus_vm.start(odh::routine_key(4096),{1,1,1});
    check(focus_vm.run(1)==odh::VmStatus::returned_true&&focus_calls==1);
    check(integrated_view.apply(camera)==std::optional<std::uint8_t>{1}&&
        camera.center_tile==integrated_pelvis&&integrated_view.focused());
    check(camera.center_tile[0]!=pelvis_state.avatar_tile[0]&&
        camera.center_tile[2]>pelvis_state.avatar_tile[2]);
    const auto& logical_after=*focus_world.find(1)->position;
    check(logical_after.x==logical_before.x&&logical_after.y==logical_before.y&&
        logical_after.level==logical_before.level&&logical_after.direction==logical_before.direction);
    camera.center_tile={90.f,80.f,70.f};
    check(!integrated_view.apply(camera)&&camera.center_tile==std::array<float,3>{90.f,80.f,70.f});
    state.hands.left=static_cast<odh::HandGesture>(4);odh::AnimationHeader missing{};missing.xskill="missing";state.animation=&missing;
    rejects([&]{renderer.render(state,camera);});check(renderer.cache_clips()==0);
    state.hands={};state.hands.right=static_cast<odh::HandGesture>(255);rejects([&]{renderer.render(state,camera);});
    auto bad=config;bad.parts[1].hand->gesture=odh::HandGesture::none;rejects([&]{SceneRenderer invalid(bad);});
    bad.parts[1].hand->gesture=static_cast<odh::HandGesture>(4);rejects([&]{SceneRenderer invalid(bad);});
    bad=config;while(bad.parts.size()<17)bad.parts.push_back(config.parts[0]);rejects([&]{SceneRenderer invalid(bad);});
    std::cout<<checks<<" scene hand gesture checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
