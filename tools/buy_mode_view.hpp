// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/buy_mode.hpp"
#include "odh/animation.hpp"
#include "odh/ui_bitmap.hpp"
#include "original_ui_art.hpp"
#include "live_panel_view.hpp"
#include "catalog_info_view.hpp"
#include "saved_object_renderer.hpp"
#include "catalog_object_thumbnail.hpp"
#include "object_pickup_view.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <string_view>
namespace odh_host {
using BuyTextDecoder=std::function<std::string(std::string_view,std::uint8_t)>;
struct BuyCatalogDiagnostics {std::size_t assets=0,listed=0,ambiguous=0,build_mode=0,missing_names=0,missing_icons=0;};
struct BuyCatalogResource {
    odh::DefinitionCandidate definition;
    std::uint16_t strings_id=0;
    // Raw OBJD words82..88 (including version): Hunger, Comfort, Hygiene, Bladder, Energy,
    // Fun, Room. A truncated/older definition is unknown, not seven zeroes.
    std::optional<std::array<std::int16_t,7>> ratings;
    std::optional<std::uint16_t> skill_flags; // Raw word89, including version.
};
// All metadata is read in one grouped pass per explicit resource. Icons remain
// identified by exact IFF+BMP_ chunk; only the current page is decoded later.
struct BuyModeCatalog {
    std::vector<odh::BuyCatalogItem> items;
    std::map<std::uint32_t,BuyCatalogResource> resources;
    BuyCatalogDiagnostics diagnostics;
};
BuyModeCatalog load_buy_mode_catalog(const std::filesystem::path& game_root,
    const std::vector<odh::DefinitionCandidate>&,const odh::DefinitionIndex&,
    std::uint8_t language,BuyTextDecoder={});
using BuyUiRect=odh::UiBitmapRect;
enum class OriginalPanelMode { live,buy,build,options };
enum class OriginalPanelAction { none,pause,rotate_camera_left,rotate_camera_right,zoom_in,zoom_out,walls_up,walls_down,level_up,level_down,walls_cutaway,options,live_needs,live_relationships,live_career,live_personality,live_interests,live_house,speed_normal,speed_fast,speed_ultra };
struct OriginalPanelState {bool paused=false,walls_down=false,build_buy_enabled=true;std::optional<std::int16_t> mood={};bool show_family_funds=true;
    // Optional capabilities come from actual host view state. Missing ones are
    // drawn disabled, with no invented floor/roof or simulation state.
    std::optional<bool> can_level_up,can_level_down,cutaway;
    bool options_enabled=false,options_open=false;
    std::array<bool,6> live_tabs{};
    LivePanelTab live_tab=LivePanelTab::needs;
    // Actual host multiplier from the pinned TS1 UI (normal1, fast3, ultra10).
    // Missing/invalid values have no live consumer; edit/modal callers omit it.
    std::optional<std::uint8_t> simulation_speed;
    // Presentation capture/hover use the same actual-multiplier identity.
    // They never change which speed the simulation is currently running.
    std::optional<std::uint8_t> pressed_simulation_speed,hovered_simulation_speed;
};
struct OriginalControlPanelLayout {
    float scale=1;
    BuyUiRect base,left_patch,panel_background,clock,budget,portraits,right_content;
    std::array<BuyUiRect,3> modes; // Live, Buy, Build, using original four-state art.
    std::array<BuyUiRect,8> needs; // Hunger/Comfort/Hygiene/Bladder then Energy/Fun/Social/Room.
    BuyUiRect needs_title,green_mood,red_mood,live_gadget,camera_patch,pause,rotate_left,rotate_right,zoom_in,zoom_out,walls_up,walls_down;
    BuyUiRect level_up,level_down,roof,walls_cutaway,camera,options;
    std::array<BuyUiRect,3> speeds;
    std::array<BuyUiRect,6> tabs; // Mood, relationship, job, personality, interests, house.
};
std::array<BuyUiRect,8> original_portrait_rects(const OriginalControlPanelLayout&,std::size_t residents);
OriginalControlPanelLayout original_control_panel_layout(std::uint32_t width,std::uint32_t height);
// One immutable native artwork owner shared by live and buy HUDs. All resources
// come from the selected game's UIGraphics.far; no screenshot/generated assets.
class OriginalControlPanelArt {
public:
    explicit OriginalControlPanelArt(const std::filesystem::path& game_root);
    const odh::BitmapImage& at(const std::string& archive_key)const;
    const odh::BitmapImage* find(const std::string& archive_key)const;
    void draw(odh::RasterFrame&,OriginalPanelMode,OriginalPanelState={})const;
    // Only controls with loaded original art and an enabled host capability
    // emit actions; unavailable tabs and speed controls remain disabled.
    OriginalPanelAction hit(const OriginalControlPanelLayout&,std::int32_t,std::int32_t,OriginalPanelState={})const;
    void draw_portrait(odh::RasterFrame&,BuyUiRect,bool selected,const odh::BitmapImage* head,bool authored_icon=false) const;
    void draw_button(odh::RasterFrame&,const std::string& archive_key,const BuyUiRect&,unsigned state=0)const;
private:
    OriginalUiArt art_;
};
struct BuyModeLayout {
    BuyUiRect panel,previous,next,cancel,rotate,sell,toggle;
    std::array<BuyUiRect,8> categories;
    std::vector<BuyUiRect> items;
    std::uint32_t viewport_width=0,viewport_height=0;
};
BuyModeLayout buy_mode_layout(std::uint32_t width,std::uint32_t height);
struct BuyModeText {
    std::array<std::string,8> categories;
    std::string cancel="Cancel",rotate="Rotate",sell="Sell",place="Choose a place",funds="Funds",pending="Placing object...",cannot_place="This object could not be placed here.",insufficient_funds="Not enough money.";
    std::array<std::string,14> query_labels; // Original UIText160: seven ratings, seven skill flags.
    CatalogInfoTextMeasure info_measure;
    CatalogInfoTextPainter info_painter;
    CatalogInfoScrollPainter info_scroll_painter;
    ObjectPickupText pickup_text;
};
using BuyModeTextPainter=std::function<void(odh::RasterFrame&,std::string_view,const BuyUiRect&,bool emphasis)>;
struct BuyModePreviewResult {bool sprite=false;std::string diagnostic;};
struct BuyModeExistingInfo {
    ObjectPickupContent content;
    std::vector<SavedObjectVisual> visuals;
};
// Asset-backed presentation edge, independent of X11/Pango/SDL. The host text
// painter handles each platform's fonts. The actual transaction stays outside.
class BuyModeView {
public:
    BuyModeView(std::filesystem::path game_root,BuyModeCatalog,const odh::DefinitionIndex&,BuyModeText={},
        std::shared_ptr<const OriginalControlPanelArt> shared_panel={});
    const BuyModeCatalog& catalog()const{return catalog_;}
    // Explicit UI update phase, OUTSIDE draw/VM callbacks. Replaces bounded
    // page icon cache; this is the only icon resource I/O after construction.
    void prepare_page(const odh::BuyModeController&);
    // Builds an isolated sprite layer without creating/moving any World entity
    // or running Init. New entries use their catalog's base Graphic0 artwork;
    // existing objects use the exact supplied live graphic/flags when known.
    BuyModePreviewResult prepare_preview(const odh::BuyModeController&,const odh::WorldProjection&,
        std::optional<std::array<float,3>> actual_tile_position=std::nullopt,
        std::function<std::array<float,3>(const odh::EntityPosition&)> part_graphics={});
    std::uint64_t preview_preparations()const{return preview_preparations_;}
    // Returns true only when the click belongs to the panel. World picking is
    // left to the host and continues through the portable controller methods.
    // Selection update only, never draw: prepare a full authored SPR2 group.
    void prepare_info(const odh::BuyModeController&);
    void prepare_existing_info(const BuyModeExistingInfo&);
    void clear_existing_info(){info_scroll_.reset();pickup_content_.reset();info_thumbnail_.clear();info_image_guid_.reset();}
    std::optional<ObjectPickupContent> pickup_content(const odh::BuyModeController&)const;
    const std::string& info_image_diagnostic()const{return info_image_diagnostic_;}
    std::uint64_t info_image_preparations()const{return info_image_preparations_;}
    bool click(odh::BuyModeController&,const BuyModeLayout&,std::int32_t,std::int32_t);
    // Original catalog information is opened by a catalog click, not hover.
    // Leaving the catalog hides the information without canceling placement.
    void pointer_motion(const BuyModeLayout&,std::int32_t,std::int32_t);
    // The host's delayed tooltip reads the exact visible item. Hovering never
    // opens its description or changes the placement holder.
    const odh::BuyCatalogItem* hover_item(const odh::BuyModeController&,
        const BuyModeLayout&,std::int32_t,std::int32_t)const;
    void hide_info(){info_visible_=false;info_scroll_.reset();}
    bool scroll_info(const odh::BuyModeController&,CatalogInfoScrollInput,const odh::WorldProjection&,int x,int y);
    int info_scroll_offset()const{return info_scroll_.offset();}
    void prepare_info_layout(const odh::BuyModeController&,const odh::WorldProjection&)const;
    bool info_visible(const odh::BuyModeController&)const;
    std::optional<CatalogInfoContent> info_content(const odh::BuyModeController&)const;
    bool info_contains(const odh::BuyModeController&,const odh::WorldProjection&,
                       std::int32_t,std::int32_t)const;
    void draw(odh::RasterFrame&,const odh::BuyModeController&,const odh::WorldProjection&,
              BuyModeTextPainter={},OriginalPanelState={})const;
private:
    std::filesystem::path root_;BuyModeCatalog catalog_;BuyModeText text_;
    std::shared_ptr<const OriginalControlPanelArt> panel_art_;
    CatalogInfoView info_view_;
    ObjectPickupView pickup_view_;
    std::optional<ObjectPickupContent> pickup_content_;
    const odh::DefinitionIndex& definitions_;
    CatalogObjectThumbnail info_thumbnail_;
    std::optional<std::uint32_t> info_image_guid_;
    std::string info_image_diagnostic_;
    std::uint64_t info_image_preparations_=0;
    bool info_visible_=false;
    CatalogInfoScrollState info_scroll_;
    std::map<std::uint32_t,odh::BitmapImage> icons_;
    std::vector<std::uint32_t> prepared_page_;
    SavedObjectRenderer preview_renderer_;
    std::optional<odh::CompositeFrame> preview_;
    std::optional<odh::WorldProjection> preview_camera_;
    std::optional<odh::BuyModeHeld> preview_selection_;
    std::optional<std::array<float,3>> preview_tile_position_;
    BuyModePreviewResult preview_result_;
    bool preview_key_valid_=false;
    std::optional<std::uint32_t> preview_group_guid_;
    std::optional<odh::BuyModePreviewGroup> preview_authored_group_;
    std::string preview_group_error_;
    std::uint64_t preview_preparations_=0;
};
}
