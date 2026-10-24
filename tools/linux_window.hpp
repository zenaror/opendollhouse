#pragma once

#include "odh/raster.hpp"

#include <memory>
#include <optional>
#include <string>

namespace odh_host {

enum class WindowQuitReason { none, escape_key, window_manager_close, window_destroyed };

// Stable diagnostic names for logs/JSON. The first observed cause is retained
// even if more close events are drained by the same or a later poll.
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
    bool quit = false, action = false, pause = false, interrupt = false, stand = false;
    bool neighborhood = false, save = false, move_out = false, redraw = false;
    int rotation = 0, zoom = 0, interaction_selection_delta = 0;
    // Last Button1 press drained by this poll, in drawable pixels. Keyboard
    // action does not fabricate a pointer position; coordinates may be outside.
    std::optional<std::array<std::int32_t,2>> primary_click;
    WindowQuitReason quit_reason = WindowQuitReason::none;
};

// Optional host-only X11 backend; Xlib types never enter the ODH core API.
class LinuxWindow {
public:
    LinuxWindow(std::uint32_t width, std::uint32_t height, const std::string& title);
    ~LinuxWindow();
    LinuxWindow(const LinuxWindow&) = delete;
    LinuxWindow& operator=(const LinuxWindow&) = delete;

    void present(const odh::RasterFrame& frame);
    // Resize this native window and replace its pixel buffer. Existing native
    // window identity, title, event selection, and key state are preserved.
    void resize(std::uint32_t width, std::uint32_t height);
    std::uint32_t width() const noexcept;
    std::uint32_t height() const noexcept;
    WindowInput poll();
    WindowQuitReason quit_reason() const noexcept;
    void set_title(const std::string& title);
    // Read only this backend's drawable, synchronizing pending drawing first.
    std::vector<std::array<std::uint8_t,4>> readback() const;
    unsigned long native_id() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace odh_host
