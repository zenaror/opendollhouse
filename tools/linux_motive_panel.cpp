#include "linux_motive_panel.hpp"

#include "linux_text_renderer.hpp"
#include "buy_mode_view.hpp"
#include "odh/ui_bitmap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace odh_host {
namespace {
using Labels=std::array<std::string_view,8>;
constexpr std::array<Labels,20> kLabels{{
    Labels{"Hunger","Comfort","Hygiene","Bladder","Energy","Fun","Social","Room"},
    Labels{"Hunger","Comfort","Hygiene","Bladder","Energy","Fun","Social","Room"},
    Labels{"Faim","Confort","Hygiène","Vessie","Énergie","Plaisir","Social","Pièce"},
    Labels{"Hunger","Komfort","Hygiene","Blase","Energie","Spaß","Sozial","Raum"},
    Labels{"Fame","Comfort","Igiene","Vescica","Energia","Divertimento","Socialità","Stanza"},
    Labels{"Hambre","Comodidad","Higiene","Vejiga","Energía","Diversión","Social","Habitación"},
    Labels{"Honger","Comfort","Hygiëne","Blaas","Energie","Plezier","Sociaal","Kamer"},
    Labels{"Sult","Komfort","Hygiejne","Blære","Energi","Sjov","Social","Rum"},
    Labels{"Hunger","Komfort","Hygien","Blåsa","Energi","Kul","Social","Rum"},
    Labels{"Sult","Komfort","Hygiene","Blære","Energi","Moro","Sosial","Rom"},
    Labels{"Nälkä","Mukavuus","Hygienia","Rakko","Energia","Hauskuus","Sosiaalisuus","Huone"},
    Labels{"רעב","נוחות","היגיינה","שלפוחית","אנרגיה","כיף","חברה","חדר"},
    Labels{"Голод","Комфорт","Гигиена","Мочевой пузырь","Энергия","Досуг","Общение","Комната"},
    Labels{"Fome","Conforto","Higiene","Banheiro","Energia","Diversão","Social","Cômodos"},
    Labels{"空腹","快適さ","衛生","膀胱","エネルギー","楽しさ","社交","部屋"},
    Labels{"Głód","Komfort","Higiena","Pęcherz","Energia","Zabawa","Towarzystwo","Pokój"},
    Labels{"饥饿","舒适","卫生","膀胱","精力","娱乐","社交","房间"},
    Labels{"飢餓","舒適","衛生","膀胱","精力","娛樂","社交","房間"},
    Labels{"ความหิว","ความสบาย","สุขอนามัย","ปัสสาวะ","พลังงาน","ความสนุก","สังคม","ห้อง"},
    Labels{"배고픔","편안함","위생","용변","에너지","재미","사교","방"}
}};

// Complete Collection's UIText contains individual needs but not the full
// set of panel headings. Keep the short product label in the selected locale.
constexpr std::array<std::string_view,20> headings{{
    "Needs","Needs","Besoins","Bedürfnisse","Bisogni","Necesidades","Behoeften","Behov",
    "Behov","Behov","Tarpeet","צרכים","Потребности","Necessidades","欲求","Potrzeby",
    "需求","需求","ความต้องการ","욕구"
}};
using Color=std::array<std::uint8_t,4>;
void fill_rect(odh::RasterFrame& frame,std::int32_t x,std::int32_t y,
               std::int32_t width,std::int32_t height,Color color) {
    const auto left=std::clamp<std::int64_t>(x,0,frame.width);
    const auto top=std::clamp<std::int64_t>(y,0,frame.height);
    const auto right=std::clamp<std::int64_t>(static_cast<std::int64_t>(x)+width,0,frame.width);
    const auto bottom=std::clamp<std::int64_t>(static_cast<std::int64_t>(y)+height,0,frame.height);
    for(auto row=top;row<bottom;++row)for(auto column=left;column<right;++column)
        frame.pixels[static_cast<std::size_t>(row)*frame.width+static_cast<std::size_t>(column)]=color;
}
std::uint8_t mix(std::uint8_t from,std::uint8_t to,double amount) {
    return static_cast<std::uint8_t>(std::clamp(from+(to-from)*amount,0.0,255.0));
}
}

const std::array<std::string_view,8>& motive_labels(std::uint8_t language) {
    if(language<1||language>kLabels.size())throw std::invalid_argument("unsupported motive-panel language");
    return kLabels[language-1];
}

