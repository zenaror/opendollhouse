#pragma once
#include <memory>

struct _PangoFontMap;
namespace odh_host {
// Linux host lifetime, independent of window/frame and portable game state.
// Scopes are strictly LIFO, non-movable and must die on their creating thread.
// A default nested scope shares its parent's current map lease (owned/borrowed).
// Without a scope, measure/draw use a bounded local lease (standalone tools).
class LinuxTextRuntime {
public:
    LinuxTextRuntime();
    // The caller owns this valid Cairo-compatible map on the current thread.
    // Retain a reference only; never replace defaults or shut down external maps.
    explicit LinuxTextRuntime(_PangoFontMap* borrowed_map);
    ~LinuxTextRuntime();
    LinuxTextRuntime(const LinuxTextRuntime&)=delete;
    LinuxTextRuntime& operator=(const LinuxTextRuntime&)=delete;
    LinuxTextRuntime(LinuxTextRuntime&&)=delete;
    LinuxTextRuntime& operator=(LinuxTextRuntime&&)=delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace odh_host
