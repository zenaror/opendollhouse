/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims front end — see FrontEnd.h
#include "files/SafeFile.h" // FIX(sims) SAVE
#include "app/front/FrontEnd.h"
#include "app/LotSession.h"
#include "app/front/FrontStrings.h"
#include "app/front/FrontUI.h"
#include "app/front/NeighbourhoodScreen.h"
#include "app/front/TS1Font.h"
#include "app/front/ResGraphics.h"
#include <algorithm>
#include "files/LoadProgress.h"
#include <cstdio>
#include <cstdlib>

namespace simapp { namespace front {

// ======================================================================= settings

void FrontSettings::Load(const std::string& path, bool handheldGraphics) {
    HandheldGraphics = handheldGraphics;
    if (HandheldGraphics) {
        EdgeScrolling = false;
        Shadows = Lighting = InterfaceEffects = false;
        TerrainDetail = CharacterDetail = 0;
    }

    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    bool versionedHandheldGraphics = false;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[64]; int v;
        if (sscanf(line, "%63[^=]=%d", key, &v) != 2) continue;
        std::string k = key;
        if (k == "HandheldGraphicsVersion") versionedHandheldGraphics = v >= 1;
        else if (k == "Shadows") Shadows = v != 0;
        else if (k == "Lighting") Lighting = v != 0;
        else if (k == "InterfaceEffects") InterfaceEffects = v != 0;
        else if (k == "TerrainDetail") TerrainDetail = v;
        else if (k == "CharacterDetail") CharacterDetail = v;
        else if (k == "SoundVolume") SoundVolume = v;
        else if (k == "MusicVolume") MusicVolume = v;
        else if (k == "VoxVolume") VoxVolume = v;
        else if (k == "AutoCentering") AutoCentering = v != 0;
        else if (k == "FreeWill") FreeWill = v != 0;
        else if (k == "EdgeScrolling") EdgeScrolling = v != 0;
        else if (k == "BackgroundSimulation") BackgroundSimulation = v != 0;
        else if (k == "QuickTips") QuickTips = v != 0;
        else if (k == "AutoSnapshot") AutoSnapshot = v != 0;
        else if (k == "LivePIP") LivePIP = v != 0;
        else if (k == "AutonomyLevel") AutonomyLevel = v;
        else if (k == "PlayIntro") PlayIntro = v != 0;
        else if (k == "LastNeighbourhood") LastNeighbourhood = v;
    }
    fclose(f);
    SoundVolume=std::clamp(SoundVolume,0,100);
    MusicVolume=std::clamp(MusicVolume,0,100);
    VoxVolume=std::clamp(VoxVolume,0,100);
    TerrainDetail=std::clamp(TerrainDetail,0,2);
    CharacterDetail=std::clamp(CharacterDetail,0,2);
    AutonomyLevel=std::clamp(AutonomyLevel,0,3);
    if (HandheldGraphics) {
        // An old options file serialized PC quality values even for controls
        // this host never exposed. Start those profiles with the low preset.
        if (!versionedHandheldGraphics) Shadows = false;
        Lighting = InterfaceEffects = false;
        TerrainDetail = CharacterDetail = 0;
    }
}

void FrontSettings::Save(const std::string& path) const {
    FILE* f = FSO::Files::SafeFile::BeginWrite(path); // FIX(sims) SAVE: the crash-safe writer
    if (!f) return;
    fprintf(f, "# sims options (TS1 Options panel)\n");
    if (HandheldGraphics) fprintf(f, "HandheldGraphicsVersion=1\n");
    fprintf(f, "Shadows=%d\nLighting=%d\nInterfaceEffects=%d\nTerrainDetail=%d\nCharacterDetail=%d\n", Shadows, Lighting, InterfaceEffects, TerrainDetail, CharacterDetail);
    fprintf(f, "SoundVolume=%d\nMusicVolume=%d\nVoxVolume=%d\n", SoundVolume, MusicVolume, VoxVolume);
    fprintf(f, "AutoCentering=%d\nFreeWill=%d\nEdgeScrolling=%d\nQuickTips=%d\nAutoSnapshot=%d\nLivePIP=%d\nAutonomyLevel=%d\n", AutoCentering, FreeWill, EdgeScrolling, QuickTips, AutoSnapshot, LivePIP, AutonomyLevel);
    fprintf(f, "BackgroundSimulation=%d\n", BackgroundSimulation);
    fprintf(f, "PlayIntro=%d\nLastNeighbourhood=%d\n", PlayIntro, LastNeighbourhood);
    FSO::Files::SafeFile::EndWrite(path, f);
}

// ======================================================================= front end

std::string FrontEnd::DesktopCursorPath() const {
    return IsDesktop() && _State==State::Neighbourhood && _Nhood?
        _Nhood->DesktopCursorPath():"Shared/cursors/Arrow.cur";
}

FrontEnd::FrontEnd() {}
FrontEnd::~FrontEnd() { EndIntro(); } // ADDED(sims): the intro's sound stops with it

bool FrontEnd::Init(const Config& cfg) {
    _Cfg = cfg;
    if (_Cfg.SaveRoot.empty()) _Cfg.SaveRoot = _Cfg.DataRoot + "/Saves";
    UserDataSetup::MakeDirs(_Cfg.SaveRoot);
    auto& ui = ResGraphics::Get();
    if (!ui.IsOpen()) ui.Open(_Cfg.DataRoot + "/UIGraphics");
    if (!ui.IsOpen()) printf("[front] UIGraphics/UIGraphics.far not found: plain look (copy UIGraphics/ next to GameData/)\n");
#if defined(__ANDROID__) || defined(__3DS__)
    _Settings.EdgeScrolling = false; // Keep the portable input preset.
#endif
#if defined(__vita__) || defined(__SWITCH__)
    _Settings.Load(_Cfg.SaveRoot + "/sims_options.txt", true);
#else
    _Settings.Load(_Cfg.SaveRoot + "/sims_options.txt");
#endif
    _Setup.Configure(_Cfg.DataRoot, _Cfg.SaveRoot);
    _Loading.Init(_Cfg.Desktop);
    _BootStep = 0;
    _BootDone = false;
    if (!_Cfg.SkipIntro && _Settings.PlayIntro && _Video.Open(_Cfg.CacheDir + "/intro.stv")) _State = State::Intro;
    else _State = State::Loading;
    return true;
}

void FrontEnd::DrawInitialFrame(Canvas& top, Canvas& bottom) {
    if (_State == State::Intro) {
        _Video.Update(0);
        _Video.Draw(top);
        if (&top != &bottom) bottom.Clear(RGBA(0, 0, 0));
    } else {
        _Loading.Draw(top, bottom);
        _DrawnFrame.scene = FramePresentation::Scene::Loading;
    }
}

void FrontEnd::LoadBootLabels() {
    FrontStrings::Get().Load(_Cfg.DataRoot + "/GameData");
    LoadTS1Fonts();
    _Loading.LoadBootLabels();
    _LastBootFrameMs = BootClock();
}

void FrontEnd::AdvanceBootCaption() {
    const double now = BootClock();
    _Loading.AdvanceBootCaption(now - _LastBootFrameMs);
    _LastBootFrameMs = now;
}

void FrontEnd::SaveSettings() {
    _Settings.Save(_Cfg.SaveRoot + "/sims_options.txt");
    if (SettingsChanged) SettingsChanged(_Settings);
}

/// One boot step per call (the loading screen animates between them). Steps: plan the save area, copy it,
/// boot the content (Objects.far index, globals, catalogs: blocking), open the neighbourhood, load its screen.
bool FrontEnd::BootStep(std::string& label) {
    using FSO::Files::LoadProgress; // TS1(sims): step log
    switch (_BootStep) {
        case 0:
            FrontArt::Get().Load(ResGraphics::Get());
            _Nhood.reset(new NeighbourhoodScreen(this));
            LoadProgress::Step("front: save area");
            _Setup.PlanAll();
            label = _Setup.Busy() ? "Setting up the neighborhoods (first run)" : "";
            _BootStep++;
            return true;
        case 1:
            if (_Setup.Busy()) {
                std::string f;
                _Setup.Step(1536 * 1024, &f);
                size_t sl = f.find_last_of('/');
                label = "Copying " + (sl == std::string::npos ? f : f.substr(sl + 1));
                return true;
            }
            _BootStep++;
            label = FrontStrings::Get().Get(155, 4, "Reticulating Splines");
            return true;
        case 2: {
            LoadProgress::Step("front: game data");
            std::string err;
            if (BootContent && !BootContent(err)) { Error = err; _State = State::Error; return false; }
            _ContentReady = true;
            _BootStep++;
            label = FrontStrings::Get().Get(155, 15, "Loading Characters");
            return true;
        }
        case 3: {
            // FIX(sims) SAVE: a neighbourhood whose Neighborhood.iff is missing / damaged with no backup (Available()
            // and Switch() restore interrupted saves first) no longer stops the boot: the next one is loaded and the map
            // offers to restore the broken one; with none left the error screen offers it.
            _RestoreOffer = -1;
            auto avail = _Setup.Available();
            int n = _Settings.LastNeighbourhood;
            if (std::find(avail.begin(), avail.end(), n) == avail.end()) {
                auto broken = _Setup.Broken();
                if (std::find(broken.begin(), broken.end(), n) != broken.end()) _PendingBroken = n;
                if (!avail.empty()) n = avail[0];
            }
            std::string err;
            bool ok = false;
            while (!avail.empty()) {
                LoadProgress::Step("front: neighbourhood " + std::to_string(n));
                if (_Model.Switch(n, _Setup, err)) { ok = true; break; }
                LoadProgress::Step("ERROR: " + err);
                if (_Model.BrokenIndex == n && _PendingBroken < 0) _PendingBroken = n;
                avail.erase(std::remove(avail.begin(), avail.end(), n), avail.end());
                if (!avail.empty()) n = avail[0];
            }
            if (!ok) {
                auto broken = _Setup.Broken();
                if (!broken.empty() && _Setup.CanRestore(_PendingBroken >= 0 ? _PendingBroken : broken[0])) {
                    _RestoreOffer = _PendingBroken >= 0 ? _PendingBroken : broken[0];
                    _PendingBroken = -1;
                    Error = "Neighborhood " + std::to_string(_RestoreOffer + 1) + " could not be loaded: its save file (" +
                            UserDataSetup::FolderName(_RestoreOffer) + "/Neighborhood.iff) is missing or damaged, and no backup of it was found.";
                } else if (err.empty()) Error = "No neighborhood found. Copy UserData/ (Neighborhood.iff, Houses/, Characters/) to " + _Cfg.DataRoot;
                else Error = err;
                LoadProgress::Step("FATAL: " + Error);
                _State = State::Error;
                return false;
            }
            if (_PendingBroken == n) _PendingBroken = -1;
            _BootStep++;
            label = FrontStrings::Get().Get(155, 6, "Fabricating Imaginary Infrastructure");
            return true;
        }
        case 4:
            LoadProgress::Step("front: neighbourhood screen");
            _Nhood->Enter(Area::Neighbourhood);
            if (_PendingBroken >= 0) { _Nhood->OfferRestore(_PendingBroken); _PendingBroken = -1; } // FIX(sims) SAVE
            LoadProgress::Step("front: neighbourhood ready"); // TS1(sims) hardware pass: idle from here
            _BootStep++;
            label = "";
            return false;
        default:
            return false;
    }
}

void FrontEnd::DrawBootProgress(const std::string& text, float fraction, Canvas& top, Canvas& bottom) {
    _Loading.CurrentStep = text;
    // the game data boot is the 0.3 .. 0.6 part of the bar (FrontEnd::Frame)
    if (_BootStep == 2 && fraction >= 0) _Loading.SetProgress(0.3f + 0.3f * (fraction > 1 ? 1.0f : fraction));
    AdvanceBootCaption();
    _Loading.Draw(top, bottom);
}

// ADDED(sims): the intro is over (ended / skipped / boot forced): its sound stops with it.
void FrontEnd::EndIntro() {
    if (_IntroStarted && IntroSound) IntroSound("");
    _IntroStarted = false;
}

void FrontEnd::FinishBootNow() {
    if (_State != State::Intro && _State != State::Loading) return;
    if (_State == State::Intro) EndIntro(); // ADDED(sims)
    std::string label;
    if (!_BootDone) while (_State != State::Error && BootStep(label)) {}
    _BootDone = true;
    if (_State == State::Error) return;
    _State = State::Neighbourhood;
    if (Music) Music(GetAreaDef(_Nhood->CurrentArea(), _Model.Unleashed()).Music);
}

void FrontEnd::ShowSubScreen(std::unique_ptr<SubScreen> s, std::function<void()> onDone) {
    _Sub = std::move(s);
    _SubDone = onDone;
    _State = State::SubScreen;
}

void FrontEnd::Frame(const AppInput& in, double dtMs, Canvas& top, Canvas& bottom) {
    _DrawnFrame = {};
    _Time += dtMs;
    switch (_State) {
        case State::Intro: {
            // ADDED(sims): the intro's sound (<cache>/intro.wav, tools/convert_intro.py) starts with the first frame shown,
            // and so does the video clock (the time since Init is dropped); the video holds its first (black) frame for the
            // audio output's latency so the picture and the sound line up.
            double vdt = dtMs;
            if (!_IntroStarted) {
                _IntroStarted = true;
                double lat = IntroSound ? IntroSound(_Cfg.CacheDir + "/intro.wav") : -1;
                vdt = lat > 0 ? -lat : 0;
            }
            bool skip = in.TouchDown || (in.Down & (BTN_A | BTN_B | BTN_START));
            if (!_Video.Update(vdt) || skip) {
                EndIntro();
                _State = State::Loading;
                _LastBootFrameMs = BootClock();
                if (Music) Music("Load");
            }
            _Video.Draw(top);
            if (&top == &bottom) break;
            bottom.Clear(RGBA(0, 0, 0));
            TextCentered(bottom, TS1Size::S9, Rect(0, 222, bottom.Width, 14), "Tap to skip", RGBA(70, 70, 70));
            break;
        }
        case State::Loading: {
            if (!_BootDone) {
                std::string label;
                bool more = BootStep(label);
                if (!label.empty()) _Loading.CurrentStep = label;
                if (_State == State::Error) break;
                float p = _BootStep <= 1 ? 0.25f * _Setup.Progress() : (_BootStep == 2 ? 0.3f : (_BootStep == 3 ? 0.6f : (_BootStep == 4 ? 0.85f : 1.0f)));
                if (!more) { _BootDone = true; p = 1.0f; _Loading.CurrentStep = ""; }
                _Loading.SetProgress(p);
            }
            AdvanceBootCaption();
            _Loading.Draw(top, bottom);
            _DrawnFrame.scene = FramePresentation::Scene::Loading;
            if (_BootDone) {
                _State = State::Neighbourhood;
                if (Music) Music(GetAreaDef(_Nhood->CurrentArea(), _Model.Unleashed()).Music);
            }
            break;
        }
        case State::Neighbourhood:
            _Nhood->Frame(in, dtMs, top, bottom);
            _DrawnFrame = _Nhood->DrawnFrame();
            break;
        case State::SubScreen:
            if (!_Sub || !_Sub->Frame(in, dtMs, top, bottom)) {
                _Sub.reset();
                _State = State::Neighbourhood;
                auto cb = _SubDone;
                _SubDone = nullptr;
                if (cb) cb();
            }
            break;
        case State::InLot:
            break;
        case State::Error: {
            top.GradientV(0, 0, top.Width, top.Height, RGBA(30, 20, 60), RGBA(10, 10, 30));
            TextCentered(top, TS1Size::S16, Rect(0, 60, top.Width, 22), _RestoreOffer >= 0 ? std::string("Neighborhood not loaded") : FrontStrings::Get().Get(153, 21, "Fatal Error"), RGBA(255, 216, 96)); // FIX(sims) SAVE
            int y = 100;
            for (auto& l : Wrap(TS1Size::S11, Error, top.Width - 40)) { TextCentered(top, TS1Size::S11, Rect(0, y, top.Width, 14), l, RGBA(255, 255, 255)); y += 15; }
            if (&top != &bottom) bottom.GradientV(0, 0, bottom.Width, bottom.Height, RGBA(20, 30, 80), RGBA(5, 10, 30));
            if (_RestoreOffer >= 0) { // FIX(sims) SAVE: the only way on when no neighbourhood can be loaded
                std::string msg = "Press A to restore Neighborhood " + std::to_string(_RestoreOffer + 1) + " from the original game. "
                    "Only this neighborhood: its families and houses go back to how they were when installed (the damaged "
                    "files are kept in " + UserDataSetup::FolderName(_RestoreOffer) + ".damaged).";
                int ly = &top == &bottom ? y+24 : 60;
                for (auto& l : Wrap(TS1Size::S11, msg, bottom.Width - 24)) { TextCentered(bottom, TS1Size::S11, Rect(0, ly, bottom.Width, 16), l, RGBA(200, 210, 240)); ly += 16; }
                TextCentered(bottom, TS1Size::S11, Rect(0, ly + 16, bottom.Width, 16), "START quits", RGBA(200, 210, 240));
                if (in.Down & BTN_A) { // (not a touch: restoring moves the damaged folder aside)
                    std::string err;
                    int idx = _RestoreOffer;
                    if (_Setup.RestoreFromInstall(idx, &err)) {
                        _Settings.LastNeighbourhood = idx;
                        _RestoreOffer = -1; Error.clear();
                        _State = State::Loading; _BootStep = 3; _BootDone = false; // load it like at boot
                    } else { Error = "The neighborhood could not be restored: " + err; _RestoreOffer = -1; }
                }
            } else TextCentered(bottom, TS1Size::S11, Rect(0, &top == &bottom ? bottom.Height-32 : 110, bottom.Width, 16), "START quits", RGBA(200, 210, 240));
            if (in.Down & BTN_START) QuitRequested = true;
            break;
        }
    }
}

void FrontEnd::EnteredLot() {
    RequestedLot = -1;
    _LotLoadKey.clear(); // FIX(sims) FB1: the next load starts its loading screen afresh
    _State = State::InLot;
    // App::AudioFrame selects the entered lot mode. Keep any editor track
    // already chosen during loading, instead of fading it a second time.
    _Nhood->ReleaseArt(); // TS1(sims): memory; ReturnFromLot / BeginLotPick rebuild it (NeighbourhoodScreen::Enter)
}

void FrontEnd::ReturnFromLot(int16_t family, int budget, bool save, bool neighbourhoodAlreadySaved) {
    _State = State::Neighbourhood;
    RequestedLot = -1;
    if (save) {
        if (family >= 0 && budget >= 0) _Model.SetBudget(family, budget);
        if (!neighbourhoodAlreadySaved) SaveNeighbourhood();
    } else {
        // discard what the session changed in memory (the VM writes neighbours/families directly)
        std::string err;
        _Model.Switch(_Model.Current, _Setup, err);
    }
    Area a = _Nhood->CurrentArea();
    int house = _Nhood->SelectedHouse();
    FSO::Files::LoadProgress::Step("front: neighbourhood map"); // ADDED(sims): the map's art and lots (~3.5 s on the 3DS)
    _Nhood->Enter(a);
    _Nhood->SelectHouse(house);
}

Area FrontEnd::AreaOfHouse(int h) {
    if (h >= 21 && h <= 30) return Area::Downtown;
    if (h >= 40 && h <= 49) return Area::Vacation;
    if (h >= 81 && h <= 89) return Area::StudioTown;
    if (h >= 90 && h <= 99) return Area::MagicTown;
    return Area::Neighbourhood;
}

void FrontEnd::BeginLotPick(int mapId, const std::string& title, std::function<void(int)> done) {
    Area a = Area::Neighbourhood;
    if (mapId == 2) a = Area::Downtown;
    else if (mapId == 3) a = Area::Vacation;
    else if (mapId == 5) a = Area::StudioTown;
    else if (mapId == 7) a = Area::MagicTown;
    if (!_Model.AreaAvailable(a)) { done(-1); return; }
    Log += "lot selection: map " + std::to_string(mapId) + "\n";
    _Nhood->BeginPick(a, title, [this, done](int house) {
        Log += "picked lot " + std::to_string(house) + "\n";
        done(house);
    });
}

bool FrontEnd::Picking() const { return _Nhood && _Nhood->Picking(); }

void FrontEnd::PickFrame(const AppInput& in, double dtMs, Canvas& top, Canvas& bottom) {
    _DrawnFrame = {};
    if (_Nhood) {
        _Nhood->Frame(in, dtMs, top, bottom);
        _DrawnFrame = _Nhood->DrawnFrame();
        // A selection may complete during this draw; it is still the destination
        // map frame, never the ordinary neighbourhood input layer.
        if (_DrawnFrame.scene == FramePresentation::Scene::Map)
            _DrawnFrame.destination_picker = true;
    }
}

bool FrontEnd::ShowLotLoadingIndicator() const {
#if defined(__vita__) || defined(__SWITCH__) || defined(__ANDROID__)
    return true;
#else
    return !IsDesktop();
#endif
}

void FrontEnd::PaintLotLoad(const std::string& key, const char* art, Canvas& top, Canvas& bottom) {
    if (_LotLoadKey != key) {
        _LotLoadKey = key;
        DrawLotLoadStart(top, bottom, art, ShowLotLoadingIndicator());
    }
    if (&top != &bottom) DrawLotLoadBar(bottom, _LotLoadProgress);
}

void FrontEnd::DrawTravelLoading(int dest, int from, Canvas& top, Canvas& bottom) {
    Area area = AreaOfHouse(dest);
    const bool returning = area == Area::Neighbourhood && AreaOfHouse(from) != Area::Neighbourhood;
    if (returning) area = AreaOfHouse(from);
    const char* art = GetAreaDef(area, _Model.Unleashed()).LoadScreen;
    if (returning && area == Area::Vacation) art = "kVacationGo2";
    PaintLotLoad("t" + std::to_string(dest) + "/" + std::to_string(from), art, top, bottom);
}

void FrontEnd::LotLoadStep(const std::string& step) {
    if (step.find("first frame") != std::string::npos || step.find(": view") != std::string::npos)
        _LotLoadProgress = std::max(_LotLoadProgress, .75f);
    else if (step.find("family") != std::string::npos || step.find("travellers") != std::string::npos)
        _LotLoadProgress = std::max(_LotLoadProgress, .5f);
    else if (step.find("objects") != std::string::npos)
        _LotLoadProgress = std::max(_LotLoadProgress, .25f);
}

bool FrontEnd::SaveNeighbourhood() {
    FSO::Files::LoadProgress::Step("save: neighbourhood"); // TS1(sims) hardware pass
    std::string err;
    if (!_Model.Save(&err)) { Error = err; Log += "save failed: " + err + "\n"; return false; }
    Log += "neighbourhood saved\n";
    return true;
}

bool FrontEnd::CommunityLotLoading(int house) const {
    const auto found = std::find_if(_Model.Lots.begin(), _Model.Lots.end(),
        [house](const LotInfo& candidate) { return candidate.House == house; });
    const auto* lot = found == _Model.Lots.end() ? nullptr : &*found;
    return lot && lot->Community;
}

bool FrontEnd::ShouldRetainLotLoadingFrame(int house, bool travel) const {
    return !ShowLotLoadingIndicator() && !travel && _Nhood && !CommunityLotLoading(house);
}

void FrontEnd::DrawLotLoading(int house, Canvas& top, Canvas& bottom) {
    const char* art = CommunityLotLoading(house) ? GetAreaDef(_Model.CurrentArea, _Model.Unleashed()).LoadScreen : nullptr;
    PaintLotLoad("l" + std::to_string(house), art, top, bottom);
}

// ======================================================================= scripting

bool FrontEnd::ScriptLotPos(int house, int& x, int& y) {
    FinishBootNow();
    if (_State != State::Neighbourhood) return false;
    if (!_Model.Lot(house)) {
        // the house is in another area: go there (as a player would, with the travel button)
        for (int a = 0; a < (int)Area::Count; a++) {
            const AreaDef& d = GetAreaDef((Area)a, _Model.Unleashed());
            if (house >= d.FirstHouse && house <= d.LastHouse && (Area)a != _Nhood->CurrentArea() && _Model.AreaAvailable((Area)a)) {
                _Nhood->Enter((Area)a);
                if (_Model.Lot(house)) break;
            }
        }
    }
    return _Nhood->LotPos(house, x, y);
}

bool FrontEnd::ScriptButtonPos(const std::string& id, int& x, int& y) {
    FinishBootNow();
    if (_State == State::SubScreen && _Sub) return _Sub->ScriptButtonPos(id, x, y); // TS1(sims): CAS scripting
    if (_State != State::Neighbourhood) return false;
    return _Nhood->ButtonPos(id, x, y);
}

bool FrontEnd::ScriptPickLotPos(int house, int& x, int& y) { return _Nhood && _Nhood->LotPos(house, x, y); }
bool FrontEnd::ScriptPickButtonPos(const std::string& id, int& x, int& y) { return _Nhood && _Nhood->ButtonPos(id, x, y); }

std::string FrontEnd::Describe() const {
    static const char* names[] = { "intro", "loading", "neighbourhood", "subscreen", "inlot", "error" };
    std::string s = std::string("front: ") + names[(int)_State];
    if (_Nhood && (_State == State::Neighbourhood || _State == State::SubScreen)) {
        if (_State == State::SubScreen && _Sub) s += "; " + _Sub->Describe(); // TS1(sims)
        s += "; " + _Nhood->Describe() + "\n  lots:";
        for (auto& l : _Model.Lots) {
            char b[96];
            snprintf(b, sizeof b, " %d%s%s$%d", l.House, l.Community ? "c" : "", l.Family >= 0 ? "F" : (l.EmptyLot ? "e" : ""), l.Value);
            s += b;
        }
    }
    return s;
}

}}
