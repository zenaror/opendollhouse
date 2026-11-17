/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.VMClock
#pragma once
#include <cstdint>
namespace FSO { namespace SimAntics {
class VMClock {
public:
    int64_t Ticks = 0;
    int32_t MinuteFractions = 0;
    int32_t TicksPerMinute = 0;
    int32_t Minutes = 0;
    int32_t Hours = 0;
    int32_t DayOfMonth = 0;
    int32_t Month = 0;
    int32_t Year = 0;
    // TS1(sims): FreeSO VMClock.FirePercent - chance (of 10000) that Burn spreads; each spread lowers it by 500,
    // it recovers by 1 per tick up to 20000.
    int32_t FirePercent = 20000;

    int32_t TimeOfDay() const {
        // The PC game's four periods also drive authored light/autonomy trees.
        // Dawn and dusk have separate values; nighttime is not a boolean flag.
        if (Hours < 6 || Hours >= 19) return 2;
        if (Hours < 7) return 3;
        return Hours < 18 ? 0 : 1;
    }
    int32_t Seconds() const { return (TicksPerMinute == 0) ? 0 : ((MinuteFractions * 60) / TicksPerMinute); }

    void FinishCalendarCarry() {
        if (DayOfMonth > 30) {
            DayOfMonth=1;
            if (++Month > 12) { Month=1; ++Year; }
        }
    }

    void Tick(bool deferCalendarCarry=false) {
        // TS1(sims): FreeSims forced the TSO rate (30 * 5 ticks per game minute) here on every tick, overriding
        // VMContext's 30. FreeSO keeps the context's value: The Sims 1 runs 30 ticks per game minute ("1 minute per
        // irl second"). At 150, everything Sims do (animations, routes, idle loops counted in ticks) took a fifth of
        // the game time it takes in TS1 while motives decay per game minute.
        if (TicksPerMinute <= 0) TicksPerMinute = 30 * 5;
        if (++MinuteFractions >= TicksPerMinute) {
            MinuteFractions = 0;
            if (++Minutes >= 60) {
                Minutes = 0;
                if (++Hours >= 24) {
                    Hours = 0;
                    // TS1(sims): FreeSO VMClock — the date advances at midnight (30-day months). FreeSims never
                    // advanced it, so day-keyed trees (car portal 'check for school bus schedule': once per Day of
                    // Month) ran only on the first day: no school bus from day 2 on.
                    ++DayOfMonth;
                    if (!deferCalendarCarry) FinishCalendarCarry();
                }
            }
        }
        if (FirePercent < 20000) FirePercent++; // TS1(sims): FreeSO
        this->Ticks++;
    }
    VMClock() {}
};
}}
