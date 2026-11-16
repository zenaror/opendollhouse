// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "original_control_panel.hpp"
#include "retained_panel_art.hpp"
#include <tuple>

namespace simapp { namespace original {

// One cache per immutable set of control panel artwork. Labels, portraits,
// the clock and other live overlays are drawn separately by their owners.
class RetainedControlPanel {
    const odh_host::OriginalControlPanelArt& art;
    odh_host::RetainedPanelArt layer;
    std::optional<odh_host::OriginalPanelState> previous;
    odh_host::OriginalPanelMode previous_mode{};
    std::uint32_t width=0,height=0;

    static auto inputs(const odh_host::OriginalPanelState& s) {
        return std::tie(s.paused,s.walls_down,s.build_buy_enabled,s.mood,s.show_family_funds,
            s.can_level_up,s.can_level_down,s.cutaway,s.options_enabled,s.options_open,
            s.inventory_enabled,s.inventory_selected,s.live_tabs,s.live_tab,s.simulation_speed,
            s.pressed_simulation_speed,s.hovered_simulation_speed,s.live_enabled,s.pointer,s.press_origin);
    }
public:
    explicit RetainedControlPanel(const odh_host::OriginalControlPanelArt& artwork):art(artwork){}

    void draw(odh::UiBitmapSurface frame,odh_host::OriginalPanelMode mode,
              odh_host::OriginalPanelState state) {
        if(frame.width>odh_host::RetainedPanelArt::max_width) {
            art.draw(frame,mode,state);
            return;
        }
        // At every supported scale all controls fit in the bottom 183 pixels.
        // Moving over the lot cannot change a control's hover or press state.
        const auto outside=[&](std::optional<std::array<int,2>>& p) {
            if(p && (*p)[1]<int(frame.height)-183) p=std::array<int,2>{-1,-1};
        };
        outside(state.pointer);
        outside(state.press_origin);
        if(previous && width==frame.width && height==frame.height && previous_mode==mode &&
           inputs(*previous)==inputs(state)) {
            layer.replay(frame);
            return;
        }
        // Only publish a reusable batch after every sprite validated and drew.
        previous.reset();
        art.draw(frame,mode,state,&layer);
        previous=state;
        previous_mode=mode;
        width=frame.width;
        height=frame.height;
    }
};
}}
