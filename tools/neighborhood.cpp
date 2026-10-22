#include "odh/lot_locations.hpp"
#include "odh/family.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <charconv>
#include <iomanip>
#include <iostream>
#include <sstream>
namespace {
unsigned decimal(const std::string& s) {
    unsigned value=0;
    const auto parsed=std::from_chars(s.data(),s.data()+s.size(),value);
    if (s.empty() || parsed.ec!=std::errc{} || parsed.ptr!=s.data()+s.size())
        throw odh::FormatError("invalid decimal selector");
    return value;
}
}
int main(int argc,char** argv) {
    try {
        std::optional<std::string> folder;
        std::optional<unsigned> mode,house;
        for(int i=1;i<argc;i+=2) {
            if(i+1>=argc) throw odh::FormatError("missing argument value");
            const std::string key=argv[i],value=argv[i+1];
            if(key=="--user-data" && !folder) folder=value;
            else if(key=="--mode" && !mode) mode=decimal(value);
            else if(key=="--house" && !house) house=decimal(value);
            else throw odh::FormatError("unknown or duplicate option");
        }
        if(!folder || !mode) throw odh::FormatError("Usage: odh-neighborhood --user-data DIR --mode 1..7 [--house N]");
        const bool supported=odh::neighborhood_mode_supported(*mode);
        if(house && (*house<1 || *house>98 || *house==99)) throw odh::FormatError("house selector outside selectable range");
        if(!supported) {
            std::cout<<"{\"evidence\":\"catalog_selection_only\",\"mode\":6,\"status\":\"unsupported\",\"reason\":\"Simitone mode 6 has no selectable background/flow\",\"lots\":[]}\n";
            return 0;
        }
        const std::filesystem::path root=*folder;
        std::optional<odh::LotLocations> locations;
        with_iff_file((root/"LotLocations.iff").string(),[&](const auto& view) {
            odh::iff_chunks(view,[&](const odh::Chunk& c) {
                if(c.type!="STR#" || c.id!=*mode) return;
                if(locations) throw odh::FormatError("ambiguous LotLocations STR# mode");
                locations=odh::read_lot_locations(c.payload);
            });
        });
        if(!locations) throw odh::FormatError("LotLocations mode STR# not found");
        std::vector<odh::FamilyEntry> entries;
        with_iff_file((root/"Neighborhood.iff").string(),[&](const auto& view) {
            odh::iff_chunks(view,[&](const odh::Chunk& c) {
                if(c.type=="FAMI") {
                    if(entries.size()>=4096) throw odh::FormatError("too many FAMI chunks");
                    entries.push_back({c.id,odh::read_fami(c.payload)});
                }
            });
        });
        const odh::FamilyContext families(entries);
        std::ostringstream out;
        out<<"{\"evidence\":\"catalog_selection_only\",\"mode\":"<<*mode<<",\"status\":\"supported\",\"lot_locations_remaining_bytes\":"<<locations->remaining_bytes<<",\"lots\":[";
        bool first=true,found=false;
        for(const auto& loc:locations->lots) {
            if(house && loc.house!=*house) continue;
            found=true;
            std::ostringstream filename; filename<<"House"<<std::setw(2)<<std::setfill('0')<<loc.house<<".iff";
            const auto path=root/"Houses"/filename.str();
            const bool exists=std::filesystem::is_regular_file(path);
            const auto* fam=families.by_house(static_cast<std::int16_t>(loc.house));
            if(house && !exists) throw odh::FormatError("selected house file missing");
            if(!first) out<<',';
            first=false;
            out<<"{\"house_number\":"<<loc.house<<",\"x\":"<<loc.x<<",\"y\":"<<loc.y
               <<",\"path\":"<<json_bytes(path.string())<<",\"file_exists\":"<<(exists?"true":"false")
               <<",\"selectable\":"<<(exists?"true":"false")<<",\"family\":";
            if(!fam) out<<"null";
            else {
                out<<"{\"house_number\":"<<fam->family.house_number<<",\"fami_chunk_id\":"<<fam->chunk_id
                   <<",\"family_number\":"<<fam->family.family_number<<",\"member_count\":"<<fam->family.member_guids.size()<<",\"member_guids\":[";
                bool member_first=true;
                for(const auto guid:fam->family.member_guids) { if(!member_first) out<<','; member_first=false; out<<guid; }
                out<<"]}";
            }
            out<<'}';
        }
        if(house && !found) throw odh::FormatError("house not in selected mode");
        out<<"]}\n";std::cout<<out.str();return 0;
    } catch(const std::exception& e) { std::cerr<<"odh-neighborhood: "<<e.what()<<'\n';return 2; }
}
