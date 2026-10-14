#include "saved_appearance.hpp"
#include "odh/mesh.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <map>
#include <algorithm>

namespace {
std::string fold(std::string s) {for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c+'a'-'A');return s;}
void bounded_name(const std::string& s) {
    if(s.empty()||s.size()>4096||s.find('\0')!=std::string::npos)
        throw odh::FormatError("saved appearance empty, oversized or embedded-NUL name");
}
std::string uniform(std::string s) {
    s=fold(std::move(s));
    for(const auto* skin:{"lgt","med","drk"})for(auto p=s.find(skin);p!=std::string::npos;p=s.find(skin))s.erase(p,3);
    return s;
}
std::optional<std::string> texture_override(const std::string& raw) {
    if(raw.empty())return {};
    if(raw.size()>4096||raw.find('\0')!=std::string::npos)throw odh::FormatError("saved texture map size or NUL invalid");
    auto eq=raw.find('=');
    if(eq==std::string::npos||eq==0||eq+1==raw.size())throw odh::FormatError("saved texture map requires label=value");
    auto result=raw.substr(eq+1);bounded_name(result);
    if(result.find_first_of("=;,")!=std::string::npos)throw odh::FormatError("saved texture map multiple bindings unsupported");
    return result;
}
struct Archive {
    std::ifstream stream;
    std::uint64_t size;
    std::vector<odh::FarEntry> entries;
    explicit Archive(const std::string& path):stream(path,std::ios::binary),size(0) {
        bounded_name(path);if(!stream)throw odh::FormatError("saved appearance cannot open FAR");
        size=std::filesystem::file_size(path);odh::Limits lim;lim.far_entries=8192;lim.far_name_bytes=256*1024;
        entries=odh::far1_index(odh::View(stream,size),lim);
    }
    const odh::FarEntry& select(const std::string& name) const {
        const auto key=fold(name);const odh::FarEntry* out=nullptr;
        for(const auto& e:entries)if(fold(e.name)==key){if(out)throw odh::FormatError("saved appearance ambiguous FAR entry: "+name);out=&e;}
        if(!out)throw odh::FormatError("saved appearance missing FAR entry: "+name);
        return *out;
    }
    odh::View view(const odh::FarEntry& e){return odh::View(stream,size).subview(e.offset,e.length);}
};
struct Named {std::string entry,name;};
}
SavedAnimationAppearance saved_animation_appearance(const std::string& raw,const odh::AnimationHeader& header) {
    bounded_name(raw);std::array<std::string,8> fields;std::size_t start=0;
    for(std::size_t i=0;i<fields.size();++i){auto end=raw.find(';',start);
        if((i<7&&end==std::string::npos)||(i==7&&end!=std::string::npos))throw odh::FormatError("saved animation requires eight fields");
        fields[i]=raw.substr(start,end==std::string::npos?end:end-start);start=end==std::string::npos?raw.size():end+1;}
    if(fold(fields[0])!=fold(header.name))throw odh::FormatError("saved animation header name mismatch");
    auto number=[&](std::size_t i){std::int32_t n=0;const auto& s=fields[i];auto r=std::from_chars(s.data(),s.data()+s.size(),n);
        if(s.empty()||r.ec!=std::errc{}||r.ptr!=s.data()+s.size())throw odh::FormatError("saved animation invalid integer");
        return n;};
    SavedAnimationAppearance out;out.raw=raw;out.header=header;out.priority=number(1);out.speed_milli=number(2);
    out.normalized_frame=number(3);out.weight_milli=number(4);auto loop=number(5);out.unknown1=number(6);out.unknown2=number(7);
    if((fields[5]!="0"&&fields[5]!="1")||loop<0||loop>1||out.normalized_frame<0||out.normalized_frame>1000||out.weight_milli<0||out.weight_milli>1000)
        throw odh::FormatError("saved animation frame, weight or loop outside supported range");
    if(header.motions.empty())throw odh::FormatError("saved animation has no motions");
    std::uint32_t frames=0;for(const auto& m:header.motions)frames=std::max(frames,m.frames);
    if(frames==0)throw odh::FormatError("saved animation zero frames");
    out.frame=static_cast<double>(frames)*out.normalized_frame/1000.0;out.loop=loop==1;out.backwards=out.speed_milli<0;
    out.end_reached=out.normalized_frame==(out.backwards?0:1000);return out;
}
struct SavedAppearanceResources::Impl {
    SceneConfig config;Archive animations,textures;
    std::map<std::string,std::vector<Named>> appearances,clips;
    Impl(const std::string& a,const std::string& t,const std::string& se,const std::string& sn):animations(a),textures(t) {
        config.animation_far=a;config.textures_far=t;config.skeleton_entry=animations.select(se).name;config.skeleton_name=sn;bounded_name(sn);
        std::uint64_t count=0,bytes=0;
        auto add=[&](auto& index,const std::string& name,const std::string& entry){bounded_name(name);
            if(++count>8192||(bytes+=name.size()+entry.size())>256*1024)throw odh::FormatError("saved appearance named resource budget exceeded");
            index[fold(name)].push_back({entry,name});};
        for(const auto& entry:animations.entries){const auto key=fold(entry.name);if(key.size()<4||key.substr(key.size()-4)!=".bcf")continue;
            auto view=animations.view(entry);if(view.le32(0)!=0)continue;
            if(view.le32(4)==0){auto decoded=odh::read_bcf_animations(view);if(decoded.remaining_bytes)throw odh::FormatError("saved appearance trailing animation BCF bytes");
                for(const auto& c:decoded.animations)add(clips,c.name,entry.name);
            }else{auto decoded=odh::read_bcf_appearances(view);if(decoded.remaining_bytes)throw odh::FormatError("saved appearance trailing appearance BCF bytes");
                for(const auto& p:decoded.appearances)add(appearances,p.name,entry.name);}}
    }
    template<class Index> const Named& named(const Index& index,const std::string& name) {
        auto found=index.find(fold(name));if(found==index.end())throw odh::FormatError("saved appearance named resource missing: "+name);
        if(found->second.size()!=1)throw odh::FormatError("saved appearance named resource ambiguous: "+name);
        return found->second.front();
    }
    void part(SceneConfig& out,const std::string& name,const std::string& raw_texture) {
        bounded_name(name);auto host=named(appearances,name);auto collection=odh::read_bcf_appearances(animations.view(animations.select(host.entry)));
        const odh::Appearance* appearance=nullptr;for(const auto& p:collection.appearances)if(p.name==host.name)appearance=&p;
        if(!appearance||appearance->bindings.empty()||appearance->bindings.size()>16)throw odh::FormatError("saved appearance binding count unsupported");
        auto override=texture_override(raw_texture);std::vector<std::string> mesh_entries,defaults;
        std::size_t replaced=0;
        for(std::size_t i=0;i<appearance->bindings.size();++i){const auto& binding=appearance->bindings[i];auto mesh_entry=animations.select(binding.mesh_name+".bmf").name;
            auto mesh=odh::read_bmf(animations.view(animations.select(mesh_entry)));if(fold(mesh.skin_name)!=fold(binding.mesh_name))throw odh::FormatError("saved appearance BMF skin mismatch");
            const auto m=uniform(mesh.texture_name),o=override?uniform(*override):std::string{};
            if(override&&(fold(mesh.texture_name)=="x"||(m.size()>=o.size()&&m.compare(m.size()-o.size(),o.size(),o)==0)))replaced=i;
            mesh_entries.push_back(mesh_entry);defaults.push_back(mesh.texture_name);}
        for(std::size_t i=0;i<mesh_entries.size();++i){auto texture=override&&i==replaced?*override:defaults[i];bounded_name(texture);
            if(out.parts.size()>=16)throw odh::FormatError("saved appearance cumulative part budget exceeded");
            out.parts.push_back({host.entry,host.name,mesh_entries[i],textures.select(texture+".bmp").name});}
    }
    std::optional<SavedAnimationAppearance> animation(const std::string& raw) {
        if(raw.empty())return {};
        bounded_name(raw);auto semi=raw.find(';');if(semi==std::string::npos)throw odh::FormatError("saved animation missing fields");
        auto host=named(clips,raw.substr(0,semi));auto decoded=odh::read_bcf_animations(animations.view(animations.select(host.entry)));
        for(const auto& c:decoded.animations)if(c.name==host.name){auto out=saved_animation_appearance(raw,c);out.entry=host.entry;out.cfp_entry=animations.select(c.xskill+".cfp").name;return out;}
        throw odh::FormatError("saved animation indexed header missing");
    }
};
SavedAppearanceResources::SavedAppearanceResources(const std::string& a,const std::string& t,const std::string& se,const std::string& sn):impl_(std::make_unique<Impl>(a,t,se,sn)){}
SavedAppearanceResources::~SavedAppearanceResources()=default;
SavedAppearanceResources::SavedAppearanceResources(SavedAppearanceResources&&) noexcept=default;
SavedAppearanceResources& SavedAppearanceResources::operator=(SavedAppearanceResources&&) noexcept=default;
SavedAppearance SavedAppearanceResources::resolve(const odh::ObjmPersonState& person) {
    if(!impl_)throw odh::FormatError("saved appearance resolver used after move");
    if(person.accessory_count!=0||!person.accessories.empty())throw odh::FormatError("saved appearance accessories unsupported");
    if(!person.carry_animation.bytes.empty())throw odh::FormatError("saved appearance carry animation unsupported");
    SavedAppearance out;out.scene=impl_->config;
    impl_->part(out.scene,person.body.bytes,person.body_texture.bytes);impl_->part(out.scene,person.head.bytes,person.head_texture.bytes);
    impl_->part(out.scene,person.left_hand.bytes,person.left_hand_texture.bytes);impl_->part(out.scene,person.right_hand.bytes,person.right_hand_texture.bytes);
    out.current=impl_->animation(person.animation.bytes);out.base=impl_->animation(person.base_animation.bytes);
    if(out.current&&out.current->weight_milli!=1000)throw odh::FormatError("saved appearance current animation blending unsupported");
    return out;
}
