/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
namespace FSO { namespace LotView { class World; class Framebuffer; }
namespace SimAntics { class VMAvatar; } }
namespace simapp {
class Canvas;
// Preserved original-asset renderer; only the selected actor/camera binding is new.
void DrawOriginalSelectedCrystal(Canvas&, FSO::LotView::World&,
    const FSO::LotView::Framebuffer&, FSO::SimAntics::VMAvatar*, double milliseconds);
}
#endif
