/* SPDX-License-Identifier: MPL-2.0 */
#include "original_win_button.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using Pixel=std::array<uint8_t,4>;
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main(){try{
 odh::BitmapImage strip;strip.width=260;strip.height=33;strip.bit_count=24;
 for(unsigned y=0;y<33;++y)for(unsigned x=0;x<260;++x)
  strip.pixels.push_back({uint8_t(10+x%65),uint8_t(30+y),uint8_t(60+x/65*40),255});
 for(unsigned state=0;state<4;++state)strip.pixels[4*260+state*65+3]={255,0,255,255};
 const Pixel background{2,3,4,255};
 for(unsigned state=0;state<4;++state)for(int width:{65,100,185}){
  odh::RasterFrame result;result.width=210;result.height=45;result.pixels.assign(210*45,background);
  const auto input=strip.pixels;const odh::UiBitmapRect box{7,5,width,33};
  need(odh_host::draw_original_win_button(result,strip,box,state),"measured native artwork rejected");
  need(input==strip.pixels,"painting mutated immutable artwork");
  for(int y=0;y<33;++y){
   std::vector<Pixel> expected;const auto start=strip.pixels.begin()+y*260+state*65;
   expected.insert(expected.end(),start,start+32);
   expected.insert(expected.end(),width-64,*(start+32));
   expected.insert(expected.end(),start+33,start+65);
   for(int x=0;x<width;++x){auto p=expected[x];if(p==Pixel{255,0,255,255})p=background;
    need(result.pixels[(box.y+y)*result.width+box.x+x]==p,"art end was rescaled, centre changed, state crossed or key lost");
    const auto sx=odh_host::original_win_button_source_x(x,width);
    need(strip.pixels[y*260+state*65+sx]==expected[x],"Canvas and Raster consumers disagree on source geometry");
   }
  }
  need(result.pixels.front()==background&&result.pixels.back()==background,"painting escaped control bounds");
  odh::RasterFrame clipped;clipped.width=210;clipped.height=45;clipped.pixels.assign(210*45,background);
  need(odh_host::draw_original_win_button(clipped,strip,{-10,-2,width,33},state),"clipped native artwork rejected");
  for(int y=0;y<31;++y)for(int x=0;x<width-10;++x)
   need(clipped.pixels[y*210+x]==result.pixels[(y+7)*210+x+17],"clipping changed unscaled cap/centre phase");
 }
 odh::RasterFrame rejected;rejected.width=100;rejected.height=40;rejected.pixels.assign(4000,background);auto unchanged=rejected.pixels;
 need(!odh_host::draw_original_win_button(rejected,strip,{0,0,64,33},0),"compact width silently acquired a new policy");
 need(!odh_host::draw_original_win_button(rejected,strip,{0,0,100,32},0),"different height silently acquired a new policy");
 need(!odh_host::draw_original_win_button(rejected,strip,{0,0,100,33},4),"invalid state accepted");
 need(unchanged==rejected.pixels,"unsupported art changed output before caller fallback");
 need(odh_host::original_win_button_key("Shared\\Sys\\WinBtn.BMP")&&!odh_host::original_win_button_key("Other/WinBtn.bmp"),"resource gate aliases unrelated art");
 std::cout<<"PASS immutable four-state WinBtn art, native identity, end-preserving width extension, centre, color key, clipping and shared Canvas/Raster coordinates\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
