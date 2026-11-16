#include "simantics/utils/VMLotDemolitionScope.h"
/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims app — see LotSession.h
#include "files/SafeFile.h" // FIX(sims) SAVE
#include "files/LoadProgress.h"
#include <algorithm>
#include "app/LotSession.h"
#include "app/front/GameOptions.h"
#include "app/front/FrontEnd.h"
#include "app/front/FrontAudio.h"
#include "app/UISounds.h"
#include "core/LotOpenPolicy.h"
#include "files/formats/iff/chunks/FAMI.h"
#ifndef __3DS__
#include "app/ui/OriginalPhonebook.h"
#include "app/ui/OriginalDialogText.h"
#include "phonebook_candidates.hpp"
#endif
#include "files/formats/iff/chunks/SIMI.h"
#include "app/WorldPicking.h"
#include "app/live/UIHeadlineRenderer.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/model/VMRuntimeHeadline.h"
#include "simantics/model/VMDialogInfo.h"
#include "simantics/utils/VMWorldActivator.h"
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include "files/formats/iff/chunks/ODHI.h"
#include "files/formats/iff/chunks/OBJM.h"
#include "simantics/utils/VMArchitectureTools.h"
#include "simantics/utils/VMTS1FamilyLoader.h"
#include "simantics/engine/VMTS1Travel.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMTS1Outfits.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/engine/VMDialogOperand.h"
#include "files/formats/iff/IffFile.h"
#include "world/World.h"
#include "world/model/Blueprint.h"
#include "world/components/AvatarComponent.h"
#include <unordered_set>
#include "simantics/VMClock.h"
#include "app/front/FrontStrings.h"
#include "simantics/model/VMStackObjectVariable.h"
#include "files/formats/iff/chunks/OBJD.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace simapp {

using namespace FSO::SimAntics;
using FSO::LotView::Model::LotTilePos;
using FSO::LotView::Model::Direction;
using FSO::LotView::WorldZoom;
using FSO::LotView::WorldRotation;

static const uint32_t GOTO_GUID = 0x000007C4; // UILotControl.GOTO_GUID

LotSession::LotSession() {}

LotSession::~LotSession() {
    // the VM's entities reference the context, world components and the house's chunks
    _VM.reset();
    _Ctx.reset();
    _Headlines.reset();
    _World.reset();
    _House.reset();
}

bool LotSession::Load(int houseNumber, int viewW, int viewH, int scrollBuffer, std::string& error, bool travel) {
    return OpenHouse(houseNumber, travel, error) && ActivateOpenedHouse(viewW, viewH, scrollBuffer, error, travel);
}

bool LotSession::LoadForDemolition(int houseNumber, std::string& error) {
    _PreparingDemolition = true;
    struct Reset {bool& value; ~Reset(){value=false;}} reset{_PreparingDemolition};
    return Load(houseNumber, 264, 144, 0, error, false);
}

bool LotSession::OpenHouse(int houseNumber, bool travel, std::string& error) {
    auto content = FSO::Content::Content::Get();
    auto hood = content ? content->Neighborhood.get() : nullptr;
    HouseNumber = houseNumber;
    std::string path;
    if (hood != nullptr) path = hood->GetHousePath(houseNumber);
    if (path.empty()) {
        char buf[64]; snprintf(buf, sizeof buf, "/UserData/Houses/House%02d.iff", houseNumber);
        path = content->BasePath + buf;
    }
    const std::string lotTag = "lot " + std::to_string(houseNumber) + ": "; // TS1(sims): step log (files/LoadProgress.h)
    FSO::Files::LoadProgress::Step(lotTag + "house file");
    // FIX(sims) SAVE: an interrupted house save left House<N>.iff missing (the template was loaded instead) or cut:
    // put the newest complete copy back (.new / .tmp / .bak; NhoodModel::Switch does this for the whole folder once).
    if (hood) {
        // Never repair the template returned by GetHousePath.
        char rel[48]; snprintf(rel, sizeof rel, "/Houses/House%02d.iff", houseNumber);
        const auto mine = hood->UserPath + rel;
        if (FSO::Files::SafeFile::HasLeftover(mine) && hood->HasPrivateNeighbourhood() &&
            FSO::Files::SafeFile::Recover(mine, FSO::Files::SafeFile::IffValidator()) == FSO::Files::SafeFile::Result::Restored)
            path = hood->GetHousePath(houseNumber);
    }
    if(hood && travel && hood->GameState.ResumeVisit && hood->GameState.ActiveFamily) {
        auto id=(int16_t)hood->GameState.ActiveFamily->ChunkID;
        auto it=hood->GameState.SavedVisits.find(id);
        if(it==hood->GameState.SavedVisits.end()) {error="Saved visit record is missing";return false;}
        path=it->second.SnapshotPath(hood->UserPath,id);
        if(!FSO::Files::SafeFile::Exists(path)) {error="Saved visit snapshot is missing: "+path;return false;}
    }
    LoadedHousePath=path;
    try { _House.reset(new FSO::Files::Formats::IFF::IffFile(path)); }
    catch (std::exception& e) { error = "Cannot open " + path + ": " + e.what(); return false; }

    return true;
}

