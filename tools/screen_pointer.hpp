// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "window_input.hpp"
#include <cstddef>
#include <optional>

namespace odh_host {
// Original cTSWinBtn captures a press, then invokes on release inside the
// same control. A press alone, a different release target or lost focus must
// never perform the action. Target IDs belong to the current screen only.
class ScreenPointer {
public:
    template<class Hit>
    std::optional<std::array<std::int32_t,2>> click(const WindowInput& input,const Hit& hit){
        if(input.pointer_cancelled||input.pointer_overflow){reset();return {};}
        for(const auto& event:input.pointer_events){
            if(event.kind==WindowPointerKind::motion)continue;
            const auto target=hit(event.x,event.y);
            if(event.kind==WindowPointerKind::press)pressed_=target;
            else{
                const bool activate=target&&target==pressed_;
                reset();
                if(activate)return std::array<std::int32_t,2>{event.x,event.y};
            }
        }
        return {};
    }
    std::optional<std::size_t> pressed()const noexcept{return pressed_;}
    void reset()noexcept{pressed_.reset();}
private:
    std::optional<std::size_t> pressed_;
};
}
