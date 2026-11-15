/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "app/front/FrontUI.h"
#include "app/front/ResGraphics.h"
#include "app/ui/OriginalPresentation.h"
#include "linux_vm_dialog.hpp"
namespace simapp { namespace original {
// One selected original skin: the 3x3 12px popup tile atlas and four native
// 65x33 button frames. These resources are paired by the preserved PC painter;
// Gendlg and scaled 3DS button art belong to different presentation contracts.
struct DialogArt {
    odh::BitmapImage tiles,buttons;
    odh_host::VmDialogAppearance appearance;
    DialogArt(){
        auto& ui=front::ResGraphics::Get();
        // Resource-free diagnostic consumers retain the painter's explicit
        // generic appearance. A selected game archive must supply the full pair.
        if(!ui.IsOpen())return;
        auto& art=front::FrontArt::Get();
        art.Load(ui);
        if(!art.OriginalDialogTiles.Width || !art.NativeWinBtn.Width)
            throw odh::FormatError("original popup requires PopupInfoTiles and native WinBtn artwork");
        tiles=Bitmap(art.OriginalDialogTiles);buttons=Bitmap(art.NativeWinBtn);
        appearance.panel_tiles=&tiles;appearance.button_strip=&buttons;
        // Painting retains its own exact atlas and strip validation.
    }
    DialogArt(const DialogArt&)=delete;
    DialogArt& operator=(const DialogArt&)=delete;
};
}}
#endif
