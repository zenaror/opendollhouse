#pragma once
#include "saved_architecture_resources.hpp"
#include "odh/installed_lot_limits.hpp"
#include "odh/object_group.hpp"
#include "saved_floor_renderer.hpp"
#include "saved_wall_renderer.hpp"
#include "saved_object_renderer.hpp"
#include "room_lighting_view.hpp"
#include "odh/graphics_state.hpp"
#include "odh/room_map.hpp"
#include "odh/saved_world.hpp"
#include "odh/terrain_surface.hpp"
#include "odh/architecture_snapshot.hpp"
#include "odh/wall_appearance.hpp"
#include <tuple>
#include <filesystem>
#include <map>
#include <set>

struct SavedGraphicsLimits {
    std::size_t slot_tables=odh::installed_lot_limits().definition_catalogs,slot_records=4096;
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
    std::optional<odh::ArchitectureSnapshot> restored_architecture;
};
struct SavedLotView {
    std::uint8_t level=1;
    bool walls_down=false;
    std::optional<odh::TerrainTheme> terrain_theme;
};
struct SavedLotObjectIdentity {
    std::uint32_t guid=0;
    std::uint64_t generation=0;
};
struct SavedLotFrame {
    odh::RasterFrame image;
    std::uint32_t pick_width=0,pick_height=0;
    std::vector<std::uint16_t> avatar_pick_ids; // Separate per-pixel avatar token buffer when requested.
    std::vector<std::uint16_t> object_pick_ids; // <=1Mi pixels, zero is scenery.
    std::map<std::int16_t,SavedLotObjectIdentity> object_pick_identities; // Presented GUID/generation, <=4096.
    std::vector<std::uint16_t> ground_pick_ids; // Separate so the display image can be moved.
    std::uint64_t floor_draws=0,floor_triangles=0,floor_texture_bytes=0;
    std::uint64_t terrain_triangles=0,terrain_reserved_bytes=0;
    std::uint64_t wall_layers=0,wall_junctions=0,wall_cached_frame_bytes=0;
    std::uint64_t wall_tested_samples=0,wall_shaded_samples=0;
    std::uint64_t objects=0,object_layers=0,object_offscreen_layers=0,object_cached_frame_bytes=0;
    std::uint64_t object_tested_samples=0,object_shaded_samples=0;
    std::uint64_t object_hidden=0,object_above_level=0,object_outside=0,object_removed=0;
};
// A pre-rendered, screen-positioned resident layer. owner_ids marks pixels
// belonging to the resident; pick_id is the stable caller-selected token.
struct SavedLotAvatarLayer {
    odh::RasterFrame image;
    std::uint16_t pick_id=0;
    std::int16_t entity_id=0;
    std::uint64_t entity_generation=0;
};
// Host-authenticated World/GEXP origin, resolved to this catalog's resource paths.
// This binds artwork only; it creates no entity or execution state.
struct SavedLotRuntimeObject {
    std::int16_t id=0;
    std::uint64_t generation=0;
    std::uint32_t guid=0;
    std::string source,asset;
    std::uint16_t definition_chunk_id=0;
};
// A slot exported from the same animated pose used to draw this avatar.
// Position already includes its visual container and terrain height.
struct SavedLotAvatarSlotAnchor {
    std::int16_t actor=0;
    std::uint64_t generation=0;
    odh::EntityPosition logical_position{};
    std::size_t slot=0;
    std::array<float,3> tile_position{};
};
struct SavedObjectSelection {
    std::int16_t id;
    std::uint32_t guid;
    std::uint64_t generation;
    std::uint16_t tree_table_id;
};
struct SavedGroundSelection {
    std::uint32_t x,y;
    std::uint8_t level;
    std::uint16_t room;
};
// Borrow the live saved World and immutable definitions; both must outlive this
// host renderer. Architecture owns its bounded decoded snapshot. Rendering does
// not restore/clone a World, execute saved threads or change simulation state.
// Ground/floors -> opaque live avatar -> binary-alpha walls -> object sprites.
// Objects can blend over nearer opaque scene surfaces while depth rejects
// sprites behind them. Intra-object order retains SavedObjectRenderer's profile.
// Original dynamic sprite flags and group centers are snapshots. Runtime-created
// sprite flags, and all logical positions, graphics, directions and visibility
// are read from the borrowed World on every capture. New/reused object
// identities require an explicit binding refresh. Other avatars remain in World.
class SavedLotRenderer {
public:
    SavedLotRenderer(SavedArchitectureResource,const odh::SavedWorld&,
        const odh::DefinitionIndex&,const std::string& floors,
        const std::optional<std::string>& walls,SavedLotView={},
        const std::optional<SavedLotMaterialOptions>& materials = {});
    const std::optional<odh::ResolvedArchitectureAliases>& material_aliases() const{return aliases_;}
    const std::optional<odh::PreparedArchitecture>& prepared_architecture() const{return prepared_;}
    const SavedArchitectureResource& architecture_resource() const{return architecture_;}
    // Immutable derived runtime geometry, shared by rendering, collision and
    // routing. The original resource remains available only as provenance.
    const SavedArchitectureResource& runtime_architecture_resource() const {
        return prepared_render_?*prepared_render_:architecture_;
    }
    const odh::SavedArchitecture& render_architecture() const {
        return runtime_architecture_resource().architecture;
    }
    odh::GraphicsEntity avatar(std::int16_t id);
    std::uint8_t avatar_visual_level(const odh::GraphicsEntity&) const;
    // Runtime projection anchor including the same terrain/group-centre rules
    // used when drawing a saved object in this lot.
    odh::GraphicsEntity entity_graphics(std::int16_t id);
    std::array<float,3> object_look_target(std::int16_t id);
    std::array<float,3> preview_tile_position(const odh::EntityPosition&) const;
    // Available only when the immutable prepared architecture has unambiguous
    // floor/wall identities. Matches GetRoomAt room-zero fallback outside the
    // lot; it resolves room membership, not portal clearance.
    std::optional<std::uint16_t> runtime_room_at(const odh::EntityPosition&) const;
    bool runtime_room_map_available() const noexcept { return runtime_rooms_.has_value(); }
    const std::optional<odh::GeometricRoomMap>& runtime_room_map() const noexcept { return runtime_rooms_; }
    SavedLotFrame render(const odh::WorldProjection&,const odh::RasterFrame& avatar,
                         bool record_picks=false,bool include_objects=true);
    // Bounded to 64 residents and the normal raster viewport/pixel/sample
    // limits. Layers are merged in vector order; nearer depth wins and ties
    // retain the earlier layer. Avatar pick tokens are independent of object
    // and ground tokens.
    SavedLotFrame render(const odh::WorldProjection&,
                         const std::vector<SavedLotAvatarLayer>& avatar_layers,
                         bool record_picks=false,bool include_objects=true);
    // Last presented pixel ownership, revalidated against live generation,
    // definition GUID and visibility; out-of-bounds/scenery/stale IDs select none.
    std::optional<SavedObjectSelection> pick(const SavedLotFrame&,std::int32_t x,std::int32_t y) const;
    // Returns the visible ground cell only if its floor/terrain won this pixel
    // and a prepared room map can resolve the cell center.
    std::optional<SavedGroundSelection> pick_ground(const SavedLotFrame&,
                                                     std::int32_t x,std::int32_t y) const;
    // Atomically replace only the runtime-created artwork bindings (<=4096,
    // <=256 KiB resource names). Supply live, unique IDs with exact
    // World generation/GUID and uniquely indexed resource provenance. A runtime
    // binding shadows any original saved row with that ID after host-verified
    // removal/reuse (including the old row's terrain group metadata). Removed
    // entities stop drawing without a refresh; reused IDs require a new binding.
    // Reapply authenticated GEXP origins after restoring the borrowed World.
    // Live associations authenticate each origin against the exact catalog and
    // created binding or immutable imported row. game_root resolves portable
    // session source keys; it never permits basename matching.
    void restore_runtime_group_placements(const std::vector<odh::ObjectGroup>&);
    void set_runtime_objects(const std::vector<SavedLotRuntimeObject>&,
        const std::vector<odh::ObjectGroup>& authenticated_groups={},
        const std::filesystem::path& game_root={});
    void set_avatar_slot_anchors(const std::vector<SavedLotAvatarSlotAnchor>&);
    // Source-event appearance wins over re-deriving post-EP6 object flags.
    // Value is bounded and authenticated against the current World. Not save
    // storage: the host must restore/replay its actual event owner separately.
    void set_runtime_wall_appearance(std::optional<odh::WallAppearance>);
    // Actual position/style lifecycle owners invalidate after later changes.
    // A live pose/identity guard also rejects stale captured wall owners.
    void invalidate_runtime_wall_appearance() noexcept;
    const std::optional<odh::WallAppearance>& runtime_wall_appearance();

