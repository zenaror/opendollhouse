/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims front end — see SimDialogView.h
#include "app/front/SimDialogView.h"
#include "app/ui/OriginalDialogButtons.h"
#include "app/front/FrontAudio.h"
#include "app/front/FrontStrings.h"
#include "app/front/ResGraphics.h"
#include "app/front/TS1Font.h"
#include "app/Font.h"
#include "app/cas/CASSimPreview.h"
#include "simantics/engine/VMSuitProvider.h"
#include "app/LotScreen.h"
#include "app/LotSession.h"
#include "simantics/VM.h"
#include "simantics/entities/VMAvatar.h"
#include <algorithm>
#include <chrono>
#include <charconv>
#include "content/TS1PersonalText.h"
#include "content/ContentManager.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "files/formats/iff/chunks/STR.h"
#ifndef __3DS__
#include "app/ui/OriginalHover.h"
#endif
#include "simantics/engine/VMTS1Outfits.h"
#include "simantics/engine/VMTS1Travel.h"
#include "vitaboy/content/TS1AvatarProvider.h"
#include "vitaboy/Texture.h"
#include "files/formats/iff/chunks/BMP.h"
#include "app/ui/UIGraphics.h"
#ifndef __3DS__
#include "linux_vm_dialog.hpp"
#include "phonebook_candidates.hpp"
#include "app/ui/OriginalPresentation.h"
#include "app/ui/OriginalPCUI.h"
#include "app/ui/OriginalDialogArt.h"
#include "app/ui/OriginalTextEncoding.h"
#include "app/ui/OriginalDialogText.h"
#endif

