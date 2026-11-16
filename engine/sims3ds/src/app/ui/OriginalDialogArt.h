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
inline odh_host::VmDialogAppearance DialogAppearance() {
    auto& ui=front::ResGraphics::Get();
    if(!ui.IsOpen())return {};
    auto& art=front::FrontArt::Get();
    art.Load(ui);
    if(!art.OriginalDialogTiles.Width || !art.NativeWinBtn.Width)
        throw odh::FormatError("original popup requires PopupInfoTiles and native WinBtn artwork");
    // FrontArt owns this immutable pair for the life of the renderer. Hover,
    // scroll, captions and illustrations belong to each caller's appearance.
    static const odh::BitmapImage tiles=Bitmap(art.OriginalDialogTiles);
    static const odh::BitmapImage buttons=Bitmap(art.NativeWinBtn);
    odh_host::VmDialogAppearance appearance;
    appearance.panel_tiles=&tiles;
    appearance.button_strip=&buttons;
    return appearance;
}

inline odh_host::TextOverlayResult PaintDialog(Canvas& canvas, const odh::DialogState& state,
        std::uint8_t language, std::size_t first_line, const odh_host::VmDialogAppearance& appearance,
        const odh_host::VmMessageDialogLayout& layout) {
    // Only the measured window needs a copy of the pixels behind its transparent edges.
    // Keep one staging tile for all dialog types rather than an extra full display.
    static thread_local odh::RasterFrame tile;
    tile.width=layout.width;tile.height=layout.height;
    tile.pixels.resize(std::size_t(tile.width)*tile.height);
    for(int row=0;row<layout.height;++row){
        const auto offset=std::size_t(layout.y+row)*canvas.Width+layout.x;
        detail::Import(canvas.Px.data()+offset,tile.pixels.data()+std::size_t(row)*tile.width,tile.width);
    }
    auto page=odh_host::draw_vm_dialog_tile(tile,state,language,first_line,appearance,layout,canvas.Height<240);
    for(int row=0;row<layout.height;++row){
        const auto offset=std::size_t(layout.y+row)*canvas.Width+layout.x;
        detail::Export(tile.pixels.data()+std::size_t(row)*tile.width,canvas.Px.data()+offset,tile.width);
    }
    return page;
}
}}
#endif
