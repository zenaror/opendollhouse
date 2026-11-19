/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims app — TS1(sims): the state the original's cheats switch on for the rest of the session (The Sims keeps
// them until the game is closed; they are not saved). Set by the cheat console (app/ui/UICheatConsole), read by
// the lot UI (move_objects in UIObjectHolder, tile_info / route_balloons in LotScreen) and App::LiveFrame
// (sim_speed, sweep, autonomy, map_edit).
#pragma once
#include <cstdint>

namespace simapp {

struct CheatState {
    bool MoveObjects = false;    // move_objects on|off: pick up / delete any object and Sims (buy & build mode)
    bool MapEdit = false;        // map_edit on|off: build and place outside the buildable area
    bool TileInfo = false;       // tile_info on|off: the tile under the cursor (position, room, floor, wall, terrain)
    bool RouteBalloons = false;  // route_balloons on|off: the routing Sims' waypoints as balloons
    bool DrawRoutes = false;     // draw_routes on|off: their path as lines
    bool PreviewAnims = false;   // preview_anims on|off  (engine debug; accepted, no effect in this port)
    bool DrawAllFrames = false;  // draw_all_frames on|off (accepted, the port always draws every frame)
    bool DrawFloorable = false;  // draw_floorable on|off (accepted, no effect)
    bool Sweep = true;           // sweep on|off: the simulation ticks (off = frozen)
    int SimSpeed = 0;            // non-TS1 legacy relative speed override
    // DVD sim_speed and the HUD presets write the same UI raw speed field.
    // Keep the signed UI value; the original scheduler clamps only at its
    // owner boundary. Literal0 is a valid slow rate, not an unset override.
    int TS1RawSimSpeed = 760;
    uint64_t TS1RawSimSpeedRevision = 0;
    void SetTS1RawSimSpeed(int raw) {
        TS1RawSimSpeed = raw;
        ++TS1RawSimSpeedRevision;
    }
    int Autonomy = 0;            // autonomy 1..100 (0 = not set by a cheat)

    static CheatState& Get() { static CheatState s; return s; }
};

}
