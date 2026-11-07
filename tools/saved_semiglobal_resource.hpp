// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "resource.hpp"
#include <optional>

// WorldGlobalProvider.Get at pinned FreeSO7e76 compares complete entry names
// case-insensitively within the explicitly selected Global FAR only. Keep the
// actual entry spelling for exact reads and saved resource provenance.
inline bool saved_semiglobal_name_equal(const std::string& a,const std::string& b) {
    if(a.size()!=b.size())return false;
    const auto fold=[](unsigned char c){return c>='A'&&c<='Z'?static_cast<unsigned char>(c+('a'-'A')):c;};
    for(std::size_t n=0;n<a.size();++n)
        if(fold(static_cast<unsigned char>(a[n]))!=fold(static_cast<unsigned char>(b[n])))return false;
    return true;
}
inline std::string resolve_saved_semiglobal_entry(const std::string& archive,const std::string& entry) {
    if(entry.size()<5||entry.size()>256||entry.find_first_of("/\\:")!=std::string::npos||
       entry.find('\0')!=std::string::npos||
       !saved_semiglobal_name_equal(entry.substr(entry.size()-4),".iff")||
       entry.substr(0,entry.size()-4).find('.')!=std::string::npos)
        throw odh::FormatError("semiglobal entry must be a bounded flat .iff resource name");
    std::ifstream stream(archive,std::ios::binary);
    if(!stream)throw odh::FormatError("cannot open semiglobal archive");
    odh::Limits limits;limits.far_entries=4096;limits.far_name_length=256;limits.far_name_bytes=256*1024;
    const odh::View view(stream,std::filesystem::file_size(archive));std::optional<std::string> selected;
    for(const auto& candidate:odh::far1_index(view,limits)){
        if(!saved_semiglobal_name_equal(candidate.name,entry))continue;
        if(selected)throw odh::FormatError("ambiguous case-insensitive semiglobal entry");
        selected=candidate.name;
    }
    if(!selected)throw FarEntryNotFound("semiglobal archive entry not found");
    return *selected;
}
