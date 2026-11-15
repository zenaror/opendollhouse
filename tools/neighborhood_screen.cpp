#include "neighborhood_screen.hpp"
#include "language_picker.hpp"
#include "neighborhood_resources.hpp"
#include "linux_window.hpp"
#include "character_display_names.hpp"

#include "odh/family.hpp"
#include "odh/neighborhood.hpp"
#include "odh/character_catalog.hpp"
#include "odh/family_overlay.hpp"
#include "odh/neighborhood_picker.hpp"
#include "odh/resident_selection.hpp"
#include "odh/sha256.hpp"
#include "resource.hpp"
#include "original_ui_art.hpp"
#include "linux_cas_layout.hpp"
#include "screen_pointer.hpp"
#include "original_name_prompt.hpp"
#include "odh/ui_bitmap.hpp"
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
#include "linux_text_renderer.hpp"
#include "linux_vm_dialog.hpp"
#include "linux_ui_strings.hpp"
#endif
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
#include "linux_text_codec.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <thread>
#include <utility>

namespace odh_host {
namespace {
std::optional<OriginalUiArt> screen_art(const std::filesystem::path& archive,const std::vector<std::string>& keys){
    if(archive.empty())return std::nullopt;
    // The standalone map inspector also accepts arbitrary archives; do not
    // silently substitute a different installation's artwork for that input.
    const auto file=std::filesystem::absolute(archive).lexically_normal();
    const auto root=file.parent_path().parent_path();
    if(file!=(root/"UIGraphics/UIGraphics.far"))return std::nullopt;
    return OriginalUiArt(root,{},keys);
}
// These hit areas follow the three empty button wells in Nbhd\PickBkg.bmp.
constexpr std::int32_t kFamilyPickerButtonTop = 530;
constexpr std::int32_t kFamilyPickerButtonBottom = 592;
constexpr std::int32_t kFamilyPickerArchiveLeft = 414;
constexpr std::int32_t kFamilyPickerMoveInLeft = 506;
constexpr std::int32_t kFamilyPickerButtonRight = 670;

std::array<std::uint8_t, 32> neighborhood_identity(const odh::View& view) {
    odh::Sha256 hash;
    std::array<std::uint8_t, 64 * 1024> block{};
    for (std::uint64_t offset = 0; offset < view.size();) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            block.size(), view.size() - offset));
        view.read(offset, block.data(), count);
        hash.update(block.data(), count);
        offset += count;
    }
    return hash.finish();
}

std::string family_display_label(const odh::FamilyEntry& family) {
    if (!family.display_name.empty()) return family.display_name;
    return "Family " + std::to_string(family.family.family_number);
}
}

PreparedNeighborhoodMap prepare_neighborhood_map(
        const std::filesystem::path& user_data,
        const std::filesystem::path& ui_graphics, unsigned mode,
        const odh::FamilyOverlay* saved_overlay, std::uint8_t language) {
    std::vector<odh::FamilyEntry> entries;
    std::vector<odh::FamilyEntry> source_families;
    std::map<std::uint16_t, std::string> family_names;
    std::map<std::uint32_t,bool> unique_neighbors;
    with_iff_file((user_data / "Neighborhood.iff").string(), [&](const auto& view) {
        const auto identity = saved_overlay ? neighborhood_identity(view) :
            std::array<std::uint8_t, 32>{};
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if(chunk.type=="NBRS"&&chunk.id==1){
                try{
                    auto nbrs=odh::read_nbrs(chunk.payload);
                    if(saved_overlay&&!saved_overlay->creations.empty())
                        nbrs=odh::apply_neighbor_overlay(nbrs,identity,*saved_overlay);
                    for(const auto& item:nbrs.entries)if(item.record&&item.record->guid){
                        const auto added=unique_neighbors.emplace(item.record->guid,true);
                        if(!added.second)added.first->second=false;
                    }
                }catch(const odh::FormatError& error){
                    unique_neighbors.clear();
                    std::clog<<"OpenDollhouse: neighborhood resident labels unavailable: "<<error.what()<<'\n';
                }
                return;
            }
            if (chunk.type == "FAMs") {
                if (family_names.count(chunk.id))
                    throw odh::FormatError("ambiguous FAMs family name chunk");
                const auto table = odh::read_strings(chunk.payload);
                if (const auto* name = odh::string_in_language_or_english(table, language, 0)) {
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
                    family_names.emplace(chunk.id, legacy_text_to_utf8(name->value,name->language?name->language:1));
#else
                    family_names.emplace(chunk.id, name->value);
#endif
                }
                return;
            }
            if (chunk.type != "FAMI") return;
            if (entries.size() >= 4096) throw odh::FormatError("too many FAMI chunks");
            entries.push_back({chunk.id, odh::read_fami(chunk.payload)});
        });
        for (auto& entry : entries) {
            const auto name = family_names.find(entry.chunk_id);
            if (name != family_names.end()) entry.display_name = name->second;
        }
        source_families = entries;
        if (saved_overlay) {
            entries = odh::apply_family_overlay(entries, identity, *saved_overlay);
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
            // Sidecar names retain game-encoded bytes; decode at this single
            // presentation boundary, after the original FAMs name fallback.
            for (auto& entry : entries) {
                const auto override_name = std::find_if(saved_overlay->names.begin(), saved_overlay->names.end(),
                    [&](const auto& name) { return name.family_chunk_id == entry.chunk_id; });
                const bool created = std::any_of(saved_overlay->creations.begin(), saved_overlay->creations.end(),
                    [&](const auto& creation) { return creation.family_chunk_id == entry.chunk_id; });
                if (override_name != saved_overlay->names.end())
                    entry.display_name = legacy_text_to_utf8(entry.display_name, override_name->language);
                else if (created)
                    entry.display_name = legacy_text_to_utf8(entry.display_name, language);
            }
#endif
        }
    });
    const odh::FamilyContext families(entries);
    auto map=prepare_neighborhood_map_resources(user_data,ui_graphics,mode,language,
        [&](std::uint16_t house){return families.by_house(static_cast<std::int16_t>(house))!=nullptr;});
    map.source_families=std::move(source_families);map.effective_families=std::move(entries);
    map.unique_neighbors=std::move(unique_neighbors);
    return map;
}

