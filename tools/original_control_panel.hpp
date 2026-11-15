// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ui_bitmap.hpp"
#include "original_ui_art.hpp"
#include "live_panel_view.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace odh_host {
inline constexpr const char* original_panel_category_art[8]={"cpanel\\catalog\\buyfseating.bmp","cpanel\\catalog\\buyfsurfaces.bmp",
    "cpanel\\catalog\\buyfappliances.bmp","cpanel\\catalog\\buyfelectronics.bmp","cpanel\\catalog\\buyfplumbing.bmp",
    "cpanel\\catalog\\buyfdecorative.bmp","cpanel\\catalog\\buyfmisc.bmp","cpanel\\catalog\\buyflighting.bmp"};
using BuyUiRect=odh::UiBitmapRect;
enum class OriginalPanelMode { live,buy,build,options };
enum class OriginalPanelAction { none,pause,rotate_camera_left,rotate_camera_right,zoom_in,zoom_out,walls_up,walls_down,level_up,level_down,walls_cutaway,options,live_needs,live_relationships,live_career,live_personality,live_interests,live_house,live_inventory,speed_normal,speed_fast,speed_ultra };
struct OriginalPanelState {bool paused=false,walls_down=false,build_buy_enabled=true;std::optional<std::int16_t> mood={};bool show_family_funds=true;
    // Optional capabilities come from actual host view state. Missing ones are
    // drawn disabled, with no invented floor/roof or simulation state.
    std::optional<bool> can_level_up,can_level_down,cutaway;
    bool options_enabled=false,options_open=false;
    bool inventory_enabled=false,inventory_selected=false;
    std::array<bool,6> live_tabs{};
    LivePanelTab live_tab=LivePanelTab::needs;
    // Actual host multiplier from the pinned TS1 UI (normal1, fast3, ultra10).
    // Missing/invalid values have no live consumer; edit/modal callers omit it.
    std::optional<std::uint8_t> simulation_speed;
    // Presentation capture/hover use the same actual-multiplier identity.
    // They never change which speed the simulation is currently running.
    std::optional<std::uint8_t> pressed_simulation_speed,hovered_simulation_speed;
    bool live_enabled=true; // Direct public/vacant lot edit sessions have no Live capability.
    std::optional<std::array<int,2>> pointer,press_origin; // Shared physical PC pointer; absent while background is modal.
};
struct OriginalControlPanelLayout {
    float scale=1;
    BuyUiRect base,left_patch,panel_background,clock,budget,portraits,right_content;
    std::array<BuyUiRect,3> modes; // Live, Buy, Build, using original four-state art.
    std::array<BuyUiRect,8> needs; // Hunger/Comfort/Hygiene/Bladder then Energy/Fun/Social/Room.
    BuyUiRect needs_title,green_mood,red_mood,live_gadget,camera_patch,pause,rotate_left,rotate_right,zoom_in,zoom_out,walls_up,walls_down;
    BuyUiRect level_up,level_down,roof,walls_cutaway,camera,options;
    std::array<BuyUiRect,3> speeds;
    std::array<BuyUiRect,6> tabs; // Mood, relationship, job, personality, interests, house.
};
std::array<BuyUiRect,8> original_portrait_rects(const OriginalControlPanelLayout&,std::size_t residents);
OriginalControlPanelLayout original_control_panel_layout(std::uint32_t width,std::uint32_t height);
// One immutable native artwork owner shared by live and buy HUDs. All resources
// come from the selected game's UIGraphics.far; no screenshot/generated assets.
class OriginalControlPanelArt {
public:
    explicit OriginalControlPanelArt(const std::filesystem::path& game_root);
    const odh::BitmapImage& at(const std::string& archive_key)const;
    const odh::BitmapImage* find(const std::string& archive_key)const;
    void draw(odh::RasterFrame&,OriginalPanelMode,OriginalPanelState={})const;
    // Only controls with loaded original art and an enabled host capability
    // emit actions; unavailable tabs and speed controls remain disabled.
    OriginalPanelAction hit(const OriginalControlPanelLayout&,std::int32_t,std::int32_t,OriginalPanelState={})const;
    void draw_portrait(odh::RasterFrame&,BuyUiRect,bool selected,const odh::BitmapImage* head,bool authored_icon=false) const;
    void draw_portrait_state(odh::RasterFrame&,BuyUiRect,unsigned frame,const odh::BitmapImage* head,bool authored_icon=false) const;
    void draw_button(odh::RasterFrame&,const std::string& archive_key,const BuyUiRect&,unsigned state=0)const;
private:
    struct BackdropRun {
        std::uint16_t x,y,width;
        bool translucent;
    };
    // Decoded panel artwork is immutable for this owner's lifetime.
    std::map<const odh::BitmapImage*,std::vector<BackdropRun>> backdrop_runs_;
    void prepare_backdrop(const odh::BitmapImage&);
    void draw_backdrop(odh::RasterFrame&,const odh::BitmapImage&,BuyUiRect) const;
    OriginalUiArt art_;
};
// Shared colour-keyed drawing edge retained from the original panel consumer.
namespace original_panel_detail {
void blit(odh::RasterFrame&,const odh::BitmapImage&,BuyUiRect);
void blit(odh::RasterFrame&,const odh::BitmapImage&,BuyUiRect,BuyUiRect crop);
}
} // namespace odh_host