bool LotSession::ActivateOpenedHouse(int viewW, int viewH, int scrollBuffer, std::string& error, bool travel) {
    if (!_House) { error = "No house file was opened"; return false; }
    const int houseNumber = HouseNumber;
    const std::string lotTag = "lot " + std::to_string(houseNumber) + ": ";
    auto content = FSO::Content::Content::Get();
    auto hood = content->Neighborhood.get();
    const auto resident = hood ? hood->GetFamilyForHouse(int16_t(houseNumber)) : nullptr;
    _AllowsLive = LotOpeningAllowsLive(travel,hood && hood->GetZoning(int16_t(houseNumber)) != 0,
        resident && !resident->FamilyGUIDs.empty());
    _DisplayWidth = viewW;
    _DisplayHeight = viewH;
    const int scale = _HalfResolutionLot && viewW % 2 == 0 && viewH % 2 == 0 ? 2 : 1;
    viewW /= scale;
    viewH /= scale;
    _World.reset(new FSO::LotView::World(viewW, viewH));
    _World->PresentationScale = scale;
    Blueprint = nullptr; // The session cutaway binding is ready only after LoadFromIff returns.
    Frame.Resize(viewW, viewH);
#if defined(__vita__) || defined(__SWITCH__)
    std::printf("[portable-perf] lot scene %dx%d, display %dx%d, zoom 2\n", viewW, viewH, _DisplayWidth, _DisplayHeight);
#endif
    _World->SetScrollBuffer(scrollBuffer);
    VM::UseWorld(true);
    _Ctx.reset(new VMContext(_World.get()));
    _Headlines.reset(new UIHeadlineRendererProvider()); // FreeSims UIHeadlineRendererProvider (balloons)
    _VM.reset(new FSO::SimAntics::VM(_Ctx.get(), _Headlines.get()));
    FSO::SimAntics::Utils::VMLotDemolitionScope preparation(_PreparingDemolition ? _VM.get() : nullptr);
    _VM->Init();
    _VM->OnDialog = [this](Model::VMDialogInfo& info) {
        if (info.Title == "SimAntics Exception!") { // object resets: logged, not shown (FreeSims shows them in debug)
            static int shown = 0; // TS1(sims) QA: SIM3DS_RESETLOG=N prints the first N reset reports (stack) to stdout
            static const int maxShown = getenv("SIM3DS_RESETLOG") ? atoi(getenv("SIM3DS_RESETLOG")) : 0;
            if (shown < maxShown) { shown++; printf("[reset] %s\n", info.Message.c_str()); }
            return;
        }
        PendingDialog d;
        d.Title = info.Title; d.Message = info.Message;
        d.Yes = info.Yes; d.HasYes = info.HasYes;
        d.No = info.No; d.HasNo = info.HasNo;
        d.Cancel = info.Cancel; d.HasCancel = info.HasCancel;
#ifndef __3DS__
        original::ApplyDialogTextUTF8(info, d);
#endif
        d.Block = info.Block;
        d.CallerID = info.Caller ? info.Caller->ObjectID : 0;
        d.Type = (uint8_t)info.Operand.Type;
        // FIX(sims): the buttons come from the dialog type, a string id of 0 means the default label (FreeSO
        // UILotControl.ShowDialog / DialogDescriptors). Before, a Yes/No dialog without button strings (the Help
        // System's "Would you like the help system activated?") showed only "OK", which always answered Yes.
        {
            auto& T = simapp::front::FrontStrings::Get();
            const auto label = [&](int index, const char* fallback) {
                auto bytes = T.Get(152, index, fallback);
#ifndef __3DS__
                if (d.TextUTF8) return original::LegacyToUTF8(bytes);
#endif
                return bytes;
            };
            auto t = info.Operand.Type;
            if (t == FSO::SimAntics::Primitives::VMDialogType::YesNo || t == FSO::SimAntics::Primitives::VMDialogType::YesNoCancel) {
                if (!d.HasYes || d.Yes.empty()) { d.Yes = label(2, "Yes"); d.HasYes = true; }
                if (!d.HasNo || d.No.empty()) { d.No = label(3, "No"); d.HasNo = true; }
                if (t == FSO::SimAntics::Primitives::VMDialogType::YesNoCancel && (!d.HasCancel || d.Cancel.empty())) { d.Cancel = label(1, "Cancel"); d.HasCancel = true; }
            }
        }
        d.IconID = info.Icon ? info.Icon->ObjectID : 0;
        d.IconMode = (static_cast<int>(info.Operand.Flags) >> 1) & 7;
        d.IndexedIconBMP = info.IndexedIconBMP;
        if (d.Type == 9 && info.Caller && !info.Caller->Dead && info.Caller->Thread) {
            // Keep the initiating thread even if an object raised the offer.
            auto thread = info.Caller->Thread.get();
            d.ChoiceCallerGUID = info.Caller->Object->OBJ->GUID;
            d.ChoiceCallerThread = thread;
            auto pet = thread->Stack.empty() ? nullptr : dynamic_cast<VMAvatar*>(thread->Stack.back()->StackObject);
            if (pet && !pet->Dead && pet->IsPet()) {
                d.AvatarID = pet->ObjectID;
                auto coats = Engine::Utils::VMTS1Outfits::ValidOutfits(pet, 0);
                for (auto& coat : coats) {
                    if (d.Choices.size() == 32768) break; // The returned clothing index is signed 16-bit.
                    PendingDialogChoice choice;
                    choice.Label = std::move(coat.BodyString);
                    choice.Value = int(d.Choices.size());
                    choice.GUID = pet->Object->OBJ->GUID;
                    d.Choices.push_back(std::move(choice));
                }
            }
        }
        if (d.Type == 12 && info.Caller) {
            auto actor=dynamic_cast<VMAvatar*>(info.Caller);
            if(!actor&&info.Caller->Thread)actor=dynamic_cast<VMAvatar*>(info.Caller->Thread->GetEntity());
            if(actor&&!actor->Dead)d.AvatarID=actor->ObjectID;
        }
        if (d.Type == 6 && info.Caller != nullptr) {
            // TS1(sims): the dressers / racks set the outfit type in temp 0 and read the answer (an index into
            // VMTS1Outfits::ValidOutfits) from temp 1 for Generic Sims Call 24/29 (Behavior.iff STR#220)
            d.AvatarID = info.Caller->ObjectID;
            if (info.Caller->Thread != nullptr) d.OutfitType = info.Caller->Thread->TempRegisters[0];
            if (dynamic_cast<VMAvatar*>(info.Caller) == nullptr && info.Caller->Thread && info.Caller->Thread->Stack.size() > 0) {
                auto so = info.Caller->Thread->Stack.back()->StackObject;
                if (so) d.AvatarID = so->ObjectID;
            }
        }
        #ifndef __3DS__
        if(d.Type==10)original::PreparePhonebook(*this,d);
        #endif
        // Fresh native restore already owns its validated wait before emitting
        // the UI. Bind choice identity now: modal UI prevents a first tick.
        if ((d.Type == 9 || d.Type == 10) && d.Block && info.Caller && info.Caller->Thread) {
            auto wait = std::dynamic_pointer_cast<Primitives::VMDialogResult>(info.Caller->Thread->BlockingState);
            if (wait && !wait->Responded && uint8_t(wait->Type) == d.Type)
                d.ResolveChoiceSourceAfterUpdate(wait);
        }
#ifdef __3DS__
        if (Dialogs.size() < 8) Dialogs.push_back(d);
#else
        const auto pendingHelp = [](const auto& pending) { return pending.Type == 4 && !pending.Block; };
        if (Dialogs.size() >= 8 && !pendingHelp(d)) {
            // Continue help has no VMDialogResult. Evict the newest notification
            // rather than lose a new ordinary/critical message or its wait.
            auto help = Dialogs.end();
            for (auto it = Dialogs.begin(); it != Dialogs.end(); ++it) if (pendingHelp(*it)) help = it;
            if (help != Dialogs.end()) Dialogs.erase(help);
        }
        // Blocking producers will install a response wait. Retain it even
        // if all eight existing entries are non-removable waits/notices.
        if (d.Block || Dialogs.size() < 8) {
            if (pendingHelp(d)) Dialogs.push_back(d);
            else Dialogs.insert(std::find_if(Dialogs.begin(), Dialogs.end(), pendingHelp), d);
        }
#endif
    };
    _VM->OnSimanticsException = [this](VMEntity*, VMEntity*, const std::string&) { Resets++; };

    // LoadFromIff executes object init/load trees. Consume their camera requests
    // now, while keeping early selection events ignored as in the existing load.
    const auto focusCamera = [this](int32_t evt, int32_t value) {
        const bool automatic=evt==Engine::Utils::VMTS1Travel::EventAutomaticCameraFocus;
        if (!automatic && evt != Engine::Utils::VMTS1Travel::EventCameraFocus) return false;
        auto frontend=front::GameOptions::Get().FE;
        if(automatic && frontend && !frontend->Settings().AutoCentering){
            front::FrontSound(UISounds::Error);return true;
        }
        auto target = _VM->GetObjectById((int16_t)value);
        const auto before = _World ? _World->State.CenterTile() : st::Vector2();
        const int levelBefore = _World ? _World->State.Level() : -1;
        CenterOn(target);
        if (_World) {
            const auto after = _World->State.CenterTile();
            const auto component = target ? target->WorldUI.get() : nullptr;
            std::printf("[camera-focus] oid=%d room=%d target_level=%d center=(%.3f,%.3f)->(%.3f,%.3f) view_level=%d->%d\n",
                int(value), component ? int(component->Room) : -1, component ? int(component->Level) : -1,
                double(before.X), double(before.Y), double(after.X), double(after.Y), levelBefore, int(_World->State.Level()));
        }
        return true;
    };
    _VM->OnTS1Event = [focusCamera](int32_t evt, int32_t value) { focusCamera(evt, value); };

    FSO::Files::LoadProgress::Step(lotTag + "objects + architecture");
    try {
        VMWorldActivator act(_VM.get(), _World.get());
        Blueprint = act.LoadFromIff(_House.get(), true);
    } catch (std::exception& e) { error = std::string("Lot load failed: ") + e.what(); return false; }
    if (Blueprint == nullptr) { error = "Lot load failed"; return false; }
    if (_PreparingDemolition) {
        // A converter prepares an empty property, never resumes its residents,
        // actions, visitors or delayed NPC/controllers before the final commit.
        auto entities = _VM->Entities;
        for (auto e : entities) if (auto av = dynamic_cast<VMAvatar*>(e)) {
            if (av->MultitileGroup) av->MultitileGroup->Delete(_Ctx.get());
        }
        Family.clear(); _AllowsLive = false;
        _VM->SetGlobalValue(3, 0); _VM->SetGlobalValue(9, 0);
        _VM->SetGlobalValue(10, int16_t(houseNumber));
        _VM->SetGlobalValue(13, 0); _VM->SetGlobalValue(14, 0);
        _World->State.Zoom(SceneScale() == 2 ? WorldZoom::Far : WorldZoom::Medium);
        _World->State.Rotation(WorldRotation::TopLeft);
        _World->State.Level(1);
        return true;
    }
    bool departureCheckpoint = false;
    try {
        departureCheckpoint = Engine::Utils::VMTS1InteractionCheckpoint::IsDeparture(*_House);
        // The source snapshot predates the return trip. Reconnect it before
        // Arrive consumes incoming time, motives, inventory and family state.
        if (departureCheckpoint) Engine::Utils::VMTS1InteractionCheckpoint::Restore(*_VM, *_House,
            travel && hood && hood->GameState.ResumeVisit);
    } catch (const std::exception& e) { error = e.what(); return false; }
    if (travel && hood && hood->GameState.ResumeVisit) {
        // Object init trees run again when loading TS1 data. Their temporary
        // transit gate must not replace the authored state of a saved visit:
        // an already-arrived ped marker will not repeat its arrival transition.
        auto simi = _House->Get<FSO::Files::Formats::IFF::Chunks::SIMI>(1);
        if (!simi || simi->GlobalData.size() <= 34) {
            error = "Saved visit is missing its transit state";
            return false;
        }
        _VM->SetGlobalValue(34, simi->GlobalData[34]);
    }


    // TS1(sims): the travellers arriving on an expansion lot are spawned by its ped markers a few ticks later
    // and selected through VMTS1Travel::SelectSim (EventSelectedSimChanged): add them to the controllable Sims.
    _VM->OnTS1Event = [this, focusCamera](int32_t evt, int32_t value) {
        if (focusCamera(evt, value)) return;
        if (evt != Engine::Utils::VMTS1Travel::EventSelectedSimChanged) return;
        auto av = dynamic_cast<VMAvatar*>(_VM->GetObjectById((int16_t)value));
        if (av == nullptr) return;
        // Whole-family arrival creates its companions before selecting the caller.
        // Expose all arrived members, including pets, without waiting for a reload.
        auto content=FSO::Content::Content::Get();
        auto active=content && content->Neighborhood ? content->Neighborhood->GameState.ActiveFamily : nullptr;
        if(active) for(auto entity:_VM->Entities) {
            auto member=dynamic_cast<VMAvatar*>(entity);
            if(member && !member->Dead && IndexOfFamily(member)<0 &&
               std::find(active->RuntimeSubset.begin(),active->RuntimeSubset.end(),member->Object->OBJ->GUID)!=active->RuntimeSubset.end()) {
                bool first=Family.empty();Family.push_back(member);if(first)CenterOn(member);
            }
        }
        int idx = IndexOfFamily(av);
        // FIX(sims): the camera follows the first traveller who arrives (it stayed on the lot's saved view, often
        // a corner without the Sim, e.g. Downtown house 23)
        if (idx < 0) { bool first = Family.empty(); Family.push_back(av); idx = (int)Family.size() - 1; if (first) CenterOn(av); }
        Selected = idx;
    };
    FSO::Files::LoadProgress::Step(lotTag + (travel ? "travellers" : "family"));
    if (travel) {
        std::vector<std::string> log;
        try { Family = Engine::Utils::VMTS1Travel::Arrive(_VM.get(), (int16_t)houseNumber, &log); }
        catch (std::exception& e) { error=std::string("Travel arrival failed: ")+e.what(); return false; }
        for (auto& l : log) printf("[travel] %s\n", l.c_str());
        auto fam = hood != nullptr ? hood->GameState.ActiveFamily : nullptr;
        if (fam != nullptr && hood != nullptr) {
            const auto personal = hood->GetFamilyText((int16_t)fam->ChunkID);
            FamilyName = personal.Value(0); FamilyNameSourceLanguage = personal.SourceLanguage;
            FamilyNameUTF8 = personal.UTF8; FamilyNameAmbiguous = personal.Ambiguous;
        }
    } else {
        VMTS1FamilyLoader loader(_VM.get());
        try {
            auto savedObjects = _House->Get<FSO::Files::Formats::IFF::Chunks::OBJM>(1);
            Family = loader.LoadFamily((int16_t)houseNumber, nullptr, savedObjects && savedObjects->Decoded);
        }
        catch (std::exception& e) { printf("[app] family load failed: %s\n", e.what()); }
        if (loader.Family != nullptr && hood != nullptr) {
            const auto personal = hood->GetFamilyText((int16_t)loader.Family->ChunkID);
            FamilyName = personal.Value(0); FamilyNameSourceLanguage = personal.SourceLanguage;
            FamilyNameUTF8 = personal.UTF8; FamilyNameAmbiguous = personal.Ambiguous;
        }
        for (auto& l : loader.Log) printf("[family] %s\n", l.c_str());
    }

    const bool hasCheckpoint = _House->HasChunkType<FSO::Files::Formats::IFF::Chunks::ODHI>();
    bool classicRestored = false;
    if (hasCheckpoint && !departureCheckpoint) {
        try { Engine::Utils::VMTS1InteractionCheckpoint::Restore(*_VM, *_House); }
        catch (const std::exception& e) { error = e.what(); return false; }
    }
    if (!hasCheckpoint) {
        auto savedObjects = _House->Get<FSO::Files::Formats::IFF::Chunks::OBJM>(1);
        if (savedObjects && savedObjects->Decoded && !getenv("WA_NO_RESTORE")) {
            try { Engine::Utils::VMTS1InteractionCheckpoint::RestoreClassic(*_VM, *_House); }
            catch (const std::exception& e) { error = e.what(); return false; }
            classicRestored = true;
        }
    }
    // All saved objects and native interaction frames are available now. A
    // legacy foreign neighbour OID can be repaired without restarting actions.
    Engine::Utils::VMTS1Neighbourhood::RepairForeignObjectSocialReferences(_VM.get());
    FSO::Files::LoadProgress::Step(lotTag + "view");
    // FreeSims UILotControl: the goto ghost used for "Go Here" pie menus on empty floor
    auto gotoGroup = _Ctx->CreateObjectInstance(GOTO_GUID, LotTilePos::OUT_OF_WORLD, Direction::NORTH, true, false);
    if (gotoGroup != nullptr && !gotoGroup->Objects.empty()) GotoObject = gotoGroup->Objects[0];

    auto arch = _Ctx->Architecture();
    auto& st = _World->State;
    st.Zoom(SceneScale() == 2 ? WorldZoom::Far : WorldZoom::Medium);
    st.Rotation(WorldRotation::TopLeft);
    st.Level(1);
    // centre on the house (walls of level 1), else the lot
    int minX = 9999, minY = 9999, maxX = -1, maxY = -1;
    auto& wl = arch->Walls[0];
    for (int y = 0; y < arch->Height; y++) for (int x = 0; x < arch->Width; x++)
        if ((int)wl[(size_t)(y * arch->Width + x)].Segments != 0) { minX = std::min(minX, x); minY = std::min(minY, y); maxX = std::max(maxX, x); maxY = std::max(maxY, y); }
    if (maxX >= 0) st.CenterTile(st::Vector2((minX + maxX + 1) / 2.0f, (minY + maxY + 1) / 2.0f));
    else st.CenterTile(st::Vector2(arch->Width / 2.0f, arch->Height / 2.0f));
    UpdateCutaway();

    if (!Family.empty()) Select(0);
    if (hasCheckpoint) {
        try { Engine::Utils::VMTS1InteractionCheckpoint::PresentDialogs(*_VM); }
        catch (const std::exception& e) { error = e.what(); return false; }
    }
    // settle: a few ticks so the Sims have their pose/outfit before the first frame
    // Restored classic frames, like native checkpoints, must reach the caller
    // before any simulation tick. Only newly initialized fallback lots settle.
    if (!hasCheckpoint && !classicRestored) Update(4);
    return true;
}

