/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims app — see LotScreen.h
#include "app/LotScreen.h"
#include "app/LotScenePresentation.h"
#ifndef __3DS__
#include "original_button_state.hpp"
#endif
#include "app/ui/OriginalHotkeys.h"
#ifndef __3DS__
#include "app/ui/OriginalPCUI.h"
#include "app/ui/DesktopLivePanels.h"
#include "app/ui/DesktopExpansionPanels.h"
#include "app/ui/OriginalHover.h"
#include "catalog_info_text.hpp"
#include "original_live_help.hpp"
#include "original_control_panel.hpp"
#include "app/ui/OriginalSelectedCrystal.h"
#include "app/ui/OriginalTextEncoding.h"
#endif
#include <cstring>
#include "files/formats/iff/chunks/STR.h"
#include "app/cas/CASFamilyWriter.h"
#include "app/cas/CASSimPreview.h"
#include "app/front/ResGraphics.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "audio/GameAudio.h"
#include "app/UISounds.h"
#include "app/Font.h"
#include "app/Theme.h"
#include "app/WorldPicking.h"
#include "app/ui/UIGraphics.h"
#include "app/ui/GameText.h"
#include "app/ui/OriginalMoneyHeadline.h"
#include "app/ui/UIModePanel.h"
#include "app/ui/UIUCP.h"
#include "app/ui/UIOptionsPanel.h"
#include "app/ui/CheatState.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/VMArchitecture.h"
#include "simantics/model/VMArchitectureTerrain.h"
#include "app/live/UILiveMode.h"
#include "app/live/UIHeadlineRenderer.h"
#include "app/buy/UIBuyMode.h"
#include "app/buy/UIObjectHolder.h"
#include "app/build/UIBuildMode.h"
#include "content/ContentManager.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "files/formats/iff/chunks/BMP.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "world/World.h"
#include "world/model/Blueprint.h"
#include <algorithm>
#ifndef __3DS__
#include "app/front/GameOptions.h"
#include "app/front/FrontEnd.h"
#include "lot_camera.hpp"
#include "original_cursor_policy.hpp"
#endif
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace simapp {

using namespace FSO::SimAntics;

#ifndef __3DS__
struct LotScreen::DesktopHoverState {
    struct ManualHelp {std::string title,description,identity,context,illustration;};
    std::optional<ManualHelp> manual_help;
    std::string automatic_help_identity,automatic_help_context;
    std::string pressed_automatic_help_identity,pressed_automatic_help_context;
    std::string dismissed_help_identity,dismissed_help_context;
    OriginalHover hover;
    FSO::LotView::Framebuffer object_mask;
    AppInput pointer;
    std::string pressed_control;
    std::optional<std::array<int,2>> press_origin;
    std::string name,identity,context;
    std::string help_title,help_description,help_illustration,help_root;
    std::array<std::uint32_t,2> help_objects{};
    bool help_portrait=false;
    std::map<std::uint32_t,odh_host::CatalogObjectThumbnailResult> help_pictures;
    std::unique_ptr<odh_host::CatalogInfoView> help_view;
    odh_host::CatalogInfoScrollState help_scroll;
    uint64_t selection=0;
    uint32_t foreground=RGBA(0,0,0);
    Rect bounds;
    bool external_modal=false;
};
static uint64_t HoverKey(const std::string& value){
    uint64_t hash=14695981039346656037ULL;
    for(unsigned char c:value){hash^=c;hash*=1099511628211ULL;}
    return hash;
}
#endif
void LotScreen::ObserveDesktopPointer(const AppInput& in){
#ifndef __3DS__
    if(!Desktop||(!in.DesktopPointer&&!in.PointerCancelled))return;
    if(!DesktopHover)DesktopHover.reset(new DesktopHoverState());
    auto& h=*DesktopHover;
    if(in.TouchDown){
        // Preserve what was actually visible before this press hides hover.
        // The ordered release may be delivered by a later App::Frame.
        h.pressed_automatic_help_identity=h.automatic_help_identity;
        h.pressed_automatic_help_context=h.automatic_help_context;
    }
    auto& p=h.pointer;
    p.DesktopPointer=true;p.TouchX=in.TouchX;p.TouchY=in.TouchY;
    p.Touching=in.Touching;p.TouchDown=in.TouchDown;p.TouchUp=in.TouchUp;
    p.MouseRightDown=in.MouseRightDown;p.MouseRightHeld=in.MouseRightHeld;
    p.WheelY=in.WheelY;p.PointerCancelled=in.PointerCancelled;
    if(in.PointerCancelled){p.TouchX=p.TouchY=-1;h.pressed_control.clear();h.press_origin.reset();h.pressed_automatic_help_identity.clear();h.pressed_automatic_help_context.clear();return;}
    const bool modal=HasModalSimDialog()||Pie.IsOpen||h.external_modal;
    if(modal||in.MouseRightDown){h.pressed_automatic_help_identity.clear();h.pressed_automatic_help_context.clear();}
    if(modal||in.MouseRightDown||in.TouchUp||!in.Touching){h.pressed_control.clear();h.press_origin.reset();}
    if(in.TouchDown&&!modal){
        h.pressed_control=HitAt(in.TouchX,in.TouchY);
        h.press_origin=h.pressed_control.empty()?std::nullopt:std::optional<std::array<int,2>>{{in.TouchX,in.TouchY}};
    }
#else
    (void)in;
#endif
}
std::optional<std::array<int,2>> LotScreen::DesktopPointerPosition(bool modalOwner)const{
#ifndef __3DS__
    if(!Desktop||!DesktopHover)return {};
    const auto& h=*DesktopHover;const auto& p=h.pointer;
    if(p.PointerCancelled||!p.DesktopPointer||p.TouchX<0||p.TouchY<0)return {};
    if(!modalOwner&&(h.external_modal||HasModalSimDialog()||Pie.IsOpen))return {};
    return std::array<int,2>{{p.TouchX,p.TouchY}};
#else
    (void)modalOwner;return {};
#endif
}
std::optional<std::array<int,2>> LotScreen::DesktopPressOrigin()const{
#ifndef __3DS__
    if(DesktopPointerPosition()&&DesktopHover)return DesktopHover->press_origin;
#endif
    return {};
}
std::string LotScreen::DesktopPressedControl()const{
#ifndef __3DS__
    if(DesktopPointerPosition()&&DesktopHover)return DesktopHover->pressed_control;
#endif
    return {};
}
bool LotScreen::DesktopButtonHovered(const Rect& r)const{
    const auto point=DesktopPointerPosition();return point&&r.Contains((*point)[0],(*point)[1]);
}
ButtonState LotScreen::ArtState(const Rect& r,const std::string& id,bool selected,bool disabled)const{
#ifndef __3DS__
    if(Desktop)return static_cast<ButtonState>(odh_host::original_button_frame(!disabled,selected,DesktopButtonHovered(r),
        !id.empty()&&DesktopPressedControl()==id));
#else
    (void)r;(void)id;
#endif
    return disabled?ButtonState::Disabled:selected?ButtonState::Highlight:ButtonState::Normal;
}

void LotScreen::SetDesktopHoverModal(bool modal){
#ifndef __3DS__
    if(!DesktopHover)DesktopHover.reset(new DesktopHoverState());
    DesktopHover->external_modal=modal;
    if(modal){DesktopHover->hover.Clear();DesktopHover->pressed_control.clear();DesktopHover->press_origin.reset();Camera.Cancel();CameraCursor.clear();}
#else
    (void)modal;
#endif
}
void LotScreen::OfferDesktopHover(const std::string& name,const std::string& identity,
        const std::string& context,uint64_t selection,const Rect& bounds,uint32_t foreground){
#ifndef __3DS__
    if(!Desktop || !DesktopHover || !bounds.Contains(DesktopHover->pointer.TouchX,DesktopHover->pointer.TouchY))return;
    auto& h=*DesktopHover;h.help_title.clear();h.help_description.clear();h.help_illustration.clear();h.help_objects={};h.help_portrait=false;h.name=name;h.identity=identity;h.context=context;h.selection=selection;h.bounds=bounds;h.foreground=foreground;
#else
    (void)name;(void)identity;(void)context;(void)selection;(void)bounds;(void)foreground;
#endif
}

void LotScreen::OfferDesktopHelp(const std::string& title,const std::string& description,
        const std::string& identity,const std::string& context,uint64_t selection,const Rect& bounds,
        const std::string& illustration,const std::array<std::uint32_t,2>& objects,bool selectedPortrait){
#ifndef __3DS__
    if(title.empty() || description.empty() || !Desktop || !DesktopHover ||
       !bounds.Contains(DesktopHover->pointer.TouchX,DesktopHover->pointer.TouchY))return;
    OfferDesktopHover(title+"\n"+description,identity,context,selection,bounds);
    auto& h=*DesktopHover;h.help_title=title;h.help_description=description;h.help_illustration=illustration;h.help_objects=objects;h.help_portrait=selectedPortrait;
#else
    (void)title;(void)description;(void)identity;(void)context;(void)selection;(void)bounds;(void)illustration;(void)objects;(void)selectedPortrait;
#endif
}

void LotScreen::ClearDesktopSettingsHelp(){
#ifndef __3DS__
    if(DesktopHover){DesktopHover->manual_help.reset();DesktopHover->automatic_help_identity.clear();DesktopHover->automatic_help_context.clear();DesktopHover->pressed_automatic_help_identity.clear();DesktopHover->pressed_automatic_help_context.clear();DesktopHover->dismissed_help_identity.clear();DesktopHover->dismissed_help_context.clear();DesktopHover->hover.Clear();}
#endif
}
void LotScreen::ToggleDesktopSettingsHelp(const std::string& title,const std::string& description,
        const std::string& identity,const std::string& context,const std::string& illustration){
#ifndef __3DS__
    if(!Desktop||title.empty()||description.empty())return;
    if(!DesktopHover)DesktopHover.reset(new DesktopHoverState());
    auto& h=*DesktopHover;
    if((h.manual_help&&h.manual_help->identity==identity&&h.manual_help->context==context)||
       (h.automatic_help_identity==identity&&h.automatic_help_context==context)||
       (h.pressed_automatic_help_identity==identity&&h.pressed_automatic_help_context==context)){
        h.manual_help.reset();h.dismissed_help_identity=identity;h.dismissed_help_context=context;
    }else{
        h.manual_help=DesktopHoverState::ManualHelp{title,description,identity,context,illustration};
        h.dismissed_help_identity.clear();h.dismissed_help_context.clear();
    }
    h.automatic_help_identity.clear();h.automatic_help_context.clear();
    h.pressed_automatic_help_identity.clear();h.pressed_automatic_help_context.clear();
    h.hover.Clear();
#else
    (void)title;(void)description;(void)identity;(void)context;(void)illustration;
#endif
}

static const int16_t SELECT_SIM_PARAM = -32768; // pie entry "Select <name>" (not a TTAB interaction)

const Rect LotScreen::GadgetRect(0, 0, 48, 240);
const Rect LotScreen::PanelRect(48, 0, 272, 240);

static std::string Lower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

static std::string FirstName(const std::string& n) {
    size_t s = n.find(' ');
    return s == std::string::npos ? n : n.substr(0, s);
}

LotScreen::LotScreen(LotSession* session, UIStrings* str, Canvas* top, Canvas* bottom)
    : S(session), Str(str), Top(top), Bottom(bottom) {
    auto content = FSO::Content::Content::Get();
    std::string root = content ? content->SimsCompleteDir : std::string(".");
    UIGraphics::Get().Init(root);
    GameText::Get().Load(root + "/GameData");
    UIHeadlineRenderer::LoadSprites(root + "/GameData");
    UIHeadlineRenderer::IconProvider = [this](VMEntity* e) { return IconFor(e); };
    Holder.reset(new UIObjectHolder(S));
    Holder->OnFunds = [this](int delta, VMEntity* at) { MoneyPopup(delta, at); };
    Live.reset(new UILiveMode(this));
    Buy.reset(new UIBuyMode(this));
    Build.reset(new UIBuildMode(this));
    Options.reset(new UIOptionsPanel(this));
    _LastBudget = S->Budget();
    CurrentMode = DefaultMode();
    if (S->World()) S->World()->State.BuildMode(CurrentMode==Mode::Build?2:CurrentMode==Mode::Buy?1:0);
    if (CurrentMode == Mode::Live) Live->Activate();
    else { Speed = 0; Build->Activate(); }
}

LotScreen::~LotScreen() {
    Pie.Close();
    if (Holder) Holder->ClearSelected();
    UIHeadlineRenderer::IconProvider = nullptr;
}

// Retained 3DS presentation. Desktop uses the preserved original head-arrow
// mesh/texture and mood curve through DrawOriginalSelectedCrystal. Four equator corners at
// angle a + k*90 degrees; a facet is drawn when its outward normal faces the viewer, back to front.
static void DrawPlumbob(Canvas& c, float cx, float cy, float halfW, float halfH, float angle) {
    const float PI = 3.14159265f;
    float lx = -0.55f, ly = -0.6f, lz = 0.58f; // light direction (towards the light; x right, y up, z to viewer)
    struct Facet { float depth; int x0, y0, x1, y1, x2, y2; uint32_t col; };
    Facet f[8]; int n = 0;
    for (int k = 0; k < 4; k++) {
        float a0 = angle + k * PI * 0.5f, a1 = a0 + PI * 0.5f, am = a0 + PI * 0.25f;
        float x0 = std::cos(a0) * halfW, z0 = std::sin(a0), x1 = std::cos(a1) * halfW, z1 = std::sin(a1);
        float zm = std::sin(am); // the facet pair's horizontal facing (toward the viewer when > 0)
        if (zm <= -0.05f) continue;
        for (int half = 0; half < 2; half++) { // 0 = upper facet, 1 = lower
            // facet normal: horizontal part along (cos am, sin am) scaled, vertical part up/down
            float hx = std::cos(am) * 0.70710678f * halfH, hz = zm * 0.70710678f * halfH;
            float ny = (half == 0 ? 1.0f : -1.0f) * 0.70710678f * halfW * 0.9f;
            float len = std::sqrt(hx * hx + hz * hz + ny * ny);
            float d = (hx * lx + ny * ly * -1.0f + hz * lz) / (len > 0 ? len : 1); // y up in light space
            float l = 0.35f + 0.65f * std::max(0.0f, d);
            if (half == 1) l *= 0.78f;
            float r = 60 * l + 20, g = 255 * l, b = 50 * l + 10;
            Facet& ff = f[n++];
            ff.depth = zm;
            ff.x0 = (int)std::lround(cx + x0); ff.y0 = (int)std::lround(cy);
            ff.x1 = (int)std::lround(cx + x1); ff.y1 = (int)std::lround(cy);
            ff.x2 = (int)std::lround(cx); ff.y2 = (int)std::lround(half == 0 ? cy - halfH : cy + halfH);
            ff.col = RGBA((int)std::min(255.0f, r), (int)std::min(255.0f, g), (int)std::min(255.0f, b));
            (void)z0; (void)z1;
        }
    }
    // back to front
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (f[j].depth < f[i].depth) std::swap(f[i], f[j]);
    for (int i = 0; i < n; i++) c.FillTriangle(f[i].x0, f[i].y0, f[i].x1, f[i].y1, f[i].x2, f[i].y2, f[i].col);
    // a thin bright rim on the equator and the silhouette, as the original's lit edges
    int top = (int)std::lround(cy - halfH), bot = (int)std::lround(cy + halfH);
    float ex = 0;
    for (int k = 0; k < 4; k++) ex = std::max(ex, std::fabs(std::cos(angle + k * PI * 0.5f)) * halfW);
    int l = (int)std::lround(cx - ex), r = (int)std::lround(cx + ex), m = (int)std::lround(cx), y = (int)std::lround(cy);
    c.Line(l, y, m, top, RGBA(20, 90, 20, 200)); c.Line(m, top, r, y, RGBA(20, 90, 20, 200));
    c.Line(l, y, m, bot, RGBA(10, 60, 10, 200)); c.Line(m, bot, r, y, RGBA(10, 60, 10, 200));
}

static UIModePanel* PanelOf(LotScreen* l) {
    switch (l->CurrentMode) {
        case LotScreen::Mode::Buy: return (UIModePanel*)l->Buy.get();
        case LotScreen::Mode::Build: return (UIModePanel*)l->Build.get();
        case LotScreen::Mode::Options: return (UIModePanel*)l->Options.get();
        default: return (UIModePanel*)l->Live.get();
    }
}

void LotScreen::SetMode(Mode m) {
    if (m == Mode::Live && !S->CanEnterLive()) return;
    if (m == CurrentMode) return;
    PanelOf(this)->Deactivate();
#ifndef __3DS__
    if(DesktopHover){DesktopHover->pressed_control.clear();DesktopHover->press_origin.reset();}
    Camera.Cancel();CameraCursor.clear();
#endif
    Pie.Close(); PieTarget = nullptr;
    TopTip.clear(); // FIX(sims) QA2: a buy-mode placement error ("Must place on a table or surface") stayed over live/options mode
    bool wasBuildBuy = CurrentMode == Mode::Buy || CurrentMode == Mode::Build;
    CurrentMode = m;
    if (S && S->World()) S->World()->State.BuildMode(m==Mode::Build?2:m==Mode::Buy?1:0);
    bool isBuildBuy = m == Mode::Buy || m == Mode::Build;
    // TS1(sims): leaving buy/build ends the "changed my mind" full refund window
    if (wasBuildBuy && !isBuildBuy) Holder->BoughtThisSession.clear();
    PanelOf(this)->Activate();
    if (Log) *Log += std::string("mode ") + (m == Mode::Live ? "live" : m == Mode::Buy ? "buy" : m == Mode::Build ? "build" : "options") + "\n";
}

int LotScreen::EffectiveSpeed() const {
    if (!S->CanEnterLive()) return 0;
    for (auto& d : S->Dialogs) if (d.Block) return 0; // TS1 pauses while a dialog waits for an answer
    if (CurrentMode != Mode::Live) return 0;            // TS1: time stands still in buy/build/options
    return Speed;
}

std::string LotScreen::HitAt(int x, int y) const {
    #ifndef __3DS__
    if(Desktop){auto id=OriginalPCUI::Get().ControlAt(x,y);if(!id.empty())return id;}
    #endif
    for (size_t i = Hits.size(); i-- > 0;) if (Hits[i].R.Contains(x, y)) {
        #ifndef __3DS__
        if(Desktop&&OriginalPCUI::IsControl(Hits[i].Id))continue;
        #endif
        return Hits[i].Id; // topmost = last drawn
    }
    return "";
}

// ======================================================================= shared widgets

void LotScreen::DrawPanelFrame(Canvas& c, const Rect& r) {
    c.GradientV(r.X, r.Y, r.W, r.H, RGBA(16, 24, 92), RGBA(6, 10, 60));
    c.FillRect(r.X, r.Y, r.W, 1, RGBA(120, 130, 210));
    c.FillRect(r.X, r.Bottom() - 1, r.W, 1, RGBA(0, 0, 30));
}

void LotScreen::DrawArtButton(Canvas& c, const Rect& r, const std::string& art, int frames, bool selected, bool disabled,
                              const std::string& fallbackLabel, const std::string& hitId) {
    ButtonState st = ArtState(r,hitId,selected,disabled);
    bool drawn = !art.empty() && UIGraphics::Get().DrawButton(c, art, st, r, frames);
    if (!drawn) {
        c.FillRoundRect(r, 5, disabled ? RGBA(40, 50, 80) : (selected ? theme::ButtonHi : theme::ButtonBg));
        c.StrokeRoundRect(r.X, r.Y, r.W, r.H, 5, selected ? RGBA(0, 255, 255) : theme::PanelEdge);
        if (!fallbackLabel.empty())
            DrawTextCentered(c, FontId::Small, r, Ellipsize(FontId::Small, fallbackLabel, r.W - 2), disabled ? theme::TextDim : (selected ? RGBA(0, 255, 255) : theme::TextBright));
    }
    if (!hitId.empty() && !disabled) Hit(r, hitId);
}

void LotScreen::DrawTouchView(Canvas& c, const Rect& area) {
    if (Desktop) { ViewRect = Rect(0,0,Top->Width,Top->Height-100); ViewScale = 1; return; }
    auto& f = S->LastFrame();
    float sc = std::min((float)area.W / Top->Width, (float)area.H / Top->Height);
    int w = (int)(Top->Width * sc), h = (int)(Top->Height * sc);
    Rect r(area.X + (area.W - w) / 2, area.Y + (area.H - h) / 2, w, h);
    if (r.W != ViewRect.W || r.H != ViewRect.H) ViewFrameNo = 0;
    ViewRect = r; ViewScale = sc;
    if (f.Width > 0) {
        if (ViewThumb.Width != w || ViewThumb.Height != h) { ViewThumb.Resize(w, h); ViewFrameNo = 0; }
        if (ViewFrameNo++ % (uint32_t)std::max(1, ThumbInterval) == 0)
            ViewThumb.BlitScaled(f.Color.data(), f.Width, f.Height, Rect(0, 0, w, h));
        c.Blit(ViewThumb.Px.data(), w, h, r.X, r.Y, false);
    }
    c.StrokeRect(r.X - 1, r.Y - 1, r.W + 2, r.H + 2, RGBA(90, 100, 190));
    // the selected Sim's plumbob, the cursor
    if (auto sim = S->SelectedSim()) {
        float x, y;
        if (CurrentMode == Mode::Live && sim->Position().Level <= S->World()->State.Level() && S->EntityScreenPos(sim, x, y, 2.05f)) {
            int px, py; TopToView(x, y, px, py); py -= 4;
            if (r.Contains(px, py)) { c.FillTriangle(px - 3, py, px + 3, py, px, py - 5, theme::Plumbob); c.FillTriangle(px - 3, py, px + 3, py, px, py + 5, theme::PlumbobDark); }
        }
    }
    if (CursorShown) {
        int px, py; TopToView((float)CursorX, (float)CursorY, px, py);
        c.Line(px - 3, py, px + 3, py, RGBA(255, 255, 255, 200));
        c.Line(px, py - 3, px, py + 3, RGBA(255, 255, 255, 200));
    }
    Hit(r, "view");
}

bool LotScreen::ViewToTop(int bx, int by, int& tx, int& ty) const {
    if (ViewScale <= 0) return false;
    tx = (int)std::lround((bx - ViewRect.X) / ViewScale);
    ty = (int)std::lround((by - ViewRect.Y) / ViewScale);
    tx = std::max(0, std::min(Top->Width - 1, tx));
    ty = std::max(0, std::min(Top->Height - 1, ty));
    return true;
}

void LotScreen::TopToView(float tx, float ty, int& bx, int& by) const {
    bx = ViewRect.X + (int)std::lround(tx * ViewScale);
    by = ViewRect.Y + (int)std::lround(ty * ViewScale);
}

void LotScreen::ShowTopTip(const std::string& s, int x, int y, uint32_t color, double ms) {
    TopTip = s; TopTipX = x; TopTipY = y; TopTipColor = color; TopTipUntil = Time + ms;
}

// ======================================================================= icons

static bool DecodeBmpChunk(FSO::Files::Formats::IFF::Chunks::BMP* bmp, Canvas& out, bool keyCorner, bool originalPC = false) {
    if (bmp == nullptr || !UIGraphics::DecodeBMP(bmp->GetData(), out) || out.Px.empty()) return false;
    if (keyCorner && originalPC) {
        // OriginalQueueView/catalogue convention: two side-by-side object
        // images, except for the full-width 127/128px catalogue pictures.
        // Only magenta is transparent. A corner colour can belong to the object.
        const int crop = out.Width * 10 > out.Height * 11 && out.Width != 127 && out.Width != 128 ? out.Width / 2 : out.Width;
        if (crop != out.Width) {
            Canvas normal(crop,out.Height);
            for(int y=0;y<out.Height;++y)
                std::copy_n(out.Px.data()+size_t(y)*out.Width,crop,normal.Px.data()+size_t(y)*crop);
            out=std::move(normal);
        }
    }
    if (keyCorner && !originalPC) {
    if (keyCorner && (out.Px[0] >> 24)) {
        uint32_t key = out.Px[0] & 0x00FFFFFFu;
        for (auto& p : out.Px) if ((p & 0x00FFFFFFu) == key) p = 0;
    }
    if (keyCorner) {
        // FIX(sims) FB1: catalogue BMP_ = button frames side by side (FreeSO UICatalogItem): keep the normal frame
        int frames = (out.Height > 0 && out.Width / out.Height > 2) ? 4 : (out.Height > 48 || out.Width == out.Height) ? 1 : 2;
        if (frames > 1) {
            int fw = out.Width / frames;
            Canvas f(fw, out.Height);
            for (int y = 0; y < out.Height; y++) for (int x = 0; x < fw; x++) f.Px[(size_t)y * fw + x] = out.Px[(size_t)y * out.Width + x];
            out = std::move(f);
        }
    }
    }
    return true;
}

const Canvas* LotScreen::IconFor(VMEntity* e) {
    if (auto av = dynamic_cast<VMAvatar*>(e)) {
        // Original queue icons use BMP2004, not the separate 35x41 HUD face.
        if(Desktop && av->Object && av->Object->Resource) {
            uint64_t key=av->Object->GUID;
            auto found=_Icons.find(key);
            if(found!=_Icons.end())return found->second.get();
            auto bmp=av->Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::BMP>(2004);
            std::unique_ptr<Canvas> thumb(new Canvas());
            if(bmp && front::DecodeImage(bmp->GetData(),*thumb,true)) {
                auto result=thumb.get();_Icons[key]=std::move(thumb);return result;
            }
        }
        return FaceFor(av,0);
    }
    auto go = dynamic_cast<VMGameObject*>(e);
    if (go == nullptr || go->Object == nullptr || go->Object->OBJ == nullptr || go->Object->Resource == nullptr) return nullptr;
    // multitile parts: the master's catalogue icon
    uint64_t key = go->Object->GUID;
    auto it = _Icons.find(key);
    if (it != _Icons.end()) return it->second.get();
    std::unique_ptr<Canvas> c(new Canvas());
    auto definition = Desktop && go->MasterDefinition ? go->MasterDefinition : go->Object->OBJ;
    auto bmp = go->Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::BMP>(definition->CatalogStringsID);
    if(!Desktop && !bmp && go->MasterDefinition)
        bmp=go->Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::BMP>(go->MasterDefinition->CatalogStringsID);
    if (!DecodeBmpChunk(bmp, *c, true, Desktop)) c.reset();
    auto res = c.get();
    _Icons[key] = std::move(c);
    return res;
}

const Canvas* LotScreen::IconForGUID(uint32_t guid) {
    uint64_t key = guid;
    auto it = _Icons.find(key);
    if (it != _Icons.end()) return it->second.get();
    std::unique_ptr<Canvas> c;
    auto content = FSO::Content::Content::Get();
    auto obj = content ? content->WorldObjects->Get(guid, true) : nullptr;
    if (obj != nullptr && obj->OBJ != nullptr && obj->Resource != nullptr) {
        c.reset(new Canvas());
        if (!DecodeBmpChunk(obj->Resource->Get<FSO::Files::Formats::IFF::Chunks::BMP>(obj->OBJ->CatalogStringsID), *c, true, Desktop)) c.reset();
    }
    auto res = c.get();
    _Icons[key] = std::move(c);
    return res;
}

std::string LotScreen::NameForGUID(uint32_t guid) {
    auto content = FSO::Content::Content::Get();
    auto obj = content ? content->WorldObjects->Get(guid, true) : nullptr;
    if (obj == nullptr || obj->OBJ == nullptr) return std::string();
    if (obj->Resource != nullptr) {
        if (auto ctss = obj->Resource->Get<FSO::Files::Formats::IFF::Chunks::CTSS>(obj->OBJ->CatalogStringsID)) {
            auto n = ctss->GetString(0);
            if (!n.empty()) return ToLatin1(n);
        }
    }
    return ToLatin1(obj->OBJ->ChunkLabel);
}

const Canvas* LotScreen::FaceFor(VMAvatar* a, int frame) {
    #ifndef __3DS__
    if(Desktop)return OriginalPCUI::Get().Face(a,frame);
    #endif
    if (a == nullptr || a->Object == nullptr || a->Object->Resource == nullptr) return nullptr;
    // Preserve the authored face strip. Selection highlights its surrounding UI frame.
    frame=std::clamp(frame,0,2);
    uint64_t key = (uint64_t(a->Object->GUID) << 2) | uint64_t(frame & 3);
    auto it = _Faces.find(key);
    if (it != _Faces.end()) return it->second.get();
    std::unique_ptr<Canvas> out;
    Canvas all;
    std::string source;
    auto faces=a->Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::BMP>(2002);
    // Use the UI decoder, including the RLE formats supported by original character portraits.
    if (faces && front::DecodeImage(faces->GetData(),all,true) && all.Width>=3 && all.Width%3==0) {
        source="authored-faces";
        int fw = all.Width / 3;
        out.reset(new Canvas(fw, all.Height));
        for (int y = 0; y < all.Height; y++)
            for (int x = 0; x < fw; x++) out->Px[(size_t)y * fw + x] = all.Px[(size_t)y * all.Width + frame * fw + x];
    } else if (auto thumbnail=a->Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::BMP>(2004);
               thumbnail && front::DecodeImage(thumbnail->GetData(),all,true)) {
        // Authored thumbnails are authoritative too. Do not replace a supplied
        // face with a newly rendered interpretation merely because it is smaller.
        out.reset(new Canvas(all)); source="authored-thumbnail";
    } else if (auto str = a->Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::STR>(200)) {
        // TS1(sims): a character without portraits (imported .FAM members, babies, grown-up children): draw the
        // face from its body strings like Create-A-Sim does (CASSimPreview::RenderPortraits).
        // Generate once only when the character supplies neither faces nor a
        // usable thumbnail. Keep imported families' authored faces unchanged.
        auto bs = [&](int i) { return i < str->Length() ? str->GetString(i) : std::string(); };
        auto split = [](const std::string& v, const char* key, FSO::Content::TS1::CAS::CASOutfit& o) {
            auto c = v.find(','); auto k = v.find(key);
            if (c == std::string::npos || k == std::string::npos) return;
            o.Mesh = Lower(v.substr(0, c)); o.Texture = Lower(v.substr(k + strlen(key)));
        };
        using namespace FSO::Content::TS1::CAS;
        CASSimDesc d;
        d.Age = bs(0) == "child" ? CASAge::Child : CASAge::Adult;
        d.Gender = bs(12) == "male" ? CASGender::Male : CASGender::Female;
        auto skin = bs(14);
        d.Skin = skin == "drk" ? CASSkin::Dark : (skin == "med" ? CASSkin::Medium : CASSkin::Light);
        split(bs(1), "BODY=", d.Body);
        split(bs(2), "HEAD-HEAD=", d.Head);
        if (!d.Head.Empty() && !d.Body.Empty() && bs(0) != "dog" && bs(0) != "cat" && bs(0) != "kat") {
            simapp::cas::CASSimPreview preview;
            preview.Set(d);
            if (preview.Valid()) {
                simapp::cas::CASPortraits p;
                preview.RenderPortraits(p,Desktop);
                source="cached-frontal-face";
                if (p.Faces.Width >= 3) {
                    int fw = p.Faces.Width / 3;
                    out.reset(new Canvas(fw, p.Faces.Height));
                    for (int y = 0; y < p.Faces.Height; y++)
                        for (int x = 0; x < fw; x++) out->Px[(size_t)y * fw + x] = p.Faces.Px[(size_t)y * p.Faces.Width + frame * fw + x];
                }
            }
        }
    }
    if(Desktop && Log && out) *Log+="portrait guid="+std::to_string(a->Object->GUID)+" source="+source+
        " size="+std::to_string(out->Width)+"x"+std::to_string(out->Height)+" frame="+std::to_string(frame)+"\n";
    auto res = out.get();
    _Faces[key] = std::move(out);
    return res;
}

// ======================================================================= money

void LotScreen::MoneyPopup(int amount, VMEntity* at) {
    if (amount == 0) return;
    if (at == nullptr) at = S->SelectedSim();
    Popup p;
    p.Amount = amount; p.Start = Time; p.X = -1; p.Y = -1;
    if (at != nullptr && at->WorldUI != nullptr) {
        auto pos = at->WorldUI->DrawPosition();
        p.X = pos.X; p.Y = pos.Y;
        if (dynamic_cast<VMGameObject*>(at)) { p.X += 0.5f; p.Y += 0.5f; }
    }
    if (_Popups.size() < 6) _Popups.push_back(p);
    _LastBudget = S->Budget();
}

// FIX(sims) QA2: wall/floor/terrain costs used the selected Sim's position: off screen they piled up, cut off at the
// screen edge ("-$486" x4 at the right border). They now rise from where the stylus let go.
void LotScreen::MoneyPopupAt(int amount, int topX, int topY) {
    if (amount == 0) return;
    Popup p;
    p.Amount = amount; p.Start = Time; p.X = -1000; p.Y = -1000; p.SX = topX; p.SY = topY;
    if (_Popups.size() < 6) _Popups.push_back(p);
    _LastBudget = S->Budget();
}

// Live-mode overhead amounts are explicitly requested by the object's behavior.
void LotScreen::PollMoneyHeadlines() {
    for (auto e : S->VM()->Entities) {
        auto av = dynamic_cast<VMAvatar*>(e);
        if (av == nullptr) continue;
        auto& previous = _MoneyHeadlineSerial[av->ObjectID];
        if (previous == av->MoneyHeadlineSerial) continue;
        previous = av->MoneyHeadlineSerial;
        int amount = av->MoneyHeadlineValue;
        if (amount == 0 || av->WorldUI == nullptr) continue;
        auto pos = av->WorldUI->DrawPosition();
        float lift = (av->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable::PersonsAge) < 18) ? 1.7f : 2.3f;
        if (_Popups.size() >= 6) continue;
        Popup p;
        p.Amount = amount; p.Start = Time; p.X = pos.X; p.Y = pos.Y; p.Z = pos.Z; p.Lift = lift; p.Head = true;
        _Popups.push_back(p);
    }
}

