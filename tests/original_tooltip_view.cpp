// SPDX-License-Identifier: MPL-2.0
#include "original_tooltip_view.hpp"
#include "linux_text_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace odh_host;
using Pixel=std::array<std::uint8_t,4>;
constexpr Pixel background{12,34,56,255},yellow{255,255,230,255},black{0,0,0,255};
void need(bool condition,const char* why){if(!condition)throw std::runtime_error(why);}
template<class F> void reject(F function){bool rejected=false;try{function();}catch(const std::invalid_argument&){rejected=true;}need(rejected,"invalid tooltip input accepted");}
odh::RasterFrame frame(unsigned width,unsigned height){
    odh::RasterFrame out{};out.width=width;out.height=height;out.pixels.assign(std::size_t(width)*height,background);
    out.depth.assign(out.pixels.size(),42);out.owner_ids.assign(out.pixels.size(),77);out.shaded_samples=13;out.tested_samples=29;return out;
}
void cases(){
    auto price=frame(800,600);const auto oldDepth=price.depth;const auto oldOwners=price.owner_ids;
    const auto priceBox=draw_original_tooltip(price,"$7.999",14,{750,550},1,{255,0,0,255});
    need(bool(priceBox),"unaffordable catalog price has tooltip geometry");
    bool redInk=false;
    for(int y=priceBox->y+1;y<priceBox->y+priceBox->height-1;++y)
        for(int x=priceBox->x+1;x<priceBox->x+priceBox->width-1;++x){
            const auto& pixel=price.pixels[size_t(y)*price.width+x];
            redInk=redInk||(pixel[0]>pixel[1]+40&&pixel[0]>pixel[2]+40);
        }
    need(redInk&&price.depth==oldDepth&&price.owner_ids==oldOwners,"price foreground preserves world picking and depth");
    const auto measured=measure_text_overlay("Viagens",14,10);
    const auto layout=original_tooltip_layout(1024,768,"Viagens",14,{559,703});
    need(layout&&layout->x==559&&layout->width==measured.width+10&&layout->y+layout->height==689,
        "measured single name gets original pointer X, padding and fourteen-pixel gap");
    // Font metrics depend on the installed face. Exact48x15 original capture
    // remains a visual calibration target, not a fabricated test measurement.
    for(const auto dimensions:std::array<std::array<unsigned,2>,3>{{{800,600},{1024,768},{320,240}}})
        for(float scale:{.4f,1.f,2.f})for(const auto pointer:std::array<std::array<int,2>,5>{{{100,100},{0,0},{-100,-100},{3000,3000},{319,239}}}){
            auto original=frame(dimensions[0],dimensions[1]),painted=original;
            const auto box=original_tooltip_layout(original.width,original.height,"Viagens",14,pointer,scale);
            const auto result=draw_original_tooltip(painted,"Viagens",14,pointer,scale);
            need(box&&result&&box->x==result->x&&box->y==result->y&&box->width==result->width&&box->height==result->height,
                "layout and drawing share viewport/scale/font metrics");
            need(box->x>=0&&box->y>=0&&box->x+box->width<=int(original.width)&&box->y+box->height<=int(original.height)&&box->width<=300,
                "name tooltip clamps to every viewport edge within300pixels");
            const int edge=std::max(1,int(std::lround(scale)));
            std::size_t yellow_count=0,ink_count=0;
            for(unsigned y=0;y<original.height;++y)for(unsigned x=0;x<original.width;++x){
                const auto& color=painted.pixels[std::size_t(y)*original.width+x];
                if(!box->contains(int(x),int(y)))need(color==background,"tooltip never paints outside measured box");
                else{
                    const auto local_x=int(x)-box->x,local_y=int(y)-box->y;
                    const bool outline=local_x<edge||local_y<edge||local_x>=box->width-edge||local_y>=box->height-edge;
                    if(outline)need(color==black,"all four borders retain original solid black");
                    else{if(color==yellow)++yellow_count;if(color!=yellow)++ink_count;}
                }
            }
            need(yellow_count>0&&ink_count>0,"original pale yellow interior and dark name glyphs are both visible");
            need(painted.depth==original.depth&&painted.owner_ids==original.owner_ids&&painted.tested_samples==29&&painted.shaded_samples==13,
                "tooltip is presentation only and preserves picking/depth/counters");
        }
    need(original_tooltip_layout(1920,1080,"Viagens",14,{1800,1000}).has_value(),
        "tooltip accepts a large supported viewport without imposing a smaller frame budget");
    auto long_name=std::string("Um nome original suficientemente comprido para precisar de mais de uma linha sem incluir outra descrição");
    const auto wrapped=original_tooltip_layout(320,240,long_name,14,{300,200});
    need(wrapped&&wrapped->width<=300&&wrapped->height>layout->height,"long single names fit by measured word wrapping");
    for(const auto& name:std::vector<std::string>{"","name\ndescription","name\rdescription",std::string("name\0hidden",11),std::string(4097,'a')}){
        auto untouched=frame(320,240);const auto pixels=untouched.pixels;
        need(!original_tooltip_layout(320,240,name,14,{100,100})&&!draw_original_tooltip(untouched,name,14,{100,100})&&untouched.pixels==pixels,
            "empty/description/oversized input returns no tooltip without painting");
    }
    need(!original_tooltip_layout(2,2,"name",14,{1,1}),"tiny viewport cannot produce malformed text box");
    need(!original_tooltip_layout(320,20,long_name,14,{100,10}),"unfittable wrapped name is not silently truncated");
    for(float scale:{0.f,-1.f,5.f,std::numeric_limits<float>::quiet_NaN()})
        reject([&]{original_tooltip_layout(320,240,"name",14,{100,100},scale);});
    reject([&]{original_tooltip_layout(0,240,"name",14,{100,100});});
    reject([&]{original_tooltip_layout(8193,240,"name",14,{100,100});});
    reject([&]{original_tooltip_layout(320,240,"name",21,{100,100});});
    reject([&]{original_tooltip_layout(320,240,std::string("\xff",1),14,{100,100});});
    auto malformed=frame(320,240);malformed.pixels.pop_back();
    reject([&]{draw_original_tooltip(malformed,"name",14,{100,100});});
}
}
int main(){try{cases();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