VMAvatar* LotSession::SelectedSim() {
    if (Selected < 0 || Selected >= (int)Family.size()) return nullptr;
    auto a = Family[(size_t)Selected];
    if (a == nullptr || a->Dead) return nullptr;
    return a;
}

void LotSession::Select(int index) {
    if (index < 0 || index >= (int)Family.size()) return;
    Selected = index;
    // TS1 global 3 "Selected Person ID" (Behavior.iff STR#129), as VMTS1FamilyLoader sets it
    if (_VM && _VM->GlobalState.size() > 3) _VM->GlobalState[3] = Family[(size_t)index]->ObjectID;
}

int LotSession::IndexOfFamily(VMEntity* e) const {
    for (size_t i = 0; i < Family.size(); i++) if (Family[i] == e) return (int)i;
    return -1;
}

void LotSession::Update(int n) {
    if (!CanEnterLive()) return; // Direct community/vacant openings are editing sessions.
    for (int i = 0; i < n; i++) {
        // Keep only the visible roster here. FAMI and the authored travelling
        // subset remain authoritative; a death or birth must not be undone by UI.
        uint32_t priorSelectedGUID = 0;
        const int priorSelectedIndex = Selected;
        if (Selected >= 0 && Selected < int(Family.size())) {
            auto candidate = Family[size_t(Selected)];
            // Removed avatars can remain in the old roster until this update.
            // Establish VM ownership before dereferencing the old pointer.
            if (candidate && std::find(_VM->Entities.begin(), _VM->Entities.end(), candidate) != _VM->Entities.end()
                && !candidate->Dead && candidate->Object && candidate->Object->OBJ)
                priorSelectedGUID = candidate->Object->OBJ->GUID;
        }
        _VM->Update(); Updates++;
        auto content = FSO::Content::Content::Get();
        auto hood = content ? content->Neighborhood.get() : nullptr;
        auto family = Engine::Utils::VMTS1Neighbourhood::CurrentFamily(_VM.get());
        const bool visiting = hood && family && hood->GameState.VisitingHouse == HouseNumber
            && family->HouseNumber != HouseNumber;
        size_t count = 0;
        if (family) for (auto guid : family->FamilyGUIDs) {
            if (visiting && std::find(family->RuntimeSubset.begin(), family->RuntimeSubset.end(), guid) == family->RuntimeSubset.end()) continue;
            VMAvatar* member = nullptr;
            for (auto entity : _VM->Entities) {
                auto avatar = dynamic_cast<VMAvatar*>(entity);
                if (avatar && !avatar->Dead && avatar->Object && avatar->Object->OBJ
                    && avatar->Object->OBJ->GUID == guid) { member = avatar; break; }
            }
            if (!member || std::find(Family.begin(), Family.begin() + count, member) != Family.begin() + count) continue;
            if (count < Family.size()) Family[count] = member;
            else Family.push_back(member);
            ++count;
        }
        Family.resize(count);
        int selected = -1;
        auto authoredSelection = _VM->GetObjectById(_VM->GetGlobalValue(Engine::Utils::VMTS1Neighbourhood::GLOBAL_SELECTED_PERSON));
        for (size_t index = 0; index < Family.size(); ++index) {
            if (Family[index] == authoredSelection) { selected = int(index); break; }
        }
        if (selected < 0 && priorSelectedGUID) for (size_t index = 0; index < Family.size(); ++index) {
            if (Family[index]->Object->OBJ->GUID == priorSelectedGUID) { selected = int(index); break; }
        }
        if (selected < 0 && !Family.empty()) selected = std::clamp(priorSelectedIndex, 0, int(Family.size()) - 1);
        // This reconciliation changes presentation only. The VM owns global 3
        // (including deliberate zero/temporary selections); user input writes
        // it through Select(), and KeepSelectedSim handles removed VM actors.
        Selected = selected;
        for (auto& dialog : Dialogs) {
            if (dialog.Type != 9 || !dialog.Block || dialog.ChoiceSourceResolved) continue;
            auto source = _VM->GetObjectById(dialog.CallerID);
            std::shared_ptr<Model::TSOPlatform::VMAsyncState> wait;
            if (source && !source->Dead && source->Thread && source->Thread.get() == dialog.ChoiceCallerThread &&
                source->Object->OBJ->GUID == dialog.ChoiceCallerGUID) {
                auto state = std::dynamic_pointer_cast<Primitives::VMDialogResult>(source->Thread->BlockingState);
                if (state && uint8_t(state->Type) == 9 && !state->Responded) wait = state;
            }
            // The primitive installs its result after raising the dialog. An
            // absent original wait closes this binding permanently as well.
            dialog.ResolveChoiceSourceAfterUpdate(wait);
        }
#ifndef __3DS__
        // ShowDialog is synchronous inside the primitive, just before it
        // installs VMDialogResult. Capture that exact source after the update,
        // once; a later timeout/reused ObjectID must not rebind an old prompt.
        for(auto& dialog:Dialogs)if(dialog.Type==10&&dialog.Block&&!dialog.ChoiceSourceResolved){
            auto current=dynamic_cast<VMAvatar*>(_VM->GetObjectById(dialog.CallerID));
            auto thread=current&&!current->Dead&&current->Object->OBJ->GUID==dialog.ChoiceCallerGUID&&current->GetPersonData(Model::VMPersonDataVariable::NeighborId)==dialog.ChoiceCallerNID&&current->Thread.get()==dialog.ChoiceCallerThread?current->Thread.get():nullptr;
            std::shared_ptr<Model::TSOPlatform::VMAsyncState> captured;
            if(thread&&thread->BlockingState&&thread->BlockingState->StateType()==Model::TSOPlatform::VMAsyncStateType::DialogResult){
                auto state=std::static_pointer_cast<Primitives::VMDialogResult>(thread->BlockingState);
                if(uint8_t(state->Type)==10)captured=state;
            }
            // Even a reset which removed the original wait finalizes this
            // prompt's source. Never capture a later Type10 on the same Sim.
            dialog.ResolveChoiceSourceAfterUpdate(captured);
        }
#endif
        // TS1(sims): objects lose value once a day (OBJD DailyDepreciation, down to DepreciationLimit), on the
        // object's current value (object data 41 CurrentValue, saved in the house file's OBJM records)
        int day = _Ctx->Clock->DayOfMonth;
        if (_LastDepreciationDay < 0) _LastDepreciationDay = day;
        else if (day != _LastDepreciationDay) { _LastDepreciationDay = day; DepreciateObjects(); }
    }
}

