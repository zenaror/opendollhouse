#include "odh/neighborhood_picker.hpp"
#include <iostream>
#include <limits>
namespace {
int checks=0;
void require(bool value) { ++checks;if(!value) throw std::runtime_error("picker check "+std::to_string(checks)); }
template<class F> void rejects(F fn) { bool rejected=false;try{fn();}catch(const odh::FormatError&){rejected=true;}require(rejected); }
}
int main() {
    try {
        const std::vector<std::string> resources={"Nbhd\\NScreen.BMP","Downtown\\DScreen.bmp",
            "VIsland\\visland.bmp","Community\\NScreen_unleashed.bmp","Studiotown\\DScreen.bmp","","Magicland\\DScreen.bmp"};
        for(unsigned mode=1;mode<=7;++mode) {
            const auto config=odh::neighborhood_map_config(mode);
            require(bool(config)==(mode!=6));if(!config) continue;
            require(config->resource==resources[mode-1]);require(config->marker_scale==(mode==4?2u:1u));
            const int r=static_cast<int>(config->marker_scale*6);
            const std::vector<odh::NeighborhoodMarker> markers={{{7,100,100},true,false},{{8,300,300},true,true},{{9,500,400},false,true}};
            for(const auto& offset:std::vector<std::array<int,2>>{{0,0},{-r,-r},{r,r}})
                require(odh::pick_neighborhood_lot(markers,config->marker_scale,100+offset[0],100+offset[1])==7);
            require(!odh::pick_neighborhood_lot(markers,config->marker_scale,100+r+1,100));
            require(odh::pick_neighborhood_lot(markers,config->marker_scale,300,300)==8);
            require(!odh::pick_neighborhood_lot(markers,config->marker_scale,500,400));
            odh::BitmapImage image{};image.width=800;image.height=600;image.pixels.resize(800*600,{1,2,3,255});
            const auto frame=odh::draw_neighborhood_map(image,markers,config->marker_scale);
            require(frame.pixels[0]==image.pixels[0]);
            require(frame.depth.size()==frame.pixels.size() && frame.depth[0]==std::numeric_limits<float>::infinity());
            require(frame.pixels[100*800+100]==std::array<std::uint8_t,4>{40,220,120,255});
            require(frame.pixels[300*800+300]==std::array<std::uint8_t,4>{245,185,30,255});
            require(frame.pixels[400*800+500]==std::array<std::uint8_t,4>{120,120,120,255});
            require(frame.pixels[100*800+100+r]==std::array<std::uint8_t,4>{0,0,0,255});
            require(image.pixels[100*800+100]==std::array<std::uint8_t,4>{1,2,3,255});
            std::cout<<"mode "<<mode<<": mapping, edges, occupancy and frame passed\n";
        }
        const std::vector<odh::NeighborhoodMarker> overlap={{{7,0,0},true,false},{{8,0,0},true,true}};
        require(odh::pick_neighborhood_lot(overlap,1,0,0)==8);
        auto missing=overlap;missing.back().file_exists=false;
        require(!odh::pick_neighborhood_lot(missing,1,0,0));
        for(const auto& p:std::vector<std::array<int,2>>{{-1,0},{0,-1},{800,0},{0,600},{std::numeric_limits<int>::max(),0}})
            require(!odh::pick_neighborhood_lot(overlap,1,p[0],p[1]));
        rejects([]{odh::neighborhood_map_config(0);});rejects([]{odh::neighborhood_map_config(8);});
        rejects([&]{odh::pick_neighborhood_lot(overlap,0,0,0);});
        rejects([&]{odh::draw_neighborhood_map({},overlap,1);});
        odh::BitmapImage edge{};edge.width=800;edge.height=600;edge.pixels.resize(800*600);
        const auto frame=odh::draw_neighborhood_map(edge,{{{1,0,0},true,false},{{2,799,599},true,true},
            {{3,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()},true,false}},2);
        require(frame.pixels[0]==std::array<std::uint8_t,4>{40,220,120,255});
        require(frame.pixels.back()==std::array<std::uint8_t,4>{245,185,30,255});
        std::cout<<checks<<" synthetic checks passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