void LotScreen::TrackBudget() {
    PollMoneyHeadlines();
    int b = S->Budget();
    if (_LastBudget < 0) { _LastBudget = b; return; }
    if (b != _LastBudget) {
        // The price over a Sim and the household ledger are separate. A fridge
        // can show a positive price while debiting that amount from the family.
        const int delta = b - _LastBudget;
        _LastBudget = b;
        if (Log) { char buf[64]; snprintf(buf, sizeof buf, "funds %+d -> %d\n", delta, b); *Log += buf; }
    }
}

// ======================================================================= input

void LotScreen::OpenPieAt(int tx, int ty) {
    std::vector<PieChoice> pie;
    bool isGoto = false;
    int16_t picked = 0;
    double t0 = NowMs ? NowMs() : 0;
    auto target = S->PieTargetAt(tx, ty, pie, isGoto, picked);
    PickMsLast = NowMs ? NowMs() - t0 : 0;
    if (target == nullptr) { if (Log) *Log += "tap: nothing\n"; return; }
    std::string title = isGoto ? std::string("") : ToLatin1(target->ToString());
    bool titleUTF8 = false;
#ifndef __3DS__
    if (Desktop && !isGoto) if (auto* avatar = dynamic_cast<VMAvatar*>(target)) {
        title = original::PersonalLabelUTF8(avatar->Name(), avatar->PersonalTextLanguage, avatar->PersonalTextUTF8, avatar->PersonalTextAmbiguous);
        titleUTF8 = true;
    }
#endif
    // FIX(sims) REL: object names that end in a line break (D.I.Y. Bookshelf) showed "??" after the pie title
    while (!title.empty() && (unsigned char)title.back() <= 32) title.pop_back();
    // a family member: offer selecting it first (TS1 selects Sims from their portraits; here also from the lot)
    int fam = S->IndexOfFamily(target);
    if (!Desktop && fam >= 0 && fam != S->Selected) {
        PieChoice sel;
        sel.Name = "Select " + FirstName(ToLatin1(target->ToString()));
        sel.ID = 255; sel.Param0 = SELECT_SIM_PARAM;
        pie.insert(pie.begin(), sel);
    }
    if (Log) {
        char buf[256];
        snprintf(buf, sizeof buf, "tap view -> top (%d,%d): picked #%d -> %s'%s' (%zu interactions, pick %.2f ms)\n",
                 tx, ty, picked, isGoto ? "goto " : "", title.c_str(), pie.size(), PickMsLast);
        *Log += buf;
    }
    if (pie.empty()) {
        if (!isGoto && picked > 0) ShowTopTip(GameText::Get().UI(133, 3, "There is nothing to select here."), tx, ty - 14, RGBA(255, 255, 255));
        return;
    }
    PieTarget = target;
    PieGoto = isGoto;
    PieMarkX = (float)tx; PieMarkY = (float)ty;
    Pie.Desktop=Desktop;
    Pie.Open(pie, title, Rect(0, 0, Bottom->Width, Desktop?std::max(120,Bottom->Height-100):Bottom->Height),tx,ty,titleUTF8);
    if(Desktop) {
#ifndef __3DS__
        Pie.SetHead(S->SelectedSim());
#else
        // A separate UI avatar keeps turning the menu head from changing the live Sim's pose.
        cas::CASSimDesc desc;
        auto actor=S->SelectedSim();
        if(actor && actor->Object && actor->Object->Resource) {
            if(auto strings=actor->Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::STR>(200)) {
                auto value=[&](int i) { return i<strings->Length()?strings->GetString(i):std::string(); };
                auto split=[&](int index,const char* label,cas::CASOutfit& outfit) {
                    auto text=Lower(value(index)); auto comma=text.find(','); auto key=text.find(Lower(label));
                    if(comma!=std::string::npos && key!=std::string::npos) {
                        outfit.Mesh=text.substr(0,comma); outfit.Texture=text.substr(key+std::strlen(label));
                    }
                };
                desc.Age=value(0)=="child"?cas::CASAge::Child:cas::CASAge::Adult;
                desc.Gender=value(12)=="male"?cas::CASGender::Male:cas::CASGender::Female;
                desc.Skin=value(14)=="drk"?cas::CASSkin::Dark:value(14)=="med"?cas::CASSkin::Medium:cas::CASSkin::Light;
                if(value(0)!="dog" && value(0)!="cat" && value(0)!="kat") {
                    split(1,"BODY=",desc.Body); split(2,"HEAD-HEAD=",desc.Head);
                }
            }
        }
        Pie.SetHead(desc);
#endif
    }
}

