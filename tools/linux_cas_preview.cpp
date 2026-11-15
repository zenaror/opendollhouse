// SPDX-License-Identifier: MPL-2.0
#include "odh/host_filesystem.hpp"
#include "linux_cas_preview.hpp"
#include "saved_appearance.hpp"
#include "resource.hpp"
#include "original_portrait.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <deque>
#include <set>
#include <chrono>
#include <cstdio>

namespace odh_host {
struct LinuxCasPreview::Impl {
    std::filesystem::path root;
    std::string animations,textures,skins;
    std::vector<std::string> archives,extra_skins;
    std::unique_ptr<SavedAppearanceResources> resources;
    SavedAppearanceResources& appearance_resources(){
        if(!resources){
            ColdResourceStage stage("portrait-metadata");
            resources=std::make_unique<SavedAppearanceResources>(animations,textures,
                "adult-skeleton.cmx.bcf","adult",skins,archives,extra_skins);
        }
        return *resources;
    }
    struct Entry {std::string key;std::optional<odh::BitmapImage> image;std::string error;std::string source;};
    std::string error,last_source;
    std::deque<Entry> cache,portraits;
    std::unique_ptr<SceneRenderer> pie_renderer;
    std::string pie_body;
    std::optional<odh::BitmapImage> pie_image;
    std::array<std::int32_t,2> pie_center{},pie_target{};
    struct PetPose {
        odh::AnimationHeader header;
        std::shared_ptr<const odh::CfpPlanes> planes;
    };
    std::array<std::optional<PetPose>,2> petPoses;

    const PetPose& penPose(bool dog) {
        auto& cached = petPoses[dog ? 0 : 1];
        if (cached) return *cached;
        const std::string clip = dog ? "d2o-displaypen-sit-loop" : "k2o-displaypen-sit-loop";
        const auto archive = odh::hostfs::canonical(root / "ExpansionPack5" / "ExpansionPack5.far").string();
        if (!std::binary_search(archives.begin(), archives.end(), archive))
            throw odh::FormatError("Pet preview archive is outside the selected installation");
        PetPose result;
        with_far_entry(archive, "Animations\\" + clip + ".cmx.bcf", [&](const odh::View& view) {
            auto collection = odh::read_bcf_animations(view);
            for (auto& header : collection.animations) {
                if (header.name != clip) continue;
                if (!result.header.name.empty()) throw odh::FormatError("Ambiguous pet preview animation");
                result.header = std::move(header);
            }
        });
        if (result.header.name.empty()) throw odh::FormatError("Missing pet preview animation");
        with_far_entry(archive, "Animations\\" + result.header.xskill + ".cfp", [&](const odh::View& view) {
            result.planes = std::make_shared<odh::CfpPlanes>(
                odh::read_cfp(view, result.header.translations, result.header.rotations));
        });
        cached = std::move(result);
        return *cached;
    }

