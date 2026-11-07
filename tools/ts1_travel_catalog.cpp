#include "ts1_travel_catalog.hpp"
#include "odh/lot_locations.hpp"
#include "odh/ts1_travel.hpp"
#include <algorithm>
#include <array>
#include "linux_text_codec.hpp"
#include "resource.hpp"
namespace odh_host {
namespace {
struct Selector { std::uint8_t type,mode; };
constexpr std::array<Selector,5> selectors{{{5,2},{7,3},{8,4},{11,5},{13,7}}};
constexpr std::uint64_t file_limit=64u*1024u*1024u;
void bounded_path(const std::filesystem::path& path){
    if(path.empty()||path.native().size()>4096)throw odh::FormatError("travel catalog path exceeds bound");
}
bool within(const std::filesystem::path& root,const std::filesystem::path& path){
    const auto relative=path.lexically_relative(root);
    return !relative.empty()&&!relative.is_absolute()&&*relative.begin()!="..";
}
std::filesystem::path checked_file(const std::filesystem::path& root,const std::filesystem::path& path){
    bounded_path(path);const auto canonical=std::filesystem::canonical(path);bounded_path(canonical);
    if(!within(root,canonical)||!std::filesystem::is_regular_file(canonical)||std::filesystem::file_size(canonical)>file_limit)
        throw odh::FormatError("travel resource is outside the installation or file bound");
    return canonical;
}
std::optional<std::string> lot_name(const std::filesystem::path& root,const std::filesystem::path& user,
    std::uint16_t house,std::uint8_t language){
    const auto file=user/"Houses"/(house<80?"NeighborhoodDesc.iff":house<90?"STDesc.iff":"MTDesc.iff");
    std::error_code error;
    if(!std::filesystem::exists(file,error)){if(error)throw odh::FormatError("cannot inspect travel lot text resource");return {};}
    const auto path=checked_file(root,file);
    std::optional<std::string> result;bool found=false;
    with_iff_file(path.string(),[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type!="STR#"||chunk.id!=2000+house)return;
        if(found)throw odh::FormatError("ambiguous localized travel lot name");
        found=true;
        odh::AnimationLimits limits;limits.strings=4096;limits.text_bytes=1024*1024;
        const auto table=odh::read_strings(chunk.payload,limits);
        if(const auto* entry=odh::string_in_language_or_english(table,language,0))
            result=legacy_text_to_utf8(entry->value,entry->language?entry->language:std::uint8_t{1},4096);
    });});
    return result;
}