// FIX(sims) SND: FreeSims UISounds through the HIT VM (UI events keep playing while the lot's sounds are paused)
void PlayUISound(const char* evt) {
    auto& ga = simaudio::GameAudio::Get();
    if (ga.Ready()) ga.PlayUI(evt);
}

// FIX(sims) SND: FreeSims UIButton plays UISounds.Click after every button click (mode, catalogue category, sub-sort,
// page arrows, build tools, options, Sim portraits, tabs...). Not buttons there, so not here: catalogue items
// (UICatalogItem: silent, Error when unaffordable), the interaction queue (QueueDelete), the money / clock readouts;
// the hold toolbar has its own sounds (place / rotate / money back, UIObjectHolder) and the speed buttons play
// TS1's ui_speed_* transitions (App::AudioFrame).
static bool TapClicks(const std::string& id) {
    auto starts = [&](const char* p) { return id.rfind(p, 0) == 0; };
    if (id == "funds" || id == "clock" || id == "panel" || id == "view") return false;
    if (starts("speed") || starts("item") || starts("bitem") || starts("queue")) return false;
    if (id == "hold_place" || id == "hold_sell" || id == "hold_rotl" || id == "hold_rotr") return false;
    return true;
}

void LotScreen::HandleTap(const std::string& id) {
    if (TapClicks(id)) PlayUISound(UISounds::Click); // FIX(sims) SND
    if (id.rfind("mode_", 0) == 0) {
        if (id == "mode_live") SetMode(Mode::Live);
        else if (id == "mode_buy") {
            if (Desktop && CurrentMode == Mode::Buy)
                Buy->Tap(Buy->RoomCategories ? "sort_function" : "sort_room");
            else SetMode(Mode::Buy);
        }
        else if (id == "mode_build") SetMode(Mode::Build);
        else if (id == "mode_options") SetMode(CurrentMode == Mode::Options ? DefaultMode() : Mode::Options);
        return;
    }
    if (id.rfind("speed", 0) == 0) {
        if (!S->CanEnterLive()) return;
        int sp = id[5] - '0';
        Speed = std::max(0, std::min(3, sp));
        if (Speed) LastSpeed = Speed;
        if (CurrentMode != Mode::Live) SetMode(Mode::Live);
        return;
    }
    if (id == "funds" || id == "clock") return;
    if (id == "rotate_left") { S->Rotate(-1); return; }
    if (id == "rotate_right") { S->Rotate(1); return; }
    if (id == "zoom_in") { S->Zoom(1); return; }
    if (id == "zoom_out") { S->Zoom(-1); return; }
    if (id == "level_up") { S->ChangeLevel(1); return; }
    if (id == "level_down") { S->ChangeLevel(-1); return; }
    if (id == "walls_up") { S->SetWallsMode(2); return; }
    if (id == "walls_down") { S->SetWallsMode(1); return; }
    if (id == "walls_cut") { S->SetWallsMode(0); return; }
    PanelOf(this)->Tap(id);
}

