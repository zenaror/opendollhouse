#include "linux_cas_catalog.hpp"

#include "odh/formats.hpp"
#include "odh/mesh.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace odh_host {
namespace {
std::string lower(std::string value) {
    for (auto& ch : value) if (ch >= 'A' && ch <= 'Z') ch=static_cast<char>(ch+('a'-'A'));
    return value;
}

bool safe_code(const std::string& value, std::size_t minimum, std::size_t maximum) {
    if (value.size()<minimum||value.size()>maximum) return false;
    return std::all_of(value.begin(),value.end(),[](unsigned char ch){
        return (ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-';
    });
}

std::string filename_without_extension(const std::string& name) {
    const auto dot=name.find_last_of('.');
    return dot==std::string::npos?name:name.substr(0,dot);
}

std::string archive_basename(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    const auto slash = name.find_last_of('/');
    if (slash != std::string::npos) name.erase(0, slash + 1);
    return name;
}

std::string texture_prefix(std::string appearance,const std::string& skin) {
    const auto underscore=appearance.find('_');
    if (underscore!=std::string::npos) appearance.resize(underscore);
    return appearance+skin;
}

void add_appearance(std::vector<LinuxCasCatalog::AppearanceName>& appearances,
                    const std::string& raw_name) {
    auto name=lower(raw_name);
    if (name.size()<4||(name[0]!='b'&&name[0]!='c')||
        !std::all_of(name.begin()+1,name.begin()+4,[](unsigned char ch){return ch>='0'&&ch<='9';}))
        return;
    auto end=name.find('_',4);
    if (end==std::string::npos) end=name.size();
    auto type=name.substr(4,end-4);
    for (const auto* skin: {"lgt","med","drk"})
        if (type.size()>=3&&type.compare(type.size()-3,3,skin)==0) { type.resize(type.size()-3); break; }
    appearances.push_back({name[0],type,name});
    if (name[0]=='b') {
        for (const auto* body_type:{"fat","fit","skn"})
            if (type.size()>=3&&type.compare(type.size()-3,3,body_type)==0) {
                appearances.push_back({name[0],type.substr(0,type.size()-3),name});
                break;
            }
    }
}

void read_cmx_file(std::vector<LinuxCasCatalog::AppearanceName>& appearances,
                   const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if (!input) throw odh::FormatError("cannot open CAS CMX asset");
    const odh::View view(input,std::filesystem::file_size(path));
    for (const auto& appearance:odh::read_cmx_appearances(view).appearances)
        add_appearance(appearances,appearance.name);
}

void read_cmx_far_entry(std::vector<LinuxCasCatalog::AppearanceName>& appearances,
                        const odh::View& archive,
                        const odh::FarEntry& entry) {
    auto stream_view=archive.subview(entry.offset,entry.length);
    for (const auto& appearance:odh::read_cmx_appearances(stream_view).appearances)
        add_appearance(appearances,appearance.name);
}

void add_texture_name(std::vector<std::string>& textures,const std::string& name) {
    const auto normalized=lower(name);
    const auto dot=normalized.find_last_of('.');
    if (dot==std::string::npos) return;
    const auto extension=normalized.substr(dot);
    if (extension==".bmp"||extension==".tga") textures.push_back(normalized);
}

std::vector<LinuxCasOutfitChoice> choices(const std::vector<LinuxCasCatalog::AppearanceName>& appearances,
        const std::vector<std::string>& textures,char category,
        const std::string& avatar_type,const std::string& skin,bool is_body) {
    if (!safe_code(avatar_type,2,8)||
        (skin!="lgt"&&skin!="med"&&skin!="drk"))
        throw std::invalid_argument("CAS outfit query has an invalid avatar type or skin code");
    std::set<std::string> names;
    for (const auto& item:appearances) {
        if (item.category!=category) continue;
        const bool selected=is_body?item.collection_type.rfind(avatar_type,0)==0:
                                    item.collection_type==avatar_type;
        if (selected) names.insert(item.name);
    }
    std::vector<LinuxCasOutfitChoice> result;
    for (const auto& name:names) {
        const auto wanted=texture_prefix(name,skin);
        auto texture=std::find_if(textures.begin(),textures.end(),
            [&](const auto& candidate){return candidate.rfind(wanted,0)==0;});
        if (texture==textures.end()&&is_body) {
            const auto fallback=texture_prefix(name,"");
            texture=std::find_if(textures.begin(),textures.end(),
                [&](const auto& candidate){return candidate.rfind(fallback,0)==0;});
        }
        if (texture==textures.end()) continue;
        std::string body_type;
        std::string handgroup;
        if (is_body) {
            const auto underscore=name.find('_');
            if (underscore==std::string::npos||underscore<3) continue;
            body_type=name.substr(underscore-3,3);
            const auto suffix=name.substr(underscore);
            const auto desired_hand="huao"+suffix;
            const auto found_hand=std::find_if(textures.begin(),textures.end(),
                [&](const auto& candidate){return candidate==desired_hand+".bmp"||candidate==desired_hand+".tga";});
            handgroup=found_hand==textures.end()?skin:suffix;
        }
        result.push_back({name,filename_without_extension(*texture),body_type,handgroup});
    }
    return result;
}
} // namespace

std::vector<LinuxCasOutfitChoice> LinuxCasCatalog::heads(
        const std::string& avatar_type,const std::string& skin) const {
    return choices(appearances_,textures_,'c',avatar_type,skin,false);
}

std::vector<LinuxCasOutfitChoice> LinuxCasCatalog::bodies(
        const std::string& avatar_type,const std::string& skin) const {
    return choices(appearances_,textures_,'b',avatar_type,skin,true);
}

LinuxCasCatalog scan_linux_cas_catalog(const std::filesystem::path& game_root) {
    std::error_code error;
    const auto root=std::filesystem::canonical(game_root,error);
    if (error||!std::filesystem::is_directory(root))
        throw odh::FormatError("CAS catalog game root is not an accessible directory");
    LinuxCasCatalog result;
    std::set<std::filesystem::path> metadata_roots;
    for (const auto& relative:{std::filesystem::path("GameData/Skins"),
                               std::filesystem::path("ExpansionShared/SkinsBuy"),
                               std::filesystem::path("Downloads")}) {
        const auto path=root/relative;
        if (std::filesystem::is_directory(path)) metadata_roots.insert(path);
    }
    for (const auto& entry:std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        const auto name=lower(entry.path().filename().string());
        if (name.rfind("expansion",0)==0) {
            const auto skins=entry.path()/"Skins";
            if (std::filesystem::is_directory(skins)) metadata_roots.insert(skins);
        }
    }
    std::vector<std::filesystem::path> loose_files;
    for (const auto& directory:metadata_roots) {
        for (const auto& entry:std::filesystem::recursive_directory_iterator(directory))
            if (entry.is_regular_file()) loose_files.push_back(entry.path());
    }
    std::sort(loose_files.begin(),loose_files.end());
    for (const auto& path:loose_files) {
        const auto ext=lower(path.extension().string());
        if (ext==".bmp"||ext==".tga") add_texture_name(result.textures_,path.filename().string());
        else if (ext==".cmx") {
            if (result.stats_.loose_cmx_files==std::numeric_limits<std::uint32_t>::max())
                throw odh::FormatError("too many loose CAS CMX files");
            ++result.stats_.loose_cmx_files;
            try { read_cmx_file(result.appearances_,path); }
            catch (const std::exception&) { ++result.stats_.skipped_appearance_files; }
        }
    }

    std::vector<std::filesystem::path> archives;
    for (const auto& entry:std::filesystem::recursive_directory_iterator(root))
        if (entry.is_regular_file()&&lower(entry.path().extension().string())==".far")
            archives.push_back(entry.path());
    std::sort(archives.begin(),archives.end());
    for (const auto& path:archives) {
        if (result.stats_.far_archives==std::numeric_limits<std::uint32_t>::max())
            throw odh::FormatError("too many CAS FAR archives");
        ++result.stats_.far_archives;
        std::ifstream input(path,std::ios::binary);
        if (!input) { ++result.stats_.skipped_appearance_files; continue; }
        const odh::View archive(input,std::filesystem::file_size(path));
        const auto entries=odh::far1_index(archive);
        for (const auto& entry:entries) {
            const auto base=archive_basename(entry.name);
            const auto ext=lower(std::filesystem::path(base).extension().string());
            if (ext==".bmp"||ext==".tga") add_texture_name(result.textures_,base);
            else if (ext==".cmx") {
                if (result.stats_.far_appearance_entries==std::numeric_limits<std::uint32_t>::max())
                    throw odh::FormatError("too many CAS appearance entries in FAR archives");
                ++result.stats_.far_appearance_entries;
                try { read_cmx_far_entry(result.appearances_,archive,entry); }
                catch (const std::exception&) { ++result.stats_.skipped_appearance_files; }
            }
        }
    }
    std::sort(result.appearances_.begin(),result.appearances_.end(),[](const auto& a,const auto& b){
        if(a.category!=b.category)return a.category<b.category;
        if(a.collection_type!=b.collection_type)return a.collection_type<b.collection_type;
        return a.name<b.name;
    });
    result.appearances_.erase(std::unique(result.appearances_.begin(),result.appearances_.end(),
        [](const auto& a,const auto& b){return a.category==b.category&&a.collection_type==b.collection_type&&a.name==b.name;}),result.appearances_.end());
    std::sort(result.textures_.begin(),result.textures_.end());
    result.textures_.erase(std::unique(result.textures_.begin(),result.textures_.end()),result.textures_.end());
    if (result.textures_.size()>std::numeric_limits<std::uint32_t>::max())
        throw odh::FormatError("too many CAS texture names");
    result.stats_.texture_names=static_cast<std::uint32_t>(result.textures_.size());
    return result;
}

} // namespace odh_host
