// SPDX-License-Identifier: MPL-2.0
#include "object_pickup_view.hpp"
#include "buy_mode_view.hpp"
#include "resource.hpp"
#include "odh/animation.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace odh_host {
namespace {
bool utf8(std::string_view text){
    for(std::size_t i=0;i<text.size();){
        const auto first=static_cast<unsigned char>(text[i++]);
        if(first<128)continue;
        unsigned count=0;std::uint32_t value=0,minimum=0;
        if(first>=0xc2&&first<=0xdf){count=1;value=first&31;minimum=0x80;}
        else if(first>=0xe0&&first<=0xef){count=2;value=first&15;minimum=0x800;}
        else if(first>=0xf0&&first<=0xf4){count=3;value=first&7;minimum=0x10000;}
        else return false;
        if(count>text.size()-i)return false;
        while(count--){const auto byte=static_cast<unsigned char>(text[i++]);
            if((byte&0xc0)!=0x80)return false;value=(value<<6)|(byte&63);}
        if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    }
    return true;
}
void validate(const ObjectPickupContent& c){
    if(c.target.id<=0||!c.target.generation||c.name.size()>4096||c.name.find('\0')!=std::string::npos)
        throw odh::FormatError("object pickup identity or name invalid");
    if(c.image&&(!c.image->width||!c.image->height||c.image->width>1024||c.image->height>1024||
        c.image->pixels.size()!=std::uint64_t(c.image->width)*c.image->height))
        throw odh::FormatError("object pickup image invalid");
}
}
std::optional<std::string> resolve_object_pickup_name(const odh::DefinitionCandidate& requested,
        const odh::DefinitionIndex& index,std::uint8_t language,
        std::function<std::string(std::string_view,std::uint8_t)> decoder){
    if(language<1||language>20||index.unique(requested.definition.guid)!=&requested)
        throw odh::FormatError("object pickup name needs exact indexed identity and language");
    const auto* selected=&requested;
    if(requested.definition.master_id&&requested.definition.sub_index!=-1){
        selected=index.unique_master(requested);if(!selected)return {};
    }
    std::optional<std::string> result;
    const auto read=[&](const odh::View& resource){
        std::optional<std::uint16_t> strings_id;std::string fallback;
        std::map<std::uint16_t,odh::View> tables;bool found=false;
        odh::iff_chunks(resource,[&](const odh::Chunk& chunk){
            if(chunk.type=="CTSS"){
                if(!tables.emplace(chunk.id,chunk.payload).second)throw odh::FormatError("duplicate pickup CTSS");
            }else if(chunk.type=="OBJD"&&chunk.id==selected->chunk_id){
                if(found)throw odh::FormatError("duplicate pickup OBJD");found=true;
                const auto actual=odh::read_object_definition(chunk.payload);const auto& expected=selected->definition;
                if(actual.guid!=expected.guid||actual.master_id!=expected.master_id||actual.sub_index!=expected.sub_index||
                    actual.version!=expected.version||actual.object_type!=expected.object_type)
                    throw odh::FormatError("pickup OBJD differs from indexed identity");
                strings_id=chunk.payload.le16(82);fallback=chunk.label;
            }
        });
        if(!found)throw odh::FormatError("pickup indexed OBJD absent");
        std::uint8_t text_language=1;std::string name=fallback;
        const auto table=tables.find(*strings_id);
        if(table!=tables.end()){
            odh::AnimationLimits limits;limits.text_bytes=1024*1024;limits.strings=4096;
            const auto strings=odh::read_strings(table->second,limits);
            if(const auto* value=odh::string_in_language_or_english(strings,language,0)){
                name=value->value;text_language=value->language;
            }
        }
        if(decoder)name=decoder(name,text_language);
        if(name.size()>4096||name.find('\0')!=std::string::npos||!utf8(name))
            throw odh::FormatError("pickup name exceeds text bounds");
        if(!name.empty())result=std::move(name);
    };
    if(selected->asset.empty())with_iff_file(selected->source,read);
    else with_far_entry(selected->source,selected->asset,read);
    return result;
}
ObjectPickupLayout object_pickup_layout(std::uint32_t width,std::uint32_t height){
    const auto hud=original_control_panel_layout(width,height);const auto s=hud.scale;
    const auto px=[&](int n){return int(std::lround(n*s));};
    auto panel=hud.panel_background;
    panel.width=std::max(0,std::min(panel.width,int(width)-panel.x));
    panel.height=std::max(0,std::min(panel.height,int(height)-panel.y));
    return {panel,{panel.x+px(70),panel.y+px(10),px(100),px(80)},
        {panel.x+px(180),panel.y+px(13),std::max(1,panel.width-px(194)),px(74)}};
}
std::string object_pickup_message(const ObjectPickupContent& c,const ObjectPickupText& text){
    validate(c);
    const std::string* pattern=&text.unknown_value;
    if(c.can_sell&&!*c.can_sell)pattern=&text.messages[2];
    else if(c.can_sell&&*c.can_sell&&c.refund)pattern=&text.messages[*c.refund>0?1:0];
    if(pattern->empty())pattern=&text.unknown_value;
    if(pattern->size()>8192)throw odh::FormatError("object pickup message exceeds bounds");
    std::string result;const auto money=c.refund?"§"+std::to_string(*c.refund):std::string{};
    for(const char ch:*pattern){
        if(ch=='%')result+=c.name;
        else if(ch=='$'&&c.refund)result+=money;
        else result+=ch;
        if(result.size()>16384)throw odh::FormatError("object pickup expanded message exceeds bounds");
    }
    return result;
}
bool object_pickup_sell_hit(const ObjectPickupLayout& layout,const ObjectPickupContent& c,int x,int y){
    validate(c);return c.can_sell&&*c.can_sell&&c.refund&&layout.panel.contains(x,y);
}
ObjectPickupView::ObjectPickupView(ObjectPickupText text):text_(std::move(text)){}
void ObjectPickupView::draw(odh::RasterFrame& frame,const ObjectPickupContent& c,ObjectPickupTextPainter painter)const{
    const auto message=object_pickup_message(c,text_);const auto layout=object_pickup_layout(frame.width,frame.height);
    if(frame.pixels.size()!=std::uint64_t(frame.width)*frame.height)
        throw odh::FormatError("object pickup frame pixels invalid");
    if(c.image){
        const auto& image=*c.image;
        const double scale=std::min({1.,double(layout.image.width)/image.width,double(layout.image.height)/image.height});
        const auto w=std::max(1,int(image.width*scale)),h=std::max(1,int(image.height*scale));
        const odh::UiBitmapRect box{layout.image.x+(layout.image.width-w)/2,layout.image.y+(layout.image.height-h)/2,w,h};
        if(c.image_rgba)odh::blit_rgba_bitmap(frame,image,box);
        else odh::blit_color_keyed_bitmap(frame,image,box,std::nullopt,{255,0,255});
    }
    if(painter)painter(frame,message,layout.message);
}
}
