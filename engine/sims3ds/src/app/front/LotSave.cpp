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
#include "files/formats/iff/chunks/ARRY.h"
#include "world/World.h"
#include "common/TS1/TS1LotPictures.h"
#include "world/utils/TriangleRaster.h"
#include <cmath>
#include <stdexcept>
#include "world/model/Blueprint.h"
#include "world/components/ObjectComponent.h"
#include "world/components/TerrainComponent.h"
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

// The DVD saves independently resized full/half pairs. The second image adds
// roofs to the retained base canvas; the first inherits the live roof view.
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

using namespace FSO::Common::TS1;
static std::array<LotPictureSurface,2> RenderPicturePair(LotSession& s,
    const LotPicturePlan& requested,const std::vector<uint8_t>& heights,float scale,
    int centroidX,int centroidY,LotPictureMetadata& metadata,PictureProgress& prog) {
    auto world = s.World();
    auto ctx = s.Context();
    auto bp = s.Blueprint;
    if (!world || !ctx || !bp || !ctx->Architecture()) return {};
    ctx->PrepareVisualLighting();
    auto arch = ctx->Architecture();
    auto& st = world->State;
    const auto oldPreciseZoom=st.PreciseZoom();
    auto oldZoom = st.Zoom(); auto oldRot = st.Rotation(); auto oldLevel = st.Level(); auto oldCenter = st.CenterTile();
    auto oldCut = bp->Cutaway;
    const auto oldBuildMode = st.BuildMode();
    bool oldRoofs = st.DrawRoofs;
    auto oldOutside = bp->OutsideColor;
    const auto oldStateOutside = st.OutsideColor;
    const auto oldRenderingThumbnail = st.RenderingThumbnail;
    const auto oldThumbnailBounds = st.ThumbnailGroundBounds;
    auto oldVisualLight = bp->VisualLight;
    const auto oldTerrainPeriod=st.TerrainPeriod;
    const auto oldTerrainDetail=st.TerrainDetail;
    const auto oldSimpleTerrain=st.SimpleTerrain;
    const auto oldTerrainSourceViewport=st.TerrainSourceViewport;
    const auto oldTerrainSourceClip=st.TerrainSourceClip;
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
        bp->VisualLight=std::move(oldVisualLight);
        st.TerrainPeriod=oldTerrainPeriod;
        st.TerrainDetail=oldTerrainDetail;
        st.SimpleTerrain=oldSimpleTerrain;
        st.TerrainSourceViewport=oldTerrainSourceViewport;
        st.TerrainSourceClip=oldTerrainSourceClip;
        st._2D->AmbientLight = oldAmbientLight;
        bp->RoofComp.SetLightingAmbient(oldRoofAmbient);
        world->DrawAvatars = oldDrawAvatars;
        world->AvatarDrawHook = std::move(oldAvatarDrawHook);
        st.Zoom(oldZoom); st.Rotation(oldRot); st.Level(oldLevel); st.BuildMode(oldBuildMode); st.PreciseZoom(oldPreciseZoom);
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
    // Source picture entry scopes DAY0 and High terrain quality independently
    // of the live clock and F5 actor/roof settings. The detached snapshot leaves
    // logical room scores, events and canonical light generations untouched.
    bp->VisualLight=ctx->BuildPictureVisualLighting(0);
    st.TerrainPeriod=0;st.TerrainDetail=2;st.SimpleTerrain=false;
    const int width=int(requested.RequestedWidth/scale+.5f);
    const int height=int(requested.RequestedHeight/scale+.5f);
    if(width<=0 || height<=0)throw std::runtime_error("Invalid lot-picture canvas");
    const auto bounds=PlanLotPictures(heights,arch->Width,width,height,1,0,58);
    const int screenX=(std::clamp(requested.OffsetX,bounds.MinX,bounds.MaxX)+1)&~3;
    const int screenY=std::clamp(requested.OffsetY,bounds.MinY,bounds.MaxY);
    const int shift=scale==.25f?2:3;
    metadata={(16*(centroidX-centroidY)+screenX)>>shift,
        (8*(centroidX+centroidY)+screenY)>>shift,bounds.BaseYOff>>shift,0,0};
    st.BuildMode(0);st.PreciseZoom(1);
    st.Zoom(FSO::LotView::WorldZoom::Far);
    st.Rotation(FSO::LotView::WorldRotation::TopLeft);
    st.Level(2);st.DrawRoofs=oldRoofs;
    bp->Cutaway.assign(size_t(arch->Width)*arch->Height,false);
    st.RenderingThumbnail=true;
    bp->RoofComp.SetLightingAmbient({{0,0,0}});
    world->DrawAvatars=false;world->AvatarDrawHook={};
    // Flags bit20h is the canonical fine buildable mask. MapEdit is a UI
    // override and source picture entry explicitly clears its override.
    int rx0=arch->Width,ry0=arch->Height,rx1=-1,ry1=-1;
    for(int y=0;y<arch->Height;++y)for(int x=0;x<arch->Width;++x) {
        const bool accepted=!arch->FineBuildableArea.empty()
            ? arch->FineBuildableArea[size_t(y)*arch->Width+x]
            : arch->BuildableArea.Contains(x,y);
        if(accepted){rx0=std::min(rx0,x);ry0=std::min(ry0,y);rx1=std::max(rx1,x);ry1=std::max(ry1,y);}
    }
    if(rx1<0){rx0=ry0=0;rx1=arch->Width-1;ry1=arch->Height-1;}
    st.ThumbnailGroundBounds=st::Rectangle(rx0,ry0,rx1-rx0+1,ry1-ry0+1);
    world->TS1ScrollBounds=false;world->FreeCenter=true;
    bp->Damage.push_back(FSO::LotView::Model::BlueprintDamage(FSO::LotView::Model::BlueprintDamageType::LIGHTING_CHANGED));
    bp->Damage.push_back(FSO::LotView::Model::BlueprintDamage(FSO::LotView::Model::BlueprintDamageType::WALL_CUT_CHANGED));
    PrewarmSprites(&*bp,st,prog,1);
    std::array<LotPictureSurface,2> canvases;
    for(auto& canvas:canvases){canvas.Width=width;canvas.Height=height;canvas.Pixels.assign(size_t(width)*height,canvas.Key);}
    // Keep renderer layer buffers bounded on portable targets. Stitch raw565
    // words first; cubic support spans tile boundaries only after assembly.
    const int pw=(width+kPictureGrid-1)/kPictureGrid,ph=(height+kPictureGrid-1)/kPictureGrid;
    FSO::LotView::Framebuffer fb(pw,ph);
    auto copy=[&](LotPictureSurface& canvas,int x0,int y0) {
        for(int y=0;y<ph && y0+y<height;++y)for(int x=0;x<pw && x0+x<width;++x) {
            const size_t at=size_t(y)*pw+x;
            if(!std::isfinite(fb.Depth[at]))continue;
            const uint32_t c=fb.Color[at];
            canvas.Pixels[size_t(y0+y)*width+x0+x]=PackLotPictureRGB({{uint8_t(c),uint8_t(c>>8),uint8_t(c>>16)}},canvas.Format);
        }
    };
    for(int y0=0;y0<height;y0+=ph)for(int x0=0;x0<width;x0+=pw) {
        const float cx=pw*.5f-(screenX-x0),cy=ph*.5f-(screenY-y0);
        // At rotation0 TileProjectionOrigin = viewport/2 - projectedCenter.
        st.CenterTile(st::Vector2((cx/16+cy/8)*.5f,(cy/8-cx/16)*.5f));
        // Raster parity/clipping uses the complete offscreen source viewport,
        // while the tile framebuffer only restricts writes.
        st.TerrainSourceViewport=st::Rectangle(-x0,-y0,width,height);
        st.DrawRoofs=oldRoofs;world->Draw(fb);copy(canvases[0],x0,y0);
        // The retained source target itself is565, including before any roof
        // blending. Keep its expanded WORD values in the software framebuffer.
        for(size_t i=0;i<fb.Color.size();++i) {
            const auto c=fb.Color[i];
            const auto word=std::isfinite(fb.Depth[i])
                ? PackLotPictureRGB({{uint8_t(c),uint8_t(c>>8),uint8_t(c>>16)}},LotPictureFormat::RGB565)
                : uint16_t(0xf81f);
            const auto rgb=ExpandLotPictureRGB(word,LotPictureFormat::RGB565);
            fb.Color[i]=uint32_t(rgb[0])|(uint32_t(rgb[1])<<8)|(uint32_t(rgb[2])<<16)|0xff000000u;
        }
        // Preserve this actual base/depth canvas, then add the original roof
        // producer. No second cleared screenshot or overlay-only BMP.
        st.DrawRoofs=true;bp->RoofComp.DrawInto(st,fb);copy(canvases[1],x0,y0);
        prog.Add(1);
    }
    return {{ResizeLotPicture(canvases[0],scale,scale),ResizeLotPicture(canvases[1],scale,scale)}};
}

