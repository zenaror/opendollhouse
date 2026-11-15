/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims front end — see NeighbourhoodScreen.h. TS1(sims).
#include "app/front/NeighbourhoodScreen.h"
#include "app/front/LotSave.h"
#include "app/ui/UIGraphics.h"
#include "content/WorldContentFiles.h"
#include "content/TS1FamilyImport.h"
#include "app/front/CASBridge.h"
#include "app/front/FrontEnd.h"
#include "app/front/FrontStrings.h"
#include "app/front/TS1Font.h"
#include "files/LoadProgress.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <tuple>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#ifndef __3DS__
#include "content/ContentManager.h"
#include "content/TS1LotLocationFiles.h"
#include "neighborhood_presentation.hpp"
#include "app/ui/OriginalNeighborhoodResources.h"
#include "original_ui_art.hpp"
#include "app/ui/OriginalPresentation.h"
#include "app/ui/OriginalInput.h"
#include "app/ui/OriginalTextEncoding.h"
#endif

namespace simapp { namespace front {
namespace {
std::string PersonalFamilyLabel(const FamilyInfo& family) {
    if (family.TextAmbiguous) return "[name unavailable]";
#ifndef __3DS__
    return original::PersonalToUTF8(family.Name, family.SourceLanguage, family.TextUTF8);
#else
    return family.Name;
#endif
}
std::string InterfaceLabel(const std::string& raw) {
#ifndef __3DS__
    return original::LegacyToUTF8(raw);
#else
    return raw;
#endif
}
}


// Import previews contain explicit user exports only. Installer templates are
// consumed separately from the neighbourhood's Patch queue, never offered as
// user exports simply because their GUIDs are absent from a saved family.
static std::vector<FamilyPicker::ImportEntry> ReadFamilyImportExports(const std::string& saveRoot,const std::string& dataRoot) {
    namespace WCF = FSO::Content::WorldContentFiles;
    std::vector<std::string> files;
    for (auto& dir : { saveRoot + "/Export", WCF::Find(dataRoot, "Export") })
        for (auto& f : WCF::FilesWithExt(dir, ".fam", false)) files.push_back(f);
    std::vector<FamilyPicker::ImportEntry> entries;
    for (auto& f : files) {
        FSO::Content::TS1::TS1FamFile fam;
        if (!FSO::Content::TS1::TS1FamilyImport::Read(f, fam)) continue;
        FamilyPicker::ImportEntry e;
        e.Path = f;
        e.Info.ChunkID = fam.FamilyID;
        e.Info.Name = fam.Name.empty() ? WCF::BaseNameNoExt(f) : fam.Name;
        e.Info.Budget = fam.Budget;
        for (auto& m : fam.Members) {
            SimInfo si;
            si.GUID = m.GUID; si.FirstName = m.FirstName.empty() ? m.UserName : m.FirstName; si.Bio = m.Bio;
            if (m.PersonData.size() > 65) si.Female = m.PersonData[65] != 0;   // person data 65: gender
            if (!m.Thumbnail.Data.empty()) UIGraphics::DecodeBMP(m.Thumbnail.Data, si.Face);
            e.Info.Members.push_back(std::move(si));
        }
        entries.push_back(std::move(e));
    }
    return entries;
}

static const int MAP_W = 800, MAP_H = 600;
static const float BS = 0.4f;   // touch screen scale of the 800x600 screens
// FIX(sims) FB1: the banner at half the original size (was 0.42: 22 px buttons, too small to hit on the console):
// 53x52 frames -> 27x26, the banner art 800x52 -> 320x26 (the map's own 0.4 would make it 21 px tall)
static const float BTN = 0.5f;
static const int BAR_H = 26;

#ifndef __3DS__
struct NeighbourhoodScreen::DesktopPresentation {
    odh_host::PreparedNeighborhoodMap Map;
    odh_host::NeighborhoodMapPresentation View;
    std::map<uint32_t,odh::BitmapImage> Faces, Thumbnails;
    std::vector<FamilyPicker::ImportEntry> Imports;
    std::unique_ptr<odh_host::OriginalUiArt> Art;
    std::tuple<int,int,int,int> Context{-1,-1,0,0};
};
void NeighbourhoodScreen::PrepareDesktopPresentation(){
    _Original=std::make_shared<DesktopPresentation>();auto& p=*_Original;auto& map=p.Map;
    auto& M=_FE->Model();
    p.Imports=ReadFamilyImportExports(_FE->SaveRoot(),_FE->_Cfg.DataRoot);
    const unsigned mode=_Area==Area::Neighbourhood?1:_Area==Area::Downtown?2:_Area==Area::Vacation?3:_Area==Area::StudioTown?5:7;
    auto* provider=FSO::Content::Content::Get()->Neighborhood.get();
    if(!provider)throw odh::FormatError("no neighborhood provider for PC map");
    map=original::PrepareNeighborhoodMapResources(M,_FE->_Cfg.DataRoot+"/UIGraphics/UIGraphics.far",mode,
        original::TextLanguage(),*provider);
    std::map<int,std::pair<int,int>> points;
    for(const auto& marker:map.markers)points.emplace(marker.location.house,std::make_pair(marker.location.x,marker.location.y));
    M.LoadArea(_Area,&points);
    for(const auto& family:M.Families){
        odh::FamilyEntry entry{};entry.chunk_id=family.ChunkID;entry.display_name=original::PersonalLabelUTF8(family.Name,family.SourceLanguage,family.TextUTF8,family.TextAmbiguous);
        entry.family.house_number=family.House;entry.family.budget=family.Budget;
        entry.family.value_in_arch=family.NetWorth;entry.family.family_friends=family.Friends;
        for(const auto& member:family.Members){entry.family.member_guids.push_back(member.GUID);
            if(member.Face.Width)p.Faces.emplace(member.GUID,original::Bitmap(member.Face));
            if(member.Thumbnail.Width)p.Thumbnails.emplace(member.GUID,original::Bitmap(member.Thumbnail));}
        map.effective_families.push_back(std::move(entry));
    }
    p.Art=std::make_unique<odh_host::OriginalUiArt>(_FE->_Cfg.DataRoot,std::vector<std::string>{"NghUI/Downtown.bmp","NghUI/Vacation.bmp","NghUI/Studiotown.bmp","NghUI/Magicland.bmp",
        "NghUI/banner_neighborhood.bmp","NghUI/banner_downtown.bmp","NghUI/Exit.bmp","NghUI/MoveIn.bmp",
        "NghUI/Bulldoze.bmp","NghUI/Rezone.bmp","NghUI/Import.bmp","NghUI/Return.bmp","NghUI/Previous.bmp",
        "NghUI/Next.bmp","NghUI/Current.bmp","NghUI/TheSimsLogo.bmp","NghUI/InetBtn.bmp",
        "Downtown/Return.bmp","Downtown/Bulldoze.bmp","Downtown/Import.bmp","VIsland/Import.bmp",
        "Studiotown/Return.bmp","Studiotown/Bulldoze.bmp","Studiotown/import.bmp",
        "Magicland/Return.bmp","Magicland/Bulldoze.bmp","Magicland/Import.bmp","Nbhd/HotSpotPopupBkg.bmp",
        "Nbhd/HotSpotPopupBkgAlpha.bmp","Nbhd/HotspotPopupTiler.TGA","CPanel/People/PeopleTemplate.bmp"});
}
void NeighbourhoodScreen::FrameDesktop(const AppInput& in,Canvas& top,double dtMs){
    if(!_Original)PrepareDesktopPresentation();auto owner=_Original;auto& p=*owner;auto& view=p.View;auto& map=p.Map;
    auto& M=_FE->Model();
    view.can_manage=!_PickDone&&std::any_of(M.Lots.begin(),M.Lots.end(),[](const auto& lot){return lot.Exists;});
    view.allow_create_family=!_PickDone;view.allow_import=!_PickDone&&_Area==Area::Neighbourhood&&!p.Imports.empty();
    view.previous_available=!_PickDone&&_Area==Area::Neighbourhood;
    view.next_available=!_PickDone&&_Area==Area::Neighbourhood;

    view.managing=_Mode==Mode::Evict;
    view.rezoning=_Mode==Mode::Rezone;
    view.can_rezone=!_PickDone&&_Area==Area::Neighbourhood&&M.Unleashed();
    view.community.clear();for(const auto& lot:M.Lots)view.community[lot.House]=lot.Community;
    auto input=original::PointerInput(in);view.pointer=input.pointer_position;
    if(input.pointer_cancelled||input.pointer_overflow)view.pointer.reset();
    if(Dialog.Open){view.activation.reset();Dialog.Update(in);}
    else if(Picker.Open){view.activation.reset();Picker.Update(in);}
    else {
        auto hovered=odh_host::pick_map_lot(map,in.TouchX,in.TouchY);int index=-1;
        if(in.TouchY>=52&&hovered)for(size_t i=0;i<M.Lots.size();++i)if(M.Lots[i].House==*hovered){index=int(i);break;}
        if(index!=_Sel){_Sel=index;if(index>=0)Sound("ui_nhood_rollover");}
        auto click=view.activation.click(input,[&](int x,int y){return view.target_at(map,x,y);});
        if(click){auto target=view.target_at(map,(*click)[0],(*click)[1]);
            if(target){
                if(*target>=100){for(size_t i=0;i<M.Lots.size();++i)if(M.Lots[i].House==int(*target-100)){Activate(int(i));break;}}
                else if(*target==8)OnButton("movein");else if(*target==9){if(_PickDone)FinishPick(-1);else OnButton("exit");}
                else if(*target==10)OnButton("bulldoze");else if(*target==11)OnButton("prev");else if(*target==12)OnButton("next");
                else if(*target==13)OnButton("import");else if(*target==14)OnButton("credits");else if(*target==15)OnButton("rezone");
                else {unsigned next=unsigned(*target);if(next==1||next==map.mode)OnButton("return");else if(next==2)OnButton("downtown");
                    else if(next==3)OnButton("vacation");else if(next==5)OnButton("studiotown");else if(next==7)OnButton("magictown");}
            }
        }
        if(in.Down&BTN_START)OnButton("exit");
        if(in.Down&BTN_B){SetToolMode(Mode::Normal);_MovingFamily=-1;view.activation.reset();if(_PickDone)FinishPick(-1);}
    }
    if(_FE->RequestedLot>=0||_FE->CurrentState()==FrontEnd::State::SubScreen)return;
    // A completed action can replace the entire map. Resolve its owner again.
    if(_Original.get()!=&p){FrameDesktop(AppInput(),top);return;}
    if(Picker.Open)view.pointer.reset();
    const auto* moving_family=_Mode==Mode::MoveIn?M.Family(_MovingFamily):nullptr;
    // Rollover includes affordability and remaining funds. Reusing a family id
    // after its budget changes must not reuse the former price explanation.
    auto context=std::make_tuple(int(_Mode),int(_MovingFamily),moving_family?moving_family->Budget:0,
                                 moving_family?moving_family->MagiCoins:0);if(p.Context!=context){view.hover_cache.clear();p.Context=context;}
    view.advance(map,dtMs);
    auto frame=view.draw(map,p.Art.get(),original::LegacyToUTF8(FrontStrings::Get().Get(152,1,"Cancel")),original::TextLanguage(),M.Current+1,
        [&](uint16_t house){odh_host::NeighborhoodHoverText text;auto* lot=M.Lot(house);if(lot){text.address=original::LegacyToUTF8(lot->Address);auto* moving=_Mode==Mode::MoveIn?M.Family(_MovingFamily):nullptr;text.family_summary=M.Rollover(*lot,moving);
                if(_Mode==Mode::Rezone){
                    auto& strings=FrontStrings::Get();
                    if(lot->Family>=0){const auto hint=strings.Get(134,9);text.family_summary+="\n"+InterfaceLabel(hint.substr(hint.rfind('\n')+1));}
                    else text.family_summary=InterfaceLabel(Format(strings.Get(134,19),{lot->Name,lot->Desc}));
                }}return text;},
        [&](uint32_t guid)->const odh::BitmapImage*{auto it=p.Faces.find(guid);return it==p.Faces.end()?nullptr:&it->second;},
        [&](uint32_t guid)->const odh::BitmapImage*{
            auto miniature=p.Thumbnails.find(guid);if(miniature!=p.Thumbnails.end())return &miniature->second;
            auto face=p.Faces.find(guid);return face==p.Faces.end()?nullptr:&face->second;});
    top.Clear(RGBA(0,0,45));Canvas surface(800,600);original::Copy(frame,surface);top.Blit(surface.Px.data(),800,600,0,0,false);
    if(Picker.Open)Picker.DrawBottom(top);
    const bool modalDrawn = Dialog.Draw(top);
    RecordDrawnFrame(modalDrawn);
}
#endif

// ======================================================================= spans (animation overlays)

void NeighbourhoodScreen::Spans::Build(const Canvas& c, int ox, int oy, int w, int h) {
    W = w;
    Start.clear(); Len.clear(); Px.clear();
    for (int y = 0; y < c.Height; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= h) continue;
        int x = 0;
        while (x < c.Width) {
            while (x < c.Width && (c.Px[(size_t)y * c.Width + x] >> 24) == 0) x++;
            int s = x;
            while (x < c.Width && (c.Px[(size_t)y * c.Width + x] >> 24) != 0 && x - s < 60000) x++;
            if (x > s) {
                int dx0 = std::max(0, ox + s), dx1 = std::min(w, ox + x);
                if (dx1 > dx0) {
                    Start.push_back((uint32_t)(dy * w + dx0));
                    Len.push_back((uint16_t)(dx1 - dx0));
                    for (int k = dx0; k < dx1; k++) Px.push_back(c.Px[(size_t)y * c.Width + (k - ox)]);
                }
            }
        }
    }
}

void NeighbourhoodScreen::Spans::Draw(Canvas& dst, int camX, int camY) const {
    size_t p = 0;
    for (size_t i = 0; i < Start.size(); i++) {
        int y = (int)(Start[i] / (uint32_t)W), x0 = (int)(Start[i] % (uint32_t)W);
        int n = Len[i];
        int dy = y - camY;
        if (dy >= 0 && dy < dst.Height) {
            uint32_t* row = &dst.Px[(size_t)dy * dst.Width];
            for (int k = 0; k < n; k++) {
                int dx = x0 + k - camX;
                if (dx < 0 || dx >= dst.Width) continue;
                uint32_t c = Px[p + k];
                if ((c >> 24) == 255) row[dx] = c; else Canvas::BlendPx(row[dx], c);
            }
        }
        p += n;
    }
}

// ======================================================================= set-up

NeighbourhoodScreen::NeighbourhoodScreen(FrontEnd* fe) : _FE(fe) {}

void NeighbourhoodScreen::Sound(const char* name) { if (_FE->Sound) _FE->Sound(name); }

NeighbourhoodScreen::~NeighbourhoodScreen() { SetToolMode(Mode::Normal); }

void NeighbourhoodScreen::SetToolMode(Mode mode) {
    if(_Mode==mode)return;
#ifndef __3DS__
    const bool desktop=_FE->IsDesktop();
    if(desktop && _Mode==Mode::Evict){
        Sound("ui_nhood_bdoze_end");
    }
#endif
    _Mode=mode;
#ifndef __3DS__
    if(desktop && mode==Mode::Evict){
        // BdozeStart itself starts track 1551 (GenericLooped); BdozeStop
        // kills that track. A second explicit loop would double its volume.
        Sound("ui_nhood_bdoze");
    }
#endif
}


void NeighbourhoodScreen::Enter(Area a) {
    auto& M = _FE->Model();
    _Area = a;
    SetToolMode(Mode::Normal);
    _MovingFamily = -1;
    _Sel = -1;
    _ManualCam = false;
#ifndef __3DS__
    if(!_FE->IsDesktop())
#endif
    M.LoadArea(a);
    BuildArt();
    FSO::Files::LoadProgress::Progress("front: neighbourhood map", 0, 0); // ADDED(sims)
    BuildButtons();
    const AreaDef& def = GetAreaDef(a, M.Unleashed());
    if (_FE->Music && !_PickDone) _FE->Music(def.Music);
    // start on the first occupied lot (or the first lot)
    for (size_t i = 0; i < M.Lots.size(); i++) if (M.Lots[i].Family >= 0) { _Sel = (int)i; break; }
    if (_Sel < 0 && !M.Lots.empty()) _Sel = 0;
    if (_Sel >= 0) {
        auto& l = M.Lots[(size_t)_Sel];
        _CamX = (float)(l.X + l.Thumb.Width / 2 - 96); _CamY = (float)(l.Y + l.Thumb.Height / 2 - 122);
    }
}

void NeighbourhoodScreen::BuildArt() {
#ifndef __3DS__
    if(_FE->IsDesktop()){
        _Base=Canvas();_MapTop=Canvas();_MapBottom=Canvas();_Over.clear();_AnimTop.clear();_AnimBottom.clear();
        PrepareDesktopPresentation();_HasMap=true;return;
    }
#endif
    auto& ui = ResGraphics::Get();
    auto& M = _FE->Model();
    const AreaDef& def = GetAreaDef(_Area, M.Unleashed());
    _Base = Canvas(); _Over.clear(); _AnimTop.clear(); _AnimBottom.clear();
    _HasMap = ui.Load(def.Background, _Base);
    FSO::Files::LoadProgress::Progress("front: neighbourhood map", 0, 0); // ADDED(sims) progress ticks (save overlay)
    if (!_HasMap || _Base.Width != MAP_W || _Base.Height != MAP_H) {
        // no UIGraphics: a plain lawn so the lots are still there
        _Base.Resize(MAP_W, MAP_H);
        _Base.GradientV(0, 0, MAP_W, MAP_H, RGBA(70, 120, 50), RGBA(40, 90, 40));
    }
    for (auto u : def.Under) {
        Canvas l;
        if (ui.Load(u, l)) _Base.Blit(l.Px.data(), l.Width, l.Height, 0, 0, true);
    }
    // the holes the animation fills (magenta in the map)
    int hx0 = MAP_W, hy0 = MAP_H, hx1 = -1, hy1 = -1;
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++)
        if ((_Base.Px[(size_t)y * MAP_W + x] >> 24) == 0) { hx0 = std::min(hx0, x); hy0 = std::min(hy0, y); hx1 = std::max(hx1, x); hy1 = std::max(hy1, y); }
    bool first = true;
    for (auto a : def.Anim) {
        Canvas f;
        if (!ui.Load(a, f)) continue;
        int ox = 0, oy = 0;
        if ((f.Width != MAP_W || f.Height != MAP_H) && hx1 >= 0) {
            // a smaller frame (Magicland waves 647x362): align its picture with the map's hole
            int fx0 = f.Width, fy0 = f.Height;
            for (int y = 0; y < f.Height; y++) for (int x = 0; x < f.Width; x++)
                if ((f.Px[(size_t)y * f.Width + x] >> 24) != 0) { fx0 = std::min(fx0, x); fy0 = std::min(fy0, y); }
            ox = hx0 - fx0; oy = hy0 - fy0;
        }
        if (first) {
            for (int y = 0; y < f.Height; y++) for (int x = 0; x < f.Width; x++) {
                int dx = ox + x, dy = oy + y;
                if (dx < 0 || dy < 0 || dx >= MAP_W || dy >= MAP_H) continue;
                uint32_t& d = _Base.Px[(size_t)dy * MAP_W + dx];
                if ((d >> 24) == 0) d = f.Px[(size_t)y * f.Width + x];
            }
            first = false;
        }
        Spans t; t.Build(f, ox, oy, MAP_W, MAP_H);
        _AnimTop.push_back(std::move(t));
        Canvas full(MAP_W, MAP_H), sm;
        for (auto& p : full.Px) p = 0;
        for (int y = 0; y < f.Height; y++) for (int x = 0; x < f.Width; x++) {
            int dx = ox + x, dy = oy + y;
            if (dx >= 0 && dy >= 0 && dx < MAP_W && dy < MAP_H) full.Px[(size_t)dy * MAP_W + dx] = f.Px[(size_t)y * f.Width + x];
        }
        Resample(full, sm, 320, 240);
        Spans b; b.Build(sm, 0, 0, 320, 240);
        _AnimBottom.push_back(std::move(b));
        FSO::Files::LoadProgress::Progress("front: neighbourhood map", 0, 0); // ADDED(sims)
    }
    for (auto& p : _Base.Px) if ((p >> 24) == 0) p = RGBA(24, 48, 96); // any hole left: deep water
    FSO::Files::LoadProgress::Progress("front: neighbourhood map", 0, 0); // ADDED(sims)
    for (auto o : def.Over) {
        Canvas l;
        if (ui.Load(o, l)) _Over.push_back(std::move(l));
    }
    FSO::Files::LoadProgress::Progress("front: neighbourhood map", 0, 0); // ADDED(sims)
    _AnimMs = def.AnimMs > 0 ? def.AnimMs : 250;
    // banner, popup
    _Banner = Canvas();
    {   // FIX(sims) FB1: banner art stretched to the touch screen's width at the buttons' height
        Canvas full;
        if (ui.Load(_Area == Area::Neighbourhood || _Area == Area::MagicTown ? "kNghBarBkg" : "kDTBarBkg", full)) Resample(full, _Banner, 320, BAR_H);
    }
    _Popup = Canvas();
    Canvas alpha;
    if (ui.Load("kHotspotPopupBkg", _Popup, false) && ui.Load("kHotspotPopupAlphaBkg", alpha, false) && alpha.Width == _Popup.Width && alpha.Height == _Popup.Height) {
        for (size_t i = 0; i < _Popup.Px.size(); i++) {
            uint32_t a = alpha.Px[i] & 255;
            _Popup.Px[i] = (_Popup.Px[i] & 0x00FFFFFFu) | (std::min<uint32_t>(_FE->IsDesktop()?255:235, a) << 24);
        }
    }
    ui.Load("CPanel/People/PeopleTemplate.bmp",_PopupPortraitFrame);
    _CurrentBtn = Canvas();
    ui.LoadScaled("kNghCurrent", 0.62f, _CurrentBtn); // FIX(sims) FB1: 23x30 -> 14x19, fits the 26 px banner
    Composite();
}