void LotSession::DepreciateObjects() {
    for (auto e : _VM->Entities) {
        auto go = dynamic_cast<VMGameObject*>(e);
        if (go == nullptr || go->MultitileGroup == nullptr || go->MultitileGroup->BaseObject() != go) continue;
        if (go->Position() == FSO::LotView::Model::LotTilePos::OUT_OF_WORLD) continue;
        auto def = go->MasterDefinition ? go->MasterDefinition : (go->Object ? go->Object->OBJ : nullptr);
        if (def == nullptr || def->DailyDepreciation == 0) continue;
        int value = go->GetValue(FSO::SimAntics::Model::VMStackObjectVariable::CurrentValue);
        if (value <= (int)def->DepreciationLimit) continue;
        value = std::max<int>((int)def->DepreciationLimit, value - (int)def->DailyDepreciation);
        go->SetValue(FSO::SimAntics::Model::VMStackObjectVariable::CurrentValue, (int16_t)value);
        if (go->MultitileGroup->Price > 0) go->MultitileGroup->Price = value;
    }
}

int LotSession::Budget() {
    using Engine::Utils::VMTS1Neighbourhood;
    if (!_VM) return 0;
    return _VM->GetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_DIV) * 10000 + _VM->GetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_MOD);
}

void LotSession::SetBudget(int value) {
    if (!HasFamilyBudget()) return; // Actorless lot editing never transacts a household purse.
    using Engine::Utils::VMTS1Neighbourhood;
    if (!_VM) return;
    if (value < 0) value = 0;
    _VM->SetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_DIV, (int16_t)(value / 10000));
    _VM->SetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_MOD, (int16_t)(value % 10000));
}