MotivePanelResult draw_motive_panel(odh::RasterFrame& frame,const StatusHud& state,
                                    std::uint8_t language,
                                    const odh::BitmapImage* original_panel_art,
                                    const OriginalControlPanelLayout* original_layout,
                                    const std::array<std::string,8>* localized_labels) {
    (void)motive_labels(language);
    if(frame.width<160||frame.height<120||
       frame.pixels.size()!=static_cast<std::size_t>(frame.width)*frame.height)
        throw std::invalid_argument("motive panel requires a valid frame of at least 160x120");

    constexpr std::int32_t panel_height=96;
    const auto panel_y=static_cast<std::int32_t>(frame.height)-panel_height;
    if(!original_layout)fill_rect(frame,0,panel_y,static_cast<std::int32_t>(frame.width),panel_height,{17,22,29,255});
    if(original_panel_art&&!original_layout){
        const auto art_y=static_cast<std::int32_t>(frame.height)-
            static_cast<std::int32_t>(std::min<std::uint32_t>(100,original_panel_art->height));
        (void)odh::blit_color_keyed_bitmap(frame,*original_panel_art,0,art_y,{255,0,255});
    }

    if(original_layout){
        const auto& box=original_layout->needs_title;
        TextOverlayStyle title;title.x=box.x;title.y=box.y;title.width=box.width;title.height=box.height;
        title.padding=0;title.font_size_px=std::max(8,static_cast<int>(14*original_layout->scale));
        title.foreground={216,222,248,255};title.background={0,0,0,0};
        (void)draw_text_overlay(frame,headings[language-1],language,title);
    }
    const auto label_list=motive_labels(language);
    const auto column_width=(static_cast<std::int32_t>(frame.width)-16)/4;
    const auto row_height=38;
    MotivePanelResult result{};
    result.bar_pixels=0;
    for(std::size_t index=0;index<label_list.size();++index){
        const auto row=static_cast<std::int32_t>(index/4);
        const auto column=static_cast<std::int32_t>(index%4);
        const auto box=original_layout?original_layout->needs[index]:
            BuyUiRect{8+column*column_width,panel_y+4+row*row_height,column_width-4,row_height};
        const auto x=box.x,y=box.y;
        const auto label_height=original_layout?
            std::max(7,static_cast<int>(std::lround(12*original_layout->scale))):17;
        odh_host::TextOverlayStyle style;
        style.x=x;style.y=y;style.width=std::max(1,box.width);style.height=label_height;
        style.font_size_px=original_layout?std::max(7,static_cast<int>(11.0f*original_layout->scale)):13;
        style.padding=original_layout?0:1;
        style.centered=original_layout!=nullptr;
        style.foreground={232,232,255,255};style.background={17,22,29,0};
        const auto text=draw_text_overlay(frame,localized_labels?std::string_view((*localized_labels)[index]):label_list[index],language,style);
        result.labels_drawn++;
        result.missing_glyphs+=text.missing_glyphs;

        const auto bar_x=x;const auto bar_y=y+label_height+(original_layout?1:2);
        const auto bar_width=std::max(3,box.width-(original_layout?0:2));
        const auto bar_height=original_layout?std::max(2,static_cast<int>(std::lround(5*original_layout->scale))):9;
        std::optional<std::int16_t> value;
        if(index<7){if(state.needs)value=(*state.needs)[index];}
        else value=state.room_score;
        const auto normalized=value?std::clamp<int>(*value,-100,100):-100;
        const auto portion=(normalized+100)/200.0;
        const Color background=!value?Color{75,80,86,255}:original_layout?Color{214,0,0,255}:
            Color{mix(214,57,portion),mix(0,97,portion),mix(0,90,portion),255};
        const Color foreground=original_layout?Color{0,203,57,255}:
            Color{mix(57,0,portion),mix(97,213,portion),mix(90,49,portion),255};
        // Original needs are five-pixel meters, not one-pixel lines inside a
        // heavy diagnostic border. The value boundary stays a black divider.
        const auto border=original_layout?0:1;
        if(border)fill_rect(frame,bar_x,bar_y,bar_width,bar_height,{10,12,15,255});
        const auto inside_height=std::max(1,bar_height-2*border),inside_width=bar_width-2*border;
        fill_rect(frame,bar_x+border,bar_y+border,inside_width,inside_height,background);
        const auto active=static_cast<std::int32_t>(inside_width*portion);
        if(active>0){fill_rect(frame,bar_x+border,bar_y+border,active,inside_height,foreground);
            result.bar_pixels+=static_cast<std::size_t>(active)*static_cast<std::size_t>(inside_height);}
        if(original_layout&&value&&active>0&&active<inside_width)
            fill_rect(frame,bar_x+active,bar_y,1,bar_height,{0,0,0,255});
    }
    (void)state.actor;
    return result;
}
} // namespace odh_host
