#include "status_hud.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
int main(){try{
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    odh::RasterFrame frame{320,240,std::vector<std::array<std::uint8_t,4>>(320U*240U,{1,2,3,255}),
        std::vector<float>(320U*240U,7),0,0,std::vector<std::uint16_t>(320U*240U,105)};
    const auto original=frame;
    odh_host::StatusHud hud;hud.actor=137;hud.clock=odh::Ts1ClockSnapshot{};
    hud.clock->hour=23;hud.clock->minute=59;
    hud.needs=std::array<std::int16_t,7>{-100,0,100,-50,50,-32768,32767};
    odh_host::draw_status_hud(frame,hud);
    check(frame.depth==original.depth&&frame.owner_ids==original.owner_ids,"HUD changed depth or owner IDs");
    check(frame.tested_samples==0&&frame.shaded_samples==0,"HUD changed raster counters");
    check(frame.pixels[204*320+4]==original.pixels[204*320+4],"HUD overlaps action strip");
    check(frame.pixels[28*320+50]==std::array<std::uint8_t,4>{57,66,78,255},"-100 bar must be empty");
    check(frame.pixels[48*320+149]==std::array<std::uint8_t,4>{81,190,128,255},"100 bar must be full");
    check(frame.pixels[88*320+149]==std::array<std::uint8_t,4>{81,190,128,255},"bar clamp failed");
    auto missing=original;odh_host::draw_status_hud(missing,{});
    check(missing.pixels!=original.pixels,"missing-state HUD not drawn");
    check(frame.pixels[0]==original.pixels[0],"HUD escaped bounds");
    bool rejected=false;try{odh::RasterFrame bad{};odh_host::draw_status_hud(bad,hud);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"malformed frame accepted");
    std::cout<<"status HUD: 9 checks passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
