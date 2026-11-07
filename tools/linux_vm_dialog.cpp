#include "linux_vm_dialog.hpp"

#include "linux_text_renderer.hpp"
#include "odh/ui_bitmap.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace odh_host {
namespace {
using Color=std::array<std::uint8_t,4>;

void fill(odh::RasterFrame& frame,std::int32_t x,std::int32_t y,
          std::int32_t width,std::int32_t height,Color color) {
    const auto left=std::clamp<std::int64_t>(x,0,frame.width);
    const auto top=std::clamp<std::int64_t>(y,0,frame.height);
    const auto right=std::clamp<std::int64_t>(static_cast<std::int64_t>(x)+width,0,frame.width);
    const auto bottom=std::clamp<std::int64_t>(static_cast<std::int64_t>(y)+height,0,frame.height);
    for(auto row=top;row<bottom;++row)for(auto column=left;column<right;++column)
        frame.pixels[static_cast<std::size_t>(row)*frame.width+static_cast<std::size_t>(column)]=color;
}

std::int32_t input_height(const VmMessageDialogLayout& layout){return layout.height<180?22:30;}
std::int32_t input_y(const VmMessageDialogLayout& layout){return layout.ok_y-input_height(layout)-8;}
void draw_scroll_status(odh::RasterFrame& frame,const VmMessageDialogLayout& layout,
        const TextOverlayResult& page,std::uint8_t language,std::uint8_t type) {
    if(page.line_count<=page.visible_lines)return;
    TextOverlayStyle style;style.x=layout.x+18;style.height=18;style.font_size_px=11;style.padding=1;
    style.foreground={246,244,231,255};style.background={68,72,75,0};
    if(type==3) {
        style.y=layout.ok_y+6;style.width=layout.ok_x-style.x-4;
    }else {style.y=layout.ok_y-24;style.width=layout.width-36;}
    if(style.width<40)return;
    const auto text=std::to_string(page.first_line+1)+"-"+
        std::to_string(page.first_line+page.visible_lines)+"/"+std::to_string(page.line_count)+"  PgUp/PgDn";
    (void)draw_text_overlay(frame,text,language,style);
}
}

