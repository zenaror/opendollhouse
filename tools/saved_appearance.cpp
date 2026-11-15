#include "resource.hpp"
#include "odh/host_filesystem.hpp"
#include "saved_appearance.hpp"
#include "odh/ts1_texture_name.hpp"
#include "saved_appearance_far_index.hpp"
#include "odh/mesh.hpp"
#include "odh/bcf.hpp"
#include "odh/sha256.hpp"
#include "odh/original_animation.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <map>
#include <algorithm>
#include <set>
#include <cmath>
#include <climits>

namespace {
std::string fold(std::string s) {for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c+'a'-'A');return s;}
void bounded_name(const std::string& s) {
    if(s.empty()||s.size()>4096||s.find('\0')!=std::string::npos)
        throw odh::FormatError("saved appearance empty, oversized or embedded-NUL name");
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
std::string basename(std::string name){
    std::replace(name.begin(),name.end(),'\\','/');
    const auto at=name.find_last_of('/');return fold(at==std::string::npos?name:name.substr(at+1));
}
struct Archive {
    std::string path;
    BufferedResourceFile stream;
    std::uint64_t size;
    std::shared_ptr<const std::vector<odh::FarEntry>> entries;
    std::vector<std::size_t> full_name_order;
    explicit Archive(const std::string& filename):path(filename),stream(filename),size(0) {
        bounded_name(path);if(!stream)throw odh::FormatError("saved appearance cannot open FAR");
        size=std::filesystem::file_size(path);odh::Limits lim;lim.far_entries=8192;lim.far_name_bytes=256*1024;
        entries=shared_far_manifest(path,odh::View(stream,size),lim);
        full_name_order=odh_host::appearance_detail::index_full_names(*entries);
    }
    const odh::FarEntry& select(const std::string& name) const {
        return odh_host::appearance_detail::select_full_name(*entries,full_name_order,name);
    }
    odh::View view(const odh::FarEntry& e){return odh::View(stream,size).subview(e.offset,e.length);}
};
struct Named {std::string entry,name;Archive* archive=nullptr;};
struct AppearanceResource {std::string appearance,texture_map;};
AppearanceResource body_resource(const std::string& raw,const char* kind) {
    try { bounded_name(raw); }
    catch(const odh::FormatError& error) {
        throw odh::FormatError(std::string("BodySTR ")+kind+" reference length="+std::to_string(raw.size())+
            " empty="+std::to_string(raw.empty())+" embedded_nul="+std::to_string(raw.find('\0')!=std::string::npos)+": "+error.what());
    }
    const auto comma=raw.find(',');
    AppearanceResource out{raw.substr(0,comma),comma==std::string::npos?std::string{}:raw.substr(comma+1)};
    bounded_name(out.appearance);
    if(out.appearance.find_first_of("=;")!=std::string::npos)
        throw odh::FormatError(std::string("BodySTR ")+kind+" appearance syntax invalid");
    if(comma!=std::string::npos) {
        if(out.texture_map.empty()||out.texture_map.find_first_of(",;")!=std::string::npos)
            throw odh::FormatError(std::string("BodySTR ")+kind+" requires one texture binding");
        texture_override(out.texture_map);
    }
    return out;
}
}
SavedAnimationAppearance saved_animation_appearance(const std::string& raw,const odh::AnimationHeader& header) {
    const auto parsed=odh::parse_original_animation_record(raw);
    if(fold(parsed.name)!=fold(header.name))throw odh::FormatError("saved animation header name mismatch");
    SavedAnimationAppearance out;out.raw=raw;out.header=header;out.priority=parsed.priority;out.speed_milli=parsed.speed;
    out.normalized_frame=parsed.normalized_frame;out.weight_milli=parsed.weight;out.unknown1=parsed.unknown1;out.unknown2=parsed.unknown2;
    std::uint32_t frames=0;for(const auto& motion:header.motions)frames=std::max(frames,motion.frames);
    if(!frames)throw odh::FormatError("saved animation has no frames");
    out.frame=static_cast<float>(frames)*(static_cast<float>(parsed.normalized_frame)/1000.0f);
    if(!std::isfinite(out.frame)||out.frame<static_cast<double>(INT32_MIN)||out.frame>static_cast<double>(INT32_MAX))throw odh::FormatError("saved animation frame exceeds native sampling budget");
    out.loop=parsed.loop;out.backwards=parsed.speed<0;out.end_reached=parsed.normalized_frame==(out.backwards?0:1000);return out;
}

