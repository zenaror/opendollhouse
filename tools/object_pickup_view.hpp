// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/bitmap.hpp"
#include "odh/definition_index.hpp"
#include "odh/object_trade.hpp"
#include "odh/ui_bitmap.hpp"
#include <functional>
#include <string_view>

namespace odh_host {
// Resolves the exact master CTSS even for definitions excluded from the buy
// catalog. Missing/ambiguous master or absent text returns nullopt. Changed
// identities, malformed resources and text bounds fail explicitly.
std::optional<std::string> resolve_object_pickup_name(const odh::DefinitionCandidate&,
    const odh::DefinitionIndex&,std::uint8_t language,
    std::function<std::string(std::string_view,std::uint8_t)> decoder={});
struct ObjectPickupContent {
    odh::ObjectTradeIdentity target;
    std::string name;
    std::optional<std::int32_t> refund;
    std::optional<bool> can_sell;
    const odh::BitmapImage* image=nullptr;
    bool image_rgba=false;
};
struct ObjectPickupText {
    std::array<std::string,3> messages;
    std::string unknown_value="Sale value unavailable.";
};
struct ObjectPickupLayout {odh::UiBitmapRect panel,image,message;};
ObjectPickupLayout object_pickup_layout(std::uint32_t width,std::uint32_t height);
std::string object_pickup_message(const ObjectPickupContent&,const ObjectPickupText&);
// This authorizes only a UI intent. The transaction consumer must revalidate
// identity, permission and refund against its current authoritative state.
bool object_pickup_sell_hit(const ObjectPickupLayout&,const ObjectPickupContent&,
    std::int32_t x,std::int32_t y);
using ObjectPickupTextPainter=std::function<void(odh::RasterFrame&,std::string_view,const odh::UiBitmapRect&)>;
class ObjectPickupView {
public:
    // Caller paints the current original Buy HUD first, without catalog icons.
    // The historical RT's DisposeBack entry is absent from current game data;
    // it is not used as an artwork source or substituted with invented assets.
    explicit ObjectPickupView(ObjectPickupText={});
    void draw(odh::RasterFrame&,const ObjectPickupContent&,ObjectPickupTextPainter={})const;
private:
    ObjectPickupText text_;
};
}