void LotScreen::HandleButtons(const AppInput& in) {
    auto panel = PanelOf(this);
    bool consumed = panel->Buttons(in);
    if (Desktop) {
        using C = DesktopCommand;
        switch (in.DesktopAction) {
        case C::Pause:
            if (CurrentMode == Mode::Live) {
                if (Speed == 0) Speed = std::max(1, LastSpeed);
                else { LastSpeed = Speed; Speed = 0; }
            }
            break;
        case C::Live: SetMode(Mode::Live); break;
        case C::Buy:
            if (CurrentMode == Mode::Buy) Buy->Tap(Buy->RoomCategories ? "sort_function" : "sort_room");
            else SetMode(Mode::Buy);
            break;
        case C::Build: SetMode(Mode::Build); break;
        case C::SpeedNormal: case C::SpeedFast: case C::SpeedUltra:
            if (CurrentMode == Mode::Live) {
                Speed = in.DesktopAction == C::SpeedNormal ? 1 : in.DesktopAction == C::SpeedFast ? 2 : 3;
                LastSpeed = Speed;
            }
            break;
        case C::ZoomIn: S->Zoom(1); break;
        case C::ZoomOut: S->Zoom(-1); break;
        case C::WallsUp: case C::WallsDown: {
            S->SetWallsMode(original::StepOriginalWalls(S->WallsMode,
                in.DesktopAction == C::WallsUp ? 1 : -1));
            break;
        }
        case C::NextSim:
            if (CurrentMode == Mode::Live && !S->Family.empty()) S->Select((S->Selected + 1) % int(S->Family.size()));
            break;
        case C::CenterSim:
            if (CurrentMode == Mode::Live) S->CenterOn(S->SelectedSim());
            break;
        case C::CatalogPrevious: case C::CatalogNext:
            if (CurrentMode == Mode::Buy || CurrentMode == Mode::Build)
                PanelOf(this)->Tap(in.DesktopAction == C::CatalogPrevious ? "prev" : "next");
            break;
        case C::None: break;
        }
    }
    if (in.Down & BTN_START) SetMode(CurrentMode == Mode::Options ? DefaultMode() : Mode::Options);
    if (!(Desktop && Holder->Holding)) {
        if (in.Down & BTN_L) S->Rotate(-1);
        if (in.Down & BTN_R) S->Rotate(+1);
    }
    if (in.Down & BTN_UP) S->Zoom(+1);
    if (in.Down & BTN_DOWN) S->Zoom(-1);
    if (in.Down & BTN_ZL) S->ChangeLevel(-1);
    if (in.Down & BTN_ZR) S->ChangeLevel(+1);
    if (in.Down & BTN_SELECT) S->SetWallsMode(S->WallsMode + 1);
    if ((in.Down & BTN_X) && CurrentMode == Mode::Live) {
        if (Speed == 0) Speed = std::max(1, LastSpeed); else { LastSpeed = Speed; Speed = 0; }
    }
    if ((in.Down & BTN_B) && !consumed && CurrentMode == Mode::Options) SetMode(DefaultMode());
}

