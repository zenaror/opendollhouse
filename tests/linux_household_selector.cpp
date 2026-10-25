#include "linux_household_selector.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}

int main(){try{
    constexpr std::uint32_t width=800,height=600;
    const std::vector<std::int16_t> residents{48,52,67,71,89};
    const auto button=odh_host::household_selector_button_rect(width,height);
    check(odh_host::household_selector_button_hit(width,height,button.x+1,button.y+1),
          "visible Sim button accepts a click");
    check(!odh_host::household_selector_button_hit(width,height,button.x+button.width,button.y+1),
          "button hit area ends at its right edge");

    const auto first=odh_host::household_selector_pick(residents,width,height,20,35);
    const auto fifth=odh_host::household_selector_pick(residents,width,height,20,59);
    check(first&&*first==48,"first household card selects its resident");
    check(fifth&&*fifth==89,"second row card selects its resident");
    const std::vector<std::int16_t> two_residents{105,137};
    const auto second_of_two=odh_host::household_selector_pick(two_residents,width,height,250,40);
    check(second_of_two&&*second_of_two==137,"second card in a two-resident household is selectable");
    check(!odh_host::household_selector_pick(residents,width,height,790,90),
          "click outside the cards does not select a resident");
    const std::vector<std::int16_t> too_many(9);
    check(!odh_host::household_selector_pick(too_many,width,height,20,35),
          "selector refuses a household larger than its visible card layout");

    odh::RasterFrame frame{width,height,
        std::vector<std::array<std::uint8_t,4>>(width*height,{1,2,3,255}),{},0,0,{}};
    odh_host::draw_household_selector_button(frame,14);
    odh_host::draw_household_selector(frame,residents,67,14);
    check(frame.pixels[static_cast<std::size_t>(button.y)*width+button.x]!=
          std::array<std::uint8_t,4>{1,2,3,255},"selector button draws into the panel");
    const auto panel=odh_host::household_selector_panel_rect(width,height);
    check(frame.pixels[static_cast<std::size_t>(panel.y)*width+panel.x]!=
          std::array<std::uint8_t,4>{1,2,3,255},"resident chooser draws its overlay");
    for(const auto& pixel:frame.pixels)
        check(pixel[3]==255,"window overlays preserve the opaque pixel rule");
    std::cout<<"linux household selector: checks passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"linux household selector: "<<error.what()<<'\n';return 1;}}
