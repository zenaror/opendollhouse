// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "original_ui_art.hpp"
#include "odh/ui_bitmap.hpp"
#include "odh/user_interaction_tracker.hpp"
#include "odh/definition_index.hpp"
#include <deque>

namespace odh_host {
struct QueueTile {
    odh::UserInteractionIdentity identity;
    odh::UiBitmapRect box;
    std::size_t action_index=0;
    bool active=false,cancelled=false;
};
std::vector<QueueTile> original_queue_layout(const odh::InteractionQueueSnapshot& queue,
    std::uint32_t width,std::uint32_t height,std::optional<std::int16_t> active_priority={});
const QueueTile* queue_tile_at(const std::vector<QueueTile>&,std::int32_t x,std::int32_t y);
class OriginalQueueView {
public:
    explicit OriginalQueueView(const std::filesystem::path& game_root);
    const odh::BitmapImage* object_icon(const odh::DefinitionIndex&,std::uint32_t guid);
    void draw_tile(odh::RasterFrame&,const QueueTile&,const odh::BitmapImage* thumbnail) const;
    // Object BMPs may hold normal/highlight frames side by side. Use the
    // same normal frame in dialogs and the queue, with uniform scaling.
    void draw_object_icon(odh::RasterFrame&,odh::UiBitmapRect,const odh::BitmapImage&) const;
private:
    OriginalUiArt art_;
    std::deque<std::pair<std::uint32_t,std::optional<odh::BitmapImage>>> icons_;
};
}
