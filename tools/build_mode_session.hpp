// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "buy_mode_view.hpp"
#include "build_mode_presentation.hpp"
#include "catalog_info_view.hpp"
#include "odh/architecture_edit.hpp"
namespace odh_host {
struct BuildModeMaterial {
    odh::ArchitectureEditMaterialKind kind=odh::ArchitectureEditMaterialKind::wall_style;
    std::uint16_t id=0;
    std::string name;
    std::int32_t price=0;
    std::uint16_t default_pattern=0; // Exact structural UI pattern from source provider.
    std::string description; // Authenticated material text in the chosen language.
};
struct BuildModePoint {double x=0,y=0;std::int8_t level=1;};
struct BuildModeInput {
    bool toggle=false,escape=false,cancel=false,press=false,release=false,motion=false;
    bool shift=false,control=false;
    std::int32_t x=0,y=0;
};
struct BuildModeServices {
    // Continuous logical tile coordinates on the actual selected floor plane,
    // not object positions or quantized ground-pick IDs. No geometry is edited.
    std::function<std::optional<BuildModePoint>(std::int32_t,std::int32_t)> ground_pick;
};
struct BuildModeText {
    std::array<std::string,7> tools{{"Wall line","Remove wall","Wall rectangle","Wallpaper","Wallpaper fill","Floor rectangle","Floor fill"}};
    std::string cancel="Cancel",pending="Applying changes...",failure="These changes could not be applied.";
    CatalogInfoTextMeasure info_measure;
    CatalogInfoTextPainter info_painter;
    CatalogInfoScrollPainter info_scroll_painter;
};
struct BuildModeLayout {
    BuyUiRect panel,toggle,cancel,previous,next;
    std::array<BuyUiRect,3> categories;
    std::array<BuyUiRect,7> tools;
    std::vector<BuyUiRect> materials;
};
BuildModeLayout build_mode_layout(std::uint32_t,std::uint32_t);
using BuildModeIconLoader=std::function<std::optional<odh::BitmapImage>(odh::ArchitectureEditMaterialKind,std::uint16_t)>;
// Complete seven-command input consumer. Materials are authenticated prepared-
// domain descriptors from the host's source reader, never raw saved aliases.
// Ordered press/motion/release events may be delivered in the same host poll.
class BuildModeSession {
public:
    BuildModeSession(const std::filesystem::path& game_root,std::shared_ptr<const OriginalControlPanelArt>,
        std::vector<BuildModeMaterial>,BuildModeText={},BuildModeIconLoader={});
    void open();
    // Focus loss/event overflow cancels only an unpublished gesture. It never
    // exits the mode, restores simulation speed, or reopens a pending command.
    void abort_gesture();
    bool active()const{return active_;}
    bool scroll_info(CatalogInfoScrollInput,const odh::WorldProjection&,int x,int y);
    int info_scroll_offset()const{return info_scroll_.offset();}
    bool pending()const{return pending_;}
    bool drawing()const{return drawing_;}
    void select_tool(odh::ArchitectureEditType);
    bool select_material(odh::ArchitectureEditMaterialKind,std::uint16_t);
    std::optional<std::vector<odh::ArchitectureEditCommand>> handle(const BuildModeInput&,
        const odh::WorldProjection&,const BuildModeServices&);
    void reject(std::string);
    void draw(odh::RasterFrame&,const odh::WorldProjection&,BuyModeTextPainter={},OriginalPanelState={})const;
    const std::vector<odh::ArchitectureEditCommand>& preview_commands()const{return preview_;}
    const std::string& diagnostic()const{return diagnostic_;}
    odh::ArchitectureEditType tool()const{return tool_;}
    BuildModePresentationState presentation_state()const;
    // Exact, all-or-nothing catalog validation; false leaves this session
    // unchanged. No fallback IDs or page clamping. Never restores activity,
    // gestures, pending commands, previews, diagnostics or catalog popups.
    // Rejects use while drawing/pending; call open() for a new session afterward.
    bool restore(const BuildModePresentationState&);
    const BuildModeMaterial* selected_material()const;
    // Visual consultation only; absence means no open catalog popup.
    std::optional<CatalogInfoContent> catalog_info()const;
private:
    odh::ArchitectureEditMaterialKind material_kind()const;
    bool panel_input(const BuildModeInput&,const BuildModeLayout&);
    void prepare_page();
    void update_preview(const BuildModePoint&,const BuildModeInput&,std::uint8_t rotation);
    void cancel();
    std::shared_ptr<const OriginalControlPanelArt> panel_;
    CatalogInfoView info_view_;
    CatalogInfoScrollState info_scroll_;
    std::vector<BuildModeMaterial> materials_;
    BuildModeText text_;BuildModeIconLoader icon_loader_;
    std::array<std::optional<std::uint16_t>,3> selected_;
    std::array<odh::BitmapImage,3> category_art_;
    std::map<std::pair<odh::ArchitectureEditMaterialKind,std::uint16_t>,odh::BitmapImage> icons_;
    std::vector<const BuildModeMaterial*> page_items_;
    std::optional<BuildModePoint> origin_;
    std::vector<odh::ArchitectureEditCommand> preview_;
    odh::ArchitectureEditType tool_=odh::ArchitectureEditType::wall_line;
    std::size_t page_=0;
    bool active_=false,pending_=false,drawing_=false,info_open_=false;
    std::string diagnostic_;
};
}