namespace simapp { namespace front {

void SimDialogView::Layout(LotSession& s) {
    auto& d = s.Dialogs.front();
    auto& T = FrontStrings::Get();
    std::vector<std::string> labels;std::vector<int> codes;
    for(const auto& button:original::DialogButtonLabels(d.Type,d.Yes,d.No,d.Cancel,T.Get(152,0,"OK"),T.Get(152,2,"Yes"),T.Get(152,3,"No"),T.Get(152,1,"Cancel"))){labels.push_back(ToLatin1(button.Label));codes.push_back(button.Code);}
    bool changed=labels.size()!=_Btns.size() || codes!=_Codes;
    for(size_t i=0;!changed && i<labels.size();++i)changed=_Btns[i].Label!=labels[i];
    if(changed) {
        _Btns.clear();_Codes=codes;
        for(const auto& label:labels) { TextButton button;button.Label=label;_Btns.push_back(button); }
    }
    const int n=int(_Btns.size());
    const int bw=_DesktopWidth?(n>1?104:92):std::min(96,(300-8*(n-1))/n);
    const int gap=_DesktopWidth?std::max(8,std::min(bw/2,(_DesktopPanelWidth-n*bw)/(n+1))):8;
    int x=((_DesktopWidth?_DesktopWidth:320)-(n*bw+(n-1)*gap))/2;
    for(auto& button:_Btns) { button.R=Rect(x,_DesktopWidth?_DesktopButtonY:172,bw,_DesktopWidth?33:30);x+=bw+gap; }

}

// ---- TS1(sims): Clothing dialog (dialog type 6; FreeSO VMTS1PurchasableOutfitHelper lists)
void SimDialogView::LoadOutfits(LotSession& s) {
    auto& d = s.Dialogs.front();
    if (_OutfitAvatar == d.AvatarID && _OutfitType == d.OutfitType) return;
    _OutfitAvatar = d.AvatarID; _OutfitType = d.OutfitType;
    _Outfits.clear(); _OutfitPage = 0; _OutfitSel = -1;
    auto av = dynamic_cast<FSO::SimAntics::VMAvatar*>(s.VM()->GetObjectById(d.AvatarID));
    if (av == nullptr) return;
    auto list = FSO::SimAntics::Engine::Utils::VMTS1Outfits::ValidOutfits(av, d.OutfitType);
    _OutfitSel = FSO::SimAntics::Engine::Utils::VMTS1Outfits::GetSuitIndex(av, d.OutfitType);
    auto content = FSO::Content::TS1::TS1AvatarContent::Get();
    for (auto& o : list) {
        OutfitThumb t;
        auto at = o.BodyString.find("BODY=");
        t.Texture = (at == std::string::npos) ? o.BodyString : o.BodyString.substr(at + 5);
        if (content != nullptr && !_DesktopWidth) {
            if (auto tex = content->AvatarTextures.Get(t.Texture)) {
                // the body texture's upper part (the shirt) as the thumbnail, like the original dialog
                Canvas full;
                full.Resize(tex->Width, tex->Height);
                for (int i = 0; i < tex->Width * tex->Height; i++) {
                    uint32_t p = tex->Paletted ? tex->Palette[tex->Indices[(size_t)i]] : tex->Pixels[(size_t)i];
                    full.Px[(size_t)i] = (p & 0x00FFFFFFu) | 0xFF000000u;
                }
                int side = std::min(tex->Width, tex->Height);
                UIGraphics::Resample(full, Rect(0, 0, side, side), t.Img, 56, 56);
            }
        }
        _Outfits.push_back(std::move(t));
    }
}

bool SimDialogView::UpdateClothes(LotSession& s, const AppInput& in) {
#ifndef __3DS__
    if (_DesktopWidth) {
        if (_DesktopType != 6 || _Btns.size() != 4) return true;
        const int count = int(_Outfits.size());
        int hit = -1;
        for (size_t i = 0; i < _Btns.size(); ++i)
            if (_Btns[i].R.Contains(in.TouchX, in.TouchY)) hit = int(i);
        if (in.PointerCancelled) _DesktopPressed = -1;
        if (in.TouchDown) _DesktopPressed = hit;
        int choice = -1;
        if (in.TouchUp) { if (hit == _DesktopPressed) choice = hit; _DesktopPressed = -1; }
        if (in.TextCancel || (in.Down & BTN_B)) choice = 1;
        else if (in.TextAccept || (in.Down & BTN_A)) choice = 0;
        else if (in.Down & BTN_LEFT) choice = 2;
        else if (in.Down & BTN_RIGHT) choice = 3;
        if (choice >= 2) {
            if (count) { _OutfitSel = (_OutfitSel + count + (choice == 2 ? -1 : 1)) % count; FrontSound("ui_click"); }
        } else if (choice == 1 || (choice == 0 && _OutfitSel >= 0 && _OutfitSel < count && !_Btns[0].Disabled)) {
            FrontSound("ui_click");
            s.RespondDialog(choice == 0 ? 1 : 0, choice == 0 ? std::to_string(_OutfitSel) : "-1");
            _OutfitAvatar = _OutfitType = -1; _Outfits.clear(); _Btns.clear(); _DesktopPressed = -1; _DesktopWidth = 0; _DesktopType = 0;
        }
        return true;
    }
#endif
    int n = (int)_Outfits.size(), pages = std::max(1, (n + 7) / 8);
    if (in.Down & BTN_RIGHT) _OutfitPage = std::min(pages - 1, _OutfitPage + 1);
    if (in.Down & BTN_LEFT) _OutfitPage = std::max(0, _OutfitPage - 1);
    if (in.Down & BTN_B) { FrontSound("ui_click"); s.RespondDialog(0); return true; } // 0 = cancel (FreeSO)
    if (in.TouchDown) {
        for (size_t i = 0; i < _OutfitRects.size(); i++) {
            if (!_OutfitRects[i].Contains(in.TouchX, in.TouchY)) continue;
            int idx = _OutfitPage * 8 + (int)i;
            if (idx >= n) break;
            FrontSound("ui_click");
            s.RespondDialog(1, std::to_string(idx)); // the index in temp 0/1 (VMDialogPrivateStrings TS1Clothes)
            return true;
        }
        if (Rect(4, 100, 20, 40).Contains(in.TouchX, in.TouchY)) _OutfitPage = std::max(0, _OutfitPage - 1);
        if (Rect(296, 100, 20, 40).Contains(in.TouchX, in.TouchY)) _OutfitPage = std::min(pages - 1, _OutfitPage + 1);
        if (Rect(120, 210, 80, 24).Contains(in.TouchX, in.TouchY)) { FrontSound("ui_click"); s.RespondDialog(0); return true; }
    }
    if ((in.Down & BTN_A) && _OutfitSel >= 0 && _OutfitSel < n) { s.RespondDialog(1, std::to_string(_OutfitSel)); return true; }
    return true;
}

void SimDialogView::DrawClothes(LotSession& s, Canvas& top, Canvas& bottom) {
    _DesktopWidth = &top == &bottom ? top.Width : 0;
    LoadOutfits(s);
#ifndef __3DS__
    if (&top == &bottom) {
        _DesktopWidth = top.Width; _DesktopType = 6;
        auto& art = FrontArt::Get(); art.Load(ResGraphics::Get());
        const Rect panel((top.Width - 314) / 2, (top.Height - 400) / 2, 314, 400);
        DrawOriginalDialogPanel(top, panel);
        const auto& dialog = s.Dialogs.front();
        auto avatar = dynamic_cast<FSO::SimAntics::VMAvatar*>(s.VM()->GetObjectById(dialog.AvatarID));
        bool ready = false;
        if (avatar && !_Outfits.empty()) {
            _OutfitSel = std::clamp(_OutfitSel, 0, int(_Outfits.size()) - 1);
            const auto choices = FSO::SimAntics::Engine::Utils::VMTS1Outfits::ValidOutfits(avatar, dialog.OutfitType);
            const auto body = FSO::SimAntics::Engine::VMSuitProvider::BodyStringsOf(avatar);
            if (size_t(_OutfitSel) < choices.size() && body.size() > 14) {
                // Isolated CAS renderer: selecting a preview must never dress the
                // world avatar or mutate its character/outfit before confirmation.
                cas::CASSimDesc person;
                person.Age = body[0] == "child" ? cas::CASAge::Child : cas::CASAge::Adult;
                person.Gender = body[12] == "female" ? cas::CASGender::Female : cas::CASGender::Male;
                person.Skin = body[14] == "drk" ? cas::CASSkin::Dark : body[14] == "med" ? cas::CASSkin::Medium : cas::CASSkin::Light;
                const auto parse = [](const std::string& value, const std::string& marker) {
                    cas::CASOutfit outfit; const auto comma = value.find(','); outfit.Mesh = value.substr(0, comma);
                    const auto texture = value.find(marker); if (texture != std::string::npos) outfit.Texture = value.substr(texture + marker.size());
                    return outfit;
                };
                person.Body = parse(choices[size_t(_OutfitSel)].BodyString, "BODY=");
                person.Head = parse(body[2], "HEAD-HEAD=");
                static cas::CASSimPreview preview;
                bool head = false, dressed = false; preview.Set(person, &head, &dressed);
                static auto last = std::chrono::steady_clock::now();
                const auto now = std::chrono::steady_clock::now();
                preview.Update(std::clamp(std::chrono::duration<double,std::milli>(now-last).count(),0.0,100.0)); last=now;
                ready = preview.Valid() && head && dressed;
                if (ready) preview.Draw(top, Rect(panel.X + 40, panel.Y + 55, panel.W - 80, 292),
                    panel.X + panel.W / 2.f, panel.Y + 342.f, person.Age == cas::CASAge::Child ? 176.f : 250.f, 0.f, true);
            }
        }
        auto& strings = FrontStrings::Get();
        auto frame = original::Raster(top);
        odh_host::TextOverlayStyle title; title.x = panel.X + 6; title.y = panel.Y + 10;
        title.width = panel.W - 12; title.height = 48; title.font_size_px = 21; title.centered = true; title.padding = 0;
        title.background = {0,0,0,0}; title.foreground = {255,255,255,255};
        odh_host::draw_text_overlay(frame, original::LegacyToUTF8(strings.Get(220, 4, "Please Select Outfit")), original::TextLanguage(), title);
        original::Copy(frame, top);
        _Btns.resize(4);
        _Btns[0].R = Rect(panel.X + 36, panel.Y + 358, 100, 33);
        _Btns[1].R = Rect(panel.X + 171, panel.Y + 358, 100, 33);
        _Btns[0].Label = strings.Get(152, 0, "OK"); _Btns[1].Label = strings.Get(152, 1, "Cancel");
        _Btns[0].Disabled = !ready;
        for (int i = 0; i < 2; ++i) { _Btns[i].Selected = _DesktopPressed == i; _Btns[i].Draw(top); }
        _Btns[2].R = Rect(panel.X + 28, panel.Y + 180, 18, 34);
        _Btns[3].R = Rect(panel.Right() - 46, panel.Y + 180, 18, 34);
        for (int i = 2; i < 4; ++i) UIGraphics::Get().DrawButton(top,
            i == 2 ? "Nbhd/DesignCharSkinsLeftBtn.bmp" : "Nbhd/DesignCharSkinsRightBtn.bmp",
            _DesktopPressed == i ? ButtonState::Pressed : ButtonState::Normal, _Btns[i].R);
        return;
    }
#endif
    auto& d = s.Dialogs.front();
    auto& T = FrontStrings::Get();
    auto& art = FrontArt::Get();
    auto panel = [&](Canvas& c, const Rect& r) {
        if (art.DialogTiler.Width > 0) { NineSlice(c, art.DialogTiler, r, 14); NineSlice(c, art.DialogTiler, Rect(r.X + 3, r.Y + 3, r.W - 6, r.H - 6), 14); }
        else c.FillRoundRect(r, 10, RGBA(14, 22, 96, 245));
    };
    std::string title = ToLatin1(d.Title.empty() ? std::string("Clothing") : d.Title);
    top.Dim(0, 0, top.Width, top.Height, 150);
    Rect p(8, 40, top.Width - 16, 150);
    panel(top, p);
    TextCentered(top, TS1Size::S16, Rect(p.X, p.Y + 8, p.W, 22), title, RGBA(255, 216, 96), RGBA(0, 0, 30, 220));
    auto lines = Wrap(TS1Size::S12, ToLatin1(d.Message), p.W - 30);
    int y = p.Y + 38;
    for (size_t i = 0; i < lines.size() && i < 5; i++) { TextCentered(top, TS1Size::S12, Rect(p.X, y, p.W, 16), lines[i], RGBA(255, 255, 255), RGBA(0, 0, 30, 200)); y += 16; }
    // bottom: the outfits
    bottom.Dim(0, 0, bottom.Width, bottom.Height, 170);
    Rect bp(4, 4, bottom.Width - 8, 232);
    panel(bottom, bp);
    TextCentered(bottom, TS1Size::S12, Rect(bp.X, bp.Y + 6, bp.W, 16), title, RGBA(255, 216, 96), RGBA(0, 0, 30, 220));
    _OutfitRects.clear();
    int n = (int)_Outfits.size(), pages = std::max(1, (n + 7) / 8);
    for (int k = 0; k < 8; k++) {
        int idx = _OutfitPage * 8 + k;
        Rect r(32 + (k % 4) * 66, 30 + (k / 4) * 88, 60, 82);
        _OutfitRects.push_back(r);
        if (idx >= n) continue;
        bottom.FillRoundRect(r, 6, idx == _OutfitSel ? RGBA(40, 90, 190) : RGBA(0, 0, 40));
        auto& o = _Outfits[(size_t)idx];
        if (o.Img.Width > 0) bottom.Blit(o.Img.Px.data(), o.Img.Width, o.Img.Height, r.X + 2, r.Y + 2, false);
        if (idx == _OutfitSel) bottom.StrokeRoundRect(r.X, r.Y, r.W, r.H, 6, RGBA(0, 255, 255));
        std::string nm = o.Texture;
        auto u = nm.find('_'); if (u != std::string::npos) nm = nm.substr(u + 1);
        // FIX(sims) QA3: long names ("beach...") use two lines instead of an ellipsis
        if (TextW(TS1Size::S9, nm) <= r.W - 2) TextCentered(bottom, TS1Size::S9, Rect(r.X, r.Bottom() - 22, r.W, 12), nm, RGBA(220, 230, 255));
        else {
            size_t cut = nm.size() / 2;
            while (cut + 1 < nm.size() && TextW(TS1Size::S9, nm.substr(0, cut + 1)) <= r.W - 2) cut++;
            TextCentered(bottom, TS1Size::S9, Rect(r.X, r.Bottom() - 24, r.W, 11), nm.substr(0, cut), RGBA(220, 230, 255));
            TextCentered(bottom, TS1Size::S9, Rect(r.X, r.Bottom() - 13, r.W, 11), Fit(TS1Size::S9, nm.substr(cut), r.W - 2), RGBA(220, 230, 255));
        }
    }
    if (n == 0) TextCentered(bottom, TS1Size::S10, Rect(bp.X, 100, bp.W, 14), "No outfits to choose from.", RGBA(220, 230, 255));
    if (_OutfitPage > 0) bottom.FillTriangle(8, 120, 20, 104, 20, 136, RGBA(150, 200, 255));
    if (_OutfitPage + 1 < pages) bottom.FillTriangle(312, 120, 300, 104, 300, 136, RGBA(150, 200, 255));
    TextButton cancel; cancel.Label = T.Get(152, 1, "Cancel"); cancel.R = Rect(120, 210, 80, 24);
    cancel.Draw(bottom);
}

// ---- FIX(sims): the spellbook (dialog type 12)
static const int kSpellRowY = 50, kSpellRowH = 22;

#ifndef __3DS__
namespace {
struct BookIngredient { Canvas icon; std::string name; uint32_t guid=0; };
struct BookRecipe { int recipe=0; std::array<BookIngredient,3> ingredients; std::string title,description,art; };
struct DesktopBook {
    std::string key,caption,back,next,page;
    std::vector<BookRecipe> rows;
    AppInput pointer;
    OriginalHover hover;
};
Rect BookPanel(const Canvas& canvas) { return Rect((canvas.Width-600)/2,std::max(0,(canvas.Height-600)/2+10),600,380); }
int BookNumber(const std::string& value,int base=10) {
    unsigned number=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number,base);
    return parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size()&&number<=0x7fffffffU?int(number):-1;
}
std::string BookText(FSO::Files::Formats::IFF::Chunks::STR* table,int index) {
    if(!table||index<0)return {};
    size_t language=original::TextLanguage()-1;
    if(language>=table->LanguageSets.size()||table->LanguageSets[language].Strings.empty())language=0;
    if(language>=table->LanguageSets.size()||size_t(index)>=table->LanguageSets[language].Strings.size())return {};
    return original::PersonalToUTF8(table->LanguageSets[language].Strings[size_t(index)].Value,uint8_t(language+1),table->SourceFormat==-1||table->SourceFormat==-4);
}
DesktopBook& BookData(LotSession& session,int avatarID) {
    using namespace FSO::Files::Formats::IFF::Chunks;
    using FSO::SimAntics::Engine::Utils::VMTS1Travel;
    static DesktopBook data;
    auto content=FSO::Content::Content::Get();
    auto vm=session.VM();
    auto avatar=vm?dynamic_cast<FSO::SimAntics::VMAvatar*>(vm->GetObjectById(avatarID)):nullptr;
    const bool child=avatar&&avatar->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable::PersonsAge)<18;
    FSO::SimAntics::VMEntity* controller=nullptr;
    if(vm)for(auto entity:vm->Entities)if(entity&&!entity->Dead&&entity->Object&&entity->Object->OBJ&&entity->Object->OBJ->GUID==VMTS1Travel::MAGIC_CONTROLLER_GUID){controller=entity;break;}
    std::array<uint16_t,6> bits{};
    if(controller){const auto& attributes=controller->AttributeData();for(size_t i=0;i<bits.size()&&i<attributes.size();++i)bits[i]=uint16_t(attributes[i]);}
    std::string key=(content?content->SimsCompleteDir:"")+":"+std::to_string(original::TextLanguage())+":"+std::to_string(reinterpret_cast<uintptr_t>(content?content->WorldObjects.get():nullptr))+":"+std::to_string(child)+":"+std::to_string(avatarID)+":"+session.FamilyName;
    for(auto value:bits)key+=":"+std::to_string(value);
    if(data.key==key)return data;
    data.key=key;data.rows.clear();data.caption.clear();data.back.clear();data.next.clear();data.page.clear();data.hover.Clear();
    if(!content||!avatar||avatar->IsPet()||!controller)return data;
    try {
        FSO::Files::Formats::IFF::IffFile text(content->SimsCompleteDir+"/GameData/magic.iff");
        auto label=[&](int table,int index){return BookText(text.Get<STR>(uint16_t(table)),index);};
        data.caption=Format(label(1,0),{original::PersonalLabelUTF8(session.FamilyName,session.FamilyNameSourceLanguage,session.FamilyNameUTF8,session.FamilyNameAmbiguous)});
        data.back=label(1,2);data.next=label(1,3);data.page=label(1,4);
        auto definition=content->WorldObjects->Get(VMTS1Travel::MAGIC_CONTROLLER_GUID,true);
        if(!definition||!definition->Resource)return data;
        auto recipes=definition->Resource->Get<STR>(child?4:3);
        auto order=definition->Resource->Get<STR>(child?16:15);
        auto lookup=definition->Resource->Get<STR>(7);
        if(!recipes||!order||!lookup||order->LanguageSets.empty())return data;
        // Original UI order is the canonical numeric set, independent of UI
        // language. Recipe formulas retain the selected resource set, like GSC38.
        for(size_t position=1;position<order->LanguageSets[0].Strings.size()&&position<=128;++position) {
            const int id=BookNumber(order->GetString(int(position),STRLangCode::EnglishUS));
            if(id<1||id>31)continue;
            const auto recipe=recipes->GetString(id);std::array<int,3> ingredients{};size_t start=0;bool valid=true;
            for(int n=0;n<3;++n){const auto end=recipe.find(',',start);const auto piece=recipe.substr(start,end==std::string::npos?std::string::npos:end-start);ingredients[n]=BookNumber(piece);if(ingredients[n]<1||ingredients[n]>31||(n<2&&end==std::string::npos)||(n==2&&end!=std::string::npos)){valid=false;break;}start=end+1;}
            if(!valid)continue; // Explicit TBD50/51 rows are not recipes.
            BookRecipe row;row.recipe=id;
            // MagicMasterSpells Has Made Spell/Recipe4101: adult boundary15,
            // child bank2, one-based flag operands. Never write progress here.
            const bool known=VMTS1Travel::SpellbookRecipeKnown(controller->AttributeData(),child,id);
            const bool spell=!child&&id>=13;
            row.title=label(child?13:12,known?id:(spell?24:0));
            row.description=label(child?9:8,known?id:(spell?24:0));
            row.art=child?"cpanel/Backgrounds/SPELLBOOK_CharmMakerKid.bmp":spell?"cpanel/Backgrounds/SPELLBOOK_WandCharger.bmp":"cpanel/Backgrounds/SPELLBOOK_CharmMakerAdult.bmp";
            for(int n=0;n<3;++n) {
                const auto raw=lookup->GetString(ingredients[n],STRLangCode::EnglishUS);uint32_t guid=0;
                const auto parsed=std::from_chars(raw.data(),raw.data()+raw.size(),guid,16);
                if(parsed.ec!=std::errc{}||parsed.ptr!=raw.data()+raw.size())continue;
                auto object=content->WorldObjects->Get(guid,true);if(!object||!object->OBJ||!object->Resource)continue;
                auto& ingredient=row.ingredients[n];ingredient.guid=guid;
                if(auto strings=object->Resource->Get<CTSS>(object->OBJ->CatalogStringsID)) {
                    const auto personal=FSO::Content::TS1::ResolvePersonalText(strings);
                    if(!personal.Ambiguous&&(personal.SourceLanguage||personal.UTF8))ingredient.name=original::PersonalToUTF8(personal.Value(0),personal.SourceLanguage,personal.UTF8);
                }
                if(auto bitmap=object->Resource->Get<BMP>(object->OBJ->CatalogStringsID)) {
                    Canvas strip;
                    if(UIGraphics::DecodeBMP(bitmap->GetData(),strip)&&strip.Width>=2&&strip.Height>0)Crop(strip,ingredient.icon,0,0,strip.Width/2,strip.Height);
                }
            }
            data.rows.push_back(std::move(row));
        }
    } catch(const std::exception& error) { std::fprintf(stderr,"[ui] original spellbook resources: %s\n",error.what()); }
    return data;
}
}
#endif

