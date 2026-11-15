/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims front end — see FamilyPicker.h. TS1(sims).
#include "app/front/FamilyPicker.h"
#include "app/front/FrontStrings.h"
#include "app/front/TS1Font.h"
#include <algorithm>
#include <cmath>
#ifndef __3DS__
#include "neighborhood_presentation.hpp"
#include "original_ui_art.hpp"
#include "app/ui/OriginalPresentation.h"
#include "app/ui/OriginalInput.h"
#include "app/ui/OriginalTextEncoding.h"
#include "content/ContentManager.h"
#endif

namespace simapp { namespace front {



#ifndef __3DS__
struct FamilyPicker::DesktopPresentation {
    odh_host::FamilyPickerPresentation View;
    std::vector<odh::FamilyEntry> Families;
    std::map<std::pair<size_t,uint32_t>,odh::BitmapImage> Faces;
    std::unique_ptr<odh_host::OriginalUiArt> Art;
};
void FamilyPicker::SyncDesktopPresentation() {
    if(!_Original)_Original=std::make_shared<DesktopPresentation>();
    auto& p=*_Original;auto& v=p.View;
    if(!p.Art){
        auto* content=FSO::Content::Content::Get();if(!content)throw odh::FormatError("family picker has no selected content root");
        p.Art=std::make_unique<odh_host::OriginalUiArt>(content->SimsCompleteDir,std::vector<std::string>{"Nbhd/PickBkg.bmp","Nbhd/FamilyPickItemBkg.bmp","Nbhd/PAFAddFamily.bmp",
            "Nbhd/PAFDeleteFamily.bmp","Nbhd/PAFMoveIn.bmp","Nbhd/NbhdTileBtn.bmp","CPanel/People/PeopleTemplate.bmp",
            "NghUI/Previous.bmp","NghUI/Next.bmp"},std::vector<std::string>{"Nbhd/PickBkgMagic.bmp"});
    }
    p.Families.clear();p.Faces.clear();v.choices.clear();
    for(const auto* family:_List){
        odh::FamilyEntry entry{};entry.chunk_id=family->ChunkID;entry.display_name=original::PersonalLabelUTF8(family->Name,family->SourceLanguage,family->TextUTF8,family->TextAmbiguous);
        entry.family.budget=family->Budget;entry.family.value_in_arch=family->NetWorth;
        entry.family.family_friends=family->Friends;
        for(const auto& member:family->Members){
            entry.family.member_guids.push_back(member.GUID);
            if(member.Face.Width)p.Faces.emplace(std::make_pair(p.Families.size(),member.GUID),original::Bitmap(member.Face));
        }
        p.Families.push_back(std::move(entry));
    }
    for(const auto& entry:p.Families)v.choices.push_back(&entry);
    v.scoped_portraits=[&p](const odh::FamilyEntry& family,uint32_t guid)->const odh::BitmapImage*{
        const auto row=size_t(&family-p.Families.data());
        auto it=p.Faces.find({row,guid});return it==p.Faces.end()?nullptr:&it->second;
    };
    v.selected=_Sel>=0&&_Sel<int(_List.size())?std::optional<size_t>{size_t(_Sel)}:std::nullopt;
    v.first=std::max(0,_Scroll);v.creating=!ImportMode;v.rename=false;
    v.magic_town=!ImportMode&&_M&&_M->CurrentArea==Area::MagicTown;
    v.magicoins=[this,&p](const odh::FamilyEntry& entry)->int{
        const auto row=size_t(&entry-p.Families.data());
        return row<_List.size()?_List[row]->MagiCoins:0;
    };
    v.can_move=[this](const odh::FamilyEntry& e){return ImportMode||TargetLot<0||(_M&&_M->CanMoveIn(e.chunk_id,TargetLot)==NhoodModel::MoveResult::Ok);};
    v.can_archive=[this](const odh::FamilyEntry& e){auto* f=_M?_M->Family(e.chunk_id):nullptr;return !ImportMode&&f&&f->House==0&&!_M->FamilyAway(e.chunk_id);};
}
void FamilyPicker::UpdateDesktop(const AppInput& in){
    _List=Families();SyncDesktopPresentation();auto& v=_Original->View;
    auto input=original::PointerInput(in,_OriginX,_OriginY);
    v.pointer=input.pointer_position;
    if(in.Down&(BTN_UP|BTN_DOWN)){
        int n=int(_List.size());if(n){_Sel=_Sel<0?(in.Down&BTN_DOWN?0:n-1):std::clamp(_Sel+(in.Down&BTN_DOWN?1:-1),0,n-1);_Scroll=_Sel/int(v.page_size)*int(v.page_size);}
    }
    if(in.WheelY)_Scroll=std::clamp(_Scroll-in.WheelY,0,std::max(0,int(_List.size())-int(v.page_size)));
    auto click=v.activation.click(input,[&](int x,int y){return v.target_at(x,y);});
    std::optional<size_t> action;
    if(click)action=v.target_at((*click)[0],(*click)[1]);
    if((in.Down&BTN_B)||(action&&*action==0)){if(OnCancel)OnCancel();return;}
    if(action&&*action>=100){_Sel=int(*action-100);return;}
    if(action&&*action==5){_Scroll=std::max(0,_Scroll-int(v.page_size));_Sel=_Scroll;return;}
    if(action&&*action==6){_Scroll+=int(v.page_size);_Sel=_Scroll;return;}
    if(action&&*action==1){if(OnCreate)OnCreate();return;}
    if(action&&*action==2){if(OnDelete&&SelectedFamily()>=0)OnDelete(SelectedFamily());return;}
    if((action&&*action==3)||(in.Down&BTN_A)){
        if(_Sel<0||_Sel>=int(_List.size())||!v.entry()||!v.can_move(*v.entry()))return;
        if(ImportMode){if(OnImport)OnImport(Imports[size_t(_Sel)].Path);}
        else if(OnMoveIn)OnMoveIn(SelectedFamily());
    }
}
#endif

void FamilyPicker::LoadArt() {
    if (_ArtLoaded && _ArtDesktop==Desktop) return;
    const float S=Desktop?1.0f:0.4f;
    _ArtDesktop=Desktop;
    auto& ui = ResGraphics::Get();
    ui.LoadScaled("kPickFamilyBkg", S, _Bkg);
    Canvas item;
    if (ui.Load("kPickFamilyItemBkg", item)) {
        // 3 frames stacked vertically (normal, pressed, selected) -> scale each
        int fh = item.Height / 3, dw = (int)(item.Width * S + 0.5f), dh = (int)(fh * S + 0.5f);
        _Item.Resize(dw, dh * 3);
        for (int f = 0; f < 3; f++) {
            Canvas fr, sc;
            Crop(item, fr, 0, f * fh, item.Width, fh);
            Resample(fr, sc, dw, dh);
            std::copy(sc.Px.begin(), sc.Px.end(), _Item.Px.begin() + (size_t)f * dw * dh);
        }
    }
    _Add.Load(ui, "kPickFamilyAdd", S);         _Add.Id = "add";
    _Delete.Load(ui, "kPickFamilyDelete", S);   _Delete.Id = "delete";
    _MoveIn.Load(ui, "kPickFamilyMoveIn", S);   _MoveIn.Id = "movein";
    _Cancel.Load(ui, Desktop?"Nbhd/NbhdTileBtn.bmp":"kPickFamilyCancel", S, Desktop?4:6); _Cancel.Id = "cancel";
    // the button slots of PickBkg (325..668, 530..590)
    _Add.X = (int)(330 * S);    _Add.Y = (int)(531 * S);
    _Delete.X = (int)(417 * S); _Delete.Y = (int)(531 * S);
    _MoveIn.X = (int)(509 * S); _MoveIn.Y = (int)(531 * S);
    _Cancel.X = (int)(30 * S);  _Cancel.Y = (int)(538 * S);
    if(Desktop) {
        _Add.X=322; _Add.Y=529; _Delete.X=414; _Delete.Y=529;
        _MoveIn.X=506; _MoveIn.Y=529; _Cancel.X=130; _Cancel.Y=529;
        ui.Load("CPanel/People/PeopleTemplate.bmp",_FaceFrame);
        _Previous.Load(ui,"NghUI/Previous.bmp",1.f); _Previous.Id="previous"; _Previous.X=686; _Previous.Y=484;
        _Next.Load(ui,"NghUI/Next.bmp",1.f); _Next.Id="next"; _Next.X=713; _Next.Y=484;
        // The original Cancel well is 140 pixels wide. Keep its corners, tile the middle.
        Canvas fitted(140*4,62);
        for(int frame=0;frame<4;++frame) {
            int fw=_Cancel.Texture.Width/4;
            if(fw<=0 || _Cancel.Texture.Height<=0) break;
            for(int y=0;y<62;++y) for(int x=0;x<140;++x) {
                int sx=x<fw/2?x:std::max(fw/2,fw-(140-x));
                int sy=std::min(y,_Cancel.Texture.Height-1);
                fitted.Px[y*fitted.Width+frame*140+x]=_Cancel.Texture.Px[sy*_Cancel.Texture.Width+frame*fw+std::min(fw-1,sx)];
            }
        }
        _Cancel.Texture=std::move(fitted);
    }
    auto& T = FrontStrings::Get();
    _Add.Tooltip = T.Get(128, 2, "Create New Family");
    _Delete.Tooltip = T.Get(128, 4, "Delete Family");
    _MoveIn.Tooltip = T.Get(128, 3, "Move In Family");
    _Cancel.Tooltip = T.Get(128, 5, "Cancel");
    _ArtLoaded = true;
}

std::vector<const FamilyInfo*> FamilyPicker::Families() const {
    if (!ImportMode) return _M ? _M->Bin() : std::vector<const FamilyInfo*>();
    std::vector<const FamilyInfo*> r;
    for (auto& e : Imports) r.push_back(&e.Info);
    return r;
}

void FamilyPicker::ShowImport(NhoodModel* model, std::vector<ImportEntry> entries) {
    Show(model, -1);
    ImportMode = true;
    Imports = std::move(entries);
    _List = Families();
    _MoveIn.Tooltip = FrontStrings::Get().Get(151, 6, "Import Family");
}

void FamilyPicker::Show(NhoodModel* model, int targetLot) {
    _M = model;
    ImportMode = false;
    Imports.clear();
    if (_ArtLoaded) _MoveIn.Tooltip = FrontStrings::Get().Get(128, 3, "Move In Family");
    TargetLot = targetLot;
    LoadArt();
    _List = model->Bin();
#ifndef __3DS__
    _Original.reset();
#endif
    _Sel = Desktop && targetLot<0?-1:0; _Scroll = 0;
    Open = true;
}

int16_t FamilyPicker::SelectedFamily() const {
    if (!_M) return -1;
    if (ImportMode) return (int16_t)((_Sel >= 0 && _Sel < (int)Imports.size()) ? _Sel : -1);
    auto list = _M->Bin();
    if (_Sel < 0 || _Sel >= (int)list.size()) return -1;
    return list[(size_t)_Sel]->ChunkID;

}

Rect FamilyPicker::RowRect(int i) const {
    const float S=Desktop?1.0f:0.4f;
    int h = _Item.Width > 0 ? _Item.Height / 3 : 26;
    if(Desktop) return Rect(41,76+(i-_Scroll)*68,690,68);
    return Rect((int)(35 * S), (int)(74 * S) + (i - _Scroll) * (h + (Desktop?5:2)), _Item.Width > 0 ? _Item.Width : 276, h);
}

void FamilyPicker::Update(const AppInput& input) {
#ifndef __3DS__
    if(Desktop){if(Open)UpdateDesktop(input);return;}
#endif
    AppInput in=input;
    in.TouchX-=_OriginX; in.TouchY-=_OriginY;
    const float factor=Desktop?2.5f:1.0f;
    if (!Open) return;
    _List = Families();
    int n = (int)_List.size();
    if (_Sel >= n) _Sel = n - 1;
    if (_Sel < 0 && n > 0 && !Desktop) _Sel = 0;
    _MoveIn.Disabled = _Delete.Disabled = (n == 0 || _Sel<0);
    if (in.Down & BTN_DOWN) _Sel = std::min(n - 1, _Sel + 1);
    if (in.Down & BTN_UP) _Sel = std::max(0, _Sel - 1);
    if(!Desktop || (in.Down&(BTN_UP|BTN_DOWN))) {
        if (_Sel < _Scroll) _Scroll = _Sel;
        if (_Sel >= _Scroll + Rows) _Scroll = _Sel - Rows + 1;
    }
    _Scroll = std::max(0, _Scroll);
    if(Desktop && n>Rows) {
        _Previous.Disabled=_Scroll==0; _Next.Disabled=_Scroll+Rows>=n;
        if(_Previous.Update(in)) _Scroll=std::max(0,_Scroll-Rows);
        if(_Next.Update(in)) _Scroll=std::min(n-Rows,_Scroll+Rows);
        if(in.WheelY) _Scroll=std::max(0,std::min(n-Rows,_Scroll-in.WheelY));
    }
    if (in.TouchDown) {
        for (int i = _Scroll; i < std::min(n, _Scroll + Rows); i++)
            if (RowRect(i).Contains(in.TouchX, in.TouchY)) {
                if (!Desktop && i == _Sel && ImportMode) { if (OnImport) OnImport(Imports[(size_t)i].Path); return; }
                if (!Desktop && i == _Sel && OnMoveIn) { OnMoveIn(_List[(size_t)i]->ChunkID); return; } // second tap: move in
                _Sel = i;
            }
        // scroll arrows (right of the list)
        if (!Desktop && n > Rows && Rect(int(300*factor), int(28*factor), int(18*factor), int(40*factor)).Contains(in.TouchX, in.TouchY)) _Scroll = std::max(0, _Scroll - 1);
        if (!Desktop && n > Rows && Rect(int(300*factor), int(160*factor), int(18*factor), int(40*factor)).Contains(in.TouchX, in.TouchY)) _Scroll = std::min(n - Rows, _Scroll + 1);
    }
    if (ImportMode) {
        if ((_MoveIn.Update(in) || (in.Down & BTN_A)) && OnImport && _Sel >= 0 && _Sel < (int)Imports.size()) { OnImport(Imports[(size_t)_Sel].Path); return; }
        if ((_Cancel.Update(in) || (in.Down & BTN_B)) && OnCancel) { OnCancel(); return; }
        return;
    }
    if (_Add.Update(in) && OnCreate) { OnCreate(); return; }
    if (_Delete.Update(in) && OnDelete && SelectedFamily() >= 0) { OnDelete(SelectedFamily()); return; }
    if ((_MoveIn.Update(in) || (in.Down & BTN_A)) && OnMoveIn && SelectedFamily() >= 0) { OnMoveIn(SelectedFamily()); return; }
    if ((_Cancel.Update(in) || (in.Down & BTN_B)) && OnCancel) { OnCancel(); return; }
}

void FamilyPicker::DrawBottom(Canvas& c) {
    if(!Open) return;
    LoadArt();
    if(!Desktop) { _OriginX=_OriginY=0; DrawPanel(c); return; }
    _OriginX=std::max(0,(c.Width-800)/2); _OriginY=std::max(0,(c.Height-600)/2);
    if(_DesktopCanvas.Width!=800) _DesktopCanvas.Resize(800,600);
    DrawPanel(_DesktopCanvas);
    c.Blit(_DesktopCanvas.Px.data(),800,600,_OriginX,_OriginY,false);
}

void FamilyPicker::DrawPanel(Canvas& c) {
    const float S=Desktop?1.0f:0.4f;
    const float factor=Desktop?2.5f:1.0f;
    auto px=[factor](int n) { return int(n*factor); };
    auto rect=[&](int x,int y,int w,int h) { return Rect(px(x),px(y),px(w),px(h)); };
    const auto small=Desktop?TS1Size::S14:TS1Size::S10;
    const auto normal=Desktop?TS1Size::S16:TS1Size::S11;
    if (!Open) return;
    _List = Families(); // the families may have changed since Update (delete, CAS): never keep stale pointers
    auto& T = FrontStrings::Get();
    if (_Bkg.Width > 0) c.Blit(_Bkg.Px.data(), _Bkg.Width, _Bkg.Height, 0, 0, false);
    else c.GradientV(0, 0, c.Width, c.Height, RGBA(10, 30, 120), RGBA(5, 10, 60));
#ifndef __3DS__
    if(Desktop){
        SyncDesktopPresentation();auto& p=*_Original;auto& v=p.View;
        auto frame=v.draw(p.Art.get(),original::TextLanguage(),original::LegacyToUTF8(ImportMode?T.Get(151,6,"Import Family"):T.Get(128,8,"SELECT A FAMILY")),
            original::LegacyToUTF8(ImportMode?T.Get(151,6,"Import Family"):T.Get(128,3,"Move In Family")),original::LegacyToUTF8(T.Get(128,4,"Delete Family")),
            original::LegacyToUTF8(T.Get(128,5,"Cancel")),original::LegacyToUTF8(T.Get(128,13,"Create a Family?")),original::LegacyToUTF8(T.Get(128,14,"You can't afford it.")),
            {});
        original::Copy(frame,c);return;
    }
#endif
    Text(c, Desktop?TS1Size::S20:TS1Size::S12, px(12), px(7), ImportMode ? T.Get(151, 6, "Import Family") : T.Get(128, 8, "SELECT A FAMILY"), RGBA(255, 255, 255), RGBA(0, 0, 40, 200));
    int n = (int)_List.size();
    if (n == 0 && ImportMode) {
        TextCentered(c, small, rect(8, 90, 288, 14), "No family files (.FAM) found.", RGBA(200, 220, 255));
    } else if (n == 0) {
        auto lines = Wrap(small, T.Get(128, 14, "You can create a new family by clicking on the Create a Family button at the bottom of the screen"), px(240));
        int y = px(90);
        for (auto& l : lines) { TextCentered(c, small, Rect(px(8),y,px(288),px(14)), l, RGBA(200, 220, 255)); y += TextH(small)+4; }
    }
    int fh = _Item.Width > 0 ? _Item.Height / 3 : 26;
    _Scroll = std::max(0, std::min(_Scroll, std::max(0, n - Rows)));
    for (int i = _Scroll; i < std::min(n, _Scroll + Rows); i++) {
        Rect r = RowRect(i);
        bool sel = i == _Sel;
        if (_Item.Width > 0) {
            const uint32_t* src = &_Item.Px[(size_t)(sel ? 2 : 0) * _Item.Width * fh];
            c.Blit(src, _Item.Width, fh, r.X, r.Y, true);
        } else c.FillRoundRect(r, 6, sel ? RGBA(60, 60, 140) : RGBA(0, 0, 60));
        auto f = _List[(size_t)i];
        std::string name = f->Name;
        Text(c, normal, r.X + px(8), r.Y + (r.H - TextH(normal)) / 2, Fit(normal, name, px(150)), sel ? RGBA(255, 255, 255) : RGBA(170, 200, 255), RGBA(0, 0, 30, 180));
        // Net Worth under the Simoleon head (x 598), Friends under the smiley (x 675)
        std::string nw = Simoleons(f->Budget);
        Text(c, small, (int)(640 * S) - TextW(small, nw), r.Y + (r.H - TextH(small)) / 2, nw, RGBA(255, 255, 255), RGBA(0, 0, 30, 180));
        std::string fr = std::to_string(f->Friends);
        Text(c, small, (int)(681 * S) - TextW(small, fr) / 2, r.Y + (r.H - TextH(small)) / 2, fr, RGBA(255, 255, 255), RGBA(0, 0, 30, 180));
    }
    if (n > Rows) {
        c.FillTriangle(px(303), px(44), px(315), px(44), px(309), px(34), RGBA(150, 200, 255));
        c.FillTriangle(px(303), px(184), px(315), px(184), px(309), px(194), RGBA(150, 200, 255));
    }
    if (!ImportMode) { _Add.Draw(c); _Delete.Draw(c); }
    _MoveIn.Draw(c); _Cancel.Draw(c);
}

void FamilyPicker::DrawTop(Canvas& top) {
    if (!Open) return;
    _List = Families();
    _Time += 33;
    auto& T = FrontStrings::Get();
    top.Dim(0, 0, top.Width, top.Height, 110);
    Rect p(10, 8, top.Width - 20, top.Height - 16);
    auto& art = FrontArt::Get();
    if (art.DialogTiler.Width > 0) { NineSlice(top, art.DialogTiler, p, 14); NineSlice(top, art.DialogTiler, Rect(p.X + 3, p.Y + 3, p.W - 6, p.H - 6), 14); }
    else top.FillRoundRect(p, 10, RGBA(0, 10, 80, 230));
    int sel = _Sel;
    if (sel < 0 || sel >= (int)_List.size()) {
        TextCentered(top, TS1Size::S16, Rect(p.X, p.Y + 20, p.W, 24), T.Get(128, 13, "Create a Family?"), RGBA(255, 216, 96), RGBA(0, 0, 30, 200));
        auto lines = Wrap(TS1Size::S12, T.Get(128, 14, "You can create a new family by clicking on the Create a Family button at the bottom of the screen"), p.W - 60);
        int y = p.Y + 60;
        for (auto& l : lines) { TextCentered(top, TS1Size::S12, Rect(p.X, y, p.W, 16), l, RGBA(255, 255, 255)); y += 17; }
        return;
    }
    auto f = _List[(size_t)sel];
    std::string title = Format(T.Get(134, 0, "The %s Family"), { f->Name });
    title = title.substr(0, title.find('\n'));
    Text(top, TS1Size::S20, p.X + 14, p.Y + 10, title, RGBA(255, 216, 96), RGBA(0, 0, 30, 220));
    std::string money = T.Get(128, 10, "Net Worth") + ": " + Simoleons(f->Budget) + "     " + T.Get(128, 11, "Family Friends") + ": " + std::to_string(f->Friends);
    Text(top, TS1Size::S11, p.X + 16, p.Y + 38, money, RGBA(170, 210, 255), RGBA(0, 0, 30, 200));
    // FIX(sims) QA2: the lot text below the members ("If you buy this lot, the family will have $9,500 to spend to build
    // a ...") was cut after 6 lines: wrap it first, keep up to 8 lines (ellipsis beyond) and leave the members that much room.
    std::vector<std::string> lotLines;
    LotInfo* lotForText = TargetLot >= 0 ? _M->Lot(TargetLot) : nullptr;
    if (lotForText) {
        lotLines = Wrap(TS1Size::S10, _M->Rollover(*lotForText, f), p.W - 40 - (lotForText->Thumb.Width > 0 ? 90 : 0));
        if (lotLines.size() > 8) { lotLines.resize(8); lotLines[7] = Fit(TS1Size::S10, lotLines[7] + "...", p.W - 130); }
    }
    int lotTextH = lotForText ? 10 + (int)lotLines.size() * 12 + 4 : 0;
    // the members: a little figure (adult / child, blue / pink as the CAS buttons), first name, biography
    int y = p.Y + 60;
    int perRow = (int)f->Members.size() > 4 ? 2 : 1;
    int colW = (p.W - 28) / perRow;
    for (size_t i = 0; i < f->Members.size(); i++) {
        auto& m = f->Members[i];
        int col = (int)i % perRow, x = p.X + 14 + col * colW;
        int yy = y + (int)(i / perRow) * 31;
        if (yy > p.Bottom() - (lotForText ? lotTextH + 28 : 24)) break;
        int fx = x + 20;
        if (m.Face.Width > 0) {
            // the portrait (Nbhd/PersonInfoBkg.bmp frames it in the original family view)
            Canvas small;
            Resample(m.Face, small, 22, 26);
            top.FillRect(x - 1, yy - 1, 24, 28, RGBA(0, 0, 40));
            top.Blit(small.Px.data(), small.Width, small.Height, x, yy, true);
            top.StrokeRect(x - 1, yy - 1, 24, 28, m.Female ? RGBA(240, 120, 170) : RGBA(90, 160, 255));
            fx = x + 28;
        } else {
            uint32_t body = m.Female ? RGBA(240, 120, 170) : RGBA(90, 160, 255);
            int hh = m.Child ? 9 : 13;
            top.FillCircle(x + 7, yy + 16 - hh, 4, RGBA(250, 220, 180));
            top.FillRoundRect(x + 2, yy + 20 - hh, 10, hh, 3, body);
        }
        Text(top, TS1Size::S12, fx, yy, m.FirstName, RGBA(255, 255, 255), RGBA(0, 0, 30, 200));
        int nx = fx + 4 + TextW(TS1Size::S12, m.FirstName);
        Text(top, TS1Size::S9, nx, yy + 3, m.Child ? "(child)" : "(adult)", RGBA(140, 170, 220));
        if (perRow == 1 && !m.Bio.empty())
            Text(top, TS1Size::S9, fx, yy + 15, Fit(TS1Size::S9, m.Bio, colW - (fx - x) - 10), RGBA(180, 200, 235));
    }
    if (lotForText) {
        auto lot = lotForText;
        {
            auto& lines = lotLines;
            int ly = p.Bottom() - 10 - (int)lines.size() * 12;
            if (lot->Thumb.Width > 0) {
                Canvas small;
                float sc = std::min(80.0f / lot->Thumb.Width, 46.0f / lot->Thumb.Height);
                Resample(lot->Thumb, small, (int)(lot->Thumb.Width * sc), (int)(lot->Thumb.Height * sc));
                top.Blit(small.Px.data(), small.Width, small.Height, p.Right() - 14 - small.Width, p.Bottom() - 12 - small.Height, true);
            }
            for (size_t i = 0; i < lines.size(); i++) { Text(top, TS1Size::S10, p.X + 16, ly, lines[i], i == 0 ? RGBA(255, 216, 96) : RGBA(255, 255, 255), RGBA(0, 0, 30, 200)); ly += 12; }
        }
    }
}

bool FamilyPicker::ButtonPos(const std::string& id, int& x, int& y) const {
    for (auto b : { &_Add, &_Delete, &_MoveIn, &_Cancel, &_Previous, &_Next }) {
        if (b->Id != id) continue;
        x = _OriginX + b->X + b->Width() / 2; y = _OriginY + b->Y + b->Height() / 2;
        return true;
    }
    return false;
}

bool FamilyPicker::RowPos(const std::string& name, int& x, int& y) {
    for (int i = 0; i < (int)_List.size(); i++) {
        if (_List[(size_t)i]->Name.find(name) == std::string::npos) continue;
        if (i < _Scroll) _Scroll = i;
        if (i >= _Scroll + Rows) _Scroll = i - Rows + 1;
        Rect r = RowRect(i);
        x = _OriginX + r.X + r.W / 2; y = _OriginY + r.Y + r.H / 2;
        return true;
    }
    return false;
}

}}