namespace {
constexpr int choice_row_height=22;
constexpr int content_margin=18;

struct IllustratedTextFlow {
    std::size_t prefix_bytes=0,prefix_draw_bytes=0;
    int prefix_height=0,tail_y=0,tail_height=0,total_height=0;
};
IllustratedTextFlow illustrated_text_flow(std::string_view text,std::uint8_t language,
        int font,int narrow_width,int full_width,int overlap){
    struct Cache {
        std::string text;std::uint8_t language;int font,narrow,full,overlap;
        IllustratedTextFlow flow;
    };
    static thread_local std::optional<Cache> cache;
    if(cache&&cache->text==text&&cache->language==language&&cache->font==font&&
       cache->narrow==narrow_width&&cache->full==full_width&&cache->overlap==overlap)return cache->flow;
    IllustratedTextFlow flow;
    if(text.empty())return flow;
    const auto measure=[&](std::string_view part,int width){return part.empty()?0:
        measure_text_overlay(part,language,font,width).height;};
    if(overlap>0){
        // Keep the same shaped font metrics for both regions. A row beginning
        // beside the image stays narrow until that complete row has finished.
        const int line_height=std::max(1,measure_text_overlay("Ag",language,font).height);
        const int top_height=((overlap+line_height-1)/line_height)*line_height;
        std::vector<std::size_t> boundaries{0};
        for(std::size_t at=1;at<text.size();++at)
            if((static_cast<unsigned char>(text[at])&0xc0)!=0x80)boundaries.push_back(at);
        boundaries.push_back(text.size());
        std::size_t low=0,high=boundaries.size()-1;
        while(low<high){const auto middle=low+(high-low+1)/2;
            if(measure(text.substr(0,boundaries[middle]),narrow_width)<=top_height)low=middle;
            else high=middle-1;
        }
        auto cut=boundaries[low];
        const auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
        if(cut&&cut<text.size()&&!space(text[cut])&&!space(text[cut-1])){
            const auto previous=text.find_last_of(" \t\r\n",cut-1);
            const auto word_begin=previous==std::string_view::npos?0:previous+1;
            const auto next=text.find_first_of(" \t\r\n",cut);
            const auto word_end=next==std::string_view::npos?text.size():next;
            // A normal word moves intact to the wide region; genuinely long
            // words retain the renderer's character-wrap behavior, UTF8-safe.
            if(measure_text_overlay(text.substr(word_begin,word_end-word_begin),language,font).width<=narrow_width)
                cut=word_begin;
        }
        while(cut<text.size()&&(text[cut]==' '||text[cut]=='\t'))++cut;
        if(cut<text.size()&&text[cut]=='\r')++cut;
        if(cut<text.size()&&text[cut]=='\n')++cut;
        flow.prefix_bytes=cut;flow.prefix_draw_bytes=cut;
        if(cut<text.size()){
            while(flow.prefix_draw_bytes&&(text[flow.prefix_draw_bytes-1]==' '||text[flow.prefix_draw_bytes-1]=='\t'))--flow.prefix_draw_bytes;
            if(flow.prefix_draw_bytes&&text[flow.prefix_draw_bytes-1]=='\n')--flow.prefix_draw_bytes;
            if(flow.prefix_draw_bytes&&text[flow.prefix_draw_bytes-1]=='\r')--flow.prefix_draw_bytes;
        }
        flow.prefix_height=measure(text.substr(0,flow.prefix_draw_bytes),narrow_width);
    }
    if(flow.prefix_bytes<text.size()){
        flow.tail_y=flow.prefix_bytes?std::max(flow.prefix_height,overlap+2):std::max(0,overlap+2);
        flow.tail_height=measure(text.substr(flow.prefix_bytes),full_width);
    }
    flow.total_height=flow.tail_height?flow.tail_y+flow.tail_height:flow.prefix_height;
    cache=Cache{std::string(text),language,font,narrow_width,full_width,overlap,flow};return flow;
}

odh::UiBitmapRect illustration_extent(int fw,int fh,const odh::DialogState& state,const VmDialogAppearance& appearance){
    const auto* image=appearance.illustration;
    if(!image||!image->width||!image->height)return {};
    const bool compact=fh<240;
    const int rows=std::min<std::size_t>(4,state.prompt.choices.size());
    const auto max_width=std::min(520,fw-32),max_height=std::min(312,fh-32);
    constexpr int text_column_min=72,image_gap=12;
    const auto w=max_width-2*content_margin-image_gap-text_column_min;
    const auto h=max_height-(compact?8:content_margin)-33-(compact?8:12)-8-
        (state.instruction.operands[5]==3?36:0)-(rows?rows*choice_row_height+8:0);
    if(w<1||h<1)return {};
    const double scale=std::min({1.0,double(w)/image->width,double(h)/image->height});
    return {0,0,
        std::max(1,int(image->width*scale)),std::max(1,int(image->height*scale))};
}
VmMessageDialogLayout content_layout(int fw,int fh,const odh::DialogState& state,
        std::uint8_t language,const VmDialogAppearance& appearance){
    const bool compact=fh<240;
    const int fontsize=compact?11:15;
    const auto& title=state.prompt.title;const auto& message=state.prompt.message;
    if(title.size()>4096||message.size()>4096)throw std::invalid_argument("dialog text outside layout budget");
    const int rows=std::min<std::size_t>(4,state.prompt.choices.size());
    const int count=static_cast<int>(vm_dialog_button_count(state.instruction.operands[5]));
    const int button_w=count>1?104:92;
    const int max_w=std::min(520,fw-32);
    const int controls_min_w=std::max(280,count*(button_w+16)+24);
    const auto title_w=title.empty()?0:measure_text_overlay(title,language,fontsize).width;
    const auto text_w=message.empty()?0:measure_text_overlay(message,language,fontsize).width;
    auto illustration=illustration_extent(fw,fh,state,appearance);
    const int image_w=illustration.width?illustration.width+12:0;
    const int min_w=std::min(max_w,std::max(controls_min_w,image_w+2*content_margin+76));
    const int pref=std::max(title_w,std::min(480,text_w)*2/3+image_w)+2*content_margin;
    const int width=std::clamp(pref,min_w,max_w);
    const int body_w=std::max(20,width-2*content_margin-image_w-4);
    int body_h=message.empty()?fontsize:measure_text_overlay(message,language,fontsize,body_w).height;
    if(illustration.width&&!message.empty()){
        const int message_top=title.empty()?(compact?6:16):(compact?26:40);
        const int image_bottom=(compact?8:content_margin)+illustration.height;
        body_h=illustrated_text_flow(message,language,fontsize,width-2*content_margin-image_w-2,
            width-2*content_margin-2,image_bottom-message_top).total_height;
    }
    const int image_h=illustration.height;
    const bool input=state.instruction.operands[5]==3;
    const int title_h=title.empty()?0:(compact?22:28);
    const int wanted=2*content_margin+33+10+std::max(title_h+body_h+8,image_h)
        +(input?36:0)+(rows?rows*choice_row_height+8:0);
    const int height=std::clamp(wanted,std::min(126,fh-32),std::min(312,fh-32));
    const int x=(fw-width)/2,y=(fh-height)/2;
    const int bw=std::min(button_w,(width-24)/count-8);
    if(illustration.width){illustration.x=x+content_margin;illustration.y=y+(compact?8:content_margin);}
    return {x,y,width,height,x+(width-bw)/2,y+height-(compact?8:12)-33,bw,33,illustration};
}
}