bool SimDialogView::UpdateSpellbook(LotSession& s, const AppInput& in) {
#ifndef __3DS__
    if (_DesktopWidth) {
        if (_DesktopType != 12 || _Btns.size() != 3) return true;
        auto& book=BookData(s,s.Dialogs.front().AvatarID);book.pointer=in;
        const int pages=std::max(1,(int(book.rows.size())+3)/4);
        const auto page=[&](int value){value=std::clamp(value,0,pages-1);if(value!=_SpellPage){_SpellPage=value;_SpellSel=0;_DesktopFirstLine=0;book.hover.Clear();}};
        if(in.Down&BTN_LEFT)page(_SpellPage-1);
        if(in.Down&BTN_RIGHT)page(_SpellPage+1);
        if(in.Down&BTN_UP){_SpellSel=std::max(0,_SpellSel-1);_DesktopFirstLine=0;}
        if(in.Down&BTN_DOWN){_SpellSel=std::min(std::max(0,std::min(4,int(book.rows.size())-_SpellPage*4)-1),_SpellSel+1);_DesktopFirstLine=0;}
        _DesktopFirstLine=std::clamp(_DesktopFirstLine-in.WheelY*3,0,_DesktopMaxFirstLine);
        int hit=-1;
        for(size_t i=0;i<_Btns.size();++i)if(_Btns[i].R.Contains(in.TouchX,in.TouchY))hit=int(i);
        for(const auto& row:_DesktopChoiceRects)if(row.second.Contains(in.TouchX,in.TouchY))hit=10+int(row.first%4);
        if(in.PointerCancelled)_DesktopPressed=-1;
        if(in.TouchDown)_DesktopPressed=hit;
        int choice=-1;
        if(in.TouchUp){if(hit==_DesktopPressed)choice=hit;_DesktopPressed=-1;}
        if(in.TextCancel||(in.Down&BTN_B))choice=2;
        if(choice==0)page(_SpellPage-1);
        else if(choice==1)page(_SpellPage+1);
        else if(choice>=10){_SpellSel=choice-10;_DesktopFirstLine=0;book.hover.Clear();}
        else if(choice==2){
            FrontSound("ui_click");s.RespondDialog(0);_SpellPage=_SpellSel=0;_DesktopFirstLine=_DesktopMaxFirstLine=0;
            _DesktopWidth=0;_DesktopType=0;_DesktopPressed=-1;_DesktopMessageIdentity.clear();_Btns.clear();_DesktopChoiceRects.clear();book.hover.Clear();
        }
        return true;
    }
#endif
    int n = (int)FSO::SimAntics::Engine::Utils::VMTS1Travel::Spellbook(s.VM()).size();
    int pages = std::max(1, (n + 5) / 6);
    auto setPage = [&](int p) { p = std::max(0, std::min(pages - 1, p)); if (p != _SpellPage) { _SpellPage = p; _SpellSel = 0; } };
    if ((in.Down & (BTN_RIGHT | BTN_R)) || (in.TouchDown && Rect(276, 200, 40, 32).Contains(in.TouchX, in.TouchY))) setPage(_SpellPage + 1);
    if ((in.Down & (BTN_LEFT | BTN_L)) || (in.TouchDown && Rect(4, 200, 40, 32).Contains(in.TouchX, in.TouchY))) setPage(_SpellPage - 1);
    int onPage = std::max(1, std::min(6, n - _SpellPage * 6));
    if (in.Down & BTN_DOWN) _SpellSel = std::min(onPage - 1, _SpellSel + 1);
    if (in.Down & BTN_UP) _SpellSel = std::max(0, _SpellSel - 1);
    if (in.TouchDown) for (int k = 0; k < onPage; k++)
        if (Rect(12, kSpellRowY + k * kSpellRowH, 146, kSpellRowH - 2).Contains(in.TouchX, in.TouchY)) _SpellSel = k;
    if ((in.Down & (BTN_A | BTN_B)) || (in.TouchDown && Rect(120, 202, 80, 28).Contains(in.TouchX, in.TouchY))) {
        FrontSound("ui_click");
        _SpellPage = 0; _SpellSel = 0;
        s.RespondDialog(0);
    }
    return true;
}