void RenderPictures(LotSession& s,const std::string&,VMTS1LotSaver& saver) {
    auto world=s.World();auto ctx=s.Context();if(!world||!ctx||!s.Blueprint||!ctx->Architecture())return;
    auto arch=ctx->Architecture();auto& state=world->State;
    const auto heights=s.Blueprint->Terrain->CopyOriginalHeightQuads(64,s.Blueprint);
    const int zoom=int(state.Zoom());
    const auto plan=PlanLotPictures(heights,arch->Width,int(state.WorldSpace.WorldPxWidth),
        int(state.WorldSpace.WorldPxHeight),zoom,int(state.Rotation()),58<<(zoom-1));
    int sumX=0,sumY=0,count=0;
    for(int y=0;y<arch->Height;++y)for(int x=0;x<arch->Width;++x) {
        const bool accepted=!arch->FineBuildableArea.empty()?arch->FineBuildableArea[size_t(y)*arch->Width+x]
            :arch->BuildableArea.Contains(x,y);
        if(accepted){sumX+=x;sumY+=y;++count;}
    }
    count=std::max(count,1);const int cx=int(double(sumX)/count+.5),cy=int(double(sumY)/count+.5);
    FSO::Files::LoadProgress::Step("save: house pictures");
    PictureProgress progress;progress.Total=2*(1+kPictureGrid*kPictureGrid);progress.Add(0);
    for(int half=0;half<2;++half) {
        auto requested=plan;if(half){requested.RequestedWidth/=2;requested.RequestedHeight/=2;}
        LotPictureMetadata metadata;
        const auto pair=RenderPicturePair(s,requested,heights,half?.125f:.25f,cx,cy,metadata,progress);
        const auto baseTrim=TrimLotPicture(pair[0]),roofTrim=TrimLotPicture(pair[1]);
        metadata=PlaceLotPictures(metadata,baseTrim,roofTrim);
        for(int roof=0;roof<2;++roof) {
            const auto cropped=CropLotPicture(pair[size_t(roof)],roof?roofTrim:baseTrim);
            saver.SetPicture((half?1024:512)+roof,cropped.Width,cropped.Height,LotPictureRGB(cropped));
        }
        saver.SetPictureMetadata(half?1024:512,metadata.Words());
    }
}

