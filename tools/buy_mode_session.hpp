// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "buy_mode_view.hpp"
namespace odh_host {
struct BuyModeInput {
    bool toggle=false,escape=false,cancel=false,sell=false,click=false,motion=false;
    int rotate=0;
    std::int32_t x=0,y=0;
};
struct BuyModeServices {
    std::function<std::optional<odh::EntityPosition>(std::int32_t,std::int32_t)> ground_pick;
    std::function<std::optional<odh::BuyModeExistingObject>(std::int32_t,std::int32_t)> object_pick;
    std::function<std::array<float,3>(const odh::EntityPosition&)> visual_tile_position;
    // Called once on pickup, outside draw. Must authenticate target generation,
    // group visuals and optional refund/permission using current source state.
    std::function<std::optional<BuyModeExistingInfo>(odh::ObjectTradeIdentity)> existing_info;
};
// Owns the real catalog/holder presentation consumer. Host callbacks are only
// read-only picking/terrain facts; command execution and checkpoint reload stay
// with the existing authenticated transaction service.
class BuyModeSession {
public:
    BuyModeSession(std::filesystem::path game_root,std::vector<odh::DefinitionCandidate>,
        const odh::DefinitionIndex&,std::uint8_t language,
        std::shared_ptr<const OriginalControlPanelArt> panel={},BuyModeText={},BuyTextDecoder={});
    void open(std::optional<std::int32_t> budget);
    std::optional<odh::BuyModeCommand> handle(const BuyModeInput&,const odh::WorldProjection&,
        std::optional<std::int32_t> budget,const BuyModeServices&);
    // Call once before camera input. Positive values advance the document;
    // consumed at either bound too. Wheel is pointer-scoped, pages use focus.
    bool scroll_info(CatalogInfoScrollInput,const odh::WorldProjection&,int x,int y);
    int info_scroll_offset()const{return view_?view_->info_scroll_offset():0;}
    bool active()const;
    void reject(std::string diagnostic);
    void draw(odh::RasterFrame&,const odh::WorldProjection&,BuyModeTextPainter={},OriginalPanelState={})const;
    const odh::BuyModeController* controller()const{return controller_.get();}
    const odh::BuyCatalogItem* hover_item(const BuyModeLayout& layout,int x,int y)const{
        return controller_&&view_?view_->hover_item(*controller_,layout,x,y):nullptr;
    }
    std::optional<ObjectPickupContent> pickup_content()const{return controller_&&view_?view_->pickup_content(*controller_):std::nullopt;}
    const std::string& preview_diagnostic()const{return preview_diagnostic_;}
    bool info_visible()const{return controller_&&view_&&view_->info_visible(*controller_);}
    std::optional<CatalogInfoContent> info_content()const{return controller_&&view_?view_->info_content(*controller_):std::nullopt;}
    const std::string& info_image_diagnostic()const{static const std::string empty;return view_?view_->info_image_diagnostic():empty;}
    std::uint64_t info_image_preparations()const{return view_?view_->info_image_preparations():0;}
    std::uint64_t preview_preparations()const{return view_?view_->preview_preparations():0;}
private:
    void ensure_loaded();
    std::filesystem::path root_;
    std::vector<odh::DefinitionCandidate> candidates_;
    const odh::DefinitionIndex& definitions_;
    std::uint8_t language_;
    std::shared_ptr<const OriginalControlPanelArt> panel_;
    BuyModeText text_;BuyTextDecoder decoder_;
    std::unique_ptr<odh::BuyModeController> controller_;
    std::unique_ptr<BuyModeView> view_;
    std::string preview_diagnostic_;
};
}
