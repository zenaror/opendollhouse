// SPDX-License-Identifier: MPL-2.0
#include "object_pickup_view.hpp"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
using namespace odh_host;
void check(bool value){if(!value)throw std::runtime_error("object pickup view assertion failed");}
template<class F>void rejects(F action){try{action();}catch(const odh::FormatError&){return;}throw std::runtime_error("expected pickup rejection");}
void at(std::string& bytes,std::size_t offset,std::uint32_t value,unsigned count=2,bool big=false){
    for(unsigned n=0;n<count;++n)bytes.at(offset+n)=char(value>>(8*(big?count-1-n:n)));
}
std::string chunk(const char* type,unsigned id,const std::string& payload,std::string label={}){
    std::string bytes(76,'\0');bytes.replace(0,4,type);at(bytes,4,76+payload.size(),4,true);at(bytes,8,id,2,true);
    bytes.replace(12,label.size(),label);return bytes+payload;
}
struct Temp {std::filesystem::path path;Temp(){path=std::filesystem::temp_directory_path()/
    ("odh-pickup-name-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
void names(){
    Temp temp;const auto file=temp.path/"object.iff";
    std::string master(190,'\0');at(master,0,138,4);at(master,18,4);at(master,20,42);at(master,22,0xffff);at(master,28,101,4);at(master,82,2001);
    auto part=master;at(part,22,0);at(part,28,102,4);
    std::string ctss(4,'\0');at(ctss,0,0xfffd);at(ctss,2,1);ctss.push_back(1);ctss+="Bottle Lamp";ctss.append(2,'\0');
    const auto make=[&](std::string m,bool with_strings=true){std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";bytes.resize(64,'\0');
        bytes+=chunk("OBJD",1,m,"Authored label")+chunk("OBJD",2,part);if(with_strings)bytes+=chunk("CTSS",2001,ctss);return bytes;};
    const auto write=[&](const std::string& bytes){std::ofstream out(file,std::ios::binary);out.write(bytes.data(),bytes.size());};
    const auto candidate=[&](unsigned id,const std::string& bytes){std::istringstream in(bytes);return odh::DefinitionCandidate{file.string(),"",std::uint16_t(id),odh::read_object_definition(odh::View(in,bytes.size()))};};
    auto input=std::vector<odh::DefinitionCandidate>{candidate(1,master),candidate(2,part)};odh::DefinitionIndex index(input);write(make(master));
    unsigned language_seen=0;auto decoder=[&](std::string_view value,std::uint8_t language){language_seen=language;return std::string(value);};
    check(resolve_object_pickup_name(*index.unique(102),index,14,decoder)=="Bottle Lamp"&&language_seen==1);
    write(make(master,false));check(resolve_object_pickup_name(*index.unique(102),index,14,decoder)=="Authored label");
    auto wrong=master;at(wrong,28,999,4);write(make(wrong));rejects([&]{resolve_object_pickup_name(*index.unique(102),index,14);});
    write(make(master));rejects([&]{resolve_object_pickup_name(input[0],index,1);});
    rejects([&]{resolve_object_pickup_name(*index.unique(101),index,0);});
    auto long_decoder=[](auto,auto){return std::string(4097,'a');};rejects([&]{resolve_object_pickup_name(*index.unique(101),index,1,long_decoder);});
    auto invalid_decoder=[](auto,auto){return std::string("\xc0\x80",2);};rejects([&]{resolve_object_pickup_name(*index.unique(101),index,1,invalid_decoder);});
    input.push_back(input[0]);odh::DefinitionIndex ambiguous(input);
    check(!resolve_object_pickup_name(*ambiguous.unique(102),ambiguous,1));
    odh::DefinitionIndex absent({candidate(2,part)});check(!resolve_object_pickup_name(*absent.unique(102),absent,1));
    const auto archive=temp.path/"objects.far";const auto payload=make(master);std::string far(16,'\0');
    far.replace(0,8,"FAR!byAZ");at(far,8,1,4);far+=payload;at(far,12,far.size(),4);
    std::string manifest(20,'\0');at(manifest,0,1,4);at(manifest,4,payload.size(),4);at(manifest,8,payload.size(),4);
    at(manifest,12,16,4);const std::string asset="Lamp.iff";at(manifest,16,asset.size(),4);far+=manifest+asset;
    {std::ofstream out(archive,std::ios::binary);out.write(far.data(),far.size());}
    input.resize(2);for(auto& item:input){item.source=archive.string();item.asset=asset;}
    odh::DefinitionIndex packed(input);check(resolve_object_pickup_name(*packed.unique(102),packed,14)=="Bottle Lamp");
    for(auto& item:input)item.asset="Missing.iff";odh::DefinitionIndex missing(input);
    rejects([&]{resolve_object_pickup_name(*missing.unique(102),missing,1);});
}
odh::RasterFrame frame(unsigned w=1024,unsigned h=768){
    odh::RasterFrame result;result.width=w;result.height=h;
    result.pixels.assign(std::size_t(w)*h,{12,34,56,255});result.depth.assign(result.pixels.size(),42);
    result.owner_ids.assign(result.pixels.size(),77);result.tested_samples=29;result.shaded_samples=13;return result;
}
void cases(){
    const ObjectPickupText text{{"Delete %.","Press Delete or click here to sell % for $.","Cannot sell %."},"Sale value unavailable."};
    ObjectPickupContent content{{7,42},"Bottle Lamp",25,true};ObjectPickupView view(text);
    check(object_pickup_message(content,text)=="Press Delete or click here to sell Bottle Lamp for §25.");
    // The content exposes only the authenticated refund; catalog price65
    // cannot enter this presentation contract or replace the real refund25.
    content.refund=0;check(object_pickup_message(content,text)=="Delete Bottle Lamp.");
    content.refund=-2;check(object_pickup_message(content,text)=="Delete Bottle Lamp.");
    content.refund.reset();check(object_pickup_message(content,text)==text.unknown_value);
    content.can_sell=false;check(object_pickup_message(content,text)=="Cannot sell Bottle Lamp.");
    content.can_sell.reset();content.refund=25;check(object_pickup_message(content,text)==text.unknown_value);
    const auto compact=object_pickup_layout(800,600);
    check(compact.panel.x+compact.panel.width<=800&&compact.message.x+compact.message.width<=800);
    const auto l=object_pickup_layout(1024,768);
    check(l.panel.x==216&&l.panel.y==668&&l.panel.width==804&&l.panel.height==100);
    check(l.message.x==396&&l.message.y==681);
    for(const auto state:std::array<std::optional<bool>,3>{std::nullopt,false,true}){
        content.can_sell=state;content.refund=25;
        check(object_pickup_sell_hit(l,content,l.panel.x,l.panel.y)==(state&&*state));
        check(!object_pickup_sell_hit(l,content,l.panel.x-1,l.panel.y));
        check(!object_pickup_sell_hit(l,content,l.panel.x,l.panel.y+l.panel.height));
        content.refund.reset();check(!object_pickup_sell_hit(l,content,l.panel.x,l.panel.y));
    }
    content.can_sell=true;content.refund=25;
    auto painted=frame();const auto original=painted;std::string message;unsigned calls=0;
    view.draw(painted,content,[&](auto&,std::string_view value,const auto& box){
        ++calls;message=value;check(box.x==l.message.x&&box.width==l.message.width);
    });
    check(calls==1&&message.find("§25")!=std::string::npos&&painted.pixels==original.pixels);
    odh::BitmapImage image{2,1,32,true,{{255,0,255,255},{0,0,0,255}},0};content.image=&image;
    view.draw(painted,content);const int x=l.image.x+(l.image.width-2)/2,y=l.image.y+(l.image.height-1)/2;
    check(painted.pixels[std::size_t(y)*painted.width+x]==original.pixels[0]);
    check((painted.pixels[std::size_t(y)*painted.width+x+1]==std::array<std::uint8_t,4>{0,0,0,255}));
    painted=original;content.image_rgba=true;view.draw(painted,content);
    check((painted.pixels[std::size_t(y)*painted.width+x]==std::array<std::uint8_t,4>{255,0,255,255}));
    for(unsigned row=0;row<painted.height;++row)for(unsigned col=0;col<painted.width;++col)
        if(!l.image.contains(col,row))check(painted.pixels[std::size_t(row)*painted.width+col]==original.pixels[0]);
    check(painted.depth==original.depth&&painted.owner_ids==original.owner_ids&&painted.tested_samples==29&&painted.shaded_samples==13);
    for(const auto dimensions:std::array<std::array<unsigned,2>,3>{{{800,600},{320,240},{640,360}}}){
        auto small=frame(dimensions[0],dimensions[1]);const auto box=object_pickup_layout(small.width,small.height);
        check(box.message.width>0&&box.image.width>0&&box.panel.y>=0);view.draw(small,content);
    }
    image.pixels.pop_back();const auto untouched=painted.pixels;
    rejects([&]{view.draw(painted,content);});check(painted.pixels==untouched);
    content.image=nullptr;content.name.assign(4097,'x');rejects([&]{object_pickup_message(content,text);});
    content.name="Lamp";content.target.generation=0;rejects([&]{object_pickup_sell_hit(l,content,300,700);});
    content.target.generation=42;painted.pixels.pop_back();rejects([&]{view.draw(painted,content);});
    rejects([&]{object_pickup_layout(0,0);});
    auto oversized=text;oversized.messages[1].assign(8193,'a');rejects([&]{object_pickup_message(content,oversized);});
}
}
int main(){try{cases();names();std::cout<<"object pickup view checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
