/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims app — the options mode panel (FreeSims UI/Panels/UIOptions.cs, TS1's "Options" mode) together with
// TS1's view controls (rotate, zoom, walls up / cutaway / down, first / second story — cpanel\Buttons\rotleft.bmp,
// zoomin.bmp, nowall.bmp, nocut.bmp, dynacut.bmp, Lev1.BMP, Lev2.BMP) and the game buttons (neighbourhood, quit;
// cpanel\Buttons\OptNbhd.bmp, OptExit.bmp). Desktop actions retain the current save/exit flow.
#pragma once
#include "app/ui/UIModePanel.h"
#include <memory>
#ifndef __3DS__
#include "app/ui/DesktopOptionsPanel.h"
#endif

namespace simapp {

class UIOptionsPanel : public UIModePanel {
public:
    explicit UIOptionsPanel(LotScreen* owner);
    ~UIOptionsPanel() override;
    void Activate() override;
    void Deactivate() override;
    bool Buttons(const AppInput&) override;
    void Draw(Canvas& c, const Rect& area) override;
    bool Tap(const std::string& id) override;
    void PointerUpdate() override;
    std::string CursorArt() const override { return "Shared/cursors/Arrow.cur"; }
private:
#ifndef __3DS__
    std::unique_ptr<original::DesktopOptionsPanel> _Desktop;
#endif
};

}
