/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims app — FreeSims UI/Controls/UIMotiveDisplay.cs: the eight need bars (TS1 order: Hunger, Comfort,
// Hygiene, Bladder | Energy, Fun, Social, Room), coloured from red (-100) to green (+100) with the bar colour
// and background formulas of the C# control, and the little arrows that show a need rising (green, right) or
// falling (red, left) from the change over four UI seconds (240 samples at 60 Hz).
// ADDED(sims): Draw() takes a rect (the 3DS panel is 272x51 compact or 272x206 expanded); the labels are
// Live.iff STR# 130 ("Motives") instead of the hard-coded C# English names.
#pragma once
#include <cstdint>
#include <deque>
#include <array>
#include <string>
#include "app/Canvas.h"

namespace simapp {

class UIMotiveDisplay {
public:
    UIMotiveDisplay();
    int16_t MotiveValues[8] = { 0 };
    std::string MotiveNames[8];
    /// Compatibility 30-Hz update; real UI callers supply absolute time below.
    void Update();
    /// Absolute UI milliseconds (LotScreen.Time), independent of render cadence.
    void Update(double timeMs);
    void Draw(Canvas& c, const Rect& r);
    int ChangeArrowState(unsigned motive) const { return motive < 8 ? ArrowStates[motive] : 0; }
    void Reset();

private:
    int OldMotives[8] = { 0 };
    std::deque<int> ChangeBuffer[8];
    int ArrowStates[8] = { 0 };
    int TargetArrowStates[8] = { 0 };
    bool FirstFrame = true;
    struct Sample { uint64_t Tick; std::array<int16_t,8> Values; };
    std::deque<Sample> Pending;
    std::array<int16_t,8> SampledValues{};
    double OriginMs = 0, LastTimeMs = 0;
    uint64_t FixedTick = 0;
    bool ClockReady = false;
    void Step();
    void Seed(double timeMs);
    void DrawMotive(Canvas& c, int x, int y, int w, int h, int motive, bool labelAbove);
};

}