bool LotSession::RunArchitecture(std::vector<Model::VMArchitectureCommand>& commands, int& cost) {
    cost = 0;
    if (commands.empty()) return false;
    auto arch = _Ctx->Architecture();
    // VMNetArchitectureCmd.Execute sets CallerUID = the actor; RunCommands needs an avatar with that persist id
    VMEntity* actor = SelectedSim();
    if (actor == nullptr) for (auto e : _VM->Entities) if (dynamic_cast<VMAvatar*>(e)) { actor = e; break; }
    const bool actorless = !HasFamilyBudget();
    if (actor == nullptr && !actorless) return false;
    for (auto& c : commands) c.CallerUID = actorless ? 0 : actor->PersistID;
    // the price first (VMTSOGlobalLinkStub: the transaction must succeed before the commands run)
    cost = arch->SimulateCommands(commands, false, actorless);
    if (!CanAfford(cost)) return false;
    arch->RunCommands(commands, false, actorless);
    if (getenv("ST_DEBUG_PLACE")) for (auto& c : commands) {
        auto f = arch->GetFloor((int16_t)c.x, (int16_t)c.y, c.level); auto w = arch->GetWall((int16_t)c.x, (int16_t)c.y, c.level);
        printf("[arch] cmd %d at %d,%d L%d x2 %d y2 %d pat %d style %d -> floor %d wallsegs %d\n", (int)c.Type, c.x, c.y, (int)c.level, c.x2, c.y2, c.pattern, c.style, f.Pattern, (int)w.Segments);
    }
    SetBudget(Budget() - cost);
    ArchitectureSpent += cost; // TS1(sims)
    arch->SignalRedraw();
    return true;
}

