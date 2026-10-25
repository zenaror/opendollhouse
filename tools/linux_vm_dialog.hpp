#pragma once

#include "odh/raster.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace odh_host {

struct VmMessageDialogLayout {
    std::int32_t x=0,y=0,width=0,height=0;
    std::int32_t ok_x=0,ok_y=0,ok_width=0,ok_height=0;
};

std::optional<VmMessageDialogLayout> vm_message_dialog_layout(
    std::uint32_t frame_width,std::uint32_t frame_height) noexcept;
bool vm_message_dialog_ok_hit(const VmMessageDialogLayout&,std::int32_t x,std::int32_t y) noexcept;
std::string expand_vm_dialog_local_references(std::string_view,
                                               const std::vector<std::int16_t>& locals);
void draw_vm_message_dialog(odh::RasterFrame&,std::string_view title,
                            std::string_view message,std::string_view ok_label,
                            std::uint8_t language);

} // namespace odh_host