struct SavedAppearanceResources::Impl {
    SceneConfig config;Archive animations,textures;
    std::map<std::string,std::vector<Named>> appearances,loose_appearance_names,clips,skeletons;
    std::filesystem::path skins_root;
    std::map<std::string,std::vector<std::filesystem::path>> skin_files;
    bool catalog_mode=false;
    SavedAppearanceCatalogDiagnostics scan_diagnostics;
    std::vector<std::unique_ptr<Archive>> extra_archives;
    std::map<std::string,Named> files;
    std::vector<std::string> file_order,appearance_order,skin_file_order;
    template<class Decode> auto decode(const Named& selected,Decode decoder){
        if(selected.archive)return decoder(selected.archive->view(selected.archive->select(selected.entry)));
        BufferedResourceFile input(selected.entry);if(!input)throw odh::FormatError("saved appearance loose resource unavailable");
        return decoder(odh::View(input,std::filesystem::file_size(selected.entry)));
    }
    void note_metadata_failure(const std::string& source,const odh::FormatError& error){
        ++scan_diagnostics.malformed_resources;
        if(scan_diagnostics.resource_errors.size()<16){
            auto message=source+": "+error.what();if(message.size()>512)message.resize(512);
            scan_diagnostics.resource_errors.push_back(std::move(message));
        }
    }
    Impl(const std::string& a,const std::string& t,const std::string& se,const std::string& sn,
         const std::string& skins,const std::vector<std::string>& sources,const std::vector<std::string>& loose_roots,bool metadata_only=false)
         :animations(a),textures(t) {
        config.animation_far=a;config.textures_far=t;
        if(!metadata_only){config.skeleton_entry=animations.select(se).name;config.skeleton_name=sn;bounded_name(sn);}
        catalog_mode=!sources.empty()||!loose_roots.empty();
        if(sources.size()>128||loose_roots.size()>16)throw odh::FormatError("saved appearance source count exceeds bound");
        std::uint64_t count=0,bytes=0,file_count=0,file_bytes=0;
        auto add=[&](auto& index,const std::string& name,const std::string& entry,Archive* archive=nullptr,bool retain_duplicates=false){bounded_name(name);
            if(++count>8192||(bytes+=name.size()+entry.size())>(catalog_mode?4u*1024u*1024u:256u*1024u))
                throw odh::FormatError("saved appearance named resource budget exceeded");
            const auto key=fold(name);
            if(&index==&appearances&&!index.count(key))appearance_order.push_back(key);
            auto& selected=index[key];
            // TS1Provider.BuildDictionary and TS1BCFProvider assign later
            // filename/name entries. The caller supplies a stable source order.
            if(catalog_mode&&!retain_duplicates)selected.clear();
            selected.push_back({entry,name,archive});};
        std::vector<Archive*> archives{&animations,&textures};
        std::set<std::filesystem::path> admitted{odh::hostfs::canonical(a),odh::hostfs::canonical(t)};
        for(const auto& source:sources){bounded_name(source);if(!admitted.insert(odh::hostfs::canonical(source)).second)continue;
            extra_archives.push_back(std::make_unique<Archive>(source));archives.push_back(extra_archives.back().get());}
        for(auto* archive:archives)for(const auto& entry:*archive->entries){
            const auto key=basename(entry.name);
            if(key.size()<4)continue;const auto ext=key.substr(key.size()-4);
            if(ext!=".bcf"&&ext!=".cmx"&&ext!=".bmf"&&ext!=".bmp"&&ext!=".skn")continue;
            if(++file_count>65536||(file_bytes+=entry.name.size()+key.size())>4u*1024u*1024u)
                throw odh::FormatError("saved appearance FAR resource index budget exceeded");
            if(!files.count(key))file_order.push_back(key);
            files[key]={entry.name,key,archive};
        }
        auto scan_bcf=[&](Archive& archive,const odh::FarEntry& entry){
            // Keep parser failures separate from index publication: a malformed
            // optional outfit must not swallow FAR/I/O or aggregate budgets.
            std::vector<std::string> parsed_skeletons,parsed_appearances,parsed_animations;
            try{
                const auto decoded=odh::read_bcf_resources(archive.view(entry));
                if(decoded.remaining_bytes)throw odh::FormatError("saved appearance trailing BCF bytes");
                for(const auto& rig:decoded.skeletons)parsed_skeletons.push_back(rig.name);
                for(const auto& appearance:decoded.appearances)parsed_appearances.push_back(appearance.name);
                // Playback owns the full animation catalog. Preserve this
                // legacy OBJM animation index's explicit base-archive scope.
                if(&archive==&animations)for(const auto& clip:decoded.animations)parsed_animations.push_back(clip.name);
            }catch(const odh::FormatError& error){
                if(!catalog_mode||archive.stream.fail()||archive.stream.bad())throw;
                note_metadata_failure(archive.path+" ["+entry.name+"]",error);
                return;
            }
            // Bounds and name validation remain fatal, outside parser recovery.
            for(const auto& name:parsed_skeletons)add(skeletons,name,entry.name,&archive,true);
            for(const auto& name:parsed_appearances)add(appearances,name,entry.name,&archive);
            for(const auto& name:parsed_animations)add(clips,name,entry.name,&archive);
        };
        if(!catalog_mode)for(const auto& entry:*animations.entries){const auto key=fold(entry.name);
            if(key.size()>=4&&key.substr(key.size()-4)==".bcf")scan_bcf(animations,entry);}
        std::vector<std::string> roots;if(!skins.empty())roots.push_back(skins);
        roots.insert(roots.end(),loose_roots.begin(),loose_roots.end());
        std::size_t loose_files=0,path_bytes=0;std::set<std::filesystem::path> scanned;
        std::vector<std::string> cmx_order;std::set<std::string> cmx_seen;
        for(const auto& root:roots){bounded_name(root);std::error_code error;const auto canonical=odh::hostfs::canonical(root,error);
            if(error||!std::filesystem::is_directory(canonical))throw odh::FormatError("saved appearance skins directory is not accessible");
            if(!scanned.insert(canonical).second)continue;
            if(skins_root.empty())skins_root=canonical;
            std::vector<std::filesystem::path> paths;
            for(const auto& item:std::filesystem::recursive_directory_iterator(canonical)){
                if(!item.is_regular_file())continue;const auto ext=fold(item.path().extension().string());
                if(ext!=".cmx"&&ext!=".skn"&&ext!=".bmp")continue;
                if(++loose_files>65536||(path_bytes+=item.path().string().size())>4u*1024u*1024u)
                    throw odh::FormatError("saved appearance loose skin index budget exceeded");
                paths.push_back(item.path());}
            std::sort(paths.begin(),paths.end());
            for(const auto& path:paths){const auto ext=fold(path.extension().string());
                const auto skin_key=fold(path.filename().string());
                if(!skin_files.count(skin_key))skin_file_order.push_back(skin_key);
                auto& group=skin_files[skin_key];if(catalog_mode)group.clear();group.push_back(path);
                if(ext==".cmx"){
                    if(catalog_mode){const auto key=basename(path.string());if(cmx_seen.insert(key).second)cmx_order.push_back(key);continue;}
                    odh::AppearanceCollection decoded;
                    try{BufferedResourceFile input(path);decoded=odh::read_cmx_appearances(odh::View(input,std::filesystem::file_size(path)));}
                    catch(const odh::FormatError& error){note_metadata_failure(path.string(),error);continue;}
                    for(const auto& appearance:decoded.appearances)add(loose_appearance_names,appearance.name,path.string());
                }
            }
        }
        if(catalog_mode){
            // TS1BCFProvider enumerates the ONE projected BCF/CMX provider in
            // registration order before indexing its internal appearance names.
            // First key insertion establishes clothing order; later hosts select
            // the actual payload. Do not scan all BCFs before all CMXs.
            std::vector<std::string> ordered=file_order;std::set<std::string> seen(ordered.begin(),ordered.end());
            for(const auto& key:cmx_order)if(seen.insert(key).second)ordered.push_back(key);
            for(const auto& key:ordered){
                if(key.size()<4)continue;const auto ext=key.substr(key.size()-4);
                if(ext==".bcf"){const auto& selected=files.at(key);scan_bcf(*selected.archive,selected.archive->select(selected.entry));continue;}
                if(ext!=".cmx")continue;
                const auto selected=file(animations,key);odh::AppearanceCollection decoded;
                try{decoded=decode(selected,[](const auto& view){return odh::read_cmx_appearances(view);});}
                catch(const odh::FormatError& error){
                    note_metadata_failure((selected.archive?selected.archive->path:std::string{"loose"})+" ["+selected.entry+"]",error);continue;
                }
                for(const auto& appearance:decoded.appearances)add(appearances,appearance.name,selected.entry,selected.archive);
            }
        }
    }
    Named file(Archive& legacy,const std::string& name){
        if(!catalog_mode)return {legacy.select(name).name,name,&legacy};
        const auto found=files.find(basename(name));
        if(found!=files.end())return found->second;
        const auto loose=skin_files.find(basename(name));
        if(loose!=skin_files.end()&&loose->second.size()==1)return {loose->second.front().string(),name,nullptr};
        throw odh::FormatError("saved appearance resource missing: "+name);
    }
    template<class Index> const Named& named(const Index& index,const std::string& name) {
        auto found=index.find(fold(name));if(found==index.end())throw odh::FormatError("saved appearance named resource missing: "+name);
        if(found->second.size()!=1)throw odh::FormatError("saved appearance named resource ambiguous: "+name);
        return found->second.front();
    }
    std::optional<Named> find_appearance(const std::string& name){
        const auto key=fold(name);
        if(appearances.count(key))return named(appearances,name);
        if(loose_appearance_names.count(key))return named(loose_appearance_names,name);
        // A legacy producer wrote filenames instead of declared suit names.
        // Preserve the selected resource and reject ambiguous containers.
        const auto cmx=key+".cmx",bcf=cmx+".bcf";
        const bool text=skin_files.count(cmx)||(catalog_mode&&files.count(cmx));
        const bool binary=files.count(bcf);
        if(text && binary)throw odh::FormatError("saved appearance legacy suit host ambiguous: "+name);
        if(text || binary){
            Named host;
            if(catalog_mode)host=file(animations,text?cmx:bcf);
            else if(binary){
                // The explicit legacy archive has no cross-directory
                // filename precedence. A stem must identify one entry.
                bool matched=false;
                for(const auto& entry:*animations.entries)if(basename(entry.name)==bcf){
                    if(matched)throw odh::FormatError("saved appearance legacy suit host ambiguous: "+name);
                    host={entry.name,{},&animations};
                    matched=true;
                }
                if(!matched)return std::nullopt;
            }
            else {
                const auto& paths=skin_files.at(cmx);
                if(paths.size()!=1)throw odh::FormatError("saved appearance legacy suit host ambiguous: "+name);
                host={paths.front().string(),{},nullptr};
            }
            auto suits=decode(host,[&](const auto& view){
                if(text)return odh::read_cmx_appearances(view).appearances;
                auto result=odh::read_bcf_resources(view);
                if(result.remaining_bytes)throw odh::FormatError("saved appearance trailing selected BCF bytes");
                return result.appearances;
            });
            if(suits.size()>1)throw odh::FormatError("saved appearance legacy suit host ambiguous: "+name);
            if(suits.size()==1){
                const auto declared=fold(suits.front().name);
                const auto& index=appearances.count(declared)?appearances:loose_appearance_names;
                const auto& selected=named(index,declared);
                if(selected.archive==host.archive && selected.entry==host.entry)return selected;
                throw odh::FormatError("saved appearance legacy suit host shadowed: "+name);
            }
        }
        return std::nullopt;
    }
    Named appearance_named(const std::string& name){
        if(auto host=find_appearance(name))return *host;
        throw odh::FormatError("saved appearance named resource missing: "+name);
    }
    std::filesystem::path loose_file(const std::string& host,const std::string& filename){
        bounded_name(filename);const auto key=fold(filename);const auto sibling=std::filesystem::path(host).parent_path();
        std::optional<std::filesystem::path> selected;
        std::error_code error;
        if(std::filesystem::is_directory(sibling,error))for(const auto& item:std::filesystem::directory_iterator(sibling))
            if(item.is_regular_file()&&fold(item.path().filename().string())==key){
                if(selected)throw odh::FormatError("saved appearance loose sibling resource ambiguous: "+filename);
                selected=item.path();}
        if(selected)return *selected;
        const auto found=skin_files.find(key);
        if(found==skin_files.end())throw odh::FormatError("saved appearance loose resource missing: "+filename);
        if(found->second.size()!=1)throw odh::FormatError("saved appearance loose resource ambiguous: "+filename);
        return found->second.front();
    }
    odh::AppearanceCollection loose_appearances(const std::string& path){
        BufferedResourceFile input(path);if(!input)throw odh::FormatError("saved appearance cannot open CMX: "+path);
        return odh::read_cmx_appearances(odh::View(input,std::filesystem::file_size(path)));
    }
    odh::Mesh loose_mesh(const std::string& path){
        BufferedResourceFile input(path);if(!input)throw odh::FormatError("saved appearance cannot open SKN: "+path);
        auto mesh=odh::read_skn(odh::View(input,std::filesystem::file_size(path)));
        if(mesh.remaining_bytes)throw odh::FormatError("saved appearance trailing SKN bytes");
        return mesh;
    }
    Named mesh_file(const Named& host,const std::string& name){
        if(!catalog_mode&& !host.archive)return {loose_file(host.entry,name+".skn").string(),name,nullptr};
        if(catalog_mode){
            // TS1BMFProvider prefers the compiled mesh to SKN across all sources.
            if(files.count(basename(name+".bmf"))||skin_files.count(basename(name+".bmf")))return file(animations,name+".bmf");
            return file(animations,name+".skn");
        }
        return file(animations,name+".bmf");
    }
    Named texture_file(const Named& host,const std::string& name){
        const auto filename=odh::ts1_bmp_filename(name);
        if(!catalog_mode&&!host.archive)return {loose_file(host.entry,filename).string(),name,nullptr};
        return file(textures,filename);
    }
    odh::Mesh mesh(const Named& resource){
        return decode(resource,[&](const auto& view){auto result=basename(resource.entry).size()>=4&&
            basename(resource.entry).substr(basename(resource.entry).size()-4)==".skn"?
            odh::read_skn(view):odh::read_bmf(view);
            if(result.remaining_bytes)throw odh::FormatError("saved appearance trailing mesh bytes");return result;});
    }
    void part(SceneConfig& out,const std::string& name,const std::string& raw_texture,
              std::optional<SceneHandPart> hand={}) {
        bounded_name(name);const auto host=appearance_named(name);
        const auto filename=basename(host.entry);const bool cmx=filename.size()>=4&&filename.substr(filename.size()-4)==".cmx";
        const auto collection=decode(host,[&](const auto& view){
            if(cmx)return odh::read_cmx_appearances(view);
            auto decoded=odh::read_bcf_resources(view);
            if(decoded.remaining_bytes)throw odh::FormatError("saved appearance trailing selected BCF bytes");
            return odh::AppearanceCollection{std::move(decoded.appearances),0};
        });
        const odh::Appearance* appearance=nullptr;
        for(const auto& candidate:collection.appearances)if(fold(candidate.name)==fold(host.name)){
            if(appearance)throw odh::FormatError("saved appearance selected name is ambiguous");appearance=&candidate;}
        if(!appearance||appearance->bindings.empty()||appearance->bindings.size()>16)
            throw odh::FormatError("saved appearance binding count unsupported");
        const auto override=texture_override(raw_texture);std::vector<Named> meshes;
        std::vector<std::string> defaults;std::size_t replaced=0;
        for(std::size_t i=0;i<appearance->bindings.size();++i){const auto& binding=appearance->bindings[i];bounded_name(binding.mesh_name);
            const auto selected=mesh_file(host,binding.mesh_name);const auto decoded=mesh(selected);
            // TS1BMFProvider addresses the binding's file name. SkinName is
            // exporter metadata: shipped BMFs can omit an attachment token in
            // that field while their selected file and binding match exactly.
            const auto selected_name=basename(selected.entry);
            if(selected_name!=basename(binding.mesh_name+".bmf")&&selected_name!=basename(binding.mesh_name+".skn"))
                throw odh::FormatError("saved appearance selected mesh file differs from binding");
            if(override&&odh::ts1_override_matches(decoded.texture_name,*override))replaced=i;
            meshes.push_back(selected);defaults.push_back(decoded.texture_name);}
        for(std::size_t i=0;i<meshes.size();++i){const auto texture_name=override&&i==replaced?*override:defaults[i];bounded_name(texture_name);
            const auto texture=texture_file(host,texture_name);
            if(out.parts.size()>=16)throw odh::FormatError("saved appearance cumulative 16-part limit exceeded");
            out.parts.push_back({host.entry,host.name,meshes[i].entry,texture.entry,hand,cmx,
                host.archive?host.archive->path:std::string{},meshes[i].archive?meshes[i].archive->path:std::string{},
                texture.archive?texture.archive->path:std::string{},!meshes[i].archive,!texture.archive});}
    }
    void accessory(SceneConfig& out,const odh::ObjmSavedAccessory& accessory) {
        bounded_name(accessory.name.bytes);if(!accessory.binding.bytes.empty())throw odh::FormatError("saved appearance accessory binding unsupported");
        auto name=accessory.name.bytes;constexpr std::string_view suffix=".apr";
        if(name.size()<=suffix.size()||fold(name.substr(name.size()-suffix.size()))!=suffix)
            throw odh::FormatError("saved appearance accessory requires an .apr name");
        name.resize(name.size()-suffix.size());part(out,name,{});
    }
    void hands(SceneConfig& out,const std::array<AppearanceResource,6>& variants) {
        for(std::size_t i=0;i<variants.size();++i) {
            if(variants[i].appearance.empty())continue;
            const bool right=(i%2)==1;
            // Pinned FreeSO TS1 ReloadHandgroup takes right pointing/fist
            // texture from RightHand.Idle but keeps each gesture's appearance.
            part(out,variants[i].appearance,right?variants[1].texture_map:variants[i].texture_map,
                 SceneHandPart{right,static_cast<odh::HandGesture>(i/2)});
        }
    }
    std::optional<SavedAnimationAppearance> animation(const std::string& raw) {
        if(raw.empty())return {};
        bounded_name(raw);auto semi=raw.find(';');if(semi==std::string::npos)throw odh::FormatError("saved animation missing fields");
        auto host=named(clips,raw.substr(0,semi));auto decoded=odh::read_bcf_animations(animations.view(animations.select(host.entry)));
        for(const auto& c:decoded.animations)if(c.name==host.name){auto out=saved_animation_appearance(raw,c);out.entry=host.entry;out.cfp_entry=animations.select(c.xskill+".cfp").name;return out;}
        throw odh::FormatError("saved animation indexed header missing");
    }
    SavedAppearance body_head_animation(const odh::ObjmPersonState& person) {
        if(person.accessory_count<0||static_cast<std::size_t>(person.accessory_count)!=person.accessories.size()||person.accessories.size()>16)
            throw odh::FormatError("saved appearance accessory count outside supported profile");
        SavedAppearance out;out.scene=config;
        part(out.scene,person.body.bytes,person.body_texture.bytes);part(out.scene,person.head.bytes,person.head_texture.bytes);
        for(const auto& item:person.accessories)accessory(out.scene,item);
        out.current=animation(person.animation.bytes);out.base=animation(person.base_animation.bytes);out.carry=animation(person.carry_animation.bytes);
        return out;
    }
};
SavedAppearanceResources::SavedAppearanceResources(const std::string& a,const std::string& t,const std::string& se,const std::string& sn,
                                                   const std::string& skins,const std::vector<std::string>& sources,const std::vector<std::string>& loose_roots)
    :impl_(std::make_unique<Impl>(a,t,se,sn,skins,sources,loose_roots)){}
