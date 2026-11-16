// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "buy_mode_view.hpp"

namespace odh_host {
enum class OriginalSettingsPage {graphics,sound,play};
enum class OriginalSettingId {
    anti_alias,shadows,lighting,interface_effects,terrain_detail,character_detail,
    sound_effects,music,voices,auto_center,free_will,edge_scrolling,background_simulation,
    quick_tips,auto_snapshot,live_pip,export_html,count
};
constexpr std::size_t original_setting_count=static_cast<std::size_t>(OriginalSettingId::count);
// Values are observed host state, not UI defaults. Unknown state cannot be edited.
// Boolean=0/1, detail=0..2, original volume step=0..10. Capabilities require real consumers.
struct OriginalSettingsState {std::array<std::optional<int>,original_setting_count> values{};};
using OriginalSettingsCapabilities=std::array<bool,original_setting_count>;
enum class OriginalSettingControlKind {checkbox,radio,slider};
struct OriginalSettingControl {
    OriginalSettingId id;
    OriginalSettingControlKind kind;
    BuyUiRect rect;
    int value=0; // Radio choice; ignored by other controls.
};
struct OriginalSettingsLabel {std::size_t string_index;BuyUiRect rect;bool centered=false;bool right_aligned=false;};
struct OriginalSettingsLayout {
    std::vector<OriginalSettingControl> controls;
    std::vector<OriginalSettingsLabel> labels;
    float scale=1;
};
struct OriginalSettingsEvent {OriginalSettingId id;int value;};
// Base-resolution font metrics. The pure helper has conservative placeholders;
// the real view measures the localized captions with its drawing font.
struct OriginalGraphicsMetrics {
    int detail_title_width=160,header_text_height=14;
    int label_text_height=14;
    // Executable field +0x64 is not authenticated as the STR language enum.
    // Only pass its actual code when supplied by a matching authority.
    std::optional<std::uint8_t> original_language_code;
};
OriginalSettingsLayout original_settings_layout(std::uint32_t width,std::uint32_t height,OriginalSettingsPage,
    const OriginalGraphicsMetrics& graphics_metrics={});
using OriginalSettingsText=std::array<std::string,84>;
class OriginalSettingsView {
public:
    explicit OriginalSettingsView(const std::filesystem::path& game,std::uint8_t language=1,bool show_anti_alias=true,bool show_tutorial_reset=false,
        const OriginalSettingsText* selected_text=nullptr);
    OriginalSettingsLayout layout(std::uint32_t width,std::uint32_t height,OriginalSettingsPage)const;
    std::optional<std::size_t> hit(const OriginalSettingsLayout&,int x,int y,
        const OriginalSettingsState&,const OriginalSettingsCapabilities&)const;
    // Captured slider drags clamp outside the track; no event for disabled or
    // unknown state. Caller decides when a press/release should invoke this.
    std::optional<OriginalSettingsEvent> activate(const OriginalSettingsLayout&,std::size_t control,int x,
        const OriginalSettingsState&,const OriginalSettingsCapabilities&)const;
    void draw(odh::RasterFrame&,OriginalSettingsPage,const OriginalSettingsState&,
        const OriginalSettingsCapabilities&,std::optional<std::array<int,2>> pointer={},
        std::optional<std::size_t> pressed={})const;
private:
    OriginalUiArt art_;
    OriginalSettingsText strings_;
    std::uint8_t language_;
    bool show_anti_alias_;
    bool show_tutorial_reset_;
};
}

namespace odh_host {
// Presentation capture only; active values and persistence stay with the host.
struct OriginalSettingsPointerInput {int x=0,y=0;bool down=false,held=false,up=false,cancel=false;};
class OriginalSettingsInteraction {
public:
    void reset(){captured_.reset();}
    std::optional<std::size_t> captured()const{return captured_;}
    std::optional<OriginalSettingsEvent> update(const OriginalSettingsView&,const OriginalSettingsLayout&,
        const OriginalSettingsState&,const OriginalSettingsCapabilities&,OriginalSettingsPointerInput);
private:
    std::optional<std::size_t> captured_;
};
}
