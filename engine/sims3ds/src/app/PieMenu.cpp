/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
#include "app/PieMenu.h"
#include "app/Font.h"
#include "app/Theme.h"
#include "app/front/ResGraphics.h"
#include "app/front/TS1Font.h"
#ifndef __3DS__
#include "linux_pie_menu.hpp"
#include "app/ui/OriginalPresentation.h"
#include "app/ui/OriginalPiePresentation.h"
#include "app/ui/OriginalTextEncoding.h"
#include "app/ui/OriginalPCUI.h"
#endif
#include <cmath>
#include <algorithm>
#include <cctype>

namespace simapp {

static const int CENTER_R = 17;

void PieMenu::Open(const std::vector<PieChoice>& pie, const std::string& title, const Rect& area, int anchorX, int anchorY, bool titleUTF8) {
    Root.reset(new Node());
    Root->Category = true;
    // UIPieMenu constructor: split names on '/', build the category tree (C# Dictionary keeps insertion order)
    for (auto& item : pie) {
        std::vector<std::string> depth;
        std::string name = ToLatin1(item.Name);
#ifndef __3DS__
        if(Desktop)name=item.NameUTF8?item.Name:original::LegacyToUTF8(item.Name);
#endif
        size_t p = 0;
        while (true) {
            size_t e = name.find('/', p);
            if (e == std::string::npos) { depth.push_back(name.substr(p)); break; }
            depth.push_back(name.substr(p, e - p));
            p = e + 1;
        }
        Node* category = Root.get();
        for (size_t j = 0; j + 1 < depth.size(); j++) {
            Node* next = category->Find(depth[j]);
            if (next == nullptr) {
                std::unique_ptr<Node> n(new Node());
                n->Category = true; n->Name = depth[j]; n->Parent = category;
                next = n.get();
                category->Children.push_back(std::move(n));
            }
            next->Category=true;
            category = next;
        }
        {
            std::unique_ptr<Node> n(new Node());
            n->Name = depth.back(); n->Choice = item; n->Parent = category;
            category->Children.push_back(std::move(n));
        }
    }
    Current = Root.get();
    Title = ToLatin1(title);
#ifndef __3DS__
    if(Desktop)Title=titleUTF8?title:original::LegacyToUTF8(title);
#endif
    Area = area;
    IsOpen = true;
    Highlight = -1;
    PageSelection=0; Pressed=-1; PointerCaptured=false;
#ifndef __3DS__
    OriginalPointer.reset();OriginalCaptions.clear();
#endif
    AnchorX=anchorX>=0?anchorX:area.X+area.W/2;
    AnchorY=anchorY>=0?anchorY:area.Y+area.H/2;
    if(Desktop) front::ResGraphics::Get().Load("CPanel/PieFace1.bmp",PieArt);
    Layout();
#ifndef __3DS__
    HeadMotion.Reset();HeadRenderTarget={HeadX,HeadY};
#endif
}

void PieMenu::Layout() {
    if(Desktop) { LayoutDesktop(); return; }
    ItemRects.clear();
    CX = Area.X + Area.W / 2;
    CY = Area.Y + Area.H / 2 + 6;
    int n = (int)Current->Children.size();
    if (n == 0) return;
    const int h = 19;
    if (n <= 10) {
        // radial: items on an ellipse around the centre, first item at the top (TS1 / UIPieMenu.RenderMenu)
        float rx = (n <= 4) ? 70.0f : (n <= 6 ? 92.0f : 100.0f), ry = (n <= 4) ? 50.0f : 74.0f;
        if (n <= 2) ry = 40.0f;
        for (int i = 0; i < n; i++) {
            float ang = -1.5707963f + 6.2831853f * i / n;
            int px = CX + (int)std::lround(std::cos(ang) * rx);
            int py = CY + (int)std::lround(std::sin(ang) * ry);
            auto& node = Current->Children[(size_t)i];
            std::string label = node->Name + (node->Category ? " >" : "");
            int w = std::min(150, TextWidth(FontId::Bold, label) + 14);
            int x = px - w / 2;
            // items on the left/right side hang away from the centre
            float c = std::cos(ang);
            if (c > 0.2f) x = px - 12;
            else if (c < -0.2f) x = px - w + 12;
            x = std::max(Area.X + 2, std::min(Area.Right() - w - 2, x));
            int y = std::max(Area.Y + 18, std::min(Area.Bottom() - h - 2, py - h / 2));
            ItemRects.push_back(Rect(x, y, w, h));
        }
    } else {
        // many items: two columns
        int cols = 2, rows = (n + 1) / 2;
        int colW = (Area.W - 12) / cols;
        int top = Area.Y + 20;
        int rowH = std::min(h + 2, (Area.H - 24) / std::max(1, rows));
        for (int i = 0; i < n; i++) {
            int col = i / rows, row = i % rows;
            ItemRects.push_back(Rect(Area.X + 4 + col * (colW + 4), top + row * rowH, colW, rowH - 2));
        }
        CX = -100; // no centre button in list mode (B / tap outside goes back)
    }
}

PieMenu::Result PieMenu::Back() {
    if (Current != nullptr && Current->Parent != nullptr) {
        Current = Current->Parent;
        Highlight = -1; PageSelection=0; Pressed=-1;
        Layout();
        return Result::Navigated;
    }
    Close();
    return Result::Closed;
}

PieMenu::Result PieMenu::Activate(int index, PieChoice& out) {
    if (index < 0 || index >= (int)Current->Children.size()) return Result::None;
    Node* node = Current->Children[(size_t)index].get();
    if (node->Category) {
        Current = node;
        Highlight = -1; PageSelection=0; Pressed=-1;
        Layout();
        return Result::Navigated;
    }
    out = node->Choice;
    Close();
    return Result::Chosen;
}

PieMenu::Result PieMenu::Tap(int x, int y, PieChoice& out) {
    if (!IsOpen) return Result::None;
    if(Desktop) {
        int hit=HitDesktop(x,y);
        if(hit>=0) return Activate(hit,out);
        if(hit==-2) return Back();
        if(hit==-3 || hit==-4) {
            PageSelection=hit==-3?PageFirst-PageSize:PageFirst+PageSize;
            Highlight=-1; Pressed=-1; Layout(); return Result::Navigated;
        }
        Close(); return Result::Closed;
    }
    for (size_t i = 0; i < ItemRects.size(); i++) {
        Rect r = ItemRects[i];
        r.X -= 2; r.Y -= 2; r.W += 4; r.H += 4;
        if (r.Contains(x, y)) return Activate((int)i, out);
    }
    int dx = x - CX, dy = y - CY;
    if (dx * dx + dy * dy <= (CENTER_R + 4) * (CENTER_R + 4)) return Back();
    // tap anywhere else closes (UILotControl: a click while the pie is open removes it)
    Close();
    return Result::Closed;
}

PieMenu::Result PieMenu::Button(int dx, int dy, bool a, bool b, PieChoice& out) {
    if (!IsOpen) return Result::None;
    int n = (int)ItemRects.size();
    if (b) return Back();
    if (a) return (Highlight >= 0) ? Activate(Highlight, out) : Result::None;
    if ((dx != 0 || dy != 0) && n > 0) {
        if (Highlight < 0) Highlight = 0;
        else Highlight = (Highlight + (dx + dy > 0 ? 1 : n - 1)) % n;
        if(Desktop) {
            PageSelection=size_t(Highlight); Layout();
            if(Current==Root.get() && Highlight<int(ItemRects.size())) {
                auto r=ItemRects[Highlight]; HeadTargetX=r.X+r.W/2; HeadTargetY=r.Y+r.H/2;
            }
        }
    }
    return Result::None;
}

void PieMenu::Draw(Canvas& c, const Canvas* icon) {
    if (!IsOpen) return;
    if(Desktop) { DrawDesktop(c); return; }
    c.Dim(Area.X, Area.Y, Area.W, Area.H, 110);
    // title
    if (!Title.empty()) {
        std::string t = Ellipsize(FontId::Bold, Title, Area.W - 8);
        int w = TextWidth(FontId::Bold, t);
        c.FillRoundRect(Area.X + (Area.W - w) / 2 - 6, Area.Y + 1, w + 12, 16, 6, theme::PanelDark);
        DrawText(c, FontId::Bold, Area.X + (Area.W - w) / 2, Area.Y + 1, t, theme::TextBright);
    }
    // spokes + centre (the C# pie draws the Sim's head in the middle)
    if (CX > 0) {
        for (auto& r : ItemRects) c.Line(CX, CY, r.X + r.W / 2, r.Y + r.H / 2, RGBA(150, 190, 220, 110));
        c.FillCircle(CX, CY, CENTER_R, theme::PieCentre);
        c.StrokeCircle(CX, CY, CENTER_R, theme::PieEdge);
        if (icon != nullptr && icon->Width > 0) {
            Rect ir(CX - 13, CY - 13, 26, 26);
            c.BlitScaled(icon->Px.data(), icon->Width, icon->Height, ir, true);
        } else if (Current != nullptr && Current->Parent != nullptr) {
            DrawTextCentered(c, FontId::Bold, Rect(CX - CENTER_R, CY - CENTER_R, CENTER_R * 2, CENTER_R * 2), "<", theme::TextBright);
        } else { // a Sim or the floor: the plumbob
            c.FillTriangle(CX - 6, CY, CX + 6, CY, CX, CY - 11, theme::Plumbob);
            c.FillTriangle(CX - 6, CY, CX + 6, CY, CX, CY + 11, theme::PlumbobDark);
        }
    }
    for (size_t i = 0; i < ItemRects.size(); i++) {
        auto& r = ItemRects[i];
        auto& node = Current->Children[i];
        bool hl = (int)i == Highlight;
        c.FillRoundRect(r, 7, hl ? theme::PieItemHi : theme::PieItem);
        c.StrokeRoundRect(r.X, r.Y, r.W, r.H, 7, theme::PieEdge);
        std::string label = node->Name + (node->Category ? " >" : "");
        label = Ellipsize(FontId::Bold, label, r.W - 8);
        DrawTextCentered(c, FontId::Bold, r, label, hl ? theme::PieTextHi : theme::PieText);
    }
}

static std::string Lower(const std::string& s) {
    std::string r = s;
    for (auto& ch : r) ch = (char)std::tolower((unsigned char)ch);
    return r;
}

bool PieMenu::FindItem(const std::string& text, int& x, int& y) const {
    if (!IsOpen || Current == nullptr) return false;
    std::string t = Lower(text);
    for (size_t i = 0; i < Current->Children.size() && i < ItemRects.size(); i++) {
        if (ItemRects[i].W>0 && Lower(Current->Children[i]->Name).find(t) != std::string::npos) {
            x = ItemRects[i].X + ItemRects[i].W / 2; y = ItemRects[i].Y + ItemRects[i].H / 2;
            return true;
        }
    }
    return false;
}

std::vector<std::string> PieMenu::VisibleLabels() const {
    std::vector<std::string> r;
    if (!IsOpen || Current == nullptr) return r;
    for (auto& n : Current->Children) r.push_back(n->Name + (n->Category ? "/..." : ""));
    return r;
}


void PieMenu::SetHead(FSO::SimAntics::VMAvatar* sim) {
#ifndef __3DS__
    OriginalHead=sim;
#else
    (void)sim;
#endif
}
void PieMenu::SetHead(const cas::CASSimDesc& sim) {
    Head.reset();
    if(sim.Head.Empty() || sim.Body.Empty()) return;
    Head.reset(new cas::CASSimPreview());
    Head->Set(sim); Head->KeepHeadOnly();
}

#ifdef __3DS__
void PieMenu::LayoutDesktop() { Desktop=false; Layout(); }
void PieMenu::DrawDesktop(Canvas& c) { Desktop=false; Draw(c,nullptr); }
int PieMenu::HitDesktop(int,int) const { return -1; }
PieMenu::Result PieMenu::Pointer(const AppInput&,PieChoice&,double) { return Result::None; }
#else
void PieMenu::LayoutDesktop() {
    ItemRects.clear(); BackRect=PreviousRect=NextRect=Rect();
    if(!Current || Current->Children.empty()) return;
    using namespace front;
    std::vector<Node*> levels;
    for(Node* node=Current;node;node=node->Parent) levels.push_back(node);
    std::reverse(levels.begin(),levels.end());
    std::vector<std::vector<odh_host::PieMenuCaption>> captions;
    std::vector<size_t> path;
    for(size_t depth=0;depth<levels.size();++depth) {
        captions.emplace_back();
        for(const auto& child:levels[depth]->Children)captions.back().push_back({child->Name,original::TextLanguage()});
        if(depth+1<levels.size())for(size_t i=0;i<levels[depth]->Children.size();++i)
            if(levels[depth]->Children[i].get()==levels[depth+1]){path.push_back(i);break;}
    }
    PageSelection=std::min(PageSelection,Current->Children.size()-1);
    auto branch=odh_host::layout_linux_pie_menu_branch(Area.W,Area.H,
        {AnchorX-Area.X,AnchorY-Area.Y},captions,path,PageSelection);
    if(!branch)return;
    OriginalCaptions=captions.back();
    OriginalLayout=branch->layout;
    auto translate=[&](odh::PieMenuRect& r){if(r.width>0){r.x+=Area.X;r.y+=Area.Y;}};
    for(auto& r:OriginalLayout.items)translate(r);
    if(OriginalLayout.back_button)translate(*OriginalLayout.back_button);
    if(OriginalLayout.previous_page)translate(*OriginalLayout.previous_page);
    if(OriginalLayout.next_page)translate(*OriginalLayout.next_page);
    OriginalLayout.center[0]+=Area.X;OriginalLayout.center[1]+=Area.Y;
    HeadX=branch->head_center[0]+Area.X;HeadY=branch->head_center[1]+Area.Y;
    const auto target=branch->head_target.value_or(branch->head_center);
    HeadTargetX=target[0]+Area.X;HeadTargetY=target[1]+Area.Y;
    auto* layout=&branch->layout;
    auto rect=[&](const odh::PieMenuRect& r) { return Rect(r.x+Area.X,r.y+Area.Y,r.width,r.height); };
    for(const auto& r:layout->items) ItemRects.push_back(rect(r));
    if(layout->back_button) BackRect=rect(*layout->back_button);
    if(layout->previous_page) PreviousRect=rect(*layout->previous_page);
    if(layout->next_page) NextRect=rect(*layout->next_page);
    CX=layout->center[0]+Area.X; CY=layout->center[1]+Area.Y;
    PageFirst=layout->page_first; PageSize=layout->page_size;
}

int PieMenu::HitDesktop(int x,int y) const {
    for(size_t i=0;i<ItemRects.size();++i) if(ItemRects[i].W>0 && ItemRects[i].Contains(x,y)) return int(i);
    if(BackRect.W && BackRect.Contains(x,y)) return -2;
    if(PreviousRect.W && PreviousRect.Contains(x,y)) return -3;
    if(NextRect.W && NextRect.Contains(x,y)) return -4;
    return -1;
}
PieMenu::Result PieMenu::Pointer(const AppInput& input,PieChoice& out,double dtMs) {
    if(!Desktop || !IsOpen) return Result::None;
    if(input.MouseRightDown) { Close(); return Result::Closed; }
    // The click which opened this menu belongs to the lot underneath it.
    // Only a new press received by the open menu can dismiss it on release.
    if(input.TouchDown)PointerCaptured=true;
    const bool releasedCapture=input.TouchUp&&PointerCaptured;
    if(input.TouchUp)PointerCaptured=false;
    std::vector<odh_host::WindowPointerEvent> events;
    if(input.TouchDown)events.push_back({odh_host::WindowPointerKind::press,input.TouchX,input.TouchY});
    events.push_back({odh_host::WindowPointerKind::motion,input.TouchX,input.TouchY});
    if(input.TouchUp)events.push_back({odh_host::WindowPointerKind::release,input.TouchX,input.TouchY});
    size_t selected=PageSelection;
    auto result=OriginalPointer.process(OriginalLayout,events,input.PointerCancelled,selected);
    Highlight=OriginalPointer.hover_target()?int(selected):-1;
    if(input.PointerCancelled){PointerCaptured=false;return Result::None;}
    if(Current==Root.get()) {
        auto target=OriginalPointer.hover_target();
        HeadTargetX=target?(*target)[0]:HeadX;HeadTargetY=target?(*target)[1]:HeadY;
    }
    HeadRenderTarget=HeadMotion.Update({HeadX,HeadY},{HeadTargetX,HeadTargetY},dtMs);
    if(result.activated)return Tap((*result.activated)[0],(*result.activated)[1],out);
    if(releasedCapture && !result.consumed && HitDesktop(input.TouchX,input.TouchY)==-1){Close();return Result::Closed;}
    return Result::None;
}

void PieMenu::DrawDesktop(Canvas& c) {
    if(OriginalCaptions.empty())return;
    auto art=original::Bitmap(PieArt);
    std::optional<odh_host::PieMenuCaption> parent;
    if(Current && Current->Parent)parent=odh_host::PieMenuCaption{Current->Name,original::TextLanguage()};
    const size_t selected=Highlight>=0?size_t(Highlight):std::min(PageSelection,OriginalCaptions.size()-1);
    original::DrawDesktopPie(c,OriginalFrame,OriginalLayout,{HeadX,HeadY},OriginalCaptions,selected,parent,
        PieArt.Width==201 && PieArt.Height==201?&art:nullptr,OriginalPointer.pressed(),Highlight>=0,
        [&](Canvas& target){
            if(OriginalHead) {
                const auto& head=OriginalPCUI::Get().RenderPieHead(OriginalHead,{HeadX,HeadY},HeadRenderTarget);
                if(head.Width==200 && head.Height==200)target.Blit(head.Px.data(),head.Width,head.Height,HeadX-100,HeadY-100,true);
            } else if(Head && Head->Valid()) {
                float yaw=std::atan(float(HeadX-HeadRenderTarget[0])/100.f),pitch=std::atan(float(HeadY-HeadRenderTarget[1])/100.f);
                Head->DrawPieHead(target,HeadX,HeadY,yaw,pitch);
            }
    });
}

#endif

}
