// SPDX-License-Identifier: MPL-2.0
#include "app/ui/OriginalPiePresentation.h"

#include <iostream>
#include <stdexcept>

namespace {
using namespace simapp;
using namespace odh_host;
std::size_t comparisons=0;
Canvas patterned_canvas(int width,int height,int phase){
    Canvas c(width,height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        c.Px[std::size_t(y)*width+x]=RGBA((x+phase*19)%256,(y*3+phase)%256,(x^y^phase)%256,(x+y+phase)%256);
    return c;
}
void compare(odh::RasterFrame& scratch,const PieMenuBranchPresentation& branch,
        const std::vector<PieMenuCaption>& labels,const std::optional<PieMenuCaption>& parent,
        int width,int height,bool with_art,bool with_head,std::size_t selected,int phase){
    auto expected=patterned_canvas(width,height,phase),actual=expected;
    const auto head=patterned_canvas(200,200,phase);
    auto art_canvas=patterned_canvas(201,201,phase);
    // Transparent and opaque magenta, opaque black, and partial-alpha artwork.
    for(std::size_t i=0;i<art_canvas.Px.size();++i){
        if(i%17==0)art_canvas.Px[i]=RGBA(255,0,255,255);
        if(i%11==0)art_canvas.Px[i]=RGBA(0,0,0,255);
        if(i%7==0)art_canvas.Px[i]&=0xffffff;
    }
    const auto art=original::Bitmap(art_canvas);
    const auto draw_head=[&](Canvas& target){if(with_head)target.Blit(head.Px.data(),200,200,
        branch.head_center[0]-100,branch.head_center[1]-100,true);};
    const bool pressed=(phase&1)!=0,highlight=(phase&2)!=0;
    // Frozen prior DrawDesktop order with all four full-display conversions.
    auto frame=original::Raster(expected);
    auto head_layout=branch.layout;head_layout.center=branch.head_center;
    draw_linux_pie_menu_head(frame,head_layout,nullptr);
    original::Copy(frame,expected);
    draw_head(expected);
    frame=original::Raster(expected);
    draw_linux_pie_menu(frame,branch.layout,labels,selected,parent,with_art?&art:nullptr,pressed,
        PieMenuBackground::live_face,highlight);
    original::Copy(frame,expected);
    original::DrawDesktopPie(actual,scratch,branch.layout,branch.head_center,labels,selected,parent,
        with_art?&art:nullptr,pressed,highlight,draw_head);
    ++comparisons;
    if(expected.Px!=actual.Px)throw std::runtime_error("bounded pie differs from full-frame drawing at case "+std::to_string(comparisons));
}
}
int main(){
    const std::vector<PieMenuCaption> root{{"Wave",1},{"Talk",1},{"Play",1},{"Flirt",1}};
    const std::vector<PieMenuCaption> children{{"About interests",1},{"Gossip",1}};
    std::vector<PieMenuCaption> many(40,{"A longer interaction",1});
    odh::RasterFrame scratch{};
    for(const auto& display:{std::pair{800,600},std::pair{960,544},std::pair{1280,720}}){
        const auto [width,height]=display;
        for(const auto anchor:{std::array<std::int32_t,2>{0,0},std::array<std::int32_t,2>{width-1,height-1},
                std::array<std::int32_t,2>{width/2,height/2}}){
            for(int scenario=0;scenario<4;++scenario){
                const bool submenu=scenario==1||scenario==3,paged=scenario>=2;
                const auto& labels=paged?many:submenu?children:root;
                const std::size_t selected=paged?39:1;
                std::vector<std::vector<PieMenuCaption>> levels{root};
                if(submenu)levels.push_back(labels);else levels[0]=labels;
                const auto branch=layout_linux_pie_menu_branch(width,height,anchor,levels,submenu?std::vector<std::size_t>{3}:
                    std::vector<std::size_t>{},selected);
                if(!branch)throw std::runtime_error("test pie layout unavailable");
                const auto parent=submenu?std::optional<PieMenuCaption>{{"Flirt",1}}:std::nullopt;
                for(int flags=0;flags<4;++flags)compare(scratch,*branch,labels,parent,width,height,
                    (flags&1)!=0,(flags&2)!=0,selected,scenario*4+flags);
            }
            const auto nested=layout_linux_pie_menu_branch(width,height,anchor,{root,children,children},{3,0},0);
            if(!nested)throw std::runtime_error("nested pie layout unavailable");
            compare(scratch,*nested,children,PieMenuCaption{"About interests",1},width,height,true,true,0,17);
        }
    }
    // Bounds clipping and 200px head edges remain exact when the head is
    // clipped independently of the menu's in-bounds captions.
    auto edge=layout_linux_pie_menu_branch(800,600,{400,300},{root},{},0).value();
    for(const auto center:{std::array<std::int32_t,2>{0,0},std::array<std::int32_t,2>{799,599}}){
        edge.head_center=center;
        compare(scratch,edge,root,std::nullopt,800,600,true,true,0,19);
    }
    // The same buffer is reused across changing scenes, menu ancestry, pages
    // and dimensions. Pixels outside all draw regions must remain untouched.
    const auto clipped=original::Clip(Canvas(7,5),Rect{INT32_MAX,INT32_MIN,INT32_MAX,INT32_MAX});
    if(clipped.W||clipped.H)throw std::runtime_error("extreme bounded conversion did not clip safely");
    std::cout<<"Compared "<<comparisons<<" original pie frames byte-for-byte with full-frame drawing\n";
}