void NeighbourhoodScreen::Composite() {
    auto& M = _FE->Model();
#ifndef __3DS__
    if(_FE->IsDesktop()){PrepareDesktopPresentation();return;}
#endif
    _OpenHouse = -1; // FIX(sims) FB1: lots reloaded
    _MapTop = _Base;
    for (auto& l : M.Lots) if (l.Thumb.Width > 0) _MapTop.Blit(l.Thumb.Px.data(), l.Thumb.Width, l.Thumb.Height, l.X, l.Y, true);
    for (auto& o : _Over) _MapTop.Blit(o.Px.data(), o.Width, o.Height, 0, 0, true);
    if (const char* dump = getenv("SIM3DS_MAPDUMP")) { // FIX(sims) FB1: host check of the lot placement (800x600 PPM)
        std::string fn = std::string(dump) + "/map_" + std::to_string(M.Current + 1) + "_" + std::to_string((int)_Area) + ".ppm";
        if (FILE* f = fopen(fn.c_str(), "wb")) {
            fprintf(f, "P6 %d %d 255\n", _MapTop.Width, _MapTop.Height);
            for (auto p : _MapTop.Px) { unsigned char rgb[3] = { (unsigned char)(p & 255), (unsigned char)((p >> 8) & 255), (unsigned char)((p >> 16) & 255) }; fwrite(rgb, 1, 3, f); }
            fclose(f);
        }
    }
    Resample(_MapTop, _MapBottom, 320, 240);
}