// FIX(sims) QA2: "Gold Thread, Gold Thread, Llama Spit" -> "Gold Thread x2, Llama Spit" (the top list was cut off)
static std::vector<std::string> GroupIngredients(const std::vector<std::string>& in) {
    std::vector<std::string> names; std::vector<int> counts;
    for (auto& i : in) {
        auto it = std::find(names.begin(), names.end(), i);
        if (it == names.end()) { names.push_back(i); counts.push_back(1); } else counts[(size_t)(it - names.begin())]++;
    }
    for (size_t k = 0; k < names.size(); k++) if (counts[k] > 1) names[k] += " x" + std::to_string(counts[k]);
    return names;
}

void SimDialogView::DrawSpellbook(LotSession& s, Canvas& top, Canvas& bottom) {
#ifndef __3DS__
    if (&top == &bottom) {
        auto& book=BookData(s,s.Dialogs.front().AvatarID);
        const auto identity="book:"+std::to_string(s.Dialogs.front().CallerID)+":"+book.key;
        if(_DesktopMessageIdentity!=identity){_SpellPage=_SpellSel=0;_DesktopFirstLine=0;_DesktopPressed=-1;_DesktopMessageIdentity=identity;book.hover.Clear();}
        _DesktopWidth=top.Width;_DesktopType=12;
        const auto panel=BookPanel(top);auto& graphics=UIGraphics::Get();
        graphics.DrawImage(top,"cpanel/Backgrounds/magicbookbkg.bmp",panel);
        const int pages=std::max(1,(int(book.rows.size())+3)/4);
        _SpellPage=std::clamp(_SpellPage,0,pages-1);
        const int visible=std::max(0,std::min(4,int(book.rows.size())-_SpellPage*4));
        _SpellSel=std::clamp(_SpellSel,0,std::max(0,visible-1));_DesktopChoiceRects.clear();
        std::string hoverName;uint64_t hoverTarget=0;Rect hoverRect;
        const auto* rowArt=graphics.Image("Nbhd/SpellPickItemBkg.bmp");
        for(int row=0;row<visible;++row){
            const int index=_SpellPage*4+row;const auto& recipe=book.rows[size_t(index)];
            const Rect bounds(panel.X+48,panel.Y+52+row*68,240,68);
            _DesktopChoiceRects.push_back({size_t(index),bounds});
            if(rowArt&&rowArt->Width==240&&rowArt->Height==204){
                const int state=row==_SpellSel?2:bounds.Contains(book.pointer.TouchX,book.pointer.TouchY)?1:0;
                Canvas tile;Crop(*rowArt,tile,0,state*68,240,68);top.Blit(tile.Px.data(),tile.Width,tile.Height,bounds.X,bounds.Y,true);
            }
            for(int ingredient=0;ingredient<3;++ingredient){
                const auto& source=recipe.ingredients[size_t(ingredient)];
                const Rect cell(panel.X+81+ingredient*68,panel.Y+65+row*68,45,45);
                graphics.DrawButton(top,"cpanel/People/PeopleTemplate.bmp",ButtonState::Normal,cell);
                if(source.icon.Width&&source.icon.Height){
                    const float scale=std::min({1.f,41.f/source.icon.Width,41.f/source.icon.Height});
                    const int width=std::max(1,int(source.icon.Width*scale)),height=std::max(1,int(source.icon.Height*scale));
                    top.BlitScaled(source.icon.Px.data(),source.icon.Width,source.icon.Height,Rect(cell.X+(cell.W-width)/2,cell.Y+(cell.H-height)/2,width,height),true);
                }
                if(cell.Contains(book.pointer.TouchX,book.pointer.TouchY)){hoverName=source.name;hoverTarget=(uint64_t(recipe.recipe)<<32)|uint64_t(ingredient+1);hoverRect=cell;}
            }
        }
        auto frame=original::Raster(top);
        const auto text=[&](const std::string& value,Rect box,int pixels,bool centered=false){
            odh_host::TextOverlayStyle style;style.x=box.X;style.y=box.Y;style.width=box.W;style.height=box.H;style.font_size_px=pixels;style.padding=0;
            style.foreground={195,205,205,255};style.background={0,0,0,0};style.centered=centered;
            return odh_host::draw_text_overlay(frame,value,original::TextLanguage(),style);
        };
        text(book.caption,Rect(panel.X+50,panel.Y+12,250,22),16);
        if(visible){
            const auto& selected=book.rows[size_t(_SpellPage*4+_SpellSel)];
            text(selected.title,Rect(panel.X+333,panel.Y+12,260,22),16);
            odh_host::TextOverlayStyle body;body.x=panel.X+336;body.y=panel.Y+54;body.width=256;body.height=175;
            body.font_size_px=12;body.padding=0;body.wrap_lines=true;body.background={0,0,0,0};body.foreground={195,205,205,255};body.first_line=size_t(_DesktopFirstLine);
            const auto page=odh_host::draw_text_overlay(frame,selected.description,original::TextLanguage(),body);
            _DesktopFirstLine=int(page.first_line);_DesktopMaxFirstLine=int(page.max_first_line);
        }else _DesktopFirstLine=_DesktopMaxFirstLine=0;
        text(Format(book.page,{std::to_string(_SpellPage+1),std::to_string(pages)}),Rect(panel.X+208,panel.Y+330,114,33),15,true);
        original::Copy(frame,top);
        if(visible)graphics.DrawImage(top,book.rows[size_t(_SpellPage*4+_SpellSel)].art,Rect(panel.X+435,panel.Y+232,130,90));
        _Btns.resize(3);_Codes.clear();
        _Btns[0].Label=book.back;_Btns[1].Label=book.next;_Btns[2].Label=FrontStrings::Get().Get(152,1,"Cancel");
        _Btns[0].R=Rect(panel.X+52,panel.Y+330,100,33);_Btns[1].R=Rect(panel.X+324,panel.Y+330,104,33);_Btns[2].R=Rect(panel.X+470,panel.Y+330,110,33);
        for(int i=0;i<3;++i){_Btns[i].Disabled=false;_Btns[i].Selected=_DesktopPressed==i;_Btns[i].Draw(top);}
        book.hover.Draw(top,book.pointer,hoverName,hoverTarget,uint64_t(reinterpret_cast<uintptr_t>(s.VM())),uint64_t(_SpellPage*4+_SpellSel),hoverRect);
        return;
    }
#endif
    auto book = FSO::SimAntics::Engine::Utils::VMTS1Travel::Spellbook(s.VM());
    auto& T = FrontStrings::Get();
    int n = (int)book.size(), pages = std::max(1, (n + 5) / 6);
    _SpellPage = std::max(0, std::min(_SpellPage, pages - 1));
    int onPage = std::max(0, std::min(6, n - _SpellPage * 6));
    _SpellSel = std::max(0, std::min(_SpellSel, std::max(0, onPage - 1)));
    int made = 0;
    for (auto& e : book) if (e.Made) made++;
    // the title is the book's, not the dialog operand's (that can be a stale string of the caller)
    std::string title = "Spellbook";
    auto artFor = [&](int idx) { // STR#3 order: charms (EverAfter Crafter) first, then the wand spells from "Mood Up"
        bool wand = false;
        for (int i = 0; i <= idx && i < n; i++) if (book[(size_t)i].Name == "Mood Up") wand = true;
        return std::string(wand ? "cpanel/Backgrounds/SPELLBOOK_WandCharger.bmp" : "cpanel/Backgrounds/SPELLBOOK_CharmMakerAdult.bmp");
    };
    char buf[64];
    // top: the book (cpanel\Backgrounds\magicbookbkg.bmp): the page's recipes, the made ones ticked
    top.Dim(0, 0, top.Width, top.Height, 150);
    Rect p(6, 6, top.Width - 12, top.Height - 12);
    if (!UIGraphics::Get().DrawImage(top, "cpanel/Backgrounds/magicbookbkg.bmp", p)) top.FillRoundRect(p, 8, RGBA(0, 0, 80));
    TextCentered(top, TS1Size::S14, Rect(p.X, p.Y + 4, p.W, 18), title, RGBA(255, 216, 96), RGBA(0, 0, 30, 220));
    UIGraphics::Get().DrawImage(top, artFor(_SpellPage * 6 + _SpellSel), Rect(p.X + 8, p.Y + 30, 104, 72));
    snprintf(buf, sizeof buf, "%d / %d made", made, n);
    TextCentered(top, TS1Size::S10, Rect(p.X + 8, p.Y + 106, 104, 14), buf, RGBA(200, 220, 255));
    int y = p.Y + 28;
    for (int k = 0; k < onPage; k++) {
        auto& e = book[(size_t)(_SpellPage * 6 + k)];
        int x = p.X + 120, w = p.Right() - x - 8;
        if (k == _SpellSel) top.FillRoundRect(Rect(x - 4, y - 2, w + 6, 32), 4, RGBA(40, 70, 170, 200));
        Text(top, TS1Size::S12, x, y, Fit(TS1Size::S12, (e.Made ? "* " : "  ") + e.Name, w), e.Made ? RGBA(120, 255, 140) : RGBA(255, 255, 255), RGBA(0, 0, 30, 200));
        std::string ing;
        auto grouped = GroupIngredients(e.Ingredients);
        for (size_t i = 0; i < grouped.size(); i++) ing += (i ? ", " : "") + grouped[i];
        // FIX(sims) QA3: a long ingredient list ("..., Pegasu...") drops to the smaller font before it is cut
        // FIX(sims) QA3: a long ingredient list drops to the smaller font before it is cut (two lines do not fit the
        // 33-pixel row); the touch screen shows the selected recipe in full.
        TS1Size ingSize = TextW(TS1Size::S10, ing) <= w - 10 ? TS1Size::S10 : TS1Size::S9;
        Text(top, ingSize, x + 10, y + 15, Fit(ingSize, ing, w - 4), RGBA(190, 210, 255), RGBA(0, 0, 30, 180));
        y += 33;
    }
    // touch screen: the page's names (tap to select) and the selected recipe in full
    bottom.Dim(0, 0, bottom.Width, bottom.Height, 170);
    Rect bp(4, 4, bottom.Width - 8, 232);
    bottom.FillRoundRect(bp, 10, RGBA(14, 22, 96, 255));
    snprintf(buf, sizeof buf, "%s  %d / %d", title.c_str(), _SpellPage + 1, pages);
    TextCentered(bottom, TS1Size::S12, Rect(bp.X, bp.Y + 6, bp.W, 18), buf, RGBA(255, 216, 96), RGBA(0, 0, 30, 220));
    for (int k = 0; k < onPage; k++) {
        auto& e = book[(size_t)(_SpellPage * 6 + k)];
        Rect r(12, kSpellRowY + k * kSpellRowH, 146, kSpellRowH - 2);
        bottom.FillRoundRect(r, 4, k == _SpellSel ? RGBA(40, 90, 190) : RGBA(0, 0, 50));
        if (k == _SpellSel) bottom.StrokeRoundRect(r.X, r.Y, r.W, r.H, 4, RGBA(0, 255, 255));
        Text(bottom, TS1Size::S10, r.X + 4, r.Y + 4, Fit(TS1Size::S10, (e.Made ? "* " : "") + e.Name, r.W - 8), e.Made ? RGBA(120, 255, 140) : RGBA(230, 235, 255));
    }
    if (onPage > 0) {
        auto& e = book[(size_t)(_SpellPage * 6 + _SpellSel)];
        Rect dr(164, 28, 148, 168);
        bottom.FillRoundRect(dr, 6, RGBA(0, 0, 40));
        // FIX(sims) QA2: a long name ("Send Doppelganger To Work", 3 lines) pushed the ingredients over "Not made yet":
        // the picture shrinks to leave room for the text, and equal ingredients are counted ("Gold Thread x2").
        auto nameLines = Wrap(TS1Size::S12, e.Name, dr.W - 8);
        auto ings = GroupIngredients(e.Ingredients);
        int textH = (int)nameLines.size() * 15 + (int)ings.size() * 12;
        int artH = std::max(30, std::min(90, dr.H - 4 - 16 - textH - 2));
        int artW = 130 * artH / 90;
        UIGraphics::Get().DrawImage(bottom, artFor(_SpellPage * 6 + _SpellSel), Rect(dr.X + (dr.W - artW) / 2, dr.Y + 4, artW, artH));
        int dy = dr.Y + 6 + artH;
        for (auto& l : nameLines) { TextCentered(bottom, TS1Size::S12, Rect(dr.X, dy, dr.W, 15), l, RGBA(255, 216, 96)); dy += 15; }
        for (auto& ing : ings) { if (dy + 12 > dr.Bottom() - 14) break; TextCentered(bottom, TS1Size::S10, Rect(dr.X, dy, dr.W, 12), Fit(TS1Size::S10, ing, dr.W - 8), RGBA(200, 220, 255)); dy += 12; }
        TextCentered(bottom, TS1Size::S10, Rect(dr.X, dr.Bottom() - 14, dr.W, 12), e.Made ? "Made by the family" : "Not made yet", e.Made ? RGBA(120, 255, 140) : RGBA(255, 180, 120));
    }
    if (_SpellPage > 0) bottom.FillTriangle(10, 216, 30, 204, 30, 228, RGBA(150, 200, 255));
    if (_SpellPage + 1 < pages) bottom.FillTriangle(310, 216, 290, 204, 290, 228, RGBA(150, 200, 255));
    TextButton ok; ok.Label = T.Get(152, 0, "OK"); ok.R = Rect(120, 202, 80, 28);
    ok.Draw(bottom);
}

