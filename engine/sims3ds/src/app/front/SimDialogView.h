/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims front end — the SimAntics dialogs of a lot (the TS1 dialog primitive: the carpool, letters, the
// welcome / help dialogs, object messages) drawn with the original dialog art on both screens.
//
// TS1(sims): FreeSims shows VMDialogInfo in its TSO UIAlert. The Sims 1 dialog is a dark blue box tiled from
// Shared/Dlg/Gendlg.bmp with the caller's picture on the left (the object's catalogue picture, or the Sim's face),
// the title, the message and Yes / No / Cancel on Shared/Sys/WinBtn.BMP buttons (STR#152 defaults).
// Dual screen: the top screen shows the whole dialog large over the dimmed lot (readable messages, the picture),
// the touch screen repeats the title and holds the big buttons. A = first button, B = the last one.
// The lot selection dialogs (types 5/7/8/11/13) are not drawn here: the App opens the destination map instead
// (front/AppFlow.cpp, NeighbourhoodScreen::BeginPick).
#pragma once
#include <vector>
#include "app/Canvas.h"
#include "app/Input.h"
#include "app/front/FrontUI.h"

namespace FSO { namespace SimAntics { namespace Model { namespace TSOPlatform { class VMAsyncState; } } } }
namespace simapp {
class LotSession;
class LotScreen;
namespace front {

class SimDialogView {
public:
    /// Input for the first pending dialog. Returns true when a dialog is shown (the lot UI gets no input then).
    bool Update(LotSession& s, const AppInput& in);
    /// Draws the first pending dialog; returns whether its presentation was drawn.
    /// Queue presence alone does not establish this (a pet choice may be stale).
    bool Draw(LotSession& s, LotScreen& lot, Canvas& top, Canvas& bottom);
    static bool IsLotSelection(uint8_t type) { return type == 5 || type == 7 || type == 8 || type == 11 || type == 13; }
    bool ButtonPos(int index, int& x, int& y) const;
    bool PendingHelpOnly() const { return _DesktopType == 4 && _DesktopWidth > 0 && !_DesktopHelpOpen && _DesktopHelpButton.Width > 0; }

private:
    int _DesktopWidth = 0, _DesktopButtonY = 0, _DesktopPanelWidth = 0;
    int _DesktopFirstLine = 0, _DesktopMaxFirstLine = 0;
    std::string _DesktopMessageIdentity;
    std::vector<uint8_t> _IndexedBytes;
    Canvas _IndexedIcon;
    Canvas _DesktopHelpButton;
    bool _DesktopHelpOpen = false;
    std::string _DesktopInput;
    int _DesktopPressed=-1;
    std::vector<std::pair<size_t,Rect>> _DesktopChoiceRects;
    uint8_t _DesktopType=0;
    int _PetAvatar = 0, _PetIndex = 0, _PetRendered = -1;
    std::weak_ptr<FSO::SimAntics::Model::TSOPlatform::VMAsyncState> _PetWait;
    Canvas _PetPreview;
    bool DrawPetChoice(LotSession& s, Canvas& canvas);
    bool UpdatePetChoice(LotSession& s, const AppInput& in);
    bool DrawDesktop(LotSession& s, LotScreen& lot, Canvas& canvas);
    std::vector<TextButton> _Btns;
    std::vector<int> _Codes;
    Canvas _Tile;
    bool _TileLoaded = false;
    void Layout(LotSession& s);
    // TS1(sims): the Clothing dialog (type 6): outfit thumbnails (their body textures), a page of 8
    struct OutfitThumb { std::string Texture; Canvas Img; };
    std::vector<OutfitThumb> _Outfits;
    int _OutfitPage = 0, _OutfitSel = -1;
    int16_t _OutfitAvatar = -1, _OutfitType = -1;
    std::vector<Rect> _OutfitRects;
    void LoadOutfits(LotSession& s);
    bool UpdateClothes(LotSession& s, const AppInput& in);
    void DrawClothes(LotSession& s, Canvas& top, Canvas& bottom);
    // FIX(sims): the Makin' Magic spellbook (dialog type 12, 'Look Up Spells' on the Start Here Spell Book): was a
    // bare "OK" message. Recipes (VMTS1Travel::Spellbook) 6 per page, the made ones ticked; L/R or the arrows page, up/down or a tap on the
    // touch screen list selects; the touch screen shows the selected recipe.
    int _SpellPage = 0, _SpellSel = 0;
    bool UpdateSpellbook(LotSession& s, const AppInput& in);
    void DrawSpellbook(LotSession& s, Canvas& top, Canvas& bottom);
};

}}
