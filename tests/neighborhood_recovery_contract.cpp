// SPDX-License-Identifier: MPL-2.0
#include "neighborhood_presentation.hpp"
#include <stdexcept>
#include <iostream>
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){
 using namespace odh_host;
 odh::FamilyEntry a{},b{};a.family.member_guids={42};b.family.member_guids={42};
 FamilyPickerPresentation p;p.choices={&a,&b};p.can_move=[](const auto&){return true;};p.can_archive=[](const auto&){return true;};
 odh::BitmapImage red{25,25,24,true,{},0},green{25,25,24,true,{},0};red.pixels.assign(625,{200,0,0,255});green.pixels.assign(625,{0,200,0,255});
 int reads=0;p.scoped_portraits=[&](const auto& f,uint32_t guid){require(guid==42,"GUID changed");++reads;return &f==&a?&red:&green;};
 auto f=p.draw(nullptr,1,"Select a family","Move In","Delete","Cancel","No family","Create a family",{});
 require(reads==2,"one lookup per member");require(f.pixels[106*800+170][0]==200,"first source portrait lost");require(f.pixels[174*800+170][1]==200,"second source portrait reused first GUID");
 require(f.pixels[98*800+156][0]!=200,"export thumbnail stretched into a 35x41 face");
 PreparedNeighborhoodMap map;map.mode=7;map.frame.width=800;map.frame.height=600;
 map.frame.pixels.assign(800*600,{10,10,10,255});map.background=map.frame.pixels;
 map.markers={{{68,320,300},true,false}};
 odh::BitmapImage closed{1,1,24,true,{{200,0,0,255}},0},open{1,1,24,true,{{0,200,0,255}},0};
 map.houses.push_back(prepare_house_presentation(68,{68,320,300},{300,280,40,40},{300,280,40,40},closed,open));
 odh::BitmapImage foreground{800,600,24,true,{},0};foreground.pixels.assign(800*600,{255,0,255,255});foreground.pixels[300*800+315]={0,0,200,255};map.foreground.push_back(foreground);
 NeighborhoodMapPresentation view;view.pointer=std::array<int32_t,2>{320,300};view.can_manage=true;
 const auto render=[&]{return view.draw(map,nullptr,"Cancel",1,1,{}, {}, {});};
 view.advance(map,0);auto before=render();require(before.pixels[300*800+320][0]==200,"closed roof missing before elapsed time");
 view.advance(map,250);auto middle=render();require(middle.pixels[300*800+320][0]>50&&middle.pixels[300*800+320][1]>50,"roof changed abruptly instead of fading");
 require(middle.pixels[300*800+315][2]==200,"foreground lost above fading roof");
 view.advance(map,250);auto after=render();require(after.pixels[300*800+320][1]==200,"open roof missing at fade completion");
 view.pointer.reset();view.advance(map,250);auto returning=render();require(returning.pixels[300*800+320][0]>50&&returning.pixels[300*800+320][1]>50,"mouse-out did not fade roof back");
 view.advance(map,250);require(render().pixels[300*800+320][0]==200,"closed roof not restored after mouse-out");
 require(view.target_at(map,350,25)==1&&view.target_at(map,400,25)==10,"destination header controls use wrong targets");
 require(!view.target_at(map,450,25),"lot import routed to family import");
 require(view.target_at(map,374,25)==1&&view.target_at(map,375,25)==10,"adjacent destination hitboxes overlap");
 map.mode=2;require(!view.target_at(map,225,25),"Downtown gained a residential family control");
 NeighborhoodMapAnimation animation;animation.frames={closed,open,odh::BitmapImage{1,1,24,true,{{0,0,200,255}},0}};animation.x=50;animation.y=100;
 map.animations={animation};view.time_ms=125;auto wave=render();require(wave.pixels[100*800+50][0]>50&&wave.pixels[100*800+50][1]>50,"authored map animation did not interpolate");
 view.time_ms=750;require(render().pixels[100*800+50][1]==200,"pulsating animation did not reverse at the endpoint");
 map.animations[0].pulsates=false;require(render().pixels[100*800+50][0]==200,"looping animation did not wrap");
 std::cout<<"portrait identity, native thumbnail, roof timing, foreground, map-animation and destination-header contracts passed\n";
}
