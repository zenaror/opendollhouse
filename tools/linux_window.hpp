#pragma once

#include "odh/raster.hpp"
#include "odh/cursor.hpp"
#include "window_input.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace odh_host {
using WindowCursorHandle=std::uint32_t;

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
    // Enable the C shortcut only on the neighborhood screen. It is checked
    // before XIM filtering there, while CAS text fields keep normal input.
    // Ordered compatibility batches: one key press or button transition;
    // only adjacent pointer motion coalesces. Remaining native events stay
    // queued for later polls, so legacy booleans cannot erase repeated edits
    // or collapse two completed CAS clicks. No Xlib types escape this adapter.
    WindowInput poll(bool allow_create_family_shortcut = false);
    WindowQuitReason quit_reason() const noexcept;
    // Native destruction is independent of which quit request arrived first.
    bool alive() const noexcept;
    // A nested screen can consume Escape to cancel and return to its parent;
    // window-manager close and window destruction remain sticky.
    void clear_escape_quit_reason() noexcept;
    // Consume an alive window's close request before a cancellable prompt.
    // Destruction is terminal and is never acknowledged as a request.
    bool acknowledge_window_close_request() noexcept;
    void set_title(const std::string& title);
    // Handles are private IDs in this window's registry, never native XIDs.
    // Malformed geometry/arrays/hotspots throw before native requests. Valid
    // inversion, partial alpha, nonmonochrome colors, server-size limits or
    // an exhausted128-entry registry return none without approximating them.
    std::optional<WindowCursorHandle> create_cursor(const odh::CursorImage&);
    // None restores the inherited platform cursor. Re-selecting the current
    // handle does no native work. Invalid local handles throw before requests.
    void use_cursor(std::optional<WindowCursorHandle>);
    // Read only this backend's drawable, synchronizing pending drawing first.
    std::vector<std::array<std::uint8_t,4>> readback() const;
    unsigned long native_id() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace odh_host
