#pragma once

#include "odh/raster.hpp"

#include <memory>
#include <optional>
#include <string>

namespace odh_host {

struct WindowInput {
    bool quit = false, action = false, pause = false, interrupt = false, redraw = false;
    int rotation = 0, zoom = 0, interaction_selection_delta = 0;
    // Last Button1 press drained by this poll, in drawable pixels. Keyboard
    // action does not fabricate a pointer position; coordinates may be outside.
    std::optional<std::array<std::int32_t,2>> primary_click;
};

// Optional host-only X11 backend; Xlib types never enter the ODH core API.
class LinuxWindow {
public:
    LinuxWindow(std::uint32_t width, std::uint32_t height, const std::string& title);
    ~LinuxWindow();
    LinuxWindow(const LinuxWindow&) = delete;
    LinuxWindow& operator=(const LinuxWindow&) = delete;

    void present(const odh::RasterFrame& frame);
    WindowInput poll();
    void set_title(const std::string& title);
    // Read only this backend's drawable, synchronizing pending drawing first.
    std::vector<std::array<std::uint8_t,4>> readback() const;
    unsigned long native_id() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace odh_host