std::optional<std::uint8_t> show_language_picker(LinuxWindow& window,
        std::uint8_t initial_language) {
    if (!valid_game_language(initial_language))
        throw odh::FormatError("language picker requires a supported language code");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    LanguagePicker picker(initial_language);
#ifdef ODH_HAVE_LINUX_BITMAP_TEXT_FALLBACK
    constexpr bool ascii=true;
#else
    constexpr bool ascii=false;
#endif
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    constexpr bool text=true;
#else
    constexpr bool text=false;
#endif
    const auto render=[&]{window.set_title(picker.title(ascii));window.present(picker.draw(window.width(),window.height(),ascii,text));};
    render();
    for (;;) {
        const auto input=window.poll();
        if(input.quit)return std::nullopt;
        if(input.interaction_selection_delta){picker.move(input.interaction_selection_delta);render();}
        if(text && input.primary_click && picker.click((*input.primary_click)[0],(*input.primary_click)[1],window.width()))return picker.language();
        if(input.action && !input.primary_click)return picker.language();
        if(input.redraw)render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

odh::RasterFrame compose_neighborhood_picker_frame(
        const PreparedNeighborhoodMap& map,
        const std::string& localized_title,
        const std::string& localized_cancel,
        std::uint8_t language) {
    if (map.frame.width != 800 || map.frame.height != 600 || language < 1 || language > 20)
        throw odh::FormatError("neighborhood frame requires 800x600 pixels and a supported language");
    auto frame=map.frame;
    (void)localized_title;(void)localized_cancel;
    return frame;
}

std::optional<std::uint16_t> show_neighborhood_picker(
        LinuxWindow& window, const PreparedNeighborhoodMap& map,
        const std::string& localized_title,
        const std::string& localized_cancel,
        std::uint8_t language) {
    if (window.width() != 800 || window.height() != 600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    window.set_title("OpenDollhouse | " + localized_title);
    const auto frame = compose_neighborhood_picker_frame(map, localized_title,
        localized_cancel, language);
    window.present(frame);
    ScreenPointer activation;
    const auto hit=[&](int x,int y)->std::optional<std::size_t>{return pick_map_lot(map,x,y);};
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (const auto click=activation.click(input,hit)) {
            const auto selected = pick_map_lot(map,(*click)[0],(*click)[1]);
            if (selected) return selected;
        }
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<NeighborhoodPickerResult> show_neighborhood_picker_with_create(
        LinuxWindow& window,const PreparedNeighborhoodMap& map,
        const std::string& localized_title,const std::string& localized_cancel,
        std::uint8_t language) {
    return show_neighborhood_picker_with_actions(window, map, localized_title,
        localized_cancel, language, true);
}

std::optional<NeighborhoodPickerResult> show_neighborhood_picker_with_actions(
        LinuxWindow& window,const PreparedNeighborhoodMap& map,
        const std::string& localized_title,const std::string& localized_cancel,
        std::uint8_t language, bool allow_create_family,const std::string& create_label,
        NeighborhoodHoverTextProvider hover_text,const std::filesystem::path& private_characters,
        NeighborhoodPortraitProvider portraits,unsigned neighborhood_number,bool previous_available,bool next_available) {
    if(window.width()!=800||window.height()!=600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    if (!odh::neighborhood_mode_supported(map.mode))
        throw odh::FormatError("neighborhood picker received an unsupported mode");
    std::vector<std::string> keys(region_icons.begin(),region_icons.end());
    keys.insert(keys.end(),{"NghUI/banner_neighborhood.bmp","NghUI/banner_downtown.bmp","NghUI/Exit.bmp",
        "NghUI/MoveIn.bmp","NghUI/Bulldoze.bmp","NghUI/Rezone.bmp","NghUI/Import.bmp","NghUI/Return.bmp",
        "NghUI/Previous.bmp","NghUI/Next.bmp","NghUI/Current.bmp","NghUI/TheSimsLogo.bmp",
        "Nbhd/HotSpotPopupBkg.bmp","Nbhd/HotSpotPopupBkgAlpha.bmp","Nbhd/HotspotPopupTiler.TGA","CPanel/People/PeopleTemplate.bmp"});
    const auto skin=screen_art(map.ui_graphics,keys);
    const auto* art=skin?&*skin:nullptr;
    (void)create_label;
    window.set_title("OpenDollhouse | "+localized_title);
    NeighborhoodMapPresentation view;
    view.allow_create_family=allow_create_family;view.previous_available=previous_available;view.next_available=next_available;
    auto& pointer=view.pointer;auto& hover_cache=view.hover_cache;
    const odh::FamilyContext households(map.effective_families);
    CharacterDisplayNames character_names(map.installed_characters,private_characters,language);
    const bool can_manage=std::any_of(map.markers.begin(),map.markers.end(),[](const auto& marker){return marker.file_exists&&marker.occupied;});
    NeighborhoodPortraitProvider known_portraits;
    if(portraits)known_portraits=[&](std::uint32_t guid)->const odh::BitmapImage*{
        const auto it=map.unique_neighbors.find(guid);
        return it!=map.unique_neighbors.end()&&it->second?portraits(guid):nullptr;
    };
    view.can_manage=can_manage;
    auto& managing=view.managing;auto& activation=view.activation;
    const auto target_at=[&](int x,int y){return view.target_at(map,x,y);};
    const auto render=[&]{window.present(view.draw(map,art,localized_cancel,language,neighborhood_number,hover_text,portraits,known_portraits,
        [&](uint32_t guid){return character_names.name(guid);}));};
    render();
    for(;;) {
        const auto input=window.poll(allow_create_family);
        const auto hover_target=[&](const auto& at){
            if(!at)return std::pair<unsigned,std::uint16_t>{0,0};
            const int x=(*at)[0],y=(*at)[1];
            const unsigned control=family_button.contains(x,y)?8:exit_button.contains(x,y)?9:manage_button.contains(x,y)?10:
                previous_neighborhood.contains(x,y)?11:next_neighborhood.contains(x,y)?12:neighborhood_mode_button_at(x,y);
            return std::pair<unsigned,std::uint16_t>{control,control?0:pick_map_lot(map,x,y).value_or(0)};
        };
        const bool hover_changed=hover_target(pointer)!=hover_target(input.pointer_position);
        pointer=input.pointer_position;
        if(input.quit){
            if(managing&&input.quit_reason==WindowQuitReason::escape_key){
                managing=false;activation.reset();window.clear_escape_quit_reason();render();continue;
            }
            return std::nullopt;
        }
        if(input.move_out&&can_manage){activation.reset();managing=!managing;render();continue;}
        if(allow_create_family && input.create_family)
            return NeighborhoodPickerResult{NeighborhoodPickerAction::select_family,0,0};
        const auto previous_press=activation.pressed();
        const auto click=activation.click(input,target_at);
        if(click) {
            const auto x=(*click)[0],y=(*click)[1];
            if(exit_button.contains(x,y))return std::nullopt;
            if(const int delta=neighborhood_arrow_at(x,y)){
                if(delta<0?previous_available:next_available)
                    return NeighborhoodPickerResult{NeighborhoodPickerAction::switch_neighborhood,0,0,delta};
                continue;
            }
            if(import_button.contains(x,y)||rezone_button.contains(x,y)||(family_button.contains(x,y)&&!allow_create_family))continue;
            if(manage_button.contains(x,y)){if(can_manage){managing=!managing;render();}continue;}
            if(allow_create_family&&family_button.contains(x,y))
                return NeighborhoodPickerResult{NeighborhoodPickerAction::select_family,0,0};
            const auto selected_mode = neighborhood_mode_button_at(
                (*click)[0], (*click)[1]);
            if (selected_mode) {
                const auto target=selected_mode==map.mode?1U:selected_mode;
                if(map.region_available[target])return NeighborhoodPickerResult{NeighborhoodPickerAction::switch_mode,0,target};
                continue;
            }
            const auto selected=pick_map_lot(map,(*click)[0],(*click)[1]);
            if(selected){
                if(!managing)return NeighborhoodPickerResult{NeighborhoodPickerAction::lot,*selected,0};
                if(households.by_house(static_cast<std::int16_t>(*selected)))
                    return NeighborhoodPickerResult{NeighborhoodPickerAction::manage_lot,*selected,0};
            }
        }
        if(input.redraw||hover_changed||previous_press!=activation.pressed())render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

NeighborhoodLotPanelChoice show_neighborhood_lot_panel(LinuxWindow& window,
        const PreparedNeighborhoodMap& map, std::uint16_t house_number,
        const std::string& title, const std::string& description,
        const std::string& summary, const std::string& localized_accept,
        const std::string& localized_back, std::uint8_t language) {
    if (map.frame.width != 800 || map.frame.height != 600 ||
        !odh::neighborhood_mode_supported(map.mode) || house_number == 0 || house_number > 98 ||
        title.empty() || language < 1 || language > 20)
        throw odh::FormatError("lot details panel received invalid neighborhood data");
    const auto marker = std::find_if(map.markers.begin(), map.markers.end(),
        [&](const auto& value) { return value.location.house == house_number; });
    if (marker == map.markers.end())
        throw odh::FormatError("lot details panel house is absent from the selected map");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);

    auto frame = map.frame;
    constexpr std::int32_t panel_x = 20, panel_y = 92, panel_w = 420, panel_h = 438;
    const auto skin=screen_art(map.ui_graphics,{"Nbhd/HotSpotPopupBkg.bmp","Shared/Sys/WinBtn.bmp"});
    const auto* art=skin?&*skin:nullptr;
    if(art&&art->find("Nbhd/HotSpotPopupBkg.bmp"))
        ui_image(frame,art,"Nbhd/HotSpotPopupBkg.bmp",{panel_x,panel_y,panel_w,panel_h});
    else for(int y=panel_y;y<panel_y+panel_h;++y)
        std::fill_n(frame.pixels.begin()+y*800+panel_x,panel_w,std::array<std::uint8_t,4>{22,28,56,255});
    const odh::UiBitmapRect accept{panel_x+18,panel_y+panel_h-58,184,38},back{panel_x+210,panel_y+panel_h-58,panel_w-228,38};
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle style;
    style.x = panel_x + 18; style.width = panel_w - 36; style.height = 34;
    style.font_size_px = 21; style.padding = 3;
    style.foreground = {248, 248, 248, 255}; style.background = {28, 33, 42, 0};
    style.y = panel_y + 20;
    draw_text_overlay(frame, title, language, style);
    style.font_size_px = 14; style.height = 29;
    style.foreground = {220, 225, 229, 255};
    std::size_t line = 0;
    std::size_t begin = 0;
    while (begin < description.size() && line < 4) {
        const auto end = description.find_first_of("\r\n", begin);
        const auto part = description.substr(begin, end == std::string::npos ? end : end - begin);
        if (!part.empty()) {
            style.y = panel_y + 66 + static_cast<std::int32_t>(line++) * 28;
            draw_text_overlay(frame, part, language, style);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
        if (description[end] == '\r' && begin < description.size() && description[begin] == '\n')
            ++begin;
    }
    line = 0;
    begin = 0;
    style.font_size_px = 15; style.height = 32;
    style.foreground = {245, 245, 245, 255};
    while (begin < summary.size() && line < 5) {
        const auto end = summary.find_first_of("\r\n", begin);
        const auto part = summary.substr(begin, end == std::string::npos ? end : end - begin);
        if (!part.empty()) {
            style.y = panel_y + 190 + static_cast<std::int32_t>(line++) * 34;
            draw_text_overlay(frame, part, language, style);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
        if (summary[end] == '\r' && begin < summary.size() && summary[begin] == '\n') ++begin;
    }
    ui_image(frame,art,"Shared/Sys/WinBtn.bmp",accept,4);
    ui_image(frame,art,"Shared/Sys/WinBtn.bmp",back,4);
    ui_caption(frame,accept,localized_accept,language);
    ui_caption(frame,back,localized_back,language);
#endif
    window.set_title("OpenDollhouse | "+title);
    window.present(frame);
    for (;;) {
        const auto input = window.poll();
        if (input.quit) {
            if (input.quit_reason == WindowQuitReason::escape_key)
                window.clear_escape_quit_reason();
            return NeighborhoodLotPanelChoice::back_to_map;
        }
        if (input.confirm) return NeighborhoodLotPanelChoice::continue_to_lot;
        if (input.primary_click) {
            const auto x = (*input.primary_click)[0], y = (*input.primary_click)[1];
            if(accept.contains(x,y))return NeighborhoodLotPanelChoice::continue_to_lot;
            if(back.contains(x,y))return NeighborhoodLotPanelChoice::back_to_map;
        }
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

OccupiedLotChoice show_occupied_lot_choice(LinuxWindow& window,
        const odh::FamilyEntry& family, std::uint16_t house_number,
        bool move_out_supported, std::uint8_t language,const PreparedNeighborhoodMap* map) {
    const auto current_house = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(family.family.house_number) & 0xffffU);
    if (house_number == 0 || house_number > 98 || current_house != house_number ||
        language < 1 || language > 20)
        throw odh::FormatError("occupied-lot choice received an invalid family");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    odh::RasterFrame frame{};
    frame.width = window.width(); frame.height = window.height();
    frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                        std::array<std::uint8_t,4>{35, 39, 48, 255});
    frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
    if(map&&map->frame.width==800&&map->frame.height==600)frame=map->frame;
    const auto skin=screen_art(map?map->ui_graphics:std::filesystem::path{},{"Nbhd/HotSpotPopupBkg.bmp","Shared/Sys/WinBtn.bmp"});
    const auto* art=skin?&*skin:nullptr;
    const odh::UiBitmapRect enter{230,220,340,36},evict{230,276,340,36},rename{230,320,340,36},cancel{230,380,340,36};
    ui_image(frame,art,"Nbhd/HotSpotPopupBkg.bmp",{204,140,392,296});
    const auto button=[&](odh::UiBitmapRect box,const char* label,bool enabled){
        ui_image(frame,art,"Shared/Sys/WinBtn.bmp",box,4,enabled?0:3);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        ui_caption(frame,box,label,language);
#else
        (void)label;
#endif
    };
    const auto title=family_display_label(family);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    ui_caption(frame,{230,157,340,38},title,language);
#endif
    button(enter,language==14?"Entrar na casa":"Enter house",true);
    button(evict,language==14?"Despejar família":"Evict family",move_out_supported);
    button(rename,language==14?"Renomear família":"Rename family",true);
    button(cancel,language==14?"Cancelar":"Cancel",true);
    window.set_title("OpenDollhouse | "+title);
    window.present(frame);
    for (;;) {
        const auto input=window.poll();
        if(input.quit)return OccupiedLotChoice::cancel;
        if(input.text_input_utf8=="r"||input.text_input_utf8=="R")return OccupiedLotChoice::rename;
        if(input.move_out&&move_out_supported)return OccupiedLotChoice::move_out;
        if(input.primary_click){
            const auto x=(*input.primary_click)[0],y=(*input.primary_click)[1];
            if(enter.contains(x,y))return OccupiedLotChoice::enter;
            if(cancel.contains(x,y))return OccupiedLotChoice::cancel;
            if(rename.contains(x,y))return OccupiedLotChoice::rename;
            if(evict.contains(x,y)&&move_out_supported)return OccupiedLotChoice::move_out;
        }else if(input.action)return OccupiedLotChoice::enter;
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<MoveInFamilySelection> show_move_in_family_picker(
        LinuxWindow& window, const std::vector<odh::FamilyEntry>& families,
        std::uint16_t house_number, std::int32_t purchase_price, std::uint8_t language,
        const std::string& localized_action, const std::string& localized_title,
        const std::string& localized_no_family_title,
        const std::string& localized_no_family_message,
        const std::string& localized_cancel,
        const std::filesystem::path& ui_graphics,
        const std::string& localized_net_worth,
        const std::string& localized_family_friends,
        const std::string& localized_archive,
        const std::vector<std::uint16_t>& deletable_family_ids,NeighborhoodPortraitProvider portraits) {
    if (house_number > 98 || purchase_price < 0 || language < 1 || language > 20)
        throw odh::FormatError("family picker received an invalid move-in request");
    std::vector<const odh::FamilyEntry*> choices;
    for(const auto& entry:families){
        if((std::uint32_t(entry.family.house_number)&0xffffU)==0&&entry.family.family_number>=0&&
           !entry.family.member_guids.empty()&&entry.family.member_guids.size()<=8)choices.push_back(&entry);
    }
    const auto can_move=[&](const odh::FamilyEntry& entry){return !house_number||entry.family.budget>=purchase_price;};
    const auto can_archive=[&](const odh::FamilyEntry& entry){
        return std::find(deletable_family_ids.begin(),deletable_family_ids.end(),entry.chunk_id)!=deletable_family_ids.end();
    };
    const auto skin=screen_art(ui_graphics,{"Nbhd/PickBkg.bmp","Nbhd/FamilyPickItemBkg.bmp","Nbhd/PAFAddFamily.bmp",
        "Nbhd/PAFDeleteFamily.bmp","Nbhd/PAFMoveIn.bmp","Nbhd/NbhdTileBtn.bmp","CPanel/People/PeopleTemplate.bmp",
        "NghUI/Previous.bmp","NghUI/Next.bmp"});
    const auto* art=skin?&*skin:nullptr;
    const auto* background=art?art->find("Nbhd/PickBkg.bmp"):nullptr;
    const auto* rows=art?art->find("Nbhd/FamilyPickItemBkg.bmp"):nullptr;
    if(background&&(background->width!=800||background->height!=600))throw odh::FormatError("family picker dimensions changed");
    if(rows&&(rows->width!=690||rows->height!=204))throw odh::FormatError("family picker row dimensions changed");
    if(window.width()!=800||window.height()!=600)window.resize(800,600);
    FamilyPickerPresentation view;
    view.choices=choices;view.can_move=can_move;view.can_archive=can_archive;view.rename=house_number!=0;
    if(house_number&&!choices.empty())view.selected=0;
    auto& selected=view.selected;auto& first=view.first;auto& activation=view.activation;auto& pointer=view.pointer;
    constexpr auto page_size=FamilyPickerPresentation::page_size;
    constexpr auto row_x=FamilyPickerPresentation::row_x,row_y=FamilyPickerPresentation::row_y,row_height=FamilyPickerPresentation::row_height;
    constexpr auto cancel_box=FamilyPickerPresentation::cancel_box,add_box=FamilyPickerPresentation::add_box,archive_box=FamilyPickerPresentation::archive_box,
        move_box=FamilyPickerPresentation::move_box,rename_box=FamilyPickerPresentation::rename_box,previous_box=FamilyPickerPresentation::previous_box,next_box=FamilyPickerPresentation::next_box;
    const auto entry=[&](){return view.entry();};
    const auto selection=[&](MoveInFamilySelection::Action action){
        const auto& e=*entry();return MoveInFamilySelection{e.chunk_id,odh::family_overlay_identity(e),action};
    };
    const auto target_at=[&](int x,int y){return view.target_at(x,y);};
    const auto render=[&]{window.set_title("OpenDollhouse | "+localized_title);window.present(view.draw(art,language,localized_title,
        localized_action,localized_archive,localized_cancel,localized_no_family_title,localized_no_family_message,portraits));};
    (void)localized_net_worth;(void)localized_family_friends; // Original header icons are part of PickBkg.
    render();
    for(;;){
        const auto input=window.poll(true);
        if(input.quit){if(input.quit_reason==WindowQuitReason::escape_key)window.clear_escape_quit_reason();return {};}
        if(input.create_family)return MoveInFamilySelection{0,{},MoveInFamilySelection::Action::create_family};
        if(house_number&&entry()&&(input.text_input_utf8=="r"||input.text_input_utf8=="R"))
            return selection(MoveInFamilySelection::Action::rename);
        if(input.interaction_selection_delta&&!choices.empty()){
            activation.reset();
            selected=selected?odh::advance_resident_selection(*selected,input.interaction_selection_delta,choices.size()):
                input.interaction_selection_delta>0?0:choices.size()-1;
            first=*selected/page_size*page_size;render();
        }
        const auto previous_pointer=pointer;
        const auto previous_press=activation.pressed();
        pointer=input.pointer_position;
        const auto click=activation.click(input,target_at);
        if(click){
            const auto x=(*click)[0],y=(*click)[1];
            if(cancel_box.contains(x,y))return {};
            if(add_box.contains(x,y))return MoveInFamilySelection{0,{},MoveInFamilySelection::Action::create_family};
            if(entry()&&archive_box.contains(x,y)&&can_archive(*entry()))return selection(MoveInFamilySelection::Action::archive);
            if(entry()&&move_box.contains(x,y)&&can_move(*entry()))return selection(MoveInFamilySelection::Action::move_in);
            if(house_number&&entry()&&rename_box.contains(x,y))return selection(MoveInFamilySelection::Action::rename);
            if(previous_box.contains(x,y)&&first){first-=page_size;selected=first;render();}
            else if(next_box.contains(x,y)&&first+page_size<choices.size()){first+=page_size;selected=first;render();}
            else if(x>=row_x&&x<row_x+690&&y>=row_y&&y<row_y+int(page_size)*row_height){
                const auto i=first+std::size_t((y-row_y)/row_height);
                selected=i<choices.size()?std::optional<std::size_t>{i}:std::nullopt;render();
            }
        }else if(input.action&&!input.primary_click&&input.pointer_events.empty()&&entry()&&can_move(*entry()))return selection(MoveInFamilySelection::Action::move_in);
        if(input.redraw||previous_pointer!=pointer||previous_press!=activation.pressed())render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<std::string> show_family_name_editor(LinuxWindow& window,
        const std::string& initial_name_utf8, std::uint8_t language,
        const std::string& localized_title, const std::string& localized_accept,
        const std::string& localized_cancel,const std::filesystem::path& ui_graphics,
        const std::string& localized_prompt) {
    if(language<1||language>20)throw odh::FormatError("invalid family editor language");
    if(window.width()!=800||window.height()!=600)window.resize(800,600);
    odh::RasterFrame backdrop;backdrop.width=800;backdrop.height=600;
    backdrop.pixels=window.readback();
    backdrop.depth.assign(backdrop.pixels.size(),std::numeric_limits<float>::infinity());
    const auto skin=screen_art(ui_graphics,{"Shared/Sys/WinBtn.bmp",
        "CPanel/Backgrounds/PopupInfoTiles.bmp","Nbhd/DsgnFamBkg.bmp"});
    const auto layout=original_name_prompt_layout(800,600);
    // The launcher already owns the translated text catalog. Reuse its label;
    // opening this modal must not reread the whole UIText archive.
    const std::string prompt=localized_prompt.empty()?
        (language==14?"Digite o sobrenome:":"Enter Last Name:"):localized_prompt;
    std::string name=initial_name_utf8,feedback;
    bool replace_on_input=true;
    std::size_t focus=0; // Field, accept and cancel share keyboard/mouse focus.
    ScreenPointer activation;
    std::optional<std::array<std::int32_t,2>> pointer;
    const auto render=[&]{
        auto frame=backdrop;
        draw_original_name_prompt(frame,layout,skin?&*skin:nullptr,localized_title,
            feedback.empty()?prompt:feedback,name,localized_accept,localized_cancel,language,
            focus==0,replace_on_input,pointer,activation.pressed(),focus?std::optional<std::size_t>{focus-1}:std::nullopt);
        window.set_title("OpenDollhouse | "+localized_title);
        window.present(frame);
    };
    const auto invalid_name=[&]{return language==14?
        "Use um nome mais curto, com caracteres disponíveis no idioma escolhido.":
        "Use a shorter name with characters supported by the selected language.";};
    render();
    for(;;){
        const auto input=window.poll();
        if(input.quit){
            if(input.quit_reason==WindowQuitReason::escape_key)window.clear_escape_quit_reason();
            return std::nullopt;
        }
        bool repaint=input.redraw;
        if(input.interaction_selection_delta){
            focus=std::size_t((int(focus)+input.interaction_selection_delta%3+3)%3);
            activation.reset();repaint=true;
        }
        if(focus==0&&!input.text_input_utf8.empty()){
            if(replace_on_input)name.clear();
            replace_on_input=false;
            if(name.size()+input.text_input_utf8.size()<=1024){name+=input.text_input_utf8;feedback.clear();}
            else feedback=invalid_name();
            repaint=true;
        }
        if(focus==0&&input.text_backspace){
            if(replace_on_input)name.clear();
            else if(!name.empty()){
                auto at=name.size()-1;
                while(at&&(static_cast<unsigned char>(name[at])&0xc0U)==0x80U)--at;
                name.erase(at);
            }
            replace_on_input=false;feedback.clear();repaint=true;
        }
        const auto previous_pointer=pointer;
        const auto previous_press=activation.pressed();
        pointer=input.pointer_position;
        const auto click=activation.click(input,[&](int x,int y){return original_name_prompt_target(layout,x,y);});
        std::optional<std::size_t> choice;
        if(click){
            choice=original_name_prompt_target(layout,(*click)[0],(*click)[1]);
            if(choice){focus=*choice;if(focus==0){replace_on_input=false;choice.reset();}repaint=true;}
        }
        if(input.confirm)choice=focus==2?2:1;
        if(choice==2)return std::nullopt;
        if(choice==1){
            const auto first=name.find_first_not_of(" "),last=name.find_last_not_of(" ");
            if(first==std::string::npos){feedback=prompt;focus=0;repaint=true;}
            else{
                const auto trimmed=name.substr(first,last-first+1);
                feedback.clear();
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
                try{
                    const auto encoded=utf8_to_legacy_text(trimmed,language,1024);
                    if(encoded.size()>256)throw std::length_error("family name exceeds byte limit");
                    if(legacy_text_to_utf8(encoded,language)!=trimmed)
                        throw std::invalid_argument("family name cannot roundtrip exactly");
                }catch(const std::exception&){feedback=invalid_name();focus=0;repaint=true;}
                if(feedback.empty())return trimmed;
#else
                if(trimmed.size()<=256)return trimmed;
                feedback=invalid_name();focus=0;repaint=true;
#endif
            }
        }
        if(repaint||pointer!=previous_pointer||activation.pressed()!=previous_press)render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

namespace {
// One presentation and hit-test path for all management confirmations. The
// caller owns the transaction and revalidates it after this returns intent.
bool show_management_confirmation(LinuxWindow& window, std::uint8_t language,
        const std::string& title, const std::string& question,
        const std::string& yes, const std::string& no,
        const std::filesystem::path& ui_graphics) {
    odh::RasterFrame backdrop{};
    backdrop.width=window.width();backdrop.height=window.height();
    backdrop.pixels=window.readback();
    backdrop.depth.assign(backdrop.pixels.size(),std::numeric_limits<float>::infinity());
    auto art=screen_art(ui_graphics,{"Shared/Sys/WinBtn.bmp","CPanel/Backgrounds/PopupInfoTiles.bmp"});
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    odh::DialogState state;
    state.instruction.operands[5]=1; // Yes/No; no VM ticket or mutation.
    state.prompt.title=title;state.prompt.message=question;
    state.prompt.yes=yes;state.prompt.no=no;
    VmDialogAppearance appearance;
    appearance.button_strip=art?art->find("Shared/Sys/WinBtn.bmp"):nullptr;
    appearance.panel_tiles=art?art->find("CPanel/Backgrounds/PopupInfoTiles.bmp"):nullptr;
    const auto layout=vm_message_dialog_layout(backdrop.width,backdrop.height,state,language,appearance);
    if(!layout)throw odh::FormatError("management confirmation window is too small");
    const auto hit=[&](int x,int y){return vm_dialog_button_hit(*layout,1,x,y);};
    std::size_t first_line=0;
    TextOverlayResult page;
    const auto render=[&]{auto frame=backdrop;page=draw_vm_dialog(frame,state,language,first_line,appearance);window.present(frame);};
#else
    (void)language;(void)title;(void)question;(void)yes;(void)no;(void)art;
    // Without a text backend retain explicit buttons, never a whole-window
    // affirmative click. This diagnostic fallback is not original-UI proof.
    const int x=static_cast<int>(backdrop.width)/2,y=static_cast<int>(backdrop.height)/2;
    const odh::UiBitmapRect yes_box{x-114,y+25,104,33},no_box{x+10,y+25,104,33};
    const auto hit=[&](int px,int py)->std::optional<std::uint8_t>{
        if(yes_box.contains(px,py))return 0;
        if(no_box.contains(px,py))return 1;
        return {};
    };
    const auto render=[&]{auto frame=backdrop;
        for(const auto box:{yes_box,no_box})for(int py=box.y;py<box.y+box.height;++py)
            for(int px=box.x;px<box.x+box.width;++px)
                if(px>=0&&py>=0&&px<int(frame.width)&&py<int(frame.height))
                    frame.pixels[std::size_t(py)*frame.width+std::size_t(px)]={35,39,48,255};
        window.present(frame);};
#endif
    window.set_title("OpenDollhouse | "+title);
    render();
    CasPointer pointer;
    for(;;){
        const auto input=window.poll();
        if(input.quit){
            if(input.quit_reason==WindowQuitReason::escape_key){
                window.clear_escape_quit_reason();window.present(backdrop);
            }
            return false;
        }
        if(input.confirm||(input.action&&!input.primary_click)){window.present(backdrop);return true;}
        if(const auto click=pointer.click_on(input,hit)){
            if(const auto response=hit((*click)[0],(*click)[1])){
                window.present(backdrop);return *response==0;
            }
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if(input.dialog_scroll_delta){
            const auto step=std::max<std::size_t>(1,page.visible_lines);
            const auto maximum=page.line_count>page.visible_lines?page.line_count-page.visible_lines:0;
            first_line=input.dialog_scroll_delta>0?std::min(maximum,first_line+step):first_line>step?first_line-step:0;
            render();
        }
#endif
        if(input.redraw)render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
}

bool show_family_archive_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
        std::uint8_t language, const std::string& localized_title,
        const std::string& localized_question, const std::string& localized_yes,
        const std::string& localized_no, const std::filesystem::path& ui_graphics) {
    if(language<1||language>20||
        (static_cast<std::uint32_t>(family.family.house_number)&0xffffU)!=0||
        family.family.member_guids.empty())
        throw odh::FormatError("family deletion confirmation received invalid family data");
    return show_management_confirmation(window,language,localized_title,localized_question,
        localized_yes,localized_no,ui_graphics);
}

bool show_move_in_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
        std::uint16_t house_number, std::int32_t purchase_price, std::uint8_t language,
        const std::string& localized_title, const std::string& localized_question,
        const std::string& localized_yes, const std::string& localized_no,
        const std::filesystem::path& ui_graphics) {
    if(house_number==0||house_number>98||purchase_price<0||language<1||language>20||
        family.family.budget<purchase_price)
        throw odh::FormatError("move-in confirmation received an invalid purchase");
    return show_management_confirmation(window,language,localized_title,localized_question,
        localized_yes,localized_no,ui_graphics);
}

bool show_move_out_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
        std::uint16_t house_number, std::uint8_t language,
        const std::string& localized_title, const std::string& localized_yes,
        const std::string& localized_no, const std::filesystem::path& ui_graphics,
        const std::string& localized_question) {
    if(house_number==0||house_number>98||language<1||language>20||
        family.family.value_in_arch<0||family.family.budget<0)
        throw odh::FormatError("move-out confirmation received invalid family data");
    const auto next_budget=static_cast<std::int64_t>(family.family.budget)+family.family.value_in_arch;
    if(next_budget>std::numeric_limits<std::int32_t>::max())
        throw odh::FormatError("move-out refund overflows the family budget");
    return show_management_confirmation(window,language,localized_title,localized_question,
        localized_yes,localized_no,ui_graphics);
}

std::optional<NeighborhoodMapRequest> show_neighborhood_resume_picker(
        LinuxWindow& window, const PreparedNeighborhoodMap& map, std::uint16_t active_house) {
    if (window.width() != 800 || window.height() != 600)
        throw odh::FormatError("neighborhood picker requires an 800x600 window");
    window.set_title("OpenDollhouse | "+std::string(map.language==14?"Vizinhança":"Neighborhood"));
    (void)active_house;
    auto frame=map.frame;
    if(frame.width!=800||frame.height!=600||frame.pixels.size()!=800*600)
        throw odh::FormatError("neighborhood resume map frame is invalid");
    std::optional<std::array<std::int32_t,2>> management_button;
    constexpr std::array<std::array<std::int32_t,2>,4> corners{{
        {24,548},{456,548},{24,24},{456,24}}};
    const auto radius=std::int64_t{6}*map.marker_scale;
    for(const auto& corner:corners){
        const auto x=std::int64_t{corner[0]},y=std::int64_t{corner[1]};
        const bool obscures_lot=std::any_of(map.markers.begin(),map.markers.end(),[&](const auto& marker){
            const auto mx=std::int64_t{marker.location.x},my=std::int64_t{marker.location.y};
            return mx+radius>=x&&mx-radius<x+320&&my+radius>=y&&my-radius<y+40;
        });
        const bool obscures_house=std::any_of(map.houses.begin(),map.houses.end(),[&](const auto& house){
            const auto& hit=house.hit;
            return hit.x<x+320&&hit.x+hit.width>x&&hit.y<y+40&&hit.y+hit.height>y;
        });
        if(!obscures_lot&&!obscures_house){management_button=corner;break;}
    }
    if(!management_button)management_button=corners.front();
    if(management_button){
        const auto skin=screen_art(map.ui_graphics,{"Shared/Sys/WinBtn.bmp"});
        const odh::UiBitmapRect box{(*management_button)[0],(*management_button)[1],320,40};
        ui_image(frame,skin?&*skin:nullptr,"Shared/Sys/WinBtn.bmp",box,4);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        ui_caption(frame,box,map.language==14?"Famílias":"Families",map.language);
#endif
    }
    window.present(frame);
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if(input.move_out)
            return NeighborhoodMapRequest{NeighborhoodRequestKind::manage_families,0};
        if (input.primary_click) {
            if(management_button&&(*input.primary_click)[0]>=(*management_button)[0]&&
               (*input.primary_click)[0]<(*management_button)[0]+320&&
               (*input.primary_click)[1]>=(*management_button)[1]&&
               (*input.primary_click)[1]<(*management_button)[1]+40)
                return NeighborhoodMapRequest{NeighborhoodRequestKind::manage_families,0};
            const auto selected = pick_map_lot(map,(*input.primary_click)[0],(*input.primary_click)[1]);
            if (selected) return NeighborhoodMapRequest{NeighborhoodRequestKind::travel,*selected};
        }
        if (input.redraw) window.present(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

std::optional<std::size_t> show_resident_picker(
        LinuxWindow& window, const std::vector<odh::SessionEntryMember>& residents,
        std::uint8_t language) {
    if (residents.empty()) throw odh::FormatError("selected family has no residents");
    if (window.width() != 800 || window.height() != 600) window.resize(800, 600);
    std::size_t selected = 0;
    constexpr std::size_t row_height = 36;
    const std::size_t page_size = std::max<std::size_t>(1, (window.height() - 184) / row_height);
    const auto render = [&]() {
        const std::size_t page_start = (selected / page_size) * page_size;
        odh::RasterFrame frame{};
        frame.width = window.width(); frame.height = window.height();
        frame.pixels.assign(static_cast<std::size_t>(frame.width) * frame.height,
                            std::array<std::uint8_t,4>{35, 39, 48, 255});
        frame.depth.assign(frame.pixels.size(), std::numeric_limits<float>::infinity());
        for (std::size_t row = 0; row < page_size; ++row) {
            const auto index = page_start + row;
            if (index >= residents.size()) break;
            const auto color = index == selected ? std::array<std::uint8_t,4>{47,93,138,255} :
                std::array<std::uint8_t,4>{52,57,68,255};
            const auto y0 = 92 + row * row_height;
            for (std::size_t y = y0; y < y0 + 32; ++y)
                for (std::size_t x = 24; x + 24 < frame.width; ++x)
                    frame.pixels[y * frame.width + x] = color;
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        TextOverlayStyle style;
        style.x = 24; style.width = static_cast<std::int32_t>(frame.width) - 48;
        style.height = 32; style.font_size_px = 17; style.padding = 7;
        style.foreground = {245,245,245,255}; style.background = {22,25,32,255};
        style.y = 28;
        draw_text_overlay(frame, "Choose a resident - Tab: next, click a row or Enter: continue, Esc: cancel", language, style);
        for (std::size_t row = 0; row < page_size; ++row) {
            const auto index = page_start + row;
            if (index >= residents.size()) break;
            const auto& member = residents[index];
            const auto text = std::string(index == selected ? "> Resident ID " : "  Resident ID ") +
                std::to_string(member.neighbor_id) + " - " + member.character_asset;
            style.y = static_cast<std::int32_t>(92 + row * row_height);
            style.background = {22,25,32,255};
            draw_text_overlay(frame, text, language, style);
        }
        style.y = static_cast<std::int32_t>(frame.height) - 62;
        style.background = {22,25,32,255};
        const auto page_count = (residents.size() + page_size - 1) / page_size;
        draw_text_overlay(frame, "Page " + std::to_string(page_start / page_size + 1) + "/" +
            std::to_string(page_count) + " - selection is by neighborhood ID and character file", language, style);
#else
        (void)language;
#endif
        const auto& member = residents[selected];
        window.set_title("OpenDollhouse | resident " + std::to_string(member.neighbor_id) +
            " | " + member.character_asset + " | Tab: next, Enter: continue, Esc: cancel");
        window.present(frame);
    };
    render();
    for (;;) {
        const auto input = window.poll();
        if (input.quit) return std::nullopt;
        if (input.interaction_selection_delta) {
            selected = odh::advance_resident_selection(selected,
                input.interaction_selection_delta, residents.size());
            render();
        }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if (input.primary_click) {
            const auto y = (*input.primary_click)[1];
            constexpr std::size_t row_start = 92;
            const std::size_t row_end = row_start + page_size * row_height;
            if (y >= static_cast<std::int32_t>(row_start) && y < static_cast<std::int32_t>(row_end)) {
                const auto row = static_cast<std::size_t>(y - static_cast<std::int32_t>(row_start)) / row_height;
                const auto page_start = (selected / page_size) * page_size;
                const auto clicked = odh::resident_selection_row(page_start, row, page_size, residents.size());
                if (clicked) return clicked;
            }
        }
#endif
        if (input.action && !input.primary_click) return selected;
        if (input.redraw) render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}

} // namespace odh_host
