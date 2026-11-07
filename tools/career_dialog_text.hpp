// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/animation.hpp"
#include "odh/career.hpp"
#include <functional>
#include <optional>
#include <string_view>
namespace odh_host {
struct CareerDialogPerson {std::int16_t job=0,level=0;bool alternate_title=false;};
struct CareerDialogBindings {
    const odh::CareerCatalog* careers=nullptr;
    // nullopt means unknown/load failure; an engaged null pointer means a
    // known absent STR. Tables remain owned by the caller for this call.
    std::function<std::optional<const odh::StringTable*>(std::uint16_t)> strings;
    std::uint8_t language=1;
    std::optional<CareerDialogPerson> caller;
};
struct CareerDialogText {
    std::string bytes;
    std::uint8_t language=1;
    std::optional<std::uint16_t> table;
    std::optional<std::size_t> index;
};
// Original TS1 0044bf5a + 00403d7c/4033ea/4035e8, not the incompatible
// stride-three/English-offer behavior of the managed reference. Converts no
// legacy bytes: the UI caller uses the returned actual language for its codec.
std::optional<CareerDialogText> career_dialog_text(std::string_view token,
    const std::vector<std::uint16_t>& indexes,const std::vector<std::int16_t>& locals,
    const CareerDialogBindings&,std::string& error);
}
