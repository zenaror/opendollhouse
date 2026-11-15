// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/buy_mode.hpp"
#include "odh/animation.hpp"
#include "odh/ui_bitmap.hpp"
#include "original_control_panel.hpp"
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
