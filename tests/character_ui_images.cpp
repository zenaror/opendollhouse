// SPDX-License-Identifier: MPL-2.0
#include "character_ui_images.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace odh_host;
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void at(std::string& s,std::size_t offset,std::uint32_t value,unsigned width,bool big=false){
    for(unsigned i=0;i<width;++i)s.at(offset+i)=static_cast<char>(value>>(8*(big?width-1-i:i)));
}
std::array<std::uint8_t,4> pixel(unsigned x,unsigned y){
    // Include the original blue background as actual opaque RGB data.
    return x==0?std::array<std::uint8_t,4>{0,0,255,255}:
        std::array<std::uint8_t,4>{static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y),67,255};
}
std::string bitmap(unsigned width,unsigned height,bool zero_extent=true,bool top_down=false){
    const auto stride=(width*3+3)&~3u;
    std::string s(54+stride*height,'\0');at(s,0,0x4d42,2);at(s,2,zero_extent?0:static_cast<unsigned>(s.size()),4);
    at(s,10,54,4);at(s,14,40,4);at(s,18,width,4);at(s,22,top_down?0u-height:height,4);
    at(s,26,1,2);at(s,28,24,2);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const auto color=pixel(x,y);const auto offset=54+(top_down?y:height-1-y)*stride+x*3;
        s[offset]=static_cast<char>(color[2]);s[offset+1]=static_cast<char>(color[1]);s[offset+2]=static_cast<char>(color[0]);
    }
    return s;
}
std::string chunk(unsigned id,const std::string& payload){
    std::string s(76,'\0');s.replace(0,4,"BMP_");at(s,4,static_cast<unsigned>(76+payload.size()),4,true);at(s,8,id,2,true);
    return s+payload;
}
std::string iff(const std::vector<std::string>& chunks){
    std::string s="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";s.resize(64,'\0');
    for(const auto& c:chunks)s+=c;
    return s;
}
CharacterUiImages read(const std::string& bytes,CharacterUiImageLimits limits={}){
    std::istringstream stream(bytes,std::ios::binary);return read_character_ui_images(odh::View(stream,bytes.size()),limits);
}
void rejects(const std::string& bytes,CharacterUiImageLimits limits={}){
    bool rejected=false;try{(void)read(bytes,limits);}catch(const odh::FormatError&){rejected=true;}
    need(rejected,"malformed/ambiguous/over-budget character image accepted");
}
void cases(){
    for(const bool zero:{false,true})for(const bool top:{false,true}){
        // Source stream is destroyed before checking the returned owned pixels.
        const auto result=read(iff({chunk(2002,bitmap(105,41,zero,top)),chunk(2003,bitmap(200,40,zero,top)),
            chunk(2004,bitmap(25,25,zero,top)),chunk(2005,bitmap(16,16,zero,top)),chunk(2006,bitmap(32,32,zero,top))}));
        need(result.faces.size()==3&&result.relations.size()==5,"authored frame counts/order");
        for(const auto id:{2002,2003}){
            const auto bytes=id==2002?bitmap(105,41,zero,top):bitmap(200,40,zero,top);
            const auto single=read_character_ui_image(static_cast<std::uint16_t>(id),
                odh::View(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()));
            const auto& cells=id==2002?single.faces:single.relations;
            const auto& expected=id==2002?result.faces:result.relations;
            need(cells.size()==expected.size(),"requested strip has wrong number of frames");
            for(std::size_t i=0;i<cells.size();++i)
                need(cells[i].pixels==expected[i].pixels,"requested strip differs from full character");
        }
        for(const auto* strip:{&result.faces,&result.relations}){
            const auto width=strip==&result.faces?35u:40u,height=strip==&result.faces?41u:40u;
            for(unsigned frame=0;frame<strip->size();++frame){const auto& image=strip->at(frame);
                need(image.width==width&&image.height==height&&image.top_down==top,"strip cell metadata");
                for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
                    need(image.pixels[y*width+x]==pixel(frame*width+x,y),"exact horizontal frame crop/orientation/RGB");
            }
        }
        need(result.thumbnail&&result.speech_medium&&result.speech_large,"all optional image IDs");
        for(const auto* image:{&*result.thumbnail,&*result.speech_medium,&*result.speech_large})
            for(unsigned y=0;y<image->height;++y)for(unsigned x=0;x<image->width;++x)
                need(image->pixels[y*image->width+x]==pixel(x,y),"single images preserve blue background and owned pixels");
    }
    auto empty=read(iff({chunk(2001,"unrelated malformed BMP ignored")}));
    need(empty.faces.empty()&&empty.relations.empty()&&!empty.thumbnail&&!empty.speech_medium&&!empty.speech_large,"absence remains optional");
    need(read(iff({chunk(2002,bitmap(3,1))})).faces[0].width==1,"divisible non-default strip admitted");
    need(read(iff({chunk(2003,bitmap(640,128))})).relations[4].width==128,"bounded maximum cell admitted");
    for(unsigned id=2002;id<=2006;++id){
        rejects(iff({chunk(id,bitmap(15,1)),chunk(id,bitmap(15,1))}));
        rejects(iff({chunk(id,"broken BMP")}));
    }
    rejects(iff({chunk(2002,bitmap(104,41))}));rejects(iff({chunk(2003,bitmap(199,40))}));
    rejects(iff({chunk(2002,bitmap(387,1))}));rejects(iff({chunk(2004,bitmap(1,129))}));
    const auto bytes=iff({chunk(2002,bitmap(6,2)),chunk(2003,bitmap(10,2))});
    CharacterUiImageLimits limits;limits.iff_bytes=bytes.size()-1;rejects(bytes,limits);
    limits={};limits.bitmap_bytes=1;rejects(bytes,limits);
    limits={};limits.pixels=31;rejects(bytes,limits); // Each bitmap fits alone, their sum does not.
    limits={};limits.chunks=1;rejects(bytes,limits);
    limits={};limits.cell_extent=1;rejects(bytes,limits);
    limits={};limits.cell_extent=129;rejects(bytes,limits);
    auto truncated=bitmap(3,1);truncated.pop_back();rejects(iff({chunk(2002,truncated)}));
    auto bad_extent=bitmap(3,1);at(bad_extent,2,54,4);rejects(iff({chunk(2002,bad_extent)}));
}
}
int main(){try{cases();
    const std::vector<std::pair<std::int16_t,std::size_t>> scores{{-32768,0},{-100,0},{-61,0},{-60,1},{-21,1},{-20,2},{19,2},{20,3},{59,3},{60,4},{100,4},{32767,4}};
    for(const auto& item:scores)need(character_relationship_frame(item.first)==item.second,"original long-term relation icon boundary");
    need(character_relationship_frame(std::nullopt)==2,"missing relation selects neutral image without inserting a score");
    return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
