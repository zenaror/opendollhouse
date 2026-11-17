#include "simantics/utils/VMLotDemolitionScope.h"
/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// TS1(sims): saving a lot from the app; see LotSave.h.
#include "app/front/LotSave.h"
#include "files/LoadProgress.h"
#include "app/LotSession.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/utils/VMTS1LotSaver.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "app/front/UserDataSetup.h"
#include "files/SafeFile.h"
#include "content/TS1CharacterFiles.h"
#include "files/formats/iff/chunks/SIMI.h"
#include "world/World.h"
#include "world/model/Blueprint.h"
#include "world/components/ObjectComponent.h"
#include <algorithm>
#include <limits>
#include "simantics/VMClock.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "files/formats/iff/chunks/OBJD.h"
#include <memory>
#include <set>
#include <vector>

namespace simapp { namespace front {

/// FIX(sims) FB1: the neighbourhood pictures as the game saves them. BMP_ 512 = the whole lot with walls up and no roof,
/// BMP_ 513 = the same with the roof (the map shows 513, the hovered/selected house 512), magenta (248,0,248) around
/// the lot, daylight, every story, and the lot's centre at the THMB 512 anchor (x, y) so the map's anchoring (THMB +
/// LotLocations, see NhoodModel::LoadArea) stays right. Before, only 512 was written: a night-time rectangle of the
/// view centred on the walls, with no transparency, so a saved house covered the roads around it on the map.
// ADDED(sims) save progress: the pictures report their work (files/LoadProgress.h "save: house pictures"), so the
// "Saving..." overlay redraws and the sound keeps playing in between. Measured with the host's 3DS estimate (House05,
// Goths): the first far-zoom draw read ~6.5 MB of object sprites (~0.6 s of SD time inside one World::Draw), and each
// of the 2x2 passes was 0.25-0.4 s of CPU. Now the sprites are loaded object by object first (PrewarmSprites), a
// 16x16 draw takes the zoom / level / light changes, and the 4x canvas is drawn in 4x4 passes (0.1-0.15 s each, the
// same total): no stretch without a report is longer than ~0.18 s.
static const int kPictureGrid = 4;
struct PictureProgress {
    double Units = 0, Total = 0;
    void Add(double u) {
        Units += u;
        FSO::Files::LoadProgress::Progress("save: house pictures", (size_t)(Units * 100.0), (size_t)(Total * 100.0));
    }
};

/// Loads the sprites every object needs at the pictures' zoom (World::Draw would load them all at once).
static void PrewarmSprites(FSO::LotView::Model::Blueprint* bp, FSO::LotView::WorldState& st, PictureProgress& prog, double units) {
    auto objs = bp->Objects; // a copy: nothing may change the list while the sprites load, but be safe
    for (size_t i = 0; i < objs.size(); i++) {
        if (objs[i] == nullptr) continue;
        objs[i]->ValidateSprite(st);
        if ((i & 3) == 3) prog.Add(units * 4 / (double)objs.size());
    }
    prog.Add(units * (double)(objs.size() & 3) / (double)std::max<size_t>(1, objs.size()));
}

static void RenderThumbnail(LotSession& s, int w, int h, int ax, int ay, bool roofs, std::vector<uint8_t>& rgb, PictureProgress& prog) {
    auto world = s.World();
    auto ctx = s.Context();
    auto bp = s.Blueprint;
    if (!world || !ctx || !bp || !ctx->Architecture() || w <= 0 || h <= 0) return;
    ctx->PrepareVisualLighting();
    auto arch = ctx->Architecture();
    auto& st = world->State;
    auto oldZoom = st.Zoom(); auto oldRot = st.Rotation(); auto oldLevel = st.Level(); auto oldCenter = st.CenterTile();
    auto oldCut = bp->Cutaway;
    const auto oldBuildMode = st.BuildMode();
    bool oldRoofs = st.DrawRoofs;
    auto oldOutside = bp->OutsideColor;
    const auto oldStateOutside = st.OutsideColor;
    const auto oldRenderingThumbnail = st.RenderingThumbnail;
    const auto oldThumbnailBounds = st.ThumbnailGroundBounds;
    const auto oldAmbientLight = st._2D->AmbientLight;
    const auto oldRoofAmbient = bp->RoofComp.GetLighting().Ambient;
    const auto oldDrawAvatars = world->DrawAvatars;
    auto oldAvatarDrawHook = world->AvatarDrawHook;
    const bool oldBounds = world->TS1ScrollBounds, oldFreeCenter = world->FreeCenter;
    const st::Vector2 oldDimensions(st.WorldSpace.WorldPxWidth, st.WorldSpace.WorldPxHeight);
    // Thumbnail rendering temporarily changes the live world. Restore it on
    // success and on sprite/progress/storage exceptions alike.
    struct RestoreView {
        std::function<void()> Restore;
        ~RestoreView() noexcept { try { Restore(); } catch (...) {} }
    } restoreView{[&] {
        st.RenderingThumbnail = oldRenderingThumbnail;
        st.ThumbnailGroundBounds = oldThumbnailBounds;
        st._2D->AmbientLight = oldAmbientLight;
        bp->RoofComp.SetLightingAmbient(oldRoofAmbient);
        world->DrawAvatars = oldDrawAvatars;
        world->AvatarDrawHook = std::move(oldAvatarDrawHook);
        st.Zoom(oldZoom); st.Rotation(oldRot); st.Level(oldLevel); st.BuildMode(oldBuildMode);
        st.SetDimensions(oldDimensions);
        st.CenterTile(oldCenter);
        st.DrawRoofs = oldRoofs;
        world->TS1ScrollBounds = oldBounds;
        world->FreeCenter = oldFreeCenter;
        bp->Cutaway.swap(oldCut);
        bp->OutsideColor = oldOutside;
        st.OutsideColor = oldStateOutside;
        world->InvalidateScroll();
        bp->Damage.push_back(FSO::LotView::Model::BlueprintDamage(FSO::LotView::Model::BlueprintDamageType::LIGHTING_CHANGED));
        bp->Damage.push_back(FSO::LotView::Model::BlueprintDamage(FSO::LotView::Model::BlueprintDamageType::FLOOR_CHANGED));
        bp->Damage.push_back(FSO::LotView::Model::BlueprintDamage(FSO::LotView::Model::BlueprintDamageType::PRECISE_ZOOM));
        bp->Damage.push_back(FSO::LotView::Model::BlueprintDamage(FSO::LotView::Model::BlueprintDamageType::WALL_CUT_CHANGED));
    }};
    int top = 1;
    for (int l = 1; l <= (int)arch->Floors.size(); l++) {
        bool any = false;
        for (auto& f : arch->Floors[(size_t)(l - 1)]) if (f.Pattern != 0) { any = true; break; }
        if (!any && l - 1 < (int)arch->Walls.size()) for (auto& wt : arch->Walls[(size_t)(l - 1)]) if (wt.Segments != 0) { any = true; break; }
        if (any) top = std::max(top, l);
    }
    st.BuildMode(0); // A saved house picture never includes interactive build/buy grid.
    st.Zoom(FSO::LotView::WorldZoom::Far);
    st.Rotation(FSO::LotView::WorldRotation::TopLeft);
    st.Level((int8_t)top);
    st.DrawRoofs = roofs;
    st.CenterTile(st::Vector2(arch->Width / 2.0f, arch->Height / 2.0f));
    bp->Cutaway.assign((size_t)(arch->Width * arch->Height), false);
    st.RenderingThumbnail = true;
    // The DVD picture entry and actual cached roof mesh both inherit ambient0.
    // Reuse the canonical factory: new picture-created kinds must survive the
    // return, while existing per-kind material modes remain unchanged.
    bp->RoofComp.SetLightingAmbient({{0,0,0}});
    world->DrawAvatars = false; // GetLotThumb renders the lot, not live actors.
    world->AvatarDrawHook = {};
    // Neutral capture lighting is a renderer input, never a simulation-time edit.
    bp->Damage.push_back(FSO::LotView::Model::BlueprintDamage(FSO::LotView::Model::BlueprintDamageType::LIGHTING_CHANGED));
    bp->Damage.push_back(FSO::LotView::Model::BlueprintDamage(FSO::LotView::Model::BlueprintDamageType::WALL_CUT_CHANGED));
    // The game's pictures are a quarter of this renderer's far zoom (a 4x4 average). The view is centred on the lot's
    // centre, which lands on the anchor (ax, ay). To keep the world's layer caches small (3DS memory) the 4x canvas is
    // drawn as 2x2 passes of twice the picture's size, moving the view between passes.
    const int S = 4;
    int hw = std::max(ax, w - ax), hh = std::max(ay, h - ay);
    int bw = hw * 2, bh = hh * 2, ox = hw - ax, oy = hh - ay;  // box symmetric around the anchor, picture inside it
    const int G = kPictureGrid; // ADDED(sims) save progress: was 2 (see PictureProgress)
    int NW = bw * S, NH = bh * S;
    // ADDED(sims): the passes' sizes are multiples of S, so every pass starts on a picture pixel (its S x S samples)
    int PW = ((NW + G - 1) / G + S - 1) / S * S, PH = ((NH + G - 1) / G + S - 1) / S * S;
    // the lot proper (the tiles the player may build on; the street and sidewalk around it are not in the picture)
    int rx0 = arch->Width, ry0 = arch->Height, rx1 = -1, ry1 = -1;
    for (int y = 0; y < arch->Height; y++) for (int x = 0; x < arch->Width; x++)
        if (!ctx->IsUserOutOfBounds(FSO::LotView::Model::LotTilePos::FromBigTile((int16_t)x, (int16_t)y, 1))) {
            rx0 = std::min(rx0, x); ry0 = std::min(ry0, y); rx1 = std::max(rx1, x); ry1 = std::max(ry1, y);
        }
    if (rx1 < 0) { rx0 = 0; ry0 = 0; rx1 = arch->Width - 1; ry1 = arch->Height - 1; }
    st.ThumbnailGroundBounds = st::Rectangle(rx0, ry0, rx1-rx0+1, ry1-ry0+1);
    st::Vector2 C((rx0 + rx1 + 1) / 2.0f, (ry0 + ry1 + 1) / 2.0f);
    world->TS1ScrollBounds = false;
    world->FreeCenter = true; // ADDED(sims): the outer passes' centres lie off the lot (World::PreDraw kept them on it)
    std::vector<uint32_t> acc((size_t)w * h * 4, 0); // r, g, b, samples
    FSO::LotView::Framebuffer fb(PW, PH);
    st.CenterTile(C);
    PrewarmSprites(&*bp, st, prog, 0.5); // ADDED(sims) save progress
    { // ADDED(sims) save progress: a 16x16 draw takes the zoom / level / light / cut changes (terrain, walls, lights),
      // so the first pass only renders its pixels like the others
        FSO::LotView::Framebuffer tiny(16, 16);
        world->Draw(tiny);
        prog.Add(0.5);
    }
    // ADDED(sims) save progress: the view takes the passes' size here, as World::Draw does for a new target size; a
    // whole draw of the passes' size did it before (an extra pass, ~0.2-0.3 s on the 3DS, its pixels unused)
    if ((int)st.WorldSpace.WorldPxWidth != PW || (int)st.WorldSpace.WorldPxHeight != PH) {
        st.SetDimensions(st::Vector2((float)PW, (float)PH));
        world->InvalidateScroll();
        bp->Damage.push_back(FSO::LotView::Model::BlueprintDamage(FSO::LotView::Model::BlueprintDamageType::PRECISE_ZOOM));
    }
    auto& ws = st.WorldSpace;
    // Ground projection is used only to align passes, never to clip the
    // completed image: elevated object silhouettes may exceed the footprint.
    const float wallH = 57.0f * (top - 1);
    auto ground = [&](const st::Vector2& p) { return ws.GetTileAtPosWithScroll(st::Vector2(p.X, p.Y - wallH)); };
    for (int pj = 0; pj < G; pj++) for (int pi = 0; pi < G; pi++) {
        st.CenterTile(C);
        float wh = ws.TilePxWidthHalf > 0 ? ws.TilePxWidthHalf : 1, th = ws.TilePxHeightHalf > 0 ? ws.TilePxHeightHalf : 1;
        // where the lot centre is on screen now (the inverse of the picking transform: level and terrain height included)
        st::Vector2 at(PW / 2.0f, PH / 2.0f);
        for (int it = 0; it < 6; it++) {
            st::Vector2 t = ground(at);
            float ex = C.X - t.X, ey = C.Y - t.Y;
            at = at + st::Vector2((ex - ey) * wh, (ex + ey) * th);
        }
        float dx = (NW / 2.0f - pi * PW) - at.X, dy = (NH / 2.0f - pj * PH) - at.Y;   // where it has to be
        st.CenterTile(st::Vector2(C.X - (dx / wh + dy / th) / 2.0f, C.Y - (dy / th - dx / wh) / 2.0f));
        world->Draw(fb);
        prog.Add(1); // ADDED(sims)
        for (int y = 0; y < PH; y++) {
            int py = (pj * PH + y) / S - oy;
            if (py < 0 || py >= h) continue;
            for (int x = 0; x < PW; x++) {
                int px = (pi * PW + x) / S - ox;
                if (px < 0 || px >= w) continue;
                size_t i = (size_t)y * fb.Width + x;
                if (!(fb.Depth[i] < std::numeric_limits<float>::infinity())) continue; // nothing drawn: outside the lot
                uint32_t c = fb.Color[i];
                uint32_t* a = &acc[((size_t)py * w + px) * 4];
                a[0] += c & 0xFF; a[1] += (c >> 8) & 0xFF; a[2] += (c >> 16) & 0xFF; a[3]++;
            }
        }
    }
    rgb.assign((size_t)w * h * 3, 0);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint32_t* a = &acc[i * 4];
        uint8_t* p = &rgb[i * 3];
        if (a[3] < (uint32_t)(S * S / 2)) { p[0] = 248; p[1] = 0; p[2] = 248; continue; } // outside the lot: magenta
        p[0] = (uint8_t)(a[0] / a[3]); p[1] = (uint8_t)(a[1] / a[3]); p[2] = (uint8_t)(a[2] / a[3]);
        if (p[0] == 248 && p[1] == 0 && p[2] == 248) p[1] = 1; // never the key colour inside the lot
    }

}

/// FIX(sims) FB1: both neighbourhood pictures (+ the half-size copies the expansion packs read) into the saver.
static void RenderPictures(LotSession& s, const std::string& source, VMTS1LotSaver& saver) {
    FSO::Files::LoadProgress::Step("save: house pictures"); // ADDED(sims) save progress
    int32_t tw, th, ax, ay, openYOff;
    VMTS1LotSaver::ThumbnailSizeOf(source, tw, th, &ax, &ay, &openYOff);
    PictureProgress prog; // per picture: sprites 1 unit, one per pass
    prog.Total = 2.0 * (1 + kPictureGrid * kPictureGrid);
    prog.Add(0);
    for (int roofs = 0; roofs < 2; roofs++) {
        std::vector<uint8_t> rgb;
        RenderThumbnail(s, tw, th, ax, ay + (roofs ? 0 : openYOff), roofs != 0, rgb, prog);
        if (!rgb.empty()) saver.SetPicture(512 + roofs, tw, th, rgb);
    }
}

bool SaveLot(LotSession& session, std::string& error, std::string& log, bool departing) {
    FSO::Files::LoadProgress::Step("save: house " + std::to_string(session.HouseNumber)); // TS1(sims) hardware pass
    auto content = FSO::Content::Content::Get();
    auto hood = content ? content->Neighborhood.get() : nullptr;
    if (!session.VM() || hood == nullptr) { error = "no lot"; return false; }
    std::string source = session.LoadedHousePath.empty() ? hood->GetHousePath(session.HouseNumber) : session.LoadedHousePath;
    std::string dest = VMTS1LotSaver::UserHousePath((int16_t)session.HouseNumber);
    auto& gs=hood->GameState;
    auto family=gs.ActiveFamily;
    const bool visiting=family && gs.VisitingHouse==session.HouseNumber && family->HouseNumber!=session.HouseNumber;
    FSO::Content::TS1::TS1SavedVisit visit;
    if(visiting) {
        visit.Home=gs.ReturnHouse;visit.House=gs.VisitingHouse;visit.Transit=gs.LotTransitInfo;
        visit.Caller=gs.DowntownSimGUID;visit.Day=gs.TravelDay;visit.Month=gs.TravelMonth;visit.Year=gs.TravelYear;
        visit.Party=family->RuntimeSubset;
        if(visit.Party.empty() || visit.Party.size()>8 || std::find(visit.Party.begin(),visit.Party.end(),visit.Caller)==visit.Party.end()) {
            error="Cannot save a visit with an invalid travelling party";return false;
        }
        std::set<uint32_t> distinct;
        for(auto guid:visit.Party) {
            int live=0;
            for(auto e:session.VM()->Entities) {
                auto a=dynamic_cast<FSO::SimAntics::VMAvatar*>(e);
                if(a && !a->Dead && a->Object->OBJ->GUID==guid) ++live;
            }
            if(!distinct.insert(guid).second || live!=1 ||
               std::find(family->FamilyGUIDs.begin(),family->FamilyGUIDs.end(),guid)==family->FamilyGUIDs.end()) {
                error="Cannot save a visit until every travelling family member is present exactly once";return false;
            }
        }
        auto old=gs.SavedVisits.find((int16_t)family->ChunkID);
        visit.Generation=old==gs.SavedVisits.end()?1:old->second.Generation+1;
        while(visit.Generation && FSO::Files::SafeFile::Exists(visit.SnapshotPath(hood->UserPath,(int16_t)family->ChunkID))) ++visit.Generation;
        if(!visit.Generation) {error="Visit snapshot generation exhausted";return false;}
        dest=visit.SnapshotPath(hood->UserPath,(int16_t)family->ChunkID);
        UserDataSetup::MakeDirs(dest.substr(0,dest.find_last_of('/')));
    }
    VMTS1LotSaver saver(session.VM());
    saver.Departing = departing;
    RenderPictures(session, source, saver); // FIX(sims) FB1
    saver.ArchitectureDelta = session.ArchitectureSpent; // TS1(sims): the house price follows the building
    saver.ObjectsDelta = session.ObjectsSpent;
    FSO::Files::LoadProgress::Step("save: house file"); // ADDED(sims) save progress (VMTS1LotSaver reports inside)
    bool ok = saver.SaveHouse(source, dest);
    if (ok) { session.ArchitectureSpent = 0; session.ObjectsSpent = 0; }
    if (ok) ok = saver.UpdateNeighbourhood();
    // Only a successful save of this family's actual home can refresh its
    // FAMI ValueInArch cache. A travel snapshot/destination never values home.
    // Read the just-written bytes, not a potentially cached provider IFF.
    if(ok && family && !visiting && family->HouseNumber==session.HouseNumber){
        std::vector<uint8_t> bytes;std::vector<FSO::Content::TS1::RawIffChunk> chunks;
        if(!FSO::Content::TS1::RawIff::ReadFile(dest,bytes)||!FSO::Content::TS1::RawIff::Parse(bytes,chunks)){
            error="Cannot read the saved home property value";return false;
        }
        const FSO::Content::TS1::RawIffChunk* info=nullptr;
        for(const auto& chunk:chunks)if(chunk.Type=="SIMI"&&chunk.ID==1){if(info){error="Saved home has ambiguous SIMI";return false;}info=&chunk;}
        if(!info){error="Saved home has no SIMI property value";return false;}
        FSO::Files::Formats::IFF::Chunks::SIMI simi;st::Reader reader(info->Data);simi.Read(nullptr,reader);
        const int value=simi.PurchaseValue();
        if(value<0){error="Saved home has a negative property value";return false;}
        family->NetWorth=value; // Legacy C++ member name; raw FAMI offset24 excludes cash.
        log+="[save] home property cache "+std::to_string(session.HouseNumber)+" = "+std::to_string(value)+"\n";
    }
    if(ok && family) {
        if(visiting) {gs.SavedVisits[(int16_t)family->ChunkID]=visit;session.LoadedHousePath=dest;}
        else if(family->HouseNumber==session.HouseNumber) gs.SavedVisits.erase((int16_t)family->ChunkID);
    }
    for (auto& l : saver.Log) log += "[save] " + l + "\n";
    if (!ok) error = saver.Error;
    return ok;
}


static bool BuildBulldozedLot(int houseNumber, std::vector<uint8_t>* prepared, std::string& error, std::string& log) {
    std::unique_ptr<LotSession> s(new LotSession());
    if (!(prepared ? s->LoadForDemolition(houseNumber, error) : s->Load(houseNumber, 264, 144, 0, error))) return false;
    if (!s->Family.empty()) { error = "the lot is occupied"; return false; }
    auto vm = s->VM();
    FSO::SimAntics::Utils::VMLotDemolitionScope preparation(prepared ? vm : nullptr);
    auto ctx = s->Context();
    auto arch = ctx->Architecture();
    // objects: everything placed on the buildable area goes, except the mailbox and the outdoor trash can
    auto keep = [](FSO::SimAntics::VMEntity* e) {
        auto obj = e->Object ? e->Object->OBJ : nullptr;
        std::string n = obj ? obj->ChunkLabel : std::string();
        for (auto& ch : n) ch = (char)tolower((unsigned char)ch);
        return n.find("mailbox") != std::string::npos || n.find("mail box") != std::string::npos ||
               (n.find("trash") != std::string::npos && n.find("outside") != std::string::npos);
    };
    std::set<FSO::SimAntics::Entities::VMMultitileGroup*> doomed;
    int kept = 0;
    for (auto e : vm->Entities) {
        auto go = dynamic_cast<FSO::SimAntics::VMGameObject*>(e);
        if (go == nullptr || go->MultitileGroup == nullptr) continue;
        auto pos = go->Position();
        if (pos == FSO::LotView::Model::LotTilePos::OUT_OF_WORLD) continue; // controllers
        if (ctx->IsUserOutOfBounds(pos)) continue;                        // street, sidewalk
        if (keep(go)) { kept++; continue; }
        doomed.insert(go->MultitileGroup);
    }
    for (auto g : doomed) g->Delete(ctx);
    // architecture of the buildable area (FreeSO TS1 empty lots: no walls, floors or pools inside the lot)
    for (int lvl = 0; lvl < (int)arch->Walls.size(); lvl++) {
        for (int y = 0; y < arch->Height; y++) for (int x = 0; x < arch->Width; x++) {
            if (arch->OutsideClip((int16_t)x, (int16_t)y, (int8_t)(lvl + 1))) continue;
            size_t i = (size_t)(y * arch->Width + x);
            arch->Walls[(size_t)lvl][i] = FSO::LotView::Model::WallTile();
            arch->Floors[(size_t)lvl][i] = FSO::LotView::Model::FloorTile();
            if (i < arch->TS1FloorPresent[lvl].size()) arch->TS1FloorPresent[lvl][i] = 0;
            if (i < arch->TS1WallStyleTopLeft[lvl].size()) arch->TS1WallStyleTopLeft[lvl][i] = 0;
            if (i < arch->TS1WallStyleTopRight[lvl].size()) arch->TS1WallStyleTopRight[lvl][i] = 0;
            if (lvl == 0 && i < arch->TS1PoolTiles.size()) arch->TS1PoolTiles[i] = 0;
            if (lvl == 0 && vm->Context->Blueprint && i < vm->Context->Blueprint->TerrainPoolBlocked.size())
                vm->Context->Blueprint->TerrainPoolBlocked[i] = 0;
        }
    }
    arch->RegenWallsAt();
    arch->SignalRedraw();
    arch->Tick();
    if (!prepared) s->Update(2);

    auto content = FSO::Content::Content::Get();
    auto hood = content ? content->Neighborhood.get() : nullptr;
    if (hood == nullptr) { error = "no neighbourhood"; return false; }
    std::string source = hood->GetHousePath(houseNumber);
    std::string dest = VMTS1LotSaver::UserHousePath((int16_t)houseNumber);
    VMTS1LotSaver saver(vm);
    RenderPictures(*s, source, saver); // FIX(sims) FB1
    saver.SetArchitectureValue = 0;
    int objects = 0;
    for (auto e : vm->Entities) {
        auto go = dynamic_cast<FSO::SimAntics::VMGameObject*>(e);
        if (go == nullptr || go->MultitileGroup == nullptr || go->MultitileGroup->BaseObject() != go) continue;
        if (go->Position() == FSO::LotView::Model::LotTilePos::OUT_OF_WORLD || ctx->IsUserOutOfBounds(go->Position())) continue;
        objects += std::max<int>(0, go->GetValue(FSO::SimAntics::Model::VMStackObjectVariable::CurrentValue));
    }
    saver.SetObjectsValue = objects;
    bool ok = prepared ? saver.BuildHouse(source, *prepared) : saver.SaveHouse(source, dest);
    for (auto& l : saver.Log) log += "[bulldoze] " + l + "\n";
    log += "[bulldoze] house " + std::to_string(houseNumber) + ": " + std::to_string(doomed.size()) + " objects removed, " + std::to_string(kept) + " kept\n";
    if (!ok) error = saver.Error;
    return ok;
}

bool BulldozeLot(int houseNumber, std::string& error, std::string& log) {
    return BuildBulldozedLot(houseNumber, nullptr, error, log);
}
bool PrepareBulldozedLot(int houseNumber, std::vector<uint8_t>& bytes, std::string& error, std::string& log) {
    bytes.clear();
    return BuildBulldozedLot(houseNumber, &bytes, error, log);
}

}}
