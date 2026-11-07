#include "linux_motive_panel.hpp"
#include "buy_mode_view.hpp"

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
          odh_host::motive_labels(14)[3]=="Banheiro"&&
          odh_host::motive_labels(14)[7]=="Cômodos",
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
    for(const auto& pixel:frame.pixels)
        check(pixel[3]==255,"motive panel preserves the opaque window pixel rule");
    check(result.bar_pixels>0,"available motive values produce filled bars");
    {
        odh::RasterFrame original;original.width=800;original.height=600;
        original.pixels.assign(800*600,{1,2,3,255});original.depth.assign(800*600,7);
        original.owner_ids.assign(800*600,105);
        const auto layout=odh_host::original_control_panel_layout(800,600);
        const auto painted=odh_host::draw_motive_panel(original,state,14,nullptr,&layout);
        check(painted.labels_drawn==8,"shared original panel paints both four-row need columns");
        const auto energy=layout.needs[4];
        for(int y=energy.y+13;y<energy.y+18;++y)
            check(original.pixels[std::size_t(y)*800+energy.x+energy.width/2]==
                std::array<std::uint8_t,4>{0,203,57,255},
                "known full need fills the original five-pixel meter instead of a one-pixel debug line");
        check(original.depth==std::vector<float>(800*600,7)&&
            original.owner_ids==std::vector<std::uint16_t>(800*600,105),
            "original need labels and meters preserve scene depth and object selection");
    }
    bool invalid_frame=false;
    try{odh::RasterFrame tiny{159,240,{}, {},0,0,{}};(void)odh_host::draw_motive_panel(tiny,state,14);}
    catch(const std::invalid_argument&){invalid_frame=true;}
    check(invalid_frame,"undersized frame is rejected safely");
    std::cout<<"linux motive panel: checks passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"linux motive panel: "<<error.what()<<'\n';return 1;}}