std::optional<VmMessageDialogLayout> vm_message_dialog_layout(
        std::uint32_t fw,std::uint32_t fh,const odh::DialogState& state,
        std::uint8_t language,const VmDialogAppearance& appearance){
    if(fw<240||fh<160||fw>8192||fh>8192)return {};
    return content_layout(static_cast<int>(fw),static_cast<int>(fh),state,language,appearance);
}

bool vm_message_dialog_ok_hit(const VmMessageDialogLayout& layout,
        std::int32_t x,std::int32_t y) noexcept {
    return x>=layout.ok_x&&y>=layout.ok_y&&x<layout.ok_x+layout.ok_width&&
        y<layout.ok_y+layout.ok_height;
}

std::string expand_vm_dialog_local_references(std::string_view input,
        const std::vector<std::int16_t>& locals) {
    if(input.size()>4096)throw std::invalid_argument("VM dialog text exceeds its byte budget");
    std::string output;
    output.reserve(input.size());
    for(std::size_t cursor=0;cursor<input.size();){
        if(input[cursor]!='$'){
            output.push_back(input[cursor++]);
            continue;
        }
        std::size_t marker=cursor;
        if(input.compare(cursor,8,"$$Local:")==0){
            output.push_back('$');
            ++marker;
        }
        if(input.compare(marker,7,"$Local:")!=0)
            throw std::invalid_argument("VM dialog contains an unsupported string marker");
        const auto first_digit=marker+7;
        auto end=first_digit;
        while(end<input.size()&&input[end]>='0'&&input[end]<='9')++end;
        if(end==first_digit)throw std::invalid_argument("VM dialog Local reference has no index");
        std::size_t index=0;
        const auto parsed=std::from_chars(input.data()+first_digit,input.data()+end,index);
        if(parsed.ec!=std::errc{}||parsed.ptr!=input.data()+end||index>=locals.size())
            throw std::invalid_argument("VM dialog Local reference is outside the saved frame");
        output+=std::to_string(locals[index]);
        cursor=end;
        if(output.size()>4096)throw std::invalid_argument("expanded VM dialog text exceeds its byte budget");
    }
    return output;
}

