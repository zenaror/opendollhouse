// Read-only bridge to the repository's bounded IFF/FAMI/NBRS/STR/OBJD decoders.
#include "odh/animation.hpp"
#include "odh/family.hpp"
#include "odh/neighborhood.hpp"
#include "odh/object_definition.hpp"
#include "odh/lot_locations.hpp"
#include "odh/formats.hpp"
#include "json.hpp"

#include <charconv>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>

namespace {
struct DecodedNeighbors {
    odh::Nbrs value;
    std::uint32_t declared_count;
    bool early_eof_writer_profile;
};
DecodedNeighbors decode_neighbors(const odh::View& payload) {
    const auto declared=payload.le32(12);
    try {
        return {odh::read_nbrs(payload),declared,false};
    } catch(const odh::FormatError& error) {
        if(std::strcmp(error.what(),"truncated NBRS payload")!=0 ||
           payload.size()>1024ull*1024ull || payload.le32(4)!=73 || declared>4096)
            throw;
    }
    // FreeSO NBRS.Write stores Entries.Count but serializes only its active
    // NeighbourByID.Values. NBRS.Read itself accepts early EOF. Find the exact
    // complete-record prefix using the unchanged bounded decoder, without
    // inventing positions for omitted marker-0 entries.
    std::string bytes(static_cast<std::size_t>(payload.size()),'\0');
    payload.read(0,bytes.data(),bytes.size());
    auto decode_count=[&](std::uint32_t count) {
        for(unsigned i=0;i<4;++i)bytes[12+i]=static_cast<char>(count>>(8*i));
        std::istringstream input(bytes,std::ios::in|std::ios::binary);
        return odh::read_nbrs(odh::View(input,bytes.size()));
    };
    std::uint32_t low=0,high=declared;
    while(low<high){
        const auto mid=low+(high-low+1)/2;
        try { (void)decode_count(mid);low=mid; }
        catch(const odh::FormatError&) { high=mid-1; }
    }
    auto decoded=decode_count(low);
    if(low==declared || decoded.remaining_bytes!=0 || decoded.entries.empty() ||
       !std::all_of(decoded.entries.begin(),decoded.entries.end(),
                    [](const odh::NeighborEntry& entry){return entry.marker==1&&entry.record.has_value();}))
        throw odh::FormatError("NBRS is truncated outside the exact FreeSO active-only writer profile");
    return {std::move(decoded),declared,true};
}
void neighborhood(const odh::View& view) {
    bool first_family=true,first_name=true,seen_neighbors=false;
    std::set<std::uint16_t> family_ids,name_ids;
    std::cout<<"{\"families\":[";
    odh::iff_chunks(view,[&](const odh::Chunk& chunk){
        if(chunk.type!="FAMI")return;
        if(!family_ids.insert(chunk.id).second)throw odh::FormatError("duplicate FAMI chunk ID");
        const auto f=odh::read_fami(chunk.payload);
        if(!first_family)std::cout<<',';
        first_family=false;
        std::cout<<"{\"chunk_id\":"<<chunk.id<<",\"version\":"<<f.version
            <<",\"house_number\":"<<f.house_number<<",\"family_number\":"<<f.family_number
            <<",\"budget\":"<<f.budget<<",\"value_in_arch\":"<<f.value_in_arch
            <<",\"family_friends\":"<<f.family_friends<<",\"unknown\":"<<f.unknown
            <<",\"member_guids\":[";
        bool first=true;for(const auto guid:f.member_guids){if(!first)std::cout<<',';first=false;std::cout<<guid;}
        std::cout<<"],\"remaining_bytes\":"<<f.remaining_bytes<<'}';
    });
    std::cout<<"],\"family_names\":[";
    odh::iff_chunks(view,[&](const odh::Chunk& chunk){
        if(chunk.type!="FAMs")return;
        if(!name_ids.insert(chunk.id).second)throw odh::FormatError("duplicate FAMs chunk ID");
        const auto table=odh::read_strings(chunk.payload);
        const auto* selected=odh::string_in_language_or_english(table,1,0);
        if(!first_name)std::cout<<',';
        first_name=false;
        std::cout<<"{\"chunk_id\":"<<chunk.id<<",\"format\":";
        if(table.format)std::cout<<*table.format;else std::cout<<"null";
        std::cout<<",\"entry_count\":"<<table.entries.size()<<",\"name_latin1\":";
        if(selected)std::cout<<json_bytes(selected->value);else std::cout<<"null";
        std::cout<<",\"remaining_bytes\":"<<table.remaining_bytes<<'}';
    });
    std::cout<<"],\"neighbors\":[";
    odh::iff_chunks(view,[&](const odh::Chunk& chunk){
        if(chunk.type!="NBRS"||chunk.id!=1)return;
        if(seen_neighbors)throw odh::FormatError("duplicate NBRS chunk 1");
        seen_neighbors=true;
        const auto decoded=decode_neighbors(chunk.payload);
        const auto& n=decoded.value;
        bool first=true;
        for(std::size_t index=0;index<n.entries.size();++index){
            const auto& entry=n.entries[index];if(!entry.record)continue;
            const auto& r=*entry.record;
            if(!first)std::cout<<',';
            first=false;
            std::cout<<"{\"index\":"<<index<<",\"neighbor_id\":"<<r.neighbor_id
                <<",\"guid\":"<<r.guid<<",\"version\":"<<r.version
                <<",\"person_mode\":"<<r.person_mode<<",\"name_latin1\":"<<json_bytes(r.name)
                <<",\"person_data_61\":";
            if(r.person_data&&r.person_data->size()>61)std::cout<<(*r.person_data)[61];
            else std::cout<<"null";
            std::cout<<",\"relationship_count\":"<<r.relationships.size()<<'}';
        }
        std::cout<<"],\"nbrs_version\":"<<n.version
            <<",\"nbrs_entry_count\":"<<n.entries.size()
            <<",\"nbrs_declared_entry_count\":"<<decoded.declared_count
            <<",\"nbrs_decode_profile\":"
            <<json_bytes(decoded.early_eof_writer_profile?"freeso_active_only_early_eof":"strict")
            <<",\"nbrs_remaining_bytes\":"<<n.remaining_bytes;
    });
    if(!seen_neighbors)throw odh::FormatError("NBRS chunk 1 missing");
    std::cout<<'}';
}
void character(const odh::View& view) {
    bool first=true;
    std::cout<<"{\"person_definitions\":[";
    odh::iff_chunks(view,[&](const odh::Chunk& chunk){
        if(chunk.type!="OBJD")return;
        const auto d=odh::read_object_definition(chunk.payload);
        if(d.object_type!=2)return;
        if(!first)std::cout<<',';
        first=false;
        std::cout<<"{\"chunk_id\":"<<chunk.id<<",\"guid\":"<<d.guid
            <<",\"version\":"<<d.version<<'}';
    });
    std::cout<<"]}";
}
void locations(const odh::View& view) {
    bool first_mode=true;
    std::cout<<"{\"modes\":[";
    for(const std::uint16_t mode:{4,7}){
        bool found=false;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="STR#"||chunk.id!=mode)return;
            if(found)throw odh::FormatError("duplicate LotLocations mode");
            found=true;
            const auto decoded=odh::read_lot_locations(chunk.payload);
            if(!first_mode)std::cout<<',';
            first_mode=false;
            std::cout<<"{\"mode\":"<<mode<<",\"remaining_bytes\":"<<decoded.remaining_bytes
                <<",\"lots\":[";
            bool first=true;
            for(const auto& lot:decoded.lots){
                if(!first)std::cout<<',';
                first=false;
                std::cout<<"{\"house_number\":"<<lot.house<<",\"x\":"<<lot.x<<",\"y\":"<<lot.y<<'}';
            }
            std::cout<<"]}";
        });
        if(!found)throw odh::FormatError("LotLocations STR mode 4 or 7 missing");
    }
    std::cout<<"]}";
}
void zoning(const odh::View& view) {
    bool found=false;
    std::set<std::int16_t> houses;
    std::cout<<"{\"lots\":[";
    odh::iff_chunks(view,[&](const odh::Chunk& chunk){
        if(chunk.type!="STR#"||chunk.id!=1)return;
        if(found)throw odh::FormatError("duplicate LotZoning STR 1");
        found=true;
        const auto table=odh::read_strings(chunk.payload);
        if(table.state!=odh::StringTableState::decoded)throw odh::FormatError("LotZoning STR 1 undecoded");
        bool first=true;
        for(const auto& entry:table.entries){
            if(entry.language!=0&&entry.language!=1)continue;
            const auto comma=entry.value.find(',');
            if(comma==std::string::npos)throw odh::FormatError("LotZoning row has no comma");
            std::int16_t house=0;
            const auto begin=entry.value.data(),end=begin+comma;
            const auto parsed=std::from_chars(begin,end,house);
            if(parsed.ec!=std::errc{}||parsed.ptr!=end)throw odh::FormatError("invalid LotZoning house");
            const auto label=entry.value.substr(comma+1);
            if(label!=" residential"&&label!=" community")throw odh::FormatError("invalid LotZoning label");
            if(!houses.insert(house).second)throw odh::FormatError("duplicate LotZoning house");
            if(!first)std::cout<<',';
            first=false;
            std::cout<<"{\"house_number\":"<<house<<",\"zone\":"
                <<json_bytes(label==" residential"?"residential":"community")
                <<",\"code\":"<<(label==" residential"?0:1)<<'}';
        }
        std::cout<<"],\"format\":";
        if(table.format)std::cout<<*table.format;else std::cout<<"null";
        std::cout<<",\"remaining_bytes\":"<<table.remaining_bytes;
    });
    if(!found)throw odh::FormatError("LotZoning STR 1 missing");
    std::cout<<'}';
}
}
int main(int argc,char** argv) {
    if(argc!=3||(std::string(argv[1])!="--neighborhood"&&std::string(argv[1])!="--character"&&
                 std::string(argv[1])!="--locations"&&std::string(argv[1])!="--zoning")){
        std::cerr<<"usage: family_profile_extract --neighborhood|--character|--locations|--zoning FILE\n";return 2;
    }
    try {
        const std::filesystem::path path=argv[2];
        if(std::filesystem::file_size(path)>64ull*1024ull*1024ull)
            throw odh::FormatError("IFF exceeds 64 MiB inspection bound");
        std::ifstream input(path,std::ios::binary);
        if(!input)throw odh::FormatError("cannot open IFF");
        const odh::View view(input,std::filesystem::file_size(path));
        const std::string kind=argv[1];
        if(kind=="--neighborhood")neighborhood(view);
        else if(kind=="--character")character(view);
        else if(kind=="--locations")locations(view);
        else zoning(view);
        std::cout<<'\n';return 0;
    } catch(const std::exception& error) {
        std::cerr<<"family_profile_extract: "<<error.what()<<'\n';return 2;
    }
}