void LotScreen::Input(const AppInput& in, double dtMs, double timeMs) {
    if (!S->CanEnterLive()) {
        Speed = 0;
        if (CurrentMode == Mode::Live) SetMode(Mode::Build);
    }
#ifndef __3DS__
    if(Desktop){
        if(!DesktopHover)DesktopHover.reset(new DesktopHoverState());
        ObserveDesktopPointer(in);
    }
#endif
    // All early modal/overlay returns must also release camera capture.
#ifndef __3DS__
    if(Desktop){
        CameraCursor.clear();
        if(in.PointerCancelled || HasModalSimDialog() || Pie.IsOpen ||
           (DesktopHover && DesktopHover->external_modal) || Buy->DesktopQueryVisible)Camera.Cancel();
        if(in.PointerCancelled){
            P=Pointer{};
            // Preserve the existing consumers' nonactivating capture reset.
            // Never forward stale keys/button edges or synthesize Escape/Up.
            AppInput cancel;cancel.DesktopPointer=true;cancel.PointerCancelled=true;
            cancel.TouchX=cancel.TouchY=-1;
            auto panel=PanelOf(this);
            panel->Buttons(cancel); // Options forwards PointerCancelled to its slider capture.
            Holder->CancelPointerGesture();
            panel->PointerUpdate(); // Inactive P cancels Buy/Build preview gestures.
            return;
        }
    }
#endif
    // ADDED(sims): held objects are moved to room 65534 (UIObjectHolder.SetSelected); FreeSims' lighting
    // texture lit that entry, our RoomColors leaves it transparent black - light it like the outside.
    if (S->Blueprint != nullptr && S->Blueprint->RoomColors.size() > 65534) S->Blueprint->RoomColors[65534] = st::Color::White();
    Time = timeMs;
    if (PointerMovedAt < 0) PointerMovedAt = Time; // ADDED(sims): the idle time starts with the lot
    auto& s = *S;
    P.Down = P.Up = P.Click = false;
    // ADDED(sims): pointer visibility -- the stylus hides the top-screen pointer; it only counts down its idle time
    // in live mode with nothing modal open. The cut follows the active input (set below); default: keep the last cut.
    if (in.TouchDown || in.Touching) HidePointer();
    if (CurrentMode != Mode::Live || Pie.IsOpen || HasModalSimDialog()) PointerMovedAt = Time;
    CutFollow = false;

    // ---------------- modal: dialogs
    if (HasModalSimDialog()) {
        if (in.TouchDown) {
            for (auto& b : _DialogButtons) if (b.first.Contains(in.TouchX, in.TouchY)) { PlayUISound(UISounds::Click); s.RespondDialog(b.second); break; } // FIX(sims) SND: UIAlert's UIButtons
        } else if (in.Down & BTN_A) s.RespondDialog(0);
        else if (in.Down & BTN_B) s.RespondDialog(s.Dialogs.front().HasCancel ? 2 : (s.Dialogs.front().HasNo ? 1 : 0));
        _ViewTouch = false;
        return;
    }
    // ---------------- modal: pie menu
    if (Pie.IsOpen) {
        PieChoice choice;
        PieMenu::Result r = PieMenu::Result::None;
        if(Desktop) r=Pie.Pointer(in,choice,dtMs);
        else if (in.TouchDown) r = Pie.Tap(in.TouchX, in.TouchY, choice);
        if(r==PieMenu::Result::None) {
            int dx = ((in.Down & BTN_RIGHT) ? 1 : 0) - ((in.Down & BTN_LEFT) ? 1 : 0);
            int dy = ((in.Down & BTN_DOWN) ? 1 : 0) - ((in.Down & BTN_UP) ? 1 : 0);
            r = Pie.Button(dx, dy, (in.Down & BTN_A) != 0, (in.Down & BTN_B) != 0, choice);
        }
        if (r == PieMenu::Result::Chosen) {
            simaudio::GameAudio::Get().PlayUI("ui_piemenu_select");
            auto vm = s.VM();
            bool alive = PieTarget != nullptr && (PieTarget == s.GotoObject || vm->GetObjectById(PieTarget->ObjectID) == PieTarget);
            if (choice.ID == 255 && choice.Param0 == SELECT_SIM_PARAM) {
                int fam = alive ? s.IndexOfFamily(PieTarget) : -1;
                if (fam >= 0) s.Select(fam);
                if (Log) *Log += "selected " + (alive ? PieTarget->ToString() : std::string("?")) + "\n";
            } else if (alive) {
                auto caller = s.SelectedSim();
                std::vector<std::shared_ptr<Engine::VMQueuedAction>> before;
                if (Desktop && Log && caller && caller->Thread) before = caller->Thread->Queue;
                bool ok = s.PushInteraction(PieTarget, PieGoto, choice);
                simaudio::GameAudio::Get().PlayUI(ok ? "ui_queued" : "ui_error"); // FreeSims UIPieMenu: QueueAdd
                if (Log) {
                    *Log += std::string(ok ? "queued '" : "could not queue '") + choice.Name + "' on " + (PieGoto ? std::string("tile") : PieTarget->ToString()) + "\n";
                    if (Desktop) {
                        auto identity = [](const VMEntity* e) {
                            char text[64];
                            std::snprintf(text, sizeof text, "oid=%d guid=%08x", e ? int(e->ObjectID) : 0,
                                e && e->Object && e->Object->OBJ ? unsigned(e->Object->OBJ->GUID) : 0u);
                            return std::string(text);
                        };
                        *Log += "[player-action] caller{" + identity(caller) + "} target{" + identity(PieTarget) +
                            "} choice=" + std::to_string(unsigned(choice.ID)) + " param0=" + std::to_string(choice.Param0) +
                            " goto=" + std::to_string(PieGoto) + " result=" + (ok ? "queued" : "rejected") + "\n";
                        if (caller && caller->Thread) for (const auto& q : caller->Thread->Queue) {
                            if (std::find(before.begin(), before.end(), q) != before.end()) continue;
                            *Log += "[player-action] added uid=" + std::to_string(q->UID) + " callee{" + identity(q->Callee) +
                                "} tree=" + std::to_string(q->ActionRoutine ? q->ActionRoutine->ID : 0) +
                                " interaction=" + std::to_string(q->InteractionNumber) + "\n";
                        }
                    }
                }
            }
            PieTarget = nullptr;
        } else if (r == PieMenu::Result::Closed) PieTarget = nullptr;
        if (in.Down & BTN_START) { Pie.Close(); PieTarget = nullptr; }
        _ViewTouch = false;
        _ABtnHeld = (in.Held & BTN_A) != 0;
        return;
    }

    if (Desktop) {
        if (in.MouseRightDown) { Pie.Close(); PieTarget = nullptr; }
        HandleButtons(in);
#ifndef __3DS__
        if(CurrentMode==Mode::Live) {
            if(Live->CurrentTab==UILiveMode::Tab::Inventory&&
               original::ScrollDesktopExpansionPopup(Live->_DesktopInventory.popup,in.WheelY,in.TouchX,in.TouchY))return;
            if(Live->CurrentTab==UILiveMode::Tab::Job){
                const auto current=original::DesktopLiveSnapshot(*S,odh_host::LivePanelTab::career);
                const auto before=Live->_DesktopCareer.selection.selected();
                Live->_DesktopCareer.selection.reconcile(current.career?current.career->identity:std::string{});
                if(before&&!Live->_DesktopCareer.selection.selected())Live->_DesktopCareer.popup.Close();
                if(original::ScrollDesktopExpansionPopup(Live->_DesktopCareer.popup,in.WheelY,in.TouchX,in.TouchY))return;
                if(original::ScrollDesktopExpansionPopup(Live->_DesktopFame.popup,in.WheelY,in.TouchX,in.TouchY))return;
            }
        }
        // Recovered BuyModeView::pointer_motion: retain the description while
        // browsing its catalog or reading/scrolling the popup, then dismiss it
        // as the pointer returns to the lot. Motion never places the item.
        if(Buy->DesktopQueryVisible && in.TouchX>=0 && in.TouchY>=0) {
            const auto catalog=odh_host::original_control_panel_layout(Top->Width,Top->Height).base;
            if(!catalog.contains(in.TouchX,in.TouchY) && !Buy->DesktopQueryRect.Contains(in.TouchX,in.TouchY))
                Buy->DesktopQueryVisible=false;
        }
        if(!Buy->DesktopQueryVisible)OriginalPCUI::Get().ClearCatalogInfo();
        else if(OriginalPCUI::Get().ScrollCatalogInfo(in.WheelY,in.TouchX,in.TouchY))return;
#endif
        CursorX = in.TouchX; CursorY = in.TouchY;
        CursorShown = false; // the platform supplies the system pointer
        std::string hit = HitAt(in.TouchX, in.TouchY);
        if (in.TouchDown && !hit.empty()) HandleTap(hit);
        P.Active = hit.empty() && in.TouchX>=0 && in.TouchX<Top->Width && in.TouchY>=0 && in.TouchY<Top->Height-100;
#ifndef __3DS__
        const auto needsPanel=CurrentMode==Mode::Live&&Live->CurrentTab==UILiveMode::Tab::Mood?
            original::DesktopNeedsPanelBounds(Top->Width,Top->Height,s):std::nullopt;
        if(needsPanel&&needsPanel->contains(in.TouchX,in.TouchY))P.Active=false;
#endif
        if(P.Active && in.TouchX<220 && in.TouchY>=Top->Height-183) {
            const auto covered=[&](const char* path,int ox,int oy) {
                const auto* image=UIGraphics::Get().Image(path);
                const int x=in.TouchX-ox,y=in.TouchY-oy;
                return image && x>=0 && y>=0 && x<image->Width && y<image->Height &&
                    (image->Px[size_t(y)*image->Width+x]>>24)!=0;
            };
            if(covered("cpanel/Backgrounds/UniversalBack.tga",0,Top->Height-183) ||
               ((CurrentMode==Mode::Live || CurrentMode==Mode::Options) &&
                covered("cpanel/Backgrounds/LivePatch.bmp",7,Top->Height-153)))P.Active=false;
        }
        if (Buy->DesktopQueryVisible && in.TouchDown && P.Active &&
            Buy->DesktopQueryRect.Contains(in.TouchX,in.TouchY)) {
            Buy->DesktopQueryVisible=false;
            P.Active=false; // Dismissing a description cannot also place an object beneath it.
        }
        P.FromTouch = false; P.X = CursorX; P.Y = CursorY;
        const bool secondary=in.MouseRightHeld || in.MouseRightDown;
        P.Down = P.Active && in.TouchDown && !secondary; P.Held = P.Active && in.Touching && !secondary;
        P.Up = P.Active && in.TouchUp && !secondary; P.Click = P.Down;
        CutFollow = P.Active && !secondary; CutX = CursorX; CutY = CursorY;
        bool cameraModal=false;
#ifndef __3DS__
        const bool modal=(DesktopHover && DesktopHover->external_modal) || Buy->DesktopQueryVisible ||
            Live->_DesktopInventory.popup.open || Live->_DesktopCareer.popup.open || Live->_DesktopFame.popup.open;
        cameraModal=modal;
        const auto queue=OriginalPCUI::Get().QueueTiles(s.SelectedSim(),Top->Width,Top->Height);
        const bool cameraCanvas=P.Active && !odh_host::queue_tile_at(queue,CursorX,CursorY);
        auto fe=front::GameOptions::Get().FE;
        // Reuse the previous camera consumer's original HUD geometry for the
        // playable lower edge, plus this screen's actual hit/silhouette gate.
        const auto hud=odh_host::original_control_panel_layout(Top->Width,Top->Height);
        int playableHeight=CursorX<hud.left_patch.x+hud.left_patch.width?hud.left_patch.y:hud.base.y;
        if(needsPanel&&CursorX>=needsPanel->x&&CursorX<needsPanel->x+needsPanel->width)
            playableHeight=std::min(playableHeight,needsPanel->y);
        const auto edge=odh_host::lot_camera_edge(CursorX,CursorY,Top->Width,playableHeight);
        const auto motion=Camera.Update(in,cameraCanvas,modal,fe&&fe->Settings().EdgeScrolling,edge,dtMs);
        if(motion.horizontal || motion.vertical){
            st::Vector2 basis[2];s.World()->GetScrollBasis(basis);
            auto delta=(basis[0]*motion.horizontal+basis[1]*motion.vertical)*.70710678118f;
            if(motion.diagonal_edge)delta.Y*=.5f;
            s.World()->State.CenterTile(s.World()->State.CenterTile()+delta);
        }
        odh_host::OriginalCursorPolicyInput cursor;
        cursor.domain=cameraCanvas?odh_host::OriginalCursorDomain::world_canvas:odh_host::OriginalCursorDomain::ui;
        cursor.right_drag=motion.right_drag;
        if(motion.right_drag)cursor.right_drag_direction=motion.cursor;
        else {cursor.edge_scrolling_enabled=motion.cursor[0]||motion.cursor[1];cursor.edge=motion.cursor;}
        if(!modal)CameraCursor=std::string("Shared/cursors/")+odh_host::original_cursor_filename(odh_host::select_original_cursor(cursor).asset);
#endif
        const float panX = in.CircleX + in.CStickX, panY = in.CircleY + in.CStickY;
        if (!in.PointerCancelled && !cameraModal && (panX || panY)) s.ScrollScreen(panX * dtMs * .26f, -panY * dtMs * .26f);
        PanelOf(this)->PointerUpdate();
#ifndef __3DS__
        // The camera owns its captured drag; each mode supplies its real tool
        // cursor after input has updated selection/rotation for this frame.
        if(!modal && cameraCanvas && !motion.right_drag &&
           (Holder->Holding || (CurrentMode==Mode::Build && Build->Control) ||
            (!motion.cursor[0] && !motion.cursor[1])))
        {
            if(CurrentMode==Mode::Live && !Holder->Holding && !Pie.IsOpen)Live->UpdateDesktopCursor();
            CameraCursor=Pie.IsOpen?"Shared/cursors/Arrow.cur":PanelOf(this)->CursorArt();
        }
#endif
        return;
    }
    HandleButtons(in);
    if (LeaveRequested || QuitRequested) return;
    auto panel = PanelOf(this);

    // ---------------- cursor (circle pad) + scrolling (C-stick, edge scroll)
    float cx = in.CircleX, cy = in.CircleY;
    if (std::fabs(cx) < 0.15f) cx = 0;
    if (std::fabs(cy) < 0.15f) cy = 0;
    float sx = 0, sy = 0;
    if (cx != 0 || cy != 0) {
        ShowPointer(); // ADDED(sims)
        float px = 220.0f * (float)dtMs / 1000.0f;
        float nx = CursorX + cx * px, ny = CursorY - cy * px;
        // TS1 edge scrolling: pushing the cursor against the screen edge scrolls the lot
        const int EDGE = 6;
        if (nx < EDGE) { sx += nx - EDGE; nx = EDGE; }
        if (nx > Top->Width - 1 - EDGE) { sx += nx - (Top->Width - 1 - EDGE); nx = (float)(Top->Width - 1 - EDGE); }
        if (ny < EDGE) { sy += ny - EDGE; ny = EDGE; }
        if (ny > Top->Height - 1 - EDGE) { sy += ny - (Top->Height - 1 - EDGE); ny = (float)(Top->Height - 1 - EDGE); }
        CursorX = (int)std::lround(nx); CursorY = (int)std::lround(ny);
    }
    float kx = in.CStickX, ky = in.CStickY;
    if (std::fabs(kx) < 0.15f) kx = 0;
    if (std::fabs(ky) < 0.15f) ky = 0;
    if (kx != 0 || ky != 0) ShowPointer(); // ADDED(sims): the pad is in use again
    if (in.Down & BTN_A) ShowPointer();    // ADDED(sims): A clicks at the pointer: show where
    if (kx != 0 || ky != 0) { float px = 260.0f * (float)dtMs / 1000.0f; sx += kx * px; sy -= ky * px; }
    if (sx != 0 || sy != 0) s.ScrollScreen(sx * 1.6f, sy * 1.6f);

    // ---------------- touch
    if (in.TouchDown) {
        std::string id = HitAt(in.TouchX, in.TouchY);
        if (id == "view") {
            _ViewTouch = true; _ViewDragging = false;
            _TouchStartX = _TouchLastX = in.TouchX; _TouchStartY = _TouchLastY = in.TouchY;
        } else if (!id.empty()) {
            if (Log) *Log += "tap " + id + "\n";
            HandleTap(id);
            panel = PanelOf(this);
        }
    }
    bool dragScrolls = panel->ViewDragScrolls();
    if (_ViewTouch) {
        int tx, ty;
        int bx = std::max(ViewRect.X, std::min(ViewRect.Right() - 1, in.TouchX)), by = std::max(ViewRect.Y, std::min(ViewRect.Bottom() - 1, in.TouchY));
        ViewToTop(bx, by, tx, ty);
        if (in.Touching || in.TouchUp) { CutFollow = true; CutX = tx; CutY = ty; } // ADDED(sims): cut under the stylus
        if (dragScrolls) {
            if (in.Touching) {
                int dx = in.TouchX - _TouchLastX, dy = in.TouchY - _TouchLastY;
                if (std::abs(in.TouchX - _TouchStartX) + std::abs(in.TouchY - _TouchStartY) > 6) _ViewDragging = true;
                if (_ViewDragging && (dx || dy)) s.ScrollScreen(-dx / ViewScale, -dy / ViewScale);
                _TouchLastX = in.TouchX; _TouchLastY = in.TouchY;
            }
            if (in.TouchUp) {
                _ViewTouch = false;
                if (!_ViewDragging) {
                    CursorX = tx; CursorY = ty; CursorShown = false;
                    P.Active = true; P.FromTouch = true; P.X = tx; P.Y = ty; P.Click = true; P.Down = P.Up = true; P.Held = false;
                }
            }
        } else {
            P.Active = true; P.FromTouch = true; P.X = tx; P.Y = ty;
            P.Down = in.TouchDown; P.Held = in.Touching; P.Up = in.TouchUp; P.Click = in.TouchUp;
            CursorX = tx; CursorY = ty; CursorShown = false;
            if (in.TouchUp) _ViewTouch = false;
        }
    } else if (!P.Click) {
        // the cursor: A is the mouse button
        bool aHeld = (in.Held & BTN_A) != 0 || (in.Down & BTN_A) != 0;
        P.Active = true; P.FromTouch = false; P.X = CursorX; P.Y = CursorY;
        P.Down = (in.Down & BTN_A) != 0;
        P.Held = aHeld;
        P.Up = _ABtnHeld && !aHeld;
        P.Click = P.Down;
        _ABtnHeld = aHeld;
    }
    // ADDED(sims): live mode: the pointer fades out after POINTER_HIDE_MS without pad movement
    if (CursorShown && CurrentMode == Mode::Live && Time - PointerMovedAt >= POINTER_HIDE_MS + POINTER_FADE_MS) HidePointer();
    if (CursorShown) { CutFollow = true; CutX = CursorX; CutY = CursorY; }
    panel->PointerUpdate();
}

