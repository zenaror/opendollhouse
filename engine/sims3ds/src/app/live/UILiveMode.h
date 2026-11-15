/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims app — FreeSims UI/Panels/UILiveMode.cs: the live mode panel — family portraits (UIPersonGrid),
// speed controls, the interaction queue (UIInteractionQueue: tap an icon to cancel, the red cross is
// Other\QueueCancel.bmp) and the sub-panels TS1 has behind its live mode buttons (Live.iff STR# 134 "ModeTips"):
// Mood (UIMotiveDisplay), Job (career, salary, hours, performance + the six skills), Personality (five traits,
// ten points each), Relationships (faces with daily / lifetime bars), House (size, rooms, value).
// ADDED(sims): the lot is on the top screen, so the panel keeps a scaled touch view of it for picking with
// the stylus; tapping the open sub-panel's button again enlarges the sub-panel over the touch view.
#pragma once
#include <cstdint>
#include <vector>
#include "app/ui/UIModePanel.h"
#include "app/live/UIMotiveDisplay.h"
#ifndef __3DS__
#include "app/ui/DesktopExpansionPanels.h"
#include "app/ui/DesktopCursorRefresh.h"
#include "app/ui/DesktopLivePanels.h"
#endif

namespace FSO { namespace SimAntics { class VMAvatar; } }

namespace simapp {

class UILiveMode : public UIModePanel {
public:
    enum class Tab { Mood = 0, Job = 1, Personality = 2, Relationships = 3, House = 4, Inventory = 5, Interests = 6 }; // TS1(sims): Inventory (Hot Date+)
    explicit UILiveMode(LotScreen* owner);
    void Activate() override;
    void Draw(Canvas& c, const Rect& area) override;
    bool Tap(const std::string& id) override;
    void PointerUpdate() override;
    bool Buttons(const AppInput& in) override;
    std::string CursorArt() const override;

    Tab CurrentTab = Tab::Mood;
    bool Expanded = false;
    UIMotiveDisplay Motives;

private:
    friend class LotScreen;
    std::vector<uint16_t> _QueueUIDs;
    int _RelScroll = 0;
    int _InterestScroll = 0; // PC presentation only; independent of relationships.
    int _MotiveSim = -1;
    void UpdateMotiveDisplay();
    std::string _CursorArt = "Shared/cursors/LiveNothing.cur";
    int _HoverCheckX = -1, _HoverCheckY = -1;
#ifndef __3DS__
    void UpdateDesktopCursor(); // Only called when Live owns the displayed cursor.
#if defined(__vita__) || defined(__SWITCH__)
    double _NextHoverCheck = 0;
#endif
    original::DesktopCursorRefresh _CursorRefresh;
    original::DesktopCursorPickIdentity _DesktopPick;
#endif
    void DrawPortraits(Canvas& c, const Rect& r);
    void DrawSpeed(Canvas& c, const Rect& r);
    void DrawQueue(Canvas& c, const Rect& r, FSO::SimAntics::VMAvatar* sim);
    void DrawTabs(Canvas& c, const Rect& r);
    void DrawMood(Canvas& c, const Rect& r, FSO::SimAntics::VMAvatar* sim);
    void DrawJob(Canvas& c, const Rect& r, FSO::SimAntics::VMAvatar* sim);
    void DrawPersonality(Canvas& c, const Rect& r, FSO::SimAntics::VMAvatar* sim);
    void DrawRelationships(Canvas& c, const Rect& r, FSO::SimAntics::VMAvatar* sim);
    void DrawHouse(Canvas& c, const Rect& r);
    void DrawInventory(Canvas& c, const Rect& r, FSO::SimAntics::VMAvatar* sim);
    int _InvSort = 0, _InvScroll = 0;
#ifndef __3DS__
    original::DesktopInventorySelection _DesktopInventory;
    original::DesktopFameSelection _DesktopFame;
    original::DesktopCareerState _DesktopCareer;
#endif
    void DrawPips(Canvas& c, int x, int y, int value1000, int pipW, int pipH, uint32_t on, uint32_t off);
};

}