void NeighbourhoodScreen::RefreshLots() {
    auto& M = _FE->Model();
    int house = SelectedHouse();
#ifndef __3DS__
    if(!_FE->IsDesktop())
#endif
    M.LoadArea(_Area);
    Composite();
    _Sel = -1;
    for (size_t i = 0; i < M.Lots.size(); i++) if (M.Lots[i].House == house) _Sel = (int)i;
}

void NeighbourhoodScreen::BeginPick(Area a, const std::string& title, std::function<void(int)> done) {
    _PickDone = done;
    _PickTitle = title;
    Enter(a);
    // start on the first lot that can be visited
    auto& M = _FE->Model();
    for (size_t i = 0; i < M.Lots.size(); i++) if (M.Lots[i].Community) { _Sel = (int)i; break; }
    // FIX(sims) QA3: the title is already the mode line (top left); the tooltip banner repeated it.
}

void NeighbourhoodScreen::FinishPick(int house) {
    auto cb = _PickDone;
    _PickDone = nullptr;
    _PickTitle.clear();
    BuildButtons();
    if (cb) cb(house);
}

void NeighbourhoodScreen::BuildButtons() {
    auto& ui = ResGraphics::Get();
    auto& M = _FE->Model();
    auto& T = FrontStrings::Get();
    _Buttons.clear();
    auto add = [&](const std::string& id, const char* art, int x, const std::string& tip, int states = 4) -> UIButton& {
        UIButton b;
        b.Id = id;
        b.Load(ui, art, _FE->IsDesktop() ? 1.0f : BTN, states);
        b.X = x; b.Y = 0;
        if (_FE->IsDesktop()) {
            // Authored 800x600 PC toolbar: logo, neighbourhood selector, family,
            // eviction/import, expansion destinations, quit. Never a touch-screen strip.
            static const std::map<std::string,int> positions = {
                {"credits",0},{"prev",108},{"next",148},{"movein",205},
                {"bulldoze",256},{"import",307},{"downtown",358},
                {"vacation",460},{"studiotown",409},{"magictown",511},
                {"exit",562},{"return",748}};
            auto at = positions.find(id);
            if (at != positions.end()) b.X = at->second;
        }
        b.PadY = 3; // FIX(sims) FB1: a little below the banner still hits
        b.Tooltip = tip;
        if (b.Texture.Width == 0) { b.Texture.Resize(26 * states, 26); for (auto& p : b.Texture.Px) p = RGBA(40, 70, 150); }
        _Buttons.push_back(std::move(b));
        return _Buttons.back();
    };
    if (_PickDone) {
        // lot selection from a lot (taxi / phone): only the way back
        const AreaDef& def = GetAreaDef(_Area, M.Unleashed());
        add("return", def.ReturnButton ? def.ReturnButton : "NghUI/Return.bmp", 4, T.Get(152, 1, "Cancel"));
        return;
    }
    if (_Area == Area::Neighbourhood) {
        // FIX(sims) FB1: layout for the 0.5 banner (logo 35, buttons 27, Previous / Next 13 + the number 14 between)
        add("credits", "kNghSimsLogoEn", 1, T.Get(151, 8, "Credits"), 1);
        add("movein", "kNghMoveIn", 38, T.Get(151, 0, "Select or Create Family"));
        add("bulldoze", "kNghBulldoze", 65, T.Get(151, M.Unleashed() ? 10 : 1, "Evict or Bulldoze"));
        add("import", "kNghImport", 92, T.Get(151, 6, "Import Family")); // TS1(sims): OpenImport (.FAM)
        struct Tr { Area A; const char* Id; const char* Art; int Tip, Fail; };
        static const Tr tr[] = { { Area::Downtown, "downtown", "kNghExitToDowntown", 7, 0 }, { Area::Vacation, "vacation", "kNghExitToVacation", 9, 1 },
                                 { Area::StudioTown, "studiotown", "kNghExitToStudiotown", 11, 3 }, { Area::MagicTown, "magictown", "kNghExitToMagicland", 12, 4 } };
        int x = 122;
        for (auto& t : tr) {
            bool ok = M.AreaAvailable(t.A);
            auto& b = add(t.Id, t.Art, x, ok ? T.Get(151, t.Tip, "") : T.Get(225, t.Fail, "Please purchase the expansion pack"));
            b.Disabled = !ok;
            x += 27;
        }
        add("prev", "kNghPrevious", 233, T.Get(151, 4, "Switch To Previous Neighborhood")).PadX = 5;
        add("next", "kNghNext", 263, T.Get(151, 5, "Switch To Next Neighborhood")).PadX = 5;
        add("exit", "kNghExit", 293, T.Get(151, 2, "Quit"));
    } else {
        int tips = _Area == Area::Downtown ? 169 : _Area == Area::Vacation ? 170 : _Area == Area::StudioTown ? 173 : 174;
        const AreaDef& def = GetAreaDef(_Area, M.Unleashed());
        add("credits", _Area == Area::Downtown ? "kDowntownCredits" : _Area == Area::StudioTown ? "kStudiotownCredits" : "kNghSimsLogoEn", 1, T.Get(tips, 2, "Credits"), 1);
        add("return", def.ReturnButton, 38, T.Get(tips, 0, "Return to Neighborhood View"));
        add("bulldoze", _Area == Area::Downtown ? "kDowntownBulldoze" : _Area == Area::StudioTown ? "kStudiotownBulldoze" : "kNghBulldoze", 65, T.Get(tips, 4, "Bulldoze Lot"));
        add("import", _Area == Area::Downtown ? "kDowntownImport" : _Area == Area::Vacation ? "kVacationImport" : _Area == Area::StudioTown ? "kStudiotownImport" : "kMagictownImport",
            92, T.Get(tips, 3, "Import Lot")).Disabled = true; // STUB(3ds): lot import
        if (_Area == Area::MagicTown) add("movein", "kNghMoveIn", 119, T.Get(174, 5, "Select or Create Family"));
        add("exit", "kNghExit", 293, T.Get(151, 2, "Quit"));
    }
}