static void draw_original_dialog_panel(odh::RasterFrame& frame,const VmMessageDialogLayout& l,
        const odh::BitmapImage& tiles){
    if(tiles.width!=36||tiles.height!=36)
        throw odh::FormatError("original popup tile atlas must be 36 by 36");
    constexpr int side=12;
    const int xs[]={l.x,l.x+side,l.x+l.width-side,l.x+l.width};
    const int ys[]={l.y,l.y+side,l.y+l.height-side,l.y+l.height};
    for(int row=0;row<3;++row)for(int column=0;column<3;++column)
        for(int y=ys[row];y<ys[row+1];y+=side)for(int x=xs[column];x<xs[column+1];x+=side){
            const int w=std::min(side,xs[column+1]-x),h=std::min(side,ys[row+1]-y);
            odh::blit_color_keyed_bitmap(frame,tiles,{x,y,w,h},
                odh::UiBitmapRect{column*side,row*side,w,h},{255,0,255});
        }
}
static TextOverlayResult draw_dialog_body(odh::RasterFrame& frame,std::string_view title,
        std::string_view message,std::uint8_t language,
        std::size_t first_line,bool text_input,std::optional<int> body_bottom_limit,
        const VmDialogAppearance& appearance,const std::optional<VmMessageDialogLayout>& layout) {
    if(!layout||frame.pixels.size()!=static_cast<std::size_t>(frame.width)*frame.height||
       title.size()>4096||message.empty()||message.size()>4096)
        throw std::invalid_argument("VM message dialog input is outside its bounded profile");

    if(appearance.panel_tiles)draw_original_dialog_panel(frame,*layout,*appearance.panel_tiles);
    else {
    fill(frame,layout->x,layout->y,layout->width,layout->height,{38,43,48,255});
    fill(frame,layout->x,layout->y,layout->width,4,{82,186,170,255});
    fill(frame,layout->x+1,layout->y+1,layout->width-2,layout->height-2,{68,72,75,255});
    }

    TextOverlayStyle style;
    style.x=layout->x+18;style.width=layout->width-36;style.padding=2;
    style.foreground={246,244,231,255};style.background={68,72,75,0};
    const bool compact_input=frame.height<240;
    const auto icon=layout->illustration;
    if(icon.width)odh::blit_color_keyed_bitmap(frame,*appearance.illustration,icon,
        std::nullopt,{255,0,255});
    if(icon.width){style.x+=icon.width+12;style.width-=icon.width+12;}
    if(!title.empty()){
        style.y=layout->y+(compact_input?6:12);style.height=compact_input?20:26;
        style.font_size_px=compact_input?11:15;
        style.centered=appearance.center_title;
        (void)draw_text_overlay(frame,title,language,style);
    }
    style.y=layout->y+(title.empty()?(compact_input?6:16):(compact_input?26:40));
    const auto default_bottom=text_input?input_y(*layout)-4:layout->ok_y-4;
    style.height=body_bottom_limit.value_or(default_bottom)-style.y;
    style.font_size_px=compact_input?11:15;style.padding=1;
    if(style.height<=style.padding*2)throw std::invalid_argument("VM dialog has no room for message text");
    if(icon.width){
        const auto flow=illustrated_text_flow(message,language,style.font_size_px,
            style.width-2*style.padding,layout->width-2*content_margin-2*style.padding,
            icon.y+icon.height-style.y);
        if(flow.total_height+2*style.padding<=style.height){
            TextOverlayResult complete{};complete.line_count=0;complete.visible_lines=0;
            const auto append=[&](const TextOverlayResult& part){
                complete.glyphs+=part.glyphs;complete.missing_glyphs+=part.missing_glyphs;
                complete.pixels_changed+=part.pixels_changed;complete.line_count+=part.line_count;
                complete.visible_lines+=part.visible_lines;
            };
            style.wrap_lines=true;style.first_line=0;style.centered=false;
            if(flow.prefix_draw_bytes){auto top=style;top.height=flow.prefix_height+2*style.padding;
                append(draw_text_overlay(frame,message.substr(0,flow.prefix_draw_bytes),language,top));}
            if(flow.prefix_bytes<message.size()){auto tail=style;
                tail.x=layout->x+content_margin;tail.width=layout->width-2*content_margin;
                tail.y+=flow.tail_y;tail.height=flow.tail_height+2*style.padding;
                append(draw_text_overlay(frame,message.substr(flow.prefix_bytes),language,tail));}
            complete.first_line=0;complete.max_first_line=0;complete.has_more=false;return complete;
        }
    }
    if(!text_input&&!body_bottom_limit&&style.height>=48&&
       measure_text_overlay(message,language,style.font_size_px,style.width-2*style.padding).height>
            style.height-2*style.padding)style.height-=20;
    style.wrap_lines=true;style.first_line=first_line;
    // Genuine overflow/compact input retains the existing complete, scrollable
    // text column instead of dropping words to force the illustrated layout.
    style.centered=!icon.width;
    const auto page=draw_text_overlay(frame,message,language,style);

    return page;
}