bool SimDialogView::Update(LotSession& s, const AppInput& in) {
    if (s.Dialogs.empty() || IsLotSelection(s.Dialogs.front().Type)) return false;
    if (_DesktopWidth && s.Dialogs.front().Type == 4 && !s.Dialogs.front().Block && !_DesktopHelpOpen && _DesktopHelpButton.Width) {
        const Rect button(_DesktopWidth - _DesktopHelpButton.Width, 0, _DesktopHelpButton.Width, _DesktopHelpButton.Height);
        if (in.PointerCancelled) _DesktopPressed = -1;
        const bool hit = button.Contains(in.TouchX, in.TouchY);
        if (in.TouchDown) _DesktopPressed = hit ? 0 : -1;
        if (in.TouchUp) {
            const bool open = hit && _DesktopPressed == 0; _DesktopPressed = -1;
            if (open) { _DesktopHelpOpen = true; FrontSound("ui_click"); return true; }
        }
        // Pending nonblocking help must not capture the lot's ordinary controls.
        return hit && (in.TouchDown || in.TouchUp || in.Touching);
    }
    if (s.Dialogs.front().Type == 9 && _DesktopWidth) return UpdatePetChoice(s, in);
    if (s.Dialogs.front().Type == 6) return UpdateClothes(s, in); // TS1(sims)
    if (s.Dialogs.front().Type == 12) return UpdateSpellbook(s, in); // FIX(sims)
    if(!_DesktopWidth)Layout(s);
    if(_DesktopWidth) {
        if(in.PointerCancelled)_DesktopPressed=-1;
        if(_DesktopType==10){
            const auto& choices=s.Dialogs.front().Choices;
            if(!choices.empty()){
                auto found=std::find_if(choices.begin(),choices.end(),[&](const auto& choice){return std::to_string(choice.Value)==_DesktopInput;});
                const int old=found==choices.end()?-1:int(found-choices.begin());
                const int delta=-in.WheelY+((in.Down&BTN_DOWN)?1:0)-((in.Down&BTN_UP)?1:0);
                const int count=int(choices.size());if(delta)_DesktopInput=std::to_string(choices[size_t(((old<0&&delta<0?0:old)+delta%count+count)%count)].Value);
                if(in.TouchDown)for(const auto& row:_DesktopChoiceRects)if(row.second.Contains(in.TouchX,in.TouchY))_DesktopInput=std::to_string(choices[row.first].Value);
            }
        }else _DesktopFirstLine=std::clamp(_DesktopFirstLine-in.WheelY*3+((in.Down&BTN_DOWN)?1:0)-((in.Down&BTN_UP)?1:0),0,_DesktopMaxFirstLine);
    }
    int chosen = -1;
    if(_DesktopWidth) {
        if(_DesktopType==3){
            for(const auto& edit:in.TextEvents){
                if(edit.Kind==AppInput::TextEvent::Type::Insert && _DesktopInput.size()+edit.Text.size()<=4096)_DesktopInput+=edit.Text;
                else if(edit.Kind==AppInput::TextEvent::Type::Backspace && !_DesktopInput.empty()){
                    auto at=_DesktopInput.size()-1;while(at>0 && (uint8_t(_DesktopInput[at])&0xc0)==0x80)--at;_DesktopInput.erase(at);
                }
            }
        }
        int hit=-1;for(size_t i=0;i<_Btns.size();++i)if(_Btns[i].R.Contains(in.TouchX,in.TouchY))hit=int(i);
        if(in.TouchDown)_DesktopPressed=hit;
        if(in.TouchUp){if(hit>=0 && hit==_DesktopPressed)chosen=hit;_DesktopPressed=-1;}
    } else for (size_t i = 0; i < _Btns.size(); i++) if (_Btns[i].Update(in)) chosen = (int)i;
    // the buttons are rebuilt every frame: a tap is a touch-down inside a button
    if (!_DesktopWidth && chosen < 0 && in.TouchDown) for (size_t i = 0; i < _Btns.size(); i++) if (_Btns[i].R.Contains(in.TouchX, in.TouchY)) chosen = (int)i;
    if (chosen < 0 && ((in.Down & BTN_A) || (_DesktopWidth && in.TextAccept))) chosen = 0;
    if(_DesktopWidth&&_DesktopType==10&&((in.Down&BTN_B)||in.TextCancel)){
        s.RespondDialog(0,"-1");_Btns.clear();_Codes.clear();_DesktopInput.clear();_DesktopMessageIdentity.clear();_DesktopPressed=-1;return true;
    }
    if (chosen < 0 && ((in.Down & BTN_B) || (_DesktopWidth && in.TextCancel))) chosen = (int)_Btns.size() - 1;
    if(_DesktopWidth&&_DesktopType==10&&chosen==0){
        const auto& dialog=s.Dialogs.front();
        if(!dialog.ChoicesReady||std::none_of(dialog.Choices.begin(),dialog.Choices.end(),[&](const auto& c){return std::to_string(c.Value)==_DesktopInput;}))return true;
    }
    if (chosen >= 0 && chosen < int(_Codes.size())) {
        FrontSound("ui_click");
        const int code = _Codes[(size_t)chosen];
        // Identically labelled queued notices must not inherit a held click
        // when this notice was answered with Enter or Escape.
        _Btns.clear(); _Codes.clear();
        #ifndef __3DS__
        if(_DesktopType==10){
            const auto& dialog=s.Dialogs.front();
            odh_host::PhonebookSelection offered;offered.ready=dialog.ChoicesReady;std::optional<size_t> selected;
            for(size_t i=0;i<dialog.Choices.size();++i){const auto& c=dialog.Choices[i];offered.choices.push_back({c.Label,c.Value});if(std::to_string(c.Value)==_DesktopInput)selected=i;}
            if(const auto response=odh_host::phonebook_button_response(offered,size_t(chosen),selected))s.RespondDialog(response->code,response->text);
        }else if(_DesktopType==3){
            try{s.RespondDialog(code,original::UTF8ToLegacy(_DesktopInput));}
            catch(const std::exception&){return true;} // Keep the active draft; never answer with corrupt bytes.
        }else s.RespondDialog(code);
#else
        s.RespondDialog(code);
#endif
        _DesktopInput.clear();_DesktopMessageIdentity.clear();_DesktopPressed=-1;
    }
    return true;
}

