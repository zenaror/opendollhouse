// SPDX-License-Identifier: MPL-2.0
#include "original_cursors.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace odh_host;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void at(std::string& s,unsigned offset,unsigned value,unsigned width=4){for(unsigned i=0;i<width;++i)s.at(offset+i)=static_cast<char>(value>>(8*i));}
std::string fixture(){
    std::string s(86,'\0');at(s,2,2,2);at(s,4,1,2);s[6]=s[7]=2;at(s,10,1,2);at(s,12,1,2);at(s,14,64);at(s,18,22);
    at(s,22,40);at(s,26,2);at(s,30,4);at(s,34,1,2);at(s,36,1,2);
    s[66]=s[67]=s[68]=static_cast<char>(255);
    s[70]=s[74]=0x40;s[78]=static_cast<char>(0xc0);
    return s;
}
void write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;need(bool(file),"write cursor fixture");}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("odh-cursor-bank-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path directory=path/"UIGraphics/Shared/cursors";
    Temp(){need(std::filesystem::create_directories(directory),"create isolated cursor bank");}
    ~Temp(){std::error_code ec;std::filesystem::remove_all(path,ec);}
};
template<class F> void reject(F function){bool failed=false;try{function();}catch(const odh::FormatError&){failed=true;}need(failed,"bad cursor bank admitted");}
void cases(){
    Temp temp;need(original_cursor_count==51,"complete original cursor inventory");
    for(std::size_t i=0;i<original_cursor_count;++i)write(temp.directory/original_cursor_filename(static_cast<OriginalCursorAsset>(i)),fixture());
    OriginalCursors bank(temp.path);
    need(bank.image_for_path("Shared/cursors/Arrow.cur")==&bank.at(OriginalCursorAsset::Arrow).images.front(),"native resource path binds the actual validated loose asset");
    need(bank.image_for_path("sHaReD\\CuRsOrS\\bUlLdOzE.CUR")==&bank.at(OriginalCursorAsset::BULLDOZE).images.front(),"native lookup normalizes only slash/case");
    need(!bank.image_for_path("../Shared/cursors/Arrow.cur")&&!bank.image_for_path("Shared/cursors/unknown.cur"),"unknown/traversal paths never become filesystem lookups");
    for(std::size_t i=0;i<original_cursor_count;++i){const auto& cursor=bank.at(static_cast<OriginalCursorAsset>(i));
        need(cursor.images.size()==1&&cursor.images[0].width==2&&cursor.images[0].hotspot_x==1&&cursor.images[0].invert[3]==1,
            "all allowlisted identities retain owned hotspot and inversion");}
    need(std::string(original_cursor_filename(OriginalCursorAsset::Arrow))=="Arrow.cur"&&
        std::string(original_cursor_filename(OriginalCursorAsset::LivePerson))=="LivePerson.cur"&&
        std::string(original_cursor_filename(OriginalCursorAsset::edit))=="edit.cur","source file identities are not gameplay-role aliases");
    reject([&]{bank.at(OriginalCursorAsset::count);});reject([&]{original_cursor_filename(OriginalCursorAsset::count);});
    reject([&]{bank.status(OriginalCursorAsset::count);});
    const auto move_in=temp.directory/"MOVEIN.cur";
    auto icon=fixture();at(icon,2,1,2);at(icon,10,0,2);at(icon,12,0,2);
    write(move_in,icon);
    {
        const OriginalCursors icon_bank(temp.path);
        need(!icon_bank.image_for_path("Shared/cursors/MOVEIN.cur"),"ICO without a hotspot cannot become a native cursor");
        need(icon_bank.at(OriginalCursorAsset::MOVEIN).images.empty()&&
            icon_bank.status(OriginalCursorAsset::MOVEIN)==OriginalCursorStatus::unsupported_icon_without_hotspot&&
            std::string(icon_bank.diagnostic(OriginalCursorAsset::MOVEIN)).find("hotspot")!=std::string::npos,
            "shipped ICO exception exposes no fabricated hotspot or cursor image");
        need(icon_bank.status(OriginalCursorAsset::Arrow)==OriginalCursorStatus::ready&&icon_bank.at(OriginalCursorAsset::Arrow).images.size()==1,
            "unsupported MOVEIN cannot disable valid Arrow or other CUR assets");
    }
    // This is a specific allowlisted original exception, not a parser bypass
    // for arbitrary icon files replacing supported cursors.
    write(temp.directory/"Arrow.cur",icon);reject([&]{OriginalCursors bad(temp.path);});write(temp.directory/"Arrow.cur",fixture());
    auto truncated_icon=icon.substr(0,6);write(move_in,truncated_icon);reject([&]{OriginalCursors bad(temp.path);});
    write(move_in,fixture());
    const auto arrow=temp.directory/"Arrow.cur";
    std::filesystem::rename(arrow,temp.directory/"unrecognized.cur");reject([&]{OriginalCursors bad(temp.path);});
    std::filesystem::rename(temp.directory/"unrecognized.cur",arrow);
    write(temp.directory/"ARROW.CUR",fixture());reject([&]{OriginalCursors bad(temp.path);});std::filesystem::remove(temp.directory/"ARROW.CUR");
    std::filesystem::rename(arrow,temp.directory/"arrow.CUR");
    need(OriginalCursors(temp.path).at(OriginalCursorAsset::Arrow).images.size()==1,"single casefolded filename is accepted");
    std::filesystem::rename(temp.directory/"arrow.CUR",arrow);
    const auto outside=temp.path/"outside.cur";write(outside,fixture());std::filesystem::remove(arrow);
    std::filesystem::create_symlink(outside,arrow);reject([&]{OriginalCursors bad(temp.path);});std::filesystem::remove(arrow);write(arrow,fixture());
    write(temp.directory/"water.cur","bad");reject([&]{OriginalCursors bad(temp.path);});write(temp.directory/"water.cur",fixture());
    OriginalCursorLimits limits;limits.pixels=203;reject([&]{OriginalCursors bad(temp.path,limits);});
    limits={};limits.directory_entries=50;reject([&]{OriginalCursors bad(temp.path,limits);});
    limits={};limits.cursor.bytes=1;reject([&]{OriginalCursors bad(temp.path,limits);});
    for(std::size_t i=0;i<original_cursor_count;++i)write(temp.directory/original_cursor_filename(static_cast<OriginalCursorAsset>(i)),"invalid now");
    need(bank.at(OriginalCursorAsset::Arrow).images[0].pixels[1]==std::array<std::uint8_t,4>{255,255,255,255},"lookup never reopens modified files");
}
void installed(const char* root){
    OriginalCursors bank(root);std::size_t images=0,pixels=0,inversions=0;
    for(std::size_t i=0;i<original_cursor_count;++i){const auto id=static_cast<OriginalCursorAsset>(i);
        if(bank.status(id)!=OriginalCursorStatus::ready)std::cout<<original_cursor_filename(id)<<" unsupported: "<<bank.diagnostic(id)<<'\n';
        for(const auto& image:bank.at(id).images){++images;pixels+=image.pixels.size();for(auto invert:image.invert)inversions+=invert;
            std::cout<<original_cursor_filename(id)<<' '<<image.width<<'x'<<image.height<<" hotspot="<<image.hotspot_x<<','<<image.hotspot_y<<'\n';}}
    std::cout<<"files="<<original_cursor_count<<" images="<<images<<" pixels="<<pixels<<" invert="<<inversions<<'\n';
}
}
int main(int argc,char** argv){try{
    if(argc==1)cases();else if(argc==2)installed(argv[1]);else throw std::runtime_error("usage: original_cursors [game-root]");return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