static bool PetWaitMatches(const PendingDialog& dialog, VMEntity* source) {
    if (!source || source->Dead || !source->Object || !source->Thread) return false;
    if (!dialog.MatchesChoiceSource(source->Object->OBJ->GUID, 0, source->Thread.get(), source->Thread->BlockingState)) return false;
    if (!dialog.Block) return true;
    auto state = std::dynamic_pointer_cast<Primitives::VMDialogResult>(dialog.ChoiceCallerState.lock());
    return state && uint8_t(state->Type) == 9 && !state->Responded;
}

bool LotSession::PetChoiceCurrent(const PendingDialog& dialog) const {
    if (dialog.Type != 9 || dialog.Choices.empty()) return false;
    auto source = _VM->GetObjectById(dialog.CallerID);
    if (!PetWaitMatches(dialog, source) || source->Thread->Stack.empty()) return false;
    auto pet = dynamic_cast<VMAvatar*>(_VM->GetObjectById(dialog.AvatarID));
    return pet && !pet->Dead && pet->Object && pet->Object->OBJ->GUID == dialog.Choices.front().GUID &&
           source->Thread->Stack.back()->StackObject == pet;
}

void LotSession::RespondDialog(int code, const std::string& text) {
    if (Dialogs.empty()) return;
    if (Dialogs.front().Type == 9) {
        const auto& dialog = Dialogs.front();
        if (dialog.Block && !dialog.ChoiceSourceResolved) return;
        auto source = _VM->GetObjectById(dialog.CallerID);
        if (!PetWaitMatches(dialog, source)) {
            Dialogs.erase(Dialogs.begin());
            return; // The replacement interaction owns its own wait.
        }
        const bool accept = code == 1 && PetChoiceCurrent(dialog);
        if (accept) {
            if (std::none_of(dialog.Choices.begin(), dialog.Choices.end(), [&](const auto& choice) {
                return std::to_string(choice.Value) == text;
            })) return;
        } else if (code != 0 && PetChoiceCurrent(dialog)) return;
        if (dialog.Block) {
            auto state = std::static_pointer_cast<Primitives::VMDialogResult>(dialog.ChoiceCallerState.lock());
            state->ResponseText = accept ? text : "-1";
            state->ResponseCode = accept ? 1 : 0;
            state->Responded = true;
        }
        Dialogs.erase(Dialogs.begin());
        return;
    }
    auto d = Dialogs.front();
#ifndef __3DS__
    if(d.Type==10){
        auto source=dynamic_cast<VMAvatar*>(_VM->GetObjectById(d.CallerID));
        auto currentContent=FSO::Content::Content::Get();auto currentProvider=currentContent?currentContent->Neighborhood.get():nullptr;
        auto sourceRecord=currentProvider?currentProvider->GetNeighborByID(d.ChoiceCallerNID):nullptr;
        const bool sourceCurrent=source&&!source->Dead&&source->Thread&&sourceRecord&&sourceRecord->Unknown1==1&&sourceRecord->GUID==d.ChoiceCallerGUID&&
            d.MatchesChoiceSource(source->Object->OBJ->GUID,source->GetPersonData(Model::VMPersonDataVariable::NeighborId),source->Thread.get(),source->Thread->BlockingState);
        if(!sourceCurrent){
            // Closing an expired presentation never answers its replacement.
            if(code==0&&text=="-1")Dialogs.erase(Dialogs.begin());
            return;
        }
        odh_host::PhonebookSelection offered;offered.ready=d.ChoicesReady;
        std::optional<size_t> selected;
        for(size_t i=0;i<d.Choices.size();++i){const auto& choice=d.Choices[i];offered.choices.push_back({choice.Label,choice.Value});if(std::to_string(choice.Value)==text)selected=i;}
        if(text!="-1"&&!selected)return;
        const auto expected=odh_host::phonebook_response(offered,selected);
        if(!expected||expected->code!=code||expected->text!=text)return;
        if(selected){
            auto content=FSO::Content::Content::Get();auto provider=content?content->Neighborhood.get():nullptr;
            const auto& choice=d.Choices[*selected];
            auto neighbor=provider?provider->GetNeighborByID(int16_t(choice.Value)):nullptr;
            auto caller=dynamic_cast<VMAvatar*>(_VM->GetObjectById(d.CallerID));
            auto owner=caller&&provider?provider->GetNeighborByID(caller->GetPersonData(Model::VMPersonDataVariable::NeighborId)):nullptr;
            if(!caller||caller->Dead||!owner||owner->Unknown1!=1||owner->GUID!=caller->Object->OBJ->GUID||!neighbor||neighbor->Unknown1!=1||neighbor->GUID!=choice.GUID||neighbor->PersonData.size()<=65||neighbor->PersonData[61]==0||neighbor->PersonData[65]>=2)return;
            bool related=false;for(auto key:owner->OrderedRelationshipKeys())if(int16_t(uint32_t(key)&65535U)==choice.Value)related=true;
            if(!related)return;
        }
    }
#endif
    Dialogs.erase(Dialogs.begin());
    if (!d.Block) return;
    // VMNetDialogResponseCmd
    auto caller = _VM->GetObjectById(d.CallerID);
    if (caller == nullptr || caller->Thread == nullptr || caller->Thread->BlockingState == nullptr) return;
    if (caller->Thread->BlockingState->StateType() != Model::TSOPlatform::VMAsyncStateType::DialogResult) return;
    auto state = std::static_pointer_cast<Primitives::VMDialogResult>(caller->Thread->BlockingState);
    state->Responded = true;
    state->ResponseCode = (uint8_t)code;
    state->ResponseText = text;
}

void LotSession::Draw() {
#if defined(__vita__) || defined(__SWITCH__)
    _World->Draw(Frame, true);
#else
    _World->Draw(Frame);
#endif
}

