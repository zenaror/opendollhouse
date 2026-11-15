/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "app/ui/OriginalPresentation.h"
#include "app/ui/OriginalTextEncoding.h"
#include "app/Input.h"
#include "ui_hover.hpp"
#include "original_tooltip_view.hpp"
#include <chrono>
namespace simapp {
// One instance per presentation context; callers supply the authentic name,
// stable target identity and current modal/focus/selection facts.
class OriginalHover {
    odh_host::UiHover Hover{odh_host::UiHoverConfig{500}};
    // Only the opaque tooltip is retained. Copy it onto the current scene each
    // frame so animation underneath never becomes part of the saved image.
    Canvas image_;
    std::string text_;
    int width_=0,height_=0;
    uint32_t foreground_=0;
    uint8_t language_=0;
public:
    void Clear(){Hover.clear();text_.clear();}
    std::optional<odh_host::UiHoverTarget> Resolve(const AppInput& in,const std::string& name,
              uint64_t target,uint64_t context,uint64_t selection,Rect bounds,bool modal=false,bool focused=true,bool enabled=true){
        odh_host::UiHoverInput input;
        input.pointer=std::array<int32_t,2>{in.TouchX,in.TouchY};
        input.context=context;input.selection=selection;input.modal=modal;input.focused=focused;input.enabled=enabled;
        input.button_down=in.Touching;input.focused=focused&&!in.PointerCancelled;input.interacted=in.TouchDown||in.TouchUp||in.MouseRightDown||in.WheelY;
        std::optional<odh_host::UiHoverTarget> candidate;
        if(!name.empty())candidate=odh_host::UiHoverTarget{target,{bounds.X,bounds.Y,bounds.W,bounds.H},name};
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        return Hover.update(uint64_t(now),candidate,input);

    }
    void Draw(Canvas& canvas,const AppInput& in,const std::string& name,
              uint64_t target,uint64_t context,uint64_t selection,Rect bounds,bool modal=false,bool focused=true,bool enabled=true,uint32_t foreground=RGBA(0,0,0)){
        const auto visible=Resolve(in,name,target,context,selection,bounds,modal,focused,enabled);
        if(!visible)return;
        const auto language=original::TextLanguage();
        const std::array<int,2> pointer{in.TouchX,in.TouchY};
        if(text_!=visible->text || language_!=language || width_!=canvas.Width || height_!=canvas.Height ||
            foreground_!=foreground){
            const auto bounds=odh_host::original_tooltip_layout(canvas.Width,canvas.Height,visible->text,language,pointer);
            if(bounds){
                image_.Resize(bounds->width,bounds->height);
                odh::RasterFrame tile{};
                tile.width=image_.Width;tile.height=image_.Height;tile.pixels.resize(image_.Px.size());
                // With the viewport equal to the measured box its origin is (0,0).
                odh_host::draw_original_tooltip(tile,visible->text,language,{0,0},1,
                    {uint8_t(foreground),uint8_t(foreground>>8),uint8_t(foreground>>16),uint8_t(foreground>>24)});
                original::Copy(tile,image_);
            }else image_=Canvas();
            width_=canvas.Width;height_=canvas.Height;language_=language;
            foreground_=foreground;text_=visible->text;
        }
        if(!image_.Px.empty()){
            const auto box=odh_host::position_original_tooltip(width_,height_,image_.Width,image_.Height,pointer);
            canvas.Blit(image_.Px.data(),image_.Width,image_.Height,box.x,box.y,false);
        }
    }

};
}
#endif