bool SimDialogView::DrawPetChoice(LotSession& session, Canvas& canvas) {
#ifndef __3DS__
    _DesktopWidth = canvas.Width;
    const auto& dialog = session.Dialogs.front();
    if (!session.PetChoiceCurrent(dialog)) return false;
    const auto avatarID = dialog.AvatarID;
    auto pet = dynamic_cast<FSO::SimAntics::VMAvatar*>(session.VM()->GetObjectById(avatarID));
    if (_DesktopType != 9 || _PetAvatar != avatarID || _PetWait.lock() != dialog.ChoiceCallerState.lock()) {
        _DesktopType = 9;
        _PetAvatar = avatarID;
        _PetWait = dialog.ChoiceCallerState;
        _PetIndex = 0;
        _PetRendered = -1;
        _DesktopPressed = -1;
        _Btns.clear();
        _Btns.resize(4);
    }
    if (_PetRendered != _PetIndex) {
        _PetRendered = _PetIndex;
        _PetPreview = Canvas();
        if (pet && !dialog.Choices.empty()) {
            try {
                _PetPreview = OriginalPCUI::Get().PetBody(pet, dialog.Choices[_PetIndex].Label);
            } catch (const std::exception& error) {
                printf("Pet preview: %s\n", error.what());
            }
        }
    }

    auto& art = FrontArt::Get();
    art.Load(ResGraphics::Get());
    auto& strings = FrontStrings::Get();
    // The commercial arrow strips and WinBtn establish the unscaled size
    // of this centered panel, independently of the desktop resolution.
    const Rect panel((canvas.Width - 304) / 2, (canvas.Height - 304) / 2, 304, 304);
    NineSlice(canvas, art.OriginalDialogTiles, panel, 12);
    auto frame = original::Raster(canvas);
    odh_host::TextOverlayStyle title;
    title.x = panel.X + 8;
    title.y = panel.Y + 10;
    title.width = panel.W - 16;
    title.height = 38;
    title.font_size_px = 21;
    title.centered = true;
    title.background = {0,0,0,0};
    title.foreground = {255,255,255,255};
    odh_host::draw_text_overlay(frame, original::LegacyToUTF8(strings.Get(221, 4, "Please Select Your Pet")),
                                original::TextLanguage(), title);
    original::Copy(frame, canvas);
    if (_PetPreview.Width) {
        canvas.Blit(_PetPreview.Px.data(), _PetPreview.Width, _PetPreview.Height,
                    panel.X + (panel.W - _PetPreview.Width) / 2, panel.Y + 163 - _PetPreview.Height / 2);
    }
    _Btns[0].R = Rect(panel.X + 33, panel.Y + 263, 100, 33);
    _Btns[1].R = Rect(panel.X + 168, panel.Y + 263, 100, 33);
    _Btns[0].Label = strings.Get(152, 0, "OK");
    _Btns[1].Label = strings.Get(152, 1, "Cancel");
    _Btns[0].Disabled = !_PetPreview.Width;
    for (int i = 0; i < 2; ++i) {
        _Btns[i].Selected = (_DesktopPressed == i);
        _Btns[i].Draw(canvas);
    }
    _Btns[2].R = Rect(panel.X + 10, panel.Y + 123, 18, 34);
    _Btns[3].R = Rect(panel.Right() - 28, panel.Y + 123, 18, 34);
    auto& graphics = UIGraphics::Get();
    graphics.DrawButton(canvas, "Nbhd/DesignCharSkinsLeftBtn.bmp",
                        _DesktopPressed == 2 ? ButtonState::Pressed : ButtonState::Normal, _Btns[2].R);
    graphics.DrawButton(canvas, "Nbhd/DesignCharSkinsRightBtn.bmp",
                        _DesktopPressed == 3 ? ButtonState::Pressed : ButtonState::Normal, _Btns[3].R);
    return true;
#else
    return false;
#endif
}

