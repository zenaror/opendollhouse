// SPDX-License-Identifier: MPL-2.0
#include "odh/host_filesystem.hpp"
#include "reference_patch_setup.hpp"
#include "odh/sha256.hpp"
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>
namespace odh_host {
namespace {
namespace fs=std::filesystem;
constexpr const char* relative_pack="resource-patches/reference-v1";
constexpr const char* selection_text="OpenDollhouse reference patches 1\nresource-patches/reference-v1\n";
[[noreturn]] void fail(const std::string& why){throw std::runtime_error("Reference patches: "+why);}
bool present(const fs::path& p){return odh::hostfs::symlink_status(p).type()!=fs::file_type::not_found;}
void directory(const fs::path& p){
    if(present(p)){
        if(odh::hostfs::symlink_status(p).type()!=fs::file_type::directory)fail("expected a real directory: "+p.string());
    }else if(!fs::create_directory(p))fail("cannot create directory: "+p.string());
}
std::string read(const fs::path& p,std::size_t size){
    if(odh::hostfs::symlink_status(p).type()!=fs::file_type::regular || fs::file_size(p)!=size)
        fail("missing file or unexpected size: "+p.string());
    std::ifstream in(p,std::ios::binary);if(!in)fail("cannot read "+p.string());
    std::string bytes(size,'\0');in.read(bytes.data(),std::streamsize(size));
    if(!in || in.peek()!=std::char_traits<char>::eof())fail("incomplete or changed file: "+p.string());
    return bytes;
}
std::string digest(const std::string& b){
    const auto hash=odh::sha256(b.data(),b.size());std::string out;
    for(auto v:hash){out+="0123456789abcdef"[v>>4];out+="0123456789abcdef"[v&15];}return out;
}
void write(const fs::path& p,const std::string& b){
    std::ofstream out(p,std::ios::binary|std::ios::trunc);if(!out)fail("cannot write "+p.string());
    out.write(b.data(),std::streamsize(b.size()));out.close();if(!out)fail("cannot finish "+p.string());
    if(read(p,b.size())!=b)fail("copy readback differs: "+p.string());
}
using Payload=std::array<std::string,4>;
Payload validate(const fs::path& root,const ReferencePatchIdentities& ids,bool exact){
    if(odh::hostfs::symlink_status(root).type()!=fs::file_type::directory)fail("pack directory missing: "+root.string());
    std::set<std::string> names;
    for(const auto& id:ids){
        if(id.name.empty() || fs::path(id.name).filename()!=id.name || fs::path(id.name).extension()!=".piff" ||
            id.bytes>4096 || id.sha256.size()!=64 || !names.insert(id.name).second)fail("invalid built-in identities");
    }
    for(const auto& item:fs::directory_iterator(root)){
        const auto name=item.path().filename().string();
        if(!names.count(name) && (exact || item.path().extension()==".piff"))fail("unexpected pack member: "+name);
    }
    Payload bytes;
    for(std::size_t i=0;i<ids.size();++i){bytes[i]=read(root/ids[i].name,ids[i].bytes);
        if(digest(bytes[i])!=ids[i].sha256)fail("SHA256 mismatch: "+ids[i].name);}
    return bytes;
}
fs::path source_directory(const fs::path& selected,const ReferencePatchIdentities& ids){
    const auto repo=selected/"Client/Simitone/Simitone.Client/Content/TS1Patch";
    const bool direct=present(selected/ids[0].name),nested=present(repo/ids[0].name);
    if(direct && nested)fail("ambiguous source; choose the TS1Patch directory explicitly");
    return nested?repo:selected;
}
// Both endpoints exist. Compare actual directory identities at each parent,
// so symlink aliases and case-insensitive filesystem names share the rule.
bool ancestor(const fs::path& parent,fs::path child){
    if(!fs::exists(parent))return false;
    for(;;){
        if(fs::exists(child) && fs::equivalent(parent,child))return true;
        const auto next=child.parent_path();
        if(next.empty() || next==child)return false;
        child=next;
    }
}
void separate_source(const fs::path& saves,const fs::path& source){
    if(ancestor(saves,source) || ancestor(source,saves))
        fail("save root and import source must be separate and must not contain one another: "+source.string());
}
struct Temporary {
    fs::path path;
    ~Temporary(){if(!path.empty()){std::error_code ec;fs::remove_all(path,ec);}}
};
}
const ReferencePatchIdentities& reference_patch_identities(){
    static const ReferencePatchIdentities ids{{
        {"NPC_Unleashed_SaxPlayer_infinite_loop.piff",221,"38433ddb18121a6840ce7cff3595c9506aacb472ce170f6a4ed7a1cebad78e3d"},
        {"OpenMicSuperstar_objf.piff",390,"18783c0856ad71af4ef897251c4cab6cc21fc089994a6edd2e036c13ee08776c"},
        {"PetGym_performance.piff",222,"e3837c82b328dd0fadc90a7459ea3ff19c0f2f5a377520bce265c6839a5021f5"},
        {"PicnicBasketVacation_objf.piff",229,"719f0323ae5648bacdcfdf717f5ea53ed658240e02b4c94b0d4c36d4abeece18"}}};
    return ids;
}
fs::path validate_reference_patch_source(const fs::path& save_root,const fs::path& source,
        const ReferencePatchIdentities& ids){
    if(source.empty())return {};
    const auto saves=odh::hostfs::weakly_canonical(odh::hostfs::absolute(save_root));
    const auto chosen=odh::hostfs::canonical(source);
    const auto resolved=odh::hostfs::canonical(source_directory(chosen,ids));
    // Check the whole chosen repository as well as the effective pack. A
    // prospective save root may not exist yet; inspect its existing ancestors.
    separate_source(saves,chosen);separate_source(saves,resolved);
    return resolved;
}
ReferencePatchSelection prepare_reference_patches(const fs::path& save_root,const fs::path& source,
        const fs::path& override_root,const ReferencePatchIdentities& ids){
    if(!source.empty() && !override_root.empty())fail("use either import or --resource-patches, not both");
    if(!override_root.empty()){
        if(!fs::is_directory(override_root))fail("explicit override directory unavailable: "+override_root.string());
        return {odh::hostfs::canonical(override_root),"explicit resource patch override (persistent choice unchanged)"};
    }
    if(!fs::is_directory(save_root))fail("save root unavailable: "+save_root.string());
    const auto resolved_source=validate_reference_patch_source(save_root,source,ids);
    const auto config=save_root/".opendollhouse";
    const auto selector=config/"reference-patches.txt";
    const auto pack=config/relative_pack;
    if(present(config) && odh::hostfs::symlink_status(config).type()!=fs::file_type::directory)fail("profile metadata is not a real directory");
    const bool selected=present(selector);
    if(selected && read(selector,std::char_traits<char>::length(selection_text))!=selection_text)
        fail("malformed saved selection: "+selector.string());
    if(!source.empty()){
        // Read and authenticate all input bytes before touching profile state.
        const auto bytes=validate(resolved_source,ids,false);
        directory(config);directory(config/"resource-patches");
        if(present(pack))validate(pack,ids,true); // refuse changed user data, never overwrite
        else {
            Temporary temp;
            for(unsigned i=0;i<128;++i){const auto candidate=config/"resource-patches"/(".import-"+std::to_string(i));
                if(fs::create_directory(candidate)){temp.path=candidate;break;}}
            if(temp.path.empty())fail("another import or retained staging directories occupy the import slots");
            for(std::size_t i=0;i<ids.size();++i)write(temp.path/ids[i].name,bytes[i]);
            validate(temp.path,ids,true);
            // A concurrent import may win the fixed destination. Do not replace it.
            if(present(pack))fail("destination appeared during import; retry after checking it");
            fs::rename(temp.path,pack);temp.path.clear();
        }
        if(!selected){
            // Fixed temp name reserves the selection transaction. Never truncate
            // an existing stale/user file; refusal preserves the prior state.
            const auto pending=config/".reference-patches-pending";
            if(present(pending))fail("pending selection already exists: "+pending.string());
            Temporary temp;if(!fs::create_directory(pending))fail("cannot reserve selection transaction");temp.path=pending;
            write(pending/"selection",selection_text);
            if(present(selector))fail("selection appeared during import; retry after checking it");
            fs::rename(pending/"selection",selector);
        }
    }
    if(!selected && source.empty())return {{},"no persistent reference patches selected; use --import-reference-patches DIR to select your copy"};
    // Validate the relative path's directory components too: portable profiles
    // cannot silently redirect patch ownership through a symlink.
    if(odh::hostfs::symlink_status(config/"resource-patches").type()!=fs::file_type::directory)
        fail("selected pack parent missing or redirected");
    validate(pack,ids,true);
    return {odh::hostfs::canonical(pack),"verified persistent reference patches (4 files, selected in this save root)"};
}
}