std::string LotScreen::DesktopCameraCursorPath() const {
#ifndef __3DS__
    return Desktop?CameraCursor:std::string{};
#else
    return {};
#endif
}

// ADDED(sims)
void LotScreen::ShowPointer() {
    CursorShown = true;
    PointerMovedAt = Time;
}

// ADDED(sims)
int LotScreen::PointerAlpha() const {
    if (!CursorShown) return 0;
    if (CurrentMode != Mode::Live) return 255;
    double idle = Time - PointerMovedAt - POINTER_HIDE_MS;
    if (idle <= 0) return 255;
    return std::max(0, std::min(255, (int)(255.0 * (1.0 - idle / POINTER_FADE_MS))));
}

// ======================================================================= drawing

void LotScreen::Draw() {
#ifndef __3DS__
    if(Desktop){
        if(!DesktopHover)DesktopHover.reset(new DesktopHoverState());
        DesktopHover->name.clear();DesktopHover->identity.clear();DesktopHover->context.clear();
        DesktopHover->help_title.clear();DesktopHover->help_description.clear();DesktopHover->help_illustration.clear();DesktopHover->help_objects={};DesktopHover->help_portrait=false;
    }
#endif
    TrackBudget();
    DrawTop();
    if (Desktop) DrawDesktop(); else DrawBottom();
#ifndef __3DS__
    if(Desktop && !CameraCursor.empty())if(const auto anchor=Camera.Anchor()){
        // Both Simitone pins draw exploreanchor at the retained press origin.
        // The original TS1 FAR selects this artwork as cpanel/ExploreAnchor.bmp.
        if(const auto* art=UIGraphics::Get().Image("cpanel/ExploreAnchor.bmp"))
            Top->Blit(art->Px.data(),art->Width,art->Height,(*anchor)[0]-art->Width/2,(*anchor)[1]-art->Height/2,true);
    }
#endif
#ifndef __3DS__
    if(Desktop && DesktopHover){
        auto& h=*DesktopHover;
        const bool modal=h.external_modal || Pie.IsOpen || HasModalSimDialog() || Buy->DesktopQueryVisible || Camera.Anchor().has_value();
        const std::string context=std::to_string(int(CurrentMode))+":"+h.context;
        // Original F5 button help is a separate250ms automatic illustrated
        // popup. Quick Tips does not disable clicks or unrelated tooltips.
        const bool settingsHelp=!h.help_title.empty()&&h.context.rfind("settings:",0)==0;
        const auto fe=front::GameOptions::Get().FE;
        const bool dismissed=settingsHelp&&h.dismissed_help_identity==h.identity&&h.dismissed_help_context==h.context;
        const bool helpEnabled=!settingsHelp||((fe&&fe->Settings().QuickTips)&&!dismissed);
        h.hover.SetDelayMs(settingsHelp?251:500); // Original integer elapsed >250ms.
        if(CurrentMode!=Mode::Options)h.manual_help.reset();
        bool automaticHelp=false;
        if(h.help_title.empty()&&!h.manual_help)
            h.hover.Draw(*Bottom,h.pointer,h.name,HoverKey(h.identity),HoverKey(context),h.selection,h.bounds,modal,true,true,h.foreground);
        else if(!h.help_title.empty())automaticHelp=bool(h.hover.Resolve(h.pointer,h.name,HoverKey(h.identity),HoverKey(context),h.selection,h.bounds,modal,true,helpEnabled));
        else h.hover.Clear();
        h.automatic_help_identity=automaticHelp?h.identity:std::string{};
        h.automatic_help_context=automaticHelp?h.context:std::string{};
        if(automaticHelp&&settingsHelp){h.dismissed_help_identity.clear();h.dismissed_help_context.clear();}
        if(automaticHelp&&h.manual_help&&h.manual_help->identity!=h.identity)h.manual_help.reset();
        const bool manualHelp=!modal&&h.manual_help.has_value();
        if(manualHelp&&!automaticHelp){
            const auto& help=*h.manual_help;h.help_title=help.title;h.help_description=help.description;
            h.help_illustration=help.illustration;h.context=help.context;h.help_objects={};h.help_portrait=false;
        }
        if(automaticHelp||manualHelp){
            auto content=FSO::Content::Content::Get();
            if(content){
                if(!h.help_view || h.help_root!=content->SimsCompleteDir){
                    h.help_pictures.clear();h.help_root=content->SimsCompleteDir;h.help_view.reset(new odh_host::CatalogInfoView(h.help_root));
                }
                odh_host::CatalogInfoContent info;info.name=h.help_title;info.description=h.help_description;
                odh::BitmapImage illustration;
                if(!h.help_illustration.empty())if(const auto* image=UIGraphics::Get().Image(h.help_illustration)){
                    illustration=original::Bitmap(*image);info.icon=&illustration;info.icon_rgba=true;
                }
                if(h.help_pictures.size()>30)h.help_pictures.clear(); // Before taking either picture reference.
                for(unsigned slot=0;slot<2;++slot)if(const auto guid=h.help_objects[slot]){
                    auto found=h.help_pictures.find(guid);
                    if(found==h.help_pictures.end()){
                        // Only the24 authored need illustrations are admitted; cap also
                        // protects future requests and cache failed decodes once.
                        found=h.help_pictures.emplace(guid,OriginalPCUI::BuildHelpThumbnail(guid)).first;
                    }
                    if(found->second.status==odh_host::CatalogObjectThumbnailStatus::ready){
                        if(slot)info.right_icon=&found->second.image;
                        else info.icon=&found->second.image;
                    }
                }
                if(h.help_portrait)if(auto* sim=S->SelectedSim();sim&&uint64_t(sim->ObjectID)==h.selection)
                    if(const auto* face=OriginalPCUI::Get().Face(sim,0)){
                        illustration=original::Bitmap(*face);info.icon=&illustration;
                    }
                info.live_help=odh_host::original_live_panel_help(h.context,h.help_portrait,h.help_objects);
                info.icon_rgba=info.icon_rgba||info.live_help;
                info.text_only_live=!info.icon&&!info.right_icon;
                const auto language=original::TextLanguage();info.live_language=language;const auto layout=odh_host::original_control_panel_layout(Bottom->Width,Bottom->Height);
                const auto measure=odh_host::catalog_info_text_measure(language,true,layout.scale);
                auto frame=original::Raster(*Bottom);
                h.help_view->draw(frame,info,odh_host::catalog_info_text_painter(language,true,layout.scale),measure,
                    &h.help_scroll,odh_host::catalog_info_scroll_painter(language,true,layout.scale),
                    odh_host::catalog_info_heading_painter(language,true,layout.scale));
                original::Copy(frame,*Bottom);
            }
        }
        if(h.pointer.TouchUp||h.pointer.PointerCancelled){h.pressed_automatic_help_identity.clear();h.pressed_automatic_help_context.clear();}
        h.pointer.TouchDown=h.pointer.TouchUp=h.pointer.MouseRightDown=false;h.pointer.WheelY=0;
    }
#endif
}