namespace {
odh::UiBitmapRect response_box(const VmMessageDialogLayout& l,std::size_t count,std::size_t index){
    const int width=l.ok_width;
    const int gap=std::min(width/2,(l.width-static_cast<int>(count)*width)/static_cast<int>(count+1));
    const int total=count*width+(count-1)*gap;
    return {l.x+(l.width-total)/2+static_cast<int>(index)*(width+gap),l.ok_y,width,l.ok_height};
}
void draw_response(odh::RasterFrame& frame,odh::UiBitmapRect box,std::string_view label,
        std::uint8_t language,bool focused,const VmDialogAppearance& appearance){
    if(label.empty()||label.size()>4096)throw std::invalid_argument("VM dialog response label outside bounds");
    if(const auto* strip=appearance.button_strip){
        if(strip->width!=260||strip->height!=33)throw odh::FormatError("invalid original WinBtn strip");
        // Keyboard selection does not depress a momentary button. Its resting
        // state is frame zero (classic cTSWinBtn state calculation 005e3a26).
        odh::blit_color_keyed_bitmap(frame,*strip,box,odh::UiBitmapRect{0,0,65,33},{255,0,255});
    }else{
        fill(frame,box.x,box.y,box.width,box.height,focused?Color{137,107,59,255}:Color{102,76,43,255});
    }
    TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
    style.font_size_px=14;style.padding=2;style.background={0,0,0,0};
    style.centered=true;
    style.foreground=appearance.button_strip&&focused?Color{255,255,0,255}:Color{255,250,231,255};
    (void)draw_text_overlay(frame,label,language,style);
}
}

TextOverlayResult draw_vm_message_dialog(odh::RasterFrame& frame,std::string_view title,
        std::string_view message,std::string_view ok_label,std::uint8_t language,std::size_t first_line,
        bool text_input,std::optional<int> body_bottom_limit,const VmDialogAppearance& appearance){
    odh::DialogState state;state.prompt.title=title;state.prompt.message=message;
    state.instruction.operands[5]=text_input?3:0;
    const auto bounds=vm_message_dialog_layout(frame.width,frame.height,state,language,appearance);
    const auto page=draw_dialog_body(frame,title,message,language,first_line,text_input,body_bottom_limit,appearance,bounds);
    const auto layout=*bounds;
    draw_response(frame,response_box(layout,1,0),ok_label,language,false,appearance);
    if(!body_bottom_limit)draw_scroll_status(frame,layout,page,language,text_input?3:0);
    return page;
}