void LotSession::Rotate(int dir) {
    auto& st = _World->State;
    int r = ((int)st.Rotation() + (dir > 0 ? 1 : 3)) & 3;
    auto center = st.CenterTile();
    st.Rotation((WorldRotation)r);
    st.CenterTile(center);
    UpdateCutaway();
}

int LotSession::SceneScale() const {
    return _World ? _World->PresentationScale : 1;
}

int LotSession::DisplayZoom() const {
    return int(_World->ViewZoom());
}

void LotSession::Zoom(int dir) {
    auto& st = _World->State;
    const int z = DisplayZoom() + (dir > 0 ? 1 : -1);
    if (z < 1 || z > 3) return;
    // Far is the smallest authored sprite set. Use it at 1:1 at the widest
    // zoom so all three zoom levels keep their original field of view.
    const int scale = _HalfResolutionLot && z > 1 && _DisplayWidth % 2 == 0 && _DisplayHeight % 2 == 0 ? 2 : 1;
    if (SceneScale() != scale) {
        _World->PresentationScale = scale;
        Frame.Resize(_DisplayWidth / scale, _DisplayHeight / scale);
        st.SetDimensions(st::Vector2(float(Frame.Width), float(Frame.Height)));
        st.CenterTile(st.CenterTile());
        _World->InvalidatePreciseZoom();
        std::printf("[portable-perf] lot scene %dx%d, display %dx%d, zoom %d\n", Frame.Width, Frame.Height, _DisplayWidth, _DisplayHeight, z);
    }
    const auto assetZoom = WorldZoom(z - (scale == 2 ? 1 : 0));
    if (st.Zoom() == assetZoom) return; // same sprites, only the framebuffer changed
    st.Zoom(assetZoom);
    // TS1(sims): sprite memory. An object's SPR2 holds the frames of all three zooms and decoded frames stay in
    // memory (near ones are 4x the medium ones: +17 MB on a full house). The zoom change above cleared the world's
    // texture cache and invalidated every object renderer, so no decoded pixels are referenced right now: drop them
    // all; the next frame decodes only what the new zoom shows.
    auto content = FSO::Content::Content::Get();
    if (content && content->WorldObjects) content->WorldObjects->DropDecodedSprites();
    // FIX(sims) memory: the wall/floor frames (SPR#, walls.iff/floors.iff/*.wll/*.flr) of the old zoom as well
    FSO::Files::Formats::IFF::IffFile::DropDecodedFrames();
}

void LotSession::ChangeLevel(int dir) {
    auto& st = _World->State;
    int l = st.Level() + dir;
    auto arch = _Ctx->Architecture();
    if (l < 1 || l > (int)arch->Stories || (size_t)l > arch->Floors.size()) return;
    if (dir > 0) { // only go up if something is built there
        bool any = false;
        for (auto& f : arch->Floors[(size_t)(l - 1)]) if (f.Pattern != 0) { any = true; break; }
        if (!any) for (auto& w : arch->Walls[(size_t)(l - 1)]) if ((int)w.Segments != 0) { any = true; break; }
        if (!any) return;
    }
    st.Level((int8_t)l);
    UpdateCutaway();
}

void LotSession::ScrollScreen(float dxPx, float dyPx) {
    // World::Scroll moves by basis vectors (FreeSims: GetScrollBasis); one unit of basis[0] is
    // TilePxWidth * multiplier pixels across, basis[1] the same distance down.
    auto& ws = _World->State.WorldSpace;
    int multiplier = ((1 << (3 - DisplayZoom())) * 3) / 2;
    float unitX = ws.TilePxWidth * multiplier;       // pixels per basis[0] unit
    float unitY = ws.TilePxHeight * multiplier * 2;  // pixels per basis[1] unit ((2,2) tiles down)
    if (unitX <= 0 || unitY <= 0) return;
    _World->Scroll(st::Vector2(dxPx / (unitX * SceneScale()), dyPx / (unitY * SceneScale())));
}

void LotSession::CenterOn(VMEntity* e) {
    if (!e || e->Dead || !_World) return;
    const auto level = _World->State.Level();
    // During object init the renderer can focus before the session owns its
    // Blueprint binding. Load finishes by rebuilding cutaway normally.
    if (_World->CenterTo(e->WorldUI.get()) && level != _World->State.Level() && Blueprint) UpdateCutaway();
}

void LotSession::SetWallsMode(int mode) { WallsMode = mode % 3; UpdateCutaway(); }

void LotSession::UpdateCutaway() {
    auto arch = _Ctx->Architecture();
    auto& st = _World->State;
    if (WallsMode == 1) {
        Blueprint->Cutaway.assign((size_t)(arch->Width * arch->Height), true);
        st.DynamicCutaway = false;
    } else if (WallsMode == 0) {
        std::unordered_set<uint32_t> rooms;
        for (size_t i = 1; i < _Ctx->RoomInfo.size(); i++) if (!_Ctx->RoomInfo[i].Room.IsOutside) rooms.insert((uint32_t)i);
        _RoomCuts = Utils::VMArchitectureTools::GenerateRoomCut(arch, st.Level(), st.Rotation(), rooms);
        st.DynamicCutaway = true;
        ApplyMouseCut(true);
    } else {
        Blueprint->Cutaway.assign((size_t)(arch->Width * arch->Height), false);
        st.DynamicCutaway = false;
    }
    // HOOK(world roofs): TS1 shows the roof with walls up when the top story is in view (the original's "Roof"
    // view); never with walls cutaway/down or while a lower story is selected.
    {
        int top = 1;
        for (int l = 1; l <= (int)arch->Floors.size(); l++)
            for (auto& f : arch->Floors[(size_t)(l - 1)]) if (f.Pattern != 0) { top = std::max(top, l); break; }
        st.DrawRoofs = (WallsMode == 2) && st.Level() >= top;
    }
    Blueprint->Damage.push_back(FSO::LotView::Model::BlueprintDamage(FSO::LotView::Model::BlueprintDamageType::WALL_CUT_CHANGED));
}

void LotSession::UpdateMouseCut(bool shown, int screenX, int screenY) {
    if (WallsMode != 0 || _World == nullptr || Blueprint == nullptr) return;
    auto& st = _World->State;
    st::Rectangle newCut; // C#: an empty rectangle while there is no mouse (touch only)
    if (shown && CursorCutaway) {
        auto mouseTilePos = st.WorldSpace.GetTileAtPosWithScroll(ScenePoint(screenX, screenY));
        newCut = st::Rectangle((int)(mouseTilePos.X - 2.5), (int)(mouseTilePos.Y - 2.5), 5, 5);
        newCut.X -= Utils::VMArchitectureTools::CutCheckDir[(int)st.Rotation()][0] * 2;
        newCut.Y -= Utils::VMArchitectureTools::CutCheckDir[(int)st.Rotation()][1] * 2;
    }
    if (newCut.X == _MouseCutRect.X && newCut.Y == _MouseCutRect.Y && newCut.Width == _MouseCutRect.Width
        && newCut.Height == _MouseCutRect.Height) return;
    _MouseCutRect = newCut;
    ApplyMouseCut(false);
}

