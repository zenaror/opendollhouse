#include "linux_motive_panel.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}

int main(){try{
    check(odh_host::motive_labels(1)[0]=="Hunger"&&
          odh_host::motive_labels(14)[0]=="Fome"&&
          odh_host::motive_labels(14)[7]=="Cômodo",
          "English and Portuguese labels match their locale");
    for(std::uint8_t language=1;language<=20;++language)
        for(const auto label:odh_host::motive_labels(language))
            check(!label.empty(),"every supported language has all eight motive labels");
    bool invalid_language=false;
    try{(void)odh_host::motive_labels(21);}catch(const std::invalid_argument&){invalid_language=true;}
    check(invalid_language,"unsupported motive language is rejected");

    odh::RasterFrame frame{320,240,
        std::vector<std::array<std::uint8_t,4>>(320U*240U,{1,2,3,255}),
        std::vector<float>(320U*240U,7),0,0,
        std::vector<std::uint16_t>(320U*240U,105)};
    const auto depth=frame.depth;const auto owners=frame.owner_ids;
    odh_host::StatusHud state;state.actor=105;
    state.needs=std::array<std::int16_t,7>{-100,-50,0,50,100,0,100};
    state.room_score=-100;
    odh::BitmapImage panel{8,100,24,false,
        std::vector<std::array<std::uint8_t,4>>(800,{20,80,120,255}),0};
    panel.pixels[0]={255,0,255,255};
    const auto result=odh_host::draw_motive_panel(frame,state,14,&panel);
    check(result.labels_drawn==8,"panel draws each motive label");
    check(frame.depth==depth&&frame.owner_ids==owners,
          "panel leaves scene depth and picking ownership unchanged");
    check(frame.pixels!=std::vector<std::array<std::uint8_t,4>>(320U*240U,{1,2,3,255}),
          "panel adds visible labels and bars to the scene");
    check(frame.pixels[239U*320U+1]==std::array<std::uint8_t,4>{20,80,120,255},
          "original panel artwork appears and its color key stays transparent");
    check(result.bar_pixels>0,"available motive values produce filled bars");
    bool invalid_frame=false;
    try{odh::RasterFrame tiny{159,240,{}, {},0,0,{}};(void)odh_host::draw_motive_panel(tiny,state,14);}
    catch(const std::invalid_argument&){invalid_frame=true;}
    check(invalid_frame,"undersized frame is rejected safely");
    std::cout<<"linux motive panel: checks passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"linux motive panel: "<<error.what()<<'\n';return 1;}}