void LotScreen::DrawQueueTop(VMAvatar* sim) {
    #ifndef __3DS__
    if(Desktop){OriginalPCUI::Get().DrawQueue(*Top,sim);return;}
    #endif
    // TS1: the selected Sim's action queue, icons in a row at the top left, the active one first
    if (sim == nullptr || sim->Thread == nullptr) return;
    int x = Desktop ? 8 : 4, shown = 0;bool seenParent=false;
    for (size_t i = 0; i < sim->Thread->Queue.size() && shown < 8; i++) {
        auto& a = sim->Thread->Queue[i];
        if (int(i) < sim->Thread->ActiveQueueBlock || a->HasHiddenIcon()) continue;
        if(Desktop) {
            if(a->Mode==Engine::VMQueueMode::Idle || (i && a->Mode==Engine::VMQueueMode::ParentExit))continue;
            if(a->Mode==Engine::VMQueueMode::ParentIdle) {if(seenParent)continue;seenParent=true;}
        } else if (a->Mode == Engine::VMQueueMode::ParentExit || a->Mode == Engine::VMQueueMode::Idle) continue;
        int sz = Desktop ? 45 : (shown == 0 ? 34 : 26);
        Rect r(x, Desktop ? 8 : 4, sz, sz);
        if (Desktop) {
            // Reuse OriginalQueueView: two native 45px frames, first action active.
            const auto* frame = UIGraphics::Get().Image("CPanel/Buttons/IconFrame.bmp");
            if (frame && frame->Width == 90 && frame->Height == 45) {
                int sourceX = i == 0 ? 45 : 0;
                for (int y=0;y<45;++y) Top->Blit(frame->Px.data()+size_t(y)*90+sourceX,45,1,r.X,r.Y+y,true);
            }
        } else {
        Top->FillRoundRect(r, 4, RGBA(10, 16, 70, 210));
        Top->StrokeRoundRect(r.X, r.Y, r.W, r.H, 4, shown == 0 ? RGBA(0, 255, 255) : RGBA(110, 120, 200));
        }
        auto icon = IconFor(a->IconOwner());
        if (Desktop && icon && icon->Width > 0 && icon->Height > 0) {
            float scale = std::min({1.f,37.f/icon->Width,37.f/icon->Height});
            int w=std::max(1,int(icon->Width*scale)),h=std::max(1,int(icon->Height*scale));
            Top->BlitScaled(icon->Px.data(),icon->Width,icon->Height,Rect(r.X+(45-w)/2,r.Y+(45-h)/2,w,h),true);
        }
        else if (!Desktop && icon) Top->BlitScaled(icon->Px.data(), icon->Width, icon->Height, Rect(r.X + 2, r.Y + 2, sz - 4, sz - 4), true);
        else if (!Desktop) Top->FillCircle(r.X + sz / 2, r.Y + sz / 2, sz / 3, theme::Plumbob);
        const int priority=int(i)==sim->Thread->ActiveQueueBlock?sim->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable::Priority):a->Priority;
        if (Cancelled.count(a->UID) || (Desktop && a->Cancelled && priority<=0)) UIGraphics::Get().DrawImage(*Top, "Other/QueueCancel.bmp", Desktop ? r : Rect(r.X + 2, r.Y + 2, sz - 4, sz - 4));
        if (!Desktop && shown == 0) {
            std::string name = ToLatin1(a->Name);
            DrawTextShadow(*Top, FontId::Bold, 4, 41, Ellipsize(FontId::Bold, name, 190), RGBA(255, 255, 255), RGBA(0, 0, 0, 200));
        }
        x += Desktop ? 45 : sz + 3;
        shown++;
    }
}

void LotScreen::DrawHeadlines(Canvas& scene) {
    auto world = S->World();
    int zoom = (int)world->State.Zoom();
    int level = world->State.Level();
    for (auto e : S->VM()->Entities) {
        if (e == nullptr || e->Headline == nullptr || e->HeadlineRenderer == nullptr) continue;
        if (e->Position().Level > level || e->Position().TileX() < 0) continue;
        auto r = dynamic_cast<UIHeadlineRenderer*>(e->HeadlineRenderer.get());
        if (r == nullptr) continue;
        auto av = dynamic_cast<VMAvatar*>(e);
        // C# AvatarComponent: head bone + 0.66 tiles; ObjectComponent: position + 0.66
        float lift = av ? ((av->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable::PersonsAge) < 18) ? 1.5f : 2.05f) : 0.66f;
        if (av && av->IsPet()) lift = 1.3f;
        float x, y;
        if (!S->EntityScreenPos(e, x, y, lift)) continue;
        int w, h;
        r->Size(zoom, w, h);
        if (x < -w || y < -h || x > scene.Width + w || y > scene.Height + h) continue;
        // Sims: the balloon sits above the head and the plumbob; keep it on screen (TS1 balloons never leave the view)
        float zs = zoom == 3 ? 2.0f : (zoom == 2 ? 1.0f : 0.5f);
        int by = av ? (int)y - h / 2 - (int)(14 * zs) : (int)y;
        by = std::max(h / 2 + 2, by);
        r->Draw(scene, (int)x, by, zoom);
    }
}

void LotScreen::DrawPopups() {
    auto world = S->World();
    for (size_t i = 0; i < _Popups.size();) {
        auto& p = _Popups[i];
        double t = (Time - p.Start) / 2500.0;
        if (t >= 1.0) { _Popups.erase(_Popups.begin() + (long)i); continue; }
        float x = Top->Width / 2.0f, y = Top->Height / 2.0f;
        if (p.X > -100) {
            auto sp = TileToScreen(*world, st::Vector3(p.X, p.Y, (p.Head ? p.Z : (world->State.Level() - 1) * 2.95f / 3.0f) + p.Lift));
            x = sp.X * S->SceneScale(); y = sp.Y * S->SceneScale();
        } else if (p.SX >= 0) { x = (float)p.SX; y = (float)p.SY; }
        y -= (float)(t * 36.0);
        const int alpha = t < 0.7 ? 255 : int(255 * (1.0 - t) / 0.3);
        const auto headline = original::FormatMoneyHeadline(p.Amount, p.Head, alpha);
        const int w = TextWidth(FontId::Title, headline.text);
        int tx = std::max(2, std::min(Top->Width - w - 2, (int)x - w / 2)); // FIX(sims) QA2: never cut off at the screen edge
        DrawTextShadow(*Top, FontId::Title, tx, (int)y, headline.text, headline.color, RGBA(0, 0, 0, alpha * 3 / 4));
        i++;
    }
}

void LotScreen::DrawCursor() {
    int alpha = PointerAlpha(); // ADDED(sims): hidden after a touch / faded out after the idle time
    if (alpha <= 0) return;
    int hx = 0, hy = 0;
    auto cur = UIGraphics::Get().Cursor(PanelOf(this)->CursorArt(), hx, hy);
    if (cur == nullptr) cur = UIGraphics::Get().Cursor("Shared/cursors/hand.cur", hx, hy);
    if (cur != nullptr && alpha < 255) {
        for (int y = 0; y < cur->Height; y++)
            for (int x = 0; x < cur->Width; x++) Top->BlendA(CursorX - hx + x, CursorY - hy + y, cur->Px[(size_t)y * cur->Width + x], alpha);
    } else if (cur != nullptr) Top->Blit(cur->Px.data(), cur->Width, cur->Height, CursorX - hx, CursorY - hy, true);
    else {
        Top->FillTriangle(CursorX, CursorY, CursorX + 10, CursorY + 6, CursorX + 4, CursorY + 11, RGBA(255, 255, 255));
        Top->Line(CursorX, CursorY, CursorX + 10, CursorY + 6, RGBA(0, 0, 0));
        Top->Line(CursorX, CursorY, CursorX + 4, CursorY + 11, RGBA(0, 0, 0));
    }
}

void LotScreen::DrawLotScene(Canvas& scene) {
    auto& s = *S;
    auto& f = s.LastFrame();
    std::copy(f.Color.begin(), f.Color.end(), scene.Px.begin());
    auto sim = s.SelectedSim();
#ifndef __3DS__
    // The live cursor already owns the bounded pick and current interaction
    // eligibility. Highlight that same generation, never pick/query the VM again.
    if(Desktop && CurrentMode==Mode::Live && P.Active && !P.FromTouch && DesktopHover &&
       !DesktopHover->external_modal && !HasModalSimDialog() && !Pie.IsOpen &&
       CameraCursor=="Shared/cursors/LiveObjectAvail.cur") {
        auto* vm=s.VM();auto* target=vm->GetObjectById(Live->_DesktopPick.ID());
        if(target && !target->Dead && Live->_DesktopPick.Matches(target)) {
            std::vector<int16_t> ids;
            if(target->MultitileGroup)for(auto part:target->MultitileGroup->Objects) {
                if(part && !part->Dead)ids.push_back(part->ObjectID);
            } else ids.push_back(target->ObjectID);
            DrawObjectHover(*s.World(),f,scene,DesktopHover->object_mask,ids);
        }
    }
#endif
    bool live = CurrentMode == Mode::Live || CurrentMode == Mode::Options;
    if (live) {
        DrawHeadlines(scene);
        // plumbob over the selected Sim (TS1)
#ifndef __3DS__
        if(Desktop) DrawOriginalSelectedCrystal(scene,*s.World(),f,sim,Time);
        else
#endif
        if (sim != nullptr && sim->Position().Level <= s.World()->State.Level()) {
            float x, y;
            auto zoom = s.World()->State.Zoom();
            float zs = (zoom == FSO::LotView::WorldZoom::Near) ? 2.0f : (zoom == FSO::LotView::WorldZoom::Medium ? 1.0f : 0.5f);
            float lift = (sim->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable::PersonsAge) < 18) ? 1.5f : 2.05f;
            if (s.EntityScreenPos(sim, x, y, lift)) {
                int px = (int)x, py = (int)y - (int)(8 * zs);
                int w = std::max(3, (int)(6 * zs)), h = std::max(5, (int)(10 * zs));
                py += (int)(std::sin((float)Time / 300.0f) * 1.5f * zs);
                // TS1(sims): spinning, shaded 3-D plumbob (one turn every 2.4 s)
                DrawPlumbob(scene, (float)px, (float)py, (float)w, (float)h, (float)std::fmod(Time / 2400.0, 1.0) * 6.2831853f);
            }
        }
        if (!Desktop && Pie.IsOpen && PieMarkX >= 0) {
            int r = 6 + (int)(2 * std::sin((float)Time / 120.0f));
            scene.StrokeCircle((int)PieMarkX, (int)PieMarkY, r, RGBA(255, 255, 255, 230));
            scene.StrokeCircle((int)PieMarkX, (int)PieMarkY, r + 1, RGBA(0, 0, 0, 150));
        }
    }
}

void LotScreen::DrawTop() {
    auto& s = *S;
    const auto& frame = s.LastFrame();
    if (frame.Width * s.SceneScale() != Top->Width || frame.Height * s.SceneScale() != Top->Height ||
        frame.Color.size() != size_t(frame.Width) * frame.Height || Top->Px.size() != size_t(Top->Width) * Top->Height) return;
    if (s.SceneScale() == 2 && CurrentMode != Mode::Live && CurrentMode != Mode::Options) {
        ExpandLotScene2x(frame.Color.data(),Top->Px.data(),frame.Width,frame.Height);
    } else if (s.SceneScale() == 2) {
        // Borrow the output pixels for the small scene and its depth-tested
        // overlays. Expand backwards in the same allocation before drawing UI.
        Canvas scene;
        scene.Width = frame.Width;
        scene.Height = frame.Height;
        scene.Px.swap(Top->Px);
        struct RestorePixels {
            Canvas& output;
            Canvas& scene;
            ~RestorePixels() { output.Px.swap(scene.Px); }
        } restore{*Top, scene};
        DrawLotScene(scene);
        ExpandLotScene2x(scene.Px.data(),scene.Px.data(),scene.Width,scene.Height);
    } else {
        DrawLotScene(*Top);
    }
    if (CurrentMode == Mode::Live) DrawQueueTop(s.SelectedSim());
    DrawPopups();
    PanelOf(this)->DrawTop(*Top);
    if (!TopTip.empty() && Time < TopTipUntil) {
        int w = TextWidth(FontId::Bold, TopTip) + 10;
        int x = std::max(2, std::min(Top->Width - w - 2, TopTipX - w / 2)), y = std::max(2, std::min(Top->Height - 20, TopTipY));
        Top->FillRoundRect(x, y, w, 17, 4, RGBA(255, 255, 210, 235));
        Top->StrokeRoundRect(x, y, w, 17, 4, RGBA(0, 0, 0, 200));
        DrawText(*Top, FontId::Bold, x + 5, y + 2, TopTip, TopTipColor ? TopTipColor : RGBA(0, 0, 0));
    }
    // level indicator when not on the ground floor (TS1 shows the story in the view controls)
    if (s.World()->State.Level() > 1) {
        char lv[32]; snprintf(lv, sizeof lv, "%s", GameText::Get().UI(138, s.World()->State.Level() - 1, "Second Story").c_str());
        DrawTextShadow(*Top, FontId::Small, Top->Width - 4 - TextWidth(FontId::Small, lv), Top->Height - 16, lv, RGBA(255, 255, 255), RGBA(0, 0, 0, 180));
    }
    DrawCheatOverlays();
    DrawCursor();
    if (ShowStats && !StatsLine.empty()) {
        Top->FillRect(0, Top->Height - 14, Top->Width, 14, RGBA(0, 0, 0, 150));
        DrawText(*Top, FontId::Small, 4, Top->Height - 14, StatsLine, RGBA(200, 255, 200));
    }
}

