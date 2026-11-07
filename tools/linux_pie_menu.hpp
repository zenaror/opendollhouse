// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/bitmap.hpp"
#include "odh/pie_menu.hpp"
#include "odh/raster.hpp"
#include "linux_window.hpp"

namespace odh_host {
struct PieMenuCaption {
    std::string text;
    std::uint8_t language=1;
};
struct PieMenuPointerResult {
    bool consumed=false,redraw=false;
    std::optional<std::array<std::int32_t,2>> activated;
};
class PieMenuPointer {
public:
    PieMenuPointerResult process(const odh::PieMenuLayout&,const std::vector<WindowPointerEvent>&,
        bool cancelled,std::size_t& selected);
    bool pressed() const noexcept{return pressed_;}
    std::optional<std::array<std::int32_t,2>> hover_target() const noexcept;
    void reset() noexcept{capture_.reset();hover_.reset();pressed_=false;}
private:
    enum class Kind { action,back,previous,next };
    struct Target {
        Kind kind;
        std::size_t index,page;
        odh::PieMenuRect rect;
        bool matches(const Target&) const noexcept;
    };
    static std::optional<Target> hit(const odh::PieMenuLayout&,int x,int y);
    std::optional<Target> capture_,hover_;
    bool pressed_=false;
};
enum class PieMenuBackground { live_face,diagnostic_generic };
// Coverage, not a colour key, distinguishes the rendered head from empty
// pixels. Original black hair and magenta clothing remain visible.
odh::BitmapImage pie_menu_head_bitmap(const odh::RasterFrame&);
// Draw before plaques: the reference's 200px soft black disc, then the current
// Sim's transparent 3D render. A null head still preserves the disc.
void draw_linux_pie_menu_head(odh::RasterFrame&,const odh::PieMenuLayout&,
    const odh::BitmapImage* head);
// Shrink each visible plaque to its measured caption, preserving the radial
// attachment edge. The returned geometry is shared by drawing and hit testing.
void fit_linux_pie_menu_captions(odh::PieMenuLayout&,const std::vector<PieMenuCaption>&);
struct PieMenuBranchPresentation {
    odh::PieMenuLayout layout;
    std::array<std::int32_t,2> head_center;
    std::optional<std::array<std::int32_t,2>> head_target;
};
// Legacy observations: a two-choice submenu surrounds its parent plaque,
// retaining the root head and its gaze toward that category. Deeper branches
// reuse the same rule; only one-level, two-choice branches were observed.
// At display edges the parent/children composition, including the 201px
// surface and classic 4px margin, moves as a unit; the root head/gaze stay put.
// A composition larger than the display is outside this host profile.
std::optional<PieMenuBranchPresentation> layout_linux_pie_menu_branch(
    std::uint32_t width,std::uint32_t height,std::array<std::int32_t,2> root_anchor,
    const std::vector<std::vector<PieMenuCaption>>& levels,
    const std::vector<std::size_t>& path,std::size_t selected);
void draw_linux_pie_menu(odh::RasterFrame&,const odh::PieMenuLayout&,
    const std::vector<PieMenuCaption>&,std::size_t selected,
    const std::optional<PieMenuCaption>& parent,const odh::BitmapImage* background,bool pressed=false,
    PieMenuBackground kind=PieMenuBackground::live_face);
}
