#include "odh/sprite.hpp"
#include "odh/dgrp.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>

namespace {
std::uint32_t number(const std::string& s, std::uint32_t maximum) {
    const bool hex=s.size()>2 && s[0]=='0' && (s[1]=='x'||s[1]=='X');
    const unsigned base=hex?16:10;
    std::uint64_t value=0;
    if(s.empty())throw odh::FormatError("empty numeric selector");
    for(std::size_t i=hex?2:0;i<s.size();++i) {
        const char c=s[i];
        const unsigned digit=c>='0'&&c<='9'?static_cast<unsigned>(c-'0'):
            hex&&c>='a'&&c<='f'?static_cast<unsigned>(c-'a'+10):
            hex&&c>='A'&&c<='F'?static_cast<unsigned>(c-'A'+10):base;
        if(digit>=base)throw odh::FormatError("invalid numeric selector");
        value=value*base+digit;
        if(value>maximum)throw odh::FormatError("numeric selector outside supported range");
    }
    return static_cast<std::uint32_t>(value);
}
const odh::Chunk& unique(const std::vector<odh::Chunk>& chunks,const std::string& type,std::uint16_t id) {
    const odh::Chunk* found=nullptr;
    for(const auto& chunk:chunks)if(chunk.type==type && chunk.id==id) {
        if(found)throw odh::FormatError("ambiguous "+type+" chunk ID");
        found=&chunk;
    }
    if(!found)throw odh::FormatError(type+" chunk not found in selected IFF");
    return *found;
}
void write_bytes(const std::string& path,const void* data,std::size_t size) {
    if(size>static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        throw odh::FormatError("output byte count exceeds stream bounds");
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out)throw odh::FormatError("cannot open output "+path+" (partial outputs may remain)");
    if(size)out.write(static_cast<const char*>(data),static_cast<std::streamsize>(size));
    out.close();
    if(!out)throw odh::FormatError("cannot write output "+path+" (partial outputs may remain)");
}
}
int main(int argc,char** argv) { try {
    if(argc<4)throw odh::FormatError("Usage: odh-sprite FAR IFF_ENTRY OBJD_ID [--graphic INDEX --direction BYTE --zoom N --rotation 0..3 --layer INDEX --output PREFIX]");
    const auto objd_id=static_cast<std::uint16_t>(number(argv[3],65535));
    std::uint32_t graphic=0,direction=1,zoom=3,rotation=0,layer=0;
    std::string output;std::set<std::string> seen;
    for(int i=4;i<argc;) {
        const std::string opt=argv[i++];
        if(!seen.insert(opt).second)throw odh::FormatError("duplicate option");
        if(i==argc)throw odh::FormatError("incomplete option");
        const std::string value=argv[i++];
        if(opt=="--graphic")graphic=number(value,65535);
        else if(opt=="--direction")direction=number(value,255);
        else if(opt=="--zoom")zoom=number(value,UINT32_MAX);
        else if(opt=="--rotation")rotation=number(value,3);
        else if(opt=="--layer")layer=number(value,UINT32_MAX);
        else if(opt=="--output") {if(value.empty())throw odh::FormatError("empty output prefix");output=value;}
        else throw odh::FormatError("unknown option");
    }
    std::string json;
    odh::Spr2Frame frame{};
    with_far_entry(argv[1],argv[2],[&](const odh::View& view) {
        std::vector<odh::Chunk> chunks;
        const auto iff_version=odh::iff_chunks(view,[&](const auto& chunk){chunks.push_back(chunk);});
        const auto obj=odh::read_objd_graphics(unique(chunks,"OBJD",objd_id).payload);
        if(graphic>=obj.num_graphics)throw odh::FormatError("graphic index outside OBJD graphics count");
        const auto group_id=std::uint32_t{obj.base_graphic}+graphic;
        if(group_id>65535)throw odh::FormatError("OBJD graphic group ID exceeds uint16");
        const auto group=odh::read_dgrp(unique(chunks,"DGRP",static_cast<std::uint16_t>(group_id)).payload);
        const auto* image=odh::dgrp_image(group,static_cast<std::uint8_t>(direction),zoom,static_cast<std::uint8_t>(rotation));
        if(!image)throw odh::FormatError("DGRP direction and zoom not found");
        if(layer>=image->sprites.size())throw odh::FormatError("DGRP layer index outside image");
        const auto& reference=image->sprites[layer];
        if(reference.sprite_id>65535)throw odh::FormatError("DGRP SPR2 reference exceeds uint16");
        const auto& spr=unique(chunks,"SPR2",static_cast<std::uint16_t>(reference.sprite_id)).payload;
        if(spr.le32(0)!=1000)throw odh::FormatError("unsupported SPR2 version (supports 1000)");
        const auto palette_id=static_cast<std::uint16_t>(spr.le32(8));
        const auto palette=odh::read_palt(unique(chunks,"PALT",palette_id).payload);
        frame=odh::read_spr2_frame(spr,reference.frame,palette);
        std::size_t opaque=0,partial=0,transparent=0;
        unsigned zmin=255,zmax=0;bool visible_z=false;
        for(std::size_t i=0;i<frame.pixels.size();++i) {
            const auto alpha=frame.pixels[i][3];
            if(alpha==0)++transparent;else if(alpha==255)++opaque;else ++partial;
            if(alpha!=0 && !frame.z.empty()){visible_z=true;zmin=std::min(zmin,unsigned(frame.z[i]));zmax=std::max(zmax,unsigned(frame.z[i]));}
        }
        // A zero-sized frame can still have a Z flag; use flags rather than vector size.
        const bool has_z=(frame.flags&2U)!=0;
        std::ostringstream out;
        out<<"{\"evidence\":\"ODH_DGRP_SPR2_paletted_sprite_decode\",\"sources\":{\"far\":"<<json_bytes(argv[1])<<",\"iff_entry\":"<<json_bytes(argv[2])<<",\"iff_version\":"<<json_bytes(iff_version)<<"},\"objd\":{\"id\":"<<objd_id<<",\"version\":"<<obj.version<<",\"base_graphic\":"<<obj.base_graphic<<",\"num_graphics\":"<<obj.num_graphics<<",\"graphic_index\":"<<graphic<<",\"group_id\":"<<group_id<<"},\"dgrp\":{\"version\":"<<group.version<<",\"requested_direction\":"<<direction<<",\"rotation\":"<<rotation<<",\"direction_rotated\":"<<image->direction<<",\"zoom\":"<<image->zoom<<",\"layer\":"<<layer<<",\"sprite_id\":"<<reference.sprite_id<<",\"frame\":"<<reference.frame<<",\"flags\":"<<reference.flags<<",\"x\":"<<reference.x<<",\"y\":"<<reference.y<<",\"object_offset_bits\":["<<reference.object_offset_bits[0]<<','<<reference.object_offset_bits[1]<<','<<reference.object_offset_bits[2]<<"],\"legacy_type\":";
        if(reference.legacy_type)out<<*reference.legacy_type;else out<<"null";
        out<<",\"remaining_bytes\":"<<group.remaining_bytes<<"},\"spr2\":{\"version\":"<<frame.version<<",\"default_palette_id\":"<<frame.default_palette_id<<",\"palette_id_raw\":"<<frame.palette_id<<",\"effective_palette_id\":"<<frame.effective_palette_id<<",\"transparent_index\":"<<frame.transparent_index<<",\"flags\":"<<frame.flags<<",\"width\":"<<frame.width<<",\"height\":"<<frame.height<<",\"x\":"<<frame.x<<",\"y\":"<<frame.y<<",\"ended_with_marker\":"<<(frame.ended_with_marker?"true":"false")<<",\"remaining_bytes\":"<<frame.remaining_bytes<<"},\"palette\":{\"version\":"<<palette.version<<",\"size\":"<<palette.colors.size()<<",\"reserved\":[";
        for(std::size_t i=0;i<palette.reserved.size();++i){if(i)out<<',';out<<unsigned(palette.reserved[i]);}
        out<<"],\"remaining_bytes\":"<<palette.remaining_bytes<<"},\"stats\":{\"opaque\":"<<opaque<<",\"partial\":"<<partial<<",\"transparent\":"<<transparent<<",\"has_z\":"<<(has_z?"true":"false")<<",\"z_min\":";
        if(visible_z)out<<zmin;else out<<"null";
        out<<",\"z_max\":";if(visible_z)out<<zmax;else out<<"null";
        out<<"},\"flip_required\":"<<((reference.flags&1U)?"true":"false")<<",\"export_pixel_orientation\":\"raw_frame_unflipped\",\"placement\":{\"profile\":\"raw_frame_only_not_calibrated\",\"original_calibrated\":false,\"origin_applied\":false,\"crop_applied\":false,\"dgrp_offsets_applied\":false},\"output\":";
        if(output.empty())out<<"null";
        else {out<<"{\"rgba\":"<<json_bytes(output+".rgba")<<",\"idx\":"<<json_bytes(output+".idx")<<",\"z\":";if(has_z)out<<json_bytes(output+".z");else out<<"null";out<<'}';}
        out<<"}\n";json=out.str();
    });
    if(!output.empty()) {
        static_assert(sizeof(std::array<std::uint8_t,4>)==4,"RGBA array must be packed");
        write_bytes(output+".rgba",frame.pixels.data(),frame.pixels.size()*4);
        write_bytes(output+".idx",frame.indices.data(),frame.indices.size());
        if(frame.flags&2U)write_bytes(output+".z",frame.z.data(),frame.z.size());
    }
    std::cout<<json;return 0;
} catch(const std::exception& e) {std::cerr<<"odh-sprite: "<<e.what()<<'\n';return 2;} }