    explicit Impl(const std::filesystem::path& path):root(odh::hostfs::canonical(path)) {
        const auto started=std::chrono::steady_clock::now();
        const auto admit=[&](const std::filesystem::path& candidate){
            const auto full=odh::hostfs::canonical(candidate);
            const auto relative=full.lexically_relative(root);
            if(relative.empty()||odh::hostfs::is_absolute(relative)||std::any_of(relative.begin(),relative.end(),
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
        std::printf("[cold-ui] stage=portrait-sources ms=%.3f filesystem_entries=%zu archives=%zu loose_roots=%zu\n",
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count(),
            files,archives.size(),extra_skins.size());
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
    odh::BitmapImage render_hud(const odh::AvatarBodyStrings& body,const LiveAvatarPortraitInput* live=nullptr){
        ColdResourceStage stage("hud-portrait");
        auto& provider=appearance_resources();
        const auto config=live?provider.resolve_live_portrait(live->skeleton,live->body,live->head,live->hands,live->accessories)
            :provider.resolve_avatar_body(body);
        last_source="type="+body.avatar_type+" body="+body.body+" head="+body.head+
            " rig="+config.skeleton_name+" @"+config.skeleton_far+" ["+config.skeleton_entry+"]";
        for(const auto& part:config.parts)last_source+=" part="+part.appearance_name+
            " mesh="+part.bmf_entry+" @"+part.mesh_far+" texture="+part.texture_entry+" @"+part.texture_far;
        SceneRenderer renderer(config);
        const bool quadruped=body.avatar_type=="dog"||body.avatar_type=="cat"||body.avatar_type=="kat";
        auto frame=renderer.render_head_portrait(SceneState{},original_hud_portrait_camera(quadruped)).image;
        if(frame.depth.size()!=frame.pixels.size())throw odh::FormatError("HUD portrait coverage unavailable");
        odh::BitmapImage image{frame.width,frame.height,32,true,std::move(frame.pixels),0};
        for(std::size_t i=0;i<frame.depth.size();++i)
            if(!std::isfinite(frame.depth[i]))image.pixels[i]={255,0,255,0};
        // Compose the complete HUD cell once. Consumers must preserve this
        // canvas rather than enlarging every silhouette to the button bounds.
        int left=image.width,top=image.height,right=0,bottom=0;
        for(int y=0;y<int(image.height);++y)for(int x=0;x<int(image.width);++x)
            if(image.pixels[std::size_t(y)*image.width+x][3]){
                left=std::min(left,x);top=std::min(top,y);
                right=std::max(right,x+1);bottom=std::max(bottom,y+1);
            }
        if(right<=left||bottom<=top)throw odh::FormatError("HUD portrait has no visible head");
        const int sw=right-left,sh=bottom-top;
        const double fit=std::min(35.0/sw,37.0/sh);
        // Pets retain the renderer's common scale, including size differences
        // between breeds. Reduce only when needed to stay inside the cell.
        const double scale=quadruped?std::min(1.0,fit):fit;
        const int width=std::max(1,int(std::lround(sw*scale))),height=std::max(1,int(std::lround(sh*scale)));
        odh::BitmapImage cell{35,41,32,true,
            std::vector<std::array<std::uint8_t,4>>(35*41,{255,0,255,0}),0};
        const int dx=(35-width)/2,dy=(41-height)/2;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)
            cell.pixels[std::size_t(dy+y)*35+dx+x]=image.pixels[std::size_t(top+y*sh/height)*image.width+left+x*sw/width];
        last_source="generated HUD cell v2; "+last_source;
        return cell;
    }
    odh::BitmapImage render_body(const odh::AvatarBodyStrings& body,bool child,bool portrait){
        ColdResourceStage stage("cas-preview");
        auto descriptor=body;descriptor.avatar_type=child?"child":"adult";
        SceneRenderer renderer(appearance_resources().resolve_human_body(descriptor));
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
odh::BitmapImage LinuxCasPreview::pet_body(const odh::AvatarBodyStrings& body) {
    const bool dog = body.avatar_type == "dog";
    if (!dog && body.avatar_type != "cat" && body.avatar_type != "kat")
        throw odh::FormatError("Pet preview requires a cat or dog");
    auto& resources = impl_->appearance_resources();

    const auto& pose = impl_->penPose(dog);
    SceneState state;
    state.animation = &pose.header;
    SceneRenderer renderer(resources.resolve_avatar_body(body), [&](const odh::AnimationHeader& clip) {
        if (&clip != &pose.header) throw odh::FormatError("Unexpected pet preview animation");
        return pose.planes;
    });
    HeadPortraitCamera camera;
    camera.viewport_size = 512;
    camera.yaw_radians = -0.7;
    camera.pitch_radians = -0.7;
    camera.zoom = 4.45;
    camera.target_y_offset = -0.22;
    auto frame = renderer.render_body_portrait(state, camera).image;
    if (frame.depth.size() != frame.pixels.size()) throw odh::FormatError("Pet preview coverage unavailable");

    int left = frame.width, top = frame.height, right = -1, bottom = -1;
    for (int y = 0; y < int(frame.height); ++y) for (int x = 0; x < int(frame.width); ++x) {
        if (!std::isfinite(frame.depth[y * frame.width + x])) continue;
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
    }
    if (right < left || bottom < top || left == 0 || top == 0 ||
        right == int(frame.width) - 1 || bottom == int(frame.height) - 1)
        throw odh::FormatError("Pet preview is absent or clipped");

    // All breeds use the same camera and scale. The transparent margin is
    // centered around the full silhouette and partial edge pixels retain alpha.
    constexpr int size = 160;
    const int sampleSize = std::max({2, (right - left + size) / size, (bottom - top + size) / size});
    const int startX = (left + right - size * sampleSize) / 2;
    const int startY = (top + bottom - size * sampleSize) / 2;
    odh::BitmapImage image{size, size, 32, true, std::vector<std::array<std::uint8_t,4>>(size * size), 0};
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        unsigned rgb[3]{}, covered = 0;
        for (int dy = 0; dy < sampleSize; ++dy) for (int dx = 0; dx < sampleSize; ++dx) {
            const int sx = startX + sampleSize * x + dx, sy = startY + sampleSize * y + dy;
            if (sx < 0 || sx >= int(frame.width) || sy < 0 || sy >= int(frame.height)) continue;
            const size_t sample = size_t(sy) * frame.width + sx;
            if (!std::isfinite(frame.depth[sample])) continue;
            for (unsigned c = 0; c < 3; ++c) rgb[c] += frame.pixels[sample][c];
            ++covered;
        }
        if (!covered) continue;
        auto& pixel = image.pixels[y * size + x];
        for (unsigned c = 0; c < 3; ++c) pixel[c] = uint8_t(rgb[c] / covered);
        pixel[3] = uint8_t(255 * covered / (sampleSize * sampleSize));
    }
    return image;
}

const odh::BitmapImage* LinuxCasPreview::hud_portrait(const odh::AvatarBodyStrings& body){
    std::string key="hud-cell-v2:"+body.avatar_type;
    for(const auto* part:{&body.body,&body.head,&body.hands[0],&body.hands[1],&body.hands[2],&body.hands[3],&body.hands[4],&body.hands[5]}){key+='\0';key+=*part;}
    auto& cache=impl_->portraits;
    const auto found=std::find_if(cache.begin(),cache.end(),[&](const auto& item){return item.key==key;});
    if(found!=cache.end()){impl_->error=found->error;impl_->last_source=found->source;return found->image?&*found->image:nullptr;}
    if(cache.size()>=64)cache.pop_front();
    auto& entry=cache.emplace_back(Impl::Entry{std::move(key),{}, {},{}});
    impl_->error.clear();impl_->last_source.clear();
    try{entry.image=impl_->render_hud(body);entry.source=impl_->last_source;}
    catch(const odh::FormatError& error){entry.error=impl_->error=std::string(error.what()).substr(0,512);return nullptr;}
    return &*entry.image;
}
const odh::BitmapImage* LinuxCasPreview::live_portrait(const LiveAvatarPortraitInput& input){
    if(input.accessories.size()>16){impl_->error="live portrait accessory count exceeds limit";return nullptr;}
    std::string key="live-queue-v1:"+std::to_string(input.head_outfit)+":"+
        std::to_string(input.body_outfit)+":"+std::to_string(input.skin_tone);
    for(const auto* text:{&input.skeleton,&input.body,&input.head}){key+='\0';key+=*text;}
    for(const auto& name:input.hands){key+='\0';key+=name;}
    for(const auto& name:input.accessories){key+='\0';key+=name;}
    if(key.size()>65536){impl_->error="live portrait descriptor exceeds limit";return nullptr;}
    auto& cache=impl_->portraits;
    const auto found=std::find_if(cache.begin(),cache.end(),[&](const auto& item){return item.key==key;});
    if(found!=cache.end()){impl_->error=found->error;impl_->last_source=found->source;return found->image?&*found->image:nullptr;}
    if(cache.size()>=64)cache.pop_front();
    auto& entry=cache.emplace_back(Impl::Entry{std::move(key),{}, {},{}});
    impl_->error.clear();impl_->last_source.clear();
    odh::AvatarBodyStrings body;body.avatar_type=input.skeleton;body.body=input.body;body.head=input.head;
    try{entry.image=impl_->render_hud(body,&input);entry.source=impl_->last_source;}
    catch(const odh::FormatError& error){entry.error=impl_->error=std::string(error.what()).substr(0,512);return nullptr;}
    return &*entry.image;
}
const odh::BitmapImage* LinuxCasPreview::pie_portrait(const odh::AvatarBodyStrings& body,
        std::array<std::int32_t,2> center,std::array<std::int32_t,2> target){
    std::string key="pie-v1:"+body.avatar_type;
    for(const auto* part:{&body.body,&body.head,&body.hands[0],&body.hands[1],&body.hands[2],&body.hands[3],&body.hands[4],&body.hands[5]}){key+='\0';key+=*part;}
    impl_->error.clear();
    try{
        if(!impl_->pie_renderer||impl_->pie_body!=key){
            ColdResourceStage stage("pie-assets");
            auto renderer=std::make_unique<SceneRenderer>(impl_->appearance_resources().resolve_avatar_body(body));
            impl_->pie_renderer=std::move(renderer);impl_->pie_body=std::move(key);impl_->pie_image.reset();
        }
        if(impl_->pie_image&&impl_->pie_center==center&&impl_->pie_target==target)return &*impl_->pie_image;
        auto frame=impl_->pie_renderer->render_head_portrait(SceneState{},original_pie_portrait_camera(center,target)).image;
        if(frame.depth.size()!=frame.pixels.size())throw odh::FormatError("pie portrait coverage unavailable");
        odh::BitmapImage image{frame.width,frame.height,32,true,std::move(frame.pixels),0};
        for(std::size_t i=0;i<frame.depth.size();++i)
            if(!std::isfinite(frame.depth[i]))image.pixels[i]={255,0,255,0};
        impl_->pie_center=center;impl_->pie_target=target;impl_->pie_image=std::move(image);
        return &*impl_->pie_image;
    }catch(const odh::FormatError& error){impl_->error=std::string(error.what()).substr(0,512);return nullptr;}
}
const std::string& LinuxCasPreview::portrait_source() const{return impl_->last_source;}
const std::string& LinuxCasPreview::diagnostic() const{return impl_->error;}
}
