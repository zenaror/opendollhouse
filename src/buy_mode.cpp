// SPDX-License-Identifier: MPL-2.0
#include "odh/buy_mode.hpp"
#include <algorithm>
#include <set>
namespace odh {
namespace {
bool pose_valid(const EntityPosition& p){return p.x>=INT16_MIN&&p.x<=INT16_MAX&&p.y>=INT16_MIN&&p.y<=INT16_MAX&&p.level>=1&&p.level<=127&&p.direction<=7;}
}
std::vector<EntityPosition> buy_mode_group_positions(const BuyModePreviewGroup& group,const EntityPosition& anchor){
    if(!pose_valid(anchor)||group.parts.empty()||group.parts.size()>64||group.base_index>=group.parts.size())
        throw FormatError("buy preview group identity/anchor outside bounds");
    const auto word=[](std::int64_t value){const auto bits=static_cast<std::uint16_t>(value);
        return static_cast<std::int16_t>(bits<32768?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536);};
    const auto level=[](std::int64_t value){const auto bits=static_cast<std::uint8_t>(value);
        return static_cast<std::int16_t>(bits<128?static_cast<std::int16_t>(bits):static_cast<std::int16_t>(bits)-256);};
    const auto& base=group.parts[group.base_index];std::vector<EntityPosition> result;result.reserve(group.parts.size());
    const bool native=group.native_head_heading_delta.has_value();
    if(native!=group.native_selected_heading.has_value()||
       (native&&(*group.native_head_heading_delta>7||*group.native_selected_heading>7||!base.native||
                 base.native->heading_delta||!base.native->current)))
        throw FormatError("buy native preview lacks exact selected/head heading authority");
    std::size_t flags=0;
    for(const auto& part:group.parts){
        if(!part.guid||part.dynamic_sprite_flags.size()>4096-flags)throw FormatError("buy preview part resource/flags outside bounds");
        flags+=part.dynamic_sprite_flags.size();
        if(part.native.has_value()!=native||(part.native&&(part.native->heading_delta>7||!part.native->current)))
            throw FormatError("buy native preview has partial or invalid native fields");
        if(native){
            const bool unchanged=anchor.direction==*group.native_selected_heading;
            const auto& a=unchanged?*part.native->current:part.native->authored;
            const auto& b=unchanged?*base.native->current:base.native->authored;
            const auto heading=std::uint8_t((anchor.direction+*group.native_head_heading_delta)&7U);
            const auto dx=std::int64_t(a.x)-b.x,dy=std::int64_t(a.y)-b.y;
            std::int64_t x=0,y=0;
            if(unchanged){x=dx;y=dy;}
            else switch(heading){case 0:x=dx;y=dy;break;case 2:x=-dy;y=dx;break;
                case 4:x=-dx;y=-dy;break;case 6:x=dy;y=-dx;break;}
            // Native 32-bit intermediate wrap has identical low coordinate
            // bits after tile*16 and final word/byte publication. Wide math
            // avoids C++ signed overflow, including full int32 source fields.
            result.push_back({word(std::int64_t(anchor.x)+x*16),word(std::int64_t(anchor.y)+y*16),
                level(std::int64_t(anchor.level)+std::int64_t(a.level)-b.level),
                std::uint8_t((anchor.direction+part.native->heading_delta)&7U)});
            continue;
        }
        auto dx=std::int32_t(part.offset_x)-base.offset_x,dy=std::int32_t(part.offset_y)-base.offset_y;const auto x=dx;
        switch(anchor.direction){case 2:dx=-dy;dy=x;break;case 4:dx=-dx;dy=-dy;break;case 6:dx=dy;dy=-x;break;default:break;}
        result.push_back({word(anchor.x+dx),word(anchor.y+dy),level(anchor.level+part.offset_level),anchor.direction});
    }return result;
}
BuyModeController::BuyModeController(std::vector<BuyCatalogItem> items,std::size_t page_size):catalog_(std::move(items)){
    if(catalog_.size()>8192)throw FormatError("buy catalog exceeds bounded metadata count");
    std::set<std::uint32_t> guids;std::size_t text=0;
    for(const auto& item:catalog_){if(!item.guid||item.category>7||!guids.insert(item.guid).second||item.name.size()>4096||item.description.size()>16384||
        item.name.size()+item.description.size()>4*1024*1024-text)throw FormatError("buy catalog identity or text is invalid");text+=item.name.size()+item.description.size();}
    // Simitone UIBuyBrowsePanel504: price ordering is stable for equal prices.
    std::stable_sort(catalog_.begin(),catalog_.end(),[](const auto& a,const auto& b){return a.price<b.price;});set_page_size(page_size);
}
void BuyModeController::open(std::optional<std::int32_t> budget){if(phase_==BuyModePhase::awaiting_command)return;budget_=budget;held_.reset();pending_.reset();issue_=BuyModeIssue::none;diagnostic_.clear();phase_=BuyModePhase::browsing;}
void BuyModeController::close(){if(phase_==BuyModePhase::awaiting_command)return;held_.reset();pending_.reset();phase_=BuyModePhase::closed;issue_=BuyModeIssue::none;diagnostic_.clear();}
void BuyModeController::cancel(){if(phase_==BuyModePhase::awaiting_command)return;if(held_){held_.reset();phase_=BuyModePhase::browsing;}else phase_=BuyModePhase::closed;issue_=BuyModeIssue::none;diagnostic_.clear();}
void BuyModeController::set_page_size(std::size_t n){if(!n||n>32)throw FormatError("buy catalog page must contain 1..32 items");page_size_=n;page_=std::min(page_,page_count()-1);}
std::size_t BuyModeController::page_count()const{const auto n=static_cast<std::size_t>(std::count_if(catalog_.begin(),catalog_.end(),[&](const auto& item){return item.category==category_;}));return std::max<std::size_t>(1,(n+page_size_-1)/page_size_);}
void BuyModeController::category(std::uint8_t value){if(value>7||phase_!=BuyModePhase::browsing)return;category_=value;page_=0;issue_=BuyModeIssue::none;}
void BuyModeController::page(int delta){if(phase_!=BuyModePhase::browsing)return;const auto last=static_cast<std::int64_t>(page_count()-1);page_=static_cast<std::size_t>(std::clamp(static_cast<std::int64_t>(page_)+delta,std::int64_t{0},last));}
std::vector<const BuyCatalogItem*> BuyModeController::visible()const{std::vector<const BuyCatalogItem*> result;std::size_t skipped=0;for(const auto& item:catalog_)if(item.category==category_){if(skipped++<page_*page_size_)continue;if(result.size()==page_size_)break;result.push_back(&item);}return result;}
const BuyCatalogItem* BuyModeController::find(std::uint32_t guid)const{const auto it=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto& item){return item.guid==guid;});return it==catalog_.end()?nullptr:&*it;}
bool BuyModeController::select(std::size_t index){if(phase_!=BuyModePhase::browsing)return false;const auto items=visible();if(index>=items.size())return false;
    BuyModeHeld held;held.guid=items[index]->guid;held_=std::move(held);phase_=BuyModePhase::holding_purchase;issue_=BuyModeIssue::none;diagnostic_.clear();return true;}
