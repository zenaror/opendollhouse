/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.LotView.World2D (World/World2D.cs)
#include "world/World2D.h"
#include "world/WorldState.h"
#include "world/model/Blueprint.h"
#include "world/utils/_2DWorldBatch.h"
#include "content/ContentManager.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <limits>
#include <cstring>
#include <algorithm>

namespace FSO { namespace LotView {

using namespace FSO::LotView::Model;
using namespace FSO::LotView::Components;

static WorldObjectRenderInfo* GetRenderInfo(WorldComponent* component) {
    auto obj = dynamic_cast<ObjectComponent*>(component);
    return (obj == nullptr) ? nullptr : &obj->renderInfo;
}

bool World2D::ObjectView::operator==(const ObjectView& o) const {
    return Object == o.Object && Graphic == o.Graphic && Definition == o.Definition && Position == o.Position &&
        Bounds.X == o.Bounds.X && Bounds.Y == o.Bounds.Y && Bounds.Width == o.Bounds.Width &&
        Bounds.Height == o.Bounds.Height && Flags == o.Flags && Flags2 == o.Flags2 &&
        Identity == o.Identity && ObjectID == o.ObjectID && Room == o.Room && Level == o.Level &&
        Direction == o.Direction && Visible == o.Visible && Hidden == o.Hidden &&
        Rejected == o.Rejected && Partial == o.Partial && Eligible == o.Eligible;
}

bool World2D::CaptureObjects(WorldState& state, bool& memberChanged) {
    const auto bits = [](float value) { uint32_t result; std::memcpy(&result, &value, 4); return uint64_t(result); };
    const auto& bounds = state.ThumbnailGroundBounds;
    FrameView = {{uint64_t(state.DrawOOB) | (uint64_t(state.RenderingThumbnail) << 1) |
        (uint64_t(state.SimpleTerrain) << 2) | (uint64_t(state.DrawRoofs) << 3) |
        (uint64_t(state.DynamicCutaway) << 4), uint64_t(bounds.X), uint64_t(bounds.Y),
        uint64_t(bounds.Width), uint64_t(bounds.Height), uint64_t(state.Zoom()), uint64_t(state.Rotation()),
        uint64_t(state.Level()), uint64_t(state.BuildMode()), bits(state.PreciseZoom()),
        bits(state.WorldSpace.WorldPxWidth), bits(state.WorldSpace.WorldPxHeight),
        bits(state.WorldSpace.CadgeWidth), bits(state.WorldSpace.CadgeBaseLine),
        bits(state.WorldSpace.TilePxWidthHalf), bits(state.WorldSpace.TilePxHeightHalf)}};
    bool changed = !HaveObjectView || FrameView != PreviousView;
    const bool hadMembers = ResolveAllObjects || std::any_of(PreviousObjects.begin(), PreviousObjects.end(),
        [](const ObjectView& view) { return view.Resolved; });
    if (changed && hadMembers) memberChanged = true;
    ObjectViewFallback = false;
    try {
        FrameObjects.clear();
        FrameObjects.reserve(Blueprint->Objects.size());
        for (auto* obj : Blueprint->Objects) {
            ObjectView view;
            view.Object = obj; view.ObjectID = obj->ObjectID; view.Identity = obj->ID;
            view.Resolved = obj->renderInfo.Composited;
            const auto position = obj->DrawPosition();
            bool eligible = obj->Visible && !obj->CutawayHidden() && obj->Level <= state.Level() &&
                (state.DrawOOB || !(position.X < -2043 && position.Y < -2043));
            if (state.RenderingThumbnail && bounds.Width > 0)
                eligible &= position.X >= bounds.Left() && position.X < bounds.Right() &&
                    position.Y >= bounds.Top() && position.Y < bounds.Bottom();
            if (eligible) {
                obj->ValidateSprite(state);
                view.Bounds = ObjectScreenBounds(obj, state);
                eligible = view.Bounds.Width > 0 && view.Bounds.X < Origin.X + Composite.Width &&
                    view.Bounds.Y < Origin.Y + Composite.Height && view.Bounds.Right() > Origin.X &&
                    view.Bounds.Bottom() > Origin.Y;
            }
            view.Eligible = eligible;
            if (eligible) {
                view.Position = position; view.Graphic = obj->DGRP; view.Definition = obj->Obj;
                view.Flags = obj->DynamicSpriteFlags; view.Flags2 = obj->DynamicSpriteFlags2;
                view.Room = obj->Room; view.Level = obj->Level; view.Direction = int(obj->Direction);
                view.Visible = obj->Visible; view.Hidden = obj->CutawayHidden();
                view.Rejected = obj->PlacementRejected(); view.Partial = obj->HasPartialAlpha(state);
            } else view.Bounds = {};
            const size_t index = FrameObjects.size();
            view.Changed = !HaveObjectView || index >= PreviousObjects.size() || !(view == PreviousObjects[index]);
            if (view.Changed) {
                changed = true;
                // Object order matters for overlapping translucent sources.
                if (hadMembers && index < PreviousObjects.size() &&
                    (view.Object != PreviousObjects[index].Object ||
                     view.ObjectID != PreviousObjects[index].ObjectID ||
                     view.Identity != PreviousObjects[index].Identity ||
                     view.Definition != PreviousObjects[index].Definition)) memberChanged = true;
            }
            FrameObjects.push_back(view);
        }
        if (FrameObjects.size() != PreviousObjects.size()) {
            changed = true;
            memberChanged |= hadMembers;
        }
    } catch (const std::bad_alloc&) {
        FrameObjects.clear(); ObjectViewFallback = true; memberChanged = true; return true;
    }
    return changed;
}

bool World2D::PrepareResolvedGroups(bool& memberChanged) {
    const int tile = Framebuffer::TRANSPARENCY_TILE;
    const int columns = (Composite.Width + tile - 1) / tile;
    const int rows = (Composite.Height + tile - 1) / tile;
    const bool wasAll = ResolveAllObjects;
    std::vector<st::Rectangle> previousRegions;
    previousRegions.swap(ResolveRegions);
    ResolveAllObjects = false;
    for (auto* obj : Blueprint->Objects) obj->renderInfo.Composited = false;
    try {
        if (ObjectViewFallback) throw std::bad_alloc();
        ResolveTiles.assign(size_t(columns) * rows, 0);
        for (auto* layer : {&StaticFloor.Buffer, &StaticWall.Buffer, &StaticObjects.Buffer}) {
            if (layer->Width != Composite.Width || layer->Height != Composite.Height) continue;
            if (layer->TransparencyAll) throw std::bad_alloc(); // bounded whole-composite fallback
            if (layer->Transparency.empty()) continue;
            for (size_t i = 0; i < ResolveTiles.size(); ++i)
                if (layer->IsTransparentTile(i)) ResolveTiles[i] = 1;
        }
        const auto extent = [&](const ObjectView& view) {
            const auto& b = view.Bounds;
            const int x0 = std::clamp((b.X - int(Origin.X) - 2) / tile, 0, columns);
            const int y0 = std::clamp((b.Y - int(Origin.Y) - 2) / tile, 0, rows);
            const int x1 = std::clamp((b.Right() - int(Origin.X) + 2 + tile - 1) / tile, 0, columns);
            const int y1 = std::clamp((b.Bottom() - int(Origin.Y) + 2 + tile - 1) / tile, 0, rows);
            return st::Rectangle(x0, y0, x1 - x0, y1 - y0);
        };
        const auto mark = [&](const st::Rectangle& r) {
            bool added = false;
            for (int y = r.Top(); y < r.Bottom(); ++y) for (int x = r.Left(); x < r.Right(); ++x) {
                auto& selected = ResolveTiles[size_t(y) * columns + x];
                if (!selected) { selected = 1; added = true; }
            }
            return added;
        };
        // Dynamic translucent sources also seed groups. This preserves their
        // authored order relative to cached opaque and translucent neighbours.
        for (const auto& view : FrameObjects) if (view.Eligible && view.Partial) mark(extent(view));
        bool extended = true;
        while (extended) {
            extended = false;
            for (auto& view : FrameObjects) if (view.Eligible) {
                const auto r = extent(view);
                bool touches = false;
                for (int y = r.Top(); y < r.Bottom() && !touches; ++y)
                    for (int x = r.Left(); x < r.Right(); ++x)
                        if (ResolveTiles[size_t(y) * columns + x]) { touches = true; break; }
                if (touches) {
                    extended |= mark(r);
                    view.Object->renderInfo.Composited = true;
                }
            }
        }
        ResolveRegions.clear();
        for (int y = 0; y < rows; ++y) for (int x = 0; x < columns;) {
            if (!ResolveTiles[size_t(y) * columns + x]) { ++x; continue; }
            const int begin = x;
            while (x < columns && ResolveTiles[size_t(y) * columns + x]) ++x;
            auto previous = std::find_if(ResolveRegions.begin(), ResolveRegions.end(), [&](const st::Rectangle& r) {
                return r.X == begin * tile && r.Width == (x - begin) * tile && r.Bottom() == y * tile;
            });
            if (previous != ResolveRegions.end()) previous->Height += tile;
            else ResolveRegions.emplace_back(begin * tile, y * tile, (x - begin) * tile, tile);
        }
        for (auto& r : ResolveRegions) {
            r.Width = std::min(r.Width, Composite.Width - r.X);
            r.Height = std::min(r.Height, Composite.Height - r.Y);
        }
        // Each crop traverses terrain/wall source geometry. Bound that work:
        // dense fragmented coverage uses one conservative bounding crop, with
        // complete bounds of every additional touched object included too.
        if (ResolveRegions.size() > 8) {
            int x0 = Composite.Width, y0 = Composite.Height, x1 = 0, y1 = 0;
            for (const auto& r : ResolveRegions) {
                x0 = std::min(x0, r.Left()); y0 = std::min(y0, r.Top());
                x1 = std::max(x1, r.Right()); y1 = std::max(y1, r.Bottom());
            }
            bool expanded = true;
            while (expanded) {
                expanded = false;
                for (const auto& view : FrameObjects) if (view.Eligible) {
                    const int bx0 = std::max(0, view.Bounds.X - int(Origin.X) - 2);
                    const int by0 = std::max(0, view.Bounds.Y - int(Origin.Y) - 2);
                    const int bx1 = std::min(Composite.Width, view.Bounds.Right() - int(Origin.X) + 2);
                    const int by1 = std::min(Composite.Height, view.Bounds.Bottom() - int(Origin.Y) + 2);
                    if (bx0 >= x1 || by0 >= y1 || bx1 <= x0 || by1 <= y0) continue;
                    view.Object->renderInfo.Composited = true;
                    const int nx0 = std::min(x0, bx0), ny0 = std::min(y0, by0);
                    const int nx1 = std::max(x1, bx1), ny1 = std::max(y1, by1);
                    expanded |= nx0 != x0 || ny0 != y0 || nx1 != x1 || ny1 != y1;
                    x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
                }
            }
            ResolveRegions.assign(1, st::Rectangle(x0, y0, x1 - x0, y1 - y0));
        }
        for (const auto& view : FrameObjects)
            if (view.Resolved != view.Object->renderInfo.Composited) memberChanged = true;
        if (ResolveRegions.size() != previousRegions.size()) memberChanged = true;
        else for (size_t i = 0; i < ResolveRegions.size(); ++i) {
            const auto& a = ResolveRegions[i]; const auto& b = previousRegions[i];
            if (a.X != b.X || a.Y != b.Y || a.Width != b.Width || a.Height != b.Height) memberChanged = true;
        }
        if (wasAll) memberChanged = true;
    } catch (const std::bad_alloc&) {
        ResolveAllObjects = true;
        ResolveTiles.clear(); ResolveRegions.clear();
        for (auto* obj : Blueprint->Objects) obj->renderInfo.Composited = true;
        memberChanged = true;
    }
    return ResolveAllObjects || !ResolveRegions.empty();
}

void World2D::PreDraw(WorldState& state) {
    if (Blueprint == nullptr) return;
    auto pxOffset = state.WorldSpace.GetScreenOffset();
    pxOffset = st::Vector2(-pxOffset.X, -pxOffset.Y);
    auto& damage = Blueprint->Damage;
    auto _2d = state._2D;

    /**
     * Tasks:
     *  If zoom or rotation has changed, redraw all static layers
     *  If scroll has changed, redraw static layer if the scroll is outwith the buffered region
     *  If architecture has changed, redraw appropriate static layer
     *  If there is a new object in the static layer, redraw the static layer
     *  If an objects in the static layer has changed, redraw the static layer and move the object to the dynamic layer
     *  If wall visibility has changed, redraw wall layer (should think about how this works with breakthrough wall mode
     */

    auto redrawStaticObjects = false;
    auto redrawFloor = false;
    auto redrawWall = false;

    auto recacheWalls = false;
    auto recacheFloors = false;
    auto recacheTerrain = false;
    auto recacheObjects = false;
    bool relight = false;
    bool cutChanged = false;
    std::vector<st::Rectangle> objDirty; // FSO_CPP: world-pixel areas of the static object layer to redraw

    if (TicksSinceLight++ > 60 * 4) damage.push_back(BlueprintDamage(BlueprintDamageType::LIGHTING_CHANGED));

    // FSO_CPP: damage can reference components the VM has deleted since (C# keeps them alive via the GC).
    std::unordered_set<WorldComponent*> live(Blueprint->Objects.begin(), Blueprint->Objects.end());

    WorldObjectRenderInfo* info = nullptr;
    for (auto& item : damage) {
        switch (item.Type) {
            case BlueprintDamageType::ROTATE:
            case BlueprintDamageType::ZOOM:
            case BlueprintDamageType::LEVEL_CHANGED:
                recacheObjects = true;
                recacheWalls = true;
                recacheFloors = true;
                recacheTerrain = true;
                break;
            case BlueprintDamageType::SCROLL:
                // FSO_CPP: handled below by shifting the layers (see World2D.h)
                break;
            case BlueprintDamageType::PRECISE_ZOOM:
                redrawFloor = redrawWall = redrawStaticObjects = true;
                break;
            case BlueprintDamageType::LIGHTING_CHANGED: {
                // TS1 sprite ambient uses VisualLight independently of F5.
                // Leave the unproved legacy rig/terrain RGB path unchanged.
                Blueprint->GenerateRoomLights(state.ObjectLighting);
                state.OutsideColor = Blueprint->RoomColors[1];
                _2d->AmbientLight = Blueprint->RoomColors.data();
                TicksSinceLight = 0;
                // FSO_CPP: C# re-renders the static layers on every lighting tick (every 4s); on the CPU that is
                // expensive, so only do it when a room colour actually changed.
                bool changed = LastRoomColors.size() != Blueprint->Light.size() + 1;
                if (!changed) {
                    for (size_t i = 0; i < Blueprint->Light.size(); i++)
                        if (LastRoomColors[i] != Blueprint->RoomColors[i].PackedValue()) { changed = true; break; }
                }
                if (state.OutsideColor.PackedValue() != LastOutside) changed = true;
                if (changed) {
                    LastRoomColors.resize(Blueprint->Light.size() + 1);
                    for (size_t i = 0; i < Blueprint->Light.size(); i++) LastRoomColors[i] = Blueprint->RoomColors[i].PackedValue();
                    LastOutside = state.OutsideColor.PackedValue();
                    // FSO_CPP: the static layers hold unlit colour + room ids, only the composite is rebuilt
                    relight = true;
                }
                break;
            }
            case BlueprintDamageType::OBJECT_MOVE:
            case BlueprintDamageType::OBJECT_GRAPHIC_CHANGE: {
                /** Redraw if its in static layer **/
                if (item.Component == nullptr || !live.count(item.Component)) {
                    // removed object: it may still be painted in the static layer
                    // FSO_CPP: only the area it covered is redrawn (C# redrew the whole layer)
                    if (item.Component != nullptr) TakeStaticBounds(item.Component, objDirty, recacheObjects);
                    break;
                }
                info = GetRenderInfo(item.Component);
                if (info != nullptr && info->Layer == WorldObjectRenderLayer::STATIC) {
                    TakeStaticBounds(item.Component, objDirty, recacheObjects);
                    info->Layer = WorldObjectRenderLayer::DYNAMIC;
                }
                if (auto oc = dynamic_cast<ObjectComponent*>(item.Component)) oc->DynamicCounter = 0;
                break;
            }
            case BlueprintDamageType::OBJECT_RETURN_TO_STATIC:
                if (item.Component == nullptr || !live.count(item.Component)) break;
                info = GetRenderInfo(item.Component);
                if (info != nullptr && info->Layer == WorldObjectRenderLayer::DYNAMIC) {
                    info->Layer = WorldObjectRenderLayer::STATIC;
                    if (auto oc = dynamic_cast<ObjectComponent*>(item.Component)) {
                        auto r = ObjectScreenBounds(oc, state);
                        if (r.Width > 0) objDirty.push_back(r); // empty sprite: nothing to paint
                    } else recacheObjects = true;
                }
                break;
            case BlueprintDamageType::WALL_CUT_CHANGED:
                cutChanged = true; // FSO_CPP: resolved after the loop (only the changed walls when possible)
                break;
            case BlueprintDamageType::ROOF_STYLE_CHANGED:
                Blueprint->RoofComp.StyleDirty = true;
                if (state.DrawRoofs) recacheWalls = true; // the roof is part of the wall layer
                break;
            case BlueprintDamageType::ROOM_CHANGED:
                Blueprint->RoofComp.ShapeDirty = true;
                if (state.DrawRoofs) recacheWalls = true;
                break;
            case BlueprintDamageType::FLOOR_CHANGED:
            case BlueprintDamageType::WALL_CHANGED:
                recacheTerrain = true;
                recacheFloors = true;
                recacheWalls = true;
                Blueprint->RoofComp.ShapeDirty = true;
                break;
        }
        if (getenv("WORLD_DAMAGE")) printf("damage %d -> floor %d wall %d obj %d\n", (int)item.Type, (int)(recacheFloors || recacheTerrain), (int)recacheWalls, (int)recacheObjects);
        if (recacheFloors || recacheTerrain) redrawFloor = true;
        if (recacheWalls) redrawWall = true;
        if (recacheObjects) redrawStaticObjects = true;
    }
    damage.clear();
    st::Rectangle cutRect; // buffer-independent world pixels; Width 0 = nothing
    if (cutChanged && !redrawWall) {
        if (!CutDirtyRect(state, cutRect)) redrawWall = true;
        if (getenv("WORLD_DAMAGE")) printf("cut change: rect %d,%d %dx%d full %d\n", cutRect.X, cutRect.Y, cutRect.Width, cutRect.Height, (int)redrawWall);
    }
    if (redrawWall || cutRect.Width > 0) { LastCut = Blueprint->Cutaway; LastCutLevel = state.Level(); }
    // FSO_CPP: World3D drew the roof every frame when State.DrawRoofs; here it lives in the cached wall layer.
    if (state.DrawRoofs != LastDrawRoofs) { LastDrawRoofs = state.DrawRoofs; redrawWall = true; }

    // Save captures follow GetLotThumb's ClearLighting(false)/white exterior.
    // Null means unlit in the sprite rasterizer; normal frames rebind rooms.
    _2d->AmbientLight = state.RenderingThumbnail ? nullptr : Blueprint->RoomColors.data();
    VisualLight = Blueprint->VisualLight.Enabled && !state.RenderingThumbnail ? &Blueprint->VisualLight : nullptr;
    _2d->VisualLight = VisualLight;
    if (state.RenderingThumbnail) state.OutsideColor = st::Color::White();

    if (recacheTerrain && Blueprint->Terrain)
        Blueprint->Terrain->RegenTerrain(state, Blueprint);

    int32_t bw = (int32_t)state.WorldSpace.WorldPxWidth + SCROLL_BUFFER, bh = (int32_t)state.WorldSpace.WorldPxHeight + SCROLL_BUFFER;
    AmbientLight = state.RenderingThumbnail ? nullptr : Blueprint->RoomColors.data();
    // Exterior geometry shares the settled palette/cadence of outside sprites.
    OutsideLight = state.RenderingThumbnail ? st::Color::White() :
        (Blueprint->Light.size() > 1 ? Blueprint->RoomColors[1] : state.OutsideColor);
    if (OutsideLight.A == 0) OutsideLight = st::Color::White();
    const bool visualModeChanged = RenderVisualEnabled != (VisualLight != nullptr);
    const bool visualGeometryChanged = VisualLight && RenderVisualGeometry != VisualLight->GeometryGeneration;
    const bool visualChanged = VisualLight && RenderVisualGeneration != VisualLight->Generation;
    const uint64_t spatialGeneration = VisualLight ? VisualLight->SpatialGeneration : 0;
    const uint64_t intensityGeneration = VisualLight ? VisualLight->IntensityGeneration : 0;
    const bool spatialMapChanged = visualModeChanged || spatialGeneration != RenderSpatialGeneration;
    if (spatialMapChanged) {
        HaveSpatialSources = false;
        if (VisualLight) for (const auto& map : VisualLight->SpatialMaps) {
            if (std::any_of(map.begin(),map.end(),[](const auto& cell) { return cell.Slots[0].Dx != 127; })) {
                HaveSpatialSources = true; break;
            }
        }
    }
    bool spatialEnabled = false;
    if (VisualLight && HaveSpatialSources && state.ObjectLighting) {
        const auto* content = FSO::Content::Content::Get();
        spatialEnabled = content && SpatialMasks.Prepare(content->BasePath, int(state.Zoom()));
    }
    _2d->SpatialMasks = spatialEnabled ? &SpatialMasks : nullptr;
    _2d->SpatialEnabled = spatialEnabled;
    _2d->SpatialRotation = int(state.Rotation());
    const bool spatialChanged = spatialMapChanged || visualChanged || intensityGeneration != RenderIntensityGeneration ||
        SpatialMasks.Generation() != RenderMaskGeneration ||
        spatialEnabled != RenderSpatialEnabled;
    if (spatialChanged) {
        // Potential dependencies include layers cached while no sources were
        // active, the option was off, or the ambient used its base palette.
        redrawFloor |= StaticFloor.Buffer.HasSpatialSurface;
        redrawWall |= StaticWall.Buffer.HasSpatialSurface;
        relight = true;
    }
    RenderSpatialGeneration = spatialGeneration;
    RenderIntensityGeneration = intensityGeneration;
    RenderSpatialEnabled = spatialEnabled;
    RenderMaskGeneration = SpatialMasks.Generation();
    if (visualModeChanged || visualGeometryChanged) redrawFloor = redrawWall = redrawStaticObjects = true;
    bool paletteChanged = visualModeChanged || visualChanged || RenderLighting != (AmbientLight != nullptr) ||
        RenderOutside != OutsideLight.PackedValue() || RenderRoomColors.size() != Blueprint->Light.size();
    if (!paletteChanged) for (size_t i = 0; i < RenderRoomColors.size(); ++i)
        if (RenderRoomColors[i] != Blueprint->RoomColors[i].PackedValue()) { paletteChanged = true; break; }
    if (paletteChanged) {
        RenderLighting = AmbientLight != nullptr;
        RenderOutside = OutsideLight.PackedValue();
        RenderRoomColors.resize(Blueprint->Light.size());
        for (size_t i = 0; i < RenderRoomColors.size(); ++i) RenderRoomColors[i] = Blueprint->RoomColors[i].PackedValue();
        relight = true;
        // Only layers which contain resolved light mixtures depend on the
        // palette during rasterization. Opaque/single-domain layers stay unlit.
        redrawFloor |= !VisualLight && StaticFloor.Buffer.HasPrelit;
        redrawWall |= !VisualLight && StaticWall.Buffer.HasPrelit;
        redrawStaticObjects |= !VisualLight && StaticObjects.Buffer.HasPrelit;
    }
    RenderVisualEnabled = VisualLight != nullptr;
    RenderVisualGeneration = VisualLight ? VisualLight->Generation : 0;
    RenderVisualGeometry = VisualLight ? VisualLight->GeometryGeneration : 0;
    bool sizeChanged = Composite.Width != bw || Composite.Height != bh;
    auto newOrigin = GetBufferOrigin(pxOffset);
    const bool bufferShifted = HaveOrigin && newOrigin != Origin;
    bool anyInvalid = !StaticFloor.Valid || !StaticWall.Valid || !StaticObjects.Valid || !CompositeValid || !HaveOrigin;
    if (sizeChanged || anyInvalid) {
        redrawFloor = redrawWall = redrawStaticObjects = true;
        HaveOrigin = false;
    }
    bool redraw[3] = { redrawFloor, redrawWall, redrawStaticObjects };
    bool compositeAll = sizeChanged || anyInvalid || redrawFloor || redrawWall || redrawStaticObjects || relight;
    if (sizeChanged) Composite.Resize(bw, bh);

    // scrolling: shift the cached pixels, render only what scrolled in
    std::vector<st::Rectangle> strips;
    if (HaveOrigin && newOrigin != Origin) {
        int32_t sx = (int32_t)(Origin.X - newOrigin.X), sy = (int32_t)(Origin.Y - newOrigin.Y);
        if (std::abs(sx) >= bw / 2 || std::abs(sy) >= bh / 2) {
            redraw[0] = redraw[1] = redraw[2] = true;
            compositeAll = true;
        } else {
            for (int l = 0; l < 3; l++) if (!redraw[l]) ShiftBuffer(LayerBuffer(l).Buffer, sx, sy);
            if (!compositeAll) ShiftBuffer(Composite, sx, sy);
            if (sx > 0) strips.push_back(st::Rectangle(0, 0, sx, bh));
            else if (sx < 0) strips.push_back(st::Rectangle(bw + sx, 0, -sx, bh));
            if (sy > 0) strips.push_back(st::Rectangle(0, 0, bw, sy));
            else if (sy < 0) strips.push_back(st::Rectangle(0, bh + sy, bw, -sy));
        }
    }
    Origin = newOrigin;
    HaveOrigin = true;
    for (int l = 0; l < 3; l++) {
        auto& buf = LayerBuffer(l);
        buf.PxOffset = Origin;
        buf.WorldPosition = st::Vector3(state.CenterTile().X, state.CenterTile().Y, 0);
    }

    for (int l = 0; l < 3; l++) {
        if (redraw[l]) { RenderLayer(l, state, nullptr); StaticRedraws++; }
        else for (auto& r : strips) { RenderLayer(l, state, &r); StripRedraws++; StripPixels += (int64_t)r.Width * r.Height; }
    }
    std::vector<st::Rectangle> compositeRects;
    if (!compositeAll) compositeRects = strips;
    if (!redraw[WALL] && cutRect.Width > 0) {
        int32_t x0 = std::max<int32_t>(0, cutRect.X - (int32_t)Origin.X), y0 = std::max<int32_t>(0, cutRect.Y - (int32_t)Origin.Y);
        int32_t x1 = std::min<int32_t>(bw, cutRect.X + cutRect.Width - (int32_t)Origin.X), y1 = std::min<int32_t>(bh, cutRect.Y + cutRect.Height - (int32_t)Origin.Y);
        if (x1 > x0 && y1 > y0) {
            st::Rectangle r(x0, y0, x1 - x0, y1 - y0);
            RenderLayer(WALL, state, &r); DirtyRedraws++; StripPixels += (int64_t)r.Width * r.Height;
            if (!compositeAll) compositeRects.push_back(r);
        }
    }
    if (!redraw[OBJECTS] && !objDirty.empty()) {
        int64_t area = 0;
        std::vector<st::Rectangle> rects;
        for (auto& w : objDirty) {
            int32_t x0 = std::max<int32_t>(0, w.X - (int32_t)Origin.X - 2), y0 = std::max<int32_t>(0, w.Y - (int32_t)Origin.Y - 2);
            int32_t x1 = std::min<int32_t>(bw, w.X + w.Width - (int32_t)Origin.X + 2), y1 = std::min<int32_t>(bh, w.Y + w.Height - (int32_t)Origin.Y + 2);
            if (x1 <= x0 || y1 <= y0) continue;
            rects.push_back(st::Rectangle(x0, y0, x1 - x0, y1 - y0));
            area += (int64_t)(x1 - x0) * (y1 - y0);
        }
        if (area * 2 > (int64_t)bw * bh) {
            RenderLayer(OBJECTS, state, nullptr); StaticRedraws++;
            compositeAll = true;
        } else {
            for (auto& r : rects) {
                RenderLayer(OBJECTS, state, &r); DirtyRedraws++; StripPixels += (int64_t)r.Width * r.Height;
                if (!compositeAll) compositeRects.push_back(r);
            }
        }
    }
    bool memberChanged = false;
    const bool hadGroups = ResolveAllObjects || !ResolveRegions.empty();
    const bool objectsChanged = CaptureObjects(state, memberChanged);
    if (compositeAll || !compositeRects.empty() || objectsChanged || bufferShifted || !HaveObjectView)
        PrepareResolvedGroups(memberChanged);
    // With an unchanged group topology, a member can only alter pixels inside
    // its old/new drawing bounds. Reconstruct that footprint against all source
    // contributors; global, identity and topology changes retain the full path.
    if (!compositeAll && !memberChanged && !bufferShifted && !FullCompositeDamage &&
        HaveObjectView && !ObjectViewFallback && !ResolveAllObjects && FrameView == PreviousView &&
        FrameObjects.size() == PreviousObjects.size()) {
        try {
            for (size_t i = 0; i < FrameObjects.size(); ++i) {
                const auto& current = FrameObjects[i]; const auto& old = PreviousObjects[i];
                if (!current.Changed || !old.Resolved || !current.Object->renderInfo.Composited) continue;
                const int x0 = std::max(0, std::min(old.Bounds.Left(), current.Bounds.Left()) - int(Origin.X) - 2);
                const int y0 = std::max(0, std::min(old.Bounds.Top(), current.Bounds.Top()) - int(Origin.Y) - 2);
                const int x1 = std::min(bw, std::max(old.Bounds.Right(), current.Bounds.Right()) - int(Origin.X) + 2);
                const int y1 = std::min(bh, std::max(old.Bounds.Bottom(), current.Bounds.Bottom()) - int(Origin.Y) + 2);
                if (x1 > x0 && y1 > y0) {
                    const auto same = std::find_if(compositeRects.begin(), compositeRects.end(), [&](const st::Rectangle& r) {
                        return r.X == x0 && r.Y == y0 && r.Width == x1 - x0 && r.Height == y1 - y0;
                    });
                    if (same == compositeRects.end()) compositeRects.emplace_back(x0, y0, x1 - x0, y1 - y0);
                }
            }
        } catch (const std::bad_alloc&) { compositeAll = true; }
    }
    // Buffer-relative groups move with the retained composite. A changed origin
    // or membership needs their complete visible bounds painted before dynamic
    // draws can be omitted.
    compositeAll |= memberChanged || (bufferShifted && hadGroups) || FullCompositeDamage;
    if (!compositeAll && !ResolveRegions.empty() && compositeRects.size() > 1) {
        int x0 = bw, y0 = bh, x1 = 0, y1 = 0;
        for (const auto& r : compositeRects) {
            x0 = std::min(x0, r.Left()); y0 = std::min(y0, r.Top());
            x1 = std::max(x1, r.Right()); y1 = std::max(y1, r.Bottom());
        }
        // One damage crop avoids traversing source geometry once per changed
        // member. The existing region cap still bounds the number of replays.
        compositeRects[0] = st::Rectangle(x0, y0, x1 - x0, y1 - y0);
        compositeRects.resize(1);
    }
    if (compositeAll) BuildComposite(st::Rectangle(0, 0, bw, bh), state);
    else for (const auto& r : compositeRects) BuildComposite(r, state);
    FullCompositeDamage = false;
    if (!ObjectViewFallback) {
        for (auto& view : FrameObjects) view.Resolved = view.Object->renderInfo.Composited;
        PreviousObjects.swap(FrameObjects);
        PreviousView = FrameView; HaveObjectView = true;
    } else HaveObjectView = false;
    CompositeValid = true;
    _2d->Target = nullptr;
}

// FSO_CPP: world-pixel bounds of the walls whose cut changed since the wall layer was rendered. A tile's walls (and
// the junctions/end caps its neighbours draw) lie within the tiles around it, from the floor of the current level up
// to wall height. Returns false when the whole layer should be redrawn (no previous cut, level change, larger than
// the buffer).
bool World2D::CutDirtyRect(WorldState& state, st::Rectangle& out) {
    out = st::Rectangle();
    auto& cut = Blueprint->Cutaway;
    if (LastCut.size() != cut.size() || LastCutLevel != state.Level() || Blueprint->Width <= 0) return false;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    int w = Blueprint->Width, n = 0;
    float z0 = (state.Level() - 1) * 2.95f;
    for (size_t i = 0; i < cut.size(); i++) {
        if (cut[i] == LastCut[i]) continue;
        n++;
        int tx = (int)(i % (size_t)w), ty = (int)(i / (size_t)w);
        for (int cx = 0; cx < 2; cx++) for (int cy = 0; cy < 2; cy++) {
            auto p = state.WorldSpace.GetScreenFromTile(st::Vector3((float)(tx - 1 + cx * 3), (float)(ty - 1 + cy * 3), z0));
            minX = std::min(minX, p.X); maxX = std::max(maxX, p.X); minY = std::min(minY, p.Y); maxY = std::max(maxY, p.Y);
        }
    }
    if (n == 0) return true; // nothing visible changed
    // walls rise one story above the floor (FreeSims wall heights: 57 / 115 / 231 px at far / medium / near)
    int32_t wallH = state.Zoom() == WorldZoom::Far ? 57 : (state.Zoom() == WorldZoom::Medium ? 115 : 231);
    int32_t m = (int32_t)(state.WorldSpace.TilePxWidth / 2);
    // same space as ObjectScreenBounds (GetScreenFromTile, buffer pixel = value - Origin)
    out = st::Rectangle((int32_t)std::floor(minX) - m, (int32_t)std::floor(minY) - wallH - m,
                        (int32_t)std::ceil(maxX - minX) + 2 * m, (int32_t)std::ceil(maxY - minY) + wallH + 2 * m);
    int64_t bw = (int64_t)state.WorldSpace.WorldPxWidth + SCROLL_BUFFER, bh = (int64_t)state.WorldSpace.WorldPxHeight + SCROLL_BUFFER;
    return (int64_t)out.Width * out.Height < bw * bh;
}

st::Rectangle World2D::ObjectScreenBounds(ObjectComponent* obj, WorldState& state) {
    obj->ValidateSprite(state);
    auto b = obj->Bounding();
    if (b.Width <= 0 || b.Height <= 0) return st::Rectangle();
    auto oPx = state.WorldSpace.GetScreenFromTile(obj->DrawPosition());
    return st::Rectangle(b.X + (int32_t)oPx.X, b.Y + (int32_t)oPx.Y, b.Width, b.Height);
}

void World2D::TakeStaticBounds(WorldComponent* comp, std::vector<st::Rectangle>& dirty, bool& fallback) {
    auto it = StaticBounds.find(comp);
    if (it == StaticBounds.end()) return; // never painted in the static layer
    (void)fallback;
    if (it->second.Width > 0) dirty.push_back(it->second);
    StaticBounds.erase(it);
}

void World2D::Invalidate() {
    StaticFloor.Valid = StaticWall.Valid = StaticObjects.Valid = false;
    CompositeValid = false;
    RetainedTarget = nullptr;
    HaveOrigin = false;
}

st::Vector2 World2D::GetScrollIncrement(const st::Vector2& pxOffset, WorldState& state) {
    auto scrollSize = SCROLL_BUFFER / state.PreciseZoom();
    return st::Vector2((float)std::floor(pxOffset.X / scrollSize) * scrollSize, (float)std::floor(pxOffset.Y / scrollSize) * scrollSize);
}

// The view [px, px + view) must stay inside [origin, origin + view + SCROLL_BUFFER): origin moves in steps of
// SCROLL_BUFFER/4 and keeps a margin on both sides.
st::Vector2 World2D::GetBufferOrigin(const st::Vector2& px) const {
    int32_t step = std::max<int32_t>(1, SCROLL_BUFFER / 4);
    int32_t m = (SCROLL_BUFFER - step) / 2;
    auto f = [&](float v) { return (float)((int32_t)std::floor(v / step) * step - m); };
    return st::Vector2(f(px.X), f(px.Y));
}

void World2D::RenderLayer(int layer, WorldState& state, const st::Rectangle* clip) {
    auto _2d = state._2D;
    auto& buf = LayerBuffer(layer);
    auto& fb = buf.Buffer;
    const bool resolving = fb.ResolvePrelit;
    const bool alreadyAll = fb.PrelitAll;
    fb.NewPrelitPixels = 0;
    fb.CacheLight = _2d->AmbientLight;
    fb.CacheOutsideLight = OutsideLight;
    int32_t bw = Composite.Width, bh = Composite.Height;
    if (clip == nullptr) {
        if (layer == OBJECTS) StaticBounds.clear();
        fb.Resize(bw, bh);
        if (fb.Room.size() != fb.Color.size()) fb.EnableRoom(true);
        fb.Clear(0);
        fb.ResetClip();
        buf.Valid = true;
    } else {
        fb.ClearRect(clip->X, clip->Y, clip->X + clip->Width, clip->Y + clip->Height, 0);
        fb.SetClip(clip->X, clip->Y, clip->X + clip->Width, clip->Y + clip->Height);
    }
    _2d->Target = &fb;
    _2d->SetScroll(Origin);
    switch (layer) {
        case FLOOR:
            /** Draw archetecture to a texture **/
            if (Blueprint->Terrain) {
                // FSO_CPP: the terrain mesh (3D in FreeSims) is rasterized straight into the floor layer
                Blueprint->Terrain->DepthMode = _2d->OutputDepth;
                Blueprint->Terrain->DrawInto(state, fb);
            }
            _2d->Begin(state.Camera());
            if (Blueprint->Floors) Blueprint->FloorComp->Draw(state);
            _2d->End();
            break;
        case WALL:
            _2d->Begin(state.Camera());
            if (Blueprint->Walls && Blueprint->WallsAt) Blueprint->WallComp->Draw(state);
            _2d->End();
            // FSO_CPP: World3D drew the roof every frame when State.DrawRoofs; here it lives in the cached wall layer.
            if (state.DrawRoofs) Blueprint->RoofComp.DrawInto(state, fb);
            break;
        case OBJECTS: {
            /** Draw static objects to a texture **/
            int32_t cx0 = std::max<int32_t>(0, fb.ClipX0), cy0 = std::max<int32_t>(0, fb.ClipY0);
            int32_t cx1 = std::min(fb.Width, fb.ClipX1), cy1 = std::min(fb.Height, fb.ClipY1);
            _2d->Begin(state.Camera());
            for (auto obj : Blueprint->Objects) {
                if (obj->Level > state.Level()) continue;
                if (obj->renderInfo.Layer == WorldObjectRenderLayer::STATIC) {
                    auto tilePosition = obj->DrawPosition();
                    auto oPx = state.WorldSpace.GetScreenFromTile(tilePosition);
                    if (clip != nullptr) {
                        obj->ValidateSprite(state);
                        auto b = obj->Bounding();
                        int32_t x = b.X + (int32_t)oPx.X - (int32_t)Origin.X, y = b.Y + (int32_t)oPx.Y - (int32_t)Origin.Y;
                        if (x >= cx1 || y >= cy1 || x + b.Width <= cx0 || y + b.Height <= cy0) continue;
                    }
                    _2d->OffsetPixel(oPx);
                    _2d->OffsetTile(tilePosition);
                    _2d->SetObjID(obj->ObjectID);
                    obj->Draw(state);
                    StaticBounds[obj] = ObjectScreenBounds(obj, state);
                }
            }
            _2d->End();
            break;
        }
    }
    fb.ResetClip();
    _2d->Target = nullptr;
    if (!VisualLight && !resolving && (fb.NewPrelitPixels || fb.PrelitAll)) {
        // Replay from the first fragment: lighting an already mixed RGB value
        // cannot recover its individual ordinary/luminous contributions.
        fb.ResolvePrelit = true;
        // A failed mask allocation changes the meaning of every pixel in this
        // layer. Promote a partial update, including the final composite.
        const bool promote = clip != nullptr && fb.PrelitAll && !alreadyAll;
        if (promote) FullCompositeDamage = true;
        try { RenderLayer(layer, state, promote ? nullptr : clip); }
        catch (...) { fb.ResolvePrelit = false; throw; }
        fb.ResolvePrelit = false;
        ++PrelitReplays;
    }
}

void World2D::ShiftBuffer(Framebuffer& fb, int32_t sx, int32_t sy) {
    // content moves by (sx, sy): new[x + sx, y + sy] = old[x, y]
    int32_t W = fb.Width, H = fb.Height;
    if (W <= 0 || H <= 0) return;
    int32_t n = W - std::abs(sx);
    if (n <= 0 || std::abs(sy) >= H) return;
    int32_t dstX = std::max<int32_t>(0, sx), srcX = std::max<int32_t>(0, -sx);
    auto moveRow = [&](int32_t dy, int32_t syRow) {
        std::memmove(&fb.Color[(size_t)dy * W + dstX], &fb.Color[(size_t)syRow * W + srcX], (size_t)n * sizeof(uint32_t));
        std::memmove(&fb.Depth[(size_t)dy * W + dstX], &fb.Depth[(size_t)syRow * W + srcX], (size_t)n * sizeof(float));
        if (!fb.Room.empty()) std::memmove(&fb.Room[(size_t)dy * W + dstX], &fb.Room[(size_t)syRow * W + srcX], (size_t)n * sizeof(uint16_t));
        if (!fb.Prelit.empty() && !fb.PrelitAll) {
            // Traverse like memmove when rows also overlap horizontally.
            for (int j = 0; j < n; ++j) {
                const int i = sx > 0 ? n - 1 - j : j;
                const size_t dst = (size_t)dy * W + dstX + i;
                const bool lit = fb.IsPrelit((size_t)syRow * W + srcX + i);
                if (lit) fb.MarkPrelit(dst); else fb.ClearPrelit(dst);
            }
        }
    };
    if (sy > 0) { for (int32_t y = H - 1; y >= sy; y--) moveRow(y, y - sy); }
    else { for (int32_t y = 0; y < H + sy; y++) moveRow(y, y - sy); }
    fb.ShiftTransparency(sx, sy);
}

// drawSimpleRestoreDepth: copies a cached layer (colour + absolute depth) into dst with the depth test.
void World2D::MergeLayer(const Framebuffer& src, Framebuffer& dst, const st::Rectangle& rect) const {
    const uint16_t* rooms = src.Room.empty() ? nullptr : src.Room.data();
    int32_t x0 = std::max<int32_t>(0, rect.X), y0 = std::max<int32_t>(0, rect.Y);
    int32_t x1 = std::min(std::min(src.Width, dst.Width), rect.X + rect.Width), y1 = std::min(std::min(src.Height, dst.Height), rect.Y + rect.Height);
    for (int32_t y = y0; y < y1; y++) {
        const uint32_t* sc = &src.Color[(size_t)y * src.Width];
        const float* sd = &src.Depth[(size_t)y * src.Width];
        uint32_t* tc = &dst.Color[(size_t)y * dst.Width];
        float* td = &dst.Depth[(size_t)y * dst.Width];
        for (int32_t x = x0; x < x1; x++) {
            uint32_t c = sc[x];
            uint32_t a = c >> 24;
            if (a < 3) continue; // color.a < 0.01: discard
            float d = sd[x];
            if (d > td[x]) continue;
            td[x] = d;
            const uint16_t key = rooms ? rooms[(size_t)y * src.Width + x] : uint16_t(65535);
            if (rooms && VisualLight && VisualLight->IsKey(key)) {
                if (a == 255) c = VisualLight->Transform(st::Color(c & 255, (c >> 8) & 255,
                    (c >> 16) & 255, 255), key).PackedValue();
                // Partial (including aggregated opaque) coverage is replaced
                // by fragment replay in BuildComposite, never post-LUT blended.
            } else if (rooms && AmbientLight != nullptr && !src.IsPrelit((size_t)y * src.Width + x)) {
                // deferred ambient light (psZDepthSprite: colour * tex2D(ambientSampler, room))
                uint16_t room = rooms[(size_t)y * src.Width + x];
                if (room != 65535) {
                    const st::Color& l = (room == Framebuffer::OUTSIDE_LIGHT_ROOM) ? OutsideLight : AmbientLight[room];
                    uint32_t lr = l.R + (l.R >> 7), lg = l.G + (l.G >> 7), lb = l.B + (l.B >> 7), la = l.A + (l.A >> 7);
                    a = (a * la) >> 8;
                    c = (((c & 0xFF) * lr) >> 8) | (((((c >> 8) & 0xFF) * lg) >> 8) << 8) | (((((c >> 16) & 0xFF) * lb) >> 8) << 16) | (a << 24);
                }
            }
            if (a == 255) { tc[x] = c; continue; }
            uint32_t dc = tc[x], ia = 255 - a;
            // Cached RGB already includes its coverage alpha.
            uint32_t r = std::min<uint32_t>(255, (c & 0xFF) + ((dc & 0xFF) * ia) / 255);
            uint32_t g = std::min<uint32_t>(255, ((c >> 8) & 0xFF) + (((dc >> 8) & 0xFF) * ia) / 255);
            uint32_t b = std::min<uint32_t>(255, ((c >> 16) & 0xFF) + (((dc >> 16) & 0xFF) * ia) / 255);
            uint32_t na = std::min<uint32_t>(255, a + ((dc >> 24) * ia) / 255);
            tc[x] = Framebuffer::Pack((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)na);
        }
    }
}

void World2D::ResolveComposite(const st::Rectangle& rect, WorldState& state) {
    auto* batch = state._2D;
    struct Restore {
        WorldState& State; Framebuffer& Target; Utils::_2DWorldBatch& Batch;
        st::Color Outside; Framebuffer* PreviousTarget; st::Vector2 Scroll;
        bool OutsideTint; const st::Color* Light; st::Color CachedOutside;
        ~Restore() {
            State.OutsideColor = Outside; Target.ResetClip(); Batch.Target = PreviousTarget; Batch.SetScroll(Scroll);
            Target.DeferredOutsideTint = OutsideTint; Target.CacheLight = Light; Target.CacheOutsideLight = CachedOutside;
        }
    } restore{state, Composite, *batch, state.OutsideColor, batch->Target, batch->GetScroll(),
        Composite.DeferredOutsideTint, Composite.CacheLight, Composite.CacheOutsideLight};
    state.OutsideColor = OutsideLight; // direct triangle producers use this input
    Composite.DeferredOutsideTint = true;
    Composite.CacheLight = AmbientLight; Composite.CacheOutsideLight = OutsideLight;
    Composite.ClearRect(rect.X, rect.Y, rect.Right(), rect.Bottom(), ClearColor);
    Composite.SetClip(rect.X, rect.Y, rect.Right(), rect.Bottom());
    batch->Target = &Composite; batch->SetScroll(Origin);
    if (Blueprint->Terrain) Blueprint->Terrain->DrawInto(state, Composite);
    if (Blueprint->Floors) {
        batch->Begin(state.Camera()); Blueprint->FloorComp->Draw(state); batch->End();
    }
    if (Blueprint->Walls && Blueprint->WallsAt) {
        batch->Begin(state.Camera()); Blueprint->WallComp->Draw(state); batch->End();
    }
    if (state.DrawRoofs) Blueprint->RoofComp.DrawInto(state, Composite);
    batch->Begin(state.Camera());
    // The complete overlapping group is painted in the original object order.
    // Both static and dynamic members are included exactly once.
    for (auto* obj : Blueprint->Objects) {
        if (obj->Level > state.Level()) continue;
        const auto b = ObjectScreenBounds(obj, state);
        const int x = b.X - int(Origin.X), y = b.Y - int(Origin.Y);
        if (x >= rect.Right() || y >= rect.Bottom() || x + b.Width <= rect.X || y + b.Height <= rect.Y) continue;
        const auto position = obj->DrawPosition();
        batch->OffsetPixel(state.WorldSpace.GetScreenFromTile(position));
        batch->OffsetTile(position); batch->SetObjID(obj->ObjectID); obj->Draw(state);
    }
    batch->End();
    ++CompositeReplays;
    CompositeReplayPixels += int64_t(rect.Width) * rect.Height;
}

void World2D::BuildComposite(const st::Rectangle& rect, WorldState& state) {
    RetainedTarget = nullptr;
    Composite.ClearRect(rect.X, rect.Y, rect.X + rect.Width, rect.Y + rect.Height, ClearColor);
    MergeLayer(StaticFloor.Buffer, Composite, rect);
    MergeLayer(StaticWall.Buffer, Composite, rect);
    MergeLayer(StaticObjects.Buffer, Composite, rect);
    if (ResolveAllObjects) ResolveComposite(rect, state);
    else for (const auto& region : ResolveRegions) {
        if (region.X >= rect.Right() || region.Y >= rect.Bottom() ||
            region.Right() <= rect.X || region.Bottom() <= rect.Y) continue;
        const int x0 = std::max(region.X, rect.X), y0 = std::max(region.Y, rect.Y);
        const int x1 = std::min(region.Right(), rect.Right()), y1 = std::min(region.Bottom(), rect.Bottom());
        // Other pixels of an already resolved group remain valid. Clear the
        // intersection before replaying, so cached alpha is never applied twice.
        ResolveComposite(st::Rectangle(x0, y0, x1 - x0, y1 - y0), state);
    }
}

void World2D::MarkDynamic(int x0, int y0, int x1, int y1) {
    if (!RetainedTarget) return;
    x0 = std::max(0, x0);
    x1 = std::min<int>(RetainedTarget->Width, x1);
    if (x0 >= x1) return;
    y1 = std::min<int>(RetainedTarget->Height, y1);
    for (int y = std::max(0, y0); y < y1; ++y) {
        auto& span = RestoreRows[y];
        span.X = std::min<int32_t>(span.X, x0);
        span.Y = std::max<int32_t>(span.Y, x1);
    }
}

void World2D::Draw(WorldState& state, Framebuffer& target, bool retain) {
    if (Blueprint == nullptr || !CompositeValid) { RetainedTarget = nullptr; target.Clear(ClearColor); return; }
    auto _2d = state._2D;
    auto pxOffset = state.WorldSpace.GetScreenOffset();
    pxOffset = st::Vector2(-pxOffset.X, -pxOffset.Y);

    const int ox = (int)(pxOffset.X - Origin.X), oy = (int)(pxOffset.Y - Origin.Y);
    const st::Point offset(ox, oy);
    // The lot owns its frame across draws. With an unchanged view and composite,
    // only pixels touched by last frame's sprites, Sims or effects need restoring.
    const bool partial = retain && RetainedTarget == &target && RetainedOffset == offset;
    RetainedTarget = nullptr;
    for (int y = 0; y < target.Height; ++y) {
        const int left = partial ? RestoreRows[y].X : 0;
        const int right = partial ? RestoreRows[y].Y : target.Width;
        if (left >= right) continue;
        int sy = y + oy;
        uint32_t* tc = target.Color.data() + (size_t)y * target.Width;
        float* td = target.Depth.data() + (size_t)y * target.Width;
        const int x0 = std::clamp(-ox, left, right);
        const int x1 = std::clamp<int32_t>(Composite.Width - ox, left, right);
        if (sy < 0 || sy >= Composite.Height) {
            std::fill(tc + left, tc + right, ClearColor);
            std::fill(td + left, td + right, std::numeric_limits<float>::infinity());
            continue;
        }
        if (left < x0) {
            std::fill(tc + left, tc + x0, ClearColor);
            std::fill(td + left, td + x0, std::numeric_limits<float>::infinity());
        }
        if (x1 < right) {
            std::fill(tc + x1, tc + right, ClearColor);
            std::fill(td + x1, td + right, std::numeric_limits<float>::infinity());
        }
        if (x0 < x1) {
            const size_t src = (size_t)sy * Composite.Width + (x0 + ox);
            std::memcpy(tc + x0, Composite.Color.data() + src, (size_t)(x1 - x0) * sizeof(*tc));
            std::memcpy(td + x0, Composite.Depth.data() + src, (size_t)(x1 - x0) * sizeof(*td));
        }
    }
    if (retain) {
        RestoreRows.assign(target.Height, st::Point(target.Width, 0));
        RetainedOffset = offset;
        RetainedTarget = &target;
    }

    /**
     * Draw dynamic objects. If an object has been static for X frames move it back into the static layer
     */
    _2d->Target = &target;
    _2d->SetScroll(pxOffset);
    _2d->Begin(state.Camera());

    st::Rectangle worldBounds((int32_t)pxOffset.X, (int32_t)pxOffset.Y, (int32_t)state.WorldSpace.WorldPxWidth, (int32_t)state.WorldSpace.WorldPxHeight);

    for (auto obj : Blueprint->Objects) {
        if (obj->Level > state.Level()) continue;
        if (obj->renderInfo.Layer == WorldObjectRenderLayer::DYNAMIC && !obj->renderInfo.Composited) {
            auto tilePosition = obj->DrawPosition();
            auto oPx = state.WorldSpace.GetScreenFromTile(tilePosition);
            obj->ValidateSprite(state);
            auto b = obj->Bounding();
            st::Rectangle offBound(b.X + (int32_t)oPx.X, b.Y + (int32_t)oPx.Y, b.Width, b.Height);
            if (!(offBound.X < worldBounds.Right() && worldBounds.X < offBound.Right() &&
                  offBound.Y < worldBounds.Bottom() && worldBounds.Y < offBound.Bottom())) continue;
            // Sprite placement truncates after adding its fractional tile offset.
            if (retain) MarkDynamic(offBound.Left() - worldBounds.X - 1, offBound.Top() - worldBounds.Y - 1,
                                    offBound.Right() - worldBounds.X + 1, offBound.Bottom() - worldBounds.Y + 1);
            _2d->OffsetPixel(oPx);
            _2d->OffsetTile(tilePosition);
            _2d->SetObjID(obj->ObjectID);
            obj->Draw(state);
        }
    }
    _2d->End();
    _2d->Target = nullptr;
}

}}
