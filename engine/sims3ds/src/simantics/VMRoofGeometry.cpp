// SPDX-License-Identifier: MPL-2.0
#include "simantics/VMContext.h"
#include "simantics/VM.h"
#include "simantics/entities/VMEntity.h"
#include "common/TS1/TS1EnvironmentGeometry.h"
#include "world/components/TerrainComponent.h"
#include <algorithm>

namespace FSO { namespace SimAntics {
namespace {
using Model::VMEnvironmentRoom;
using Model::VMStackObjectVariable;
using FSO::LotView::Model::LotTilePos;

bool RoofStair(const VMEntity* entity) {
    if (!entity) return false;
    const auto* definition = entity->MasterDefinition ? entity->MasterDefinition :
        entity->Object ? entity->Object->OBJ : nullptr;
    // DVD 0056e33b resolves the master reference before reading OBJD word69.
    return definition && definition->BuildModeType == 3;
}
bool RelevantRoofSubtree(VMEntity* entity) {
    if (!entity) return false;
    if (RoofStair(entity) || entity->GetValue(VMStackObjectVariable::Category) == 8) return true;
    for (int slot = 0; slot < entity->TotalSlots(); ++slot)
        if (RelevantRoofSubtree(entity->GetSlot(slot))) return true;
    return false;
}
struct RoofTileObjects { bool Category8 = false, Stair = false; };
void CollectRoofSubtree(VMEntity* entity, RoofTileObjects& result) {
    if (!entity) return;
    result.Stair |= RoofStair(entity);
    // DVD 00467ee7: descendants belong to the root tile's room; their own
    // Room29 is only a nonnegative guard, never a membership selector.
    result.Category8 |= entity->GetValue(VMStackObjectVariable::Category) == 8 &&
        entity->GetValue(VMStackObjectVariable::Hidden) == 0 &&
        entity->GetValue(VMStackObjectVariable::Room) >= 0;
    for (int slot = 0; slot < entity->TotalSlots(); ++slot)
        CollectRoofSubtree(entity->GetSlot(slot), result);
}
uint16_t PreviewWallStyle(const FSO::LotView::Model::WallTile& wall,
                         const FSO::LotView::Model::WallTile& saved,
                         uint16_t original, bool left) {
    const auto style = left ? wall.TopLeftStyle : wall.TopRightStyle;
    const auto oldStyle = left ? saved.TopLeftStyle : saved.TopRightStyle;
    const auto object = left ? wall.ObjSetTLStyle : wall.ObjSetTRStyle;
    const auto oldObject = left ? saved.ObjSetTLStyle : saved.ObjSetTRStyle;
    const int bit = left ? 1 : 2;
    if ((int(wall.Segments) & 48) ||
        ((int(wall.Segments) ^ int(saved.Segments)) & (bit | 48)) || style != oldStyle)
        original = style;
    if (!(int(wall.Segments) & 48) && object != oldObject) {
        uint16_t logical = object;
        if (object >= 256) {
            const auto* content = FSO::Content::Content::Get();
            const auto* source = content && content->WorldWalls ? content->WorldWalls->GetWallStyle(object) : nullptr;
            logical = source ? source->LogicalType : 0;
        }
        original = object ? logical : style;
    }
    if (!(int(wall.Segments) & (bit | 48))) original = 0;
    return original;
}

// A construction preview has display walls/floors but must not replace the
// live VM's room records. Run the same recovered flood on that temporary view.
void RoofPreviewRooms(const VMContext& context, std::vector<VMEnvironmentRoom>& rooms,
                      std::vector<std::vector<uint32_t>>& maps,
                      std::vector<std::vector<uint8_t>>& floorPresent) {
    using namespace FSO::Common::TS1;
    const auto* arch = context.Architecture();
    const auto* blueprint = context.Blueprint;
    const size_t count = size_t(arch->Width) * arch->Height;
    const int stories = std::min(2,arch->Stories);
    rooms.emplace_back(); maps.resize(size_t(stories)); floorPresent.resize(size_t(stories));
    std::vector<EnvironmentGeometryTile> tiles(count);
    for (int story = 0; story < stories; ++story) {
        auto& present = floorPresent[size_t(story)]; present.resize(count);
        const auto& walls = (*blueprint->Walls)[size_t(story)];
        const auto& floors = (*blueprint->Floors)[size_t(story)];
        for (size_t i = 0; i < count; ++i) {
            const auto& wall = walls[i]; auto& tile = tiles[i];
            tile.Segments = uint8_t(wall.Segments);
            const auto left = arch->TS1WallStyleTopLeft[size_t(story)].size() == count ?
                arch->TS1WallStyleTopLeft[size_t(story)][i] : arch->Walls[size_t(story)][i].TopLeftStyle;
            const auto right = arch->TS1WallStyleTopRight[size_t(story)].size() == count ?
                arch->TS1WallStyleTopRight[size_t(story)][i] : arch->Walls[size_t(story)][i].TopRightStyle;
            tile.TopLeftStyle = PreviewWallStyle(wall,arch->Walls[size_t(story)][i],left,true);
            tile.TopRightStyle = PreviewWallStyle(wall,arch->Walls[size_t(story)][i],right,false);
            tile.TopLeftPattern = wall.TopLeftPattern; tile.TopRightPattern = wall.TopRightPattern;
            tile.BottomLeftPattern = wall.BottomLeftPattern; tile.BottomRightPattern = wall.BottomRightPattern;
            bool floor = arch->TS1FloorPresent[size_t(story)].size() == count ?
                arch->TS1FloorPresent[size_t(story)][i] != 0 : arch->Floors[size_t(story)][i].Pattern != 0;
            if (floors[i].Pattern != arch->Floors[size_t(story)][i].Pattern && floors[i].Pattern < 65534)
                floor = floors[i].Pattern != 0;
            tile.Floor = floor; present[i] = uint8_t(floor);
            tile.Pool = arch->TS1PoolTiles.size() == count ? arch->TS1PoolTiles[i] != 0 :
                arch->Floors[0][i].Pattern == 65535;
        }
        auto geometry = BuildEnvironmentGeometry(tiles,arch->Width,arch->Height,story+1);
        const uint32_t base = uint32_t(rooms.size());
        auto& map = maps[size_t(story)]; map.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const auto low = uint16_t(geometry.Map[i]), high = uint16_t(geometry.Map[i] >> 16);
            map[i] = (low + base) | ((high + base) << 16);
        }
        for (size_t local = 0; local < geometry.Rooms.size(); ++local) {
            const auto& source = geometry.Rooms[local]; rooms.emplace_back(); auto& target = rooms.back();
            target.SpatialTileOrder = source.Visits;
            target.Outside = source.Outside;
            target.CanonicalID = CanonicalRoomID(story+1,unsigned(local)); target.Story = uint8_t(story+1);
        }
    }
}
}

void VMContext::InvalidateRoofObject(VMEntity* entity) {
    if (RoofAdmissionDirty || !VM || !VM->TS1 || !Blueprint || !Architecture() || !entity) return;
    if (RelevantRoofSubtree(entity)) { RoofAdmissionDirty = true; return; }
    auto* root = entity;
    while (root->Container) root = root->Container;
    const auto pos = root->Position();
    const auto* arch = Architecture();
    if (pos.Level == 2 && pos.TileX() >= 0 && pos.TileY() >= 0 &&
        pos.TileX() < arch->Width && pos.TileY() < arch->Height) {
        const auto tile = size_t(pos.TileY()) * arch->Width + pos.TileX();
        if (tile < RoofGroundStairs.size() && RoofGroundStairs[tile]) {
            const auto& upper = SpatialTileHeads[1];
            const auto& floors = arch->TS1FloorPresent[1];
            const bool floor = tile < floors.size() ? floors[tile] != 0 : arch->Floors[1][tile].Pattern != 0;
            const bool blocked = floor || (tile < upper.size() && upper[tile] != 0);
            for (const auto& input : Blueprint->RoofComp.GetSourceAdmissions()) if (input.Story == 1) {
                const auto half = size_t(pos.TileY()*2)*128+pos.TileX()*2;
                // Only the empty/nonempty guard above a stair changes roof
                // admission. Movement on an already blocked tile needs no scan.
                if (half < input.Blocked.size() && bool(input.Blocked[half]) != blocked)
                    RoofAdmissionDirty = true;
                break;
            }
        }
    }
}

void VMContext::PrepareRoofGeometry() {
    using namespace FSO::Common::TS1;
    auto* arch = Architecture();
    if (!VM || !VM->TS1 || !Blueprint || !Blueprint->Terrain || !arch || EnvironmentMaps.empty()) return;
    const auto heightVersion = Blueprint->Terrain->OriginalHeightGeneration();
    if (!RoofAdmissionDirty && RoofAdmissionGeometryVersion == VisualGeometryVersion &&
        RoofAdmissionHeightVersion == heightVersion) return;
    const int width = arch->Width, height = arch->Height;
    const size_t count = size_t(width) * height;
    const auto heights = Blueprint->Terrain->CopyOriginalHeightQuads(64,Blueprint);
    const bool preview = Blueprint->Walls != &arch->Walls || Blueprint->Floors != &arch->Floors;
    std::vector<VMEnvironmentRoom> previewRooms;
    std::vector<std::vector<uint32_t>> previewMaps;
    std::vector<std::vector<uint8_t>> previewFloorPresent;
    if (preview) RoofPreviewRooms(*this,previewRooms,previewMaps,previewFloorPresent);
    const auto& rooms = preview ? previewRooms : EnvironmentRooms;
    const auto& maps = preview ? previewMaps : EnvironmentMaps;
    std::vector<std::vector<RoofTileObjects>> objects(size_t(2),std::vector<RoofTileObjects>(count));
    for (int story = 1; story <= 2; ++story) for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto tile = size_t(y)*width+x;
        const auto& heads = SpatialTileHeads[size_t(story-1)];
        if (tile < heads.size() && heads[tile])
            CollectRoofSubtree(VM->GetObjectById(heads[tile]),objects[size_t(story-1)][tile]);
    }
    std::vector<uint8_t> eligibleRooms(rooms.size());
    for (size_t i = 1; i < rooms.size(); ++i) {
        const auto& room = rooms[i]; bool flat = true, category8 = false;
        for (const auto tile : room.SpatialTileOrder) {
            const int x = tile % width, y = tile / width;
            if (x < 0 || y < 0 || x >= 64 || y >= 64) { flat = false; break; }
            const size_t at = (size_t(y)*64+x)*4;
            flat &= heights[at] == heights[at+1] && heights[at] == heights[at+2] && heights[at] == heights[at+3];
            category8 |= objects[size_t(room.Story-1)][size_t(tile)].Category8;
        }
        const bool room74 = category8 && (room.CanonicalID & 0x3ff) != 0;
        eligibleRooms[i] = flat && (room74 || !room.Outside);
    }
    std::vector<RoofAdmissionLevel> levels;
    std::vector<uint8_t> stairs(count);
    for (size_t i = 0; i < count; ++i) stairs[i] = uint8_t(objects[0][i].Stair);
    for (int story = 1; story <= std::min(2,arch->Stories); ++story) {
        RoofAdmissionLevel level; level.Story = story; level.Width = 128;
        level.Eligible.resize(128*128); level.Blocked.resize(128*128);
        if (size_t(story) <= maps.size()) for (int y = 0; y < std::min(64,height); ++y)
            for (int x = 0; x < std::min(64,width); ++x) {
                const size_t tile = size_t(y)*width+x;
                const auto roomPair = maps[size_t(story-1)][tile];
                const auto low = uint16_t(roomPair), high = uint16_t(roomPair >> 16);
                bool eligible = (low < eligibleRooms.size() && eligibleRooms[low]) ||
                    (high < eligibleRooms.size() && eligibleRooms[high]);
                bool blocked = false;
                if (story == 1 && arch->Stories >= 2) {
                    const auto& present = preview ? previewFloorPresent[1] : arch->TS1FloorPresent[1];
                    const bool floor = present.size() == count ? present[tile] != 0 : arch->Floors[1][tile].Pattern != 0;
                    const bool occupied = SpatialTileHeads[1].size() == count && SpatialTileHeads[1][tile] != 0;
                    blocked = floor || (occupied && stairs[tile]);
                    eligible &= !blocked;
                }
                for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
                    const size_t at = size_t(y*2+dy)*128+x*2+dx;
                    level.Eligible[at] = uint8_t(eligible); level.Blocked[at] = uint8_t(blocked);
                }
            }
        levels.push_back(std::move(level));
    }
    Blueprint->RoofComp.SetSourceAdmission(levels,heights,64);
    RoofGroundStairs.swap(stairs);
    RoofAdmissionGeometryVersion = VisualGeometryVersion;
    RoofAdmissionHeightVersion = heightVersion;
    RoofAdmissionDirty = false;
    ++RoofAdmissionPublishes;
}
}}