bool SaveLot(LotSession& session, std::string& error, std::string& log, bool departing) {
    if (session.VM() && session.VM()->HasTS1PendingRemovals()) {
        error = "Cannot save until the current object-removal dispatch has finished";
        return false;
    }
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
    if (ok) {
        // The house is committed even if a later neighbourhood update fails.
        // Rebase from the actual destination, never from BuildHouse's buffer.
        session.LoadedHousePath = dest;
        session.ArchitectureSpent = 0; session.ObjectsSpent = 0;
        auto arch = session.Context()->Architecture();
        std::vector<uint8_t> bytes, heights;
        std::vector<FSO::Content::TS1::RawIffChunk> chunks;
        bool decoded = false;
        try {
            if (FSO::Content::TS1::RawIff::ReadFile(dest, bytes) &&
                FSO::Content::TS1::RawIff::Parse(bytes, chunks)) {
                const FSO::Content::TS1::RawIffChunk* found = nullptr;
                for (const auto& chunk : chunks) if (chunk.Type == "Arry" && chunk.ID == 0) {
                    if (found) { found = nullptr; break; }
                    found = &chunk;
                }
                if (found) {
                    FSO::Files::Formats::IFF::Chunks::ARRY alt;
                    st::Reader reader(found->Data, st::Endian::Little);
                    alt.Read(nullptr, reader);
                    if (alt.Width == 64 && alt.Height == 64 &&
                        alt.Type == FSO::Files::Formats::IFF::Chunks::ARRYType::RLEAlt) {
                        heights = alt.TransposeData();
                        decoded = heights.size() == 64u * 64u * 4u;
                    }
                }
            }
        } catch (const std::exception&) { decoded = false; }
        if (arch && arch->Terrain) arch->Terrain->SetTS1HeightSource(decoded ? heights : std::vector<uint8_t>{});
        if (session.Blueprint && session.Blueprint->Terrain) {
            if (decoded) session.Blueprint->Terrain->SetOriginalHeightSource(heights);
            else session.Blueprint->Terrain->SetOriginalSource({}, {}, 0);
        }
        if (!decoded) {
            error = "House was saved, but its terrain source could not be read back";
            return false;
        }
    }
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
        FSO::Files::Formats::IFF::Chunks::SIMI simi;st::Reader reader(info->Data);simi.ReadMetadata(reader);
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