// TS1(sims): the debug views the original's cheats switch on (app/ui/UICheatConsole):
//   tile_info       the tile under the cursor: position, story, room, floor pattern, wall segments, terrain, grass
//   route_balloons  a balloon on every waypoint of the routes the Sims are walking (draw_routes: the path as a line)
void LotScreen::DrawCheatOverlays() {
    auto& C = CheatState::Get();
    auto world = S->World();
    auto vm = S->VM();
    if (world == nullptr || vm == nullptr) return;
    if (C.RouteBalloons || C.DrawRoutes) {
        int level = world->State.Level();
        for (auto e : vm->Entities) {
            auto av = dynamic_cast<VMAvatar*>(e);
            if (av == nullptr || av->Thread == nullptr || av->Position().Level > level) continue;
            for (auto& frame : av->Thread->Stack) {
                auto rf = dynamic_cast<FSO::SimAntics::Engine::VMRoutingFrame*>(frame.get());
                if (rf == nullptr) continue;
                std::vector<st::Vector2> pts;
                float z = (av->Position().Level - 1) * 2.95f;
                if (rf->WalkTo) for (auto& p : *rf->WalkTo) pts.push_back(TileToScreen(*world, st::Vector3(p.X / 16.0f, p.Y / 16.0f, z)));
                for (auto& point : pts) point = point * float(S->SceneScale());
                if (C.DrawRoutes) {
                    float sx, sy;
                    if (S->EntityScreenPos(av, sx, sy, 0.0f) && !pts.empty()) Top->Line(int(sx * S->SceneScale()), int(sy * S->SceneScale()), (int)pts[0].X, (int)pts[0].Y, RGBA(255, 255, 0, 220));
                    for (size_t i = 1; i < pts.size(); i++) Top->Line((int)pts[i - 1].X, (int)pts[i - 1].Y, (int)pts[i].X, (int)pts[i].Y, RGBA(255, 255, 0, 220));
                }
                if (C.RouteBalloons) for (auto& p : pts) {
                    int x = (int)p.X, y = (int)p.Y - 10;
                    Top->FillCircle(x, y, 4, RGBA(255, 255, 255, 230));
                    Top->StrokeCircle(x, y, 4, RGBA(0, 0, 0, 200));
                    Top->Line(x, y + 4, x, y + 10, RGBA(255, 255, 255, 200));
                }
                break; // the innermost route only
            }
        }
    }
    if (C.TileInfo) {
        auto arch = vm->Context->Architecture();
        auto tile = world->State.WorldSpace.GetTileAtPosWithScroll(S->ScenePoint(CursorX, CursorY));
        int tx = (int)std::floor(tile.X), ty = (int)std::floor(tile.Y), lv = world->State.Level();
        char b[160];
        if (arch && tx >= 0 && ty >= 0 && tx < arch->Width && ty < arch->Height) {
            auto f = arch->GetFloor((int16_t)tx, (int16_t)ty, (int8_t)lv);
            auto w = arch->GetWall((int16_t)tx, (int16_t)ty, (int8_t)lv);
            int room = 0;
            if (lv - 1 < (int)arch->Rooms.size() && (size_t)(ty * arch->Width + tx) < arch->Rooms[(size_t)(lv - 1)].Map.size())
                room = (int)(arch->Rooms[(size_t)(lv - 1)].Map[(size_t)(ty * arch->Width + tx)] & 0xFFFF);
            int alt = arch->Terrain ? arch->GetTerrainHeight((int16_t)tx, (int16_t)ty) : 0;
            int grass = arch->Terrain ? arch->GetTerrainGrass((int16_t)tx, (int16_t)ty) : 0;
            snprintf(b, sizeof b, "tile %d,%d  story %d  room %d  floor %d  wall %02X  alt %d  grass %d%s", tx, ty, lv, room, (int)f.Pattern,
                     (int)w.Segments, alt, grass, vm->Context->IsUserOutOfBounds(FSO::LotView::Model::LotTilePos::FromBigTile((int16_t)tx, (int16_t)ty, (int8_t)lv)) ? "  (locked)" : "");
        } else snprintf(b, sizeof b, "tile %d,%d  (off the lot)", tx, ty);
        int w = TextWidth(FontId::Small, b) + 8;
        Top->FillRect(Top->Width - w - 2, Top->Height - 32, w, 14, RGBA(0, 0, 0, 170));
        DrawText(*Top, FontId::Small, Top->Width - w + 2, Top->Height - 32, b, RGBA(255, 255, 160));
    }
}

void LotScreen::DrawBottom() {
    Bottom->Clear(RGBA(4, 6, 40));
    Hits.clear();
    UIUCP::Draw(*this, *Bottom, GadgetRect);
    PanelOf(this)->Draw(*Bottom, PanelRect);
    // TS1: the active Sim's head in the middle of the pie (C# UIPieMenu draws the avatar head)
    if (Pie.IsOpen) Pie.Draw(*Bottom, S->SelectedSim() ? FaceFor(S->SelectedSim(), 0) : nullptr);
    if (!S->Dialogs.empty()) DrawDialog();
}

void LotScreen::DrawDialog() {
    auto& d = S->Dialogs.front();
    Bottom->Dim(0, 0, Bottom->Width, Bottom->Height, 100);
    auto lines = WrapText(FontId::Small, ToLatin1(d.Message), Bottom->Width - 48 - 40);
    if (lines.size() > 9) lines.resize(9);
    int h = 30 + std::max(40, (int)lines.size() * 13) + 34;
    Rect p(10, std::max(4, (Bottom->Height - h) / 2), Bottom->Width - 20, h);
    // TS1 dialog: dark blue frame with a light edge (Shared\Dlg\Gendlg.bmp is the TS1 tile); the object's icon left
    Bottom->FillRoundRect(p, 10, RGBA(14, 22, 96, 245));
    Bottom->StrokeRoundRect(p.X, p.Y, p.W, p.H, 10, RGBA(140, 150, 230));
    DrawTextCentered(*Bottom, FontId::Bold, Rect(p.X, p.Y + 5, p.W, 16), Ellipsize(FontId::Bold, ToLatin1(d.Title), p.W - 10), RGBA(255, 230, 120));
    int textX = p.X + 10;
    if (d.CallerID != 0) {
        auto caller = S->VM()->GetObjectById(d.CallerID);
        auto icon = caller ? IconFor(caller) : nullptr;
        if (icon) {
            Bottom->FillRoundRect(p.X + 8, p.Y + 26, 40, 40, 4, RGBA(0, 0, 30));
            Bottom->BlitScaled(icon->Px.data(), icon->Width, icon->Height, Rect(p.X + 10, p.Y + 28, 36, 36), true);
            textX = p.X + 54;
        }
    }
    int y = p.Y + 26;
    for (auto& l : lines) { DrawText(*Bottom, FontId::Small, textX, y, l, theme::TextBright); y += 13; }
    _DialogButtons.clear();
    std::vector<std::pair<std::string, int>> btns;
    if (d.HasYes) btns.push_back({ ToLatin1(d.Yes), 0 });
    if (d.HasNo) btns.push_back({ ToLatin1(d.No), 1 });
    if (d.HasCancel) btns.push_back({ ToLatin1(d.Cancel), 2 });
    if (btns.empty()) btns.push_back({ Str->Get(152, 0, "OK"), 0 });
    int bw = std::min(90, (p.W - 20) / (int)btns.size() - 6);
    int bx = p.X + (p.W - (int)btns.size() * (bw + 6)) / 2;
    for (auto& b : btns) {
        Rect r(bx, p.Bottom() - 28, bw, 22);
        if (!UIGraphics::Get().DrawButton(*Bottom, "cpanel/Buttons/ButtonTileDialog.BMP", ButtonState::Normal, r)) {
            Bottom->FillRoundRect(r, 6, theme::ButtonBg);
            Bottom->StrokeRoundRect(r.X, r.Y, r.W, r.H, 6, theme::PanelEdge);
        }
        DrawTextCentered(*Bottom, FontId::Bold, r, Ellipsize(FontId::Bold, b.first, r.W - 6), theme::TextBright);
        _DialogButtons.push_back({ r, b.second });
        bx += bw + 6;
    }
}

// ======================================================================= scripting

bool LotScreen::ScriptButtonPos(const std::string& id, int& x, int& y) const {
    for (auto& h : Hits) if (h.Id == id) { x = h.R.X + h.R.W / 2; y = h.R.Y + h.R.H / 2; return true; }
    return false;
}

bool LotScreen::ScriptObjectPos(const std::string& name, int& x, int& y, std::string* found) {
    auto& s = *S;
    auto world = s.World();
    std::string want = Lower(name);
    if (ViewRect.W <= 0) return false;
    for (auto e : s.VM()->Entities) {
        if (e == nullptr || e->WorldUI == nullptr || e == s.GotoObject) continue;
        if (Lower(e->ToString()).find(want) == std::string::npos) continue;
        float cx, cy;
        float lift = dynamic_cast<VMAvatar*>(e) ? 1.2f : 0.4f;
        if (!s.EntityScreenPos(e, cx, cy, lift)) continue;
        if (e->Position().Level != world->State.Level()) continue;
        cx *= s.SceneScale(); cy *= s.SceneScale();
        if (cx < 30 || cy < 30 || cx > Top->Width - 30 || cy > Top->Height - 30) {
            s.CenterOn(e);
            s.Draw();
            if (!s.EntityScreenPos(e, cx, cy, lift)) continue;
            cx *= s.SceneScale(); cy *= s.SceneScale();
        }
        int16_t want_id = e->ObjectID;
        int16_t leader = (e->MultitileGroup != nullptr) ? e->MultitileGroup->GetInteractionGroupLeader(e)->ObjectID : want_id;
        for (int rad = 0; rad <= 40; rad += 3) {
            for (int dy = -rad; dy <= rad; dy += 3) for (int dx = -rad; dx <= rad; dx += 3) {
                if (std::max(std::abs(dx), std::abs(dy)) != rad) continue;
                int tx = (int)cx + dx, ty = (int)cy + dy;
                if (tx < 0 || ty < 0 || tx >= Top->Width || ty >= Top->Height) continue;
                int bx, by; TopToView((float)tx, (float)ty, bx, by);
                int rtx, rty; ViewToTop(bx, by, rtx, rty);
                auto hit = s.PickDisplayObject(rtx, rty);
                if (hit.ObjectID == 0) continue;
                auto he = s.VM()->GetObjectById(hit.ObjectID);
                int16_t hl = (he && he->MultitileGroup) ? he->MultitileGroup->GetInteractionGroupLeader(he)->ObjectID : hit.ObjectID;
                if (hit.ObjectID == want_id || hl == leader) {
                    x = bx; y = by;
                    if (found) *found = e->ToString();
                    return true;
                }
            }
        }
    }
    return false;
}

std::string LotScreen::DescribeQueue() {
    std::string r;
    auto sim = S->SelectedSim();
    if (sim == nullptr || sim->Thread == nullptr) return r;
    r = sim->ToString() + ":";
    for (auto& a : sim->Thread->Queue)
        r += " [" + a->Name + (a->Callee ? " @ " + a->Callee->ToString() : std::string("")) + (Cancelled.count(a->UID) ? " (cancelled)" : "") + "]";
    auto p = sim->Position();
    char b[64]; snprintf(b, sizeof b, " pos (%d,%d)", p.TileX(), p.TileY());
    return r + b;
}

}