void LotSession::ApplyMouseCut(bool force) {
    // C#: finalCut = copy of LastCuts + ApplyCutRectangle; damage only if the room cut changed, or the rectangle
    // cut (now or last time) touched a wall.
    auto arch = _Ctx->Architecture();
    if (_RoomCuts.size() != (size_t)(arch->Width * arch->Height)) return;
    auto finalCut = _RoomCuts;
    bool notableChange = (_MouseCutRect.Width > 0)
        && Utils::VMArchitectureTools::ApplyCutRectangle(arch, _World->State.Level(), finalCut, _MouseCutRect);
    if (force || notableChange || _LastRectCutNotable) {
        Blueprint->Cutaway = finalCut;
        if (!force) Blueprint->Damage.push_back(FSO::LotView::Model::BlueprintDamage(FSO::LotView::Model::BlueprintDamageType::WALL_CUT_CHANGED));
    }
    _LastRectCutNotable = notableChange;
}

static void ToChoices(const std::vector<VMPieMenuInteraction>& menu, std::vector<PieChoice>& pie) {
    pie.clear();
    for (auto& m : menu) {
        PieChoice c;
        c.Name = m.Name.empty() ? std::string("???") : m.Name;
        c.ID = m.ID;
        c.Param0 = m.Param0;
        pie.push_back(c);
    }
}

PickResult LotSession::PickDisplayObject(int x, int y, int radius) {
    return PickObjectAt(*_World, &Frame, x / SceneScale(), y / SceneScale(),
                        (radius + SceneScale() - 1) / SceneScale());
}

VMEntity* LotSession::PieTargetAt(int x, int y, std::vector<PieChoice>& pie, bool& isGoto, int16_t& pickedId) {
    pie.clear();
    isGoto = false;
    pickedId = 0;
    auto active = SelectedSim();
    if (active == nullptr) return nullptr;
    auto hit = PickDisplayObject(x, y);
    pickedId = hit.ObjectID;
    VMEntity* obj = nullptr;
    if (hit.ObjectID > 0) {
        obj = _VM->GetObjectById(hit.ObjectID);
        if (obj != nullptr && obj->MultitileGroup != nullptr) obj = obj->MultitileGroup->GetInteractionGroupLeader(obj);
        if (obj != nullptr) {
            ToChoices(obj->GetPieMenu(_VM.get(), active, false), pie);
            if (!pie.empty() || dynamic_cast<VMAvatar*>(obj) != nullptr) return obj;
        }
    }
    // empty floor (or an object without interactions): the goto object on that tile, if walkable
    if (GotoObject == nullptr) return obj;
    auto tile = _World->State.WorldSpace.GetTileAtPosWithScroll(ScenePoint(x, y));
    LotTilePos targetPos = LotTilePos::FromBigTile((int16_t)std::floor(tile.X), (int16_t)std::floor(tile.Y), _World->State.Level());
    if (_Ctx->IsOutOfBounds(targetPos) || _Ctx->SolidToAvatars(targetPos).Solid) targetPos = LotTilePos::OUT_OF_WORLD;
    GotoObject->SetPosition(targetPos, Direction::NORTH, _Ctx.get());
    if (GotoObject->Position() == LotTilePos::OUT_OF_WORLD) return obj;
    if (obj != nullptr && hit.ObjectID > 0) return obj; // tapped an object with no interactions: no "go here" (FreeSims: error tooltip)
    ToChoices(GotoObject->GetPieMenu(_VM.get(), active, false), pie);
    isGoto = true;
    return GotoObject;
}

bool LotSession::PushInteraction(VMEntity* target, bool isGoto, const PieChoice& choice) {
    auto caller = SelectedSim();
    if (caller == nullptr || target == nullptr || caller->Thread == nullptr) return false;
    // The TS1 enqueue operation may discard a lower-priority suffix even
    // when its ring is full. Let that operation decide whether input fits.
    if (!_VM->TS1 && caller->Thread->Queue.size() >= size_t(Engine::VMThread::MAX_USER_ACTIONS)) return false;
    if (isGoto) {
        // VMNetGotoCmd: a fresh goto object at the tile, the interaction is pushed on it
        auto pos = target->Position();
        auto group = _Ctx->CreateObjectInstance(GOTO_GUID, pos, Direction::NORTH, false, false);
        VMEntity* callee = (group == nullptr || group->Objects.empty()) ? nullptr : group->Objects[0];
        if (callee == nullptr || callee->Position() == LotTilePos::OUT_OF_WORLD) return false;
        auto before = caller->Thread->Queue;
        callee->PushUserInteraction(choice.ID, caller, _Ctx.get());
        bool added = false;
        for (auto& q : caller->Thread->Queue) if (std::find(before.begin(), before.end(), q) == before.end()) {
            q->UserQueued = true; // FIX(sims)
            added = true;
        }
        return added;
    }
    // VMNetInteractionCmd
    std::vector<int16_t> args = { choice.Param0, 0, 0, 0 };
    auto before = caller->Thread->Queue;
    target->PushUserInteraction(choice.ID, caller, _Ctx.get(), &args);
    // FIX(sims): mark the player's action (VMThread::EnqueueAction keeps it when a higher-priority push queue-skips)
    bool added = false;
    for (auto& q : caller->Thread->Queue) if (std::find(before.begin(), before.end(), q) == before.end()) {
        q->UserQueued = true;
        added = true;
    }
    return added;
}

void LotSession::CancelAction(uint16_t uid) {
    auto caller = SelectedSim();
    if (caller == nullptr || caller->Thread == nullptr) return;
    caller->Thread->CancelAction(uid); // VMNetInteractionCancelCmd
}

bool LotSession::EntityScreenPos(VMEntity* e, float& sx, float& sy, float heightTiles) {
    if (e == nullptr || e->WorldUI == nullptr) return false;
    auto p = e->WorldUI->DrawPosition();
    if (p.X < -2043 && p.Y < -2043) return false;
    st::Vector3 tile(p.X, p.Y, p.Z + heightTiles);
    if (dynamic_cast<VMGameObject*>(e) != nullptr) tile = st::Vector3(p.X + 0.5f, p.Y + 0.5f, p.Z + heightTiles);
    auto s = TileToScreen(*_World, tile);
    sx = s.X; sy = s.Y;
    return true;
}

}