bool BuyModeController::select_existing(const BuyModeExistingObject& item){if(phase_!=BuyModePhase::browsing||item.identity.id<=0||!item.identity.generation||!item.guid||!pose_valid(item.position)||item.dynamic_sprite_flags.size()>4096){issue_=BuyModeIssue::stale_identity;return false;}
    if(item.preview_group)try{(void)buy_mode_group_positions(*item.preview_group,item.position);}catch(const FormatError&){issue_=BuyModeIssue::stale_identity;return false;}
    held_=BuyModeHeld{item.guid,item.identity,item.position,item.position.direction,item.graphic,item.dynamic_sprite_flags,BuyPreviewValidity::unknown,item.preview_group};phase_=BuyModePhase::holding_existing;issue_=BuyModeIssue::none;diagnostic_.clear();return true;}
void BuyModeController::hover(std::optional<EntityPosition> p,BuyPreviewValidity validity){if(!held_||phase_==BuyModePhase::awaiting_command)return;if(p&&!pose_valid(*p))p.reset();if(p)p->direction=held_->direction;held_->position=p;held_->validity=validity;issue_=BuyModeIssue::none;diagnostic_.clear();}
void BuyModeController::rotate(int quarters){if(!held_||phase_==BuyModePhase::awaiting_command)return;const auto d=(static_cast<std::int64_t>(held_->direction)+2*static_cast<std::int64_t>(quarters))%8;held_->direction=static_cast<std::uint8_t>(d<0?d+8:d);if(held_->position)held_->position->direction=held_->direction;held_->validity=BuyPreviewValidity::unknown;issue_=BuyModeIssue::none;diagnostic_.clear();}
bool BuyModeController::emit(BuyModeCommandKind kind){if(!held_||phase_==BuyModePhase::awaiting_command||pending_)return false;
    if(kind!=BuyModeCommandKind::sell&&(!held_->position||held_->validity==BuyPreviewValidity::blocked)){issue_=BuyModeIssue::no_ground;return false;}
    if(kind==BuyModeCommandKind::purchase){const auto* item=find(held_->guid);if(!item)return false;if(budget_&&(*budget_<0||item->price>static_cast<std::uint32_t>(*budget_))){issue_=BuyModeIssue::insufficient_funds;return false;}}
    if(kind==BuyModeCommandKind::sell&&!held_->existing)return false;
    pending_=BuyModeCommand{kind,held_->guid,held_->existing.value_or(ObjectTradeIdentity{}),held_->position.value_or(EntityPosition{})};phase_=BuyModePhase::awaiting_command;return true;}
bool BuyModeController::place(){return emit(held_&&held_->existing?BuyModeCommandKind::move:BuyModeCommandKind::purchase);}
bool BuyModeController::sell(){return emit(BuyModeCommandKind::sell);}
std::optional<BuyModeCommand> BuyModeController::take_command(){auto result=pending_;pending_.reset();return result;}
void BuyModeController::command_rejected(std::string diagnostic){if(phase_!=BuyModePhase::awaiting_command)return;if(diagnostic.size()>4096)diagnostic.resize(4096);diagnostic_=std::move(diagnostic);issue_=BuyModeIssue::operation_failed;phase_=held_&&held_->existing?BuyModePhase::holding_existing:BuyModePhase::holding_purchase;pending_.reset();}
}
