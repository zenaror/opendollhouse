// SPDX-License-Identifier: MPL-2.0
#include "catalog_object_thumbnail.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
namespace {
void check(bool condition){if(!condition)throw std::runtime_error("catalog object thumbnail assertion failed");}
void set(std::string& bytes,std::size_t p,std::uint32_t value,unsigned n=4,bool big=false){
    for(unsigned i=0;i<n;++i)bytes.at(p+i)=char(value>>((big?n-1-i:i)*8));
}
std::string word(std::uint32_t value){std::string bytes(2,'\0');set(bytes,0,value,2);return bytes;}
std::string chunk(const std::string& type,std::uint16_t id,const std::string& payload){
    std::string bytes(76,'\0');bytes.replace(0,4,type);set(bytes,4,76+payload.size(),4,true);set(bytes,8,id,2,true);return bytes+payload;
}
std::string objd(){std::string bytes(216,'\0');set(bytes,0,138);set(bytes,6,10,2);set(bytes,8,1,2);set(bytes,18,4,2);set(bytes,28,123);return bytes;}
std::string palette(){std::string bytes(25,'\0');set(bytes,0,1);set(bytes,4,3);
    bytes[19]=char(255);bytes[21]=char(255); // palette1 magenta; palette0 original black.
    bytes[22]=char(40);bytes[23]=char(80);bytes[24]=char(120);return bytes;}
std::string sprite(){
    std::string bytes(32,'\0');set(bytes,0,1000);set(bytes,4,1);set(bytes,8,7);set(bytes,12,16);
    set(bytes,16,5,2);set(bytes,18,4,2);set(bytes,20,7);
    bytes+=word((4<<13)|1); // transparent top margin.
    auto row=word((3<<13)|1)+word((1<<13)|1)+std::string("\xff\0",2)+
        word((3<<13)|1)+word((1<<13)|1)+std::string("\xff\1",2)+word((3<<13)|1);
    bytes+=word(row.size()+2)+row; // black, hole, magenta; horizontal transparent margins.
    row=word((3<<13)|1)+word((2<<13)|1)+std::string("\xff\2\x10\0",4)+
        word((3<<13)|1)+word((1<<13)|1)+std::string("\xff\0",2)+word((3<<13)|1);
    bytes+=word(row.size()+2)+row;
    return bytes+word((4<<13)|1)+word(5<<13);
}
std::string group(std::int32_t x=0){std::string bytes(50,'\0');set(bytes,0,20004,2);set(bytes,2,1);
    set(bytes,6,1);set(bytes,10,3);set(bytes,14,1);set(bytes,18,5);set(bytes,26,std::uint32_t(x));return bytes;}
std::string iff(std::int32_t x=0,bool no_graphics=false){
    std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";bytes.resize(64,'\0');
    auto definition=objd();if(no_graphics){set(definition,6,0,2);set(definition,8,0,2);}
    bytes+=chunk("OBJD",1,definition);bytes+=chunk("DGRP",10,group(x));
    return bytes+chunk("PALT",7,palette())+chunk("SPR2",5,sprite());
}
struct Temp {std::filesystem::path path;Temp(){path=std::filesystem::temp_directory_path()/
    ("odh-catalog-thumbnail-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
odh::DefinitionCandidate fixture(const std::filesystem::path& path,std::int32_t x=0,bool no_graphics=false){
    const auto bytes=iff(x,no_graphics);std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());out.close();
    auto definition=objd();if(no_graphics){set(definition,6,0,2);set(definition,8,0,2);}
    std::istringstream in(definition);return {path.string(),"",1,odh::read_object_definition(odh::View(in,definition.size()))};
}
}
int main(){try{
    Temp temp;odh::DefinitionIndex definitions({fixture(temp.path/"art.iff")});
    odh_host::CatalogObjectThumbnail thumbnail(definitions);
    using Status=odh_host::CatalogObjectThumbnailStatus;
    SavedObjectVisual visual{1700,123,0,0,{100,200,1},{}};
    auto result=thumbnail.select({visual});check(result.status==Status::ready);
    check(result.image.width==3&&result.image.height==2&&result.image.pixels.size()==6);
    check((result.image.pixels[0]==std::array<std::uint8_t,4>{0,0,0,255}));
    check(result.image.pixels[1][3]==0&&result.image.pixels[4][3]==0);
    check((result.image.pixels[2]==std::array<std::uint8_t,4>{255,0,255,255}));
    check((result.image.pixels[3]==std::array<std::uint8_t,4>{40,80,120,16*255/31}));
    check(visual.id==1700&&visual.tile_position[0]==100); // no caller mutation.
    auto second=visual;second.id=1700;second.tile_position[0]+=1;
    check(thumbnail.select({visual,second}).status==Status::ready); // local tokens, authenticated parts.
    auto bad=visual;bad.guid=999;
    check(thumbnail.select({bad}).status==Status::unknown_definition&&thumbnail.result().image.pixels.empty());
    check(thumbnail.select({visual,bad}).status==Status::unknown_definition&&thumbnail.result().image.pixels.empty());
    check(thumbnail.select({visual}).status==Status::ready);
    bad=visual;bad.direction=8;
    check(thumbnail.select({bad}).status==Status::failed&&thumbnail.result().image.pixels.empty());
    for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        bad=visual;bad.tile_position[1]=invalid;check(thumbnail.select({bad}).status==Status::failed);
        bad=visual;bad.linear_light[0]=invalid;check(thumbnail.select({bad}).status==Status::failed);
    }
    bad=visual;bad.dynamic_sprite_flags.resize(65537);check(thumbnail.select({bad}).status==Status::failed);
    check(thumbnail.select(std::vector<SavedObjectVisual>(65,visual)).status==Status::failed);
    check(thumbnail.select({visual},4).status==Status::failed);
    check(thumbnail.select({}).status==Status::no_graphics&&thumbnail.result().image.pixels.empty());
    thumbnail.select({visual});thumbnail.clear();check(thumbnail.result().image.pixels.empty());
    {odh::DefinitionIndex empty({fixture(temp.path/"empty.iff",0,true)});odh_host::CatalogObjectThumbnail t(empty);
        check(t.select({visual}).status==Status::no_graphics);}
    {const auto candidate=fixture(temp.path/"duplicate.iff");odh::DefinitionIndex ambiguous({candidate,candidate});
        odh_host::CatalogObjectThumbnail t(ambiguous);check(t.select({visual}).status==Status::unknown_definition);}
    // Geometry clipping, including fully offscreen layers, must never produce
    // a falsely complete thumbnail. The bounded camera is independent of UI.
    for(int offset:{510,2000}){odh::DefinitionIndex outside({fixture(temp.path/(std::to_string(offset)+".iff"),offset)});
        odh_host::CatalogObjectThumbnail t(outside);check(t.select({visual}).status==Status::failed);}
    std::cout<<"catalog object thumbnail synthetic SPR2/depth checks passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
