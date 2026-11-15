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

namespace {
OriginalMotivePanelGeometry standard_motive_geometry(std::uint32_t width,const OriginalControlPanelLayout& common) {
    OriginalMotivePanelGeometry out;
    const auto scale=common.scale;
    out.panel=common.right_content;
    out.panel.width=std::max(0,int(width)-out.panel.x);
    out.title=common.needs_title;
    out.title_font_size_px=std::max(8,int(14*scale));
    out.label_font_size_px=std::max(7,int(11*scale));
    for(std::size_t i=0;i<out.labels.size();++i) {
        out.labels[i]=common.needs[i];
        out.labels[i].height=std::max(7,int(std::lround(12*scale)));
        out.meters[i]={out.labels[i].x,out.labels[i].y+out.labels[i].height+1,
            std::max(3,out.labels[i].width),std::max(2,int(std::lround(5*scale)))};
    }
    return out;
}
}
OriginalMotivePanelGeometry original_motive_panel_geometry(
        std::uint32_t width,std::uint32_t height,const OriginalControlPanelLayout& common,
        std::uint8_t language,const odh::BitmapImage* tall_art) {
    (void)motive_labels(language);
    auto out=standard_motive_geometry(width,common);
    const auto scale=common.scale;
    if(language!=15)return out;
    if(!tall_art||tall_art->width!=600||tall_art->height!=150||
       tall_art->pixels.size()!=std::size_t(tall_art->width)*tall_art->height)
        throw std::invalid_argument("Japanese original Needs requires TallSubPanel600x150 artwork");
    // Res_CPanel.RT kTallSubpanel supplies the actual150px canvas. The
    // DVD6e1:1 reference supplies local label/heading/meter geometry: title
    //16px ink, labels12px ink, first meter59px below that canvas top,24px
    // pitch. These are authenticated presentation measurements, NOT a claim
    // about the original font handler or FONT12J bytes per glyph.
    const auto rect=[&](float x,float y,float w,float h) {
        return odh::UiBitmapRect{int(std::lround(x*scale)),
            int(std::lround(float(height)-(float(tall_art->height)-y)*scale)),
            int(std::lround(w*scale)),int(std::lround(h*scale))};
    };
    out.tall=true;
    out.artwork=rect(520,0,float(tall_art->width),float(tall_art->height));
    out.panel=out.artwork;out.panel.width=std::max(0,int(width)-out.panel.x);
    out.title=rect(528,0,164,24);
    out.title_font_size_px=std::max(1,int(16*scale));
    out.label_font_size_px=std::max(1,int(12*scale));
    for(std::size_t i=0;i<out.labels.size();++i) {
        const float x=540+100*float(i/4),row=24*float(i%4);
        out.labels[i]=rect(x,40+row,60,18);
        out.labels[i].height=std::max(7,out.labels[i].height);
        out.meters[i]=rect(x,59+row,60,5);
        out.meters[i].width=std::max(3,out.meters[i].width);
        out.meters[i].height=std::max(2,out.meters[i].height);
    }
    return out;
}

MotivePanelResult draw_motive_panel(odh::RasterFrame& frame,const StatusHud& state,
        std::uint8_t language,const odh::BitmapImage* original_panel_art,
        const OriginalControlPanelLayout* original_layout,
        const std::array<std::string,8>* localized_labels,const std::string* localized_heading) {
    return draw_motive_panel(frame,state,language,original_panel_art,original_layout,
        localized_labels,localized_heading,nullptr);
}