std::string expand_vm_dialog_text(std::string_view input,const VmDialogSubstitution& substitute){return odh::expand_dialog_text(input,substitute);}
namespace {
int choice_rows(const VmMessageDialogLayout& layout) {
    return std::max(1,(layout.ok_y-layout.y-90)/choice_row_height);
}
int choice_top(const VmMessageDialogLayout& layout) {
    return layout.ok_y-4-choice_rows(layout)*choice_row_height;
}
}
std::size_t vm_dialog_choice_index(const odh::DialogState& state,int delta) {
    const auto& choices=state.prompt.choices;
    if(choices.empty())return 0;
    auto current=std::find_if(choices.begin(),choices.end(),[&](const auto& c){
        return std::to_string(c.value)==state.input;
    });
    const auto count=static_cast<int>(choices.size());
    const int index=current==choices.end()?0:static_cast<int>(current-choices.begin());
    return static_cast<std::size_t>((index+delta%count+count)%count);
}
std::optional<std::size_t> vm_dialog_choice_hit(const VmMessageDialogLayout& layout,
        const odh::DialogState& state,int x,int y) {
    const int top=choice_top(layout);
    if(x<layout.x+18||x>=layout.x+layout.width-18||y<top||y>=layout.ok_y-4)return {};
    const auto rows=static_cast<std::size_t>(choice_rows(layout));
    const auto index=vm_dialog_choice_index(state)/rows*rows+(y-top)/choice_row_height;
    if(index>=state.prompt.choices.size())return {};
    return index;
}
std::size_t vm_dialog_button_count(std::uint8_t type) noexcept {return type==1?2U:type==2?3U:1U;}
std::optional<std::uint8_t> vm_dialog_button_hit(const VmMessageDialogLayout& layout,std::uint8_t type,std::int32_t x,std::int32_t y) noexcept {
    const auto count=vm_dialog_button_count(type);
    for(std::size_t n=0;n<count;++n)
        if(response_box(layout,count,n).contains(x,y))return static_cast<std::uint8_t>(n);
    return {};
}
TextOverlayResult draw_vm_dialog(odh::RasterFrame& frame,const odh::DialogState& state,
        std::uint8_t language,std::size_t first_line,const VmDialogAppearance& appearance){
    const auto type=state.instruction.operands[5];
    const auto default_label=[](std::string_view supplied,std::string_view fallback){
        return supplied.empty()?fallback:supplied;
    };
    const auto cancel=default_label(appearance.cancel,language==14?"Cancelar":"Cancel");
    std::string_view first=state.prompt.yes;
    if(first.empty())first=type==1||type==2?default_label(appearance.yes,"Yes"):default_label(appearance.ok,"OK");
    if(odh::dialog_selector_type(type)&&state.prompt.choices.empty())
        first=state.prompt.cancel.empty()?cancel:std::string_view(state.prompt.cancel);
    const auto layout=vm_message_dialog_layout(frame.width,frame.height,state,language,appearance);
    const auto has_rows=layout&&odh::dialog_selector_type(type)&&!state.prompt.choices.empty();
    const auto page=draw_dialog_body(frame,state.prompt.title,state.prompt.message.empty()?" ":state.prompt.message,
        language,first_line,type==3,has_rows?std::optional<int>(choice_top(*layout)):std::nullopt,appearance,layout);
    if(!layout)return page;
    const auto count=vm_dialog_button_count(type);
    const std::array<std::string_view,3> labels{{first,
        state.prompt.no.empty()?default_label(appearance.no,"No"):std::string_view(state.prompt.no),
        state.prompt.cancel.empty()?cancel:std::string_view(state.prompt.cancel)}};
    for(std::size_t n=0;n<count;++n)
        draw_response(frame,response_box(*layout,count,n),labels[n],language,n==state.selected_button,appearance);
    TextOverlayStyle style;style.font_size_px=14;style.padding=2;style.foreground={255,250,231,255};
    style.background={0,0,0,0};
    if(odh::dialog_selector_type(type)&&!state.prompt.choices.empty()){
        const int rows=choice_rows(*layout);
        const auto selected=vm_dialog_choice_index(state);
        const auto start=selected/rows*rows;
        const int top=choice_top(*layout);
        fill(frame,layout->x+18,top,layout->width-36,rows*choice_row_height,{46,52,57,255});
        style.x=layout->x+18;style.width=layout->width-36;style.height=choice_row_height;
        style.font_size_px=13;style.wrap_lines=false;
        for(int row=0;row<rows;++row){
            const auto index=start+row;
            if(index>=state.prompt.choices.size())break;
            style.y=top+row*choice_row_height;
            if(index==selected)fill(frame,style.x,style.y,style.width,style.height,{102,76,43,255});
            (void)draw_text_overlay(frame,state.prompt.choices[index].label,language,style);
        }
        style.y=layout->ok_y;style.width=layout->ok_x-style.x-4;
        const auto position=std::to_string(selected+1)+" / "+std::to_string(state.prompt.choices.size());
        (void)draw_text_overlay(frame,position+"   Tab   Esc",language,style);
        return page;
    }
    if(type==3){
        const auto y=input_y(*layout);const auto height=input_height(*layout);
        fill(frame,layout->x+18,y,layout->width-36,height,{27,31,35,255});
        style.x=layout->x+20;style.y=y;style.width=layout->width-40;style.height=height;
        (void)draw_text_overlay(frame,state.input+"|",language,style);
    }
    draw_scroll_status(frame,*layout,page,language,type);
    return page;
}

} // namespace odh_host
