#pragma once

#include "odh/raster.hpp"
#include "odh/bitmap.hpp"
#include "odh/ui_bitmap.hpp"
#include "odh/dialog.hpp"
#include "linux_text_renderer.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace odh_host {

struct VmDialogAppearance {
    const odh::BitmapImage* button_strip=nullptr;
    std::string_view ok,yes,no,cancel;
    const odh::BitmapImage* panel_tiles=nullptr;
    const odh::BitmapImage* illustration=nullptr;
    bool center_title=false; // Game VM notices opt in; existing management/CAS keep their style.
};

struct VmMessageDialogLayout {
    std::int32_t x=0,y=0,width=0,height=0;
    std::int32_t ok_x=0,ok_y=0,ok_width=0,ok_height=0;
    odh::UiBitmapRect illustration; // Exact same image bounds for measuring and painting.
};

std::optional<VmMessageDialogLayout> vm_message_dialog_layout(
    std::uint32_t frame_width,std::uint32_t frame_height,const odh::DialogState&,
    std::uint8_t language,const VmDialogAppearance& appearance={});
bool vm_message_dialog_ok_hit(const VmMessageDialogLayout&,std::int32_t x,std::int32_t y) noexcept;
std::string expand_vm_dialog_local_references(std::string_view,
                                               const std::vector<std::int16_t>& locals);
TextOverlayResult draw_vm_message_dialog(odh::RasterFrame&,std::string_view title,
                            std::string_view message,std::string_view ok_label,
                            std::uint8_t language,std::size_t first_line=0,bool text_input=false,
                            std::optional<int> body_bottom_limit={},const VmDialogAppearance& appearance={});

using VmDialogSubstitution=std::function<std::optional<std::string>(std::string_view,const std::vector<std::uint16_t>&)>;
std::string expand_vm_dialog_text(std::string_view,const VmDialogSubstitution&);
// The highlighted row is kept in the existing dialog draft as a catalog value.
std::size_t vm_dialog_choice_index(const odh::DialogState&,int delta=0);
std::optional<std::size_t> vm_dialog_choice_hit(const VmMessageDialogLayout&,
    const odh::DialogState&,int x,int y);
std::size_t vm_dialog_button_count(std::uint8_t type) noexcept;
std::optional<std::uint8_t> vm_dialog_button_hit(const VmMessageDialogLayout&,std::uint8_t type,std::int32_t x,std::int32_t y) noexcept;
// Scroll offset is presentation-only: changing pages never answers the dialog
// or changes its VM ticket, text input, selected response or persisted state.
TextOverlayResult draw_vm_dialog(odh::RasterFrame&,const odh::DialogState&,
    std::uint8_t language,std::size_t first_line=0,const VmDialogAppearance& appearance={});

} // namespace odh_host