using TravelMaps=std::array<std::optional<odh::LotLocations>,8>;
TravelMaps read_maps(const std::filesystem::path& root,const std::filesystem::path& user,
        std::optional<unsigned> only_mode={}) {
    TravelMaps maps;
    const auto file=checked_file(root,user/"LotLocations.iff");
    with_iff_file(file.string(),[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type!="STR#"||(only_mode&&chunk.id!=*only_mode)||
           std::none_of(selectors.begin(),selectors.end(),[&](auto s){return s.mode==chunk.id;}))return;
        auto& map=maps[chunk.id];
        if(map)throw odh::FormatError("ambiguous travel LotLocations mode");
        // Authored coordinates use English group 1 independently of the UI language.
        map=odh::read_lot_locations(chunk.payload,1);
    });});
    return maps;
}
std::optional<unsigned> unique_mode(const TravelMaps& maps,std::uint16_t house) {
    std::optional<unsigned> selected;
    for(const auto& selector:selectors){
        const auto& map=maps[selector.mode];
        if(!map||std::none_of(map->lots.begin(),map->lots.end(),[&](const auto& lot){return lot.house==house;}))continue;
        if(selected)return {};
        selected=selector.mode;
    }
    return selected;
}
std::filesystem::path house_path(const std::filesystem::path& user,std::uint16_t house) {
    return user/"Houses"/("House"+std::string(house<10?"0":"")+std::to_string(house)+".iff");
}
std::optional<std::vector<odh::DialogChoice>> load_choices(
    const std::filesystem::path& game_root,const std::filesystem::path& user_data,
    std::uint8_t type,std::uint8_t language,std::string& reason,bool require_unique){
    reason.clear();
    const auto selector=std::find_if(selectors.begin(),selectors.end(),[&](auto s){return s.type==type;});
    if(selector==selectors.end()){reason="dialog selector type has no implemented resource-backed travel catalog";return {};}
    const auto mode=selector->mode;
    try{
        bounded_path(game_root);bounded_path(user_data);
        const auto root=std::filesystem::canonical(game_root),user=std::filesystem::canonical(user_data);
        if(!std::filesystem::is_directory(root)||!std::filesystem::is_directory(user)||!within(root,user))
            throw odh::FormatError("travel user-data directory must remain inside the installation");
        const auto maps=read_maps(root,user,require_unique?std::nullopt:std::optional<unsigned>{mode});
        const auto& locations=maps[mode];
        if(!locations)throw odh::FormatError("travel LotLocations mode is unavailable");
        std::vector<odh::DialogChoice> choices;choices.reserve(locations->lots.size());
        for(const auto& location:locations->lots){
            if(require_unique&&unique_mode(maps,location.house)!=mode)continue;
            const auto file=house_path(user,location.house);
            std::error_code error;
            if(!std::filesystem::exists(file,error)){if(error)throw odh::FormatError("cannot inspect travel destination resource");continue;}
            (void)checked_file(root,file);
            auto label=lot_name(root,user,location.house,language).value_or("");
            if(label.empty())label=std::string(language==14?"Lote ":"Lot ")+std::to_string(location.house);
            choices.push_back({std::move(label),location.house});
            if(choices.size()>4096)throw odh::FormatError("travel choice bound exceeded");
        }
        return choices;
    }catch(const std::exception& error){reason=error.what();return {};}
}
}
std::optional<std::vector<odh::DialogChoice>> load_ts1_travel_choices(
    const std::filesystem::path& game,const std::filesystem::path& user,
    std::uint8_t type,std::uint8_t language,std::string& reason){
    return load_choices(game,user,type,language,reason,false);
}
std::optional<std::vector<odh::DialogChoice>> load_ts1_travel_selector_choices(
    const std::filesystem::path& game,const std::filesystem::path& user,
    std::uint8_t type,std::uint8_t language,std::string& reason){
    return load_choices(game,user,type,language,reason,true);
}
bool ts1_travel_selector_allowed(bool all_destinations,std::uint8_t type) noexcept {
    return std::any_of(selectors.begin(),selectors.end(),[&](auto s){
        return s.type==type&&(all_destinations||s.mode==2);
    });
}

namespace {
unsigned installed_destination_mode(const std::filesystem::path& game,
        const std::filesystem::path& user,std::uint16_t house) {
    bounded_path(game);bounded_path(user);
    const auto root=std::filesystem::canonical(game),data=std::filesystem::canonical(user);
    if(!within(root,data))throw odh::FormatError("travel user-data directory must remain inside the installation");
    const auto selected=unique_mode(read_maps(root,data),house);
    if(!selected)throw odh::FormatError("temporary travel lot is absent or ambiguous across expansion maps");
    (void)checked_file(root,house_path(data,house));
    return *selected;
}
}

void validate_ts1_travel_source(const std::filesystem::path& game,const std::filesystem::path& user,
        const odh::Ts1TravelState& state,std::uint8_t /*language*/,bool all_destinations) {
    if(state.current_house==state.home_house){
        if(state.current_mode!=state.home_mode)throw odh::FormatError("temporary travel home mode differs from its source");
        return;
    }
    if(!all_destinations&&state.current_mode!=2)
        throw odh::FormatError("this visit is outside the enabled Downtown travel route");
    if(installed_destination_mode(game,user,state.current_house)!=state.current_mode)
        throw odh::FormatError("temporary travel source differs from its installed neighborhood map");
}

unsigned resolve_ts1_travel_destination(const std::filesystem::path& game,const std::filesystem::path& user,
        const odh::Ts1TravelState& state,int requested,std::uint8_t language,bool all_destinations) {
    validate_ts1_travel_source(game,user,state,language,all_destinations);
    if(requested!=-1&&(requested<1||requested>98))throw odh::FormatError("temporary destination is outside the lot range");
    const auto target=requested==-1?state.home_house:static_cast<std::uint16_t>(requested);
    if(target==state.current_house)throw odh::FormatError("temporary travel cannot replay a transition to its current lot");
    if(target==state.home_house)return state.home_mode;
    const auto mode=installed_destination_mode(game,user,target);
    if(!all_destinations&&state.current_house!=state.home_house)
        throw Ts1TravelPolicyDenied("onward travel from Downtown is not enabled");
    if(!all_destinations&&mode!=2)throw Ts1TravelPolicyDenied("this destination is outside the enabled Downtown travel route");
    return mode;
}

}
