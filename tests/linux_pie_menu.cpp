// SPDX-License-Identifier: MPL-2.0
#include "linux_pie_menu.hpp"
#include <stdexcept>
#include <algorithm>
#include "linux_text_renderer.hpp"
#include "original_portrait.hpp"
#include <limits>
#include <cmath>
#include <utility>

int main(){
    using namespace odh_host;
    {
        const std::vector<PieMenuCaption> root{{"Acenar",14},{"Flerte",14},{"Brincar",14},{"Conversar",14}};
        const std::vector<PieMenuCaption> children{{"Sobre Interesses",14},{"Fofocar",14}};
        const auto same_rect=[](const odh::PieMenuRect& a,const odh::PieMenuRect& b){
            return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;
        };
        auto expected=odh::layout_pie_menu(800,600,{400,300},root.size(),false).value();
        fit_linux_pie_menu_captions(expected,root);
        const auto top=layout_linux_pie_menu_branch(800,600,{400,300},{root},{},0);
        if(!top||top->head_target||top->head_center!=expected.center||top->layout.center!=expected.center)
            throw std::runtime_error("root branch changed its original head center or gaze");
        for(std::size_t i=0;i<root.size();++i)if(!same_rect(top->layout.items[i],expected.items[i]))
            throw std::runtime_error("root branch changed its original plaques");
        // Independently observed left Talk and right Flirt categories keep
        // their plaque, with the two children in that same vertical column.
        for(const auto category:{std::size_t{1},std::size_t{3}}){
            const auto branch=layout_linux_pie_menu_branch(800,600,{400,300},{root,children},{category},1);
            const auto parent=expected.items[category];
            const std::array<std::int32_t,2> parent_center{parent.x+parent.width/2,parent.y+parent.height/2};
            if(!branch||branch->head_center!=expected.center||branch->head_target!=parent_center||
               branch->layout.center!=parent_center||!branch->layout.back_button||
               !same_rect(*branch->layout.back_button,parent))
                throw std::runtime_error("submenu replaced the root head or moved the parent return plaque");
            const auto& up=branch->layout.items[0];const auto& down=branch->layout.items[1];
            if(up.x+up.width/2!=parent_center[0]||down.x+down.width/2!=parent_center[0]||
               up.y+up.height!=parent_center[1]-60||down.y!=parent_center[1]+60)
                throw std::runtime_error("submenu did not surround its category in the same column");
            PieMenuPointer pointer;std::size_t selected=0;
            pointer.process(branch->layout,{{WindowPointerKind::motion,up.x+2,up.y+2}},false,selected);
            if(selected!=0||!pointer.hover_target())throw std::runtime_error("submenu child geometry not shared with pointer");
            const auto back=pointer.process(branch->layout,{{WindowPointerKind::press,parent.x+2,parent.y+2},
                {WindowPointerKind::release,parent.x+2,parent.y+2}},false,selected);
            if(!back.activated)throw std::runtime_error("original category plaque did not return from submenu");
        }
        std::vector<PieMenuCaption> many(30,{"Escolha",14});
        const std::vector<PieMenuCaption> vertical_root{{"Dançar",14},{"Conversar",14}};
        // Regression: independently clamping the top child covered the parent
        // return plaque. Move the complete branch, preserving its head gaze.
        for(const auto& [anchor_y,category]:{std::pair{0,std::size_t{0}},std::pair{599,std::size_t{1}}}){
            const auto base=layout_linux_pie_menu_branch(800,600,{400,anchor_y},{vertical_root},{},category);
            const auto branch=layout_linux_pie_menu_branch(800,600,{400,anchor_y},
                {vertical_root,children},{category},0);
            if(!base||!branch)throw std::runtime_error("edge submenu unavailable");
            const auto before=base->layout.items[category];
            const std::array<std::int32_t,2> old_center{before.x+before.width/2,before.y+before.height/2};
            const auto parent=*branch->layout.back_button;
            if(branch->head_center!=base->head_center||branch->head_target!=old_center||
               branch->layout.center==old_center||parent.x+parent.width/2!=branch->layout.center[0]||
               parent.y+parent.height/2!=branch->layout.center[1])
                throw std::runtime_error("edge adjustment moved the head or detached the parent plaque");
            if(branch->layout.center[0]-100<4||branch->layout.center[0]+101>796||
               branch->layout.center[1]-100<4||branch->layout.center[1]+101>596)
                throw std::runtime_error("edge adjustment omitted transparent surface or 4px margin");
            const auto& up=branch->layout.items[0];const auto& down=branch->layout.items[1];
            if(up.y+up.height!=branch->layout.center[1]-60||down.y!=branch->layout.center[1]+60)
                throw std::runtime_error("edge adjustment distorted child spacing");
            const auto overlap=[](const odh::PieMenuRect& a,const odh::PieMenuRect& b){
                return a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;
            };
            if(overlap(parent,up)||overlap(parent,down)||overlap(up,down))
                throw std::runtime_error("edge submenu hides its return target");
            PieMenuPointer pointer;std::size_t selected=0;
            const auto back=pointer.process(branch->layout,{{WindowPointerKind::press,parent.x+2,parent.y+2},
                {WindowPointerKind::release,parent.x+2,parent.y+2}},false,selected);
            if(!back.activated)throw std::runtime_error("edge submenu return target cannot be clicked");
        }
        for(const auto anchor:{std::array<std::int32_t,2>{0,0},std::array<std::int32_t,2>{799,599}}){
            const auto branch=layout_linux_pie_menu_branch(800,600,anchor,{root,many},{3},29);
            if(!branch||branch->layout.page_first==0||!branch->layout.previous_page)
                throw std::runtime_error("submenu selected page was not preserved");
            const auto whole=[](const odh::PieMenuRect& b){
                return b.width<=0||(b.x>=0&&b.y>=0&&b.x+b.width<=800&&b.y+b.height<=600);
            };
            for(const auto& box:branch->layout.items)if(!whole(box))
                throw std::runtime_error("submenu plaque clipped at display edge");
            if(!whole(*branch->layout.back_button)||!whole(*branch->layout.previous_page))
                throw std::runtime_error("submenu return/page control clipped at display edge");
        }
        const auto nested=layout_linux_pie_menu_branch(800,600,{400,300},{root,children,children},{3,0},0);
        const auto first=layout_linux_pie_menu_branch(800,600,{400,300},{root,children},{3},0);
        if(!nested||nested->head_center!=first->head_center||nested->head_target!=first->head_target)
            throw std::runtime_error("nested generic branch lost its root head identity");
        if(layout_linux_pie_menu_branch(800,600,{400,300},{root},{3},0)||
           layout_linux_pie_menu_branch(800,600,{400,300},{root,children},{4},0)||
           layout_linux_pie_menu_branch(800,600,{400,300},{root,{}},{3},0)||
           layout_linux_pie_menu_branch(800,600,{400,300},{root,children},{3},2))
            throw std::runtime_error("invalid submenu ancestry or selection accepted");
    }
    {
        const auto center=std::array<std::int32_t,2>{160,120};
        const auto neutral=original_pie_portrait_camera(center,center);
        const auto right=original_pie_portrait_camera(center,{260,120});
        const auto upper=original_pie_portrait_camera(center,{160,20});
        if(neutral.viewport_size!=200||neutral.zoom!=5.12||neutral.yaw_radians!=0||neutral.pitch_radians!=0||
           std::abs(right.yaw_radians+std::atan(1.0))>1e-12||right.pitch_radians!=0||
           std::abs(upper.pitch_radians-std::atan(1.0))>1e-12||upper.yaw_radians!=0||
           original_frontal_portrait_camera().viewport_size!=74||original_frontal_portrait_camera().yaw_radians!=0)
            throw std::runtime_error("pie look direction or separate frontal portrait profile changed");

        odh::RasterFrame render{};render.width=render.height=200;
        render.pixels.assign(40000,{17,29,43,255});
        render.depth.assign(40000,std::numeric_limits<float>::infinity());
        render.pixels[100*200+100]={0,0,0,255};render.depth[100*200+100]=1;
        render.pixels[100*200+101]={255,0,255,255};render.depth[100*200+101]=2;
        const auto first=pie_menu_head_bitmap(render);
        auto second=first;second.pixels[100*200+100]={60,120,180,255};
        odh::RasterFrame display{};display.width=240;display.height=220;
        const std::array<std::uint8_t,4> scene{200,160,120,255};
        display.pixels.assign(display.width*display.height,scene);
        // Deliberately overlap the edge: the disc must retain its center and
        // gradient when clipped, instead of shrinking to the available frame.
        odh::PieMenuLayout edge{};edge.center={10,110};
        const auto pixel=[&](int x,int y){return display.pixels[y*display.width+x];};
        draw_linux_pie_menu_head(display,edge,&first);
        if(pixel(10,110)!=std::array<std::uint8_t,4>{0,0,0,255}||
           pixel(11,110)!=std::array<std::uint8_t,4>{255,0,255,255}||
           pixel(10,111)!=std::array<std::uint8_t,4>{100,80,60,255}||
           pixel(85,110)!=std::array<std::uint8_t,4>{150,120,90,255}||pixel(110,110)!=scene||
           pixel(10,10)!=scene||first.pixels.front()[3]!=0)
            throw std::runtime_error("pie head coverage, original soft disc or clipping failed");
        display.pixels.assign(display.width*display.height,scene);
        draw_linux_pie_menu_head(display,edge,&second);
        if(pixel(10,110)!=second.pixels[100*200+100])
            throw std::runtime_error("pie composition retained another Sim's head");
        bool rejected=false;render.depth.clear();
        try{(void)pie_menu_head_bitmap(render);}catch(const odh::FormatError&){rejected=true;}
        if(!rejected)throw std::runtime_error("head without geometry coverage accepted");
    }
    {
        auto fitted=odh::layout_pie_menu(800,600,{400,250},4,false).value();
        const auto original=fitted;
        const std::vector<PieMenuCaption> captions{{"Sentar",14},{"Ir Até",14},{"Falar",14},{"Ler",14}};
        fit_linux_pie_menu_captions(fitted,captions);
        for(std::size_t i=0;i<captions.size();++i){
            const auto& box=fitted.items[i];
            if(box.width>=original.items[i].width||box.width<24||box.height!=original.items[i].height)
                throw std::runtime_error("short action did not get a content-sized plaque");
            if(box.x<original.items[i].x||box.x+box.width>original.items[i].x+original.items[i].width)
                throw std::runtime_error("caption fit escaped its radial slot");
        }
        PieMenuPointer pointer;std::size_t selected=0;
        const auto& before=original.items[0];
        const std::vector<WindowPointerEvent> outside{{WindowPointerKind::press,before.x+1,before.y+5},
            {WindowPointerKind::release,before.x+1,before.y+5}};
        if(pointer.process(fitted,outside,false,selected).activated)
            throw std::runtime_error("empty former plaque area still activates an action");
    }
    const auto layout=odh::layout_pie_menu(320,240,{160,120},2,true).value();
    odh::RasterFrame frame{};frame.width=320;frame.height=240;
    const std::array<std::uint8_t,4> scene{10,20,30,255},plate{0,40,140,255},mark{41,61,81,255};
    frame.pixels.assign(320*240,scene);
    odh::BitmapImage art{};art.width=art.height=201;
    art.pixels.assign(201*201,{255,0,255,255});
    art.pixels[1]=mark;
    const auto at=[&](int x,int y){return frame.pixels[static_cast<std::size_t>(y)*320+x];};
    const std::vector<PieMenuCaption> labels{{"Sentar",14},{"Falar",14}};
    const PieMenuCaption parent{"Ações",14};
    draw_linux_pie_menu(frame,layout,labels,1,parent,&art);
    const auto box=layout.items.front();
    if(at(60,20)!=scene||at(61,20)!=mark||at(box.x,box.y)!=scene||
       at(box.x+5,box.y)!=plate||at(box.x+box.width-4,box.y+2)!=plate)
        throw std::runtime_error("original background transparency or rounded click plate not preserved");
    {
        PieMenuPointer pointer;
        std::size_t selected=0;
        const auto a=layout.items[0],b=layout.items[1];
        const auto event=[](WindowPointerKind kind,const odh::PieMenuRect& box){
            return WindowPointerEvent{kind,box.x+2,box.y+2};
        };
        const auto apply=[&](std::vector<WindowPointerEvent> events,bool cancel=false){
            return pointer.process(layout,events,cancel,selected);
        };
        if(!apply({event(WindowPointerKind::motion,b)}).redraw||selected!=1)
            throw std::runtime_error("hover did not select the visible action");
        const auto b_center=std::array<std::int32_t,2>{b.x+b.width/2,b.y+b.height/2};
        if(pointer.hover_target()!=b_center||apply({event(WindowPointerKind::motion,b)}).redraw)
            throw std::runtime_error("head target is not the stable hovered plaque center");
        if(!apply({{WindowPointerKind::motion,0,0}}).redraw||pointer.hover_target()||selected!=1)
            throw std::runtime_error("hover exit retained the last option's head direction");
        apply({event(WindowPointerKind::motion,b)});
        if(!apply({event(WindowPointerKind::motion,*layout.back_button)}).redraw||pointer.hover_target())
            throw std::runtime_error("parent control became a head look target");
        if(!apply({event(WindowPointerKind::press,a)}).consumed||!pointer.pressed()||selected!=0)
            throw std::runtime_error("press did not capture action");
        if(apply({event(WindowPointerKind::motion,b)}).activated||pointer.pressed()||selected!=1)
            throw std::runtime_error("drag across actions retained wrong depressed highlight");
        auto r=apply({event(WindowPointerKind::release,b)});
        if(r.activated||!r.consumed)throw std::runtime_error("release over another action triggered a choice");
        r=apply({event(WindowPointerKind::press,b),event(WindowPointerKind::release,b)});
        if(!r.activated||pointer.pressed()||pointer.hover_target()!=b_center)
            throw std::runtime_error("complete gesture or release hover in one poll was lost");
        if(!apply({},true).redraw||pointer.hover_target())
            throw std::runtime_error("cancel did not clear and redraw the head target");
        apply({event(WindowPointerKind::motion,b)});pointer.reset();
        if(pointer.hover_target())throw std::runtime_error("explicit pointer reset retained a head target");
        apply({event(WindowPointerKind::motion,b)});
        auto removed=layout;removed.items[1]={};
        if(!pointer.process(removed,{},false,selected).redraw||pointer.hover_target())
            throw std::runtime_error("removed or disabled plaque retained its head target");
        apply({event(WindowPointerKind::press,a)});
        if(!apply({},true).consumed||apply({event(WindowPointerKind::release,a)}).activated)
            throw std::runtime_error("cancelled pointer sequence was retained");
        if(apply({}).consumed)throw std::runtime_error("synthetic input without pointer events was consumed");
        const auto paged=odh::layout_pie_menu(320,200,{160,0},18,true).value();
        const auto later=odh::layout_pie_menu(320,200,{160,0},18,true,paged.page_size).value();
        pointer.process(paged,{event(WindowPointerKind::press,paged.items[0])},false,selected);
        if(!pointer.process(later,{},false,selected).redraw||pointer.hover_target())
            throw std::runtime_error("page replacement retained an old hover without pointer motion");
        if(pointer.process(later,{event(WindowPointerKind::release,later.items[later.page_first])},false,selected).activated)
            throw std::runtime_error("page replacement rebound an old press to another action");
        for(const auto& control:{paged.next_page,paged.back_button,later.previous_page}){
            const auto& current=control->x==later.previous_page->x?later:paged;
            const auto choice=pointer.process(current,{event(WindowPointerKind::press,*control),
                event(WindowPointerKind::release,*control)},false,selected);
            if(!choice.activated||pointer.pressed())throw std::runtime_error("page or back control gesture was lost");
        }
    }
    // Compare complete glyphs rendered on the source plate, so the installed
    // font's subpixel rendering is identical on both sides of the comparison.
    const auto check_ink=[&](std::size_t i,std::array<std::uint8_t,4> ink){
        const auto& rect=layout.items[i];
        auto reference=frame;
        std::fill(reference.pixels.begin(),reference.pixels.end(),plate);
        TextOverlayStyle style;style.x=rect.x;style.y=rect.y;style.width=rect.width;style.height=rect.height;
        style.font_size_px=13;style.padding=3;style.foreground=ink;style.background={0,0,0,0};
        if(!draw_text_overlay(reference,labels[i].text,labels[i].language,style).pixels_changed)
            throw std::runtime_error("reference glyphs missing");
        for(int y=rect.y+3;y<rect.y+rect.height-3;++y)for(int x=rect.x+3;x<rect.x+rect.width-3;++x)
            if(at(x,y)!=reference.pixels[static_cast<std::size_t>(y)*320+x])
                throw std::runtime_error("action text differs from the live mode ink");
    };
    check_ink(0,{165,195,214,255});
    check_ink(1,{255,255,255,255});
    const auto resting=frame.pixels;
    draw_linux_pie_menu(frame,layout,labels,1,parent,&art,true);
    bool changed=false;
    for(int y=0;y<240;++y)for(int x=0;x<320;++x){
        if(at(x,y)==resting[static_cast<std::size_t>(y)*320+x])continue;
        if(!layout.items[1].contains(x,y))throw std::runtime_error("pressed highlight escaped selected action");
        changed=true;
    }
    check_ink(1,{0,255,255,255});
    odh::BitmapImage generic{};generic.width=generic.height=195;generic.pixels.resize(195*195);
    bool rejected=false;
    try{draw_linux_pie_menu(frame,layout,labels,1,parent,&generic);}
    catch(const odh::FormatError&){rejected=true;}
    if(!rejected)throw std::runtime_error("generic hands image accepted as live face");
    draw_linux_pie_menu(frame,layout,labels,1,parent,&generic,false,PieMenuBackground::diagnostic_generic);
    if(!changed)throw std::runtime_error("press did not highlight selected action");
}
