// SPDX-License-Identifier: MPL-2.0
#include "buy_mode_session.hpp"
namespace odh_host {
using namespace odh;
BuyModeSession::BuyModeSession(std::filesystem::path root,std::vector<DefinitionCandidate> candidates,
    const DefinitionIndex& definitions,std::uint8_t language,
    std::shared_ptr<const OriginalControlPanelArt> panel,BuyModeText text,BuyTextDecoder decoder)
    :root_(std::move(root)),candidates_(std::move(candidates)),definitions_(definitions),language_(language),
     panel_(std::move(panel)),text_(std::move(text)),decoder_(std::move(decoder)){}
void BuyModeSession::ensure_loaded(){
    if(controller_)return;
    auto catalog=load_buy_mode_catalog(root_,candidates_,definitions_,language_,decoder_);
    auto controller=std::make_unique<BuyModeController>(std::move(catalog.items),16);
    auto view=std::make_unique<BuyModeView>(root_,std::move(catalog),definitions_,text_,panel_);
    controller_=std::move(controller);view_=std::move(view);
    std::vector<DefinitionCandidate>().swap(candidates_);
}
void BuyModeSession::open(std::optional<std::int32_t> budget){ensure_loaded();controller_->open(budget);view_->hide_info();view_->prepare_page(*controller_);}
bool BuyModeSession::scroll_info(CatalogInfoScrollInput input,const WorldProjection& camera,int x,int y){
    return active()&&view_->scroll_info(*controller_,input,camera,x,y);
}
bool BuyModeSession::active()const{return controller_&&controller_->active();}
void BuyModeSession::reject(std::string diagnostic){if(controller_)controller_->command_rejected(std::move(diagnostic));}
std::optional<BuyModeCommand> BuyModeSession::handle(const BuyModeInput& input,const WorldProjection& camera,
    std::optional<std::int32_t> budget,const BuyModeServices& services){
    const auto layout=buy_mode_layout(camera.width,camera.height);
    if(!active()){
        if(!input.toggle&&!(input.click&&layout.toggle.contains(input.x,input.y)))return std::nullopt;
        open(budget);
        controller_->set_page_size(layout.items.size());view_->prepare_page(*controller_);
        return std::nullopt; // Opening click never clicks through to a World object.
    }
    controller_->set_budget(budget);controller_->set_page_size(layout.items.size());
    if(input.toggle){view_->hide_info();controller_->close();}
    else if(input.escape||input.cancel){view_->hide_info();controller_->cancel();}
    else {
        if(input.rotate)controller_->rotate(input.rotate);
        if(input.sell)controller_->sell();
        // Compute the currently drawn panel before handling motion/selection.
        // A click inside it cannot publish a placement into the world behind it.
        const bool in_info=view_->info_contains(*controller_,camera,input.x,input.y);
        bool panel_click=in_info;
        if((input.motion||input.click)&&!in_info)view_->pointer_motion(layout,input.x,input.y);
        if(input.click&&!in_info)panel_click=view_->click(*controller_,layout,input.x,input.y);
        // Both the catalog rectangle and opaque left original panel reserve
        // pointer input. Their empty portions cannot select/place into the lot.
        const auto common=original_control_panel_layout(camera.width,camera.height);
        const bool panel=layout.panel.contains(input.x,input.y)||common.left_patch.contains(input.x,input.y)||
            layout.cancel.contains(input.x,input.y)||layout.rotate.contains(input.x,input.y)||layout.sell.contains(input.x,input.y);
        if(active()&&!panel&&!panel_click&&controller_->phase()!=BuyModePhase::awaiting_command){
            if(controller_->held()){
                if(input.motion||input.click){
                    auto point=services.ground_pick?services.ground_pick(input.x,input.y):std::nullopt;
                    controller_->hover(point);
                }
                if(input.click)controller_->place();
            }else if(input.click&&services.object_pick){
                if(const auto picked=services.object_pick(input.x,input.y))if(controller_->select_existing(*picked)){
                    view_->clear_existing_info();
                    BuyModeExistingInfo info;info.content.target=picked->identity;
                    if(services.existing_info)if(auto prepared=services.existing_info(picked->identity))
                        if(prepared->content.target.id==picked->identity.id&&prepared->content.target.generation==picked->identity.generation)
                            info=std::move(*prepared);
                    view_->prepare_existing_info(info);
                }
            }
        }
    }
    if(active()){
        view_->prepare_page(*controller_);
        view_->prepare_info(*controller_);
        view_->prepare_info_layout(*controller_,camera);
        std::optional<std::array<float,3>> visual;
        if(controller_->held()&&controller_->held()->position&&services.visual_tile_position)
            visual=services.visual_tile_position(*controller_->held()->position);
        preview_diagnostic_=view_->prepare_preview(*controller_,camera,visual,services.visual_tile_position).diagnostic;
    }else {
        (void)view_->prepare_preview(*controller_,camera);
        preview_diagnostic_.clear();
    }
    return controller_->take_command();
}
void BuyModeSession::draw(RasterFrame& frame,const WorldProjection& camera,BuyModeTextPainter text,OriginalPanelState state)const{
    if(active())view_->draw(frame,*controller_,camera,std::move(text),state);
}
}
