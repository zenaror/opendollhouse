// SPDX-License-Identifier: MPL-2.0
#include "linux_pie_menu.hpp"
#include "linux_text_renderer.hpp"
#include "odh/ui_bitmap.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace odh_host {
namespace {
// cWinPeople overrides the generic palette before opening the lot menus
// (classic 00518781–00518882). cTSPieMenu 005eae1b chooses hover or press.
constexpr std::array<std::uint8_t,4> regular{165,195,214,255},highlight{255,255,255,255},
    down{0,255,255,255},plate{0,40,140,255};

void draw_caption(odh::RasterFrame& frame,const odh::PieMenuRect& box,const PieMenuCaption& label,
        std::array<std::uint8_t,4> ink){
    if(box.width<12||box.height<12||box.x<0||box.y<0||
       std::int64_t(box.x)+box.width>frame.width||std::int64_t(box.y)+box.height>frame.height)
        throw odh::FormatError("pie caption outside display");
    // Classic cTSPieMenu 005eac32. Fill the inset body, then its two
    // one-pixel horizontal and vertical edge strips on each side.
    const auto fill=[&](int x,int y,int w,int h){
        for(int row=y;row<y+h;++row){
            auto start=frame.pixels.begin()+static_cast<std::size_t>(row)*frame.width+x;
            std::fill_n(start,w,plate);
        }
    };
    const int x=box.x+2,y=box.y+2,w=box.width-4,h=box.height-4;
    fill(x,y,w,h);
    for(int i=1;i<=2;++i){
        const int inset=i==1?1:3;
        fill(x+inset,y-i,w-2*inset,1);
        fill(x+inset,y+h+i-1,w-2*inset,1);
        fill(x-i,y+inset,1,h-2*inset);
        fill(x+w+i-1,y+inset,1,h-2*inset);
    }
    if(label.text.empty())return;
    TextOverlayStyle style;
    style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
    style.font_size_px=13;style.padding=3;style.foreground=ink;style.background={0,0,0,0};
    (void)draw_text_overlay(frame,label.text,label.language,style);
}
}
odh::BitmapImage pie_menu_head_bitmap(const odh::RasterFrame& frame){
    if(frame.width!=200||frame.height!=200||frame.pixels.size()!=40000||frame.depth.size()!=40000)
        throw odh::FormatError("pie head requires its 200px render and coverage");
    odh::BitmapImage image{200,200,32,true,frame.pixels,0};
    for(std::size_t i=0;i<image.pixels.size();++i)
        image.pixels[i][3]=std::isfinite(frame.depth[i])?255:0;
    return image;
}
void draw_linux_pie_menu_head(odh::RasterFrame& frame,const odh::PieMenuLayout& layout,
        const odh::BitmapImage* head){
    if(head&&(head->width!=200||head->height!=200||head->pixels.size()!=40000||head->bit_count!=32))
        throw odh::FormatError("pie head must be a transparent 200px render");
    // FreeSO TextureGenerator.GetPieBG at the pinned reference: half-opacity
    // inside radius 50, fading to transparent at radius 100.
    static const auto disc=[](){
        odh::BitmapImage image{200,200,32,true,{},0};image.pixels.reserve(40000);
        for(int y=0;y<200;++y)for(int x=0;x<200;++x){
            const auto alpha=0.5*std::clamp(2.0-std::hypot(x-100.0,y-100.0)/50.0,0.0,1.0);
            image.pixels.push_back({0,0,0,static_cast<std::uint8_t>(std::lround(alpha*255))});
        }
        return image;
    }();
    const odh::UiBitmapRect destination{layout.center[0]-100,layout.center[1]-100,200,200};
    odh::blit_rgba_bitmap(frame,disc,destination);
    if(head)odh::blit_rgba_bitmap(frame,*head,destination);
}
void fit_linux_pie_menu_captions(odh::PieMenuLayout& layout,const std::vector<PieMenuCaption>& captions){
    if(captions.size()!=layout.items.size())throw odh::FormatError("pie captions do not match layout");
    for(auto i=layout.page_first;i<captions.size()&&i-layout.page_first<layout.page_size;++i){
        auto& box=layout.items[i];
        const auto width=std::clamp(measure_text_overlay(captions[i].text,captions[i].language,13).width+12,24,box.width);
        const auto old_width=box.width;
        if(box.x+old_width<=layout.center[0])box.x+=old_width-width;
        else if(box.x<layout.center[0])box.x+=(old_width-width)/2;
        box.width=width;
    }
}
std::optional<PieMenuBranchPresentation> layout_linux_pie_menu_branch(
        std::uint32_t width,std::uint32_t height,std::array<std::int32_t,2> root_anchor,
        const std::vector<std::vector<PieMenuCaption>>& levels,
        const std::vector<std::size_t>& path,std::size_t selected){
    if(levels.size()!=path.size()+1||levels.empty())return {};
    for(const auto& captions:levels)if(captions.empty())return {};
    for(std::size_t depth=0;depth<path.size();++depth)
        if(path[depth]>=levels[depth].size())return {};
    if(selected>=levels.back().size())return {};
    auto layout=odh::layout_pie_menu(width,height,root_anchor,levels.front().size(),false,
        path.empty()?selected:path.front());
    if(!layout)return {};
    fit_linux_pie_menu_captions(*layout,levels.front());
    PieMenuBranchPresentation result{*layout,layout->center,{}};
    for(std::size_t depth=0;depth<path.size();++depth){
        const auto parent=result.layout.items[path[depth]];
        if(parent.width<=0||parent.height<=0)return {};
        const std::array<std::int32_t,2> parent_center{parent.x+parent.width/2,parent.y+parent.height/2};
        if(!result.head_target)result.head_target=parent_center;
        auto child=odh::layout_pie_menu(width,height,root_anchor,levels[depth+1].size(),true,
            depth+1<path.size()?path[depth+1]:selected);
        if(!child)return {};
        fit_linux_pie_menu_captions(*child,levels[depth+1]);
        // Centered captions can have odd measured widths. Preserve the parent
        // column exactly instead of accumulating half-pixel truncation per level.
        for(auto& box:child->items)
            if(box.width>0&&box.x<child->center[0]&&box.x+box.width>child->center[0])
                box.x=child->center[0]-box.width/2;
        const auto dx=parent_center[0]-child->center[0],dy=parent_center[1]-child->center[1];
        const auto translate=[&](odh::PieMenuRect& box){
            if(box.width<=0||box.height<=0)return;
            box.x+=dx;box.y+=dy;
        };
        for(auto& box:child->items)translate(box);
        if(child->previous_page)translate(*child->previous_page);
        if(child->next_page)translate(*child->next_page);
        child->center=parent_center;
        child->back_button=parent;
        // cTSPieMenu's classic layout includes the 201px surface even when
        // all its pixels are magenta. Its placement clamps the whole window,
        // not each plaque. Legacy edge observation confirms the head remains
        // at its root position while this parent/children composition shifts.
        std::int64_t left=std::int64_t(parent_center[0])-100,
            top=std::int64_t(parent_center[1])-100,right=left+201,bottom=top+201;
        const auto include=[&](const odh::PieMenuRect& box){
            if(box.width<=0||box.height<=0)return;
            left=std::min(left,std::int64_t(box.x));top=std::min(top,std::int64_t(box.y));
            right=std::max(right,std::int64_t(box.x)+box.width);
            bottom=std::max(bottom,std::int64_t(box.y)+box.height);
        };
        include(*child->back_button);
        for(const auto& box:child->items)include(box);
        if(child->previous_page)include(*child->previous_page);
        if(child->next_page)include(*child->next_page);
        left-=4;top-=4;right+=4;bottom+=4;
        if(right-left>width||bottom-top>height)return {};
        const auto shift_x=std::clamp<std::int64_t>(0,-left,std::int64_t(width)-right),
            shift_y=std::clamp<std::int64_t>(0,-top,std::int64_t(height)-bottom);
        const auto shift=[&](odh::PieMenuRect& box){
            if(box.width<=0||box.height<=0)return;
            box.x+=static_cast<int>(shift_x);box.y+=static_cast<int>(shift_y);
        };
        for(auto& box:child->items)shift(box);
        shift(*child->back_button);
        if(child->previous_page)shift(*child->previous_page);
        if(child->next_page)shift(*child->next_page);
        child->center[0]+=static_cast<int>(shift_x);child->center[1]+=static_cast<int>(shift_y);
        result.layout=std::move(*child);
    }
    return result;
}
bool PieMenuPointer::Target::matches(const Target& other) const noexcept{
    return kind==other.kind&&index==other.index&&page==other.page&&
        rect.x==other.rect.x&&rect.y==other.rect.y&&rect.width==other.rect.width&&rect.height==other.rect.height;
}
std::optional<PieMenuPointer::Target> PieMenuPointer::hit(const odh::PieMenuLayout& layout,int x,int y){
    for(std::size_t i=layout.page_first;i<layout.items.size()&&i-layout.page_first<layout.page_size;++i){
        if(layout.items[i].contains(x,y))return Target{Kind::action,i,layout.page_first,layout.items[i]};
    }
    for(const auto& [kind,box]:{std::pair{Kind::back,&layout.back_button},
            std::pair{Kind::previous,&layout.previous_page},std::pair{Kind::next,&layout.next_page}})
        if(*box&&(*box)->contains(x,y))return Target{kind,0,layout.page_first,**box};
    return {};
}
PieMenuPointerResult PieMenuPointer::process(const odh::PieMenuLayout& layout,
        const std::vector<WindowPointerEvent>& events,bool cancelled,std::size_t& selected){
    PieMenuPointerResult result;
    const auto before=selected;
    const auto was_pressed=pressed_;
    const auto previous_hover=hover_target();
    // A page or submenu can replace the rectangles without another pointer
    // event. Never retain an option from the previous visible menu.
    if(hover_){
        const auto point=*hover_target();
        const auto current=hit(layout,point[0],point[1]);
        if(!current||!hover_->matches(*current))hover_.reset();
    }
    if(cancelled){
        reset();
        result.consumed=true;
    }else for(const auto& event:events){
        const auto target=hit(layout,event.x,event.y);
        hover_=target&&target->kind==Kind::action?target:std::nullopt;
        if(target&&target->kind==Kind::action)selected=target->index;
        if(event.kind==WindowPointerKind::press){
            capture_=target;
            result.consumed|=capture_.has_value();
        }
        const bool same=capture_&&target&&capture_->matches(*target);
        pressed_=same&&capture_->kind==Kind::action;
        if(event.kind==WindowPointerKind::release){
            result.consumed|=capture_.has_value();
            capture_.reset();pressed_=false;
            if(same){
                result.activated=std::array<std::int32_t,2>{event.x,event.y};
                // The activated action can replace or dismiss the menu. Later
                // events in this poll cannot be interpreted against this layout.
                break;
            }
        }
    }
    result.redraw=selected!=before||pressed_!=was_pressed||hover_target()!=previous_hover;
    return result;
}
std::optional<std::array<std::int32_t,2>> PieMenuPointer::hover_target() const noexcept{
    if(!hover_)return {};
    return std::array<std::int32_t,2>{hover_->rect.x+hover_->rect.width/2,
        hover_->rect.y+hover_->rect.height/2};
}
void draw_linux_pie_menu(odh::RasterFrame& frame,const odh::PieMenuLayout& layout,
        const std::vector<PieMenuCaption>& labels,std::size_t selected,const std::optional<PieMenuCaption>& parent,
        const odh::BitmapImage* background,bool pressed,PieMenuBackground kind,bool highlight_selected){
    if(frame.pixels.size()!=std::uint64_t(frame.width)*frame.height||labels.size()!=layout.items.size()||
       selected>=labels.size()||parent.has_value()!=layout.back_button.has_value())
        throw odh::FormatError("pie menu presentation does not match layout");
    if(background){
        const int size=kind==PieMenuBackground::live_face?201:195;
        if(background->width!=static_cast<std::uint32_t>(size)||background->height!=static_cast<std::uint32_t>(size))
            throw odh::FormatError("invalid pie artwork for selected menu");
        odh::blit_color_keyed_bitmap(frame,*background,layout.center[0]-size/2,layout.center[1]-size/2,{255,0,255});
    }
    if(parent)draw_caption(frame,*layout.back_button,*parent,down);
    if(layout.previous_page)draw_caption(frame,*layout.previous_page,{"‹",1},regular);
    if(layout.next_page)draw_caption(frame,*layout.next_page,{"›",1},regular);
    for(std::size_t i=layout.page_first;i<labels.size()&&i-layout.page_first<layout.page_size;++i)
        draw_caption(frame,layout.items[i],labels[i],highlight_selected&&i==selected?(pressed?down:highlight):regular);
}
}