// ======================================================================= helpers

int NeighbourhoodScreen::SelectedHouse() const {
    auto& M = _FE->Model();
    return (_Sel >= 0 && _Sel < (int)M.Lots.size()) ? M.Lots[(size_t)_Sel].House : -1;
}

void NeighbourhoodScreen::SelectHouse(int house) {
    auto& M = _FE->Model();
    for (size_t i = 0; i < M.Lots.size(); i++) if (M.Lots[i].House == house) { _Sel = (int)i; _ManualCam = false; }
}

int NeighbourhoodScreen::LotAtBottom(int x, int y) const {
    auto& M = _FE->Model();
    int mx = (int)(x / BS), my = (int)(y / BS);
    // front-most lot whose picture has a pixel at (or right next to) the touch
    for (int i = (int)M.Lots.size() - 1; i >= 0; i--) {
        auto& l = M.Lots[(size_t)i];
        for (int dy = -3; dy <= 3; dy += 3) for (int dx = -3; dx <= 3; dx += 3) {
            int px = mx + dx - l.X, py = my + dy - l.Y;
            if (px < 0 || py < 0 || px >= l.Thumb.Width || py >= l.Thumb.Height) continue;
            if ((l.Thumb.Px[(size_t)py * l.Thumb.Width + px] >> 24) > 0) return i;
        }
    }
    // the closest lot centre within 14 touch pixels (small or hidden pictures)
    int best = -1, bestD = 14 * 14;
    for (int i = 0; i < (int)M.Lots.size(); i++) {
        auto& l = M.Lots[(size_t)i];
        int cx = (int)((l.X + std::max(20, l.Thumb.Width) / 2) * BS), cy = (int)((l.Y + std::max(12, l.Thumb.Height) / 2) * BS);
        int d = (cx - x) * (cx - x) + (cy - y) * (cy - y);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void NeighbourhoodScreen::EnterLot(int house) {
    Sound("ui_nhood_click");
    _FE->RequestedLot = house;
    _FE->Log += "enter lot " + std::to_string(house) + "\n";
}

void NeighbourhoodScreen::OpenPicker(int target) {
    auto& T = FrontStrings::Get();
    Picker.Desktop=_FE->IsDesktop();
    Picker.Show(&_FE->Model(), target);
    Picker.OnCancel = [this]() { Picker.Close(); SetToolMode(Mode::Normal); };
    Picker.OnMoveIn = [this](int16_t fam) {
        int target = Picker.TargetLot;
        Picker.Close();
        if (target >= 0) { TryMoveIn(fam, target); return; }
        SetToolMode(Mode::MoveIn);
        _MovingFamily = fam;
        auto f = _FE->Model().Family(fam);
        Tip(f ? "Move In: " + Format(FrontStrings::Get().Get(134, 0, "The %s Family"), { f->Name }).substr(0, std::string::npos) : std::string("Move In"));
    };
    Picker.OnCreate = [this, target]() {
        // "Create New Family": the Create-A-Sim screen (src/app/cas, see CASBridge.h)
        Picker.Close();
        auto& M = _FE->Model();
        _FE->ShowSubScreen(MakeCASScreen(_FE->_Cfg.DataRoot, M.UserDir, M.Current), [this, target]() {
            // CAS saved the new family into Neighborhood.iff (+ its User*.iff): re-open the neighbourhood
            std::string err;
            _FE->Model().Switch(_FE->Model().Current, _FE->_Setup, err);
            RefreshLots();
            if (_FE->Music) _FE->Music(GetAreaDef(_Area, _FE->Model().Unleashed()).Music);
            OpenPicker(target);
        });
    };
    Picker.OnDelete = [this, &T](int16_t fam) {
        auto f = _FE->Model().Family(fam);
        if (!f) return;
        std::string name = PersonalFamilyLabel(*f);
        Dialog.Show(InterfaceLabel(T.Get(128, 6, "Delete Family?")), Format(InterfaceLabel(T.Get(128, 7, "Are you sure you want to delete the \"%s\" family?")), { name }),
                    { T.Get(152, 2, "Yes"), T.Get(152, 3, "No") }, [this, fam](int r) {
            if (r != 0) return;
            if (_FE->Model().DeleteFamily(fam)) { _FE->SaveNeighbourhood(); _FE->Log += "deleted family\n"; }
        });
    };
}

// Explicit .FAM exports, distinct from installer defaults and the local family bin.
void NeighbourhoodScreen::OpenImport() {
    auto& M = _FE->Model();
    auto entries=ReadFamilyImportExports(_FE->SaveRoot(),_FE->_Cfg.DataRoot);
    Picker.Desktop=_FE->IsDesktop();
    Picker.ShowImport(&M, std::move(entries));
    Picker.OnCancel = [this]() { Picker.Close(); SetToolMode(Mode::Normal); };
    Picker.OnImport = [this](const std::string& path) {
        auto& T = FrontStrings::Get();
        FSO::Content::TS1::TS1FamFile fam;
        std::string err;
        Picker.Close();
        if (!FSO::Content::TS1::TS1FamilyImport::Read(path, fam, &err)) { Dialog.Show(T.Get(131, 10, "Error"), err, { T.Get(152, 0, "OK") }, nullptr); return; }
        FSO::Content::TS1::TS1FamilyImport::Options opts;
        opts.MoveIntoHouse = false; // TS1: an imported family arrives in the family bin
        opts.WriteHouse = false;
        auto r = FSO::Content::TS1::TS1FamilyImport::Import(_FE->Model().UserDir, fam, opts);
        if (!r.Ok) { Dialog.Show(T.Get(131, 10, "Error"), r.Error, { T.Get(152, 0, "OK") }, nullptr); return; }
        _FE->Log += "imported family " + fam.Name + "\n";
        Sound("ui_nhood_click");
        std::string e2;
        _FE->Model().Switch(_FE->Model().Current, _FE->_Setup, e2); // re-read Neighborhood.iff + the new characters
        RefreshLots();
        OpenPicker(-1); // the family bin, with the imported family
    };
}

void NeighbourhoodScreen::TryMoveIn(int16_t fam, int house) {
    auto& M = _FE->Model();
    auto& T = FrontStrings::Get();
    auto lot = M.Lot(house);
    if (!lot) return;
    auto r = M.CanMoveIn(fam, house);
    // FIX(sims) FB1: every move-in attempt in log.txt (neighbourhood folder, family, lot, result) for hardware reports
    FSO::Files::LoadProgress::Step("front: move in family " + std::to_string(fam) + " -> house " + std::to_string(house) + " in " + M.UserDir + ": result " + std::to_string((int)r));
    std::vector<std::string> ok = { T.Get(152, 0, "OK") };
    switch (r) {
        case NhoodModel::MoveResult::Occupied: Dialog.Show(T.Get(132, 14, "House Occupied"), T.Get(132, 15, "This house is occupied by another family."), ok, nullptr); return;
        case NhoodModel::MoveResult::Community:
            if (house >= 90) Dialog.Show(T.Get(132, 22, "Magic Town Lot"), T.Get(132, 23, "You cannot move a family into the lots in Calamity Forest."), ok, nullptr);
            else Dialog.Show(T.Get(132, 12, "Community Lot"), T.Get(132, 13, "You cannot move a family into a community lot."), ok, nullptr);
            return;
        case NhoodModel::MoveResult::NotEnoughMagiCoins:
        case NhoodModel::MoveResult::CantAfford: Sound("ui_nhood_error"); Dialog.Show(T.Get(132, 2, "Sorry"), T.Get(132, 3, "You can't afford it."), ok, nullptr); return;
        case NhoodModel::MoveResult::Missing: Dialog.Show(T.Get(132, 18, "Error!"), T.Get(132, 19, "Could not move in!"), ok, nullptr); return;
        default: break;
    }
    bool emptyLot = house >= 90 && house <= 92 ? lot->MagicHouseFlag == 0 : lot->EmptyLot;
    Dialog.Show(T.Get(132, emptyLot ? 6 : 0, "Purchase House?"), T.Get(132, emptyLot ? 7 : 1, "Are you sure you want to buy this house?"),
                { T.Get(152, 2, "Yes"), T.Get(152, 3, "No") }, [this, fam, house](int res) {
        auto& M = _FE->Model();
        std::string reason;
        auto result = M.PurchaseForEntry(fam, house, res == 0, _FE->PurchasedSession, reason);
        if (result == NhoodModel::MoveResult::Cancelled) return;
        if (result != NhoodModel::MoveResult::Ok) {
            auto& strings = FrontStrings::Get();
#ifndef __3DS__
            if (_Original) _Original->View.hover_cache.clear();
#endif
            _FE->Log += "move-in failed: " + (reason.empty() ? _FE->Error : reason) + "\n";
            Dialog.Show(strings.Get(132,18,"Error!"), strings.Get(132,19,"Could not move in!"),
                        {strings.Get(152,0,"OK")}, nullptr);
            return;
        }
        _FE->RequestedLot = house; // The purchase is already durable.
        Sound("ui_buy_place");
        SetToolMode(Mode::Normal); _MovingFamily = -1;
        if (!_FE->PurchasedSession) { RefreshLots(); SelectHouse(house); }
        _FE->Log += "moved in to " + std::to_string(house) + "\n";
        EnterLot(house); // the game goes straight into the new home
    });
}

void NeighbourhoodScreen::TryEvict(int house) {
    auto& M = _FE->Model();
    auto& T = FrontStrings::Get();
    auto lot = M.Lot(house);
    if (!lot) return;
    std::vector<std::string> ok = { T.Get(152, 0, "OK") };
    if (lot->Family >= 0) {
        if(M.FamilyAway((int16_t)lot->Family)) {
            Dialog.Show(T.Get(131,17,"Not So Fast!"),T.Get(131,16,"This family is currently on vacation. Strict laws forbid evictions of Sims while they are away from home."),ok,nullptr);
            return;
        }
        auto f = M.Family(lot->Family);
        int worth = f ? f->Budget + f->NetWorth : 0;
        Dialog.Show(InterfaceLabel(T.Get(131, 2, "Evict Family?")), Format(InterfaceLabel(T.Get(131, 3, "The %s family has net worth of %s.  Do you want to evict them?")), { f ? PersonalFamilyLabel(*f) : "", InterfaceLabel(Simoleons(worth)) }),
                    { T.Get(152, 2, "Yes"), T.Get(152, 3, "No") }, [this, house](int r) {
            if (r != 0) return;
            if (_FE->Model().Evict(house)) {
                _FE->SaveNeighbourhood();
                Sound("ui_nhood_bdoze_evict");
                RefreshLots();
                _FE->Log += "evicted " + std::to_string(house) + "\n";
            }
        });
    } else if (!lot->EmptyLot) {
        Dialog.Show(T.Get(131, 0, "Bulldoze House?"), T.Get(131, 1, "Do you want to bulldoze this house?"), { T.Get(152, 2, "Yes"), T.Get(152, 3, "No") }, [this, &T, house](int r) {
            if (r != 0) return;
            // TS1(sims): front::BulldozeLot (src/app/front/LotSave.h)
            std::string err, lg;
            if (front::BulldozeLot(house, err, lg)) {
                Sound("ui_nhood_bdoze_evict");
                _FE->Log += lg;
                RefreshLots();
            } else Dialog.Show(T.Get(131, 10, "Error"), err, { T.Get(152, 0, "OK") }, nullptr);
        });
    } else {
        Dialog.Show(T.Get(131, 4, "Nothing to Bulldoze"), T.Get(131, 5, "There is nothing here to bulldoze."), ok, nullptr);
    }
}

void NeighbourhoodScreen::TryRezone(int house) {
    auto& M=_FE->Model();auto& T=FrontStrings::Get();
    auto* lot=M.Lot(house);
    if(_PickDone||_Area!=Area::Neighbourhood||!M.Unleashed()||!lot||!lot->Exists)return;
    const bool previous=lot->Community;const int16_t family=lot->Family;
    Dialog.ShowChoice(InterfaceLabel(T.Get(250,0)),
        Format(InterfaceLabel(T.Get(250,1)),{InterfaceLabel(T.Get(251,previous?1:0))}),
        {InterfaceLabel(T.Get(251,0)),InterfaceLabel(T.Get(251,1))},
        {T.Get(152,0,"OK"),T.Get(152,1,"Cancel")},
        [this,house,previous,family](int result,int choice){
            if(result!=0||choice<0||choice>1||bool(choice)==previous)return;
            auto& M=_FE->Model();auto& T=FrontStrings::Get();auto* lot=M.Lot(house);
            if(!lot||lot->Community!=previous||lot->Family!=family)return;
            const bool community=choice!=0;
            const auto commit=[this,house,previous,family,community](){
                auto& M=_FE->Model();auto& T=FrontStrings::Get();auto* current=M.Lot(house);
                // No stale prompt may evict a different family or zone a changed lot.
                if(!current||current->Community!=previous||current->Family!=family)return;
                std::string error;
                if(!M.Rezone(house,community,error)){
                    Dialog.Show(T.Get(131,10,"Error"),error,{T.Get(152,0,"OK")},nullptr);return;
                }
                RefreshLots();SelectHouse(house);
                _FE->Log+="rezoned "+std::to_string(house)+"\n";
            };
            const auto demolition=[this,commit](){
                auto& T=FrontStrings::Get();
                Dialog.Show(InterfaceLabel(T.Get(131,8)),InterfaceLabel(T.Get(131,9)),
                    {T.Get(152,2,"Yes"),T.Get(152,3,"No")},
                    [commit](int result){if(result==0)commit();});
            };
            if(family>=0){
                if(M.FamilyAway(family)){
                    Dialog.Show(T.Get(131,17),T.Get(131,16),{T.Get(152,0,"OK")},nullptr);return;
                }
                const auto* f=M.Family(family);if(!f)return;
                const auto total=std::int64_t(f->Budget)+f->NetWorth;
                if(total<-2147483648LL||total>2147483647LL)return;
                Dialog.Show(InterfaceLabel(T.Get(131,6)),
                    Format(InterfaceLabel(T.Get(131,7)),{PersonalFamilyLabel(*f),InterfaceLabel(Simoleons(int(total)))}),
                    {T.Get(152,2,"Yes"),T.Get(152,3,"No")},
                    [demolition](int result){if(result==0)demolition();});
            }else demolition();
        });
}

void NeighbourhoodScreen::Activate(int idx) {
    auto& M = _FE->Model();
    if (idx < 0 || idx >= (int)M.Lots.size()) return;
    auto& lot = M.Lots[(size_t)idx];
    if (_PickDone) {
        auto& T = FrontStrings::Get();
        if (lot.Community) { Sound("ui_nhood_click"); FinishPick(lot.House); }
        else Dialog.Show(lot.Name.empty() ? lot.Address : lot.Name, T.Get(134, 20, "x\nx\nThis is a Residential lot. You cannot visit here.").substr(T.Get(134, 20, "x\nx\nThis is a Residential lot. You cannot visit here.").rfind('\n') + 1),
                         { T.Get(152, 0, "OK") }, nullptr);
        return;
    }
    if (_Mode == Mode::Rezone) { TryRezone(lot.House); return; }
    if (_Mode == Mode::Evict) { TryEvict(lot.House); return; }
    if (_Mode == Mode::MoveIn && _MovingFamily >= 0) { TryMoveIn(_MovingFamily, lot.House); return; }
    if (_FE->IsDesktop() || lot.Family >= 0 || lot.Community) { EnterLot(lot.House); return; }
    // an empty house: pick or create a family to move in (STR#134 #1 "Pick or create a family and move them in to play.")
    OpenPicker(lot.House);
}

void NeighbourhoodScreen::GoToNeighbourhood(int n) {
    std::string err;
    auto& M = _FE->Model();
    if (!M.Switch(n, _FE->_Setup, err)) {
        if (M.BrokenIndex == n) { OfferRestore(n); return; } // FIX(sims) SAVE
        Dialog.Show("Error", err, { "OK" }, nullptr);
        return;
    }
    _FE->Settings().LastNeighbourhood = n;
    _FE->SaveSettings();
    Enter(Area::Neighbourhood);
    _FE->Log += "neighbourhood " + std::to_string(n + 1) + "\n";
}

// FIX(sims) SAVE: the user's v6 card: Neighborhood.iff of UserData was gone after a save; switching to it said
// "Could not open ...", left the provider unloaded (every neighbourhood looked empty) and the "empty neighbourhood:
// create a new one?" path could not help (the folder's setup marker was there). Now: the reason, and a restore of only
// that neighbourhood from the original game.
void NeighbourhoodScreen::OfferRestore(int n) {
    auto& T = FrontStrings::Get();
    std::string name = T.Get(145, 1, "Neighborhood") + " " + std::to_string(n + 1);
    std::string folder = UserDataSetup::FolderName(n);
    _FE->Log += "broken neighbourhood " + std::to_string(n + 1) + "\n";
    if (!_FE->_Setup.CanRestore(n)) {
        Dialog.Show(name, "The save file of this neighborhood (" + folder + "/Neighborhood.iff) is missing or damaged, and the "
                    "game's original could not be found.", { T.Get(152, 0, "OK") }, nullptr);
        return;
    }
    Dialog.Show(name, "Could not be loaded: its save file (" + folder + "/Neighborhood.iff) is missing or damaged, with no "
                "backup.\n\nRestore it from the original game? Its families and houses go back to how they were when "
                "installed. Other neighborhoods stay as they are.",
                { "Restore", T.Get(152, 3, "No") }, [this, n, name](int r) {
        if (r != 0) return;
        std::string err;
        FSO::Files::LoadProgress::Step("front: restoring " + UserDataSetup::FolderName(n) + " from the install");
        if (!_FE->_Setup.RestoreFromInstall(n, &err)) { Dialog.Show(name, "The neighborhood could not be restored: " + err, { "OK" }, nullptr); return; }
        GoToNeighbourhood(n);
    });
}

void NeighbourhoodScreen::SwitchNeighbourhood(int dir) {
    auto& T = FrontStrings::Get();
    auto& M = _FE->Model();
    int n = M.Current + dir;
    if (n < 0) n = UserDataSetup::MaxNeighbourhoods - 1;
    if (n >= UserDataSetup::MaxNeighbourhoods) n = 0;
    auto avail = _FE->_Setup.Available();
    bool exists = std::find(avail.begin(), avail.end(), n) != avail.end();
    if (exists) { GoToNeighbourhood(n); return; }
    auto broken = _FE->_Setup.Broken(); // FIX(sims) SAVE: set up, but its Neighborhood.iff is gone
    if (std::find(broken.begin(), broken.end(), n) != broken.end()) { OfferRestore(n); return; }
    if (!_FE->_Setup.HasTemplates()) { Tip("No other neighborhoods."); return; }
    // TS1(sims) wording: the game silently created UserDataN from TemplateUserData
    Dialog.Show(T.Get(145, 1, "Neighborhood") + " " + std::to_string(n + 1), "This neighborhood is empty. Create a new neighborhood here?",
                { T.Get(152, 2, "Yes"), T.Get(152, 3, "No") }, [this, n](int r) {
        if (r != 0) return;
        _FE->_Setup.PlanNew(n);
        while (_FE->_Setup.Step(1u << 30)) {}
        GoToNeighbourhood(n);
    });
}

std::string NeighbourhoodScreen::DesktopCursorPath() const {
    if(Dialog.Open || Picker.Open)return "Shared/cursors/Arrow.cur";
    if(_Mode==Mode::Rezone)return "Shared/cursors/Rezone.cur";
    return _Mode==Mode::Evict?"Shared/cursors/BULLDOZE.cur":"Shared/cursors/Arrow.cur";
}

void NeighbourhoodScreen::OnButton(const std::string& id) {
    auto& T = FrontStrings::Get();
    if(id=="rezone"){
        if(_PickDone||_Area!=Area::Neighbourhood||!_FE->Model().Unleashed())return;
        Sound("ui_click");SetToolMode(_Mode==Mode::Rezone?Mode::Normal:Mode::Rezone);return;
    }
    if(id=="bulldoze"){
        if(!_FE->IsDesktop())Sound("ui_click");
        SetToolMode(_Mode==Mode::Evict?Mode::Normal:Mode::Evict);return;
    }
    // Another banner action leaves the tool before navigating/replacing UI.
    if(_FE->IsDesktop() && (_Mode==Mode::Evict||_Mode==Mode::Rezone))SetToolMode(Mode::Normal);
    Sound("ui_click");
    if (id == "credits") { _FE->ShowSubScreen(MakeCreditsScreen()); return; }
    if (id == "movein") { SetToolMode(Mode::Normal); OpenPicker(-1); return; }
    if (id == "import" && _Area == Area::Neighbourhood) { SetToolMode(Mode::Normal); OpenImport(); return; }
    if (id == "downtown") { Enter(Area::Downtown); return; }
    if (id == "vacation") { Enter(Area::Vacation); return; }
    if (id == "studiotown") { Enter(Area::StudioTown); return; }
    if (id == "magictown") { Enter(Area::MagicTown); return; }
    if (_PickDone && (id == "return" || id == "exit")) { FinishPick(-1); return; }
    if (id == "return") { Enter(Area::Neighbourhood); return; }
    if (id == "prev") { SwitchNeighbourhood(-1); return; }
    if (id == "next") { SwitchNeighbourhood(+1); return; }
    if (id == "exit") {
        Dialog.Show(T.Get(153, 1, "Quit?"), T.Get(153, 2, "Are you sure you want to quit?"), { T.Get(152, 2, "Yes"), T.Get(152, 3, "No") }, [this](int r) {
            if (r == 0) { _FE->SaveSettings(); _FE->QuitRequested = true; }
        });
        return;
    }
}

// ======================================================================= frame

void NeighbourhoodScreen::RecordDrawnFrame(bool modalDrawn) {
    _DrawnFrame.scene = FramePresentation::Scene::Map;
    _DrawnFrame.area = static_cast<int>(_Area);
    _DrawnFrame.modal = modalDrawn;
    _DrawnFrame.family_picker = Picker.Open;
    _DrawnFrame.destination_picker = Picking();
}

void NeighbourhoodScreen::Frame(const AppInput& in, double dtMs, Canvas& top, Canvas& bottom) {
    _DrawnFrame = {};
    auto& M = _FE->Model();
    _Time += dtMs;
    if (_TipTime > 0) _TipTime -= dtMs;
#ifndef __3DS__
    if(_FE->IsDesktop()){FrameDesktop(in,top,dtMs);return;}
#endif
    // ---------------- input (dialog > picker > banner > map)
    if (Dialog.Open) Dialog.Update(in);
    else if (Picker.Open) Picker.Update(in);
    else {
        bool consumed = false;
        for (auto& b : _Buttons) {
            bool click = b.Update(in);
            if (b.Pressed() || (in.TouchDown && b.Bounds().Contains(in.TouchX, in.TouchY))) { _Tip = b.Tooltip; _TipTime = 2000; consumed = true; }
            if (b.Id == "bulldoze") b.Selected = _Mode == Mode::Evict;
            if (click) { OnButton(b.Id); consumed = true; break; }
        }
        if (_FE->RequestedLot >= 0) return;
        if (in.TouchDown && in.TouchY < BAR_H) consumed = true; // the banner
        if (!consumed && in.TouchDown) {
            int i = LotAtBottom(in.TouchX, in.TouchY);
            if (i >= 0) {
                if (i == _Sel) Activate(i);
                else { _Sel = i; _ManualCam = false; Sound("ui_nhood_rollover"); }
            } else if (_Mode != Mode::Normal) { SetToolMode(Mode::Normal); _MovingFamily = -1; }
            else {
                // touching empty map: the top screen looks there
                _CamX = in.TouchX / BS - 200; _CamY = in.TouchY / BS - 120; _ManualCam = true;
            }
        }
        // buttons: d-pad walks the lots, A = click the lot, B = cancel the mode, X = family bin, START = quit
        if (!M.Lots.empty() && (in.Down & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN))) {
            int cur = std::max(0, _Sel);
            auto& c = M.Lots[(size_t)cur];
            int cx = c.X + c.Thumb.Width / 2, cy = c.Y + c.Thumb.Height / 2;
            int best = -1; int64_t bestScore = int64_t{1} << 40;
            for (int i = 0; i < (int)M.Lots.size(); i++) {
                if (i == cur) continue;
                auto& l = M.Lots[(size_t)i];
                int dx = l.X + l.Thumb.Width / 2 - cx, dy = l.Y + l.Thumb.Height / 2 - cy;
                bool okDir = ((in.Down & BTN_LEFT) && dx < -8) || ((in.Down & BTN_RIGHT) && dx > 8) || ((in.Down & BTN_UP) && dy < -8) || ((in.Down & BTN_DOWN) && dy > 8);
                if (!okDir) continue;
                long along = (in.Down & (BTN_LEFT | BTN_RIGHT)) ? std::abs(dx) : std::abs(dy);
                long across = (in.Down & (BTN_LEFT | BTN_RIGHT)) ? std::abs(dy) : std::abs(dx);
                long score = along + across * 2;
                if (score < bestScore) { bestScore = score; best = i; }
            }
            if (best >= 0) { _Sel = best; _ManualCam = false; Sound("ui_nhood_rollover"); }
        }
        if ((in.Down & BTN_A) && _Sel >= 0) Activate(_Sel);
        if (_PickDone) { if (in.Down & (BTN_B | BTN_START)) FinishPick(-1); }
        else {
        if (in.Down & BTN_B) { SetToolMode(Mode::Normal); _MovingFamily = -1; }
        if (in.Down & BTN_X) OpenPicker(-1);
        if (in.Down & BTN_START) OnButton("exit");
        if (in.Down & BTN_L) { for (auto& b : _Buttons) if (b.Id == "prev") { SwitchNeighbourhood(-1); break; } }
        if (in.Down & BTN_R) { for (auto& b : _Buttons) if (b.Id == "next") { SwitchNeighbourhood(+1); break; } }
        }
        // circle pad / c-stick: look around on the top screen
        float cx = in.CircleX + in.CStickX, cy = in.CircleY + in.CStickY;
        if (std::fabs(cx) > 0.15f || std::fabs(cy) > 0.15f) {
            float px = 320.0f * (float)dtMs / 1000.0f;
            _CamX += cx * px; _CamY -= cy * px; _ManualCam = true;
        }
    }
    if (_FE->RequestedLot >= 0 || _FE->CurrentState() == FrontEnd::State::SubScreen) return;
    // ---------------- camera follows the selection
    if (!_ManualCam && _Sel >= 0 && _Sel < (int)M.Lots.size()) {
        auto& l = M.Lots[(size_t)_Sel];
        // the lot in the left half, its popup beside it on the right
        float tx = (float)(l.X + l.Thumb.Width / 2) - 96, ty = (float)(l.Y + l.Thumb.Height / 2) - 122;
        float k = std::min(1.0f, (float)dtMs / 140.0f);
        _CamX += (tx - _CamX) * k; _CamY += (ty - _CamY) * k;
    }
    _CamX = std::max(0.0f, std::min((float)(MAP_W - top.Width), _CamX));
    _CamY = std::max(0.0f, std::min((float)(MAP_H - top.Height), _CamY));
    DrawTop(top);
    const bool modalDrawn = DrawBottom(bottom);
    RecordDrawnFrame(modalDrawn);
}

// ======================================================================= drawing

void NeighbourhoodScreen::DrawPopup(Canvas& top, const LotInfo& lot, int sx, int sy) {
    auto& M = _FE->Model();
    const FamilyInfo* moving = (_Mode == Mode::MoveIn) ? M.Family(_MovingFamily) : nullptr;
    std::string text = M.Rollover(lot, moving);
    if (_Mode == Mode::Evict) {
        auto& T = FrontStrings::Get();
        if (lot.Family >= 0) text += "\n" + T.Get(134, 9, "x\nx\nx\nClick to evict this family.").substr(T.Get(134, 9, "x\nx\nx\nClick to evict this family.").rfind('\n') + 1);
        else if (!lot.EmptyLot) text += "\n" + T.Get(134, 10, "Click to demolish this house.");
        else text += "\n" + T.Get(134, 11, "There is no one to evict and nothing to demolish.");
    }
    if(_FE->IsDesktop()) {
        if(_Popup.Width>0) top.Blit(_Popup.Px.data(),_Popup.Width,_Popup.Height,sx,sy,true);
        auto colour=RGBA(187,198,195);
        Text(top,TS1Size::S10,sx+14,sy+4,Fit(TS1Size::S10,lot.Address,178),colour);
        int y=sy+40;
        auto lines=Wrap(TS1Size::S9,text,178);
        const auto* family=lot.Family>=0?M.Family(lot.Family):nullptr;
        for(const auto& line:lines) {
            if(y+TextH(TS1Size::S9)>sy+(family?108:155)) break;
            Text(top,TS1Size::S9,sx+14,y,line,colour); y+=19;
        }
        if(family) for(size_t i=0;i<family->Members.size() && i<8;++i) {
            int x=sx+14+int(i%6)*25,py=sy+110+int(i/6)*25;
            Canvas frame;
            if(_PopupPortraitFrame.Width>=180) {
                Crop(_PopupPortraitFrame,frame,0,0,45,45);
                top.BlitScaled(frame.Px.data(),45,45,Rect(x,py,24,24),true);
            }
            const auto& face=family->Members[i].Face;
            if(face.Width>0 && face.Height>0) {
                float scale=std::min(22.f/face.Width,22.f/face.Height);
                int w=int(face.Width*scale),h=int(face.Height*scale);
                top.BlitScaled(face.Px.data(),face.Width,face.Height,Rect(x+1+(22-w)/2,py+1+(22-h)/2,w,h),true);
            }
        }
        return;
    }
    int pw = _Popup.Width > 0 ? _Popup.Width : 200, ph = _Popup.Height > 0 ? _Popup.Height : 166;
    auto lines = Wrap(TS1Size::S10, text, pw - 26);
    // long descriptions (Magic Town, Vacation): keep the last lines (cost / instructions), shorten the middle
    if (lines.size() > 9) {
        std::vector<std::string> keep(lines.begin(), lines.begin() + 5);
        keep.back() += "...";
        size_t tail = 0;
        for (size_t i = 0; i < lines.size(); i++) if (lines[i].rfind("Cost:", 0) == 0 || lines[i].rfind("This is", 0) == 0 || lines[i].rfind("Please", 0) == 0 || lines[i].rfind("Pick or", 0) == 0) { tail = i; break; }
        if (tail > 5) keep.insert(keep.end(), lines.begin() + tail, lines.end());
        if (keep.size() > 10) keep.resize(10);
        lines = keep;
    }
    // occupied: TS1(sims) adds the members under the family line
    std::string members;
    if (lot.Family >= 0) if (auto f = M.Family(lot.Family)) for (auto& m : f->Members) members += (members.empty() ? "" : ", ") + m.FirstName;
    int needH = 30 + (int)lines.size() * 13 + (members.empty() ? 0 : 60);
    ph = std::max(ph, needH);
    if (_Popup.Width > 0 && ph == _Popup.Height) top.Blit(_Popup.Px.data(), _Popup.Width, _Popup.Height, sx, sy, true);
    else {
        auto& art = FrontArt::Get();
        if (art.PopupTiler.Width > 0) NineSlice(top, art.PopupTiler, Rect(sx, sy, pw, ph), 30);
        else top.FillRoundRect(sx, sy, pw, ph, 12, RGBA(0, 0, 80, 220));
    }
    int y = sy + 14;
    for (size_t i = 0; i < lines.size(); i++) {
        bool head = i == 0;
        Text(top, head ? TS1Size::S11 : TS1Size::S10, sx + 13, y, lines[i], head ? RGBA(255, 216, 96) : RGBA(255, 255, 255), RGBA(0, 0, 30, 200));
        y += head ? 16 : 13;
    }
    if (!members.empty()) {
        y += 4;
        // the family's faces (character files' BMP_ 2002), like the family portraits of the original's family view
        if (auto f = M.Family(lot.Family)) {
            int fx = sx + 13;
            bool any = false;
            for (auto& m : f->Members) {
                if (m.Face.Width == 0 || fx + 22 > sx + pw - 10) continue;
                Canvas small;
                Resample(m.Face, small, 20, 24);
                top.FillRect(fx - 1, y - 1, 22, 26, RGBA(0, 0, 40));
                top.Blit(small.Px.data(), small.Width, small.Height, fx, y, true);
                fx += 24; any = true;
            }
            if (any) y += 28;
        }
        for (auto& l : Wrap(TS1Size::S9, members, pw - 26)) { Text(top, TS1Size::S9, sx + 13, y, l, RGBA(150, 200, 255), RGBA(0, 0, 30, 180)); y += 12; }
    }
}

void NeighbourhoodScreen::DrawTop(Canvas& top) {
    auto& M = _FE->Model();
    int cx = (int)_CamX, cy = (int)_CamY;
    for (int y = 0; y < top.Height; y++) {
        int sy = cy + y;
        if (sy < 0 || sy >= _MapTop.Height) continue;
        int copyW = std::min(top.Width, _MapTop.Width - cx);
        if (copyW > 0) std::copy_n(_MapTop.Px.begin() + (size_t)sy * _MapTop.Width + cx, copyW, top.Px.begin() + (size_t)y * top.Width);
    }
    if (!_AnimTop.empty()) {
        // 1.0 neighbourhood deltas ping-pong; the wave loops cycle
        int n = (int)_AnimTop.size();
        int f = (int)(_Time / _AnimMs) % n;
        _AnimTop[(size_t)f].Draw(top, cx, cy);
        // the lots stay in front of the water
        for (auto& l : M.Lots) {
            if (l.Thumb.Width == 0 || l.X + l.Thumb.Width < cx || l.X > cx + top.Width || l.Y + l.Thumb.Height < cy || l.Y > cy + top.Height) continue;
            top.Blit(l.Thumb.Px.data(), l.Thumb.Width, l.Thumb.Height, l.X - cx, l.Y - cy, true);
        }
        for (auto& o : _Over) BlitWindow(top, o, cx, cy);
    }
    // the selected lot glows (TS1 lights the house under the cursor)
    if (_Sel >= 0 && _Sel < (int)M.Lots.size()) {
        auto& l = M.Lots[(size_t)_Sel];
        float pulse = 0.5f + 0.5f * std::sin((float)_Time / 260.0f);
        uint32_t tint = _Mode == Mode::Evict ? RGBA(255, 60, 40) : (_Mode == Mode::MoveIn ? RGBA(120, 255, 120) : RGBA(255, 255, 210));
        int a = _FE->IsDesktop() ? 0 : 30 + (int)(50 * pulse);
        // FIX(sims) FB1: the selected house shows its inside (BMP_ 512 over the roofed picture, THMB AddYOff lower), as
        // the game does for the house under the cursor (FreeSO UINeighborhoodHouseButton HouseOpenTex)
        if (_OpenHouse != l.House) { _OpenPic = Canvas(); M.OpenPicture(l, _OpenPic); _OpenHouse = l.House; }
        const Canvas& pic = _OpenPic.Width > 0 ? _OpenPic : l.Thumb;
        int py0 = _OpenPic.Width > 0 ? l.Y + l.OpenDY : l.Y;
        if (_OpenPic.Width > 0) top.Blit(_OpenPic.Px.data(), _OpenPic.Width, _OpenPic.Height, l.X - cx, py0 - cy, true);
        for (int y = 0; y < pic.Height; y++) for (int x = 0; x < pic.Width; x++) {
            uint32_t p = pic.Px[(size_t)y * pic.Width + x];
            if ((p >> 24) < 128) continue;
            top.BlendA(l.X - cx + x, py0 - cy + y, tint, a);
        }
        // popup beside the lot
        int lx = l.X - cx, ly = l.Y - cy;
        int pw = _Popup.Width > 0 ? _Popup.Width : 200, ph = _Popup.Height > 0 ? _Popup.Height : 166;
        int sx = lx + l.Thumb.Width / 2 + 40;
        if (sx + pw > top.Width) sx = lx + l.Thumb.Width / 2 - 40 - pw;
        sx = std::max(2, std::min(top.Width - pw - 2, sx));
        int sy = ly + l.Thumb.Height / 2 - ph / 2;
        sy = std::max(2, std::min(top.Height - ph - 2, sy));
        if (_FE->IsDesktop()) { sx = std::max(2, std::min(top.Width-pw-2,lx+(l.Thumb.Width-pw)/2)); sy = std::max(54,ly-ph-6); }
        if (!Picker.Open && !_FE->IsDesktop()) DrawPopup(top, l, sx, sy);
    }
    // mode line (top-left), tooltip (bottom)
    auto& T = FrontStrings::Get();
    std::string mode;
    if (_PickDone) mode = _PickTitle;
    else if (_Mode == Mode::Evict) mode = T.Get(151, M.Unleashed() ? 10 : 1, "Evict or Bulldoze");
    else if (_Mode == Mode::MoveIn) { if (auto f = M.Family(_MovingFamily)) mode = "Move In: " + f->Name; }
    if (!mode.empty()) {
        mode = Fit(TS1Size::S12, mode, top.Width - 24); // FIX(sims) REL: long titles ran off the right edge ("...drifting nearb")
        int w = TextW(TS1Size::S12, mode) + 16;
        top.FillRoundRect(4, 4, w, 20, 8, RGBA(0, 0, 60, 190));
        Text(top, TS1Size::S12, 12, 6, mode, _Mode == Mode::Evict ? RGBA(255, 140, 120) : RGBA(160, 255, 160), RGBA(0, 0, 0, 200));
    }
    if (_TipTime > 0 && !_Tip.empty()) {
        int w = std::min(top.Width - 8, TextW(TS1Size::S12, _Tip) + 20);
        Rect r((top.Width - w) / 2, top.Height - 26, w, 21);
        top.FillRoundRect(r, 8, RGBA(0, 0, 50, 200));
        top.StrokeRoundRect(r.X, r.Y, r.W, r.H, 8, RGBA(120, 150, 230, 200));
        TextCentered(top, TS1Size::S12, r, Fit(TS1Size::S12, _Tip, r.W - 12), RGBA(255, 255, 255), RGBA(0, 0, 0, 200));
    }
    if (Picker.Open && !_FE->IsDesktop()) Picker.DrawTop(top);
}

bool NeighbourhoodScreen::DrawBottom(Canvas& bottom) {
    auto& M = _FE->Model();
    if (Picker.Open) {
        Picker.DrawBottom(bottom);
        return Dialog.Draw(bottom);
    }
    bottom.Blit(_MapBottom.Px.data(), _MapBottom.Width, _MapBottom.Height, 0, 0, false);
    if (!_AnimBottom.empty()) {
        int f = (int)(_Time / _AnimMs) % (int)_AnimBottom.size();
        _AnimBottom[(size_t)f].Draw(bottom, 0, 0);
        // keep the lots in front: re-blit their scaled pictures where they overlap water spans
        for (auto& l : M.Lots) {
            if (l.Thumb.Width == 0) continue;
            int x0 = (int)(l.X * BS), y0 = (int)(l.Y * BS), x1 = (int)((l.X + l.Thumb.Width) * BS), y1 = (int)((l.Y + l.Thumb.Height) * BS);
            for (int y = std::max(0, y0); y < std::min(bottom.Height, y1); y++)
                for (int x = std::max(0, x0); x < std::min(bottom.Width, x1); x++) {
                    int px = (int)(x / BS) - l.X, py = (int)(y / BS) - l.Y;
                    if (px < 0 || py < 0 || px >= l.Thumb.Width || py >= l.Thumb.Height) continue;
                    if ((l.Thumb.Px[(size_t)py * l.Thumb.Width + px] >> 24) > 200) bottom.Px[(size_t)y * bottom.Width + x] = _MapBottom.Px[(size_t)y * _MapBottom.Width + x];
                }
        }
    }
    // the top screen's view
    int vx = (int)(_CamX * BS), vy = (int)(_CamY * BS), vw = (int)(400 * BS), vh = (int)(240 * BS);
    bottom.StrokeRect(vx, vy, vw, vh, RGBA(255, 255, 255, 110));
    // selection marker
    if (_Sel >= 0 && _Sel < (int)M.Lots.size()) {
        auto& l = M.Lots[(size_t)_Sel];
        float pulse = 0.5f + 0.5f * std::sin((float)_Time / 200.0f);
        int x = (int)(l.X * BS), y = (int)(l.Y * BS), w = std::max(8, (int)(l.Thumb.Width * BS)), h = std::max(6, (int)(l.Thumb.Height * BS));
        uint32_t c = _Mode == Mode::Evict ? RGBA(255, 80, 60) : (_Mode == Mode::MoveIn ? RGBA(120, 255, 120) : RGBA(255, 230, 90));
        int a = 140 + (int)(100 * pulse);
        // a diamond around the lot (lots are isometric)
        int mx = x + w / 2, my = y + h / 2;
        for (int t = 0; t < 2; t++) {
            int hw = w / 2 + 2 + t, hh = h / 2 + 2 + t;
            uint32_t cc = (c & 0x00FFFFFFu) | ((uint32_t)a << 24);
            bottom.Line(mx - hw, my, mx, my - hh, cc); bottom.Line(mx, my - hh, mx + hw, my, cc);
            bottom.Line(mx + hw, my, mx, my + hh, cc); bottom.Line(mx, my + hh, mx - hw, my, cc);
        }
    }
    // banner + buttons
    if (_Banner.Width > 0) bottom.Blit(_Banner.Px.data(), _Banner.Width, _Banner.Height, 0, 0, true);
    else bottom.FillRect(0, 0, bottom.Width, BAR_H, RGBA(40, 40, 120, 220));
    for (auto& b : _Buttons) b.Draw(bottom);
    if (_Area == Area::Neighbourhood) {
        // the neighbourhood number between Previous and Next (NghUI/Current.bmp)
        int x = 248, y = 3;
        if (_CurrentBtn.Width > 0) { bottom.Blit(_CurrentBtn.Px.data(), _CurrentBtn.Width, _CurrentBtn.Height, x, y, true); }
        else bottom.FillRect(x, y, 14, 19, RGBA(10, 20, 80));
        TextCentered(bottom, TS1Size::S11, Rect(x, y, _CurrentBtn.Width > 0 ? _CurrentBtn.Width : 14, _CurrentBtn.Height > 0 ? _CurrentBtn.Height : 19),
                     std::to_string(M.Current + 1), RGBA(255, 255, 255), RGBA(0, 0, 0, 200));
    }
    return Dialog.Draw(bottom);
}

// ======================================================================= scripting

bool NeighbourhoodScreen::LotPos(int house, int& x, int& y) const {
    auto& M = _FE->Model();
#ifndef __3DS__
    if(_FE->IsDesktop()&&_Original){for(const auto& art:_Original->Map.houses)if(art.house==house){x=art.hit.x+art.hit.width/2;y=art.hit.y+art.hit.height/2;return true;}}
#endif
    for (auto& l : M.Lots) {
        if (l.House != house) continue;
        // a covered pixel near the picture centre
        int cx = l.Thumb.Width / 2, cy = l.Thumb.Height / 2;
        for (int r = 0; r < 40; r += 2) for (int dy = -r; dy <= r; dy += 2) for (int dx = -r; dx <= r; dx += 2) {
            int px = cx + dx, py = cy + dy;
            if (px < 0 || py < 0 || px >= l.Thumb.Width || py >= l.Thumb.Height) continue;
            if ((l.Thumb.Px[(size_t)py * l.Thumb.Width + px] >> 24) == 0) continue;
            x = (int)((l.X + px) * BS); y = (int)((l.Y + py) * BS);
            if (y < BAR_H + 2) continue;
            if (LotAtBottom(x, y) >= 0 && M.Lots[(size_t)LotAtBottom(x, y)].House == house) {
                if (_FE->IsDesktop()) { x = l.X+px; y = l.Y+py; }
                return true;
            }
        }
        x = (int)((l.X + cx) * BS); y = (int)((l.Y + cy) * BS);
        if (_FE->IsDesktop()) { x = l.X+cx; y = l.Y+cy; }
        return true;
    }
    return false;
}

bool NeighbourhoodScreen::ButtonPos(const std::string& id, int& x, int& y) {
#ifndef __3DS__
    if(_FE->IsDesktop()&&!Dialog.Open&&!Picker.Open){
        using namespace odh_host;odh::UiBitmapRect box{};
        if(id=="credits")box=logo_button;else if(id=="movein")box=family_button;else if(id=="bulldoze")box=manage_button;else if(id=="rezone")box=rezone_button;else if(id=="import")box=import_button;
        else if(id=="prev")box=previous_neighborhood;else if(id=="next")box=next_neighborhood;else if(id=="exit"||id=="return")box=exit_button;
        else if(id=="downtown")box=region_button(0);else if(id=="vacation")box=region_button(1);else if(id=="studiotown")box=region_button(2);else if(id=="magictown")box=region_button(3);
        if(box.width){x=box.x+box.width/2;y=box.y+box.height/2;return true;}
    }
#endif
    if (Dialog.Open && id.rfind("dlg", 0) == 0) return Dialog.ButtonPos(atoi(id.c_str() + 3), x, y);
    if (Picker.Open) {
        if (id.rfind("pick:", 0) == 0) return Picker.RowPos(id.substr(5), x, y);
        return Picker.ButtonPos(id, x, y);
    }
    for (auto& b : _Buttons) if (b.Id == id) { x = b.X + b.Width() / 2; y = b.Y + b.Height() / 2; return true; }
    return false;
}

std::string NeighbourhoodScreen::Describe() const {
    auto& M = _FE->Model();
    char b[160];
    snprintf(b, sizeof b, "nbhd %d area %d, %zu lots, selected %d, mode %d, picker %d, dialog '%s'", M.Current + 1, (int)_Area, M.Lots.size(), SelectedHouse(), (int)_Mode, Picker.Open ? 1 : 0, Dialog.Open ? Dialog.Title.c_str() : "");
    return b;
}

}}
