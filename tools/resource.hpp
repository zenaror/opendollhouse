#pragma once
#include "odh/formats.hpp"
#include <filesystem>
#include <fstream>

class FarEntryNotFound : public odh::FormatError {
public:
    using odh::FormatError::FormatError;
};

template<class F> void with_iff_file(const std::string& path,F read) {
    std::ifstream stream(path,std::ios::binary);if(!stream)throw odh::FormatError("cannot open IFF file");
    read(odh::View(stream,std::filesystem::file_size(path)));
}
template<class F> void with_far_entry(const std::string& path,const std::string& name,F read,
                                      const odh::Limits& limits = {}) {
    std::ifstream stream(path,std::ios::binary);if(!stream)throw odh::FormatError("cannot open archive");
    const odh::View archive(stream,std::filesystem::file_size(path));std::optional<odh::FarEntry> selected;
    for(const auto& entry:odh::far1_index(archive,limits)){if(entry.name!=name)continue;
        if(selected)throw odh::FormatError("ambiguous archive entry");
        selected=entry;}
    if(!selected)throw FarEntryNotFound("exact archive entry not found");
    read(archive.subview(selected->offset,selected->length));
}
