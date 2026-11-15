/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace odh::desktop {
// Fixed-space, main-thread measurements. A percentile is a bucket upper bound,
// not an exact sorted percentile. FPS includes waits; work timing excludes the
// intentional frame limiter. Loading callbacks can present without completing
// a main-loop frame, so those two counts are deliberately separate.
struct ConsoleFrameWindow {
    static constexpr std::array<double, 11> Bounds{{8, 16.7, 25, 33.4, 50, 100, 250, 500, 1000, 2000, 5000}};
    double Since = 0, Work = 0, WorkMax = 0, App = 0, Vm = 0, World = 0, Ui = 0, Upload = 0, Present = 0;
    uint64_t Frames = 0, Presents = 0, Updates = 0;
    std::array<uint64_t, 12> Histogram{};
    explicit ConsoleFrameWindow(double now = 0) : Since(now) {}
    void Frame(double work, double app, double vm, double world, double ui, uint64_t updates) {
        if (!std::isfinite(work) || work < 0) return;
        ++Frames; Work += work; WorkMax = std::max(WorkMax, work);
        App += app; Vm += vm; World += world; Ui += ui; Updates += updates;
        std::size_t bucket = 0;
        while (bucket < Bounds.size() && work > Bounds[bucket]) ++bucket;
        ++Histogram[bucket];
    }
    void Presentation(double upload, double present) { ++Presents; Upload += upload; Present += present; }
    bool Due(double now) const { return std::isfinite(now) && now - Since >= 5000; }
    double Fps(double now) const { return now > Since ? Frames * 1000.0 / (now - Since) : 0; }
    double P95Upper() const {
        if (!Frames) return 0;
        const uint64_t rank = Frames - Frames / 20; // ceil(0.95 * Frames), without multiplication overflow
        uint64_t sum = 0;
        for (std::size_t i = 0; i < Histogram.size(); ++i) {
            sum += Histogram[i];
            if (sum >= rank) return i < Bounds.size() ? Bounds[i] : WorkMax;
        }
        return WorkMax;
    }
    void Reset(double now) { *this = ConsoleFrameWindow(now); }
};
}