bool SimDialogView::UpdatePetChoice(LotSession& session, const AppInput& input) {
    const auto& dialog = session.Dialogs.front();
    if (dialog.Block && !dialog.ChoiceSourceResolved) return true;
    if (!session.PetChoiceCurrent(dialog)) {
        session.RespondDialog(0, "-1");
        _PetPreview = Canvas();
        _PetWait.reset();
        _Btns.clear();
        _Codes.clear();
        _DesktopType = 0;
        _DesktopPressed = -1;
        return true;
    }
    if (_DesktopType != 9 || _Btns.size() != 4 || _PetWait.lock() != dialog.ChoiceCallerState.lock())
        return true;
    int hit = -1;
    for (size_t i = 0; i < _Btns.size(); ++i)
        if (_Btns[i].R.Contains(input.TouchX, input.TouchY)) hit = int(i);
    if (input.PointerCancelled) _DesktopPressed = -1;
    if (input.TouchDown) _DesktopPressed = hit;
    int choice = -1;
    if (input.TouchUp) {
        if (hit == _DesktopPressed) choice = hit;
        _DesktopPressed = -1;
    }
    if (input.TextCancel || (input.Down & BTN_B)) choice = 1;
    else if (input.TextAccept || (input.Down & BTN_A)) choice = 0;
    else if (input.Down & BTN_LEFT) choice = 2;
    else if (input.Down & BTN_RIGHT) choice = 3;
    if (choice < 0) return true;

    const auto count = int(dialog.Choices.size());
    if (choice >= 2) {
        if (count) {
            _PetIndex = (_PetIndex + count + (choice == 2 ? -1 : 1)) % count;
            FrontSound("ui_click");
        }
        return true;
    }
    if (choice == 0 && (!count || !_PetPreview.Width)) return true;
    FrontSound("ui_click");
    session.RespondDialog(choice == 0 ? 1 : 0, choice == 0 ? std::to_string(_PetIndex) : "-1");
    _PetPreview = Canvas();
    _PetWait.reset();
    _Btns.clear();
    _Codes.clear();
    _DesktopPressed = -1;
    _DesktopType = 0;
    _DesktopMessageIdentity.clear();
    return true;
}

bool SimDialogView::DrawDesktop(LotSession& s,LotScreen& lot,Canvas& canvas) {
    const auto& dialog=s.Dialogs.front();
    const auto& title=dialog.Title;const auto& message=dialog.Message;
    const auto identity=std::to_string(dialog.Type)+":"+std::to_string(dialog.Block)+":"+std::to_string(dialog.CallerID)+":"+std::to_string(dialog.IconID)+":"+std::to_string(dialog.TextUTF8)+":"+title+"\n"+message;
    if(identity!=_DesktopMessageIdentity) {
        _DesktopFirstLine=0;_DesktopInput.clear();_DesktopPressed=-1;_DesktopMessageIdentity=identity;
        _DesktopHelpOpen=false;_DesktopHelpButton=Canvas();
        if (dialog.Type == 4) {
            _DesktopHelpOpen = dialog.Block;
            // The original question button is the initiating help object's own
            // BMP500, not the unrelated small HelpButton strip in UIGraphics.
            if (auto vm=s.VM()) if (auto caller=vm->GetObjectById(dialog.CallerID))
                if (caller->Object && caller->Object->Resource)
                    if (auto bmp=caller->Object->Resource->Get<FSO::Files::Formats::IFF::Chunks::BMP>(500))
                        UIGraphics::DecodeBMP(bmp->GetData(),_DesktopHelpButton);
            // Missing/stale icon resources must never make a message inaccessible.
            if (!_DesktopHelpButton.Width) _DesktopHelpOpen=true;
        }
    }
    if (dialog.Type == 4 && !_DesktopHelpOpen && _DesktopHelpButton.Width) {
        _DesktopWidth=canvas.Width;_DesktopType=4;
        _Btns.resize(1);_Codes.clear();
        _Btns[0].R=Rect(canvas.Width-_DesktopHelpButton.Width,0,_DesktopHelpButton.Width,_DesktopHelpButton.Height);
        canvas.Blit(_DesktopHelpButton.Px.data(),_DesktopHelpButton.Width,_DesktopHelpButton.Height,_Btns[0].R.X,0,true);
        return false; // Visible help notification, not an open modal.
    }
    Canvas portrait;
    const Canvas* illustration=nullptr;
    if(dialog.IconMode==3) {
        if(_IndexedBytes!=dialog.IndexedIconBMP) {
            _IndexedBytes=dialog.IndexedIconBMP;_IndexedIcon=Canvas();
            if(!_IndexedBytes.empty())DecodeImage(_IndexedBytes,_IndexedIcon,true);
        }
        if(_IndexedIcon.Width && _IndexedIcon.Height)illustration=&_IndexedIcon;
    } else if(dialog.IconMode==0 && dialog.IconID) {
        if(auto entity=s.VM()->GetObjectById(dialog.IconID)) {
            const Canvas* icon=nullptr;
            if(auto avatar=dynamic_cast<FSO::SimAntics::VMAvatar*>(entity))icon=lot.FaceFor(avatar,0);
            else icon=lot.IconFor(entity);
            if(icon && icon->Width && icon->Height) {
                if(const auto* halo=UIGraphics::Get().Image("cpanel/SimStub.bmp")) {
                    portrait=*halo;
                    const Rect frame(portrait.Width/2-22,portrait.Height/2-22,45,45);
                    UIGraphics::Get().DrawButton(portrait,"cpanel/People/PeopleTemplate.bmp",ButtonState::Normal,frame);
                    const float scale=std::min({1.f,41.f/icon->Width,41.f/icon->Height});
                    const int w=std::max(1,int(icon->Width*scale)),h=std::max(1,int(icon->Height*scale));
                    portrait.BlitScaled(icon->Px.data(),icon->Width,icon->Height,Rect(frame.X+(45-w)/2,frame.Y+(45-h)/2,w,h),true);
                    illustration=&portrait;
                } else illustration=icon;
            }
        }
    }
#ifndef __3DS__
    _DesktopWidth=canvas.Width;
    odh::DialogState state;
    std::vector<std::string> labels;std::vector<int> codes;
    auto& strings=FrontStrings::Get();
    const auto text = [&](const std::string& bytes) { return original::DialogStringUTF8(bytes, dialog.TextUTF8); };
    for(const auto& button:original::DialogButtonLabels(dialog.Type,text(dialog.Yes),text(dialog.No),text(dialog.Cancel),original::LegacyToUTF8(strings.Get(152,0,"OK")),original::LegacyToUTF8(strings.Get(152,2,"Yes")),original::LegacyToUTF8(strings.Get(152,3,"No")),original::LegacyToUTF8(strings.Get(152,1,"Cancel")))){labels.push_back(button.Label);codes.push_back(button.Code);}
    _DesktopType=dialog.Type==10?10:dialog.Type==4?4:dialog.Type==3?3:dialog.Type==2?2:dialog.Type==1?1:0;
    state.instruction.operands[5]=_DesktopType;
    state.prompt.title=text(dialog.Title);state.prompt.message=text(dialog.Message);
    state.prompt.yes=labels[0];
    if(labels.size()>1)state.prompt.no=labels[1];
    if(labels.size()>2)state.prompt.cancel=labels[2];
    if(_DesktopType==10){
        // The phone book replaces the operand's incidental message with the
        // original contact selector. Contacts are prepared by current NBRS.
        codes={1,0};
        state.prompt.title="Call Neighbour";state.prompt.message.clear();state.prompt.yes="Call";
        state.prompt.cancel=original::LegacyToUTF8(strings.Get(152,1,"Cancel"));
        for(const auto& choice:dialog.Choices)state.prompt.choices.push_back({choice.Label,choice.Value});
        illustration=nullptr; // No selected contact means no implicit identity/portrait.
        auto selected=std::find_if(dialog.Choices.begin(),dialog.Choices.end(),[&](const auto& choice){return std::to_string(choice.Value)==_DesktopInput;});
        if(selected!=dialog.Choices.end()&&selected->Portrait.Width)illustration=&selected->Portrait;
    }
    state.input=_DesktopInput;
    auto appearance=original::DialogAppearance();
    appearance.response_pointer=lot.DesktopPointerPosition(true);
    if(_DesktopPressed>=0)appearance.pressed_response=std::size_t(_DesktopPressed);
    appearance.center_title=true;appearance.phonebook_cancel_button=_DesktopType==10;appearance.tutorial_corner=_DesktopType==4;
    auto image=illustration?original::Bitmap(*illustration):odh::BitmapImage{};
    if(illustration)appearance.illustration=&image;
    const auto layout=odh_host::vm_message_dialog_layout(canvas.Width,canvas.Height,state,original::TextLanguage(),appearance);
    if(!layout)return false;
    auto page=original::PaintDialog(canvas,state,original::TextLanguage(),_DesktopFirstLine,appearance,*layout);
    _DesktopMaxFirstLine=int(page.max_first_line);_DesktopFirstLine=int(page.first_line);
    // Derive the input rectangles through the same original hit function used
    // by painting. Engine response codes remain attached to the existing ticket.
    _DesktopChoiceRects.clear();
    if(_DesktopType==10)for(size_t i=0;i<state.prompt.choices.size();++i)if(const auto r=odh_host::vm_dialog_choice_rect(*layout,state,i))_DesktopChoiceRects.push_back({i,Rect(r->x,r->y,r->width,r->height)});
    const auto count=layout->response_count?layout->response_count:odh_host::vm_dialog_button_count(_DesktopType);
    _Btns.resize(count);_Codes.resize(count);
    for(size_t i=0;i<count;++i){
        const auto r=odh_host::vm_dialog_button_rect(*layout,_DesktopType,i);
        _Btns[i].R=Rect(r.x,r.y,r.width,r.height);_Codes[i]=codes[i];
    }
    return true;
#else
    return false;
#endif
}

