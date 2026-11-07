// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace odh_host {
struct UiHoverRect {std::int32_t x=0,y=0,width=0,height=0;};
struct UiHoverTarget {
    std::uint64_t id=0; // Semantic cell/entity identity, including generation when relevant.
    UiHoverRect rect;
    std::string text;
};
struct UiHoverConfig {
    // Explicit host policy: original TS1 timing is not established. The pinned
    // FreeSO UIUtils tooltip handler fades per update without a timed delay.
    std::uint64_t delay_ms;
    std::size_t text_bytes=4096;
};
struct UiHoverInput {
    std::optional<std::array<std::int32_t,2>> pointer;
    std::uint64_t context=0,selection=0;
    // Host decides whether Quick Tips applies to this help category. No VM
    // setting is read or inferred by this presentation-only component.
    bool enabled=true,focused=true,modal=false,button_down=false,interacted=false;
};
class UiHover {
public:
    explicit UiHover(UiHoverConfig);
    // Caller supplies monotonic UI milliseconds, independent of simulation
    // speed/pause. A backward timestamp drops the prior dwell and starts anew.
    // Returned target is owned; no event/action callback can be emitted.
    std::optional<UiHoverTarget> update(std::uint64_t now_ms,
        const std::optional<UiHoverTarget>&,const UiHoverInput&);
    // A paused host can sleep until this one deadline instead of repainting
    // every frame. Once update reveals the target, no transition is pending.
    bool pending() const;
    bool refresh_due(std::uint64_t now_ms) const;
    std::optional<std::uint64_t> next_transition_ms() const;
    void clear();
private:
    UiHoverConfig config_;
    std::optional<UiHoverTarget> target_;
    std::optional<std::uint64_t> last_ms_;
    std::uint64_t since_ms_=0,context_=0,selection_=0;
    bool shown_=false;
};
}
