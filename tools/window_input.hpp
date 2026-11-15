// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "window_pointer_event.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace odh_host {
enum class WindowQuitReason { none, escape_key, window_manager_close, window_destroyed };

// Stable diagnostic names for logs/JSON. The first observed cause remains
// until a nested screen explicitly consumes an Escape cancellation.
constexpr const char* window_quit_reason_name(WindowQuitReason reason) noexcept {
    switch (reason) {
    case WindowQuitReason::none: return "none";
    case WindowQuitReason::escape_key: return "escape_key";
    case WindowQuitReason::window_manager_close: return "window_manager_close";
    case WindowQuitReason::window_destroyed: return "window_destroyed";
    }
    return "unknown";
}
constexpr WindowQuitReason record_window_quit_reason(
        WindowQuitReason current, WindowQuitReason incoming) noexcept {
    return current == WindowQuitReason::none ? incoming : current;
}

struct WindowInput {
    bool quit = false, action = false, confirm = false, pause = false, interrupt = false, stand = false;
    bool neighborhood = false, save = false, move_out = false, redraw = false;
    bool create_family = false;
    int rotation = 0, zoom = 0, interaction_selection_delta = 0;
    int dialog_scroll_delta = 0; // PageUp/PageDown or mouse wheel; modal presentation only.
    // Button1 press in this ordered batch, in drawable pixels. Keyboard
    // action does not fabricate a pointer position; coordinates may be outside.
    std::optional<std::array<std::int32_t,2>> primary_click;
    WindowQuitReason quit_reason = WindowQuitReason::none;
    // Committed printable text only, at most 4096 UTF-8 bytes per poll.
    // Text consumers should handle these separately from gameplay bindings.
    std::string text_input_utf8;
    bool text_backspace = false;
    // Presentation only: Home/End select the supported high/low wall views.
    std::optional<bool> walls_down;
    std::optional<std::array<std::int32_t,2>> pointer_motion;
    // Local client coordinates. A held right button retains its press origin;
    // leaving the window or losing focus releases camera input.
    std::optional<std::array<std::int32_t,2>> pointer_position,right_press_origin;
    std::array<int,2> camera_steps{};
    bool primary_held=false;
    bool live_mode=false,buy_mode=false,sell_object=false;
    int object_rotation=0;
    bool build_mode=false,pointer_cancelled=false,pointer_overflow=false;
    bool options_mode=false;
    // Actual tick multiplier; text screens may ignore the request while still
    // consuming the printable digit from text_input_utf8.
    std::optional<std::uint8_t> simulation_speed;
    std::vector<WindowPointerEvent> pointer_events;
    // Separate presentation requests: wheel requires a popup hit; page keys
    // address an open popup. Modal consumers retain their existing channel.
    int catalog_wheel_steps=0,catalog_page_steps=0;
};
// Coalesced commands and pointer transitions keep their normal consumer. A
// query may intercept only scrolling plus harmless pointer motion/redraw.
inline bool window_catalog_scroll_only(const WindowInput& input) noexcept {
    return (input.catalog_wheel_steps||input.catalog_page_steps)&&
        !input.primary_click&&!input.primary_held&&!input.right_press_origin&&!input.quit&&
        !input.action&&!input.confirm&&!input.pause&&!input.save&&!input.neighborhood&&!input.move_out&&
        !input.stand&&!input.interrupt&&!input.sell_object&&!input.live_mode&&!input.buy_mode&&
        !input.build_mode&&!input.options_mode&&!input.create_family&&!input.rotation&&!input.zoom&&
        !input.object_rotation&&!input.walls_down&&!input.simulation_speed&&
        !input.camera_steps[0]&&!input.camera_steps[1]&&!input.pointer_cancelled&&!input.pointer_overflow&&
        input.text_input_utf8.empty()&&!input.text_backspace&&
        std::none_of(input.pointer_events.begin(),input.pointer_events.end(),[](const auto& event){
            return event.kind!=WindowPointerKind::motion;
        });
}
// A full gesture can arrive within one poll. Keep press/move/release order;
// overflow cancels the whole gesture rather than publishing a truncated drag.
inline void append_window_pointer_event(WindowInput& input,WindowPointerEvent event){
    if(input.pointer_overflow)return;
    if(input.pointer_events.size()>=256){input.pointer_events.clear();input.pointer_overflow=true;input.pointer_cancelled=true;return;}
    input.pointer_events.push_back(event);
}

// X11 letter key symbols share their ASCII values; no Xlib types are exposed.
constexpr bool window_create_family_shortcut(unsigned long key, bool held) noexcept {
    return !held && (key == 'c' || key == 'C');
}

// X11 Return and keypad Enter confirm a form; Space remains printable text.
constexpr bool window_confirm_key(unsigned long key, bool held) noexcept {
    return !held && (key == 0xff0dUL || key == 0xff8dUL);
}

// Validates a whole input-method commit before appending; malformed UTF-8,
// control characters, and commits exceeding the per-poll budget are dropped.
bool append_window_text_utf8(WindowInput& input, std::string_view committed);

}
