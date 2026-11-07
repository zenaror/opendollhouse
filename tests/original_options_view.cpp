// SPDX-License-Identifier: MPL-2.0
// Synthetic original-format button strips exercise presentation and action bounds.
// These fixtures do not certify artwork or behavior from an installed game.
#include "original_options_view.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace odh;using namespace odh_host;
using Pixel=std::array<std::uint8_t,4>;
constexpr Pixel background{{7,9,11,255}},divider_color{{72,88,104,255}};
constexpr const char* button_keys[]={"CPanel/Buttons/OptSave.bmp","CPanel/Buttons/OptNbhd.bmp",
    "CPanel/Buttons/OptExit.bmp","CPanel/Buttons/OptGraphics.bmp",
    "CPanel/Buttons/OptSound.bmp","CPanel/Buttons/OptPlay.bmp"};
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void at(std::string& bytes,std::size_t offset,std::uint32_t value,unsigned width){
    for(unsigned n=0;n<width;++n)bytes.at(offset+n)=static_cast<char>(value>>(8*n));
}
void put(std::string& bytes,std::uint32_t value,unsigned width){
    for(unsigned n=0;n<width;++n)bytes.push_back(static_cast<char>(value>>(8*n)));
}
Pixel ink(unsigned button,unsigned state){
    return {static_cast<std::uint8_t>(20+button+20*state),
        static_cast<std::uint8_t>(60+button+20*state),
        static_cast<std::uint8_t>(120+button+20*state),255};
}
std::string bitmap(unsigned width,unsigned height,const std::function<Pixel(unsigned,unsigned)>& sample){
    const auto stride=(width*3+3)&~3U;
    std::string bytes(54+stride*height,'\0');at(bytes,0,0x4d42,2);at(bytes,2,static_cast<std::uint32_t>(bytes.size()),4);
    at(bytes,10,54,4);at(bytes,14,40,4);at(bytes,18,width,4);at(bytes,22,height,4);at(bytes,26,1,2);at(bytes,28,24,2);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const auto color=sample(x,y);const auto offset=54+(height-1-y)*stride+x*3;
        bytes[offset]=static_cast<char>(color[2]);bytes[offset+1]=static_cast<char>(color[1]);bytes[offset+2]=static_cast<char>(color[0]);
    }
    return bytes;
}
std::string strip(unsigned button){
    // Each8x8 state has a transparent border and its own distinctive color.
    // BMP24 has opaque decoded alpha; original buttons use magenta as mask.
    return bitmap(32,8,[=](unsigned x,unsigned y){
        const auto local=x%8;
        return local>=2&&local<6&&y>=2&&y<6?ink(button,x/8):Pixel{255,0,255,255};
    });
}
std::string far(const std::vector<std::pair<std::string,std::string>>& entries){
    std::string bytes="FAR!byAZ";put(bytes,1,4);put(bytes,0,4);std::vector<std::uint32_t> offsets;
    for(const auto& entry:entries){offsets.push_back(static_cast<std::uint32_t>(bytes.size()));bytes+=entry.second;}
    at(bytes,12,static_cast<std::uint32_t>(bytes.size()),4);put(bytes,static_cast<std::uint32_t>(entries.size()),4);
    for(std::size_t n=0;n<entries.size();++n){const auto& entry=entries[n];put(bytes,static_cast<std::uint32_t>(entry.second.size()),4);
        put(bytes,static_cast<std::uint32_t>(entry.second.size()),4);put(bytes,offsets[n],4);
        put(bytes,static_cast<std::uint32_t>(entry.first.size()),4);bytes+=entry.first;}
    return bytes;
}
void write(const std::filesystem::path& path,const std::string& bytes){
    std::ofstream output(path,std::ios::binary);output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    require(bool(output),"write isolated options artwork fixture");
}
struct TemporaryDirectory {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("odh-original-options-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory(){require(std::filesystem::create_directories(root/"game/UIGraphics"),"create isolated options fixture");}
    ~TemporaryDirectory(){std::error_code ec;std::filesystem::remove_all(root,ec);}
};
std::array<int,2> center(BuyUiRect box){return {box.x+box.width/2,box.y+box.height/2};}
const Pixel& pixel(const RasterFrame& frame,std::array<int,2> point){
    return frame.pixels[std::size_t(point[1])*frame.width+point[0]];
}
template<class F>void rejects(F operation){
    bool rejected=false;try{operation();}catch(const FormatError&){rejected=true;}
    require(rejected,"malformed options artwork must fail explicitly");
}
void cases(){
    TemporaryDirectory temp;const auto game=temp.root/"game",archive=game/"UIGraphics/UIGraphics.far";
    std::vector<std::pair<std::string,std::string>> entries;
    for(unsigned n=0;n<6;++n)entries.push_back({button_keys[n],strip(n)});
    entries.push_back({"CPanel/Backgrounds/OptionsToothpkHoriz.bmp",bitmap(201,2,[](auto,auto){return divider_color;})});
    entries.push_back({"CPanel/Backgrounds/OptionsToothpkVert.bmp",bitmap(2,89,[](auto,auto){return divider_color;})});
    write(archive,far(entries));const OriginalOptionsView view(game);
    const std::array<bool,6> all{{true,true,true,true,true,true}},none{};
    const auto desktop=original_options_layout(800,600),wide=original_options_layout(1024,768);
    for(unsigned n=0;n<6;++n)require(wide.buttons[n].x==desktop.buttons[n].x&&
        wide.buttons[n].y-desktop.buttons[n].y==168&&wide.buttons[n].width==desktop.buttons[n].width&&
        wide.buttons[n].height==desktop.buttons[n].height,
        "wider desktop anchors original-size option buttons to the bottom instead of scaling them");
    const std::array<std::array<unsigned,2>,3> extents{{{800,600},{1024,768},{320,240}}};
    for(const auto& extent:extents){
        const auto width=extent[0],height=extent[1];const auto layout=original_options_layout(width,height);
        RasterFrame baseline;baseline.width=width;baseline.height=height;baseline.pixels.assign(std::size_t(width)*height,background);
        baseline.depth.assign(baseline.pixels.size(),7);baseline.owner_ids.assign(baseline.pixels.size(),31);
        auto idle=baseline;view.draw(idle,all);
        require(!view.hit(layout,0,0,all)&&!view.hit(layout,int(width),int(height),all),"outside panel never selects an option");
        require(!view.hit(layout,center(layout.horizontal_divider)[0],center(layout.horizontal_divider)[1],all)&&
            !view.hit(layout,center(layout.vertical_divider)[0],center(layout.vertical_divider)[1],all),
            "drawn original dividers never become option actions");
        require(pixel(idle,center(layout.horizontal_divider))==divider_color&&pixel(idle,center(layout.vertical_divider))==divider_color,
            "both original divider resources appear at their scaled authored positions");
        for(unsigned n=0;n<6;++n){
            const auto box=layout.buttons[n];const auto inside=center(box);
            const std::array<int,2> transparent{{box.x+1,box.y+1}};
            require(box.width>0&&box.height>0&&box.x>=0&&box.y>=0&&
                box.x+box.width<=int(width)&&box.y+box.height<=int(height),"every option remains within desktop and compact viewport");
            require(view.hit(layout,inside[0],inside[1],all)==n&&pixel(idle,inside)==ink(n,0),
                "visible resting button selects its exact action index");
            require(view.hit(layout,transparent[0],transparent[1],all)==n&&pixel(idle,transparent)==background,
                "original option action rectangle remains clickable through transparent artwork without painting magenta");
            require(view.hit(layout,box.x,box.y,all)==n&&
                view.hit(layout,box.x+box.width-1,box.y+box.height-1,all)==n&&
                !view.hit(layout,box.x-1,box.y-1,all)&&
                !view.hit(layout,box.x+box.width,box.y+box.height,all),
                "option corners and interior share exact rectangle action bounds with no outside hit");
            auto hover=baseline;view.draw(hover,all,inside);
            require(pixel(hover,inside)==ink(n,2),"pointer on visible art draws original hover frame");
            auto pressed=baseline;view.draw(pressed,all,inside,n);
            require(pixel(pressed,inside)==ink(n,1),"press on the same visible button draws original pressed frame");
            auto other_pressed=baseline;view.draw(other_pressed,all,inside,(n+1)%6);
            require(pixel(other_pressed,inside)==ink(n,2),"another button's captured press does not depress the hovered button");
            auto outside=baseline;view.draw(outside,all,std::array<int,2>{0,0},n);
            require(pixel(outside,inside)==ink(n,0),"moving a captured press outside restores its resting frame");
            auto transparent_hover=baseline;view.draw(transparent_hover,all,transparent,n);
            require(pixel(transparent_hover,inside)==ink(n,1)&&pixel(transparent_hover,transparent)==background,
                "captured press through a transparent option corner uses the same active rectangle while preserving scene cutouts");
            auto enabled=all;enabled[n]=false;
            auto disabled=baseline;view.draw(disabled,enabled,inside,n);
            require(!view.hit(layout,inside[0],inside[1],enabled)&&pixel(disabled,inside)==ink(n,3),
                "disabled action remains noninteractive and disabled even under pointer and captured press");
            for(const auto* painted:{&idle,&hover,&pressed,&outside,&transparent_hover,&disabled})
                require(painted->depth==baseline.depth&&painted->owner_ids==baseline.owner_ids,
                    "options presentation never alters world depth or picking ownership");
        }
        auto disabled=baseline;view.draw(disabled,none,center(layout.buttons[0]),0);
        for(unsigned n=0;n<6;++n)require(pixel(disabled,center(layout.buttons[n]))==ink(n,3)&&
            !view.hit(layout,center(layout.buttons[n])[0],center(layout.buttons[n])[1],none),
            "all unavailable consumers remain disabled as one capability set");
        require(pixel(idle,{0,0})==background,"options preserve scene pixels outside their artwork");
    }
    const auto compact=original_options_layout(320,240);
    require(compact.buttons[0].width<desktop.buttons[0].width&&compact.buttons[0].height<desktop.buttons[0].height,
        "compact layout reduces authored artwork rather than overflowing the viewport");
    auto bad=entries;bad[0].second=bitmap(31,8,[](auto,auto){return ink(0,0);});write(archive,far(bad));
    const OriginalOptionsView broken(game);
    RasterFrame frame;frame.width=800;frame.height=600;frame.pixels.assign(800*600,background);
    rejects([&]{broken.draw(frame,all);});
    bad=entries;bad.erase(bad.begin());write(archive,far(bad));
    rejects([&]{OriginalOptionsView missing(game);});
}
}
int main(){try{cases();std::cout<<"original options view: state, mask and viewport checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
