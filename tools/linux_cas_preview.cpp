// SPDX-License-Identifier: MPL-2.0
#include "linux_cas_preview.hpp"
#include "saved_appearance.hpp"
#include "resource.hpp"
#include "original_portrait.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <deque>
#include <set>

namespace odh_host {
struct LinuxCasPreview::Impl {
    std::filesystem::path root;
    std::string animations,textures,skins;
    std::vector<std::string> archives,extra_skins;
    std::unique_ptr<SavedAppearanceResources> adults,children;
    struct Entry {std::string key;std::optional<odh::BitmapImage> image;std::string error;};
    std::string error;
    std::deque<Entry> cache,portraits;
    explicit Impl(const std::filesystem::path& path):root(std::filesystem::canonical(path)) {
        const auto admit=[&](const std::filesystem::path& candidate){
            const auto full=std::filesystem::canonical(candidate);
            const auto relative=full.lexically_relative(root);
            if(relative.empty()||relative.is_absolute()||std::any_of(relative.begin(),relative.end(),
                [](const auto& part){return part=="..";}))throw odh::FormatError("CAS preview asset escapes installation");
            return full.string();
        };
        animations=admit(root/"GameData/Animation/Animation.far");
        textures=admit(root/"GameData/Textures/Textures.far");
        skins=admit(root/"GameData/Skins");
        std::set<std::string> unique;
        std::size_t files=0;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root)){
            if(++files>200000)throw odh::FormatError("CAS preview installation index exceeds limit");
            if(!entry.is_regular_file())continue;
            auto extension=entry.path().extension().string();
            std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(extension==".far")unique.insert(admit(entry.path()));
        }
        if(unique.size()>256)throw odh::FormatError("CAS preview archive count exceeds limit");
        archives.assign(unique.begin(),unique.end());
        for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.is_directory()){
            const auto name=entry.path().filename().string();
            if(name=="Downloads")extra_skins.push_back(admit(entry.path()));
            else if(name.rfind("Expansion",0)==0){
                for(const auto* leaf:{"Skins","SkinsBuy"})
                    if(std::filesystem::is_directory(entry.path()/leaf))extra_skins.push_back(admit(entry.path()/leaf));
            }
        }
    }
    odh::BitmapImage render(const SimitoneCasPersonInput& person,bool portrait){
        odh::AvatarBodyStrings body;
        body.body=person.body_appearance+",BODY="+person.body_texture;
        body.head=person.head_appearance+",HEAD-HEAD="+person.head_texture;
        const std::string hand=person.child?"u":person.female?"f":"m";
        constexpr std::array<const char*,6> gestures{{"LO","RO","LP","RP","LO","RC"}};
        for(std::size_t i=0;i<body.hands.size();++i)body.hands[i]="H"+hand+gestures[i]+",HAND=huao"+person.handgroup_texture;
        return render_body(body,person.child,portrait);
    }
    odh::BitmapImage render_body(const odh::AvatarBodyStrings& body,bool child,bool portrait){
        auto& resources=child?children:adults;
        if(!resources)resources=std::make_unique<SavedAppearanceResources>(animations,textures,
            child?"child-skeleton.cmx.bcf":"adult-skeleton.cmx.bcf",child?"child":"adult",skins,archives,extra_skins);
        SceneRenderer renderer(resources->resolve_body(body));
        if(portrait){
            auto frame=renderer.render_head_portrait(SceneState{},original_family_portrait_camera()).image;
            if(frame.depth.size()!=frame.pixels.size())throw odh::FormatError("CAS portrait coverage unavailable");
            odh::BitmapImage image{frame.width,frame.height,24,true,std::move(frame.pixels),0};
            for(std::size_t i=0;i<frame.depth.size();++i)
                if(!std::isfinite(frame.depth[i]))image.pixels[i]={255,0,255,255};
            return image;
        }
        odh::AnimationHeader standing;
        with_far_entry(animations,child?"kid-standing-loop.cmx.bcf":"ross-standing-loop.cmx.bcf",[&](const odh::View& view){
            const auto list=odh::read_bcf_animations(view);
            if(list.animations.size()!=1)throw odh::FormatError("CAS preview standing clip is ambiguous");
            standing=list.animations.front();
        });
        SceneState state;state.animation=&standing;state.avatar_direction=4;
        odh::WorldProjection camera;camera.width=camera.height=512;camera.center_tile={0,0,.5f};
        auto frame=renderer.render(state,camera).image;
        if(frame.depth.size()!=frame.pixels.size())throw odh::FormatError("CAS preview coverage unavailable");
        int left=512,top=512,right=-1,bottom=-1;
        for(int y=0;y<512;++y)for(int x=0;x<512;++x)if(std::isfinite(frame.depth[std::size_t(y)*512+x])){
            left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);
        }
        if(right<left||bottom<top||left==0||top==0||right==511||bottom==511)
            throw odh::FormatError("CAS preview silhouette unavailable or clipped");
        const int width=100,height=220;
        odh::BitmapImage image{std::uint32_t(width),std::uint32_t(height),24,true,
            std::vector<std::array<std::uint8_t,4>>(std::size_t(width)*height,{255,0,255,255}),0};
        const int sw=right-left+1,sh=bottom-top+1;
        const double scale=std::min(double(width-8)/sw,double(height-8)/sh);
        const int dw=std::max(1,int(sw*scale)),dh=std::max(1,int(sh*scale));
        const int dx=(width-dw)/2,dy=height-4-dh;
        for(int y=0;y<dh;++y)for(int x=0;x<dw;++x){
            const auto source=std::size_t(top+y*sh/dh)*512+left+x*sw/dw;
            if(std::isfinite(frame.depth[source]))image.pixels[std::size_t(dy+y)*width+dx+x]=frame.pixels[source];
        }
        return image;
    }
};
LinuxCasPreview::LinuxCasPreview(const std::filesystem::path& root):impl_(std::make_unique<Impl>(root)){}
LinuxCasPreview::~LinuxCasPreview()=default;
const odh::BitmapImage* LinuxCasPreview::image(const SimitoneCasPersonInput& p,bool portrait){
    std::string key(1,portrait?'p':'f');key+=p.child?'c':'a';key+=p.female?'f':'m';
    for(const auto* name:{&p.body_appearance,&p.body_texture,&p.head_appearance,&p.head_texture,&p.handgroup_texture}){key+='\0';key+=*name;}
    auto& cache=impl_->cache;
    const auto found=std::find_if(cache.begin(),cache.end(),[&](const auto& e){return e.key==key;});
    if(found!=cache.end()){impl_->error=found->error;return found->image?&*found->image:nullptr;}
    if(cache.size()>=9)cache.pop_front();
    auto& entry=cache.emplace_back(Impl::Entry{std::move(key),{}, {}});
    impl_->error.clear();
    try{entry.image=impl_->render(p,portrait);}catch(const odh::FormatError& e){
        entry.error=impl_->error=std::string(e.what()).substr(0,512);return nullptr;
    }
    return &*entry.image;
}
const odh::BitmapImage* LinuxCasPreview::portrait(const odh::AvatarBodyStrings& body,bool child){
    std::string key(1,child?'c':'a');
    for(const auto* part:{&body.body,&body.head,&body.hands[0],&body.hands[1],&body.hands[2],&body.hands[3],&body.hands[4],&body.hands[5]}){
        key+='\0';key+=*part;
    }
    auto& cache=impl_->portraits;
    const auto found=std::find_if(cache.begin(),cache.end(),[&](const auto& item){return item.key==key;});
    if(found!=cache.end()){impl_->error=found->error;return found->image?&*found->image:nullptr;}
    if(cache.size()>=64)cache.pop_front();
    auto& entry=cache.emplace_back(Impl::Entry{std::move(key),{}, {}});
    impl_->error.clear();
    try{entry.image=impl_->render_body(body,child,true);}
    catch(const odh::FormatError& e){entry.error=impl_->error=std::string(e.what()).substr(0,512);return nullptr;}
    return &*entry.image;
}
const std::string& LinuxCasPreview::diagnostic() const{return impl_->error;}
}
