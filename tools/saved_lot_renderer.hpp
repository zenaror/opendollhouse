#pragma once
#include "saved_architecture_resources.hpp"
#include "saved_floor_renderer.hpp"
#include "saved_wall_renderer.hpp"
#include "saved_object_renderer.hpp"
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

// Explicit opt-in: resolve selected map ID1 (fallback ID0), prepare 64x64 and
// crop terrain alongside material grids. The constructor copies bounded metadata.
struct SavedLotMaterialOptions {
    odh::ArchitectureResourceCatalog catalog;
    odh::ArchitecturePreparationOptions preparation;
    odh::ArchitectureAliasResolverLimits alias_limits;
    odh::ArchitecturePreparationLimits preparation_limits;
};
struct SavedLotView {
    std::uint8_t level=1;
    bool walls_down=false;
    std::optional<odh::TerrainTheme> terrain_theme;
};
struct SavedLotFrame {
    odh::RasterFrame image;
    std::uint32_t pick_width=0,pick_height=0;
    std::vector<std::uint16_t> object_pick_ids; // <=1Mi pixels, zero is scenery.
    std::uint64_t floor_draws=0,floor_triangles=0,floor_texture_bytes=0;
    std::uint64_t terrain_triangles=0,terrain_reserved_bytes=0;
    std::uint64_t wall_layers=0,wall_junctions=0,wall_cached_frame_bytes=0;
    std::uint64_t wall_tested_samples=0,wall_shaded_samples=0;
    std::uint64_t objects=0,object_layers=0,object_cached_frame_bytes=0;
    std::uint64_t object_tested_samples=0,object_shaded_samples=0;
    std::uint64_t object_hidden=0,object_above_level=0,object_outside=0,object_removed=0;
};
struct SavedObjectSelection {
    std::int16_t id;
    std::uint32_t guid;
    std::uint64_t generation;
    std::uint16_t tree_table_id;
};
// Borrow the live saved World and immutable definitions; both must outlive this
// host renderer. Architecture owns its bounded decoded snapshot. Rendering does
// not restore/clone a World, execute saved threads or change simulation state.
// Ground/floors -> opaque live avatar -> binary-alpha walls -> object sprites.
// Objects can blend over nearer opaque scene surfaces while depth rejects
// sprites behind them. Intra-object order retains SavedObjectRenderer's profile.
// Dynamic sprite flags and group centers are snapshots; live logical position,
// graphic, direction and visibility are read on every capture. New/reused object
// identities require a renderer refresh. Other avatars remain in World.
class SavedLotRenderer {
public:
    SavedLotRenderer(SavedArchitectureResource,const odh::SavedWorld&,
        const odh::DefinitionIndex&,const std::string& floors,
        const std::optional<std::string>& walls,SavedLotView={},
        const std::optional<SavedLotMaterialOptions>& materials = {});
    const std::optional<odh::ResolvedArchitectureAliases>& material_aliases() const{return aliases_;}
    const std::optional<odh::PreparedArchitecture>& prepared_architecture() const{return prepared_;}
    odh::GraphicsEntity avatar(std::int16_t id);
    SavedLotFrame render(const odh::WorldProjection&,const odh::RasterFrame& avatar,bool record_picks=false);
    // Last presented pixel ownership, revalidated against live generation,
    // definition GUID and visibility; out-of-bounds/scenery/stale IDs select none.
    std::optional<SavedObjectSelection> pick(const SavedLotFrame&,std::int32_t x,std::int32_t y) const;
    const SavedLotView& view() const{return view_;}
private:
    SavedArchitectureResource architecture_;
    std::optional<odh::ResolvedArchitectureAliases> aliases_;
    std::optional<odh::PreparedArchitecture> prepared_;
    std::optional<odh::SavedArchitecture> prepared_render_;
    const odh::SavedArchitecture& render_architecture() const {
        return prepared_render_?*prepared_render_:architecture_.architecture;
    }
    const odh::SavedWorld& saved_;
    const odh::DefinitionIndex& definitions_;
    SavedGraphicsResources graphics_;
    std::map<std::int16_t,std::array<float,2>> group_centers_;
    std::map<std::int16_t,std::uint64_t> object_generations_;
    SavedLotView view_;
    SavedFloorRenderer floors_;
    std::unique_ptr<SavedWallRenderer> walls_;
    SavedObjectRenderer objects_;
};
