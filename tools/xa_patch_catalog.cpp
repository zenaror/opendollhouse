#include "xa_patch_catalog.hpp"
#include "odh/hit_object_catalog.hpp"
#include <filesystem>
#include <fstream>

namespace odh_host {
namespace {
[[noreturn]] void fail(const char* why){throw odh::FormatError(std::string("XA patch catalog: ")+why);}
bool xa(const std::string& text){
    if(text.size()<4)return false;
    const auto n=text.size();return text[n-3]=='.' && (text[n-2]=='x'||text[n-2]=='X') && (text[n-1]=='a'||text[n-1]=='A');
}
}
std::string XaPatchCatalog::key(const std::string& name) const {
    if(name.empty() || name.size()>limits_.name_length || name.find('\0')!=std::string::npos)
        fail("invalid or over-budget filename");
    const auto at=name.find_last_of("/\\");
    auto result=name.substr(at==std::string::npos?0:at+1);
    if(!xa(result))fail("only XA patch filenames supported");
    for(auto& c:result)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);
    return result;
}
void XaPatchCatalog::charge(std::uint64_t bytes){
    if(bytes>limits_.name_bytes-usage_.name_bytes)fail("aggregate name budget exhausted");
    usage_.name_bytes+=bytes;
}
void XaPatchCatalog::add(std::size_t source,std::string name,std::uint64_t offset,std::uint64_t length){
    if(!xa(name))return;
    auto normalized=key(name);charge(normalized.size());++usage_.xa_entries;
    const auto found=index_.find(normalized);
    if(found!=index_.end()){++found->second.count;return;}
    const bool archived=sources_[source].source.kind==XaSourceKind::far;
    index_.emplace(std::move(normalized),Candidate{source,offset,length,archived?std::move(name):std::string{},1});
}
XaPatchCatalog::XaPatchCatalog(const std::vector<XaPatchSource>& inputs,XaPatchCatalogLimits limits):limits_(limits){
    if(inputs.empty() || inputs.size()>limits.sources || !limits.entries || !limits.name_length)
        fail("invalid source or index budget");
    for(const auto& input:inputs){
        if(input.path.empty() || input.path.size()>limits.name_length || input.path.find('\0')!=std::string::npos)
            fail("invalid source path");
        if(input.kind!=XaSourceKind::far && input.kind!=XaSourceKind::loose)fail("unsupported source kind");
        auto fixed=input;
        fixed.path=std::filesystem::absolute(input.path).string();
        if(fixed.path.size()>limits.name_length)fail("absolute source path exceeds budget");
        charge(fixed.path.size());
        std::ifstream stream(fixed.path,std::ios::binary);if(!stream)fail("cannot open source");
        const auto size=std::filesystem::file_size(fixed.path);
        const auto source=sources_.size();sources_.push_back({std::move(fixed),size});++usage_.sources;
        if(input.kind==XaSourceKind::far){
            odh::Limits bounds;bounds.far_entries=static_cast<std::uint32_t>(limits.entries-usage_.entries);
            bounds.far_name_length=limits.name_length;bounds.far_name_bytes=limits.name_bytes-usage_.name_bytes;
            auto entries=odh::far1_index(odh::View(stream,size),bounds);
            for(auto& entry:entries){++usage_.entries;charge(entry.name.size());add(source,std::move(entry.name),entry.offset,entry.length);}
        }else{
            if(usage_.entries>=limits.entries)fail("entry budget exhausted");
            ++usage_.entries;
            // Both slash styles work; the supplied path still identifies the
            // actual host file. Its spelling never comes from HOT interpolation.
            if(!xa(input.path))fail("loose source must be XA");
            add(source,input.path,0,size);
        }
    }
}
XaPatchAsset XaPatchCatalog::load(const odh::Hot& hot,std::uint32_t id,std::optional<std::uint64_t> allowance) const {
    const auto patch=hot.patches.find(id);if(patch==hot.patches.end())fail("patch ID absent in HOT");
    return load_filename(patch->second.filename,id,allowance);
}
XaPatchAsset XaPatchCatalog::load(const odh::NativeHitPatchRef& ref,
                                  std::optional<std::uint64_t> allowance) const {
    if(!ref.id || !ref.patch)fail("invalid global Patch reference");
    return load_filename(ref.patch->filename,ref.id,allowance);
}
XaPatchAsset XaPatchCatalog::load_filename(const std::string& filename,std::uint32_t id,
                                           std::optional<std::uint64_t> allowance) const {
    auto normalized=key(filename);
    const auto found=index_.find(normalized);if(found==index_.end())fail("XA basename absent in supplied sources");
    const auto& entry=found->second;if(entry.count!=1)fail("ambiguous XA basename");
    const auto& source=sources_[entry.source];
    std::ifstream stream(source.source.path,std::ios::binary);if(!stream)fail("cannot reopen indexed source");
    const auto size=std::filesystem::file_size(source.source.path);
    if(size!=source.size)fail("source size changed after indexing");
    auto bounds=limits_.audio;
    if(allowance && *allowance<bounds.pcm_bytes)bounds.pcm_bytes=*allowance;
    auto audio=odh::read_xa(odh::View(stream,size).subview(entry.offset,entry.length),bounds);
    return {id,std::move(normalized),source.source,entry.entry,entry.offset,entry.length,std::move(audio)};
}
}
