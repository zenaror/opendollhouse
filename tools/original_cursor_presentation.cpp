// SPDX-License-Identifier: MPL-2.0
#include "original_cursor_presentation.hpp"
#include <iostream>
namespace odh_host {
OriginalCursorPresentation::OriginalCursorPresentation(const std::filesystem::path& game,LinuxWindow& window)
    :bank_(game),window_(window){(void)select(OriginalCursorAsset::Arrow);}
bool OriginalCursorPresentation::select(OriginalCursorAsset asset){
    const auto index=static_cast<std::size_t>(asset);
    if(index>=original_cursor_count)throw odh::FormatError("cursor selection outside original bank");
    if(selected_==asset)return true;
    if(!attempted_[index]){
        attempted_[index]=true;
        const auto& resource=bank_.at(asset);
        if(!resource.images.empty())handles_[index]=window_.create_cursor(resource.images.front());
        if(!handles_[index])std::clog<<"OpenDollhouse: native cursor unsupported: "<<original_cursor_filename(asset)<<'\n';
    }
    if(handles_[index]){window_.use_cursor(handles_[index]);selected_=asset;return true;}
    if(asset!=OriginalCursorAsset::Arrow)(void)select(OriginalCursorAsset::Arrow);
    else {window_.use_cursor({});selected_.reset();}
    return false;
}
OriginalCursorSelection OriginalCursorPresentation::update(const OriginalCursorPolicyInput& input){
    const auto request=select_original_cursor(input);(void)select(request.asset);return request;
}
}
