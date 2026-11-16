/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims front end — see FrontUI.h. TS1(sims).
#include "app/front/FrontUI.h"
#include <algorithm>
#ifndef __3DS__
#include "original_win_button.hpp"
#include "app/ui/OriginalDialogArt.h"
#include "app/ui/OriginalTextEncoding.h"
#endif

namespace simapp { namespace front {

void NineSlice(Canvas& dst, const Canvas& tile, const Rect& r, int b) {
    if (tile.Width <= 0 || r.W <= 0 || r.H <= 0) return;
    int tw = tile.Width, th = tile.Height;
    b = std::min(b, std::min(tw / 2, th / 2));
    b = std::min(b, std::min(r.W / 2, r.H / 2));
    for (int y = 0; y < r.H; y++) {
        int dy = r.Y + y;
        if (dy < 0 || dy >= dst.Height) continue;
        int sy;
        if (y < b) sy = y;
        else if (y >= r.H - b) sy = th - (r.H - y);
        else sy = b + (int)((int64_t)(y - b) * (th - 2 * b) / std::max(1, r.H - 2 * b));
        const uint32_t* row = &tile.Px[(size_t)sy * tw];
        uint32_t* out = &dst.Px[(size_t)dy * dst.Width];
        for (int x = 0; x < r.W; x++) {
            int dx = r.X + x;
            if (dx < 0 || dx >= dst.Width) continue;
            int sx;
            if (x < b) sx = x;
            else if (x >= r.W - b) sx = tw - (r.W - x);
            else sx = b + (int)((int64_t)(x - b) * (tw - 2 * b) / std::max(1, r.W - 2 * b));
            Canvas::BlendPx(out[dx], row[sx]);
        }
    }
}

FrontArt& FrontArt::Get() {
    static FrontArt a;
    return a;
}

void FrontArt::Load(const ResGraphics& ui) {
    if (Loaded) return;
    ui.Load("Nbhd/NScreenDialogTiler.TGA", DialogTiler, false);
    ui.Load("Nbhd/HotspotPopupTiler.TGA", PopupTiler, false);
    ui.Load("Shared/Sys/CheckBtnNew.BMP", CheckBtn);
    ui.Load("CPanel/Buttons/optradio.bmp", RadioBtn);
    ui.Load("CPanel/Backgrounds/PopupInfoTiles.bmp", OriginalDialogTiles);
    Canvas btn;
    if (ui.Load("Shared/Sys/WinBtn.BMP", btn)) {
        NativeWinBtn=btn;
        // scale each of the 4 frames to 0.6 (65x33 -> 39x20)
        int fw = btn.Width / 4, fh = btn.Height;
        int dw = (int)(fw * 0.6f + 0.5f), dh = (int)(fh * 0.6f + 0.5f);
        WinBtn.Resize(dw * 4, dh);
        for (auto& p : WinBtn.Px) p = 0;
        for (int f = 0; f < 4; f++) {
            Canvas fr, sc;
            Crop(btn, fr, f * fw, 0, fw, fh);
            Resample(fr, sc, dw, dh);
            for (int y = 0; y < dh; y++) std::copy(sc.Px.begin() + (size_t)y * dw, sc.Px.begin() + (size_t)(y + 1) * dw, WinBtn.Px.begin() + (size_t)y * WinBtn.Width + f * dw);
        }
    }
    Loaded = true;
}

// ---------------------------------------------------------------- TextButton

bool TextButton::Update(const AppInput& in) {
    if (Disabled) { _Down = false; return false; }
    bool inside = R.Contains(in.TouchX, in.TouchY);
    if(in.DesktopPointer)_Over=inside;
    if (in.TouchDown && inside) { _Down = true; _Over = true; }
    if (_Down && in.Touching) _Over = inside;
    if (_Down && in.TouchUp) { _Down = false; _Over = false; return inside; }
    if (!in.Touching && !in.TouchUp) _Down = false;
    return false;
}

void TextButton::Draw(Canvas& c) const {
    auto& art = FrontArt::Get();
    const bool desktop=c.Width>=640;
    const auto& buttonArt=desktop?art.NativeWinBtn:art.WinBtn;
    int frame = Disabled ? 3 : (Selected ? 1 : ((_Down && _Over) ? (desktop?1:2) : (desktop && _Over ? 2 : 0)));
    if (buttonArt.Width > 0) {
        // 3-slice (C# UIButton.Draw with Width set: left third, stretched centre, right third)
        int fw = buttonArt.Width / 4, fh = buttonArt.Height, third = fw / 3;
        Canvas frameImg;
        Crop(buttonArt, frameImg, frame * fw, 0, fw, fh);
        int y0 = R.Y + (R.H - fh) / 2;
        for (int y = 0; y < fh; y++) {
            int dy = y0 + y;
            if (dy < 0 || dy >= c.Height) continue;
            for (int x = 0; x < R.W; x++) {
                int sx;
#ifndef __3DS__
                if(desktop && buttonArt.Width==260 && fh==33 && R.W>=65)
                    sx=odh_host::original_win_button_source_x(x,R.W);
                else
#endif
                if (x < third) sx = x;
                else if (x >= R.W - third) sx = fw - (R.W - x);
                else sx = third + (x - third) % std::max(1, fw - 2 * third);
                int dx = R.X + x;
                if (dx < 0 || dx >= c.Width) continue;
                Canvas::BlendPx(c.Px[(size_t)dy * c.Width + dx], frameImg.Px[(size_t)y * fw + std::clamp(sx,0,fw-1)]);
            }
        }
    } else {
        c.FillRoundRect(R, 6, Disabled ? RGBA(40, 50, 80) : RGBA(30, 80, 150));
        c.StrokeRoundRect(R.X, R.Y, R.W, R.H, 6, RGBA(120, 170, 230));
    }
    uint32_t tc = Disabled ? RGBA(120, 130, 160) : (frame == 2 ? RGBA(0, 255, 255) : RGBA(255, 255, 255));
    const auto size=TS1Size::S10;
    if(desktop && (Selected || _Over))tc=RGBA(255,255,0);
#ifndef __3DS__
    if(desktop){
        auto frame=original::Raster(c);odh_host::TextOverlayStyle style;
        style.x=R.X;style.y=R.Y;style.width=R.W;style.height=R.H;
        style.padding=2;style.font_size_px=15;style.centered=true;
        style.foreground={uint8_t(tc),uint8_t(tc>>8),uint8_t(tc>>16),uint8_t(tc>>24)};
        style.background={0,0,0,0};
        (void)odh_host::draw_text_overlay(frame,original::UIString(Label),original::TextLanguage(),style);
        original::Copy(frame,c);return;
    }
#endif
    TextCentered(c, size, R, Fit(size, Label, R.W - 10), tc, RGBA(0, 0, 40, 200));
}

void DrawOriginalDialogPanel(Canvas& dst, const Rect& r) {
    const auto& tile=FrontArt::Get().OriginalDialogTiles;
    if(tile.Width!=36 || tile.Height!=36 || r.W<24 || r.H<24)return;
    const int xs[]={r.X,r.X+12,r.Right()-12,r.Right()};
    const int ys[]={r.Y,r.Y+12,r.Bottom()-12,r.Bottom()};
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)
        for(int y=ys[row];y<ys[row+1];y+=12)for(int x=xs[col];x<xs[col+1];x+=12)
            for(int py=0;py<std::min(12,ys[row+1]-y);++py)
                for(int px=0;px<std::min(12,xs[col+1]-x);++px)
                    dst.Blend(x+px,y+py,tile.Px[size_t(row*12+py)*36+col*12+px]);
}

// ---------------------------------------------------------------- FrontDialog

void FrontDialog::Show(const std::string& title, const std::string& message, const std::vector<std::string>& buttons, std::function<void(int)> onResult) {
    Title = title; Message = message;
    _FirstLine=_MaxFirstLine=0;
    Choices.clear();SelectedChoice=0;
    Buttons.clear();
    for (size_t i = 0; i < buttons.size(); i++) { TextButton b; b.Label = buttons[i]; b.Id = std::to_string(i); Buttons.push_back(b); }
    OnResult = onResult;
    Open = true;
}

void FrontDialog::ShowChoice(const std::string& title,const std::string& message,
        const std::vector<std::string>& choices,const std::vector<std::string>& buttons,
        std::function<void(int,int)> onResult) {
    Show(title,message,buttons,[this,onResult](int result){if(onResult)onResult(result,SelectedChoice);});
    // DVD sampled residential and community lots both start at Residential.
    for(std::size_t i=0;i<choices.size();++i){TextButton b;b.Label=choices[i];Choices.push_back(b);}
}

static const int DLG_W = 280;

bool FrontDialog::Update(const AppInput& in) {
    if (!Open) return false;
    if(in.DesktopPointer||in.PointerCancelled)_Pointer=in.PointerCancelled||in.TouchX<0||in.TouchY<0?std::nullopt:std::optional<std::array<int,2>>{{in.TouchX,in.TouchY}};
    if(in.PointerCancelled){
        AppInput cancelled;cancelled.DesktopPointer=true;cancelled.TouchX=cancelled.TouchY=-1;
        for(auto& b:Buttons)b.Update(cancelled);
        for(auto& b:Choices)b.Update(cancelled);
        return true;
    }
    for(std::size_t i=0;i<Choices.size();++i)if(Choices[i].Update(in))SelectedChoice=int(i);
    if(!Choices.empty()){
        if(in.Down&BTN_UP)SelectedChoice=std::max(0,SelectedChoice-1);
        if(in.Down&BTN_DOWN)SelectedChoice=std::min(int(Choices.size())-1,SelectedChoice+1);
    }
    if(_Desktop && Choices.empty()) {
        _FirstLine=std::clamp(_FirstLine-in.WheelY*3+((in.Down&BTN_DOWN)?1:0)-((in.Down&BTN_UP)?1:0),0,_MaxFirstLine);
    }
    int chosen = -1;
    for (size_t i = 0; i < Buttons.size(); i++) if (Buttons[i].Update(in)) chosen = (int)i;
    if (chosen < 0 && (in.Down & BTN_A) && !Buttons.empty()) chosen = 0;
    if (chosen < 0 && (in.Down & BTN_B) && !Buttons.empty()) chosen = (int)Buttons.size() - 1;
    if (chosen >= 0) {
        Open = false;
        auto cb = OnResult;
        OnResult = nullptr;
        if (cb) cb(chosen);
    }
    return true;
}

bool FrontDialog::Draw(Canvas& c) {
    if (!Open) return false;
    _Desktop=c.Width>=640;
#ifndef __3DS__
    if(_Desktop && !Choices.empty()) {
        // Original DVD zoning dialog: fixed authored PC pixels, one canvas.
        Rect panel((c.Width-368)/2,(c.Height-252)/2,368,252);
        DrawOriginalDialogPanel(c,panel);
        auto frame=original::Raster(c);
        const auto label=[&](const std::string& text,Rect box,bool centered,bool wrap){
            odh_host::TextOverlayStyle style;style.x=box.X;style.y=box.Y;
            style.width=box.W;style.height=box.H;style.font_size_px=15;
            style.padding=0;style.centered=centered;style.wrap_lines=wrap;
            style.background={0,0,0,0};
            (void)odh_host::draw_text_overlay(frame,original::UIString(text),original::TextLanguage(),style);
        };
        label(Title,Rect(panel.X+19,panel.Y+20,330,24),false,false);
        label(Message,Rect(panel.X+19,panel.Y+51,330,75),true,true);
        original::Copy(frame,c);
        const auto& radio=FrontArt::Get().RadioBtn;
        for(std::size_t i=0;i<Choices.size();++i){
            auto& choice=Choices[i];choice.R=Rect(panel.X+124,panel.Y+135+int(i)*25,200,25);
            const bool selected=SelectedChoice==int(i);
            const unsigned state=(choice.Hovered()?2:0)+(selected?1:0);
            if(radio.Width==120&&radio.Height==20){
                Canvas cell;Crop(radio,cell,int(state)*20,0,20,20);
                c.Blit(cell.Px.data(),20,20,choice.R.X+1,choice.R.Y+1,true);
            }
            auto textFrame=original::Raster(c);
            odh_host::TextOverlayStyle style;style.x=choice.R.X+29;style.y=choice.R.Y+2;
            style.width=170;style.height=22;style.font_size_px=15;style.padding=0;
            style.background={0,0,0,0};
            (void)odh_host::draw_text_overlay(textFrame,original::UIString(choice.Label),original::TextLanguage(),style);
            original::Copy(textFrame,c);
        }
        for(std::size_t i=0;i<Buttons.size();++i){
            Buttons[i].R=Rect(panel.X+55+int(i)*149,panel.Y+203,110,33);Buttons[i].Draw(c);
        }
        return true;
    }
    if(_Desktop) {
        odh::DialogState state;
        state.prompt.title=original::UIString(Title);
        state.prompt.message=original::UIString(Message);
        state.instruction.operands[5]=Buttons.size()==1?0:Buttons.size()==2?1:2;
        if(!Buttons.empty())state.prompt.yes=original::UIString(Buttons[0].Label);
        if(Buttons.size()>1)state.prompt.no=original::UIString(Buttons[1].Label);
        if(Buttons.size()>2)state.prompt.cancel=original::UIString(Buttons[2].Label);
        auto appearance=original::DialogAppearance();
        appearance.response_pointer=_Pointer;
        for(size_t i=0;i<Buttons.size();++i)if(Buttons[i].Pressed())appearance.pressed_response=i;
        appearance.ok=state.prompt.yes;
        for(size_t i=0;i<Buttons.size();++i)if(Buttons[i].Pressed())state.selected_button=uint8_t(i);
        const auto layout=odh_host::vm_message_dialog_layout(c.Width,c.Height,state,original::TextLanguage(),appearance);
        if(!layout)return false;
        for(size_t i=0;i<Buttons.size();++i){
            const auto rect=odh_host::vm_dialog_button_rect(*layout,state.instruction.operands[5],i);
            Buttons[i].R=Rect(rect.x,rect.y,rect.width,rect.height);
        }
        const auto result=original::PaintDialog(c,state,original::TextLanguage(),_FirstLine,appearance,*layout);
        _MaxFirstLine=int(result.max_first_line);_FirstLine=std::min(_FirstLine,_MaxFirstLine);
        return true;
    }
#endif
    c.Dim(0, 0, c.Width, c.Height, 120);
    auto lines = Wrap(TS1Size::S10, Message, DLG_W - 24);
    if (lines.size() > 10) lines.resize(10);
    int lh = TextH(TS1Size::S10) + 1;
    int h = 34 + (int)lines.size() * lh + 36;
    Rect p((c.Width - DLG_W) / 2, std::max(2, (c.Height - h) / 2), DLG_W, h);
    auto& art = FrontArt::Get();
    if (art.DialogTiler.Width > 0) {
        NineSlice(c, art.DialogTiler, p, 14);
        NineSlice(c, art.DialogTiler, Rect(p.X + 4, p.Y + 4, p.W - 8, p.H - 8), 14);
    } else c.FillRoundRect(p, 10, RGBA(0, 10, 80, 235));
    c.StrokeRoundRect(p.X + 2, p.Y + 2, p.W - 4, p.H - 4, 10, RGBA(90, 120, 210, 200));
    TextCentered(c, TS1Size::S12, Rect(p.X, p.Y + 7, p.W, 18), Fit(TS1Size::S12, Title, p.W - 16), RGBA(255, 216, 96), RGBA(0, 0, 30, 220));
    int y = p.Y + 30;
    for (auto& l : lines) { Text(c, TS1Size::S10, p.X + 12, y, l, RGBA(255, 255, 255), RGBA(0, 0, 30, 160)); y += lh; }
    int n = (int)Buttons.size();
    int bw = std::min(100, (p.W - 24) / std::max(1, n) - 6);
    int bx = p.X + (p.W - n * (bw + 6) + 6) / 2;
    for (auto& b : Buttons) {
        b.R = Rect(bx, p.Bottom() - 30, bw, 22);
        b.Draw(c);
        bx += bw + 6;
    }
    return true;
}

bool FrontDialog::ButtonPos(int index, int& x, int& y) const {
    if (!Open || index < 0 || index >= (int)Buttons.size()) return false;
    x = Buttons[(size_t)index].R.X + Buttons[(size_t)index].R.W / 2;
    y = Buttons[(size_t)index].R.Y + Buttons[(size_t)index].R.H / 2;
    return true;
}

}}
