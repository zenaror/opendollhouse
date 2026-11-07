// SPDX-License-Identifier: MPL-2.0
#include "community_lot_edit_session.hpp"
#include <stdexcept>
#include <utility>
#include <type_traits>

namespace odh_host {
static_assert(std::is_nothrow_move_assignable<CommunityLotDocument>::value,
    "community document commits require non-throwing owned moves");
namespace {
std::vector<std::uint8_t> canonical(const CommunityLotDocument& document){
    return odh::encode_session_archive(encode_community_lot_document(document));
}
}
CommunityLotEditSession::CommunityLotEditSession(CommunityLotDocument baseline,
        std::optional<CommunityLotCheckpointHead> head,CommunityLotEditPublisher publish)
    :baseline_(std::move(baseline)),current_(baseline_),
     baseline_bytes_(canonical(baseline_)),current_bytes_(baseline_bytes_),
     head_(std::move(head)),publish_(std::move(publish)){
    if(!publish_)throw std::invalid_argument("community edit session has no publisher");
}
void CommunityLotEditSession::stage(CommunityLotDocument candidate){
    if(publishing_)throw odh::FormatError("community edit publication cannot be re-entered");
    if(!same_community_lot(candidate.identity,baseline_.identity))
        throw odh::FormatError("community edit draft changes its authenticated lot identity");
    auto bytes=canonical(candidate);
    current_=std::move(candidate);current_bytes_=std::move(bytes);
    dirty_=current_bytes_!=baseline_bytes_;
}
void CommunityLotEditSession::stage_architecture(odh::ArchitectureSnapshot architecture){
    if(publishing_)throw odh::FormatError("community edit publication cannot be re-entered");
    auto candidate=current_;candidate.architecture=std::move(architecture);stage(std::move(candidate));
}
std::optional<CommunityLotEditPublication> CommunityLotEditSession::save(){
    if(publishing_)throw odh::FormatError("community edit publication cannot be re-entered");
    if(!dirty_)return {};
    // Prepare all owned copies before publication. After the host commits, the
    // remaining moves/trivial assignments cannot require allocation.
    auto next_baseline=current_;auto next_bytes=current_bytes_;
    struct Publishing {bool& active;explicit Publishing(bool& value):active(value){active=true;}~Publishing(){active=false;}} guard(publishing_);
    auto published=publish_(head_,current_);
    baseline_=std::move(next_baseline);baseline_bytes_=std::move(next_bytes);
    head_=published.head;dirty_=false;
    return std::optional<CommunityLotEditPublication>{std::move(published)};
}
void CommunityLotEditSession::discard(){
    if(publishing_)throw odh::FormatError("community edit publication cannot be re-entered");
    auto candidate=baseline_;auto bytes=baseline_bytes_;
    current_=std::move(candidate);current_bytes_=std::move(bytes);dirty_=false;
}
}