MotivePanelResult draw_motive_panel(odh::RasterFrame& frame,const StatusHud& state,
                                    std::uint8_t language,
                                    const odh::BitmapImage* original_panel_art,
                                    const OriginalControlPanelLayout* original_layout,
                                    const std::array<std::string,8>* localized_labels,
                                    const std::string* localized_heading,
                                    const OriginalMotivePanelGeometry* prepared_geometry) {
    return draw_motive_panel(frame,state,language,original_panel_art,original_layout,
        localized_labels,localized_heading,prepared_geometry,nullptr);
}
MotivePanelResult draw_motive_panel(odh::RasterFrame& frame,const StatusHud& state,
                                    std::uint8_t language,
                                    const odh::BitmapImage* original_panel_art,
                                    const OriginalControlPanelLayout* original_layout,
                                    const std::array<std::string,8>* localized_labels,
                                    const std::string* localized_heading,
                                    const OriginalMotivePanelGeometry* prepared_geometry,
                                    const std::array<int,8>* change_arrows) {
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

    const auto geometry=original_layout?std::optional<OriginalMotivePanelGeometry>{
        prepared_geometry?*prepared_geometry:standard_motive_geometry(frame.width,*original_layout)}:
        std::nullopt;
    if(geometry&&geometry->tall&&(!original_panel_art||
       original_panel_art->pixels.size()!=std::size_t(original_panel_art->width)*original_panel_art->height))
        throw std::invalid_argument("prepared tall Needs geometry requires its original artwork");
    if(geometry&&geometry->tall) {
        // Natural asset pixels and its alpha/corner are preserved. Only the
        // final flat column repeats when a wider desktop outgrows the art.
        odh::blit_rgba_bitmap(frame,*original_panel_art,geometry->artwork);
        const int right=geometry->artwork.x+geometry->artwork.width;
        if(right<int(frame.width))odh::blit_rgba_bitmap(frame,*original_panel_art,
            {right,geometry->artwork.y,int(frame.width)-right,geometry->artwork.height},
            odh::UiBitmapRect{int(original_panel_art->width)-1,0,1,int(original_panel_art->height)});
    }
    if(original_layout){
        const auto& box=geometry->title;
        TextOverlayStyle title;title.x=box.x;title.y=box.y;title.width=box.width;title.height=box.height;
        title.padding=0;title.font_size_px=geometry->title_font_size_px;
        title.foreground={216,222,248,255};title.background={0,0,0,0};
        (void)draw_text_overlay(frame,localized_heading?std::string_view(*localized_heading):headings[language-1],language,title);
    }
    const auto label_list=motive_labels(language);
    const auto column_width=(static_cast<std::int32_t>(frame.width)-16)/4;
    const auto row_height=38;
    MotivePanelResult result{};
    result.bar_pixels=0;
    for(std::size_t index=0;index<label_list.size();++index){
        const auto row=static_cast<std::int32_t>(index/4);
        const auto column=static_cast<std::int32_t>(index%4);
        const auto box=geometry?geometry->labels[index]:
            BuyUiRect{8+column*column_width,panel_y+4+row*row_height,column_width-4,row_height};
        const auto x=box.x,y=box.y;
        const auto label_height=geometry?box.height:17;
        odh_host::TextOverlayStyle style;
        style.x=x;style.y=y;style.width=std::max(1,box.width);style.height=label_height;
        style.font_size_px=geometry?geometry->label_font_size_px:13;
        style.padding=original_layout?0:1;
        style.centered=original_layout!=nullptr;
        style.foreground={232,232,255,255};style.background={17,22,29,0};
        const auto text=draw_text_overlay(frame,localized_labels?std::string_view((*localized_labels)[index]):label_list[index],language,style);
        result.labels_drawn++;
        result.missing_glyphs+=text.missing_glyphs;

        const auto meter=geometry?geometry->meters[index]:
            BuyUiRect{x,y+label_height+2,std::max(3,box.width-2),9};
        const auto bar_x=meter.x,bar_y=meter.y,bar_width=meter.width,bar_height=meter.height;
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
        if(original_layout&&value&&change_arrows){
            // Complete's observed left chevron is 3x5 pixels, rows1/2/3/2/1,
            // 4px pitch and a1px gap to the meter. The existing history owns
            // count/fade; this painter only presents its signed visual state.
            const int change=std::clamp((*change_arrows)[index],-300,300);
            const double scale=bar_height/5.0;
            const Color color=change>0?Color{0,203,57,255}:Color{214,0,0,255};
            for(int arrow=0;arrow<(std::abs(change)+59)/60;++arrow){
                const int alpha=std::min(60,std::abs(change)-arrow*60)*255/60;
                const int anchor=change>0?bar_x+bar_width+int(std::lround((1+arrow*4)*scale)):
                    bar_x-int(std::lround((4+arrow*4)*scale));
                for(int row=0;row<5;++row)for(int col=0;col<3;++col){
                    if(change>0?col>2-std::abs(row-2):col<std::abs(row-2))continue;
                    const int x=anchor+int(std::lround(col*scale)),y=bar_y+int(std::lround(row*scale));
                    const int right=anchor+int(std::lround((col+1)*scale)),bottom=bar_y+int(std::lround((row+1)*scale));
                    for(int yy=std::max(0,y);yy<std::min(int(frame.height),bottom);++yy)
                        for(int xx=std::max(0,x);xx<std::min(int(frame.width),right);++xx){
                            auto& pixel=frame.pixels[std::size_t(yy)*frame.width+xx];
                            for(unsigned channel=0;channel<3;++channel)
                                pixel[channel]=std::uint8_t((int(pixel[channel])*(255-alpha)+int(color[channel])*alpha+127)/255);
                            ++result.arrow_pixels;
                        }
                }
            }
        }
    }
    (void)state.actor;
    return result;
}
} // namespace odh_host
