// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/raster.hpp"
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace odh_host {
struct GameLanguage { const char* tag; const char* name; const char* ascii_name; };
// TS1 resource codes, in the same order as the preserved first-run picker.
// Availability of a code is not a claim that every installed asset is translated.
inline constexpr std::array<GameLanguage,20> game_languages{{
    {"en-US","English (US)","English (US)"},{"en-GB","English (UK)","English (UK)"},
    {"fr","Français","Francais"},{"de","Deutsch","Deutsch"},{"it","Italiano","Italiano"},
    {"es","Español","Espanol"},{"nl","Nederlands","Nederlands"},{"da","Dansk","Dansk"},
    {"sv","Svenska","Svenska"},{"no","Norsk","Norsk"},{"fi","Suomi","Suomi"},
    {"he","עברית","Hebrew"},{"ru","Русский","Russian"},{"pt-BR","Português","Portugues"},
    {"ja","日本語","Japanese"},{"pl","Polski","Polski"},{"zh-CN","简体中文","Chinese Simplified"},
    {"zh-TW","繁體中文","Chinese Traditional"},{"th","ไทย","Thai"},{"ko","한국어","Korean"}
}};
inline bool valid_game_language(int code) { return code>=1 && code<=int(game_languages.size()); }
inline std::uint8_t game_language_or_english(int code) { return valid_game_language(code)?std::uint8_t(code):1; }
inline std::optional<std::uint8_t> parse_game_language(std::string_view value) {
    if(value.empty())return {};
    for(std::size_t i=0;i<game_languages.size();++i)if(value==game_languages[i].tag)return std::uint8_t(i+1);
    int code=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),code);
    if(parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size() && valid_game_language(code))return std::uint8_t(code);
    return {};
}
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
    odh::RasterFrame draw(int width,int height,bool ascii=false,bool text=true,std::string_view notice={}) const;
private:
    int selected_=0;
};
}
