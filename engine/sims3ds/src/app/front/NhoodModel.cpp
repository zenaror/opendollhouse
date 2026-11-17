/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims front end — see NhoodModel.h. TS1(sims): new code on top of FreeSims' TS1NeighborhoodProvider.
#include "files/SafeFile.h" // FIX(sims) SAVE
#include "files/LoadProgress.h"
#include <set>
#include <optional>
#include "files/SafeFile.h" // FIX(sims) SAVE
#include "files/LoadProgress.h"
#include "content/TS1FamilyImport.h"
#include <cstdio>
#include "app/front/NhoodModel.h"
#include "odh/lot_picture_placement.hpp"
#include "app/front/LotSave.h"
#include "files/LotRezoneTransaction.h"
#include "files/formats/iff/IffRawFile.h"
#include "app/LotSession.h"
#include "app/front/FrontStrings.h"
#include "app/front/ResGraphics.h"
#include "app/Font.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/BMP.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "files/formats/iff/chunks/FAMI.h"
#include "files/formats/iff/chunks/FAMs.h"
#include "content/TS1NeighbourhoodWriter.h"
#include "files/formats/iff/chunks/NBRS.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "files/formats/iff/chunks/SIMI.h"
#include "files/formats/iff/chunks/STR.h"
#include "core/io.h"
#ifndef __3DS__
#include "app/ui/OriginalTextEncoding.h"
#include "app/ui/OriginalPCUI.h"
#include <exception>
#endif
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <charconv>
#include <cstdlib>
#include <limits>

