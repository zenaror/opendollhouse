#include "scene_renderer.hpp"
#include <chrono>
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
std::string rig(bool include_head=true){
    std::string b;word(b,1);text(b,"rig");word(b,include_head?2:1);
    const auto bone=[&](const char* name,const char* parent,float y){
        text(b,name);text(b,parent);word(b,0);
        for(float n:{0.f,y,0.f,0.f,0.f,0.f,1.f})scalar(b,n);
        for(auto n:{1U,1U,0U})word(b,n);scalar(b,0);scalar(b,0);
    };
    bone("ROOT","NULL",0.f);if(include_head)bone("HEAD","ROOT",0.f);
    word(b,0);word(b,0);return b;
}
std::string appearance(const std::string& name,const std::string& bone="ROOT"){
    std::string b;word(b,0);word(b,1);text(b,name);word(b,0);word(b,0);word(b,1);text(b,bone);text(b,name);word(b,0);word(b,0);word(b,0);return b;
}
std::string mesh(const std::string& name,float x,const std::string& bone="ROOT"){
    std::string b;text(b,name);text(b,"x");word(b,1);text(b,bone);
    for(auto n:{1U,0U,1U,2U,1U,0U,0U,3U,0U,0U,3U})word(b,n);
    for(int i=0;i<6;++i)scalar(b,.25f);word(b,0);word(b,3);
    for(const auto& position:std::array<std::array<float,3>,3>{{{x-.7f,2,0},{x+.7f,2,0},{x,4,0}}}){
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
    animations.push_back({"head.bmf",mesh("head",0.f,"HEAD")});
    textures.push_back({"head.bmp",bitmap(230)});
    config.parts.push_back({"head.bcf","head","head.bmf","head.bmp"});
    far(a,animations);far(t,textures);SceneRenderer renderer(config);
    odh::WorldProjection camera;camera.width=160;camera.height=160;
    SceneState state;const auto defaults=renderer.render(state,camera);
    check(defaults.image.shaded_samples>0);
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
    state.hands.left=static_cast<odh::HandGesture>(4);odh::AnimationHeader missing{};missing.xskill="missing";state.animation=&missing;
    rejects([&]{renderer.render(state,camera);});check(renderer.cache_clips()==0);
    state.hands={};state.hands.right=static_cast<odh::HandGesture>(255);rejects([&]{renderer.render(state,camera);});
    auto bad=config;bad.parts[1].hand->gesture=odh::HandGesture::none;rejects([&]{SceneRenderer invalid(bad);});
    bad.parts[1].hand->gesture=static_cast<odh::HandGesture>(4);rejects([&]{SceneRenderer invalid(bad);});
    bad=config;while(bad.parts.size()<17)bad.parts.push_back(config.parts[0]);rejects([&]{SceneRenderer invalid(bad);});
    std::cout<<checks<<" scene hand gesture checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
