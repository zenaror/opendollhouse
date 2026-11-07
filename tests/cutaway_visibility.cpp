#include "odh/cutaway_visibility.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
odh::FullCutawayVisibilityInput scene(){odh::FullCutawayVisibilityInput i;i.hide_for_cutaway=true;i.walls_down=true;
    i.width=8;i.height=8;i.x=3;i.y=3;i.wall_segments=15;return i;}
}
int main(){try{
    for(unsigned direction=0;direction<8;++direction){const std::uint8_t required[]={2,2,4,2,8,2,1,2};
        require(odh::source_adjacent_wall(1,static_cast<std::uint8_t>(direction))==required[direction],
            "source wall rotation covers cardinals and diagonal north fallback");}
    require(odh::source_adjacent_wall(15,0)==0&&odh::source_adjacent_wall(31,0)==2&&odh::source_adjacent_wall(-1,0)==15,
        "source wall-use modulo15 is preserved including signed remainder, not replaced with mask15");
    for(const auto pair:{std::pair<float,int>{0.5f,0},{1.5f,2},{2.5f,2},{-0.5f,0},{-1.5f,-2},{-2.5f,-2}}){
        const auto p=odh::source_cutaway_tile(pair.first,0);require(p&&(*p)[0]==pair.second,"graphics coordinates use source midpoint-to-even tile rounding");}
    auto s=scene();require(odh::full_cutaway_hidden(s)==std::optional<bool>{true},"full walls-down map proves every interior object hidden despite unknown wall-use state");
    for(unsigned a=0;a<16;++a)for(unsigned w=0;w<16;++w){s.adjacent_wall=static_cast<std::uint8_t>(a);s.occupied_walls=static_cast<std::uint8_t>(w);
        require(odh::full_cutaway_hidden(s)==std::optional<bool>{true},"all authored masks share full interior visibility rule");}
    struct Boundary{float x,y;std::uint8_t outward;};
    for(const auto edge:{Boundary{0,3,1},Boundary{3,0,2},Boundary{7,3,4},Boundary{3,7,8}}){
        s=scene();s.x=edge.x;s.y=edge.y;s.adjacent_wall=edge.outward;s.occupied_walls=0;
        require(odh::full_cutaway_hidden(s)==std::optional<bool>{false},"adjacent single wall crossing lot boundary stays visible");
        s.adjacent_wall=0;s.occupied_walls=edge.outward;
        require(odh::full_cutaway_hidden(s)==std::optional<bool>{false},"occupied wall is source fallback when adjacent mask is not a dictionary key");
        s.adjacent_wall=15;s.occupied_walls=15;
        require(odh::full_cutaway_hidden(s)==std::optional<bool>{true},"combined wall masks are not single dictionary keys and retain zero-offset fallback");
        s.adjacent_wall.reset();s.occupied_walls.reset();require(!odh::full_cutaway_hidden(s),"unknown outward boundary wall-use is unresolved rather than zero or guessed visibility");
        s.wall_segments=static_cast<std::uint8_t>(15U&~edge.outward);
        require(odh::full_cutaway_hidden(s)==std::optional<bool>{true},"boundary geometry without outward segment still proves full cutaway result");
    }
    s=scene();s.x=0;s.wall_segments=15;s.adjacent_wall=2;s.occupied_walls=1;
    require(odh::full_cutaway_hidden(s)==std::optional<bool>{true},"known adjacent single wall takes precedence over occupied outward fallback");
    s=scene();s.previous_hidden=true;s.x=-1;
    require(odh::full_cutaway_hidden(s)==std::optional<bool>{true},"out-of-lot source update retains prior hidden state");
    s.hide_for_cutaway=false;require(odh::full_cutaway_hidden(s)==std::optional<bool>{true},"flagoff source update also leaves existing property untouched");
    s.hide_for_cutaway=true;s.object_level=0;require(odh::full_cutaway_hidden(s)==std::optional<bool>{true},"source levelzero skips update without invented reset");
    s.object_level=2;require(odh::full_cutaway_hidden(s)==std::optional<bool>{false},"different floor explicitly resets source hidden state");
    s.object_level=1;s.build_mode=2;require(odh::full_cutaway_hidden(s)==std::optional<bool>{false},"construction mode overrides cutaway");
    s=scene();s.walls_down=false;require(odh::full_cutaway_hidden(s)==std::optional<bool>{false},"full walls-up map resets matching-level component");
    s=scene();s.x=std::numeric_limits<float>::quiet_NaN();require(!odh::full_cutaway_hidden(s),"invalid graphics coordinate cannot invent tilezero");
    std::cout<<"full cutaway source visibility matrix passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
