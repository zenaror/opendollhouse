#include "linux_motive_panel.hpp"
#include "buy_mode_view.hpp"
#include "linux_text_renderer.hpp"

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
    {
        // Text supplied by the game STR service must override legacy fallback
        // without changing motives, depth or picking ownership.
        const std::array<std::string,8> labels{{"空腹","心地よさ","衛生","便意","体力","楽しさ","社交","部屋"}};
        const std::string heading="気分";
        auto localized=frame;const auto before=state.needs;
        const auto layout=odh_host::original_control_panel_layout(320,240);
        odh::BitmapImage tall{600,150,32,false,
            std::vector<std::array<std::uint8_t,4>>(600U*150U,{7,11,55,255}),0};
        const auto geometry=odh_host::original_motive_panel_geometry(320,240,layout,15,&tall);
        const auto painted=odh_host::draw_motive_panel(localized,state,15,&tall,&layout,&labels,&heading,&geometry);
        check(painted.labels_drawn==8&&state.needs==before&&localized.depth==depth&&localized.owner_ids==owners,
            "authored UTF8 needs retain selected motive values and world picking");
    }
    {
        // External oracle: raw DVD6e JA1024x768 meter runs677..681,
        //701..705,725..729,749..753 and authored TallSubPanel600x150.
        // Validate paint/geometry/occlusion together, not a copy of internal
        // spacing constants. Other viewports retain those bottom distances.
        odh::BitmapImage tall{600,150,32,false,
            std::vector<std::array<std::uint8_t,4>>(600U*150U,{7,11,55,255}),0};
        tall.pixels[0]={90,80,70,0}; // Authored rounded corner stays transparent.
        for(unsigned y=0;y<tall.height;++y)tall.pixels[y*tall.width+tall.width-1]={3,4,5,255};
        const std::array<std::string,8> labels{{"空腹","心地よさ","衛生","便意","体力","楽しさ","社交","部屋"}};
        odh_host::StatusHud full;full.needs=std::array<std::int16_t,7>{100,100,100,100,100,100,100};full.room_score=100;
        for(const auto viewport:std::array<std::array<unsigned,2>,5>{{{{320,240}},{{640,480}},{{800,600}},{{1024,768}},{{1600,900}}}}) {
            const auto width=viewport[0],height=viewport[1];
            const auto common=odh_host::original_control_panel_layout(width,height);
            const auto ja=odh_host::original_motive_panel_geometry(width,height,common,15,&tall);
            check(ja.tall&&ja.panel.contains(ja.panel.x+2,ja.panel.y+2)&&
                !ja.panel.contains(ja.panel.x-1,ja.panel.y+2),
                "same tall rectangle gives raised pointer occlusion without covering family controls");
            odh::RasterFrame raster;raster.width=width;raster.height=height;
            raster.pixels.assign(width*height,{1,2,3,255});raster.depth.assign(width*height,7);raster.owner_ids.assign(width*height,105);
            odh_host::draw_motive_panel(raster,full,15,&tall,&common,&labels,nullptr,&ja);
            check(raster.pixels[std::size_t(ja.panel.y)*width+ja.panel.x]==std::array<std::uint8_t,4>{1,2,3,255},
                "tall artwork alpha preserves its original corner");
            check(raster.pixels[std::size_t(common.portraits.y)*width+common.portraits.x]==std::array<std::uint8_t,4>{1,2,3,255},
                "raising Needs does not resize or repaint the family area");
            for(std::size_t i=0;i<8;++i) {
                const auto& cell=ja.labels[i];const auto& meter=ja.meters[i];
                check(cell.y+cell.height<=meter.y&&meter.y+meter.height<=int(height),
                    "label cells and last-row meters fit without overlap or footer clipping");
                const auto measured=odh_host::measure_text_overlay(labels[i],15,ja.label_font_size_px);
                check(measured.height<=cell.height,"the same active font metrics fit every localized label viewport");
                for(int y=meter.y;y<meter.y+meter.height;++y)
                    check(raster.pixels[std::size_t(y)*width+meter.x+meter.width/2]==std::array<std::uint8_t,4>{0,203,57,255},
                        "full five-pixel source meter retains all rows after composition/scaling");
            }
            if(width==1024&&height==768) {
                const int measured_rows[]{677,701,725,749};
                check(ja.panel.x==520&&ja.panel.y==618,"authored150px panel matches original bottom-relative anchor");
                for(unsigned row=0;row<4;++row)check(ja.meters[row].y==measured_rows[row]&&ja.meters[row+4].y==measured_rows[row],
                    "both columns match measured original row pairs");
            }
            if(width>1120)check(raster.pixels[std::size_t(ja.panel.y+2)*width+width-1]==std::array<std::uint8_t,4>{3,4,5,255},
                "wide desktop repeats the authored flat edge instead of stretching the corner");
            check(raster.depth==std::vector<float>(width*height,7)&&raster.owner_ids==std::vector<std::uint16_t>(width*height,105),
                "tall background and text preserve world depth and picking tokens");
            for(const auto language:{std::uint8_t(1),std::uint8_t(14)}) {
                const auto normal=odh_host::original_motive_panel_geometry(width,height,common,language,&tall);
                check(!normal.tall&&normal.title.x==common.needs_title.x&&normal.title.y==common.needs_title.y,
                    "English and Portuguese keep the original standard heading");
                for(std::size_t i=0;i<8;++i)check(normal.labels[i].x==common.needs[i].x&&normal.labels[i].y==common.needs[i].y,
                    "English and Portuguese retain every original standard row");
            }
        }
        const auto common=odh_host::original_control_panel_layout(800,600);
        bool missing=false;
        try{(void)odh_host::original_motive_panel_geometry(800,600,common,15,nullptr);}
        catch(const std::invalid_argument&){missing=true;}
        check(missing,"missing original tall resource cannot silently claim Japanese layout fidelity");
    }
    {
        // External oracle: Complete live-panel.png has 3x5 left chevrons
        // with rows1/2/3/2/1, separated by4px at the left of its meter.
        odh::RasterFrame raster;raster.width=800;raster.height=600;
        raster.pixels.assign(800*600,{1,2,3,255});raster.depth.assign(800*600,7);
        raster.owner_ids.assign(800*600,105);
        const auto layout=odh_host::original_control_panel_layout(800,600);
        const auto geometry=odh_host::original_motive_panel_geometry(800,600,layout,14);
        auto baseline=raster;odh_host::draw_motive_panel(baseline,state,14,nullptr,&layout);
        const std::array<int,8> changes{{-120,0,0,0,60,0,0,0}};
        const auto result=odh_host::draw_motive_panel(raster,state,14,nullptr,&layout,nullptr,nullptr,&geometry,&changes);
        check(result.arrow_pixels==27,"two left and one right three-by-five chevrons paint27pixels");
        for(unsigned row=0;row<5;++row)for(unsigned col=0;col<3;++col){
            const auto left=geometry.meters[0];const auto right=geometry.meters[4];
            const int distance=std::abs(int(row)-2);
            if(int(col)>=distance)for(unsigned n=0;n<2;++n)
                check(raster.pixels[std::size_t(left.y+row)*800+left.x-4-int(n)*4+col]==std::array<std::uint8_t,4>{214,0,0,255},
                      "falling chevrons preserve the observed left mask and4px pitch");
            if(int(col)<=2-distance)
                check(raster.pixels[std::size_t(right.y+row)*800+right.x+right.width+1+col]==std::array<std::uint8_t,4>{0,203,57,255},
                      "rising chevron points right of its actual meter");
        }
        check(raster.depth==baseline.depth&&raster.owner_ids==baseline.owner_ids,"trend paint preserves depth and ownership");
        const std::array<int,8> zero{};auto quiet=baseline;quiet.pixels.assign(800*600,{1,2,3,255});
        check(odh_host::draw_motive_panel(quiet,state,14,nullptr,&layout,nullptr,nullptr,&geometry,&zero).arrow_pixels==0&&quiet.pixels==baseline.pixels,
              "zero visual changes retain the original meter-only frame");
    }
    bool invalid_frame=false;
    try{odh::RasterFrame tiny{159,240,{}, {},0,0,{}};(void)odh_host::draw_motive_panel(tiny,state,14);}
    catch(const std::invalid_argument&){invalid_frame=true;}
    check(invalid_frame,"undersized frame is rejected safely");
    std::cout<<"linux motive panel: checks passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"linux motive panel: "<<error.what()<<'\n';return 1;}}
