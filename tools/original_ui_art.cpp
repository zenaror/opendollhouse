// SPDX-License-Identifier: MPL-2.0
#include "original_ui_art.hpp"
#include <algorithm>
#include <fstream>

namespace odh_host {
namespace {
std::string folded(std::string key){
    for(auto& c:key){
        if(c=='/')c='\\';
        else if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    }
    return key;
}
// Complete Collection's current control-panel backgrounds are uncompressed
// 32-bit TGA, unlike the older BMP panel fragments retained in the same FAR.
// Keep this intentionally bounded to their true-colour/8-bit-alpha profile.
odh::BitmapImage read_ui_tga(const odh::View& view,const odh::BitmapLimits& limits){
    std::array<std::uint8_t,18> header{};view.read(0,header.data(),header.size());
    if(header[1]!=0||header[2]!=2||header[16]!=32||(header[17]&15)!=8||
       (header[17]&0xc0)||view.le16(3)||view.le16(5)||header[7])
        throw odh::FormatError("unsupported original UI TGA profile");
    odh::BitmapImage image{};image.width=view.le16(12);image.height=view.le16(14);
    image.bit_count=32;image.top_down=(header[17]&0x20)!=0;
    const auto count=std::uint64_t(image.width)*image.height;
    const auto offset=std::uint64_t(18)+header[0],end=offset+count*4;
    if(!image.width||!image.height||image.width>limits.width||image.height>limits.height||
       count>limits.pixels||count>image.pixels.max_size()||end>view.size())
        throw odh::FormatError("original UI TGA extent exceeds its payload or pixel budget");
    // Original files have the standard26-byte footer. Extension/developer
    // areas are outside this profile; a truncated/unknown tail is rejected.
    if(view.size()!=end){
        constexpr std::array<char,18> signature{{'T','R','U','E','V','I','S','I','O','N','-','X','F','I','L','E','.',0}};
        std::array<char,18> actual{};
        if(view.size()!=end+26||view.le32(end)||view.le32(end+4))
            throw odh::FormatError("unsupported original UI TGA trailing data");
        view.read(end+8,actual.data(),actual.size());
        if(actual!=signature)throw odh::FormatError("original UI TGA footer signature invalid");
    }
    image.pixels.resize(static_cast<std::size_t>(count));
    std::vector<std::uint8_t> row(static_cast<std::size_t>(image.width)*4);
    for(std::uint32_t y=0;y<image.height;++y){
        view.read(offset+std::uint64_t(y)*row.size(),row.data(),row.size());
        const auto output_y=image.top_down?y:image.height-1-y;
        for(std::uint32_t x=0;x<image.width;++x){
            const auto output_x=header[17]&0x10?image.width-1-x:x;
            image.pixels[static_cast<std::size_t>(output_y)*image.width+output_x]=
                {row[x*4+2],row[x*4+1],row[x*4],row[x*4+3]};
        }
    }
    image.remaining_bytes=view.size()-end;return image;
}
}

std::optional<odh::BitmapImage> read_object_catalog_icon(const odh::View& view,std::uint16_t strings_id){
    std::optional<odh::BitmapImage> image;
    odh::iff_chunks(view,[&](const odh::Chunk& chunk){
        if(chunk.type!="BMP_"||chunk.id!=strings_id)return;
        if(image)throw odh::FormatError("duplicate object catalog icon");
        odh::BitmapLimits limits;limits.width=limits.height=256;limits.pixels=65536;
        image=odh::read_bmp(chunk.payload,limits,odh::BmpExtentPolicy::iff_container);
    });
    return image;
}

OriginalUiArt::OriginalUiArt(const std::filesystem::path& game_root,const std::vector<std::string>& required,
        const std::vector<std::string>& optional,std::size_t pixel_limit){
    const auto root=std::filesystem::canonical(game_root);
    const auto path=std::filesystem::canonical(root/"UIGraphics/UIGraphics.far");
    const auto relative=path.lexically_relative(root);
    if(relative.empty()||relative.is_absolute()||std::any_of(relative.begin(),relative.end(),
        [](const auto& part){return part=="..";}))throw odh::FormatError("original UI archive is outside selected installation");
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw odh::FormatError("cannot open original UI archive");
    odh::View file(stream,std::filesystem::file_size(path));
    odh::Limits limits;limits.far_entries=16384;limits.far_name_bytes=1024*1024;
    std::map<std::string,odh::FarEntry> entries;
    for(auto& entry:odh::far1_index(file,limits)){
        auto key=folded(entry.name);
        if(!entries.emplace(std::move(key),std::move(entry)).second)
            throw odh::FormatError("ambiguous original UI archive entry");
    }
    const auto load=[&](const std::string& name,bool needed){
        const auto key=folded(name);
        if(images_.count(key))return;
        const auto found=entries.find(key);
        if(found==entries.end()){
            if(needed)throw odh::FormatError("required original UI artwork absent: "+name);
            return;
        }
        odh::BitmapLimits decode;
        decode.pixels=static_cast<std::uint32_t>(std::min<std::size_t>(decode.pixels,pixel_limit));
        const auto& asset=found->second;
        const auto payload=file.subview(asset.offset,asset.length);
        auto image=key.size()>=4&&key.compare(key.size()-4,4,".tga")==0?
            read_ui_tga(payload,decode):odh::read_bmp(payload,decode);
        pixel_limit-=image.pixels.size();
        images_.emplace(key,std::move(image));
    };
    for(const auto& key:required)load(key,true);
    for(const auto& key:optional)load(key,false);
}

const odh::BitmapImage* OriginalUiArt::find(const std::string& name) const{
    const auto found=images_.find(folded(name));
    return found==images_.end()?nullptr:&found->second;
}
const odh::BitmapImage& OriginalUiArt::at(const std::string& name) const{
    const auto* image=find(name);
    if(!image)throw odh::FormatError("original UI artwork unavailable: "+name);
    return *image;
}
}
