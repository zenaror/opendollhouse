#include "odh/architecture_arrays.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <iostream>
#include <set>

template<class T>void bank(const T& values){std::cout<<'[';bool first=true;for(const auto& v:values){if(!first)std::cout<<',';first=false;std::cout<<+v;}std::cout<<']';}
int main(int argc,char** argv){try{
    if(argc!=2)throw odh::FormatError("usage: odh-architecture HOUSE_IFF");
    struct Record{std::uint16_t id;odh::ArchitectureArray array;};
    struct Mapping{std::string type;odh::ArchitectureMap map;};
    std::vector<Record> arrays;std::vector<Mapping> maps;std::set<std::pair<std::string,std::uint16_t>> ids;
    std::uint64_t encoded=0,decoded=0;
    with_iff_file(argv[1],[&](const auto& view){odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const auto& c){
            if(c.type!="Arry"&&c.type!="ARRY"&&c.type!="FLRm"&&c.type!="WALm")return;
            const auto key=c.type=="ARRY"?"Arry":c.type;
            if(!ids.emplace(key,c.id).second)throw odh::FormatError("duplicate architecture chunk");
            if(arrays.size()+maps.size()>=64||c.payload.size()>1024*1024-encoded)
                throw odh::FormatError("architecture host chunk/encoded budget exceeded");
            encoded+=c.payload.size();std::vector<std::uint8_t> bytes(static_cast<std::size_t>(c.payload.size()));
            c.payload.read(0,bytes.data(),bytes.size());
            if(key=="Arry"){
                odh::ArchitectureArrayLimits bounds;bounds.decoded_bytes=static_cast<std::size_t>(1024*1024-decoded);
                auto array=odh::read_architecture_array(bytes,bounds);decoded+=array.data.size();arrays.push_back({c.id,std::move(array)});
            }else maps.push_back({key,odh::read_architecture_map(bytes,c.id)});
        },limits);});
    if(arrays.empty())throw odh::FormatError("architecture arrays absent");
    std::cout<<"{\"evidence\":\"ODH_saved_architecture_RLE_arrays\",\"house\":"<<json_bytes(argv[1])
             <<",\"architecture_rendered\":false,\"rooms_built\":false,\"resource_remapping_applied\":false"
             <<",\"rle_profile\":\"declared runs including terminal fill; strict output bounds without wrap\",\"encoded_bytes\":"<<encoded
             <<",\"decoded_bytes\":"<<decoded<<",\"arrays\":[";
    for(std::size_t i=0;i<arrays.size();++i){if(i)std::cout<<',';const auto& r=arrays[i];const auto& a=r.array;
        std::cout<<"{\"id\":"<<r.id<<",\"zero\":"<<a.zero<<",\"width\":"<<a.width<<",\"height\":"<<a.height<<",\"stride\":"<<a.stride
                 <<",\"unknown\":"<<a.unknown<<",\"consumed_bytes\":"<<a.consumed_bytes<<",\"raw_runs\":"<<a.raw_runs<<",\"fill_runs\":"<<a.fill_runs
                 <<",\"zero_opcodes\":"<<a.zero_opcodes<<",\"encoded\":";bank(a.encoded);
        std::cout<<",\"data\":";bank(a.data);std::cout<<",\"transposed\":";bank(odh::transpose_architecture_array(a));
        std::cout<<",\"trailing\":";bank(a.trailing_bytes);std::cout<<'}';
    }
    std::cout<<"],\"maps\":[";
    for(std::size_t i=0;i<maps.size();++i){if(i)std::cout<<',';const auto& r=maps[i];const auto& m=r.map;
        std::cout<<"{\"type\":"<<json_bytes(r.type)<<",\"id\":"<<m.chunk_id<<",\"zero\":"<<m.zero<<",\"version\":"<<m.version
                 <<",\"signature\":"<<m.signature<<",\"encoded\":";bank(m.encoded);std::cout<<",\"entries\":[";
        for(std::size_t j=0;j<m.entries.size();++j){if(j)std::cout<<',';const auto& e=m.entries[j];
            std::cout<<"{\"name\":"<<json_bytes(e.name)<<",\"padding\":";bank(e.name_padding);
            std::cout<<",\"unknown\":"<<e.unknown<<",\"id\":"<<unsigned(e.id)<<",\"unknown2\":";bank(e.unknown2);std::cout<<'}';}
        std::cout<<"]}";
    }
    std::cout<<"]}\n";return 0;
}catch(const std::exception& e){std::cerr<<"architecture error: "<<e.what()<<'\n';return 2;}}
