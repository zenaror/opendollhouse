/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims app — platform independent input snapshot (filled by src/platform/3ds/main.cpp from libctru's
// hid, or by src/tools/sim3ds_host.cpp from a script).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace simapp {

enum Button : uint32_t {
    BTN_A = 1u << 0, BTN_B = 1u << 1, BTN_X = 1u << 2, BTN_Y = 1u << 3,
    BTN_L = 1u << 4, BTN_R = 1u << 5, BTN_ZL = 1u << 6, BTN_ZR = 1u << 7,
    BTN_START = 1u << 8, BTN_SELECT = 1u << 9,
    BTN_UP = 1u << 10, BTN_DOWN = 1u << 11, BTN_LEFT = 1u << 12, BTN_RIGHT = 1u << 13,
};

// One desktop key edge, delivered by the existing ordered input queue.
// Commands are consumed only by the active lot after modal ownership is resolved.
enum class DesktopCommand : uint8_t {
    None, Live, Buy, Build, Pause, SpeedNormal, SpeedFast, SpeedUltra,
    ZoomIn, ZoomOut, WallsUp, WallsDown, NextSim, CenterSim,
    CatalogPrevious, CatalogNext
};

struct AppInput {
    // Desktop pointers use native pixels in the sole PC canvas. The Touch fields
    // carry the primary mouse button without converting through a 3DS viewport.
    bool DesktopPointer = false;
    DesktopCommand DesktopAction = DesktopCommand::None;
    // Focus loss/overflow cancels presentation capture without selecting a
    // dialog response or pretending that the player pressed Escape.
    bool PointerCancelled = false;
    bool MouseRightDown = false, MouseRightHeld = false, MouseRightUp = false;
    int WheelY = 0;
    // Ordered edits preserve typing followed by Backspace/Enter in one frame.
    struct TextEvent {
        enum class Type { Insert, Backspace, Delete, Home, End, Left, Right, Accept, Cancel };
        Type Kind;
        std::string Text;
    };
    std::vector<TextEvent> TextEvents;
    std::string TextUTF8;
    bool TextBackspace = false, TextDelete = false, TextHome = false, TextEnd = false;
    bool TextLeft = false, TextRight = false, TextAccept = false, TextCancel = false;
    bool DesktopDelete = false, DesktopRotateLeft = false, DesktopRotateRight = false;
    bool DesktopCtrl = false, DesktopShift = false;
    uint32_t Held = 0;      // buttons currently down
    uint32_t Down = 0;      // pressed this frame
    float CircleX = 0;      // circle pad, -1..1 (right = +)
    float CircleY = 0;      // circle pad, -1..1 (up = +)
    float CStickX = 0;      // New 3DS C-stick, -1..1
    float CStickY = 0;
    bool Touching = false;  // stylus on the bottom screen
    bool TouchDown = false; // touch started this frame
    bool TouchUp = false;   // touch released this frame (TouchX/Y = last position)
    int TouchX = 0, TouchY = 0; // bottom screen pixels (0..319, 0..239)
};

}
