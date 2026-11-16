// SPDX-License-Identifier: MPL-2.0
#include "app/ui/OriginalNeedsPresentation.h"
#include "app/ui/OriginalHover.h"
#include "original_control_panel.hpp"
#include "app/ui/RetainedControlPanel.h"
#include "app/ui/RetainedRegion.h"
#include "original_queue_view.hpp"
#include "original_tooltip_view.hpp"
#ifdef ODH_PRESENTATION_GAME_FONT_BACKEND
#include "app/ui/OriginalTextBackend.h"
#include "linux_text_renderer.hpp"
#endif
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>

namespace {
using namespace simapp;
using Pixel=std::array<std::uint8_t,4>;
unsigned checks=0;
uint64_t pixel_comparisons=0;
void check(bool yes,const char* message){++checks;if(!yes)throw std::runtime_error(message);}
odh::RasterFrame old_raster(const Canvas& c){
    odh::RasterFrame f{};f.width=c.Width;f.height=c.Height;f.pixels.reserve(c.Px.size());
    for(auto p:c.Px)f.pixels.push_back({uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)});
    return f;
}
void old_copy(const odh::RasterFrame& f,Canvas& c){
    for(size_t i=0;i<c.Px.size();++i){const auto& p=f.pixels[i];c.Px[i]=RGBA(p[0],p[1],p[2],p[3]);}
}
Canvas background(int w,int h,unsigned seed){Canvas c(w,h);std::mt19937 rng(seed);for(auto& p:c.Px)p=rng();return c;}
void put(std::string& b,uint32_t n,unsigned bytes){for(unsigned i=0;i<bytes;++i)b+=char(n>>(8*i));}
void at(std::string& b,size_t offset,uint32_t n,unsigned bytes){for(unsigned i=0;i<bytes;++i)b.at(offset+i)=char(n>>(8*i));}
std::string bmp(unsigned w,unsigned h){
    const unsigned stride=(w*3+3)&~3U;std::string b(54+stride*h,0);
    at(b,0,0x4d42,2);at(b,2,b.size(),4);at(b,10,54,4);at(b,14,40,4);
    at(b,18,w,4);at(b,22,h,4);at(b,26,1,2);at(b,28,24,2);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        const auto n=54+y*stride+x*3;const bool transparent=(x+y)%5==0;
        b[n]=char(transparent?255:40+x%13);b[n+1]=char(transparent?0:120+y%17);b[n+2]=char(transparent?255:20+x%31);
    }return b;
}
struct Fixture {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("odh-presentation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture(){
        std::vector<std::pair<std::string,std::string>> entries;
        for(const auto key:{"cpanel/backgrounds/panelback.bmp","cpanel/backgrounds/livepatch.bmp",
             "cpanel/backgrounds/buypatch.bmp","cpanel/backgrounds/buildpatch.bmp","cpanel/catalogunknown.bmp",
             "cpanel/buttons/objects.bmp","cpanel/buttons/people.bmp","cpanel/buttons/arch.bmp",
             "cpanel/buttons/scrollleft.bmp","cpanel/buttons/scrollright.bmp"})entries.push_back({key,bmp(4,1)});
        for(const auto key:odh_host::original_panel_category_art)entries.push_back({key,bmp(4,1)});
        entries.push_back({"CPanel/People/PeopleTemplate.bmp",bmp(180,45)});
        entries.push_back({"CPanel/Catalog/ThumbTemplate.bmp",bmp(180,45)});
        entries.push_back({"CPanel/Buttons/IconFrame.bmp",bmp(90,45)});
        entries.push_back({"Other/QueueCancel.bmp",bmp(45,45)});
        std::string b="FAR!byAZ";put(b,1,4);put(b,0,4);std::vector<unsigned> offsets;
        for(const auto& e:entries){offsets.push_back(b.size());b+=e.second;}
        at(b,12,b.size(),4);put(b,entries.size(),4);
        for(size_t i=0;i<entries.size();++i){put(b,entries[i].second.size(),4);put(b,entries[i].second.size(),4);
            put(b,offsets[i],4);put(b,entries[i].first.size(),4);b+=entries[i].first;}
        std::filesystem::create_directories(path/"UIGraphics");std::ofstream file(path/"UIGraphics/UIGraphics.far",std::ios::binary);
        file.write(b.data(),b.size());check(bool(file),"write synthetic UI archive");
    }
    ~Fixture(){std::error_code e;std::filesystem::remove_all(path,e);}
};
void portrait_blend(){
    auto source=background(32,32,234);
    source.Px[0]=RGBA(255,0,255,255);
    source.Px[1]=RGBA(255,0,255,73);
    const auto image=simapp::original::Bitmap(source);
    source.Px[0]=source.Px[1]=0;
    for(int y:{-40,-8,3,28})for(int x:{-40,-13,2,31}){
        auto expected=background(41,33,710),actual=expected;
        expected.Blit(source.Px.data(),32,32,x,y,true);
        simapp::original::DrawPortraitBitmap(actual,image,x,y);
        check(expected.Px==actual.Px,"direct portrait blit differs from the original keyed canvas");
    }
}
void transfers(){
    Canvas c=background(800,600,17);const auto expected=old_raster(c);
    check(original::Raster(c).pixels==expected.pixels,"bulk import equals original numeric unpack including alpha");
    auto restored=c;original::Copy(expected,restored);check(restored.Px==c.Px,"bulk export preserves all bits");
    std::vector<Pixel> portable(c.Px.size());original::detail::ImportPortable(c.Px.data(),portable.data(),portable.size());
    check(portable==expected.pixels,"portable import equals reference");
    std::vector<uint32_t> output(c.Px.size());original::detail::ExportPortable(portable.data(),output.data(),output.size());
    check(output==c.Px,"portable export equals reference");
    odh::RasterFrame scratch{};
    for(const auto r:{Rect{-7,-9,33,37},Rect{790,590,90,90},Rect{0,0,800,600},Rect{800,600,20,20},
            Rect{4,7,-1,2},Rect{std::numeric_limits<int>::max(),2,std::numeric_limits<int>::max(),8}}){
        auto target=c;const auto clip=original::Clip(c,r);original::Raster(c,scratch,r);
        for(int y=clip.Y;y<clip.Bottom();++y)for(int x=clip.X;x<clip.Right();++x)scratch.pixels[size_t(y)*c.Width+x]={1,2,3,4};
        original::Copy(scratch,target,r);
        bool exact=true;
        for(int y=0;y<c.Height;++y)for(int x=0;x<c.Width;++x){const auto i=size_t(y)*c.Width+x;++pixel_comparisons;
            exact=exact&&target.Px[i]==(clip.Contains(x,y)?RGBA(1,2,3,4):c.Px[i]);}
        check(exact,"clipped transfer changes only requested pixels");
    }
}
void retained_tooltip(){
    // The hover clock still owns visibility. Once it is shown, move the same
    // target, change its ink, resize its viewport, and replace its backdrop.
    OriginalHover hover;
    AppInput in;in.DesktopPointer=true;in.TouchX=40;in.TouchY=80;
    const std::string label="A long tooltip for the control panel that wraps on a narrow viewport";
    const Rect target{0,0,800,600};
    auto draw=[&](Canvas& canvas,uint32_t ink){
        hover.Draw(canvas,in,label,1,2,3,target,false,true,true,ink);
    };
    auto initial=background(320,200,3);
    const auto untouched=initial.Px;
    draw(initial,RGBA(0,0,0));
    check(initial.Px==untouched,"dwell time hides the initial tooltip");
    std::this_thread::sleep_for(std::chrono::milliseconds(510));
    for(unsigned i=0;i<5;++i){
        const int w=i>=3?160:320;
        auto canvas=background(w,200,i+10);
        const auto ink=i>1?RGBA(215,26,9):RGBA(0,0,0);
        in.TouchX=i==0?40:w-1;in.TouchY=i%2?1:199;
        auto expected=original::Raster(canvas);
        const auto box=odh_host::draw_original_tooltip(expected,label,original::TextLanguage(),{in.TouchX,in.TouchY},1,
            {uint8_t(ink),uint8_t(ink>>8),uint8_t(ink>>16),uint8_t(ink>>24)});
        check(bool(box),"reference tooltip fits");
        draw(canvas,ink);
        check(original::Raster(canvas).pixels==expected.pixels,"retained tooltip matches the original painter at the current pointer");
    }
    in.PointerCancelled=true;
    auto cancelled=initial;draw(cancelled,RGBA(0,0,0));
    check(cancelled.Px==untouched,"focus loss dismisses a retained tooltip");
}
void needs(){
    odh::BitmapImage art{600,150,32,true,std::vector<Pixel>(600*150),0};
    for(size_t i=0;i<art.pixels.size();++i)art.pixels[i]={uint8_t(i),uint8_t(i>>3),uint8_t(i>>7),uint8_t(i%256)};
    odh::RasterFrame scratch{};
    for(const auto size:{std::pair<int,int>{800,600},{1024,768},{1280,720},{320,240}})
      for(uint8_t language=1;language<=20;++language)
       for(int variant=0;variant<4;++variant){
        auto full=background(size.first,size.second,language*19+variant),bounded=full;
        const auto layout=odh_host::original_control_panel_layout(full.Width,full.Height);
        const auto geometry=odh_host::original_motive_panel_geometry(full.Width,full.Height,layout,language,&art);
        odh_host::StatusHud status;status.actor=7;
        if(variant!=3)status.needs=std::array<int16_t,7>{-100,-50,0,50,100,0,100};
        if(variant!=2)status.room_score=38;
        std::array<int,8> arrows{};for(size_t i=0;i<8;++i)arrows[i]=variant==0?-300:variant==1?300:int(i)*83-279;
        auto baseline=old_raster(full);const auto before=baseline.pixels;
        const auto old=odh_host::draw_motive_panel(baseline,status,language,&art,&layout,nullptr,nullptr,&geometry,&arrows);
        old_copy(baseline,full);
        // A retained scratch deliberately contains unrelated pixels and prior sizes.
        if(!scratch.pixels.empty())std::fill(scratch.pixels.begin(),scratch.pixels.end(),Pixel{211,7,198,3});
        const auto next=original::DrawNeeds(bounded,scratch,status,language,&art,layout,nullptr,nullptr,geometry,arrows);
        check(full.Px==bounded.Px,"production Needs painter full viewport equals retained bounded output");
        check(old.labels_drawn==next.labels_drawn&&old.missing_glyphs==next.missing_glyphs&&
            old.bar_pixels==next.bar_pixels&&old.arrow_pixels==next.arrow_pixels,"Needs text and paint status unchanged");
        const auto box=original::NeedsPaintBounds(full,geometry);
        bool untouched=true;
        for(int y=0;y<full.Height;++y)for(int x=0;x<full.Width;++x)
            if(!box.Contains(x,y)){++pixel_comparisons;untouched=untouched&&before[size_t(y)*full.Width+x]==baseline.pixels[size_t(y)*full.Width+x];}
        check(untouched,"Needs never writes outside declared envelope");
    }
}
void portable_needs_cache(){
    original::NeedsPanelCache cache;
    auto input=background(800,600,7);
    const auto layout=odh_host::original_control_panel_layout(input.Width,input.Height);
    auto geometry=odh_host::original_motive_panel_geometry(input.Width,input.Height,layout,1,nullptr);
    odh_host::StatusHud status; status.needs.emplace(); status.room_score=13;
    odh::RasterFrame scratch;
    std::array<int,8> arrows{};
    std::array<std::string,8> pet_labels{};
    const std::array<std::string,8>* labels=nullptr;
    auto compare=[&](bool expected_hit){
        auto control=input,handheld=input;
        original::DrawNeeds(control,scratch,status,1,nullptr,layout,labels,nullptr,geometry,arrows);
        const bool hit=cache.draw(handheld,scratch,status,1,nullptr,layout,labels,nullptr,geometry,arrows);
        check(hit==expected_hit && control.Px==handheld.Px,"portable Needs cache respects state and backdrop");
    };
    compare(false); compare(true);
    status.room_score=0; compare(false); compare(true);
    (*status.needs)[3]=-100; compare(false);
    arrows[0]=250; compare(false);
    labels=&pet_labels; compare(false);
    const auto bounds=original::NeedsPaintBounds(input,geometry);
    input.Px[size_t(bounds.Y)*input.Width+bounds.X]^=127;
    compare(false); compare(true);
    geometry.panel.x=bounds.X-2; geometry.panel.width=bounds.W+2;
    compare(false); compare(true);
    input.Px[0]^=127; compare(true);
}
void retained_handheld_layers(){
    Fixture fixture;
    odh_host::OriginalControlPanelArt art(fixture.path);
    odh_host::RetainedPanelArt retained;
    original::RetainedControlPanel controls(art);
    for(const int width:{640,800,1024,800})for(int i=0;i<28;++i){
        Canvas input=background(width,480,unsigned(i));
        auto expected=old_raster(input),actual=expected;
        odh_host::OriginalPanelState state;
        state.live_tabs.fill(true);state.mood=int16_t(i/3-4);state.paused=(i/3)%2;
        state.simulation_speed=i/6%2?1:3;state.pointer=std::array<int,2>{75+i/4,328};
        const auto mode=odh_host::OriginalPanelMode(i/7);
        art.draw(expected,mode,state);
        controls.draw(odh::UiBitmapSurface::from(actual),mode,state);
        check(expected.pixels==actual.pixels,"retained common panel differs on changing scene or state");
    }
    odh::BitmapImage edges{3,1,32,true,{{{35,6,209,17},{0,255,0,200},{255,0,255,255}}},0};
    for(int i=0;i<3;++i){
        Canvas input=background(50,10,unsigned(i));
        auto expected=old_raster(input),actual=expected;
        retained.begin();
        for(const auto box:{odh::UiBitmapRect{0,0,40,7},{4,1,40,7}}){
            retained.add(edges,box,{0,0,3,1});
            odh::blit_color_keyed_bitmap(expected,edges,box,std::nullopt,{255,0,255});
        }
        retained.draw(odh::UiBitmapSurface::from(actual),{0,0,50,10});
        check(expected.pixels==actual.pixels,"retained overlapping translucent edges");
    }
    bool rejected=false;
    retained.begin();retained.add(edges,{0,0,8,8},{4,0,3,1});
    try{auto frame=old_raster(background(50,10,0));retained.draw(odh::UiBitmapSurface::from(frame),{0,0,50,10});}
    catch(const odh::FormatError&){rejected=true;}
    check(rejected,"retained panel accepted an invalid source crop");
    original::RetainedRegion region;
    const Rect box{2,1,12,5};Canvas scene=background(20,10,17);
    for(int i=0;i<6;++i){
        if(i==3)scene.Px[24]^=1;
        Canvas reference=scene,actual=scene;
        const auto color=i<2?RGBA(17,159,35,200):RGBA(17,25,35,30);
        reference.FillRect(box,color);
        const bool hit=region.draw(actual,box,i!=0&&i!=2,[&]{actual.FillRect(box,color);});
        check(actual.Px==reference.Px,"resident or label region changed pixels");
        check(hit==(i==1 || i>=4),"resident or label region invalidation");
    }
}
void portraits_and_queue(){
    Fixture fixture;odh_host::OriginalControlPanelArt art(fixture.path);odh_host::OriginalQueueView queue(fixture.path);
    odh::BitmapImage head{35,41,32,true,std::vector<Pixel>(35*41),0};
    for(size_t i=0;i<head.pixels.size();++i)head.pixels[i]={uint8_t(i),uint8_t(i>>3),77,uint8_t(i%256)};
    odh::RasterFrame scratch{};
    for(const auto rect:{Rect{229,505,45,45},Rect{-7,-9,45,45},Rect{790,590,45,45},Rect{30,50,17,29}})
      for(unsigned state=0;state<4;++state)for(bool authored:{false,true})for(unsigned turn=0;turn<2;++turn){
        auto full=background(800,600,state*31+turn),bounded=full;auto frame=old_raster(full);
        art.draw_portrait_state(frame,{rect.X,rect.Y,rect.W,rect.H},state,&head,authored);old_copy(frame,full);
        original::Raster(bounded,scratch,rect);art.draw_portrait_state(scratch,{rect.X,rect.Y,rect.W,rect.H},state,&head,authored);
        original::Copy(scratch,bounded,rect);check(full.Px==bounded.Px,"actual portrait painter is confined to exact box in every state");
    }
    for(unsigned count:{1U,8U,25U})for(bool cancel:{false,true}){
        auto full=background(800,600,count),bounded=full;auto frame=old_raster(full);
        for(unsigned i=0;i<count;++i){odh_host::QueueTile tile{};tile.box={32+int(i)*50,32,45,45};tile.active=i==0;tile.cancelled=cancel;
            queue.draw_tile(frame,tile,&head,odh_host::QueueIconKind::authored_avatar);
            const auto rect=original::Clip(bounded,{tile.box.x,tile.box.y,tile.box.width,tile.box.height});
            if (!rect.W || !rect.H) continue;
            original::Raster(bounded,scratch,rect);
            queue.draw_tile(scratch,tile,&head,odh_host::QueueIconKind::authored_avatar);
            original::Copy(scratch,bounded,rect);
        }
        old_copy(frame,full);
        check(full.Px==bounded.Px,"visible queue tiles match the complete strip including clipped queues");
    }
}
volatile uint64_t sink=0;
template<class F>double timing(F operation,int iterations){
    const auto start=std::chrono::steady_clock::now();for(int i=0;i<iterations;++i){operation();}
    return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/iterations;
}
void benchmark(){
    auto c=background(800,600,88);odh::RasterFrame retained{};
    for(const auto r:{Rect{0,0,800,600},Rect{0,417,800,183},Rect{0,505,800,45},Rect{229,505,45,45},Rect{519,500,281,100}}){
        const bool full=r.W==800&&r.H==600;
        const auto baseline=timing([&]{
            if(full){auto f=old_raster(c);f.pixels[0][0]^=1;old_copy(f,c);}
            else {retained.width=c.Width;retained.height=c.Height;retained.pixels.resize(c.Px.size());
                for(int y=r.Y;y<r.Bottom();++y)for(int x=r.X;x<r.Right();++x){const auto i=size_t(y)*c.Width+x;const auto p=c.Px[i];retained.pixels[i]={uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)};}
                retained.pixels[size_t(r.Y)*c.Width+r.X][0]^=1;
                for(int y=r.Y;y<r.Bottom();++y)for(int x=r.X;x<r.Right();++x){const auto i=size_t(y)*c.Width+x;const auto& p=retained.pixels[i];c.Px[i]=RGBA(p[0],p[1],p[2],p[3]);}}
            sink+=c.Px[size_t(r.Y)*c.Width+r.X];},1000);
        const auto fast=timing([&]{original::Raster(c,retained,r);retained.pixels[size_t(r.Y)*c.Width+r.X][0]^=1;
            original::Copy(retained,c,r);sink+=c.Px[size_t(r.Y)*c.Width+r.X];},1000);
        std::cout<<"transfer "<<r.W<<'x'<<r.H<<" old_us="<<baseline<<" retained_bulk_us="<<fast<<" ratio="<<baseline/fast<<'\n';
    }
}
}
int main(int argc,char**){try{
#ifdef ODH_PRESENTATION_GAME_FONT_BACKEND
    odh_host::set_text_overlay_backend(simapp::original::GameFontTextBackend());
    std::cout<<"backend=production-game-font-fallback (no commercial font assets loaded)\n";
#else
    std::cout<<"backend=existing-host-text\n";
#endif
    portrait_blend();transfers();retained_tooltip();needs();portable_needs_cache();retained_handheld_layers();portraits_and_queue();std::cout<<"checks="<<checks<<" pixel_comparisons="<<pixel_comparisons<<" passed\n";if(argc>1)benchmark();}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
