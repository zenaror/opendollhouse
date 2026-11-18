// SPDX-License-Identifier: MPL-2.0
// Source-derived stop-window controls. The real paired journey remains the
// acceptance test for planner, model, foreground and interpreter ordering.
#include "simantics/engine/Routing/VMTS1MotionRequest.h"
#include <stdexcept>
#include <limits>
#include <iostream>
using namespace FSO::SimAntics::Engine::Routing;
namespace {
int checks = 0;
void check(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
TS1StopWindowStep step(TS1MotionRequest& m, float distance, int now,
    bool foreground = false, bool ended = false, float speed = 1.4f,
    int walkStyle = 0, bool direct = false, float phase = 0.f) {
    return TS1StepStopWindow(m, distance, phase, foreground, ended, speed,
        1200.f, 1.4f, walkStyle, direct, now);
}
TS1MotionRequest profile() {
    TS1MotionRequest m;
    m.StopPhase1 = .75f; m.StopPhase2 = .25f;
    m.StopDistance1 = 4.f; m.StopDistance2 = 10.f;
    return m;
}
}
int main() {
    try {
        TS1MotionRequest zero;
        check(step(zero, 0.f, 10000) == TS1StopWindowStep::Created, "zero displacement creates a stop window");
        check(zero.StopDeadline == 10100 && zero.Outcome == 1, "foreground-less source minimum is100ms");
        check(step(zero, 0.f, 10050) == TS1StopWindowStep::Finalize, "next near-anchor update consumes before deadline");
        check(zero.StopDeadline == -1, "consumed source deadline is signed negative");

        auto distant = profile();
        check(step(distant, 3.f, 20000) == TS1StopWindowStep::Created, "admitted distant anchor produces window");
        check(step(distant, 3.f, 20050) == TS1StopWindowStep::Continue, "distant anchor respects scheduled deadline");
        check(step(distant, 3.f, 20100) == TS1StopWindowStep::Finalize, "distant deadline exhaustion consumes window");

        auto run = profile();
        check(step(run, 7.f, 30000, false, false, 1.f, 1) == TS1StopWindowStep::Created,
            "run doubles final distance threshold when not immediate");
        check(step(run, 7.f, 30001, false, false, 1.f, 1) == TS1StopWindowStep::Finalize,
            "run consumes its existing window without fabricated tick count");

        auto between = profile();
        check(step(between, 8.f, 40000, false, false, 1.f, 0, false, .5f) == TS1StopWindowStep::Created,
            "between anchors uses second distance threshold");
        auto outside = profile();
        check(step(outside, 8.f, 40000, false, false, 1.f, 0, false, .8f) == TS1StopWindowStep::Continue,
            "outside anchors uses first distance threshold");
        check(outside.StopDeadline == 0, "unadmitted distance does not queue deadline");

        auto blocked = profile();
        check(step(blocked, 0.f, 50000, true) == TS1StopWindowStep::Continue,
            "ordinary foreground is not implicitly interruptible");
        check(blocked.StopDeadline == 0 && blocked.Outcome == 1, "outcome admission precedes foreground eligibility");
        blocked.ForegroundInterruptible = 1;
        check(step(blocked, 0.f, 50000, true) == TS1StopWindowStep::Created,
            "authored interruptible event permits window creation");
        check(blocked.StopDeadline > 50100, "foreground phase and duration determine stop interval");
        auto ended = profile();
        check(step(ended, 0.f, 60000, true, true) == TS1StopWindowStep::Created, "ended foreground permits stop");
        auto stopped = profile();
        check(step(stopped, 0.f, 60000, true, false, 0.f) == TS1StopWindowStep::Created, "zero-speed foreground permits stop");

        auto direct = profile();
        check(step(direct, 0.f, 70000, true, false, 1.f, 0, true) == TS1StopWindowStep::Finalize,
            "PD64 direct anchor bypasses queue creation");
        check(direct.StopDeadline == 0, "direct anchor creates no artificial deadline");
        auto invalid = profile();
        check(step(invalid, std::numeric_limits<float>::quiet_NaN(), 70000) == TS1StopWindowStep::Continue,
            "nonfinite distance cannot publish a window");
        auto blend = profile();
        blend.ForegroundInterruptible = 1;
        check(TS1StepStopWindow(blend, 0.f, 0.f, true, false, 1.4f, 1200.f,
            1.4f, 0, false, 80000, .8f) == TS1StopWindowStep::Created,
            "foreground creation produces source pose-weight blend");
        check(blend.BlendActive && blend.BlendFrom == .8f && blend.BlendTo == .2f,
            "zero-distance blend clamps ratio to source minimum");
        check(TS1AdvanceStopBlend(blend, blend.BlendStart - 1) == .8f,
            "negative elapsed clamps blend to its source weight");
        const auto halfway = TS1AdvanceStopBlend(blend, blend.BlendStart + blend.BlendDuration / 2);
        check(std::abs(halfway - .5f) < .001f, "blend interpolates weight without touching animation phase or speed");
        check(std::abs(TS1AdvanceStopBlend(blend, blend.BlendStart + blend.BlendDuration) - .2f) < .000001f
            && blend.BlendActive, "endpoint equality retains source active flag");
        check(TS1AdvanceStopBlend(blend, blend.BlendStart + blend.BlendDuration + 1) == .2f
            && !blend.BlendActive, "past endpoint publishes target and retires active flag");
        auto ratio = profile(); ratio.ForegroundInterruptible = 1;
        TS1StepStopWindow(ratio, 2.f, 0.f, true, false, 1.4f, 1200.f, 1.4f, 0, false, 0);
        check(ratio.BlendTo == .5f, "blend target uses distance divided by the selected threshold");
        TS1MotionRequest wrap;
        wrap.BlendActive = true; wrap.BlendStart = INT32_MAX - 24; wrap.BlendDuration = 100;
        wrap.BlendFrom = 1.f; wrap.BlendTo = .2f;
        check(std::abs(TS1AdvanceStopBlend(wrap, INT32_MIN + 25) - .6f) < .000001f,
            "foreground blend clock subtraction preserves signed32 wrap");
        std::cout << checks << " source stop-window checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