bool SimDialogView::Draw(LotSession& s, LotScreen& lot, Canvas& top, Canvas& bottom) {
    if (s.Dialogs.empty() || IsLotSelection(s.Dialogs.front().Type)) return false;
    if (&top == &bottom && s.Dialogs.front().Type == 9) {
        return DrawPetChoice(s, top);
    }
    if(&top==&bottom && s.Dialogs.front().Type!=6 && s.Dialogs.front().Type!=12) {
        return DrawDesktop(s,lot,top);
    }
    _DesktopWidth=0;
    if (s.Dialogs.front().Type == 6) { DrawClothes(s, top, bottom); return true; } // TS1(sims)
    if (s.Dialogs.front().Type == 12) { DrawSpellbook(s, top, bottom); return true; } // FIX(sims)
    if (!_TileLoaded) { ResGraphics::Get().Load("Shared/Dlg/Gendlg.bmp", _Tile, false); _TileLoaded = true; }
    auto& d = s.Dialogs.front();
    Layout(s);
    auto& art = FrontArt::Get();
    auto panel = [&](Canvas& c, const Rect& r) {
        // the neighbourhood dialogs' blue tiler, framed with the system dialog tile's bevel (Shared/Dlg/Gendlg.bmp)
        if (art.DialogTiler.Width > 0) { NineSlice(c, art.DialogTiler, r, 14); NineSlice(c, art.DialogTiler, Rect(r.X + 3, r.Y + 3, r.W - 6, r.H - 6), 14); }
        else if (_Tile.Width > 0) NineSlice(c, _Tile, r, 10);
        else c.FillRoundRect(r, 10, RGBA(14, 22, 96, 245));
    };
    // ---- top: the whole dialog, large
    top.Dim(0, 0, top.Width, top.Height, 150);
    std::string title = ToLatin1(d.Title), msg = ToLatin1(d.Message);
    const Canvas* pic = nullptr;
    if (auto vm = s.VM()) {
        auto e = vm->GetObjectById(d.IconID ? d.IconID : d.CallerID);
        if (auto av = dynamic_cast<FSO::SimAntics::VMAvatar*>(e)) pic = lot.FaceFor(av, 0);
        else if (e) pic = lot.IconFor(e);
    }
    bool desktop = &top == &bottom;
    int dialogWidth = desktop ? 500 : top.Width;
    int textX = 18, textW = dialogWidth - 36;
    if (pic) { textX = 82; textW = dialogWidth - 100; }
    auto lines = Wrap(TS1Size::S12, msg, textW);
    if (lines.size() > 9) { lines.resize(9); lines.back() += "..."; }
    int h = std::max(pic ? 96 : 60, 38 + (int)lines.size() * 16 + 10);
    if (desktop) h += 50;
    Rect p((top.Width-dialogWidth)/2+8, std::max(4, (top.Height - h) / 2), dialogWidth - 16, h);
    panel(top, p);
    top.StrokeRoundRect(p.X + 1, p.Y + 1, p.W - 2, p.H - 2, 8, RGBA(120, 140, 230, 180));
    if (pic) {
        Rect pr(p.X + 12, p.Y + 30, 56, 56);
        top.FillRoundRect(pr, 4, RGBA(0, 0, 30));
        float sc = std::min(52.0f / pic->Width, 52.0f / pic->Height);
        int w = (int)(pic->Width * sc), hh = (int)(pic->Height * sc);
        top.BlitScaled(pic->Px.data(), pic->Width, pic->Height, Rect(pr.X + (pr.W - w) / 2, pr.Y + (pr.H - hh) / 2, w, hh), true);
        top.StrokeRoundRect(pr.X, pr.Y, pr.W, pr.H, 4, RGBA(140, 160, 230));
    }
    TextCentered(top, TS1Size::S14, Rect(p.X, p.Y + 6, p.W, 20), Fit(TS1Size::S14, title, p.W - 20), RGBA(255, 216, 96), RGBA(0, 0, 30, 220));
    int y = p.Y + 32;
    for (auto& l : lines) { Text(top, TS1Size::S12, p.X + textX - 8, y, l, RGBA(255, 255, 255), RGBA(0, 0, 30, 200)); y += 16; }
    if (desktop) {
        _DesktopWidth = top.Width; _DesktopButtonY = p.Bottom()-40;
        Layout(s);
        for (auto& b : _Btns) b.Draw(top);
        return true;
    }
    _DesktopWidth = 0;
    // ---- bottom: title + the buttons
    bottom.Dim(0, 0, bottom.Width, bottom.Height, 170);
    Rect bp(8, 26, bottom.Width - 16, 186);
    panel(bottom, bp);
    bottom.StrokeRoundRect(bp.X + 1, bp.Y + 1, bp.W - 2, bp.H - 2, 8, RGBA(120, 140, 230, 180));
    TextCentered(bottom, TS1Size::S12, Rect(bp.X, bp.Y + 8, bp.W, 18), Fit(TS1Size::S12, title.empty() ? std::string("...") : title, bp.W - 16), RGBA(255, 216, 96), RGBA(0, 0, 30, 220));
    // the message again, smaller (the top screen has it large)
    auto small = Wrap(TS1Size::S10, msg, bp.W - 24);
    if (small.size() > 8) { small.resize(8); small.back() += "..."; }
    int sy = bp.Y + 28;
    for (auto& l : small) { Text(bottom, TS1Size::S10, bp.X + 12, sy, l, RGBA(220, 230, 255), RGBA(0, 0, 30, 180)); sy += 13; }
    for (auto& b : _Btns) b.Draw(bottom);
    return true;
}

bool SimDialogView::ButtonPos(int index, int& x, int& y) const {
    if (index < 0 || index >= (int)_Btns.size()) return false;
    x = _Btns[(size_t)index].R.X + _Btns[(size_t)index].R.W / 2;
    y = _Btns[(size_t)index].R.Y + _Btns[(size_t)index].R.H / 2;
    return true;
}

}}
