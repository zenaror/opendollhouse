// SPDX-License-Identifier: MPL-2.0
#include "catalog_object_visuals.hpp"
#include "odh/object_runtime_resources.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
void check(bool value){if(!value)throw std::runtime_error("catalog object visuals assertion failed");}
template<class F> void rejects(F action){try{action();}catch(const odh::FormatError&){return;}throw std::runtime_error("expected FormatError");}
void put(std::string& bytes,std::size_t offset,std::uint32_t value,unsigned count=2,bool big=false){
    for(unsigned n=0;n<count;++n)bytes.at(offset+n)=char(value>>(8*(big?count-1-n:n)));
}
struct Definition {std::uint16_t id;std::string bytes;};
Definition definition(std::uint16_t id,std::uint32_t guid,std::uint16_t sub,
        std::uint16_t lead=0,std::uint16_t level=0,std::uint16_t master=42){
    std::string bytes(162,'\0');put(bytes,0,138,4);put(bytes,18,4);
    put(bytes,20,master);put(bytes,22,sub);put(bytes,28,guid,4);
    put(bytes,112,level);put(bytes,126,lead);return {id,std::move(bytes)};
}
std::string iff(const std::vector<Definition>& definitions){
    std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";bytes.resize(64,'\0');
    for(const auto& item:definitions){std::string chunk(76,'\0');chunk.replace(0,4,"OBJD");
        put(chunk,4,76+item.bytes.size(),4,true);put(chunk,8,item.id,2,true);bytes+=chunk+item.bytes;}
    return bytes;
}
void write(const std::filesystem::path& path,const std::string& bytes){
    std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());
    if(!out)throw std::runtime_error("fixture write failed");
}
std::vector<odh::DefinitionCandidate> candidates(const std::filesystem::path& path,
        const std::vector<Definition>& definitions,std::string asset={}){
    std::vector<odh::DefinitionCandidate> result;
    for(const auto& item:definitions){std::istringstream in(item.bytes);
        result.push_back({path.string(),asset,item.id,odh::read_object_definition(odh::View(in,item.bytes.size()))});}
    return result;
}
struct Temp {std::filesystem::path path;Temp(){path=std::filesystem::temp_directory_path()/
    ("odh-catalog-visuals-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
}
int main(){try{
    Temp temp;const auto path=temp.path/"group.iff";
    // Master is interleaved, chunk IDs unsorted, lead is the second actual
    // part, and the first part has a negative x and signed level offset.
    const std::vector<Definition> parts{definition(900,102,0xff02,0,0x01ff),
        definition(500,101,0xffff),definition(100,103,0x0280,7,2)};
    const auto bytes=iff(parts);write(path,bytes);odh::DefinitionIndex index(candidates(path,parts));
    const auto& master=*index.unique(101);
    const auto visuals=odh_host::prepare_catalog_object_visuals(master,index);
    check(visuals.size()==2&&visuals[0].guid==102&&visuals[1].guid==103);
    check(visuals[0].id==1&&visuals[1].id==2&&visuals[0].graphic==0&&visuals[0].direction==0);
    check(visuals[0].dynamic_sprite_flags.empty()&&visuals[1].dynamic_sprite_flags.empty());
    check(visuals[0].tile_position[0]==-1&&visuals[0].tile_position[1]==2&&
        std::abs(visuals[0].tile_position[2]+2.95f)<0.0001f);
    check(visuals[1].tile_position[0]==2&&visuals[1].tile_position[1]==-128&&
        std::abs(visuals[1].tile_position[2]-5.9f)<0.0001f);
    // The planner owns BaseObject selection; preparing artwork must preserve
    // its IFF order instead of moving the lead part to the front.
    std::istringstream resource(bytes);const auto plan=odh::plan_object_group_construction(
        odh::View(resource,bytes.size()),master,index,64);
    check(plan.base_index==1&&plan.parts[1].lead);
    const auto preview=odh_host::prepare_catalog_buy_group(master,index);
    check(preview.base_index==1&&preview.parts.size()==2&&preview.parts[0].guid==102&&preview.parts[1].guid==103);
    check(preview.parts[0].offset_x==-16&&preview.parts[0].offset_y==32&&preview.parts[0].offset_level==-1);
    check(preview.parts[1].offset_x==32&&preview.parts[1].offset_y==-2048&&preview.parts[1].offset_level==2);
    check(preview.parts[0].graphic==0&&preview.parts[1].dynamic_sprite_flags.empty());

    const auto direct=odh_host::prepare_catalog_object_visuals(*index.unique(102),index);
    check(direct.size()==1&&direct[0].guid==102&&direct[0].tile_position==visuals[0].tile_position);
    auto detached=master;rejects([&]{odh_host::prepare_catalog_object_visuals(detached,index);});
    rejects([&]{odh_host::prepare_catalog_object_visuals(master,index,1);});
    auto duplicates=candidates(path,parts);duplicates.push_back(duplicates[1]);odh::DefinitionIndex ambiguous(duplicates);
    check(ambiguous.unique(101)==nullptr);
    rejects([&]{odh_host::prepare_catalog_object_visuals(duplicates[1],ambiguous);});
    auto ambiguous_parts=candidates(path,parts);ambiguous_parts.push_back(ambiguous_parts[0]);odh::DefinitionIndex bad_parts(ambiguous_parts);
    rejects([&]{odh_host::prepare_catalog_object_visuals(*bad_parts.unique(101),bad_parts);});
    auto changed=parts;put(changed[0].bytes,28,999,4);write(path,iff(changed));
    rejects([&]{odh_host::prepare_catalog_object_visuals(master,index);});
    write(path,iff({parts[1]}));rejects([&]{odh_host::prepare_catalog_object_visuals(master,index);});
    std::vector<Definition> many{parts[1]};for(unsigned n=0;n<65;++n)many.push_back(definition(1000+n,10000+n,n));
    write(path,iff(many));odh::DefinitionIndex oversized(candidates(path,many));
    rejects([&]{odh_host::prepare_catalog_object_visuals(*oversized.unique(101),oversized);});
    many.pop_back();write(path,iff(many));odh::DefinitionIndex boundary(candidates(path,many));
    check(odh_host::prepare_catalog_object_visuals(*boundary.unique(101),boundary).size()==64);
    const std::vector<Definition> single{definition(1,201,0,0,0,0)};write(path,iff(single));
    odh::DefinitionIndex singleton(candidates(path,single));const auto one=odh_host::prepare_catalog_object_visuals(*singleton.unique(201),singleton);
    check(one.size()==1&&one[0].guid==201&&(one[0].tile_position==std::array<float,3>{0,0,0}));
    // FAR1 fixture with an exact entry name; unrelated entries are never a
    // fallback for a missing requested asset.
    const auto archive=temp.path/"objects.far";std::string far(16,'\0');far.replace(0,8,"FAR!byAZ");put(far,8,1,4);
    const auto payload=iff(parts);far+=payload;const auto manifest=far.size();put(far,12,manifest,4);
    std::string entry(20,'\0');put(entry,0,1,4);put(entry,4,payload.size(),4);put(entry,8,payload.size(),4);
    put(entry,12,16,4);const std::string name="Group.iff";put(entry,16,name.size(),4);far+=entry+name;write(archive,far);
    odh::DefinitionIndex packed(candidates(archive,parts,name));
    check(odh_host::prepare_catalog_object_visuals(*packed.unique(101),packed).size()==2);
    odh::DefinitionIndex missing(candidates(archive,parts,"Other.iff"));
    rejects([&]{odh_host::prepare_catalog_object_visuals(*missing.unique(101),missing);});
    std::cout<<"catalog object visuals checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
