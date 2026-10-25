#include "linux_household_selector.hpp"

#include "linux_text_renderer.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace odh_host {
namespace {
using Color=std::array<std::uint8_t,4>;
constexpr std::array<std::string_view,20> kButtonLabels{
    "Sims","Sims","Sims","Sims","Sims","Sims","Sims","Sims","Sims","Sims",
    "Simit","Sims","Симы","Sims","シム","Simowie","模拟市民","模擬市民","ซิม","심"};
constexpr std::array<std::string_view,20> kPanelLabels{
    "Choose a Sim","Choose a Sim","Choisir un Sim","Sim auswählen","Scegli un Sim",
    "Elige un Sim","Kies een Sim","Vælg en Sim","Välj en Sim","Velg en Sim",
    "Valitse sim","בחרו דמות","Выберите сима","Escolha um Sim","シムを選択",
    "Wybierz Sima","选择模拟市民","選擇模擬市民","เลือกซิม","심 선택"};

void fill(odh::RasterFrame& frame,UiRect rect,Color color) {
    const auto left=std::clamp<std::int64_t>(rect.x,0,frame.width);
    const auto top=std::clamp<std::int64_t>(rect.y,0,frame.height);
    const auto right=std::clamp<std::int64_t>(static_cast<std::int64_t>(rect.x)+rect.width,0,frame.width);
    const auto bottom=std::clamp<std::int64_t>(static_cast<std::int64_t>(rect.y)+rect.height,0,frame.height);
    for(auto y=top;y<bottom;++y)for(auto x=left;x<right;++x)
        frame.pixels[static_cast<std::size_t>(y)*frame.width+static_cast<std::size_t>(x)]=color;
}

UiRect card_rect(std::size_t index,std::size_t count,std::uint32_t width) {
    constexpr std::int32_t columns=4,card_height=21,gap=3;
    const auto column=static_cast<std::int32_t>(index%columns);
    const auto row=static_cast<std::int32_t>(index/columns);
    const auto inner_width=static_cast<std::int32_t>(width)-20;
    const auto card_width=std::max(1,(inner_width-(columns-1)*gap)/columns);
    const auto row_count=static_cast<std::int32_t>((count+columns-1)/columns);
    const auto used_rows=std::max(1,row_count);
    const auto panel_height=25+used_rows*(card_height+gap)+4;
    const auto x=10+column*(card_width+gap);
    return {x,6+panel_height-4-used_rows*(card_height+gap)+row*(card_height+gap),card_width,card_height};
}
}

UiRect household_selector_button_rect(std::uint32_t width,std::uint32_t height) {
    const auto button_width=static_cast<std::int32_t>(std::min<std::uint32_t>(52,width>16?width-16:0));
    return {8,static_cast<std::int32_t>(height)-20,button_width,16};
}

UiRect household_selector_panel_rect(std::uint32_t width,std::uint32_t height) {
    const auto bounded_width=static_cast<std::int32_t>(std::min<std::uint32_t>(width,2048));
    (void)height;
    return {6,6,std::max(0,bounded_width-12),88};
}

bool household_selector_button_hit(std::uint32_t width,std::uint32_t height,
                                   std::int32_t x,std::int32_t y) {
    const auto rect=household_selector_button_rect(width,height);
    return x>=rect.x&&y>=rect.y&&x<rect.x+rect.width&&y<rect.y+rect.height;
}

std::optional<std::int16_t> household_selector_pick(const std::vector<HouseholdSelectorResident>& residents,
    std::uint32_t width,std::uint32_t height,std::int32_t x,std::int32_t y) {
    if(residents.size()>8||width<160||height<120)return std::nullopt;
    for(std::size_t i=0;i<residents.size();++i){
        const auto rect=card_rect(i,residents.size(),width);
        if(x>=rect.x&&y>=rect.y&&x<rect.x+rect.width&&y<rect.y+rect.height)return residents[i].runtime_id;
    }
    return std::nullopt;
}

void draw_household_selector_button(odh::RasterFrame& frame,std::uint8_t language) {
    if(language<1||language>kButtonLabels.size()||frame.width<160||frame.height<120||
       frame.pixels.size()!=static_cast<std::size_t>(frame.width)*frame.height)return;
    const auto rect=household_selector_button_rect(frame.width,frame.height);
    fill(frame,rect,{47,75,92,255});
    TextOverlayStyle style;style.x=rect.x;style.y=rect.y;style.width=rect.width;
    style.height=rect.height;style.font_size_px=11;style.padding=1;
    style.foreground={250,248,230,255};style.background={47,75,92,0};
    (void)draw_text_overlay(frame,kButtonLabels[language-1],language,style);
}

void draw_household_selector(odh::RasterFrame& frame,const std::vector<HouseholdSelectorResident>& residents,
    std::int16_t selected,std::uint8_t language) {
    if(language<1||language>kPanelLabels.size()||residents.empty()||residents.size()>8||
       frame.width<160||frame.height<120||
       frame.pixels.size()!=static_cast<std::size_t>(frame.width)*frame.height)return;
    const auto panel=household_selector_panel_rect(frame.width,frame.height);
    fill(frame,panel,{16,22,29,255});
    TextOverlayStyle heading;heading.x=12;heading.y=8;heading.width=panel.width-12;
    heading.height=18;heading.font_size_px=12;heading.padding=1;
    heading.foreground={250,248,230,255};heading.background={16,22,29,0};
    (void)draw_text_overlay(frame,kPanelLabels[language-1],language,heading);
    for(std::size_t i=0;i<residents.size();++i){
        const auto rect=card_rect(i,residents.size(),frame.width);
        const bool active=residents[i].runtime_id==selected;
        fill(frame,rect,active?Color{54,108,133,255}:Color{45,52,58,255});
        TextOverlayStyle style;style.x=rect.x;style.y=rect.y;style.width=rect.width;
        style.height=rect.height;style.font_size_px=11;style.padding=1;
        style.foreground={250,248,230,255};style.background=active?Color{54,108,133,0}:Color{45,52,58,0};
        (void)draw_text_overlay(frame,residents[i].label,language,style);
    }
}

} // namespace odh_host