SavedAppearanceResources::~SavedAppearanceResources()=default;
SavedAppearanceResources::SavedAppearanceResources(SavedAppearanceResources&&) noexcept=default;
SavedAppearanceResources& SavedAppearanceResources::operator=(SavedAppearanceResources&&) noexcept=default;
const SavedAppearanceCatalogDiagnostics& SavedAppearanceResources::diagnostics() const {
    if(!impl_)throw odh::FormatError("saved appearance resolver used after move");
    return impl_->scan_diagnostics;
}
SavedAppearance SavedAppearanceResources::resolve(const odh::ObjmPersonState& person) {
    if(!impl_)throw odh::FormatError("saved appearance resolver used after move");
    auto out=impl_->body_head_animation(person);
    impl_->part(out.scene,person.left_hand.bytes,person.left_hand_texture.bytes);impl_->part(out.scene,person.right_hand.bytes,person.right_hand_texture.bytes);
    return out;
}
SceneConfig SavedAppearanceResources::resolve_body(const odh::AvatarBodyStrings& body) {
    if(!impl_)throw odh::FormatError("saved appearance resolver used after move");
    const auto outfit=body_resource(body.body,"body"),head=body_resource(body.head,"head");
    std::array<AppearanceResource,6> variants;
    for(std::size_t i=0;i<variants.size();++i) {
        // Outfit.ToApr("") becomes ".apr", whose empty name has no content.
        // ReloadHandgroup removes the previous hand and adds only a present
        // selected appearance; an authored empty gesture has no geometry.
        if(body.hands[i].empty())continue;
        const auto field="hand"+std::to_string(17+i);
        variants[i]=body_resource(body.hands[i],field.c_str());
    }
    auto out=impl_->config;
    impl_->part(out,outfit.appearance,outfit.texture_map);
    impl_->part(out,head.appearance,head.texture_map);
    impl_->hands(out,variants);
    return out;
}
SceneConfig SavedAppearanceResources::resolve_human_body(const odh::AvatarBodyStrings& body) {
    if(!impl_)throw odh::FormatError("saved appearance resolver used after move");
    if(body.avatar_type!="adult"&&body.avatar_type!="child")
        throw odh::FormatError("human appearance requires adult or child BodySTR type");
    const auto entry=impl_->animations.select(body.avatar_type+"-skeleton.cmx.bcf").name;
    auto scene=resolve_body(body);
    scene.skeleton_entry=entry;scene.skeleton_name=body.avatar_type;
    return scene;
}
SceneConfig SavedAppearanceResources::resolve_avatar_body(const odh::AvatarBodyStrings& body) {
    if(!impl_)throw odh::FormatError("saved appearance resolver used after move");
    const auto type=odh::avatar_body_type(body.avatar_type);
    if(!type)throw odh::FormatError("avatar appearance unsupported BodySTR type: "+body.avatar_type.substr(0,32));
    if(odh::human_avatar_body_type(*type))return resolve_human_body(body);
    std::string stage="rig",rig_source="unselected",part;
    try{
        // TS1BCFProvider indexes the internal skeleton name, not an inferred
        // filename. Selected-file precedence is already owned by this catalog;
        // duplicate internal names remain ambiguous rather than replacing a rig.
        const auto candidates=impl_->skeletons.find(fold(body.avatar_type));
        if(candidates!=impl_->skeletons.end()&&candidates->second.size()!=1){
            rig_source="ambiguous";
            for(std::size_t i=0;i<candidates->second.size()&&i<2;++i){
                const auto& candidate=candidates->second[i];
                rig_source+=" "+(candidate.archive?candidate.archive->path:std::string{"loose"})+" ["+candidate.entry+"]";
            }
        }
        const auto& rig=impl_->named(impl_->skeletons,body.avatar_type);
        if(!rig.archive)throw odh::FormatError("avatar skeleton requires an indexed FAR source");
        rig_source=rig.archive->path+" ["+rig.entry+":"+rig.name+"]";
        auto scene=impl_->config;
        scene.skeleton_far=rig.archive->path;scene.skeleton_entry=rig.entry;scene.skeleton_name=rig.name;
        stage="body";part=body.body;
        const auto outfit=body_resource(body.body,"body");
        const auto& body_host=impl_->appearance_named(outfit.appearance);
        part=outfit.appearance+" @"+(body_host.archive?body_host.archive->path:std::string{"loose"})+" ["+body_host.entry+"]";
        impl_->part(scene,outfit.appearance,outfit.texture_map);
        if(!body.head.empty()){
            stage="head";part=body.head;
            const auto head=body_resource(body.head,"head");
            if(auto head_host=impl_->find_appearance(head.appearance)){
                part=head.appearance+" @"+(head_host->archive?head_host->archive->path:std::string{"loose"})+" ["+head_host->entry+"]";
                impl_->part(scene,head.appearance,head.texture_map);
            }else if(impl_->scan_diagnostics.malformed_resources){
                throw odh::FormatError("animal head appearance absence is unproved: incomplete BCF/CMX name index");
            }
            // SimAvatar.ReloadHead adds nothing when the source appearance
            // lookup returns null. Only complete name-index absence reaches
            // this no-op; selected malformed/ambiguous parts still fail above.
        }
        // VMAvatar.BodyOutfit assigns Handgroup only for Adult/Child. Animal
        // hand strings are not parsed or substituted with human default hands.
        return scene;
    }catch(const odh::FormatError& error){
        throw odh::FormatError("avatar appearance species="+body.avatar_type+" requested_rig="+body.avatar_type+
            " stage="+stage+" selected_rig="+rig_source.substr(0,768)+" part="+part.substr(0,256)+
            " catalog_malformed="+std::to_string(impl_->scan_diagnostics.malformed_resources)+": "+error.what());
    }
}
SceneConfig SavedAppearanceResources::resolve_live_portrait(const std::string& skeleton,
        const std::string& body,const std::string& head,const std::array<std::string,6>& hands,
        const std::vector<std::string>& accessories){
    if(!impl_)throw odh::FormatError("saved appearance resolver used after move");
    if(!odh::avatar_body_type(skeleton))throw odh::FormatError("live portrait unsupported current skeleton");
    const auto& rig=impl_->named(impl_->skeletons,skeleton);
    if(!rig.archive)throw odh::FormatError("live portrait skeleton requires an indexed FAR source");
    auto scene=impl_->config;
    scene.skeleton_far=rig.archive->path;scene.skeleton_entry=rig.entry;scene.skeleton_name=rig.name;
    for(const auto* raw:{&body,&head})if(!raw->empty()){
        const auto part=body_resource(*raw,"live portrait");
        impl_->part(scene,part.appearance,part.texture_map);
    }
    std::array<AppearanceResource,6> variants;
    for(size_t i=0;i<6;++i)if(!hands[i].empty())variants[i]=body_resource(hands[i],"live hand");
    impl_->hands(scene,variants);
    append_accessories(scene,accessories);
    return scene;
}
void SavedAppearanceResources::append_accessories(SceneConfig& scene,const std::vector<std::string>& names) {
    if(!impl_)throw odh::FormatError("saved appearance resolver used after move");
    if(names.size()>16)throw odh::FormatError("runtime avatar accessory count exceeds 16-name limit");
    for(const auto& name:names){
        bounded_name(name);
        constexpr std::string_view suffix=".apr";
        if(name.size()<suffix.size()||fold(name.substr(name.size()-suffix.size()))!=suffix)
            throw odh::FormatError("runtime avatar accessory requires an .apr name");
        const auto key=fold(name.substr(0,name.size()-suffix.size()));
        // TS1 preserves BoundAppearances even when content.Get returns null.
        // Only a proven index miss is a visual no-op; selected malformed or
        // ambiguous resources still take the ordinary strict decoder path.
        if(impl_->appearances.find(key)==impl_->appearances.end()&&
           impl_->loose_appearance_names.find(key)==impl_->loose_appearance_names.end())continue;
        odh::ObjmSavedAccessory item{};item.name.bytes=name;
        impl_->accessory(scene,item);
    }
}
SavedAppearance SavedAppearanceResources::resolve_gestures(const odh::ObjmPersonState& person,const odh::AvatarBodyStrings& body) {
    if(!impl_)throw odh::FormatError("saved appearance resolver used after move");
    std::array<AppearanceResource,6> variants;
    for(std::size_t i=0;i<variants.size();++i) {
        // Outfit.ToApr("") becomes ".apr", whose empty name has no content.
        // ReloadHandgroup removes the previous hand and adds only a present
        // selected appearance; an authored empty gesture has no geometry.
        if(body.hands[i].empty())continue;
        const auto field="hand"+std::to_string(17+i);
        variants[i]=body_resource(body.hands[i],field.c_str());
    }
    auto out=impl_->body_head_animation(person);
    impl_->hands(out.scene,variants);
    return out;
}