namespace simapp { namespace front {

using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::Content::TS1::TS1NeighborhoodProvider;

static TS1NeighborhoodProvider* Prov() {
    auto c = FSO::Content::Content::Get();
    return c ? c->Neighborhood.get() : nullptr;
}

// Map presentation and lot loading must resolve the same read-only source.
// An installed expansion template is valid before this neighbourhood saves it.
// Static callers for another neighbourhood retain their explicit directory.
static std::string ReadNeighbourhoodPath(const std::string& userDir, const std::string& relative) {
    auto provider = Prov();
    if (provider && provider->UserPath == userDir) {
        const auto resolved = provider->FindUserOrTemplate(relative);
        if (!resolved.empty()) return resolved;
    }
    return userDir + "/" + relative;
}

static std::string ReadHousePath(const std::string& userDir, int house) {
    char relative[40]; snprintf(relative, sizeof relative, "Houses/House%02d.iff", house);
    return ReadNeighbourhoodPath(userDir, relative);
}

// ======================================================================= areas

const AreaDef& GetAreaDef(Area a, bool unleashed) {
    // Res_Nbhd.RT: kNbhdBck_UL + kNghFrameDelta1..6_UL (Unleashed, US), kNbhdBck + kNghFrameDelta1..3 (1.0),
    // kDowntownBackground + kDowntownWater0..5, kVacationIsland (+ port/trees) + kVIWater0..4,
    // kStudiotownBackground (+ top layer), kMagiclandBackground + kMagictownWater0..5.
    static const AreaDef base = { Area::Neighbourhood, "Nbhd/NScreen.BMP", {}, {},
        { "Nbhd/DiffN1-N2_8.bmp", "Nbhd/DiffN1-N3_8.bmp", "Nbhd/DiffN1-N4_8.bmp", "Nbhd/DiffN1-N3_8.bmp" }, 300,
        1, 1, 20, "Nhood", "Nbhd/Bus_loadscreen_800x600.bmp", nullptr, nullptr, 0 };
    static const AreaDef ul = { Area::Neighbourhood, "Community/NScreen_unleashed.bmp", {}, {},
        { "Community/NScreen_unleashed_waves001.bmp", "Community/NScreen_unleashed_waves002.bmp", "Community/NScreen_unleashed_waves003.bmp",
          "Community/NScreen_unleashed_waves004.bmp", "Community/NScreen_unleashed_waves005.bmp", "Community/NScreen_unleashed_waves006.bmp" }, 220,
        4, 1, 80, "Nhood", "Community/Bus_loadscreen_800x600.bmp", nullptr, nullptr, 0 };
    static const AreaDef dt = { Area::Downtown, "Downtown/DScreen.bmp", {}, {},
        { "Downtown/dscreen00.bmp", "Downtown/dscreen01.bmp", "Downtown/dscreen02.bmp", "Downtown/dscreen03.bmp", "Downtown/dscreen04.bmp" }, 260,
        2, 21, 30, "dt_nhood", "Downtown/Taxi_loadscreen_800x600.bmp", "NghUI/Downtown.bmp", "Downtown/Return.bmp", 7 };
    static const AreaDef vi = { Area::Vacation, "VIsland/visland.bmp", { "VIsland/visland_port.bmp" }, { "VIsland/visland_trees.bmp" },
        { "VIsland/visland_waves001.bmp", "VIsland/visland_waves002.bmp", "VIsland/visland_waves003.bmp", "VIsland/visland_waves004.bmp", "VIsland/visland_waves005.bmp" }, 260,
        3, 40, 49, "Vacation", "VIsland/vacation_loadscreen_800x600.bmp", "NghUI/Vacation.bmp", "NghUI/Return.bmp", 9 };
    static const AreaDef st = { Area::StudioTown, "Studiotown/DScreen.bmp", {}, { "Studiotown/DScreen_top_layer.bmp" }, {}, 0,
        5, 81, 89, "Superstar", "Studiotown/Studiotown_loadscreen_800x600.bmp", "NghUI/Studiotown.bmp", "Studiotown/Return.bmp", 11 };
    static const AreaDef mt = { Area::MagicTown, "Magicland/DScreen.bmp", {}, {},
        { "Magicland/DScreen_waves1.bmp", "Magicland/DScreen_waves2.bmp", "Magicland/DScreen_waves3.bmp", "Magicland/DScreen_waves4.bmp",
          "Magicland/DScreen_waves5.bmp", "Magicland/DScreen_waves6.bmp" }, 240,
        7, 90, 99, "MakinMagic", "Magicland/magicland_loadscreen_800x600.bmp", "NghUI/MagicLand.bmp", "Magicland/Return.bmp", 7 };
    switch (a) {
        case Area::Downtown: return dt;
        case Area::Vacation: return vi;
        case Area::StudioTown: return st;
        case Area::MagicTown: return mt;
        default: return unleashed ? ul : base;
    }
}

// ======================================================================= small IFF helpers

/// Reads chunks of the wanted types straight from the file (no full parse): (type, id) -> data.
static std::map<std::pair<std::string, int>, std::vector<uint8_t>> ReadChunks(const std::string& path, const std::vector<std::pair<std::string, int>>& wanted) {
    std::map<std::pair<std::string, int>, std::vector<uint8_t>> out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long p = 64;
    while (p + 76 <= size) {
        uint8_t h[76];
        fseek(f, p, SEEK_SET);
        if (fread(h, 1, 76, f) != 76) break;
        std::string type((const char*)h, 4);
        uint32_t csize = ((uint32_t)h[4] << 24) | ((uint32_t)h[5] << 16) | ((uint32_t)h[6] << 8) | h[7];
        int id = (int16_t)((h[8] << 8) | h[9]);
        if (csize < 76 || p + (long)csize > size) break;
        for (auto& w : wanted) {
            if (w.first == type && (w.second == id || w.second == -99999)) {
                std::vector<uint8_t> d(csize - 76);
                if (!d.empty() && fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
                out[std::make_pair(type, id)] = std::move(d);
                break;
            }
        }
        p += csize;
    }
    fclose(f);
    return out;
}

static std::unique_ptr<IffFile> OpenIff(const std::string& path) {
    if (!UserDataSetup::FileExists(path)) return nullptr;
    try { return std::unique_ptr<IffFile>(new IffFile(path)); } catch (...) { return nullptr; }
}

static std::vector<std::string> SplitComma(const std::string& s) {
    std::vector<std::string> r;
    size_t a = 0;
    while (true) {
        size_t c = s.find(',', a);
        std::string t = s.substr(a, c == std::string::npos ? std::string::npos : c - a);
        while (!t.empty() && (t[0] == ' ' || t[0] == '\t')) t.erase(0, 1);
        while (!t.empty() && (t.back() == ' ' || t.back() == '\r' || t.back() == '\t')) t.pop_back();
        r.push_back(t);
        if (c == std::string::npos) break;
        a = c + 1;
    }
    return r;
}

// ======================================================================= neighbourhood

bool NhoodModel::Switch(int index, const UserDataSetup& setup, std::string& error) {
    auto content = FSO::Content::Content::Get();
    auto prov = Prov();
    if (content == nullptr || prov == nullptr) { error = "The game data is not loaded."; return false; }
    BrokenIndex = -1;
    if (setup.PreparationPending(index)) {
        error = "The neighbourhood copy is incomplete. Restart to retry, or restore the neighbourhood.";
        return false;
    }
    // FIX(sims) SAVE: repair interrupted saves before anything is unloaded (src/files/SafeFile.h). Each folder's
    // leftovers once per run (three listings, ~0.1 s on the 3DS); Neighborhood.iff itself is checked every time.
    {
        std::string dir = setup.SaveRoot() + "/" + UserDataSetup::FolderName(index);
        if (!FSO::Content::TS1::IsPrivateNeighbourhood(setup.DataRoot(), setup.SaveRoot(), dir)) {
            error = "The neighbourhood is outside the writable save area.";
            return false;
        }
        if (!UserDataSetup::DirExists(dir)) {
            dir = setup.SaveRoot() + "/UserData"; // Same fallback as TS1NeighborhoodProvider::InitSpecific.
            if (!FSO::Content::TS1::IsPrivateNeighbourhood(setup.DataRoot(), setup.SaveRoot(), dir)) {
                error = "The fallback neighbourhood is outside the writable save area.";
                return false;
            }
        }
        // A converter publication spans ownership, house and zoning. Repair it
        // before ordinary one-file repair or any provider/model reads.
        if (!FSO::Files::LotRezoneTransaction::Recover(dir, error)) {
            BrokenIndex = index; return false;
        }
        static std::set<std::string> scanned;
        if (scanned.insert(dir).second) {
            int n = FSO::Files::SafeFile::RecoverDir(dir) + FSO::Files::SafeFile::RecoverDir(dir + "/Houses") +
                    FSO::Files::SafeFile::RecoverDir(dir + "/Characters");
            if (n > 0) FSO::Files::LoadProgress::Step("front: restored " + std::to_string(n) + " file(s) of an interrupted save in " + dir);
        }
        auto res = FSO::Files::SafeFile::Recover(dir + "/Neighborhood.iff", FSO::Files::SafeFile::IffValidator("NBRS"));
        if (res == FSO::Files::SafeFile::Result::Missing || res == FSO::Files::SafeFile::Result::Damaged) {
            BrokenIndex = index;
            setup.MarkBroken(index);
            error = "Neighborhood " + std::to_string(index + 1) + " could not be loaded: its save file (" + dir + "/Neighborhood.iff) is " +
                    (res == FSO::Files::SafeFile::Result::Missing ? "missing" : "damaged") + " and there is no backup of it.";
            FSO::Files::LoadProgress::Step("ERROR: " + error);
            return false;
        }
    }
    // the previous neighbourhood's characters (User*.iff GUIDs repeat between neighbourhoods)
    auto world = content->WorldObjects.get();
    if(world){auto prior=prov->CharacterFiles;
        const auto prefix=prov->UserPath+"/Characters/";
        for(const auto& entry:world->Entries)if(entry.second.FileName.compare(0,prefix.size(),prefix)==0)
            prior.push_back(entry.second.FileName);
        prior.insert(prior.end(),prov->TemplateCharacterFiles.begin(),prov->TemplateCharacterFiles.end());
        world->RemoveObjectsFromFiles(prior);
        prov->CharacterFiles.clear();prov->TemplateCharacterFiles.clear();
    }
    prov->UserDataRoot = setup.SaveRoot();
    prov->InitSpecific(index);
    if (prov->MainResource == nullptr) {
        error = "Could not open " + prov->UserPath + "/Neighborhood.iff";
        // FIX(sims) SAVE: keep the neighbourhood that was loaded (was: everything unloaded -> an empty neighbourhood)
        if (Current >= 0 && Current != index) {
            prov->InitSpecific(Current);
            if (prov->MainResource != nullptr) { prov->LoadCharacters(); if (world != nullptr) world->RebuildIndexes(); RefreshFamilies(); }
        }
        return false;
    }
    prov->LoadCharacters();
    // Complete Collection delivers pending expansion families in this save's
    // Patch folder. This explicit queue is distinct from global templates and
    // from a player's intentionally absent/deleted family.
    {
        const auto patches = FSO::Content::TS1::TS1FamilyImport::ApplyPendingPatches(prov->UserPath, setup.DataRoot());
        for (const auto& patch : patches) if (!patch.Ok) {
            error = patch.Error; return false;
        }
        if (!patches.empty()) {
            prov->InitSpecific(index);
            if (!prov->MainResource) { error = "Could not reopen neighbourhood after its family patches"; return false; }
            prov->LoadCharacters();
            if (world) world->RebuildIndexes();
        }
    }
    // Installation defaults belong to a newly created neighbourhood only.
    // Neither missing native markers nor absent template GUIDs prove newness.
    if (setup.TemplateFamiliesPending(index)) {
        const auto rs = FSO::Content::TS1::TS1FamilyImport::InstallTemplates(prov->UserPath, setup.DataRoot(), true);
        bool complete = true, changed = false;
        for (const auto& r : rs) { complete = complete && r.Ok; changed = changed || (r.Ok && !r.Skipped); }
        if (!complete || !setup.CompleteTemplateFamilies(index)) {
            error = "Could not finish preparing the new neighbourhood's template families.";
            return false;
        }
        if (changed) {
            prov->InitSpecific(index);
            if (prov->MainResource == nullptr) { error = "Could not open " + prov->UserPath + "/Neighborhood.iff"; return false; }
            prov->LoadCharacters();
            if (world != nullptr) world->RebuildIndexes();
        }
    }
    // Reconcile after this neighbourhood's characters/templates are registered,
    // before publishing family lists or exposing the neighbourhood selector.
    size_t registered=0;
    if(!prov->AddMissingNeighbors(FSO::Content::TS1::PreparePersonTemplate,registered,error))return false;
    if(registered&&!FSO::Content::TS1::TS1NeighbourhoodWriter::SaveRoster(
            prov,prov->UserPath+"/Neighborhood.iff",&error)){
        // Do not expose a roster that failed to persist. Reloading also removes
        // only this attempted registration from memory; player files stay put.
        prov->InitSpecific(index);if(prov->MainResource)prov->LoadCharacters();return false;
    }
    if(registered)FSO::Files::LoadProgress::Step("front: registered "+std::to_string(registered)+" missing person templates");
    Current = index;
    UserDir = prov->UserPath;
    _DeletedFamilies.clear();
    Dirty = false;
    RefreshFamilies();
    LoadDescriptions();
    return true;
}

void NhoodModel::RefreshFamilies() {
    Families.clear();
    auto prov = Prov();
    auto content = FSO::Content::Content::Get();
    if (prov == nullptr || prov->MainResource == nullptr) return;
    for (auto fami : prov->ListFamilies()) {
        if (fami->ChunkID == 0 || fami->FamilyNumber < 0) continue; // the townie "Default" family
        FamilyInfo f;
        f.ChunkID = (int16_t)fami->ChunkID;
        const auto familyText = prov->GetFamilyText(f.ChunkID);
        f.Name = familyText.Value(0);
        f.SourceLanguage = familyText.SourceLanguage;
        f.TextUTF8 = familyText.UTF8;
        f.TextAmbiguous = familyText.Ambiguous;
        if (f.TextAmbiguous) printf("[nhood] ambiguous personal FAMs language for family %d\n", f.ChunkID);
        f.House = fami->HouseNumber;
        f.Budget = fami->Budget;
        f.NetWorth = fami->NetWorth; // Legacy name: raw FAMI ValueInArch (offset24), excludes Budget.
        f.Friends = fami->FamilyFriends;
        f.MagiCoins = prov->GetMagicoinsForFamily(*fami);
        for (auto g : fami->FamilyGUIDs) {
            SimInfo s;
            s.GUID = g;
            int16_t nid = 0;
            auto nb = prov->GetNeighborIDForGUID(g, nid) ? prov->GetNeighborByID(nid) : nullptr;
            s.NeighbourID = nid;
            if (nb) s.Name = nb->Name;
            auto obj = content->WorldObjects->Get(g, true);
            if (obj != nullptr && obj->Resource != nullptr && obj->OBJ != nullptr) {
                auto ctss = obj->Resource->Get<CTSS>(obj->OBJ->CatalogStringsID);
                const auto personal = FSO::Content::TS1::ResolvePersonalText(ctss);
                if (!personal.Value(0).empty()) s.Name = personal.Value(0);
                s.Bio = personal.Value(1);
                s.SourceLanguage = personal.SourceLanguage;
                s.TextUTF8 = personal.UTF8;
                s.TextAmbiguous = personal.Ambiguous;
                if (s.TextAmbiguous) printf("[nhood] ambiguous personal CTSS language for neighbour %d\n", nid);
                // STR# 200 'bodystring': [0] adult/child, [12] male/female (character iffs)
                // portrait: BMP_ 2002 'faces' = normal / highlighted / selected, side by side; else the 25x25 thumbnail
                auto faces = obj->Resource->Get<BMP>(2002);
                auto thumbnail = obj->Resource->Get<BMP>(2004);
                if (thumbnail) DecodeImage(thumbnail->GetData(), s.Thumbnail, true);
                if (faces) {
                    Canvas all;
                    if (DecodeImage(faces->GetData(), all, true) && all.Width >= 3) Crop(all, s.Face, 0, 0, all.Width / 3, all.Height);
                } else if (thumbnail) DecodeImage(thumbnail->GetData(), s.Face, true);
#ifndef __3DS__
                else {
                    // MakeNewCharacter deliberately omits template portraits.
                    // Preserve every authored resource (even a failed decode);
                    // only an absent pair may use the existing HUD/CAS service.
                    try {
                        Canvas generated;
                        if (OriginalPCUI::Get().GenerateFaces(obj, generated) && generated.Width >= 3 && generated.Height > 0)
                            Crop(generated, s.Face, 0, 0, generated.Width / 3, generated.Height);
                    } catch (const std::exception& error) {
                        // One missing appearance must not lose the family
                        // member/name/GUID or prevent the map from opening.
                        printf("[nhood] missing portrait for neighbour %d GUID %08X: %s\n", nid, g, error.what());
                    }
                }
#endif
                auto body = obj->Resource->Get<STR>(200);
                if (body != nullptr) {
                    s.Child = body->GetString(0) == "child";
                    s.Female = body->GetString(12) == "female";
                    auto kind = body->GetString(0);
                    s.Pet = kind == "dog" || kind == "cat" || kind == "kat"; // FIX(sims) FB1
                }
            }
            s.FirstName = s.Name; // CTSS[0] is the complete given name, including spaces.
            f.Members.push_back(s);
        }
        Families.push_back(f);
    }
}

const FamilyInfo* NhoodModel::Family(int16_t id) const {
    for (auto& f : Families) if (f.ChunkID == id) return &f;
    return nullptr;
}

const FamilyInfo* NhoodModel::FamilyInHouse(int house) const {
    if (house <= 0) return nullptr;
    for (auto& f : Families) if (f.House == house) return &f;
    return nullptr;
}

std::vector<const FamilyInfo*> NhoodModel::Bin() const {
    std::vector<const FamilyInfo*> r;
    // FIX(sims) FB1: the Unleashed "Strays" (TemplateFamilyUnleashed/Strays_4000.FAM, family 4000: eight stray dogs and
    // cats, no person) is the stray-pet NPC family of the Old Town adoption centre, not a playable household; the
    // original family bin never lists it and a household needs at least one person. It was the only family in the bin
    // of neighbourhood 2 and every new neighbourhood, and moving it in left a lot with no controllable Sim.
    for (auto& f : Families) {
        if (f.House != 0 || f.Members.empty()) continue;
        bool person = false;
        for (auto& m : f.Members) if (!m.Pet) person = true;
        if (person) r.push_back(&f);
    }
    return r;
}

void NhoodModel::LoadDescriptions() {
    _Desc.clear();
    // TS1NeighborhoodProvider.GetHouseNameDesc reads NeighborhoodDesc/STDesc/MTDesc (STR# 2000+house: name, desc);
    // Downtown (DTDesc) and Vacation (VIDesc) work the same way.
    static const char* files[] = { "NeighborhoodDesc.iff", "DTDesc.iff", "VIDesc.iff", "STDesc.iff", "MTDesc.iff" };
    for (auto f : files) {
        auto iff = OpenIff(ReadNeighbourhoodPath(UserDir, std::string("Houses/") + f));
        if (!iff) continue;
        for (auto str : iff->List<STR>()) {
            int house = (int)str->ChunkID - 2000;
            if (house < 0 || house > 99 || str->Length() < 1) continue;
            std::string name = str->GetString(0), desc = str->Length() > 1 ? str->GetString(1) : "";
            while (!name.empty() && (name.back() == ' ' || name.back() == ':')) name.pop_back();
            if (name.empty() && desc.empty()) continue;
            if (!_Desc.count(house) || _Desc[house].first.empty()) _Desc[house] = std::make_pair(name, desc);
        }
    }
}

void NhoodModel::LoadPositions(int table) {
    _Positions.clear();
    auto prov = Prov();
    if (!prov) return;
    auto entries = prov->LotMaps.find((int16_t)table);
    if (entries == prov->LotMaps.end()) return;
    for (const auto& entry : entries->second) {
        int house = std::get<0>(entry);
        if (!_Positions.count(house))
            _Positions[house] = std::make_pair(std::get<1>(entry), std::get<2>(entry));
    }
}

static std::string FormatStreetAddress(IffFile* iff, int house) {
    auto& S = FrontStrings::Get();
    std::string fmt;
    if (iff) {
        auto names = iff->Get<STR>(2000), assign = iff->Get<STR>(2001);
        if (names && assign && house >= 1 && house <= assign->Length()) {
            int street = atoi(assign->GetString(house - 1).c_str());
            if (street >= 1 && street <= names->Length()) fmt = names->GetString(street - 1);
        }
    }
    if (fmt.empty()) {
        // NghRollover street formats: Sim Lane / Wright Way (downtown) / Old Volcano Way (vacation) /
        // Studio Town Drive / Calamity Forest
        if (house >= 21 && house <= 30) fmt = S.Get(134, 13, "%s Wright Way");
        else if (house >= 40 && house <= 49) fmt = S.Get(134, 15, "%s Old Volcano Way");
        else if (house >= 81 && house <= 89) fmt = S.Get(134, 25, "%s Studio Town Drive");
        else if (house >= 90) fmt = S.Get(134, 27, "%s Calamity Forest");
        else fmt = S.Get(134, 7, "%s Sim Lane");
    }
    return Format(fmt, { std::to_string(house) });
}

std::string NhoodModel::StreetAddress(const std::string& userDir, int house) {
    const auto streets = OpenIff(ReadNeighbourhoodPath(userDir, "StreetNames.iff"));
    return FormatStreetAddress(streets.get(), house);
}

bool NhoodModel::HousePicture(const std::string& userDir, int house, Canvas& out) {
    auto chunks = ReadChunks(ReadHousePath(userDir, house), { { "BMP_", 512 } });
    auto it = chunks.find(std::make_pair(std::string("BMP_"), 512));
    return it != chunks.end() && DecodeImage(it->second, out);
}

bool NhoodModel::OpenPicture(const LotInfo& lot, Canvas& out) const {
    const int id=lot.HalfPictureFamily?1024:512;
    auto chunks=ReadChunks(ReadHousePath(UserDir,lot.House),{{"BMP_",id}});
    auto it=chunks.find(std::make_pair(std::string("BMP_"),id));
    Canvas picture;
    if(it==chunks.end()||!DecodeImage(it->second,picture))return false;
    if(lot.HalfPictureFamily||lot.Scale<=1)out=std::move(picture);
    else Resample(picture,out,std::max(1,picture.Width/lot.Scale),std::max(1,picture.Height/lot.Scale));
    return true;
}

bool NhoodModel::Unleashed() const {
    return UserDataSetup::FileExists(ReadHousePath(UserDir, 50));
}

bool NhoodModel::AreaAvailable(Area a) const {
    const AreaDef& d = GetAreaDef(a, Unleashed());
    if (a == Area::Neighbourhood) return true;
    for (int h = d.FirstHouse; h <= d.LastHouse; h++) {
        if (UserDataSetup::FileExists(ReadHousePath(UserDir, h))) return ResGraphics::Get().Exists(d.Background) || !ResGraphics::Get().IsOpen();
    }
    return false;
}

LotInfo* NhoodModel::Lot(int house) {
    for (auto& l : Lots) if (l.House == house) return &l;
    return nullptr;
}

void NhoodModel::LoadArea(Area a, const std::map<int,std::pair<int,int>>* mapPositions, bool loadThumbnail) {
    CurrentArea = a;
    Lots.clear();
    auto prov = Prov();
    const bool unleashed = Unleashed();
    const auto streets = OpenIff(ReadNeighbourhoodPath(UserDir, "StreetNames.iff"));
    const AreaDef& def = GetAreaDef(a, unleashed);
    if(mapPositions)_Positions=*mapPositions;else LoadPositions(def.LotTable);
    for (auto& kv : _Positions) {
        FSO::Files::LoadProgress::Progress("front: neighbourhood map", 0, 0); // ADDED(sims): ~40 ms per lot on the 3DS
        int h = kv.first;
        if (a != Area::Neighbourhood && (h < def.FirstHouse || h > def.LastHouse)) continue;
        LotInfo lot;
        lot.House = h;
        lot.CX = kv.second.first; lot.CY = kv.second.second;
        std::string path = ReadHousePath(UserDir, h);
        lot.Exists = UserDataSetup::FileExists(path);
        if (!lot.Exists) continue;
        lot.Community = prov != nullptr && prov->GetZoning((int16_t)h) == 1;
        if (a == Area::Downtown || a == Area::Vacation || a == Area::StudioTown) lot.Community = true;
        if (a == Area::MagicTown) lot.Community = prov == nullptr || prov->GetZoning((int16_t)h) != 0;
        auto d = _Desc.find(h);
        if (d != _Desc.end()) { lot.Name = d->second.first; lot.Desc = d->second.second; }
        lot.Address = FormatStreetAddress(streets.get(), h);
        auto fam = FamilyInHouse(h);
        lot.Family = fam ? fam->ChunkID : -1;
        // Each original picture family has its own centroid/crop packet.
        // Use half-size artwork with THMB1024, or scale the full family and
        // THMB512 together; independent crops need not have matching bounds.
        lot.Scale = (a == Area::Neighbourhood && unleashed) ? 2 : 1;
        // The one-screen presentation has already decoded its map thumbnails.
        // Lot value, zoning and magic-house rules still come from the saved lot.
        static const std::vector<std::pair<std::string,int>> facts{{"SIMI",1}};
        static const std::vector<std::pair<std::string,int>> factsAndArt{
            {"SIMI",1},{"BMP_",513},{"BMP_",512},{"BMP_",1025},{"BMP_",1024},{"THMB",512},{"THMB",1024}};
        auto chunks = ReadChunks(path,loadThumbnail ? factsAndArt : facts);
        if (loadThumbnail) {
            auto bmp = [&](int id) { auto it = chunks.find(std::make_pair(std::string("BMP_"), id)); return it == chunks.end() ? (const std::vector<uint8_t>*)nullptr : &it->second; };
            Canvas full;
            int fullW = 0, fullH = 0, fullID = 0;
            for (int id : { 513, 512 }) if (auto b = bmp(id)) if (DecodeImage(*b, full)) { fullW = full.Width; fullH = full.Height; fullID = id; break; }
            const auto readMetadata=[&](int id)->std::optional<std::array<int,5>>{
                const auto found=chunks.find(std::make_pair(std::string("THMB"),id));
                if(found==chunks.end()||found->second.size()<20)return {};
                std::array<int,5> values;
                for(size_t i=0;i<5;++i){const auto* q=found->second.data()+i*4;
                    values[i]=int(int32_t(uint32_t(q[0])|(uint32_t(q[1])<<8)|(uint32_t(q[2])<<16)|(uint32_t(q[3])<<24)));
                    if(values[i]<-32768||values[i]>32767)return {};
                }
                return values;
            };
            lot.HalfPictureFamily=false;
            const auto halfMetadata=readMetadata(1024);
            if(lot.Scale>1&&halfMetadata){
                Canvas half,halfBase;
                const auto base=bmp(1024),roof=bmp(1025);
                if(base&&roof&&DecodeImage(*base,halfBase)&&DecodeImage(*roof,half)){
                    lot.Thumb=std::move(half);lot.HalfPictureFamily=true;
                }
            }
            if(!lot.HalfPictureFamily){
                if(lot.Scale==1)lot.Thumb=std::move(full);
                else if(fullW>0)Resample(full,lot.Thumb,std::max(1,fullW/lot.Scale),std::max(1,fullH/lot.Scale));
            }
            const int divisor=lot.HalfPictureFamily?1:lot.Scale;
            const auto metadata=lot.HalfPictureFamily?*halfMetadata:
                readMetadata(512).value_or(std::array<int,5>{{fullW/2,fullH/2,0,0,0}});
            const auto origins=odh::lot_picture_origins(lot.CX,lot.CY,metadata,divisor);
            const bool baseFallback=!lot.HalfPictureFamily&&fullID==512;
            const auto selected=baseFallback?origins.base:origins.roofed;
            lot.X=selected[0];lot.Y=selected[1];
            lot.OpenDX=origins.base[0]-lot.X;lot.OpenDY=origins.base[1]-lot.Y;
        }
        auto si = chunks.find(std::make_pair(std::string("SIMI"), 1));
        if (si != chunks.end() && !si->second.empty()) {
            SIMI simi;
            try {
                st::Reader r(si->second);
                simi.Read(nullptr, r);
                lot.MagicHouseFlag = simi.GlobalData.at(22) != 0;
            } catch (...) {}
            lot.Value = simi.PurchaseValue();
            lot.EmptyLot = simi.ArchitectureValue == 0;
        }
        Lots.push_back(std::move(lot));
    }
    // draw order: back to front (FIX(sims) FB1: by the lot's centre, as FreeSO orders the house buttons)
    std::stable_sort(Lots.begin(), Lots.end(), [](const LotInfo& x, const LotInfo& y) { return x.CY < y.CY; });
}

static bool IsMagicResidence(int house) { return house >= 90 && house <= 92; }

static int MagicPrice(int house) {
    const auto raw = FrontStrings::Get().Get(134, house == 91 ? 32 : 33);
    int value = 0;
    const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size() && value > 0 ? value : -1;
}

std::string NhoodModel::Rollover(const LotInfo& lot, const FamilyInfo* moving) const {
    auto& S = FrontStrings::Get();
    auto text = [](const std::string& raw) {
#ifndef __3DS__
        return original::LegacyToUTF8(raw);
#else
        return raw;
#endif
    };
    auto familyName = [](const FamilyInfo& family) {
        if (family.TextAmbiguous) return std::string("[name unavailable]");
#ifndef __3DS__
        return original::PersonalToUTF8(family.Name, family.SourceLanguage, family.TextUTF8);
#else
        return family.Name;
#endif
    };
    if (lot.Family >= 0) {
        auto f = Family(lot.Family);
        if (f && IsMagicResidence(lot.House)) {
            return Format(text(S.Get(134, 34)), {familyName(*f), text(Simoleons(f->Budget + f->NetWorth)),
                          std::to_string(f->MagiCoins), std::to_string(f->Friends)});
        }
        if (f) return Format(text(S.Get(134, 0, "The %s Family\nNet Worth: %s\nFriends: %d")), { familyName(*f), text(Simoleons(f->Budget + f->NetWorth)), std::to_string(f->Friends) });
    }
    std::string name = text(lot.Name.empty() ? lot.Address : lot.Name);
    std::string desc = text(lot.Desc.empty() ? (lot.Name.empty() ? S.Get(134, 23, "No Description") : lot.Address) : lot.Desc);
    if (lot.Community) {
        if (CurrentArea == Area::Neighbourhood || CurrentArea == Area::MagicTown) return Format(text(S.Get(134, 17, "%s\n%s\nThis is a community lot.")), { name, desc });
        return Format(text(S.Get(134, 21, "%s\n%s\nThis is a Community lot. Please click here to visit.")), { name, desc });
    }
    std::string cost = text(Simoleons(lot.Value));
    if (IsMagicResidence(lot.House)) {
        const int price = MagicPrice(lot.House);
        if (price < 0) return text(S.Get(132, 19, "Could not move in!"));
        std::vector<std::string> args{name, desc, cost, std::to_string(price)};
        int index = 29;
        if (moving) {
            const bool cash = moving->Budget >= lot.Value;
            const bool coins = moving->MagiCoins >= price;
            index = coins ? (cash ? 31 : 38) : (cash ? 36 : 37);
            args.push_back(familyName(*moving));
            args.push_back(coins ? text(Simoleons(moving->Budget)) : std::to_string(moving->MagiCoins));
        }
        return Format(text(S.Get(134, index)), args);
    }
    if (moving == nullptr) return Format(text(S.Get(134, 1, "%s\n%s\nCost: %s\nPick or create a family and move them in to play.")), { name, desc, cost });
    if (moving->Budget < lot.Value)
        return Format(text(S.Get(134, lot.EmptyLot ? 4 : 2, "%s\n%s\nCost: %s\nThe %s family only has %s.  They can't afford this house.")), { name, desc, cost, familyName(*moving), text(Simoleons(moving->Budget)) });
    return Format(text(S.Get(134, lot.EmptyLot ? 5 : 3, "%s\n%s\nCost: %s\nThis could be the house of your dreams!  If you buy, the family will have %s to spend on household items.")),
                  { name, desc, cost, text(Simoleons(moving->Budget - lot.Value)) });
}

// ======================================================================= actions

NhoodModel::MoveResult NhoodModel::CanMoveIn(int16_t family, int house) const {
    auto f = Family(family);
    auto prov = Prov();
    if (f == nullptr || prov == nullptr || FamilyAway(family)) return MoveResult::Missing;
    { bool person = false; for (auto& m : f->Members) if (!m.Pet) person = true; if (!person) return MoveResult::Missing; } // FIX(sims) FB1
    if (FamilyInHouse(house) != nullptr) return MoveResult::Occupied;
    if (prov->GetZoning((int16_t)house) == 1 || (house >= 21 && house <= 49) || (house >= 81 && house <= 89)) return MoveResult::Community;
    const auto lot = std::find_if(Lots.begin(), Lots.end(), [house](const LotInfo& l) { return l.House == house; });
    if (!IsMagicResidence(house))
        return f->Budget < (lot == Lots.end() ? 0 : lot->Value) ? MoveResult::CantAfford : MoveResult::Ok;
    if (lot == Lots.end() || !lot->Exists) return MoveResult::Missing;
    auto* candidate = prov->GetFamily(family);
    if (!candidate || candidate->HouseNumber != 0) return MoveResult::Missing;
    if (prov->GetFamilyForHouse(int16_t(house))) return MoveResult::Occupied;
    if (lot->MagicHouseFlag < 0) return MoveResult::Missing;
    if (lot->Community) return MoveResult::Community;
    if (lot->MagicHouseFlag) {
        const int price = MagicPrice(house);
        if (price < 0) return MoveResult::Missing;
        if (prov->GetMagicoinsForFamily(*candidate) < price) return MoveResult::NotEnoughMagiCoins;
    }
    return candidate->Budget < lot->Value ? MoveResult::CantAfford : MoveResult::Ok;
}

NhoodModel::MoveResult NhoodModel::PurchaseForEntry(int16_t family, int house, bool confirmed,
        std::unique_ptr<LotSession>& opened, std::string& error) {
    if (!confirmed) return MoveResult::Cancelled;
    auto* lot = Lot(house);
    auto* prov = Prov();
    if (!lot || !prov) return MoveResult::Missing;
    std::unique_ptr<LotSession> session;
    const bool magic = IsMagicResidence(house);
    if (magic) {
        // The 2003 purchase checks an opened HouseNN.iff before charging.
        // Reuse its parsed chunks when the live VM starts.
        try {
            session = std::make_unique<LotSession>();
            if (!session->OpenHouse(house, false, error)) return MoveResult::Missing;
            auto* simi = session->LoadedHouseResource()->Get<SIMI>(1);
            if (!simi || simi->GlobalData.size() <= 22) return MoveResult::Missing;
            const int flag = simi->GlobalData[22] != 0;
            const int value = simi->PurchaseValue();
            if (lot->MagicHouseFlag != flag || lot->Value != value) {
                // Let the map quote the new price/confirmation before retrying.
                lot->MagicHouseFlag = flag;
                lot->Value = value;
                error = "House data changed since the purchase was offered";
                return MoveResult::Missing;
            }
        } catch (const std::exception& e) { error = e.what(); return MoveResult::Missing; }
    }
    const auto result = CanMoveIn(family, house);
    if (result != MoveResult::Ok) return result;
    auto* fami = prov->GetFamily(family);
    if (!fami || fami->HouseNumber != 0) return MoveResult::Missing;
    if (prov->GetFamilyForHouse(int16_t(house))) return MoveResult::Occupied;
    if (fami->Budget < lot->Value) return MoveResult::CantAfford;
    const int price = magic && lot->MagicHouseFlag ? MagicPrice(house) : 0;
    // Prepare copies before mutating the neighbourhood. Only paying residents
    // and the three existing FAMI purchase fields change.
    struct Wallet { std::vector<InventoryItem>* Live; std::vector<InventoryItem> Paid; };
    std::vector<Wallet> wallets;
    int remaining = price;
    if (remaining) for (const auto& stack : prov->FamilyMagiCoinStacks(*fami)) {
        auto* inv = prov->GetInventoryByNID(stack.first);
        wallets.push_back({inv, *inv});
        auto& paid = wallets.back().Paid;
        auto it = paid.begin() + stack.second;
        const int take = std::min(remaining, int(it->Count));
        it->Count -= uint16_t(take);
        remaining -= take;
        if (!it->Count) paid.erase(it);
        if (!remaining) break;
    }
    if (remaining) return MoveResult::NotEnoughMagiCoins;
    const auto oldHouse = fami->HouseNumber;
    const auto oldBudget = fami->Budget;
    const auto oldWorth = fami->NetWorth;
    auto owner = prov->FamilyForHouse.emplace(int16_t(house), nullptr);
    fami->HouseNumber = house;
    fami->Budget -= lot->Value;
    fami->NetWorth = lot->Value;
    for (auto& w : wallets) w.Live->swap(w.Paid);
    bool published = false;
    int balance = 0;
    try {
        balance = prov->GetMagicoinsForFamily(*fami);
        published = FSO::Content::TS1::TS1NeighbourhoodWriter::SavePurchase(
            prov, UserDir + "/Neighborhood.iff", _DeletedFamilies, price > 0, &error);
    } catch (const std::exception& e) { error = e.what(); }
    if (!published) {
        fami->HouseNumber = oldHouse;
        fami->Budget = oldBudget;
        fami->NetWorth = oldWorth;
        for (auto& w : wallets) w.Live->swap(w.Paid);
        if (owner.second) prov->FamilyForHouse.erase(owner.first);
        return MoveResult::SaveFailed;
    }
    // The writer has committed. Finalize only existing scalar/cache fields;
    // rebuilding portraits or refreshing UI cannot undo the saved purchase.
    owner.first->second = fami;
    lot->Family = family;
    for (auto& f : Families) if (f.ChunkID == family) {
        f.House = house;
        f.Budget = fami->Budget;
        f.NetWorth = fami->NetWorth;
        f.MagiCoins = balance;
    }
    _DeletedFamilies.clear();
    Dirty = false;
    opened = std::move(session);
    return MoveResult::Ok;
}

bool NhoodModel::FamilyAway(int16_t family) const {
    auto prov=Prov(); return prov && prov->GameState.SavedVisits.count(family);
}

bool NhoodModel::Rezone(int house, bool community, std::string& error) {
    using Transaction = FSO::Files::LotRezoneTransaction;
    auto provider = Prov();
    auto lot = Lot(house);
    if (!provider || provider->UserPath != UserDir || !provider->HasPrivateNeighbourhood() ||
        CurrentArea != Area::Neighbourhood || !lot || !lot->Exists ||
        house < 1 || house > 80 || (house >= 21 && house <= 49)) {
        error = "This property cannot be rezoned from this map"; return false;
    }
    // The original does not demolish, evict or save when the chosen type is unchanged.
    if ((provider->GetZoning(int16_t(house)) != 0) == community) return true;
    if (Dirty || !_DeletedFamilies.empty()) {
        error = "Save pending neighbourhood changes before changing zoning"; return false;
    }
    auto family = provider->GetFamilyForHouse(int16_t(house));
    if (family && FamilyAway(int16_t(family->ChunkID))) {
        error = "Return the travelling family home before changing this lot"; return false;
    }
    const int64_t refund = family ? int64_t(family->Budget) + family->NetWorth : 0;
    if (family && (family->NetWorth < 0 || refund < INT32_MIN || refund > INT32_MAX)) {
        error = "The property value cannot be refunded safely"; return false;
    }
    if (!Transaction::Recover(UserDir, error)) return false;
    std::vector<Transaction::Change> changes;
    Transaction::Change change;
    std::vector<uint8_t> neighbourhood;
    if (family) {
        if (!FSO::Content::TS1::TS1NeighbourhoodWriter::PrepareEviction(provider, int16_t(house), neighbourhood, &error) ||
            !Transaction::Capture(UserDir, house, 1, neighbourhood, change, error)) return false;
        changes.push_back(std::move(change));
    }
    // Pin the writable house preimage before load/render progress callbacks.
    if (!Transaction::Capture(UserDir, house, 2, {}, change, error)) return false;
    std::string log;
    if (!PrepareBulldozedLot(house, change.After, error, log)) return false;
    changes.push_back(std::move(change));
    IffRawFile zoning;
    const auto zoningPath = ReadNeighbourhoodPath(UserDir, "LotZoning.iff");
    if (!zoning.Load(zoningPath, &error)) return false;
    auto raw = zoning.Find("STR#", 1);
    if (!raw) {error="The zoning file has no authoritative table";return false;}
    STR table;
    try {st::Reader reader(raw->Data);table.Read(nullptr,reader);} catch (const std::exception& e) {error=e.what();return false;}
    const auto us = STRLangCode::EnglishUS;
    const std::string prefix=std::to_string(house)+",";
    int match=-1;
    for(int i=0;i<table.Length(us);++i){
        auto row=table.GetString(i,us);auto comma=row.find(',');
        if(comma==std::string::npos)continue;
        int id=0;auto parsed=std::from_chars(row.data(),row.data()+comma,id);
        if(parsed.ec==std::errc()&&parsed.ptr==row.data()+comma&&id==house){
            if(match>=0){error="The zoning table has duplicate lot entries";return false;}match=i;
        }
    }
    const auto value=prefix+(community?"Community":"Residential");
    if(match>=0)table.SetString(match,value,us);
    else {STRItem row;row.LanguageCode=1;row.Value=value;table.AppendString(row,us);}
    st::Writer writer(st::Endian::Little);
    if(!table.Write(nullptr,writer)){error="Cannot serialize the zoning table";return false;}
    raw->Data=writer.ToArray();
    if(!Transaction::Capture(UserDir,house,3,zoning.Write(),change,error))return false;
    changes.push_back(std::move(change));
    if(!Transaction::Commit(UserDir,house,changes,error))return false;
    // No progress/render callbacks between durable commit and this mirror update.
    if(family){family->Budget=int32_t(refund);family->NetWorth=0;family->HouseNumber=0;
        provider->FamilyForHouse.erase(int16_t(house));}
    provider->ZoningDictionary[int16_t(house)]=community?1:0;
    RefreshFamilies();LoadArea(CurrentArea,&_Positions);
    if(!log.empty())std::printf("%s",log.c_str());
    return true;
}

bool NhoodModel::Evict(int house) {
    auto prov = Prov();
    if (prov == nullptr) return false;
    auto fami = prov->GetFamilyForHouse((int16_t)house);
    if (fami == nullptr) return false;
    // The travelling family still owns its home; resume and return before moving it.
    if (prov->GameState.SavedVisits.count((int16_t)fami->ChunkID)) return false;
    // FAMI offset24 is ValueInArch, not total assets. Realize that stored
    // property cache exactly once (Simitone TS1GameScreen.EvictLot), then clear
    // it. Do not guess repairs from a fresh house quote or the family's cash.
    const auto refunded=std::int64_t(fami->Budget)+fami->NetWorth;
    if(fami->NetWorth<0||refunded<std::numeric_limits<int32_t>::min()||refunded>std::numeric_limits<int32_t>::max())return false;
    for (auto& l : Lots) if (l.House == house) l.Family = -1;
    fami->Budget = static_cast<int32_t>(refunded);
    fami->NetWorth = 0;
    fami->HouseNumber = 0;
    prov->FamilyForHouse.erase((int16_t)house);
    Dirty = true;
    RefreshFamilies();
    return true;
}

bool NhoodModel::DeleteFamily(int16_t family) {
    auto prov = Prov();
    if (prov == nullptr || prov->MainResource == nullptr) return false;
    auto fami = prov->GetFamily(family);
    if (fami == nullptr || fami->HouseNumber != 0 || FamilyAway(family)) return false;
    // Alex's DeleteFamily detaches neighbours before removing the family chunks.
    // VerifyFamily later reconstructs membership from this NBRS field; leaving it
    // behind would attach old Sims to the next family that reuses this chunk ID.
    if (prov->Neighbors) for (auto& entry : prov->Neighbors->Entries) {
        auto* neighbour = entry.get();
        if (neighbour && neighbour->Unknown1 == 1 &&
            neighbour->PersonData.size() > size_t(FSO::Content::TS1::TS1NeighborhoodProvider::PERSON_FAMILY_NUMBER) &&
            neighbour->PersonData[FSO::Content::TS1::TS1NeighborhoodProvider::PERSON_FAMILY_NUMBER] == family)
            neighbour->PersonData[FSO::Content::TS1::TS1NeighborhoodProvider::PERSON_FAMILY_NUMBER] = 0;
    }
    fami->FamilyGUIDs.clear();
    fami->RuntimeSubset.clear();
    if (prov->GameState.ActiveFamily == fami) prov->GameState.ActiveFamily = nullptr;
    auto fams = prov->MainResource->Get<FAMs>((uint16_t)family);
    prov->MainResource->FullRemoveChunk(fami);
    if (fams) prov->MainResource->FullRemoveChunk(fams);
    _DeletedFamilies.push_back(family);
    Dirty = true;
    RefreshFamilies();
    return true;
}

void NhoodModel::SetBudget(int16_t family, int budget) {
    auto prov = Prov();
    auto fami = prov ? prov->GetFamily(family) : nullptr;
    if (fami == nullptr || fami->Budget == budget) return;
    // Changing cash does not change the independently cached property value.
    fami->Budget = budget;
    Dirty = true;
    RefreshFamilies();
}

bool NhoodModel::Save(std::string* error) {
    auto prov = Prov();
    if (prov == nullptr || prov->MainResource == nullptr) { if (error) *error = "no neighbourhood"; return false; }
    std::string path = UserDir + "/Neighborhood.iff";
    // TS1(sims): the complete neighbourhood (neighbour records incl. person data and relationships, NGBH data
    // and inventories, type attributes, families; deleted families' FAMI/FAMs removed) through the single
    // Neighborhood.iff writer, content/TS1NeighbourhoodWriter (every other chunk byte for byte). This used to rewrite
    // only the FAMI chunks, so run-time neighbour state (trips, persistent person data, inventories) was lost.
    std::vector<std::string> log;
    if (!FSO::Content::TS1::TS1NeighbourhoodWriter::Save(prov, path, _DeletedFamilies, error, &log)) return false;
    for (auto& l : log) printf("[nhood] %s\n", l.c_str());
    _DeletedFamilies.clear();
    Dirty = false;
    RefreshFamilies(); // Include property-cache updates made by the successful home save.
    return true;
}

}}
