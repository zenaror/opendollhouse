/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims front end — "SELECT A FAMILY" (the family bin): Nbhd/PickBkg.bmp with FamilyPickItemBkg rows and
// the PAF buttons (Create New Family / Delete Family / Move In Family / Cancel), STR#128 PickFamilyStrs.
// TS1(sims): layout read off the original 800x600 art (list area 20,68-740,508, the Simoleon and smiley
// column heads at x 598 / 675, the button slots at y 530), drawn at 0.4 on the touch screen; the top screen
// shows the highlighted family (members, their biographies, net worth, friends) and the lot it would move into.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "app/Canvas.h"
#include "app/Input.h"
#include "app/front/FrontUI.h"
#include "app/front/NhoodModel.h"
#include "app/front/ResGraphics.h"

namespace simapp { namespace front {

class FamilyPicker {
public:
    bool Open = false;
    bool Desktop = false; // original 800x600 picker on the single PC canvas
    int TargetLot = -1;        // the lot the player clicked (Move In goes straight there), -1 from the banner
    std::function<void(int16_t family)> OnMoveIn;
    std::function<void()> OnCreate;
    std::function<void(int16_t family)> OnDelete;
    std::function<void()> OnCancel;

    void Show(NhoodModel* model, int targetLot);
    // TS1(sims): "Import Family" (NghUI\Import.bmp): the same picker over the .FAM files found (Export folders,
    // TemplateFamilyUnleashed); "Move In" becomes Import (content/TS1FamilyImport), Create/Delete are hidden.
    struct ImportEntry { std::string Path; FamilyInfo Info; };
    bool ImportMode = false;
    std::vector<ImportEntry> Imports;
    std::function<void(const std::string& path)> OnImport;
    void ShowImport(NhoodModel* model, std::vector<ImportEntry> entries);
    void Close() { Open = false; }
    /// Input on the touch screen (the dialog layer is handled by the caller first).
    void Update(const AppInput& in);
    void DrawBottom(Canvas& bottom);
    void DrawTop(Canvas& top);
    int16_t SelectedFamily() const;
    bool ButtonPos(const std::string& id, int& x, int& y) const;
    bool RowPos(const std::string& familyName, int& x, int& y);

private:
#ifndef __3DS__
    struct DesktopPresentation;
    std::shared_ptr<DesktopPresentation> _Original;
    void SyncDesktopPresentation();
    void UpdateDesktop(const AppInput&);
#endif
    NhoodModel* _M = nullptr;
    Canvas _Bkg, _Item, _FaceFrame; // PickBkg 320x240, FamilyPickItemBkg 3 frames
    UIButton _Add, _Delete, _MoveIn, _Cancel, _Previous, _Next;
    std::vector<const FamilyInfo*> _List;
    int _Sel = 0, _Scroll = 0;
    bool _ArtLoaded = false, _ArtDesktop = false;
    int _OriginX = 0, _OriginY = 0;
    Canvas _DesktopCanvas;
    void DrawPanel(Canvas&);
    double _Time = 0;
    static const int Rows = 6;
    Rect RowRect(int i) const;
    void LoadArt();
    std::vector<const FamilyInfo*> Families() const;
};

}}