SavedAppearanceResourceCatalog SavedAppearanceResources::resource_catalog(
 const std::filesystem::path& game_root,const std::string& animation_far,const std::string& textures_far,
 const std::string& skins_directory,const std::vector<std::string>& sources,const std::vector<std::string>& loose_roots){
 if(sources.empty()&&loose_roots.empty())throw odh::FormatError("wardrobe metadata requires the admitted catalog-mode provider");
 Impl provider(animation_far,textures_far,{},{},skins_directory,sources,loose_roots,true);
 const auto root=odh::hostfs::canonical(game_root);
 SavedAppearanceResourceCatalog catalog;auto& result=catalog.assets;
 std::map<std::pair<odh::WardrobeAssetKind,std::string>,std::uint32_t> ordinals;
 std::size_t text_bytes=0;std::uint64_t content_bytes=0;
 std::map<std::pair<std::string,std::string>,std::array<std::uint8_t,32>> hashes;
 const auto add=[&](odh::WardrobeAssetKind kind,const std::string& name,const Named& selected){
  const auto path=odh::hostfs::canonical(selected.archive?selected.archive->path:selected.entry);
  const auto relative=path.lexically_relative(root);
  if(relative.empty()||odh::hostfs::is_absolute(relative)||*relative.begin()=="..")throw odh::FormatError("wardrobe selected resource escaped trusted game root");
  const auto source=relative.generic_string(),asset=selected.archive?selected.entry:std::string{};
  const auto growth=name.size()+source.size()+asset.size();
  if(name.empty()||name.size()>256||source.size()>1024||asset.size()>1024||result.size()>=32768||growth>16u*1024u*1024u-text_bytes)
   throw odh::FormatError("wardrobe selected provider metadata exceeds bounds");
  text_bytes+=growth;const auto key=std::make_pair(source,asset);auto hash=hashes.find(key);
  if(hash==hashes.end()){
   const auto digest=provider.decode(selected,[&](const odh::View& view){
    if(view.size()>16u*1024u*1024u||view.size()>512u*1024u*1024u-content_bytes)throw odh::FormatError("wardrobe selected content census exceeds bounds");
    content_bytes+=view.size();std::string bytes(static_cast<std::size_t>(view.size()),'\0');view.read(0,bytes.data(),bytes.size());return odh::sha256(bytes.data(),bytes.size());
   });hash=hashes.emplace(key,digest).first;
  }
  ordinals.emplace(std::make_pair(kind,fold(name)),static_cast<std::uint32_t>(result.size()));
  result.push_back({kind,name,source,asset,hash->second,static_cast<std::uint64_t>(result.size())});
 };
 for(const auto& names:provider.appearances){
  const auto& selected=provider.named(provider.appearances,names.first);
  add(odh::WardrobeAssetKind::appearance,selected.name,selected);
 }
 // The same file() selects FAR winners before loose files, just as texture_file().
 std::set<std::string> textures;
 for(const auto& item:provider.files)if(item.first.size()>=4&&item.first.substr(item.first.size()-4)==".bmp")textures.insert(item.first);
 for(const auto& item:provider.skin_files)if(item.first.size()>=4&&item.first.substr(item.first.size()-4)==".bmp")textures.insert(item.first);
 for(const auto& name:textures)add(odh::WardrobeAssetKind::texture,name.substr(0,name.size()-4),provider.file(provider.textures,name));
 for(const auto& name:provider.appearance_order)catalog.appearance_order.push_back(ordinals.at({odh::WardrobeAssetKind::appearance,name}));
 std::set<std::string> texture_seen;
 const auto add_texture_order=[&](const std::string& name){
  if(name.size()<4||name.substr(name.size()-4)!=".bmp"||!texture_seen.insert(name).second)return;
  catalog.texture_order.push_back(ordinals.at({odh::WardrobeAssetKind::texture,name.substr(0,name.size()-4)}));
 };
 for(const auto& name:provider.file_order)add_texture_order(name);
 for(const auto& name:provider.skin_file_order)add_texture_order(name);
 if(catalog.appearance_order.size()+catalog.texture_order.size()!=result.size())throw odh::FormatError("character catalog order does not cover selected resource names");
 return catalog;
}
std::vector<odh::WardrobeCatalogAsset> SavedAppearanceResources::wardrobe_catalog(
 const std::filesystem::path& game_root,const std::string& animation_far,const std::string& textures_far,
 const std::string& skins_directory,const std::vector<std::string>& sources,const std::vector<std::string>& loose_roots){
 return resource_catalog(game_root,animation_far,textures_far,skins_directory,sources,loose_roots).assets;
}
