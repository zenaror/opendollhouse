// SPDX-License-Identifier: MPL-2.0
#include "ui_hover.hpp"
#include <limits>
#include <stdexcept>

namespace odh_host {
namespace {
bool same_rect(UiHoverRect a,UiHoverRect b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
bool contains(UiHoverRect box,const std::array<std::int32_t,2>& pointer){
    return box.width>0&&box.height>0&&pointer[0]>=box.x&&pointer[1]>=box.y&&
        std::int64_t(pointer[0])<std::int64_t(box.x)+box.width&&
        std::int64_t(pointer[1])<std::int64_t(box.y)+box.height;
}
}
UiHover::UiHover(UiHoverConfig config):config_(config){
    if(!config_.text_bytes||config_.text_bytes>65536)
        throw std::invalid_argument("hover text budget must be in 1..65536 bytes");
}
void UiHover::clear(){target_.reset();last_ms_.reset();since_ms_=context_=selection_=0;shown_=false;}
bool UiHover::pending()const{return target_.has_value()&&!shown_;}
bool UiHover::refresh_due(std::uint64_t now)const{
    return pending()&&now>=since_ms_&&now-since_ms_>=config_.delay_ms;
}
std::optional<std::uint64_t> UiHover::next_transition_ms()const{
    if(!pending()||config_.delay_ms>std::numeric_limits<std::uint64_t>::max()-since_ms_)return {};
    return since_ms_+config_.delay_ms;
}
std::optional<UiHoverTarget> UiHover::update(std::uint64_t now,
        const std::optional<UiHoverTarget>& candidate,const UiHoverInput& input){
    const bool rollback=last_ms_&&now<*last_ms_;
    last_ms_=now;
    if(!input.enabled||!input.focused||input.modal||input.button_down||input.interacted||
       !input.pointer||!candidate||candidate->text.empty()||candidate->text.size()>config_.text_bytes||
       candidate->text.find('\0')!=std::string::npos||!contains(candidate->rect,*input.pointer)){
        target_.reset();shown_=false;return {};
    }
    const bool changed=rollback||!target_||context_!=input.context||selection_!=input.selection||
        target_->id!=candidate->id||target_->text!=candidate->text||!same_rect(target_->rect,candidate->rect);
    if(changed){
        target_=*candidate;since_ms_=now;context_=input.context;selection_=input.selection;shown_=false;
    }
    if(now-since_ms_<config_.delay_ms)return {};
    shown_=true;
    return target_;
}
}
