// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/family.hpp"
#include "odh/neighborhood_picker.hpp"
#include "odh/ui_bitmap.hpp"
#include "screen_pointer.hpp"
#include <filesystem>
#include <functional>
#include <map>
namespace odh_host {
class OriginalUiArt;
inline constexpr std::array<unsigned,4> travel_modes{2,3,5,7};
inline constexpr std::array<const char*,4> region_icons{
    "NghUI/Downtown.bmp","NghUI/Vacation.bmp","NghUI/Studiotown.bmp","NghUI/Magicland.bmp"};
inline constexpr odh::UiBitmapRect logo_button{5,0,70,52},family_button{200,0,50,52},import_button{250,0,50,52},manage_button{300,0,50,52},
    rezone_button{350,0,50,52},exit_button{600,0,53,52},previous_neighborhood{106,0,26,52},next_neighborhood{141,0,26,52};
inline odh::UiBitmapRect region_button(std::size_t index){return {400+static_cast<int>(index)*50,0,50,52};}

struct NeighborhoodHouseArt {
    std::uint16_t house=0;
    odh::UiBitmapRect closed_box,open_box,hit;
    odh::BitmapImage closed;
    std::optional<odh::BitmapImage> open;
};
struct NeighborhoodHoverText {std::string address,family_summary;};
using NeighborhoodHoverTextProvider=std::function<NeighborhoodHoverText(std::uint16_t house)>;
using NeighborhoodPortraitProvider=std::function<const odh::BitmapImage*(std::uint32_t)>;
struct NeighborhoodMapAnimation {
    std::vector<odh::BitmapImage> frames;
    int x=0,y=0;
    bool pulsates=true;
    unsigned frame_ms=250;
};
struct PreparedNeighborhoodMap {
    odh::RasterFrame frame;
    std::vector<odh::NeighborhoodMarker> markers;
    unsigned marker_scale = 1;
    unsigned mode = 1;
    std::vector<odh::FamilyEntry> source_families;
    std::vector<odh::FamilyEntry> effective_families;
    std::vector<std::string> missing_layers;
    std::filesystem::path ui_graphics;
    std::array<bool,8> region_available{{false,true,true,true,true,true,false,true}};
    std::uint8_t language=1;
    std::vector<NeighborhoodHouseArt> houses;
    std::vector<std::array<std::uint8_t,4>> background;
    std::map<std::uint32_t,bool> unique_neighbors;
    std::filesystem::path installed_characters;
    unsigned visual_mode=1;
    std::vector<odh::BitmapImage> foreground;
    std::vector<NeighborhoodMapAnimation> animations;

};

std::optional<std::uint16_t> pick_map_lot(const PreparedNeighborhoodMap&,std::int32_t x,std::int32_t y);

void prepare_neighborhood_presentation(PreparedNeighborhoodMap&,const odh::BitmapImage&);
void draw_map_houses(odh::RasterFrame&,const PreparedNeighborhoodMap&,std::optional<std::uint16_t>,
    const std::map<std::uint16_t,float>* roof_openness=nullptr);
void draw_house_bubble(odh::RasterFrame&,const OriginalUiArt*,const odh::LotLocation&,
    const NeighborhoodHoverText&,const odh::FamilyEntry&,const NeighborhoodPortraitProvider&,
    const std::string&,std::uint8_t);
void ui_image(odh::RasterFrame&,const OriginalUiArt*,const char*,odh::UiBitmapRect,unsigned states=1,unsigned state=0);
void ui_caption(odh::RasterFrame&,odh::UiBitmapRect,const std::string&,std::uint8_t);
void ui_button(odh::RasterFrame&,const OriginalUiArt*,const char*,odh::UiBitmapRect,unsigned,const std::string&,std::uint8_t,bool always_caption=false);

// Extracted from the original Linux picker. It owns presentation/capture only;
// family identity and every write are revalidated by the current motor.
struct FamilyPickerPresentation {
    static constexpr std::size_t page_size=6;
    static constexpr int row_x=41,row_y=76,row_height=68;
    inline static constexpr odh::UiBitmapRect cancel_box{130,529,140,62},add_box{322,529,92,62},archive_box{414,529,92,62},
        move_box{506,529,164,62},rename_box{32,485,168,24},previous_box{686,484,26,39},next_box{713,484,26,39};
    std::vector<const odh::FamilyEntry*> choices;
    std::function<bool(const odh::FamilyEntry&)> can_move,can_archive;
    std::optional<std::size_t> selected;
    std::size_t first=0;
    ScreenPointer activation;
    std::optional<std::array<std::int32_t,2>> pointer;
    bool rename=false,creating=true;
    // Complete shows its third currency column only in the Magic Town picker.
    bool magic_town=false;
    std::function<int(const odh::FamilyEntry&)> magicoins;
    // Imported .FAM files belong to independent neighbourhoods and may reuse GUIDs.
    std::function<const odh::BitmapImage*(const odh::FamilyEntry&,std::uint32_t)> scoped_portraits;
    const odh::FamilyEntry* entry()const{return selected&&*selected<choices.size()?choices[*selected]:nullptr;}
    std::optional<std::size_t> target_at(int x,int y)const;
    unsigned state(std::size_t id,bool enabled)const;
    odh::RasterFrame draw(const OriginalUiArt*,std::uint8_t language,const std::string& localized_title,
        const std::string& localized_action,const std::string& localized_archive,const std::string& localized_cancel,
        const std::string& localized_no_family_title,const std::string& localized_no_family_message,
        const NeighborhoodPortraitProvider&)const;
};
} // namespace odh_host

namespace odh_host {
int neighborhood_arrow_at(std::int32_t,std::int32_t) noexcept;
unsigned neighborhood_mode_button_at(std::int32_t,std::int32_t) noexcept;
struct NeighborhoodMapPresentation {
    std::optional<std::array<std::int32_t,2>> pointer;
    std::map<std::uint16_t,NeighborhoodHoverText> hover_cache;
    bool managing=false,can_manage=false,allow_create_family=true,allow_import=false;
    bool rezoning=false,can_rezone=false;
    std::map<std::uint16_t,bool> community;
    bool previous_available=false,next_available=false;
    ScreenPointer activation;
    struct RoofTransition {float value=0,start=0,target=0;double elapsed_ms=500;};
    std::map<std::uint16_t,RoofTransition> roofs;
    double time_ms=0;
    void advance(const PreparedNeighborhoodMap&,double elapsed_ms);
    std::optional<std::size_t> target_at(const PreparedNeighborhoodMap&,int,int)const;
    odh::RasterFrame draw(const PreparedNeighborhoodMap&,const OriginalUiArt*,const std::string& localized_cancel,
        std::uint8_t language,unsigned neighborhood_number,const NeighborhoodHoverTextProvider&,
        const NeighborhoodPortraitProvider& portraits,const NeighborhoodPortraitProvider& known_portraits,
        const std::function<std::string(std::uint32_t)>& resident_name={});
};
NeighborhoodHouseArt prepare_house_presentation(std::uint16_t,const odh::LotLocation&,odh::UiBitmapRect,odh::UiBitmapRect,
    odh::BitmapImage,std::optional<odh::BitmapImage>);
}
