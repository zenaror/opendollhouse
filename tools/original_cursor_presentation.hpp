// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "original_cursor_policy.hpp"
#include "linux_window.hpp"
namespace odh_host {
// One window owns the native handles; this game view owns the immutable data.
// Lifetime: presentation must not outlive its supplied window. No VM state.
class OriginalCursorPresentation {
public:
    OriginalCursorPresentation(const std::filesystem::path&,LinuxWindow&);
    bool select(OriginalCursorAsset);
    OriginalCursorSelection update(const OriginalCursorPolicyInput&);
    std::optional<OriginalCursorAsset> selected()const{return selected_;}
private:
    OriginalCursors bank_;
    LinuxWindow& window_;
    std::array<std::optional<WindowCursorHandle>,original_cursor_count> handles_{};
    std::array<bool,original_cursor_count> attempted_{};
    std::optional<OriginalCursorAsset> selected_;
};
}
