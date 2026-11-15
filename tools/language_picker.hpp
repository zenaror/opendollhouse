// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/raster.hpp"
#include "odh/game_languages.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace odh_host {
class LanguagePicker {
public:
    explicit LanguagePicker(int initial):selected_(game_language_or_english(initial)-1) {}
    std::uint8_t language() const { return std::uint8_t(selected_+1); }
    void move(int delta) { selected_=(selected_+delta%20+20)%20; }
    bool click(int x,int y,int width) {
        if(x<24 || x>=width-24 || y<56 || y>=56+20*25 || (y-56)%25>=22)return false;
        selected_=(y-56)/25;return true;
    }
    std::string title(bool ascii=false) const {
        return std::string("OpenDollhouse | Language ")+std::to_string(language())+" - "+
            (ascii?game_languages[selected_].ascii_name:game_languages[selected_].name);
    }
    odh::RasterFrame draw(int width,int height,bool ascii=false,bool text=true,std::string_view notice={},std::string_view instructions={}) const;
private:
    int selected_=0;
};
}