    const SavedLotView& view() const{return view_;}
    void set_view_level(std::uint8_t level){
        if(level<1||level>2)throw odh::FormatError("saved lot view level is outside supported profile");
        view_.level=level;
    }
    // Change presentation without replacing architecture/room-map objects
    // borrowed by simulation and placement services.
    bool can_set_walls_down(std::string* diagnostic=nullptr);
    bool set_walls_down(bool down);
    // Explicit basic-source profile. Host supplies the actual visual-room
    // classifier (including emitter/disabled states) for live identities.
    void set_room_lighting(std::optional<odh_host::RoomLightingView>,
        std::function<std::optional<std::uint16_t>(std::int16_t,std::uint64_t)> visual_room = {});
    // Moving previews hide only the matching presented image. The real entity,
    // collisions, containment and execution remain in the live World.
    void set_preview_hidden_object(std::optional<std::pair<std::int16_t,std::uint64_t>> value) {
        preview_hidden_objects_.clear();if(value)preview_hidden_objects_.insert(*value);
    }
    void set_preview_hidden_objects(const std::vector<std::pair<std::int16_t,std::uint64_t>>& values);
private:
    SavedArchitectureResource architecture_;
    std::optional<odh::ResolvedArchitectureAliases> aliases_;
    std::optional<odh::PreparedArchitecture> prepared_;
    std::optional<SavedArchitectureResource> prepared_render_;
    std::optional<odh::GeometricRoomMap> runtime_rooms_;
    std::optional<odh_host::RoomLightingView> lighting_;
    std::function<std::optional<std::uint16_t>(std::int16_t,std::uint64_t)> visual_room_;
    const odh::SavedWorld& saved_;
    const odh::DefinitionIndex& definitions_;
    SavedGraphicsResources graphics_;
    std::map<std::int16_t,std::array<float,2>> group_centers_;
    struct RestoredGroupPlacement {
        odh::ObjectGroup group;
        std::vector<std::pair<std::uint64_t,std::optional<odh::EntityPosition>>> members;
        std::array<std::int32_t,2> mean{};
    };
    std::map<std::int16_t,RestoredGroupPlacement> restored_group_placements_;
    std::map<std::int16_t,std::array<float,2>> runtime_group_centers_;
    std::map<std::int16_t,std::uint64_t> runtime_group_members_;
    std::map<std::int16_t,std::uint64_t> object_generations_;
    std::map<std::int16_t,SavedLotRuntimeObject> runtime_objects_;
    std::map<std::pair<std::int16_t,std::size_t>,SavedLotAvatarSlotAnchor> avatar_slot_anchors_;
    std::optional<odh::WallAppearance> runtime_wall_appearance_;
    struct WallAppearanceOwnerState {
        std::uint64_t generation=0;
        std::uint32_t guid=0;
        std::optional<odh::EntityPosition> position;
        std::optional<std::tuple<std::int16_t,std::size_t,bool>> attachment;
    };
    std::map<std::int16_t,WallAppearanceOwnerState> runtime_wall_owners_;
    bool runtime_wall_appearance_current() const;
    SavedLotView view_;
    std::set<std::pair<std::int16_t,std::uint64_t>> preview_hidden_objects_;
    std::map<std::pair<std::int16_t,std::uint64_t>,bool> cutaway_hidden_;
    std::optional<bool> cutaway_hidden_for(const odh::Entity&,const odh::GraphicsEntity*,bool) const;
    SavedFloorRenderer floors_;
    std::unique_ptr<SavedWallRenderer> walls_;
    SavedObjectRenderer objects_;
};
