#pragma once
#include "saved_architecture_resources.hpp"
#include "saved_floor_renderer.hpp"
#include "saved_wall_renderer.hpp"
#include "odh/graphics_state.hpp"
#include "odh/saved_world.hpp"
#include "odh/terrain_surface.hpp"
#include <map>

struct SavedGraphicsLimits {
    std::size_t slot_tables=16,slot_records=4096;
};
// Explicit OBJD -> SLOT resolver shared by live saved previews with/without
// architecture. Definitions outlive this bounded cache. No World is owned.
class SavedGraphicsResources {
public:
    explicit SavedGraphicsResources(const odh::DefinitionIndex& definitions,SavedGraphicsLimits limits={})
        :definitions_(definitions),limits_(limits){}
    odh::GraphicsEntity entity(const odh::World&,std::int16_t id);
private:
    const odh::SlotTable* slots(const odh::Entity&);
    const odh::DefinitionIndex& definitions_;
    SavedGraphicsLimits limits_;
    std::map<std::uint32_t,odh::SlotTable> slots_;
    std::size_t records_=0;
};

struct SavedLotView {
    std::uint8_t level=1;
    bool walls_down=false;
    std::optional<odh::TerrainTheme> terrain_theme;
};
struct SavedLotFrame {
    odh::RasterFrame image;
    std::uint64_t floor_draws=0,floor_triangles=0,floor_texture_bytes=0;
    std::uint64_t terrain_triangles=0,terrain_reserved_bytes=0;
    std::uint64_t wall_layers=0,wall_junctions=0,wall_cached_frame_bytes=0;
    std::uint64_t wall_tested_samples=0,wall_shaded_samples=0;
};
// Borrow the live saved World and immutable definitions; both must outlive this
// host renderer. Architecture owns its bounded decoded snapshot. Rendering does
// not restore/clone a World, execute saved threads or change simulation state.
// Ground/floors -> opaque live avatar raster -> alpha/depth wall sprites. This
// order lets a nearer partially transparent wall blend over the avatar rather
// than against a previously captured background. Other avatars/objects are not
// submitted by this increment; their simulation state is retained in World.
class SavedLotRenderer {
public:
    SavedLotRenderer(SavedArchitectureResource,const odh::SavedWorld&,
        const odh::DefinitionIndex&,const std::string& floors,
        const std::optional<std::string>& walls,SavedLotView={});
    odh::GraphicsEntity avatar(std::int16_t id);
    SavedLotFrame render(const odh::WorldProjection&,const odh::RasterFrame& avatar);
    const SavedLotView& view() const{return view_;}
private:
    SavedArchitectureResource architecture_;
    const odh::SavedWorld& saved_;
    const odh::DefinitionIndex& definitions_;
    SavedGraphicsResources graphics_;
    std::map<std::int16_t,std::array<float,2>> group_centers_;
    SavedLotView view_;
    SavedFloorRenderer floors_;
    std::unique_ptr<SavedWallRenderer> walls_;
};
