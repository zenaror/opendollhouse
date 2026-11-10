#include "person_runtime.hpp"
#include "person_session.hpp"
#include "property_release_session.hpp"
#include "residential_property_scene.hpp"
#include "simulation_speed.hpp"
#ifdef ODH_HAVE_X11
#include "original_cursor_presentation.hpp"
#endif
#ifdef ODH_HAVE_X11
#include "neighborhood_screen.hpp"
#include "community_lot_editor.hpp"
#include "neighborhood_lot_admission.hpp"
#include "neighborhood_portraits.hpp"
#include "original_ui_art.hpp"
#include "linux_text_renderer.hpp"
#include "linux_vm_dialog.hpp"
#include "linux_cas_layout.hpp"
#include "odh/lot_context.hpp"
#include "linux_ui_strings.hpp"
#ifdef ODH_HAVE_LINUX_CAS_SCREEN
#include "linux_cas_screen.hpp"
#include "linux_simitone_cas_member.hpp"
#endif
#endif
#ifdef ODH_HAVE_LINUX_PRIVATE_FAMILY
#include "linux_private_family.hpp"
#endif
#include "resource.hpp"
#ifdef ODH_HAVE_LINUX_SESSION_LOG
#include "linux_session_log.hpp"
#endif

#include "odh/session_entry.hpp"
#include "odh/character_catalog.hpp"
#include "odh/saved_objects.hpp"
#include "odh/occupancy_projection.hpp"
#include "odh/animation.hpp"
#include "odh/session_bundle.hpp"
#include "odh/family.hpp"
#include "odh/family_overlay.hpp"
#include "odh/lot.hpp"
#ifdef __linux__
#include "linux_session_archive_store.hpp"
#include "community_lot_store.hpp"
#include "linux_family_overlay_store.hpp"
#include "linux_family_session_journal.hpp"
#include "linux_family_transfer.hpp"
#include "linux_travel_session.hpp"
#ifdef ODH_HAVE_TS1_TRAVEL_CATALOG
#include "ts1_travel_catalog.hpp"
#endif
#endif
#include "odh/sha256.hpp"
#include "odh/ts1_travel_return.hpp"
#include "odh/session_source_checkpoint.hpp"
#include "odh/session_resource_identity.hpp"
#include "json.hpp"
#ifdef ODH_HAVE_LINUX_USER_CONFIG
#include "linux_user_config.hpp"
#endif
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
#include "linux_text_codec.hpp"
#endif
#include "session_resource_manifest.hpp"

#include <algorithm>
#include <thread>
#include <chrono>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
#ifdef ODH_HAVE_X11
std::string_view loading_label(std::uint8_t language) {
    static constexpr std::array<std::string_view,20> labels{
        "Loading...","Loading...","Chargement...","Lädt...","Caricamento...",
        "Cargando...","Laden...","Indlæser...","Laddar...","Laster...",
        "Ladataan...","טוען...","Загрузка...","Carregando...","読み込み中...",
        "Wczytywanie...","正在加载...","正在載入...","กำลังโหลด...","불러오는 중..."};
    return language>=1&&language<=labels.size()?labels[language-1]:labels[0];
}

void present_loading_screen(odh_host::LinuxWindow& window, std::uint8_t language) {
    odh::RasterFrame frame{};
    frame.width=window.width();
    frame.height=window.height();
    const auto pixels=static_cast<std::size_t>(frame.width)*frame.height;
    frame.pixels.assign(pixels,{15,22,29,255});
    frame.depth.assign(pixels,std::numeric_limits<float>::infinity());
    const auto card_width=std::min<std::uint32_t>(frame.width,480);
    const auto card_height=std::min<std::uint32_t>(frame.height,88);
    const auto left=(frame.width-card_width)/2;
    const auto top=(frame.height-card_height)/2;
    for(std::uint32_t y=top;y<top+card_height;++y)
        for(std::uint32_t x=left;x<left+card_width;++x)
            frame.pixels[static_cast<std::size_t>(y)*frame.width+x]={39,52,63,255};
    for(std::uint32_t x=left;x<left+card_width;++x)
        frame.pixels[static_cast<std::size_t>(top)*frame.width+x]={82,186,170,255};
    odh_host::TextOverlayStyle style;
    style.x=static_cast<std::int32_t>(left+20);
    style.y=static_cast<std::int32_t>(top+(card_height>36?(card_height-36)/2:0));
    style.width=static_cast<std::int32_t>(card_width>40?card_width-40:card_width);
    style.height=36;
    style.font_size_px=22;
    style.padding=2;
    style.foreground={240,245,247,255};
    style.background={39,52,63,0};
    (void)odh_host::draw_text_overlay(frame,loading_label(language),language,style);
    const auto message=loading_label(language);
    window.set_title("OpenDollhouse | "+std::string(message));
    window.present(frame);
}
#endif

struct Options {
    struct ResidentGoHere {unsigned entity_id,x,y,level,tick;};
    struct SingleResidentGoHere {unsigned actor_id,x,y,level,tick;};
    struct ResidentSit {unsigned actor_id,object_id,stand_tick;};
    struct ResidentInteraction {unsigned actor_id,object_id,tick;std::optional<unsigned> row;
        std::optional<std::int16_t> variant_param0;unsigned variant_language=0;std::string variant_name;bool variant_hex=false;};
    struct ResidentStand {unsigned actor_id,tick;};
    struct EntityMove {unsigned id,x,y,level,direction,tick;};
    std::filesystem::path game_root;
    unsigned mode = 0;
    unsigned language = 1;
    unsigned neighborhood = 1;
    odh_host::PlayerPreferences preferences;
    std::optional<unsigned> house;
    std::optional<unsigned> resident_id;
    bool plan_only = false, choose_language = false;
    bool original_companion_protocol = false;
    std::optional<odh_host::SessionObjectCommand> object_command;
    std::optional<odh::ArchitectureEditCommand> architecture_command;
    std::optional<std::filesystem::path> load_session;
    bool load_session_advisory = false;
    std::optional<unsigned> headless_ticks;
    std::optional<unsigned> headless_checkpoint_interval;
    std::optional<std::array<unsigned,2>> lot_window_size;
    std::optional<unsigned> stop_route_depth;
    std::optional<EntityMove> headless_entity_move;
    bool headless_dialog_ok=false;
    unsigned headless_info_dialog_limit=0;
    bool check_recovery_position=false,place_recovery_position=false;
    bool recover_saved_code=false;
    struct DialogResponse {unsigned code;std::string text;};
    std::vector<DialogResponse> headless_dialog_responses;
    std::optional<unsigned> headless_move_out_family, headless_move_in_family;
    std::optional<unsigned> instruction_budget;
    std::optional<std::filesystem::path> status_frame;
    std::optional<unsigned> status_tick;
    std::vector<unsigned> headless_travel_houses;
    std::optional<unsigned> headless_travel_tick;
    bool save_on_exit = false;
    bool resume_buy_mode = false;
    bool paused_before_buy_mode = false;
    bool resume_build_mode = false;
    bool paused_before_build_mode = false;
    std::optional<odh_host::BuildModePresentationState> build_presentation;
    bool audio_enabled=false,audio_disabled=false;
    unsigned audio_drain_ms=0;
    bool single_resident_session = false;
    std::optional<unsigned> headless_sit_object;
    std::optional<unsigned> headless_stand_tick;
    std::optional<ResidentSit> headless_resident_sit;
    std::optional<ResidentInteraction> headless_resident_interaction;
    std::optional<std::pair<unsigned,unsigned>> headless_menu_query;
    std::vector<std::pair<unsigned,unsigned>> headless_menu_queries;
    // Retain the first request for existing launch-profile guards; later
    // requests keep CLI order, including requests at the same relative tick.
    std::vector<ResidentInteraction> headless_resident_interactions;
    std::vector<ResidentStand> headless_resident_stands;
    std::optional<unsigned> headless_cancel_interaction_tick;
    std::optional<ResidentGoHere> headless_resident_go_here;
    std::optional<SingleResidentGoHere> headless_single_resident_go_here;
    bool headless_stand = false;
    std::vector<std::uint8_t> restore_archive;
    std::optional<odh::SessionBundle> restore_bundle;
};

unsigned parse_number(const std::string& text, unsigned maximum, const char* label) {
    unsigned result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        result > maximum)
        throw odh::FormatError(std::string("invalid ") + label);
    return result;
}

#ifdef __linux__
// Resolve immutable origin authority from installed/player resources, never
// from a TRIP supplied by the archive being opened. This descriptor alone does
// not serialize a TRIP section or enable destination simulation.
odh::Ts1TravelState identify_travel_origin(
        const odh_host::IntegratedSessionContext& context,
        const std::filesystem::path& neighborhood) {
    odh::Ts1TravelState origin;
    origin.home_house=origin.current_house=context.house_number;
    origin.home_mode=origin.current_mode=context.neighborhood_mode;
    origin.family_chunk_id=context.family_chunk_id;
    origin.family_number=context.family_number;
    if(context.family_journal) {
        const auto lease=context.family_journal->lease();
        origin.occupancy_epoch=lease.occupancy_epoch;
        origin.neighborhood_identity=lease.neighborhood_identity;
        origin.family_identity=lease.family_identity;
    } else {
        const auto source=odh::load_neighborhood_family_source(neighborhood);
        const auto family=std::find_if(source.families.begin(),source.families.end(),
            [&](const auto& value){return value.chunk_id==context.family_chunk_id;});
        if(family==source.families.end() ||
            static_cast<std::uint32_t>(family->family.house_number)!=context.house_number ||
            family->family.family_number!=context.family_number)
            throw odh::FormatError("temporary travel origin is not the installed permanent household");
        std::set<std::uint32_t> resolved_guids;
        for(const auto& member:context.household_members)
            if(!resolved_guids.insert(member.guid).second)
                throw odh::FormatError("temporary travel origin contains duplicate resolved residents");
        const std::set<std::uint32_t> installed_guids(
            family->family.member_guids.begin(),family->family.member_guids.end());
        if(installed_guids.size()!=family->family.member_guids.size() ||
            resolved_guids!=installed_guids)
            throw odh::FormatError("temporary travel origin family roster changed during resource resolution");
        origin.neighborhood_identity=source.identity;
        origin.family_identity=odh::family_overlay_identity(*family);
    }
    for(const auto& member:context.household_members)
        origin.permanent_roster.push_back({member.guid,member.neighbor_id,
            member.object_definition_id,member.character_asset,
            odh_host::fingerprint_transfer_resource(member.character_path)});
    origin.travelers=origin.permanent_roster;
    origin.caller_guid=context.resident_guid;
    origin.caller_neighbor_id=context.resident_neighbor_id;
    odh::validate_ts1_travel_state(origin);
    return origin;
}
#endif

std::optional<std::int16_t> saved_resident_stand_target(const odh::SessionBundle& bundle,unsigned actor_id) {
    if(!bundle.generic_execution){
        const auto& seats=bundle.household_seated_continuations;
        const auto seat=std::find_if(seats.begin(),seats.end(),[&](const auto& value){return value.actor_id==actor_id;});
        return seat==seats.end()?std::nullopt:std::optional<std::int16_t>{seat->target_id};
    }
    const auto member=std::find_if(bundle.household_roster.begin(),bundle.household_roster.end(),
        [&](const auto& value){return value.id==actor_id;});
    const auto actor=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
        [&](const auto& value){return value.id==actor_id;});
    if(member==bundle.household_roster.end()||actor==bundle.world.entities.end()||
       actor->generation!=member->generation||actor->kind!=odh::EntityKind::avatar||
       actor->person_data.empty()||actor->person_data[0]!=1||!actor->attachment||!actor->attachment->positioned)return {};
    const auto target=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
        [&](const auto& value){return value.id==actor->attachment->container;});
    if(target==bundle.world.entities.end()||target->kind!=odh::EntityKind::object||!target->definition||
       !target->generation||actor->attachment->slot>=target->slots.size()||
       target->slots[actor->attachment->slot]!=actor_id||!target->entry_points||
       target->entry_points->size()<=27||target->entry_points->at(27).action<=255)return {};
    return target->id;
}

Options parse_options(int argc, char** argv) {
    Options result;
    bool have_root = false, have_mode = false, have_language = false, have_neighborhood = false;
    bool checkpoint_option_conflict=false;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        // Experimental checkpoint runs accept only the ordinary loaded-HOME
        // launch surface. Remember every parsed option, independent of order;
        // the ordinary CLI remains unchanged when checkpoints are disabled.
        if(!(key=="--game-root"||key=="--mode"||key=="--neighborhood"||
            key=="--language"||key=="--house"||key=="--load-session"||
            key=="--headless-ticks"||key=="--save-session-on-exit"||key=="--headless-checkpoint-interval"||
            key=="--no-audio"||key=="--headless-info-dialog-ok-limit"))checkpoint_option_conflict=true;
        if(key=="--original-companion-protocol"){
            if(result.original_companion_protocol)throw odh::FormatError("duplicate original companion profile option");
            result.original_companion_protocol=true;continue;
        }
        if(key=="--lot-window-size"){
            if(result.lot_window_size||i+2>=argc)
                throw odh::FormatError("--lot-window-size requires one width and height pair");
            const auto width=parse_number(argv[++i],1024,"lot window width");
            const auto height=parse_number(argv[++i],1024,"lot window height");
            if(width<320||height<200||std::uint64_t(width)*height>1048576)
                throw odh::FormatError("lot window size must be 320..1024 by 200..1024, at most 1048576 pixels");
            result.lot_window_size=std::array<unsigned,2>{width,height};continue;
        }
        if(key=="--buy-object"||key=="--move-object"||key=="--sell-object"||key=="--cancel-object-command"){
            if(result.object_command||result.architecture_command)throw odh::FormatError("only one property command may be issued per launch");
            odh_host::SessionObjectCommand command;
            if(key=="--cancel-object-command")command.kind=odh_host::SessionObjectCommand::Kind::cancel;
            else{
                const bool buy=key=="--buy-object",sell=key=="--sell-object";
                if(i+(sell?1:5)>=argc)throw odh::FormatError("object command requires GUID/ID and, for buy/move, X Y LEVEL DIRECTION");
                command.kind=buy?odh_host::SessionObjectCommand::Kind::purchase:
                    sell?odh_host::SessionObjectCommand::Kind::sell:odh_host::SessionObjectCommand::Kind::move;
                const auto identity=parse_number(argv[++i],buy?std::numeric_limits<unsigned>::max():32767,"object GUID/ID");
                if(!identity)throw odh::FormatError("object GUID/ID must be positive");
                if(buy)command.guid=identity;else command.object=static_cast<std::int16_t>(identity);
                if(!sell){
                    command.x=static_cast<std::int32_t>(parse_number(argv[++i],32767,"object X"));
                    command.y=static_cast<std::int32_t>(parse_number(argv[++i],32767,"object Y"));
                    command.level=static_cast<std::int16_t>(parse_number(argv[++i],5,"object level"));
                    if(!command.level)throw odh::FormatError("object level must be positive");
                    command.direction=static_cast<std::uint8_t>(parse_number(argv[++i],7,"object direction"));
                }
            }
            result.object_command=command;continue;
        }
        if(key=="--edit-floor"){
            if(result.object_command||result.architecture_command||i+4>=argc)
                throw odh::FormatError("--edit-floor requires one MATERIAL X Y LEVEL command per launch");
            odh::ArchitectureEditCommand command;command.type=odh::ArchitectureEditType::floor_rect;
            command.pattern=static_cast<std::uint16_t>(parse_number(argv[++i],65535,"floor material"));
            command.x=static_cast<std::int32_t>(parse_number(argv[++i],32767,"floor tile X"));
            command.y=static_cast<std::int32_t>(parse_number(argv[++i],32767,"floor tile Y"));
            command.level=static_cast<std::int8_t>(parse_number(argv[++i],2,"floor level"));
            if(!command.level)throw odh::FormatError("floor level must be 1 or 2");
            result.architecture_command=command;continue;
        }
        if (key == "--single-resident-session" && !result.single_resident_session) {
            result.single_resident_session = true;
            continue;
        }
        if (key == "--save-session-on-exit" && !result.save_on_exit) {
            result.save_on_exit = true;
            continue;
        }
        if(key=="--audio"&&!result.audio_enabled&&!result.audio_disabled){result.audio_enabled=true;continue;}
        if(key=="--no-audio"&&!result.audio_enabled&&!result.audio_disabled){result.audio_disabled=true;continue;}
        if(key=="--audio-drain-ms"&&i+1<argc&&!result.audio_drain_ms){
            result.audio_drain_ms=parse_number(argv[++i],5000,"audio drain milliseconds");
            if(!result.audio_drain_ms)throw odh::FormatError("audio drain must be positive");
            continue;
        }
        if (key == "--plan-only" && !result.plan_only) {
            result.plan_only = true;
            continue;
        }
        if(key=="--recover-saved-code"){
            if(result.recover_saved_code)throw odh::FormatError("duplicate complete class recovery option");
            result.recover_saved_code=true;continue;
        }
        if(key=="--headless-place-recovery-position"){
            if(result.place_recovery_position)throw odh::FormatError("duplicate recovery placement diagnostic");
            result.place_recovery_position=true;continue;
        }
        if(key=="--headless-check-recovery-position"){
            if(result.check_recovery_position)throw odh::FormatError("duplicate recovery position diagnostic");
            result.check_recovery_position=true;continue;
        }
        if(key=="--headless-dialog-ok"){
            if(result.headless_dialog_ok)throw odh::FormatError("duplicate --headless-dialog-ok");
            result.headless_dialog_ok=true;
            continue;
        }
        if(key=="--headless-info-dialog-ok-limit"){
            if(result.headless_info_dialog_limit||i+1>=argc)throw odh::FormatError("duplicate or missing informational dialog test limit");
            result.headless_info_dialog_limit=parse_number(argv[++i],32,"informational dialog test limit");
            if(!result.headless_info_dialog_limit)throw odh::FormatError("informational dialog test limit must be positive");
            continue;
        }
        if(key=="--headless-dialog-response"){
            if(i+2>=argc)throw odh::FormatError("--headless-dialog-response requires CODE TEXT");
            const auto code=parse_number(argv[++i],2,"dialog response code");
            std::string text=argv[++i];
            if(text.size()>4096||result.headless_dialog_responses.size()>=32)
                throw odh::FormatError("dialog response sequence exceeds its text or count budget");
            result.headless_dialog_responses.push_back({code,std::move(text)});
            continue;
        }
        if(key=="--headless-travel-tick"&&!result.headless_travel_tick&&i+1<argc){
            result.headless_travel_tick=parse_number(argv[++i],odh_host::PersonSessionPolicy::max_headless_ticks-1,"lot switch tick");
            if(!*result.headless_travel_tick)throw odh::FormatError("lot switch tick must be positive");
            continue;
        }
        if (key == "--headless-stand" && !result.headless_stand) {
            result.headless_stand = true;
            continue;
        }
        if (key == "--headless-resident-stand") {
            if (i + 2 >= argc) throw odh::FormatError("missing resident Stand values");
            if (result.headless_resident_stands.size() >= 8)
                throw odh::FormatError("resident Stand sequence exceeds eight requests");
            const auto actor_id=parse_number(argv[++i],32767,"resident Stand actor ID");
            const auto tick=parse_number(argv[++i], odh_host::PersonSessionPolicy::max_headless_ticks, "resident Stand tick");
            if (!actor_id || !tick)
                throw odh::FormatError("resident Stand actor and tick must be nonzero");
            if (!result.headless_resident_stands.empty() && tick < result.headless_resident_stands.back().tick)
                throw odh::FormatError("resident Stand sequence ticks must be nondecreasing");
            if (std::any_of(result.headless_resident_stands.begin(),result.headless_resident_stands.end(),
                    [&](const auto& request) { return request.actor_id == actor_id; }))
                throw odh::FormatError("resident Stand sequence repeats an actor");
            result.headless_resident_stands.push_back({actor_id,tick});
            continue;
        }
        if (key == "--headless-resident-go-here" && !result.headless_resident_go_here &&
            !result.headless_single_resident_go_here) {
            if (i + 5 >= argc) throw odh::FormatError("missing resident Go Here values");
            const auto entity_id=parse_number(argv[++i],32767,"resident Go Here entity ID");
            const auto x=parse_number(argv[++i],2046,"resident Go Here tile X");
            const auto y=parse_number(argv[++i],2046,"resident Go Here tile Y");
            const auto level=parse_number(argv[++i],2,"resident Go Here level");
            const auto tick=parse_number(argv[++i], odh_host::PersonSessionPolicy::max_headless_ticks, "resident Go Here tick");
            if(!entity_id||level==0||tick==0)
                throw odh::FormatError("resident Go Here entity, level, or tick is outside the supported profile");
            if(result.single_resident_session)
                result.headless_single_resident_go_here=Options::SingleResidentGoHere{entity_id,x,y,level,tick};
            else
                result.headless_resident_go_here=Options::ResidentGoHere{entity_id,x,y,level,tick};
            continue;
        }
        if (key == "--headless-single-resident-go-here" && !result.headless_single_resident_go_here) {
            if (i + 4 >= argc) throw odh::FormatError("missing single-resident Go Here values");
            const auto x=parse_number(argv[++i],2046,"Go Here tile X");
            const auto y=parse_number(argv[++i],2046,"Go Here tile Y");
            const auto level=parse_number(argv[++i],2,"Go Here level");
            const auto tick=parse_number(argv[++i], odh_host::PersonSessionPolicy::max_headless_ticks, "Go Here tick");
            if(!level||!tick)throw odh::FormatError("single-resident Go Here level or tick is outside the supported profile");
            result.headless_single_resident_go_here=Options::SingleResidentGoHere{0,x,y,level,tick};
            continue;
        }
        if (key == "--headless-resident-sit" && !result.headless_resident_sit) {
            if (i + 3 >= argc) throw odh::FormatError("missing resident Sit values");
            const auto actor_id=parse_number(argv[++i],32767,"resident Sit actor ID");
            const auto object_id=parse_number(argv[++i],32767,"resident Sit object ID");
            const auto stand_tick=parse_number(argv[++i], odh_host::PersonSessionPolicy::max_headless_ticks, "resident Stand tick");
            if(!actor_id||!object_id||stand_tick<2)
                throw odh::FormatError("resident Sit actor, object, or Stand tick is outside the supported profile");
            result.headless_resident_sit=Options::ResidentSit{actor_id,object_id,stand_tick};
            continue;
        }
        if (key == "--headless-move-entity") {
            if(result.headless_entity_move||i+6>=argc)
                throw odh::FormatError("duplicate or incomplete headless entity move");
            const auto id=parse_number(argv[++i],32767,"entity move ID");
            const auto x=parse_number(argv[++i],32767,"entity move X");
            const auto y=parse_number(argv[++i],32767,"entity move Y");
            const auto level=parse_number(argv[++i],2,"entity move level");
            const auto direction=parse_number(argv[++i],7,"entity move direction");
            const auto tick=parse_number(argv[++i], odh_host::PersonSessionPolicy::max_headless_ticks, "entity move tick");
            if(!id||!level||!tick)throw odh::FormatError("entity move ID, level and tick must be nonzero");
            result.headless_entity_move=Options::EntityMove{id,x,y,level,direction,tick};
            continue;
        }
        if(key=="--headless-menu-query"){
            if(i+2>=argc||result.headless_menu_queries.size()>=32)
                throw odh::FormatError("menu query needs actor/target pairs and permits at most thirty-two requests");
            const auto actor=parse_number(argv[++i],32767,"menu query actor");
            const auto target=parse_number(argv[++i],32767,"menu query target");
            if(!actor||!target)throw odh::FormatError("menu query identities must be nonzero");
            if(!result.headless_menu_query)result.headless_menu_query=std::make_pair(actor,target);
            result.headless_menu_queries.emplace_back(actor,target);continue;
        }
        if (key == "--headless-resident-interaction" || key == "--headless-resident-interaction-row" || key=="--headless-resident-interaction-variant" || key=="--headless-resident-interaction-variant-hex") {
            const bool variant_hex=key=="--headless-resident-interaction-variant-hex";
            const bool explicit_variant=key=="--headless-resident-interaction-variant"||variant_hex;
            const bool explicit_row=key!="--headless-resident-interaction";
            if (i + (explicit_variant?7:explicit_row?4:3) >= argc) throw odh::FormatError("missing resident interaction values");
            if(result.headless_resident_interaction&&result.headless_resident_interactions.size()>=31)
                throw odh::FormatError("resident interaction sequence exceeds thirty-two requests");
            const auto actor_id=parse_number(argv[++i],32767,"resident interaction actor ID");
            const auto object_id=parse_number(argv[++i],32767,"resident interaction object ID");
            std::optional<unsigned> row;
            if(explicit_row)row=parse_number(argv[++i],65535,"resident interaction row");
            std::optional<std::int16_t> variant_param;unsigned variant_language=0;std::string variant_name;
            if(explicit_variant){
                const std::string raw=argv[++i];int param=0;const auto parsed=std::from_chars(raw.data(),raw.data()+raw.size(),param);
                if(raw.empty()||parsed.ec!=std::errc{}||parsed.ptr!=raw.data()+raw.size()||param<INT16_MIN||param>INT16_MAX)
                    throw odh::FormatError("invalid menu variant Param0");
                variant_param=static_cast<std::int16_t>(param);variant_language=parse_number(argv[++i],20,"menu variant language");
                variant_name=argv[++i];
                if(variant_hex&&(variant_name.empty()||variant_name.size()>8192||(variant_name.size()%2)||
                    std::any_of(variant_name.begin(),variant_name.end(),[](char c){return !((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'));})))
                    throw odh::FormatError("invalid menu variant name hex");
                if(!variant_language||variant_name.empty()||variant_name.size()>(variant_hex?8192U:4096U))throw odh::FormatError("invalid observed menu variant language/name");
            }
            const auto tick=parse_number(argv[++i], odh_host::PersonSessionPolicy::max_headless_ticks, "resident interaction tick");
            if(!actor_id||!object_id||!tick)
                throw odh::FormatError("resident interaction actor, object, or tick is outside the supported profile");
            const Options::ResidentInteraction request{actor_id,object_id,tick,row,variant_param,variant_language,std::move(variant_name),variant_hex};
            if(!result.headless_resident_interaction)result.headless_resident_interaction=request;
            else{
                const auto previous_tick=result.headless_resident_interactions.empty()?
                    result.headless_resident_interaction->tick:result.headless_resident_interactions.back().tick;
                if(tick<previous_tick)
                    throw odh::FormatError("resident interaction sequence ticks must be nondecreasing");
                result.headless_resident_interactions.push_back(request);
            }
            continue;
        }
        if (key == "--headless-cancel-interaction" && !result.headless_cancel_interaction_tick) {
            if (i + 1 >= argc) throw odh::FormatError("missing interaction cancel tick");
            const auto tick=parse_number(argv[++i], odh_host::PersonSessionPolicy::max_headless_ticks, "interaction cancel tick");
            if(!tick)throw odh::FormatError("interaction cancel tick must be nonzero");
            result.headless_cancel_interaction_tick=tick;
            continue;
        }
        if (key == "--headless-travel-house") {
            if (i + 1 >= argc) throw odh::FormatError("missing headless travel destination");
            if (result.headless_travel_houses.size() >= 16)
                throw odh::FormatError("headless travel route exceeds sixteen destinations");
            const auto house = parse_number(argv[++i], 98, "headless travel house");
            if (!house) throw odh::FormatError("headless travel house is outside the supported lot range");
            result.headless_travel_houses.push_back(house);
            continue;
        }
        if (i + 1 >= argc) throw odh::FormatError("missing integrated game option value");
        const std::string value = argv[++i];
        if (key == "--game-root" && !have_root) {
            result.game_root = value;
            have_root = true;
        } else if (key == "--mode" && !have_mode) {
            result.mode = parse_number(value, 7, "neighborhood mode");
            have_mode = true;
        } else if (key == "--neighborhood" && !have_neighborhood) {
            result.neighborhood=parse_number(value,8,"neighborhood number");
            if(!result.neighborhood)throw odh::FormatError("neighborhood number is outside 1..8");
            have_neighborhood=true;
        } else if (key == "--language" && !have_language) {
            result.language = parse_number(value, 20, "language code");
            have_language = true;
        } else if (key == "--house" && !result.house) {
            result.house = parse_number(value, 98, "house number");
        } else if (key == "--load-session" && !result.load_session) {
            result.load_session = value;
        } else if (key == "--headless-ticks" && !result.headless_ticks) {
            result.headless_ticks = parse_number(value, odh_host::PersonSessionPolicy::max_headless_ticks, "headless tick count");
        } else if (key == "--headless-checkpoint-interval" && !result.headless_checkpoint_interval) {
            result.headless_checkpoint_interval=parse_number(value,odh_host::PersonSessionPolicy::max_headless_ticks,"headless checkpoint interval");
            if(!*result.headless_checkpoint_interval)throw odh::FormatError("headless checkpoint interval must be positive");
        } else if (key == "--headless-stop-route-depth" && !result.stop_route_depth) {
            result.stop_route_depth=parse_number(value,16,"routing checkpoint depth");
        } else if (key == "--headless-move-out-family" && !result.headless_move_out_family) {
            result.headless_move_out_family = parse_number(value, 65535, "family chunk ID");
        } else if (key == "--headless-move-in-family" && !result.headless_move_in_family) {
            result.headless_move_in_family = parse_number(value, 65535, "family chunk ID");
        } else if (key == "--headless-status-frame" && !result.status_frame) {
            result.status_frame = value;
        } else if (key == "--headless-status-tick" && !result.status_tick) {
            result.status_tick = parse_number(value, odh_host::PersonSessionPolicy::max_headless_ticks, "status capture tick");
        } else if (key == "--instruction-budget" && !result.instruction_budget) {
            result.instruction_budget = parse_number(value, 1000000, "instruction budget");
        } else if (key == "--resident-id" && !result.resident_id) {
            result.resident_id = parse_number(value, 32767, "resident ID");
        } else if (key == "--headless-sit-object" && !result.headless_sit_object) {
            result.headless_sit_object = parse_number(value, 32767, "Sit object ID");
        } else if (key == "--headless-stand-tick" && !result.headless_stand_tick) {
            result.headless_stand_tick = parse_number(value, odh_host::PersonSessionPolicy::max_headless_ticks, "Stand tick");
        } else {
            throw odh::FormatError("unknown or duplicate integrated game option");
        }
    }
    if(result.headless_checkpoint_interval&&checkpoint_option_conflict)
        throw odh::FormatError("--headless-checkpoint-interval requires only ordinary managed HOME options; probes, inputs, visual options and raw arguments are excluded");
    if(result.headless_checkpoint_interval&&(!result.load_session||!result.headless_ticks||!*result.headless_ticks||
       !result.save_on_exit||result.plan_only||result.single_resident_session||result.instruction_budget||
       result.status_frame||result.lot_window_size||result.stop_route_depth||result.recover_saved_code||
       result.check_recovery_position||result.place_recovery_position||result.object_command||result.architecture_command||
       !result.headless_travel_houses.empty()||result.headless_move_in_family||result.headless_move_out_family))
        throw odh::FormatError("--headless-checkpoint-interval requires a loaded managed headless session, positive ticks and save-on-exit, without window, probes or commands");
    if(result.headless_checkpoint_interval&&1+(*result.headless_ticks-1)/ *result.headless_checkpoint_interval>32)
        throw odh::FormatError("headless checkpoint schedule exceeds 32 saves including final exit");
    if(result.lot_window_size&&result.headless_ticks)
        throw odh::FormatError("--lot-window-size requires a windowed lot");
    if (result.load_session) {
#ifdef __linux__
        result.restore_archive = odh::encode_session_archive(odh_host::read_session_archive(*result.load_session));
        result.restore_bundle = odh::decode_session_bundle(result.restore_archive);
        const auto& bundle = *result.restore_bundle;
        if ((have_mode && result.mode != bundle.host.neighborhood_mode) ||
            (have_language && result.language != bundle.resources.language_code) ||
            (result.house && *result.house != bundle.host.house_number) ||
            (result.resident_id && *result.resident_id != static_cast<unsigned>(bundle.host.resident_neighbor_id)))
            throw odh::FormatError("explicit launch options conflict with the saved session");
        if (bundle.household_roster.empty() && !bundle.npc_primary && !bundle.unanchored_family_lot && !result.single_resident_session)
            throw odh::FormatError("legacy single-resident session requires explicit --single-resident-session");
        if ((!bundle.household_roster.empty()||bundle.npc_primary||bundle.unanchored_family_lot) && result.single_resident_session)
            throw odh::FormatError("household session cannot use --single-resident-session");
        if (bundle.runtime_entities.size() != ((bundle.npc_primary||bundle.unanchored_family_lot)?bundle.household_roster.size():bundle.household_roster.empty()?1u:bundle.household_roster.size()))
            throw odh::FormatError("session VM roster does not match its household profile");
        result.mode = bundle.host.neighborhood_mode;
        result.language = bundle.resources.language_code;
        result.house = bundle.host.house_number;
        if(bundle.unanchored_family_lot)result.resident_id.reset();
        else result.resident_id = static_cast<unsigned>(bundle.host.resident_neighbor_id);
        have_mode = have_language = true;
#else
        throw odh::FormatError("session loading is currently available only on Linux");
#endif
    }
    if ((result.headless_move_in_family || result.headless_move_out_family) &&
        (!result.headless_ticks || !result.house || result.load_session || result.plan_only ||
         result.single_resident_session || (result.headless_move_in_family && result.headless_move_out_family) ||
         (result.headless_move_out_family && (*result.headless_ticks || result.save_on_exit ||
           !result.headless_travel_houses.empty()))))
        throw odh::FormatError("family move diagnostic requires a complete household, explicit house and headless ticks; move-out requires zero ticks and no save/travel");
    if ((result.status_frame || result.status_tick) &&
        (!result.status_frame || result.status_frame->empty() || !result.status_tick || !*result.status_tick ||
         !result.headless_ticks || *result.status_tick > *result.headless_ticks || result.plan_only ||
         !result.headless_travel_houses.empty()))
        throw odh::FormatError("status capture requires --headless-status-frame PATH and --headless-status-tick 1..headless-ticks, without plan/travel");
    if((result.object_command||result.architecture_command)&&(result.plan_only||result.single_resident_session||(!result.house&&!result.load_session)))
        throw odh::FormatError("property commands require an executable whole-household session and a selected house");
    if (result.load_session && result.plan_only)
        throw odh::FormatError("--load-session requires execution; use --headless-ticks for a display-free restore");
    if (result.headless_sit_object && (!*result.headless_sit_object || !result.headless_ticks ||
        result.plan_only || result.load_session || result.headless_stand || result.headless_resident_sit))
        throw odh::FormatError("--headless-sit-object requires a new bounded lot session");
    if (result.headless_stand_tick && (!result.headless_sit_object || !result.single_resident_session ||
        !result.headless_ticks || result.plan_only || result.load_session || result.headless_stand ||
        *result.headless_stand_tick < 2 || *result.headless_stand_tick > *result.headless_ticks))
        throw odh::FormatError("--headless-stand-tick requires a new single-resident Sit run and an in-range tick");
    if (result.headless_resident_sit && (!result.headless_ticks || result.plan_only || result.load_session ||
        result.single_resident_session || result.headless_stand || result.headless_sit_object ||
        result.headless_resident_interaction ||
        result.headless_resident_sit->stand_tick > *result.headless_ticks))
        throw odh::FormatError("--headless-resident-sit requires a new whole-household bounded lot session and an in-range Stand tick");
    if(result.headless_menu_query&&(!result.load_session||!result.headless_ticks||*result.headless_ticks!=0||result.plan_only||
        result.single_resident_session||result.headless_resident_interaction||result.headless_dialog_ok||result.headless_info_dialog_limit||!result.headless_dialog_responses.empty()||
        result.object_command||result.architecture_command||result.recover_saved_code||result.place_recovery_position||
        result.resume_buy_mode||result.resume_build_mode||result.headless_move_out_family||result.headless_move_in_family))
        throw odh::FormatError("menu query requires an otherwise read-only zero-tick resumed household session");
    if (result.headless_resident_interaction && (!result.headless_ticks || result.plan_only ||
        result.single_resident_session || result.headless_stand || result.headless_sit_object || result.headless_resident_sit))
        throw odh::FormatError("--headless-resident-interaction requires a bounded whole-household lot session");
    if (result.headless_resident_interaction && result.headless_resident_interaction->tick > *result.headless_ticks)
        throw odh::FormatError("resident interaction tick must fit within the bounded run");
    for(const auto& request:result.headless_resident_interactions)
        if(request.tick>*result.headless_ticks)
            throw odh::FormatError("resident interaction sequence ticks must fit within the bounded run");
    if (result.headless_resident_interaction && result.load_session &&
        (!result.restore_bundle || (result.restore_bundle->household_roster.empty()&&!result.restore_bundle->npc_primary)))
        throw odh::FormatError("resumed resident interaction requires a saved whole-household session");
    if(result.headless_cancel_interaction_tick){
        const auto interaction_tick=result.headless_resident_interaction?
            result.headless_resident_interaction->tick:
            result.headless_sit_object?1U:0U;
        if(!interaction_tick||!result.headless_ticks||result.plan_only||
           *result.headless_cancel_interaction_tick<interaction_tick||
           *result.headless_cancel_interaction_tick>*result.headless_ticks||
           result.headless_resident_sit)
            throw odh::FormatError("--headless-cancel-interaction requires a scheduled non-Sit action and an in-range tick not before it");
    }
    if(result.audio_drain_ms&&(result.audio_disabled||(!result.audio_enabled&&result.headless_ticks)))
        throw odh::FormatError("--audio-drain-ms requires enabled audio");
    if (result.headless_stand && (!result.load_session || !result.headless_ticks || result.plan_only ||
        result.headless_sit_object || !result.restore_bundle || !result.restore_bundle->seated_continuation))
        throw odh::FormatError("--headless-stand requires a bounded --load-session with verified seated continuation data");
    if (!result.headless_resident_stands.empty()) {
        if (!result.load_session || !result.headless_ticks || result.plan_only || result.single_resident_session ||
            !result.restore_bundle || result.restore_bundle->household_roster.empty() ||
            result.headless_stand || result.headless_sit_object || result.headless_stand_tick ||
            result.headless_resident_sit || result.headless_resident_interaction ||
            result.headless_cancel_interaction_tick || result.headless_resident_go_here ||
            result.headless_single_resident_go_here || !result.headless_travel_houses.empty())
            throw odh::FormatError("--headless-resident-stand requires a bounded saved whole-household session without other action schedules");
        for (const auto& request : result.headless_resident_stands) {
            if (request.tick > *result.headless_ticks)
                throw odh::FormatError("resident Stand sequence ticks must fit within the bounded run");
            if (!saved_resident_stand_target(*result.restore_bundle,request.actor_id))
                throw odh::FormatError("resident Stand actor has no verified saved household seat");
        }
    }
    if(result.headless_resident_go_here&&(!result.headless_ticks||result.plan_only||
       result.single_resident_session||result.headless_resident_go_here->tick>*result.headless_ticks))
        throw odh::FormatError("--headless-resident-go-here requires an in-range tick in a whole-household headless run");
    if(result.headless_resident_go_here) {
        const auto actor_id=result.headless_resident_go_here->entity_id;
        if (result.headless_resident_interaction &&
            (result.headless_resident_interaction->actor_id==actor_id ||
             std::any_of(result.headless_resident_interactions.begin(),result.headless_resident_interactions.end(),
                 [&](const auto& request){return request.actor_id==actor_id;})))
            throw odh::FormatError("resident Go Here and selected-object input require different actors");
        if(result.load_session) {
            if(!result.restore_bundle || (result.restore_bundle->household_roster.empty()&&!result.restore_bundle->npc_primary))
                throw odh::FormatError("resumed resident Go Here requires a saved whole-household session");
            const auto& bundle=*result.restore_bundle;
            const auto member=std::find_if(bundle.household_roster.begin(),bundle.household_roster.end(),
                [&](const auto& item){return item.id==actor_id;});
            std::optional<odh::WorldEntityReference> reference;
            if(member!=bundle.household_roster.end())reference=odh::WorldEntityReference{member->id,member->generation};
            else if(bundle.character_family){
                const auto* family=odh::find_ts1_family_roster(bundle.character_family->shared,bundle.host.family_chunk_id);
                for(const auto& npc:bundle.npc_avatars)if(npc.origin.id==actor_id&&family&&
                    std::find(family->members.begin(),family->members.end(),npc.origin.guid)!=family->members.end())
                    reference=odh::WorldEntityReference{npc.origin.id,npc.origin.generation};
            }
            if(!reference||std::none_of(bundle.world.entities.begin(),bundle.world.entities.end(),[&](const auto& entity){
                   return entity.id==reference->id && entity.generation==reference->generation &&
                       entity.kind==odh::EntityKind::avatar && entity.position.has_value() && !entity.ui_ghost;
               }))
                throw odh::FormatError("resumed resident Go Here actor is not a positioned live saved household member");
        }
    }
    if(result.headless_single_resident_go_here&&(!result.headless_ticks||result.plan_only||result.load_session||
       !result.single_resident_session||!result.house||!result.save_on_exit||
       result.headless_single_resident_go_here->tick>*result.headless_ticks||
       result.headless_resident_sit||result.headless_resident_interaction||
       result.headless_resident_go_here||result.headless_stand||
       ((result.headless_sit_object||result.headless_stand_tick)&&
        (!result.headless_sit_object||!result.headless_stand_tick||
         result.headless_single_resident_go_here->tick<=*result.headless_stand_tick))))
        throw odh::FormatError("single-resident Go Here requires a fresh saved single-resident headless run with an in-range request tick");
    if(result.headless_travel_tick&&(result.headless_travel_houses.empty()||!result.headless_ticks||
       *result.headless_travel_tick>=*result.headless_ticks))
        throw odh::FormatError("--headless-travel-tick requires lot switching and a tick below the per-visit limit");
    if (!result.headless_travel_houses.empty() &&
        (!result.headless_ticks || *result.headless_ticks < 2 || result.plan_only || result.load_session ||
         result.single_resident_session || !result.house))
        throw odh::FormatError("--headless-travel-house requires a new whole-household run with --headless-ticks at least 2");
    if(result.headless_dialog_ok && (!result.headless_ticks || !*result.headless_ticks || result.plan_only))
        throw odh::FormatError("--headless-dialog-ok requires execution with --headless-ticks 1..65535, without --plan-only");
    if(!result.headless_dialog_responses.empty()&&
       (result.headless_dialog_ok||!result.headless_ticks||!*result.headless_ticks||result.plan_only))
        throw odh::FormatError("--headless-dialog-response requires bounded execution and cannot mix with --headless-dialog-ok");
    if(result.headless_info_dialog_limit&&(!result.headless_ticks||!*result.headless_ticks||result.plan_only||
       result.headless_dialog_ok||!result.headless_dialog_responses.empty()))
        throw odh::FormatError("informational dialog test input requires bounded execution without other dialog inputs");
    if(result.headless_entity_move&&(!result.headless_ticks||
       result.headless_entity_move->tick>*result.headless_ticks))
        throw odh::FormatError("--headless-move-entity requires a bounded headless run and an in-range tick");
    if (result.instruction_budget && (!*result.instruction_budget || !result.headless_ticks ||
        result.plan_only || result.single_resident_session))
        throw odh::FormatError("--instruction-budget requires a bounded whole-household run with --headless-ticks");
    if((result.check_recovery_position||result.place_recovery_position)&&(!result.headless_ticks||*result.headless_ticks!=0||
       result.load_session||result.save_on_exit||result.plan_only||result.single_resident_session||result.object_command||result.architecture_command))
        throw odh::FormatError("recovery position diagnostic requires fresh whole-household headless0 without save/restore or object command");
    if(result.stop_route_depth&&(!*result.stop_route_depth||!result.headless_ticks||!*result.headless_ticks||result.plan_only))
        throw odh::FormatError("routing checkpoint depth requires 1..16 and positive headless ticks");
    if ((result.save_on_exit && (!result.headless_ticks || result.plan_only)) ||
        (result.headless_ticks && (result.plan_only || !result.house)))
        throw odh::FormatError("headless execution needs --house (or --load-session); saving on exit needs --headless-ticks, without --plan-only");
    if (!have_mode || result.mode == 0 || result.mode == 6 || result.language == 0 ||
        (result.house && *result.house == 0) ||
        (result.resident_id && *result.resident_id == 0) ||
        (result.plan_only && !result.house))
        throw odh::FormatError(
            "Usage: odh-person [--game-root DIR] --mode 1..5,7 [--language 1..20] "
            "[--house 1..98] [--resident-id 1..32767] [--single-resident-session] "
            "[--edit-floor MATERIAL X Y LEVEL (one tile; exclusive with object commands)] "
            "[--headless-checkpoint-interval N (loaded managed HOME, positive headless ticks and save-on-exit; at most32 saves including exit)] "
            "[--headless-sit-object ID --headless-stand-tick N] "
            "[--headless-single-resident-go-here X Y LEVEL TICK] "
            "[--headless-resident-go-here ACTOR X Y LEVEL RELATIVE_TICK] "
            "[--headless-resident-interaction ACTOR TARGET TICK (repeat up to 32, nondecreasing ticks)] "
            "[--original-companion-protocol (experimental fresh original import only; persisted, no legacy upgrade)] "
            "[--headless-menu-query ACTOR TARGET (repeat up to 32; zero ticks, actual selected actor)] [--headless-resident-interaction-variant ACTOR TARGET ROW PARAM0 LANGUAGE NAME TICK (exact observed dynamic choice)] [--headless-resident-interaction-variant-hex ACTOR TARGET ROW PARAM0 LANGUAGE NAME_HEX TICK (lossless observed source bytes)] [--headless-resident-interaction-row ACTOR TARGET ROW TICK (authored zero-based TTAB row)] "
            "[--headless-resident-stand ACTOR TICK (saved household seats only; repeat up to 8, nondecreasing ticks 1..N)] "
            "[--headless-stand (legacy saved single-resident seat)] "
            "[--audio | --no-audio] [--audio-drain-ms 1..5000] "
            "[--headless-travel-house HOUSE (repeat)] [--headless-travel-tick 1..N-1] "
            "[--neighborhood 1..8] [--load-session PATH] [--plan-only | --headless-ticks 0..65535 [--headless-dialog-ok | --headless-dialog-response CODE TEXT (repeat up to32; positive ticks) | --headless-info-dialog-ok-limit 1..32] [--instruction-budget 1..1000000] [--headless-status-frame PATH --headless-status-tick 1..N] [--save-session-on-exit]]");
    result.choose_language = !have_language;
#ifdef ODH_HAVE_LINUX_USER_CONFIG
    odh_host::LinuxUserConfig saved;
    std::string config_error;
    if (!odh_host::load_linux_user_config(saved, &config_error)) {
        if (!have_root)
            throw odh::FormatError("cannot read user settings; pass --game-root DIR to replace them: " + config_error);
        saved = odh_host::LinuxUserConfig{};
    }
    if (!have_root && !saved.data_root.empty()) {
        result.game_root = saved.data_root;
        have_root = true;
    }
    result.preferences=saved.preferences;
    if(!have_neighborhood)result.neighborhood=saved.neighborhood_number;
    if (!have_language) {
        result.language = static_cast<unsigned>(saved.language_code);
        result.choose_language = !saved.has_saved_settings;
    }
#endif
    if (!have_root)
        throw odh::FormatError("game data path is not set; pass --game-root DIR on first launch");
    return result;
}

// Travel uses the same archive/profile checks as an explicit CLI restore, but
// keeps the selected installation, neighborhood and language fixed.
void load_travel_session(Options& options) {
#ifdef __linux__
    options.restore_archive = odh::encode_session_archive(
        odh_host::read_session_archive(*options.load_session));
    options.restore_bundle = odh::decode_session_bundle(options.restore_archive);
    const auto& bundle = *options.restore_bundle;
    if (bundle.host.neighborhood_mode != options.mode ||
        bundle.resources.language_code != options.language ||
        !options.house || bundle.host.house_number != *options.house)
        throw odh::FormatError("travel session does not match the selected neighborhood, language or lot");
    if ((bundle.household_roster.empty()&&!bundle.npc_primary&&!bundle.unanchored_family_lot) != options.single_resident_session ||
        bundle.runtime_entities.size() != ((bundle.npc_primary||bundle.unanchored_family_lot)?bundle.household_roster.size():bundle.household_roster.empty()?1u:bundle.household_roster.size()))
        throw odh::FormatError("travel session does not match the household runtime profile");
    if(bundle.unanchored_family_lot)options.resident_id.reset();
    else options.resident_id = static_cast<unsigned>(bundle.host.resident_neighbor_id);
#else
    (void)options;
    throw odh::FormatError("session loading is currently available only on Linux");
#endif
}

bool path_is_within(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& component : relative)
        if (component == "..") return false;
    return true;
}

std::filesystem::path require_directory(const std::filesystem::path& root,
                                        const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_directory(path, error) || error)
        throw odh::FormatError("required game directory is missing: " + path.string());
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !path_is_within(root, canonical))
        throw odh::FormatError("game directory resolves outside the selected installation: " + path.string());
    return canonical;
}

#ifdef __linux__
std::vector<std::uint8_t> read_travel_checkpoint_bytes(const std::filesystem::path& path) {
    return odh::encode_session_archive(odh_host::read_session_archive(path));
}
std::string digest_hex(const std::array<std::uint8_t,32>& digest);
std::filesystem::path require_file(const std::filesystem::path&,const std::filesystem::path&);
std::filesystem::path private_neighborhood_save_directory(const std::filesystem::path& game,
        const std::array<std::uint8_t,32>& neighborhood,odh_host::NeighborhoodStorage storage) {
    const char* data=std::getenv("XDG_DATA_HOME");std::filesystem::path directory;
    if(data&&*data)directory=data;
    else {const char* home=std::getenv("HOME");if(!home||!*home)throw odh::FormatError("user data location is required for temporary travel");directory=std::filesystem::path(home)/".local"/"share";}
    directory/="opendollhouse";directory/="saves";
    if(!directory.is_absolute())throw odh::FormatError("temporary travel save directory must be absolute");
    std::error_code error;directory=std::filesystem::weakly_canonical(directory,error);
    if(error||path_is_within(game,directory)||directory==game)throw odh::FormatError("temporary travel checkpoint must remain outside the installation");
    auto checkout=std::filesystem::current_path(error);
    if(!error)for(;;){if(std::filesystem::exists(checkout/".git")){if(path_is_within(checkout,directory)||checkout==directory)throw odh::FormatError("temporary travel checkpoint must remain outside the source checkout");break;}const auto parent=checkout.parent_path();if(parent==checkout||parent.empty())break;checkout=parent;}
    directory=storage.qualify(directory);
    const auto scope=directory/("neighborhood-"+digest_hex(neighborhood));
    const auto status=std::filesystem::symlink_status(scope);
    if(std::filesystem::exists(status)&&!std::filesystem::is_directory(status))
        throw odh::FormatError("private home save namespace must be a real directory");
    return scope;
}
std::filesystem::path permanent_travel_checkpoint_path(const std::filesystem::path& game,
        const std::array<std::uint8_t,32>& neighborhood,unsigned mode,unsigned house,std::int32_t family_number,
        odh_host::NeighborhoodStorage storage) {
    return private_neighborhood_save_directory(game,neighborhood,storage)/
        ("mode-"+std::to_string(mode)+"-house-"+std::to_string(house)+"-family-"+std::to_string(family_number)+".odhs");
}
odh_host::CommunityLotStoreOptions community_lot_store_options(const std::filesystem::path& game,
        const std::filesystem::path& userdata,odh_host::NeighborhoodStorage storage,unsigned mode,unsigned house) {
    odh_host::CommunityLotStoreOptions result;
    result.game_root=game;result.user_data=userdata;result.neighborhood_number=storage.number();
    result.mode=static_cast<std::uint8_t>(mode);result.house=static_cast<std::uint16_t>(house);
    const auto filename="House"+std::string(house<10?"0":"")+std::to_string(house)+".iff";
    result.lot_path=require_file(game,userdata/"Houses"/filename);
    result.private_directory=private_neighborhood_save_directory(game,
        odh_host::linux_neighborhood_identity(userdata/"Neighborhood.iff"),storage)/"community-lots"/("mode-"+std::to_string(mode)+"-house-"+std::to_string(house));
    return result;
}
bool same_travel_origin(const odh::Ts1TravelState& a,const odh::Ts1TravelState& b) {
    if(a.protocol!=b.protocol||a.home_house!=b.home_house||a.home_mode!=b.home_mode||a.family_chunk_id!=b.family_chunk_id||a.family_number!=b.family_number||a.occupancy_epoch!=b.occupancy_epoch||a.family_identity!=b.family_identity||a.neighborhood_identity!=b.neighborhood_identity||a.permanent_roster.size()!=b.permanent_roster.size())return false;
    for(std::size_t i=0;i<a.permanent_roster.size();++i)if(!odh::same_ts1_travel_member(a.permanent_roster[i],b.permanent_roster[i]))return false;
    return true;
}

#endif

std::filesystem::path require_file(const std::filesystem::path& root,
                                   const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error || !std::filesystem::is_regular_file(status))
        throw odh::FormatError("required game file is missing or is not a regular file: " + path.string());
    auto canonical = std::filesystem::canonical(path, error);
    if (error) throw odh::FormatError("cannot resolve required game file: " + path.string());
    if (!path_is_within(root, canonical))
        throw odh::FormatError("game file resolves outside the selected installation: " + path.string());
    return canonical;
}

#ifdef ODH_HAVE_X11
#endif

std::int32_t house_purchase_price(const std::filesystem::path& game,
        const std::filesystem::path& user_data, std::uint16_t house_number) {
    std::ostringstream filename;
    filename << "House" << (house_number < 10 ? "0" : "") << house_number << ".iff";
    const auto house_file = require_file(game, user_data / "Houses" / filename.str());
    std::optional<odh::Simi> simi;
    with_iff_file(house_file.string(), [&](const auto& view) {
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type != "SIMI" || chunk.id != 1) return;
            if (simi) throw odh::FormatError("ambiguous SIMI1 selected house");
            simi = odh::read_simi(chunk.payload);
        });
    });
    if (!simi) throw odh::FormatError("selected house has no valid SIMI1 purchase data");
    const auto price = simi->purchase_value();
    if (price < 0) throw odh::FormatError("selected house has a negative SIMI purchase value");
    return price;
}

std::optional<std::string> read_user_string(const std::filesystem::path& game,
        const std::filesystem::path& user_data, const std::filesystem::path& relative_file,
        std::uint16_t chunk_id, std::size_t string_index, std::uint8_t language) {
    const auto candidate = user_data / relative_file;
    std::error_code error;
    if (!std::filesystem::is_regular_file(candidate, error) || error) return std::nullopt;
    const auto path = require_file(game, candidate);
    std::optional<std::string> result;
    bool found_chunk = false;
    with_iff_file(path.string(), [&](const odh::View& view) {
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type != "STR#" || chunk.id != chunk_id) return;
            if (found_chunk) throw odh::FormatError("ambiguous localized lot text resource");
            found_chunk = true;
            const auto table = odh::read_strings(chunk.payload);
            if (const auto* entry = odh::string_in_language_or_english(
                    table, language, string_index)) {
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
                result = odh_host::legacy_text_to_utf8(entry->value, entry->language?entry->language:1);
#else
                result = entry->value;
#endif
            }
        });
    });
    return result;
}

std::pair<std::string, std::string> neighborhood_lot_text(
        const std::filesystem::path& game, const std::filesystem::path& user_data,
        std::uint16_t house_number, std::uint8_t language,
        odh_host::GameUiStrings& ui_strings) {
    const auto description_file = house_number < 80 ? "Houses/NeighborhoodDesc.iff" :
        house_number < 90 ? "Houses/STDesc.iff" : "Houses/MTDesc.iff";
    const auto description_chunk = static_cast<std::uint16_t>(2000 + house_number);
    auto name = read_user_string(game, user_data, description_file,
        description_chunk, 0, language).value_or("");
    auto description = read_user_string(game, user_data, description_file,
        description_chunk, 1, language).value_or("");

    if (name.empty()) {
        const auto assignment = read_user_string(game, user_data, "StreetNames.iff",
            2001, house_number - 1, language);
        if (assignment && !assignment->empty()) {
            unsigned street = 0;
            const auto parsed = std::from_chars(assignment->data(),
                assignment->data() + assignment->size(), street);
            if (parsed.ec == std::errc{} && parsed.ptr == assignment->data() + assignment->size() &&
                street > 0) {
                name = read_user_string(game, user_data, "StreetNames.iff", 2000,
                    street - 1, language).value_or("");
                const auto marker = name.find("%s");
                if (marker != std::string::npos)
                    name.replace(marker, 2, std::to_string(house_number));
            }
        }
    }
    if (name.empty()) name = ui_strings.get(134, 22, language, "House");
    if (description.empty()) description = ui_strings.get(134, 23, language, "No Description");
    return {std::move(name), std::move(description)};
}

std::string format_game_ui_string(std::string pattern,
        const std::vector<std::string>& replacements) {
    std::size_t cursor = 0;
    for (const auto& replacement : replacements) {
        const auto marker = pattern.find('%', cursor);
        if (marker == std::string::npos || marker + 1 >= pattern.size()) break;
        if (pattern[marker + 1] != 's' && pattern[marker + 1] != 'd') continue;
        pattern.replace(marker, 2, replacement);
        cursor = marker + replacement.size();
    }
    return pattern;
}

std::vector<std::filesystem::path> find_files(const std::filesystem::path& root,
        const std::string& exact_name, const std::set<std::string>& extensions,
        std::size_t maximum) {
    std::vector<std::filesystem::path> result;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(root, error), end;
    if (error) throw odh::FormatError("cannot scan installed game resources: " + root.string());
    for (; it != end; it.increment(error)) {
        if (error) throw odh::FormatError("error scanning installed game resources: " + root.string());
        const auto& path = it->path();
        if (it->is_symlink(error)) {
            if (error) throw odh::FormatError("cannot inspect installed game resource");
            if (it->is_directory(error)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(error)) {
            if (error) throw odh::FormatError("cannot inspect installed game resource");
            continue;
        }
        bool match = !exact_name.empty() && path.filename() == exact_name;
        if (!match && !extensions.empty()) {
            auto ext = path.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            match = extensions.count(ext) != 0;
        }
        if (!match) continue;
        if (result.size() >= maximum) throw odh::FormatError("installed game resource count exceeds limit");
        result.push_back(std::filesystem::canonical(path, error));
        if (error) throw odh::FormatError("cannot resolve installed game resource path");
    }
    std::sort(result.begin(), result.end());
    return result;
}

#ifdef ODH_HAVE_X11
odh_host::CommunityLotEditorOptions community_lot_editor_options(
        const odh_host::CommunityLotStoreOptions& store,unsigned language,
        const std::filesystem::path& private_characters) {
    odh_host::CommunityLotEditorOptions result;
    static_cast<odh_host::CommunityLotStoreOptions&>(result)=store;
    result.language=static_cast<std::uint8_t>(language);
    const auto& game=store.game_root;const auto data=game/"GameData";
    result.floors=require_file(game,data/"floors.iff");
    result.walls=require_file(game,data/"walls.iff");
    result.build=require_file(game,data/"Build.iff");
    result.definition_resources={require_file(game,data/"Objects/Objects.far"),
        require_file(game,data/"Global/Global.far"),
        require_file(game,game/"ExpansionShared/ExpansionShared.far")};
    const auto add_arch=[&](const auto& path){result.architecture_sources.push_back(
        {path.string(),path.extension()==".far"?odh::ArchitectureResourceSourceKind::far1_archive:
         odh::ArchitectureResourceSourceKind::loose_file});};
    add_arch(result.definition_resources.front());add_arch(result.definition_resources.back());
    for(unsigned i=1;i<=7;++i){
        const auto name=i==1?std::string("ExpansionPack"):"ExpansionPack"+std::to_string(i);
        const auto path=game/name/(name+".far");
        if(std::filesystem::exists(path)){const auto real=require_file(game,path);
            result.definition_resources.push_back(real);add_arch(real);}
    }
    for(const auto& directory:{store.user_data/"Characters",private_characters})
        if(!directory.empty()&&std::filesystem::is_directory(directory)){
            const auto files=find_files(directory,"",{".iff"},4096);
            result.definition_resources.insert(result.definition_resources.end(),files.begin(),files.end());
        }
    const auto downloads=game/"Downloads";
    if(std::filesystem::is_directory(downloads)){
        for(const auto& path:find_files(downloads,"",{".far"},128)){
            result.definition_resources.push_back(path);add_arch(path);
        }
        const auto loose=find_files(downloads,"",{".iff"},2048);
        result.definition_resources.insert(result.definition_resources.end(),loose.begin(),loose.end());
        for(const auto& path:find_files(downloads,"",{".flr",".wll"},4096))add_arch(path);
    }
    for(const auto& path:find_files(data,"",{".flr",".wll"},4096))add_arch(path);
    return result;
}
#endif

void append(std::vector<std::string>& target, std::initializer_list<std::string> values);

struct RuntimePlan {
    std::vector<std::string> arguments;
    std::vector<std::filesystem::path> resources;
    std::uint8_t expansion_mask = 0;
};

RuntimePlan build_runtime_arguments(const Options& options,
        const std::filesystem::path& game, unsigned house,
        const odh::SessionEntry& session, const odh::SessionEntryMember& resident,
        const std::optional<std::filesystem::path>& neighborhood_graphics,
        const std::filesystem::path& player_character_directory,const std::filesystem::path& userdata,
        const odh::FamilyOverlay* owned_overlay=nullptr) {
    RuntimePlan plan;
    std::set<std::filesystem::path> unique_resources;
    const auto record_resource = [&](const std::filesystem::path& path) {
        if (unique_resources.insert(path).second) plan.resources.push_back(path);
    };
    const auto gamedata = require_directory(game, game / "GameData");
    const auto global_far = require_file(game, gamedata / "Global/Global.far");
    const auto objects_far = require_file(game, gamedata / "Objects/Objects.far");
    const auto animation_far = require_file(game, gamedata / "Animation/Animation.far");
    const auto textures_far = require_file(game, gamedata / "Textures/Textures.far");
    const auto floors = require_file(game, gamedata / "floors.iff");
    const auto walls = require_file(game, gamedata / "walls.iff");
    const auto zoning = require_file(game, userdata / "LotZoning.iff");
    const auto neighborhood = require_file(game, userdata / "Neighborhood.iff");
    const auto house_file = require_file(game, session.house_file);
    const auto lot_locations = require_file(game, userdata / "LotLocations.iff");
    const auto shared = std::vector<std::filesystem::path>{
        require_file(game, game / "ExpansionShared/ExpansionShared.far")};
    std::vector<std::filesystem::path> expansions;
    for (unsigned pack = 1; pack <= 7; ++pack) {
        const auto directory = pack == 1 ? std::string("ExpansionPack")
            : std::string("ExpansionPack") + std::to_string(pack);
        const auto archive = game / directory / (directory + ".far");
        std::error_code error;
        const auto status = std::filesystem::status(archive, error);
        if (error == std::errc::no_such_file_or_directory) continue;
        if (error) throw odh::FormatError("cannot inspect optional expansion archive: " + archive.string());
        if (status.type() == std::filesystem::file_type::not_found) continue;
        if (!std::filesystem::is_regular_file(status))
            throw odh::FormatError("optional expansion archive is not a regular file: " + archive.string());
        expansions.push_back(require_file(game, archive));
    }
    std::vector<std::filesystem::path> download_archives, download_iffs;
    const auto downloads_path = game / "Downloads";
    std::error_code downloads_error;
    const auto downloads_status = std::filesystem::status(downloads_path, downloads_error);
    if (downloads_error != std::errc::no_such_file_or_directory) {
        if (downloads_error)
            throw odh::FormatError("cannot inspect optional Downloads directory: " + downloads_path.string());
        if (downloads_status.type() != std::filesystem::file_type::not_found) {
            if (!std::filesystem::is_directory(downloads_status))
                throw odh::FormatError("optional Downloads path is not a directory: " + downloads_path.string());
            const auto downloads = require_directory(game, downloads_path);
            download_archives = find_files(downloads, "", {".far"}, 128);
            download_iffs = find_files(downloads, "", {".iff"}, 128);
        }
    }

    const auto characters = require_directory(game, userdata / "Characters");
    // Discovery-only Person resources are not additional executed controllers
    // or buy-catalog entries. These files already belong to the existing
    // session manifest; retain its identity and the original roster flags.
    auto character_definition_files=find_files(characters,"",{".iff"},4096);
    character_definition_files.erase(std::unique(character_definition_files.begin(),character_definition_files.end()),
        character_definition_files.end());
    const auto resolve_character = [&](const odh::SessionEntryMember& member) {
        const std::filesystem::path asset_path(member.character_asset);
        if (asset_path.empty() || asset_path.filename()!=asset_path || asset_path.is_absolute())
            throw odh::FormatError("character catalog returned an invalid asset name");
        if (member.character_source=="UserData/Characters")
            return require_file(game,characters/asset_path);
        if (member.character_source!="PlayerData/Characters" || player_character_directory.empty())
            throw odh::FormatError("character catalog returned an unknown resource source");
        std::error_code character_error;
        const auto directory_status=std::filesystem::symlink_status(
            player_character_directory,character_error);
        if (character_error || std::filesystem::is_symlink(directory_status) ||
            !std::filesystem::is_directory(directory_status))
            throw odh::FormatError("player character directory is missing or not a real directory");
        const auto private_root=std::filesystem::canonical(player_character_directory,character_error);
        if (character_error) throw odh::FormatError("cannot resolve player character directory");
        const auto candidate=private_root/asset_path;
        const auto status=std::filesystem::symlink_status(candidate,character_error);
        if (character_error || std::filesystem::is_symlink(status) ||
            !std::filesystem::is_regular_file(status))
            throw odh::FormatError("player character resource is missing or not a regular file");
        const auto resolved=std::filesystem::canonical(candidate,character_error);
        if (character_error || resolved.parent_path()!=private_root)
            throw odh::FormatError("player character resource escaped its private directory");
        const auto size=std::filesystem::file_size(resolved,character_error);
        if (character_error || size>64u*1024u*1024u)
            throw odh::FormatError("player character resource exceeds the 64 MiB limit");
        return resolved;
    };
    // Receipt-owned files are discovery inputs, independently of current FAMI
    // or HHRD presence. Their hashes are authenticated by their creation/catalog
    // owners, and they never enter the immutable commercial META manifest.
    std::vector<std::filesystem::path> owned_definition_files;
    if (owned_overlay && !player_character_directory.empty()) {
        odh::CharacterCatalogLimits private_limits;
        private_limits.files=1024;private_limits.total_file_bytes=64ull*1024ull*1024ull;
        private_limits.person_definitions=4096;
        const odh::DefinitionIndex owned(odh::read_character_catalog_if_present(player_character_directory,private_limits));
        std::set<std::uint32_t> receipt_guids;
        const auto discover=[&](std::uint32_t guid,const odh::NativePersonalIdentity* receipt){
            if(!receipt_guids.insert(guid).second) throw odh::FormatError("private character has duplicate creation receipts");
            const auto* definition=owned.unique(guid);
            if(!definition || definition->definition.version!=138 || definition->definition.object_type!=2)
                throw odh::FormatError("receipt-owned private character is missing or ambiguous");
            odh::SessionEntryMember member{guid,1,"PlayerData/Characters",definition->asset,definition->chunk_id};
            const auto file=resolve_character(member);
            if(receipt && (receipt->definition_chunk_id!=definition->chunk_id ||
                receipt->character_asset!="PlayerData/Characters/"+definition->asset ||
                odh_host::fingerprint_transfer_resource(file)!=receipt->character_fingerprint))
                throw odh::FormatError("private character differs from its creation receipt");
            if(owned_overlay->personal_state && owned_overlay->personal_state->catalog) {
                for(const auto& old:owned_overlay->personal_state->catalog->definitions) if(old.guid==guid)
                    if(old.source!="PlayerData/Characters/"+definition->asset || !old.asset.empty() ||
                       old.definition_chunk_id!=definition->chunk_id || old.fingerprint!=odh_host::fingerprint_transfer_resource(file))
                        throw odh::FormatError("private character differs from its original catalog receipt");
            }
            owned_definition_files.push_back(file);
        };
        for(const auto& family:owned_overlay->creations) for(const auto& member:family.members) discover(member.guid,nullptr);
        if(owned_overlay->character_family) for(const auto& character:owned_overlay->character_family->characters)
            discover(character.identity.guid,&character.identity);
    }
    std::vector<std::filesystem::path> character_files;
    for (const auto& member : session.members) {
        const auto character=resolve_character(member);
        if (std::find(character_files.begin(), character_files.end(), character) == character_files.end())
            character_files.push_back(character);
    }
    // A source lot can still serialize its former residents or visitors. Their
    // exact definitions are needed to validate that World BEFORE retirement;
    // the selected family's files alone do not describe the installed snapshot.
    std::optional<odh::Objt> lot_types;
    std::optional<odh::Objm> lot_objects;
    with_iff_file(house_file.string(), [&](const auto& view) {
        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
            if (chunk.type == "objt" && chunk.id == 0) {
                if (lot_types) throw odh::FormatError("ambiguous source lot objt0");
                lot_types = odh::read_objt(chunk.payload);
            } else if (chunk.type == "ObjM" && chunk.id == 1) {
                if (lot_objects) throw odh::FormatError("ambiguous source lot ObjM1");
                lot_objects = odh::read_objm(chunk.payload);
            }
        });
    });
    if (lot_types && lot_objects) {
        const odh::DefinitionIndex installed_characters(odh::read_character_catalog(characters));
        for (const auto& saved : odh::read_saved_object_prefixes(*lot_objects, *lot_types)) {
            const auto* type = lot_types->entry_for_type(saved.type_id);
            if (!type || !type->metadata || type->metadata->objd_type != 2) continue;
            const auto* definition = installed_characters.unique(saved.guid);
            if (!definition) {
                if (installed_characters.candidates(saved.guid))
                    throw odh::FormatError("source lot character definition is ambiguous");
                continue; // A packaged prototype can be supplied by the object archives.
            }
            const auto path = require_file(game, std::filesystem::path(definition->source) / definition->asset);
            if (std::find(character_files.begin(), character_files.end(), path) == character_files.end())
                character_files.push_back(path);
        }
    }
    std::filesystem::path selected_character;
    std::string selected_semiglobal="PersonGlobals.iff";
    std::optional<std::string> selected_archive_entry;
    if(session.npc_primary||session.metadata_bootstrap){
        const auto& anchor=session.metadata_bootstrap?*session.metadata_bootstrap:*session.npc_primary;
        if(anchor.resource.guid!=resident.guid||anchor.resource.neighbor_id!=resident.neighbor_id)
            throw odh::FormatError("selected NPC differs from the authenticated execution anchor");
        const auto locator=odh::native_personal_resource_locator(odh::native_personal_resource_key(resident.character_source,resident.character_asset));
        if(!locator.asset.empty())selected_archive_entry=locator.asset;
        if(locator.character_catalog){
            const std::filesystem::path logical(locator.source);const auto parent=logical.parent_path().generic_string();
            const auto root=parent=="UserData/Characters"?characters:parent=="PlayerData/Characters"?player_character_directory:std::filesystem::path{};
            if(root.empty()||std::filesystem::is_symlink(root/logical.filename()))throw odh::FormatError("NPC execution character path is invalid");
            selected_character=std::filesystem::canonical(root/logical.filename());
            if(selected_character.parent_path()!=std::filesystem::canonical(root))throw odh::FormatError("NPC execution character escaped its catalog");
        }else selected_character=require_file(game,game/locator.source);
        const auto inspect=[&](const auto& view){
            if(view.size()>16u*1024u*1024u)throw odh::FormatError("NPC execution resource exceeds bounds");
            std::string bytes(static_cast<std::size_t>(view.size()),'\0');view.read(0,bytes.data(),bytes.size());
            if(odh::sha256(bytes.data(),bytes.size())!=anchor.fingerprint)throw odh::FormatError("NPC execution character bytes changed");
            unsigned found=0;std::optional<std::string> link;
            odh::iff_chunks(view,[&](const auto& chunk){
                if(chunk.type=="OBJD"&&chunk.id==resident.object_definition_id){
                    const auto definition=odh::read_object_definition(chunk.payload);
                    if(definition.guid!=resident.guid||definition.object_type!=2)throw odh::FormatError("NPC execution definition changed");++found;
                }else if(chunk.type=="GLOB"){
                    if(link)throw odh::FormatError("NPC execution GLOB is ambiguous");link=odh::read_glob(chunk.payload).name;
                    if(link->empty()||link->find_first_of("/\\:.")!=std::string::npos||link->find('\0')!=std::string::npos)
                        throw odh::FormatError("NPC execution GLOB is not a flat resource name");
                }
            });if(found!=1)throw odh::FormatError("NPC execution definition is missing or ambiguous");
            if(link){
                std::optional<std::string> exact;
                const auto fold=[](std::string text){for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return text;};
                std::ifstream stream(global_far,std::ios::binary);const odh::View archive(stream,std::filesystem::file_size(global_far));
                for(const auto& entry:odh::far1_index(archive))if(fold(entry.name)==fold(*link+".iff")){
                    if(exact)throw odh::FormatError("NPC execution semiglobal is ambiguous");exact=entry.name;
                }
                if(!exact)throw odh::FormatError("NPC execution semiglobal is absent");selected_semiglobal=*exact;
            }
        };
        if(locator.asset.empty())with_iff_file(selected_character.string(),inspect);else with_far_entry(selected_character.string(),locator.asset,inspect);
    }else{
        selected_character=resolve_character(resident);
        if(std::find(character_files.begin(),character_files.end(),selected_character)==character_files.end())
            throw odh::FormatError("selected resident character resource is absent from the resolved family");
    }
    // Historical bootstrap bytes remain part of the exact immutable manifest.
    // No extra --object-definition-file or body is introduced for this anchor.
    // Expansion/Downloads FARs contain external floors and walls too. Index
    // their material entries before loose files, as in the TS1 providers.
    // These archives already belong to the resource manifest; do not change
    // its identity merely because another consumer now reads their contents.
    std::vector<std::filesystem::path> architecture{objects_far};
    architecture.insert(architecture.end(),shared.begin(),shared.end());
    architecture.insert(architecture.end(),expansions.begin(),expansions.end());
    architecture.insert(architecture.end(),download_archives.begin(),download_archives.end());
    const auto loose_architecture=find_files(gamedata,"",{".flr",".wll"},4096);
    architecture.insert(architecture.end(),loose_architecture.begin(),loose_architecture.end());

    record_resource(global_far);
    record_resource(objects_far);
    record_resource(animation_far);
    record_resource(textures_far);
    record_resource(floors);
    record_resource(walls);
    record_resource(zoning);
    record_resource(neighborhood);
    record_resource(lot_locations);
    record_resource(house_file);
    for (const auto& archive : shared) record_resource(archive);
    for (const auto& archive : expansions) record_resource(archive);
    for (const auto& archive : download_archives) record_resource(archive);
    for (const auto& iff : download_iffs) record_resource(iff);
    for (const auto& character : character_definition_files)
        record_resource(character);
    for (const auto& resource : architecture) record_resource(resource);
    if (neighborhood_graphics) record_resource(*neighborhood_graphics);
    std::sort(plan.resources.begin(), plan.resources.end());

    // Expansion flags only describe pack archives actually passed to this
    // runtime plan. ExpansionShared is common support data, not an expansion.
    for (const auto& resource : plan.resources) {
        const auto relative = resource.lexically_relative(game).generic_string();
        for (unsigned pack = 1; pack <= 7; ++pack) {
            const auto directory = pack == 1 ? std::string("ExpansionPack")
                : std::string("ExpansionPack") + std::to_string(pack);
            const auto filename = directory + ".far";
            if (relative == directory + "/" + filename)
                plan.expansion_mask |= static_cast<std::uint8_t>(1u << (pack - 1));
        }
    }

    auto& args = plan.arguments;
    args = {"odh-person", "--avatar-file", selected_character.string(),
        std::to_string(resident.object_definition_id), "--semiglobal", global_far.string(),
        selected_semiglobal, "--global", global_far.string(), "Global.iff",
        "--lot", house_file.string(), std::to_string(house), "--zoning", zoning.string(),
        "--neighborhood", neighborhood.string(), "--activate-family", "--language",
        std::to_string(options.language), "--object-definitions", objects_far.string()};
    if(selected_archive_entry){
        args[1]="--avatar";args.insert(args.begin()+3,*selected_archive_entry);
    }
    for (const auto& archive : shared)
        append(args, {"--object-definitions", archive.string()});
    for (const auto& archive : expansions)
        append(args, {"--object-definitions", archive.string()});
    for (const auto& archive : download_archives)
        append(args, {"--object-definitions", archive.string()});
    for (const auto& character : character_definition_files)
        append(args, {"--character-definition-file", character.string()});
    for (const auto& character : owned_definition_files)
        append(args, {"--character-definition-file", character.string()});
    for (const auto& character : character_files)
        append(args, {"--object-definition-file", character.string()});
    for (const auto& iff : download_iffs)
        append(args, {"--object-definition-file", iff.string()});
    append(args, {"--animation-source", animation_far.string()});
    for (const auto& archive : shared) append(args, {"--animation-source", archive.string()});
    for (const auto& archive : expansions) append(args, {"--animation-source", archive.string()});
    for (const auto& archive : download_archives) append(args, {"--animation-source", archive.string()});
    append(args, {options.headless_ticks?"--headless-main":"--saved-world-main", "--render-saved-guid", std::to_string(resident.guid),
        "--render-floors", floors.string(), "--render-walls", walls.string(),
        "--render-wall-mode", "up", "--render-terrain-theme", "0"});
    if(options.audio_enabled||(!options.headless_ticks&&!options.audio_disabled))append(args,{"--session-audio"});
    if(options.audio_drain_ms)append(args,{"--audio-drain-ms",std::to_string(options.audio_drain_ms)});
    if (options.headless_ticks)
        append(args, {"--ticks", std::to_string(*options.headless_ticks)});
    else {
        const auto size=options.lot_window_size.value_or(std::array<unsigned,2>{800,600});
        append(args, {"--render-window", "--render-size", std::to_string(size[0]), std::to_string(size[1]),
            "--render-animation-far", animation_far.string(),
            "--render-textures", textures_far.string(), "--render-skeleton", "adult-skeleton.cmx.bcf", "adult"});
    }
    if(options.check_recovery_position)append(args,{"--headless-check-recovery-position"});
    if(options.place_recovery_position)append(args,{"--headless-place-recovery-position"});
    if(options.recover_saved_code)append(args,{"--recover-saved-code"});
    if(options.headless_checkpoint_interval)
        append(args,{"--headless-checkpoint-interval",std::to_string(*options.headless_checkpoint_interval)});
    if(options.headless_dialog_ok)append(args,{"--headless-dialog-ok"});
    if(options.headless_info_dialog_limit)append(args,{"--headless-info-dialog-ok-limit",std::to_string(options.headless_info_dialog_limit)});
    for(const auto& response:options.headless_dialog_responses)
        append(args,{"--headless-dialog-response",std::to_string(response.code),response.text});
    if(options.stop_route_depth)append(args,{"--headless-stop-route-depth",std::to_string(*options.stop_route_depth)});
    if(options.headless_entity_move){
        const auto& move=*options.headless_entity_move;
        append(args,{"--headless-move-entity",std::to_string(move.id),std::to_string(move.x),
            std::to_string(move.y),std::to_string(move.level),std::to_string(move.direction),std::to_string(move.tick)});
    }
    if (!options.headless_ticks && neighborhood_graphics)
        append(args, {"--ui-graphics", neighborhood_graphics->string()});
    if (options.status_frame)
        append(args, {"--render-frame", options.status_frame->string(), "--render-tick", std::to_string(*options.status_tick),
            "--render-animation-far", animation_far.string(), "--render-textures", textures_far.string(),
            "--render-skeleton", "adult-skeleton.cmx.bcf", "adult"});
    if (options.instruction_budget)
        append(args, {"--instruction-budget", std::to_string(*options.instruction_budget)});
    if (options.headless_sit_object)
        append(args, {"--headless-interaction", std::to_string(*options.headless_sit_object), "1"});
    if (options.headless_single_resident_go_here) {
        const auto& request=*options.headless_single_resident_go_here;
        append(args,{"--headless-go-here",std::to_string(request.x),std::to_string(request.y),
            std::to_string(request.level),std::to_string(request.tick),"--headless-go-here-same-room-only"});
        if(request.actor_id)
            append(args,{"--headless-go-here-primary-actor",std::to_string(request.actor_id)});
    }
    if (options.headless_sit_object && options.headless_stand_tick)
        append(args, {"--headless-stand", std::to_string(*options.headless_sit_object),
            std::to_string(*options.headless_stand_tick)});
    if (options.headless_resident_sit) {
        append(args, {"--headless-resident-interaction", std::to_string(options.headless_resident_sit->actor_id),
            std::to_string(options.headless_resident_sit->object_id), "1"});
        append(args, {"--headless-stand", std::to_string(options.headless_resident_sit->object_id),
            std::to_string(options.headless_resident_sit->stand_tick)});
    }
    const auto append_resident_interaction=[&](const Options::ResidentInteraction& request){
        append(args,{request.variant_param0?(request.variant_hex?"--headless-resident-interaction-variant-hex":"--headless-resident-interaction-variant"):request.row?"--headless-resident-interaction-row":"--headless-resident-interaction",
            std::to_string(request.actor_id),std::to_string(request.object_id)});
        if(request.row)args.push_back(std::to_string(*request.row));
        if(request.variant_param0)append(args,{std::to_string(*request.variant_param0),std::to_string(request.variant_language),request.variant_name});
        args.push_back(std::to_string(request.tick));
    };
    for(const auto& query:options.headless_menu_queries)
        append(args,{"--headless-menu-query",std::to_string(query.first),std::to_string(query.second)});
    if (options.headless_resident_interaction)
        append_resident_interaction(*options.headless_resident_interaction);
    for(const auto& request:options.headless_resident_interactions)
        append_resident_interaction(request);
    if(options.headless_cancel_interaction_tick)
        append(args,{"--headless-cancel-interaction",std::to_string(*options.headless_cancel_interaction_tick)});
    if (options.headless_stand) {
        if (!options.restore_bundle || !options.restore_bundle->seated_continuation)
            throw odh::FormatError("Stand request has no verified saved seat");
        append(args, {"--headless-stand",
            std::to_string(options.restore_bundle->seated_continuation->target_id), "1"});
    }
    for (const auto& request : options.headless_resident_stands) {
        if (!options.restore_bundle)
            throw odh::FormatError("resident Stand request has no saved household session");
        const auto target=saved_resident_stand_target(*options.restore_bundle,request.actor_id);
        if (!target)
            throw odh::FormatError("resident Stand actor has no verified saved household seat");
        append(args,{"--headless-resident-stand",std::to_string(request.actor_id),
            std::to_string(*target),std::to_string(request.tick)});
    }
    for (const auto& resource : architecture)
        append(args, {"--architecture-resource", resource.string()});
    return plan;
}

std::string digest_hex(const std::array<std::uint8_t,32>& digest) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 0x0f]);
    }
    return text;
}

void print_plan(const Options& options, unsigned house,
                const odh::SessionEntry& session, const odh::SessionEntryMember& resident,
                const RuntimePlan& plan, const odh::SessionResourceIdentity& identity,
                const std::filesystem::path& game_root) {
    std::cout << "{\"status\":\"ready\",\"neighborhood_number\":" << options.neighborhood << ",\"mode\":" << options.mode
        << ",\"house\":" << house << ",\"family_chunk_id\":" << session.family_chunk_id
        << ",\"resident_guid\":" << (session.metadata_bootstrap?0:resident.guid) << ",\"resident_neighbor_id\":"
        << (session.metadata_bootstrap?0:resident.neighbor_id) << ",\"resident_asset\":" << json_bytes(resident.character_asset)
        << ",\"metadata_bootstrap\":" << (session.metadata_bootstrap?"true":"false")
        << ",\"resident_count\":" << session.members.size()
        << ",\"resident_execution\":" << json_bytes(options.single_resident_session ? "single_resident_session" : "whole_household")
        << ",\"resource_identity\":{\"language\":" << static_cast<unsigned>(identity.language_code)
        << ",\"expansion_mask\":" << static_cast<unsigned>(identity.expansion_mask)
        << ",\"resource_count\":" << plan.resources.size()
        << ",\"sha256\":" << json_bytes(digest_hex(identity.resource_fingerprint))
        << ",\"files\":[";
    for (std::size_t i = 0; i < plan.resources.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << json_bytes(plan.resources[i].lexically_relative(game_root).generic_string());
    }
    std::cout << "]},\"command\":[";
    for (std::size_t i = 0; i < plan.arguments.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << json_bytes(plan.arguments[i]);
    }
    std::cout << "]}\n";
}

void append(std::vector<std::string>& target, std::initializer_list<std::string> values) {
    target.insert(target.end(), values.begin(), values.end());
}

} // namespace

int odh_linux_game_entry(int argc, char** argv, odh_host::LinuxSessionLog* session_log) {
    try {
#ifdef ODH_HAVE_LINUX_SESSION_LOG
        if(session_log)session_log->stage("launcher-startup");
#endif
        auto options = parse_options(argc, argv);
#ifdef ODH_HAVE_LINUX_SESSION_LOG
        if(session_log) {
            session_log->stage("launcher-options-ready");
            session_log->event("language="+std::to_string(options.language));
        }
#endif
        std::error_code error;
        const auto game = std::filesystem::canonical(options.game_root, error);
        if (error || !std::filesystem::is_directory(game))
            throw odh::FormatError("game root is not an accessible directory: " + options.game_root.string());
        options.game_root = game;
#ifdef ODH_HAVE_LINUX_SESSION_LOG
        if(session_log)session_log->stage("game-root-ready");
#endif
        odh_host::NeighborhoodStorage storage(options.neighborhood);
        auto userdata = require_directory(game, game/storage.user_data_name());
        if(userdata!=game/storage.user_data_name())throw odh::FormatError("neighborhood directory aliases another source");
#ifdef ODH_HAVE_LINUX_USER_CONFIG
        auto player_character_directory=odh_host::linux_player_character_directory(storage);
#else
        const std::filesystem::path player_character_directory;
#endif
#ifdef __linux__
        auto family_overlay = odh_host::read_linux_family_overlay_for_neighborhood(
            userdata / "Neighborhood.iff",storage);
#else
        std::optional<odh::FamilyOverlay> family_overlay;
#endif
        std::shared_ptr<odh_host::FamilySessionJournal> pending_move_in;
#if defined(__linux__) && defined(ODH_HAVE_X11)
        struct GuiPropertyReleaseRequest {
            Options previous_options;
            odh::FamilyOverlay expected_overlay;
            std::array<std::uint8_t,32> neighborhood_identity;
            std::uint16_t family=0,house=0;
            std::filesystem::path checkpoint;
        };
        std::optional<GuiPropertyReleaseRequest> gui_property_release;
#endif
#ifdef __linux__
        using CurrentProperty=std::pair<odh_host::StoredCommunityLot,odh::FamilyPropertyRelease>;
        const auto load_current_property=[&](std::uint16_t house,const std::optional<odh::FamilyOverlay>& overlay)->std::optional<CurrentProperty>{
            if(!overlay)return {};
            const auto source=odh::load_neighborhood_family_source(userdata/"Neighborhood.iff");
            const auto replay=odh::replay_family_overlay(source.families,source.identity,*overlay);
            const auto head=std::find_if(replay.properties.begin(),replay.properties.end(),[&](const auto& value){return value.house==house;});
            if(head==replay.properties.end())return {};
            const auto release=std::find_if(overlay->property_releases.begin(),overlay->property_releases.end(),[&](const auto& value){
                return value.house==house&&value.head==head->head&&value.expected_revision+1==head->revision&&
                    value.installed_lot_sha256==head->installed_lot_sha256&&value.policy==head->policy&&
                    value.neighborhood==head->neighborhood&&value.mode==head->mode&&value.vacant_purchase_value==head->vacant_purchase_value;});
            if(release==overlay->property_releases.end())throw odh::FormatError("current residential property has no exact committed release");
            // One HOUSE authority across modes1/4. Mode here locates the blob's
            // provenance directory; it never selects another property HEAD.
            auto stored=odh_host::load_community_lot_blob(community_lot_store_options(game,userdata,storage,release->mode,house),head->head);
            if(!stored.document.objects)throw odh::FormatError("released residence lacks its actual object lifetime");
            return CurrentProperty{std::move(stored),*release};
        };
#endif
        const auto current_house_purchase_price=[&](std::uint16_t house,const std::optional<odh::FamilyOverlay>& overlay){
#ifdef __linux__
            if(auto property=load_current_property(house,overlay))return property->second.vacant_purchase_value;
#else
            (void)overlay;
#endif
            return house_purchase_price(game,userdata,house);
        };
#ifdef __linux__
        const auto prepare_lot_transfer = [&](const odh::NeighborhoodFamilySource& source,
                const odh::FamilyOverlay& overlay, std::uint16_t family_id, std::uint16_t destination, unsigned entry_mode=0) {
            const auto entry = odh::load_session_entry(userdata, entry_mode?entry_mode:options.mode, destination,
                static_cast<std::uint8_t>(options.language), &overlay, player_character_directory);
            const auto roster = odh_host::identify_transfer_household(entry, game, player_character_directory,storage);
            return odh_host::load_family_transfer(source, overlay, family_id, roster, userdata / "Neighborhood.iff",storage);
        };
        if (options.headless_move_out_family) {
            const auto source = odh::load_neighborhood_family_source(userdata / "Neighborhood.iff");
            const auto id = static_cast<std::uint16_t>(*options.headless_move_out_family);
            odh_host::FamilySessionJournal journal(source.families, source.identity, family_overlay,
                static_cast<std::uint16_t>(*options.house), id,std::nullopt,storage);
            if (!journal.checkpoint_path() || !prepare_lot_transfer(source, *family_overlay, id,
                    static_cast<std::uint16_t>(*options.house)))
                throw odh::FormatError("managed move-out requires a current transferable checkpoint");
            // Validate the portable household before preparing real filtering.
            // Actual money/vacancy/head publication occurs only after the same
            // integrated runtime returns its typed prepared release below.
            options.load_session=journal.checkpoint_path();
            options.load_session_advisory=false;
        }
        if (options.headless_move_in_family) {
            const auto source = odh::load_neighborhood_family_source(userdata / "Neighborhood.iff");
            const auto id = static_cast<std::uint16_t>(*options.headless_move_in_family);
            const auto destination = static_cast<std::uint16_t>(*options.house);
            const auto price = current_house_purchase_price(destination,family_overlay);
            const auto next = odh::plan_family_managed_move_in_overlay(source.families, source.identity,
                family_overlay ? &*family_overlay : nullptr, id, destination, price);
            (void)prepare_lot_transfer(source, next, id, destination);
            pending_move_in = std::make_shared<odh_host::FamilySessionJournal>(source.families, source.identity,
                family_overlay, destination, id, next,storage);
        }
#endif
        std::optional<std::filesystem::path> graphics_resource;
#ifdef ODH_HAVE_X11
        const auto graphics = require_file(game, game / "UIGraphics/UIGraphics.far");
        graphics_resource = graphics;
#endif

#ifdef ODH_HAVE_X11
        std::optional<odh_host::PreparedNeighborhoodMap> map;
        std::unique_ptr<odh_host::LinuxWindow> window;
        std::shared_ptr<odh_host::OriginalCursorPresentation> cursors;
        std::unique_ptr<odh_host::GameUiStrings> ui_string_catalog;
        std::string lot_panel_accept = "OK";
        std::string lot_panel_back = "Cancel";
        std::string move_in_title = "Purchase House?";
        std::string move_in_question = "Are you sure you want to buy this house?";
        std::string move_out_title = "Evict Family?";
        std::string move_out_question = "Are you sure you want to evict the %s family? Their net worth will be %s.";
        std::string confirm_yes = "Yes";
        std::string confirm_no = "No";
        std::string family_picker_action = "Move In Family";
        std::string family_picker_title = "SELECT A FAMILY";
        std::string no_family_title = "Sorry";
        std::string no_family_message = "You can't afford it.";
        std::string family_picker_cancel = "Cancel";
        std::string family_picker_net_worth = "Net Worth";
        std::string family_picker_friends = "Family Friends";
        std::string family_picker_archive = "Delete Family";
        std::string family_name_prompt = "Enter Last Name:";
        std::string family_archive_title = "Delete Family?";
        std::string family_archive_question = "Are you sure you want to delete the \"%s\" family?";
        std::string neighborhood_title = "Neighborhood";
        std::string neighborhood_cancel = "Cancel";
        if (!options.plan_only && !options.headless_ticks) {
#ifdef ODH_HAVE_LINUX_SESSION_LOG
            if(session_log)session_log->stage("neighborhood-ui");
#endif
            ui_string_catalog = std::make_unique<odh_host::GameUiStrings>(game);
            auto& ui_strings = *ui_string_catalog;
            window = std::make_unique<odh_host::LinuxWindow>(options.load_session ? 320 : 800,
                options.load_session ? 240 : 600, "OpenDollhouse");
            try{cursors=std::make_shared<odh_host::OriginalCursorPresentation>(game,*window);}
            catch(const std::exception& error){
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                if(session_log)session_log->event(std::string("original cursors unavailable: ")+error.what());
#endif
            }
            if (options.choose_language) {
                const auto selected = odh_host::show_language_picker(*window,
                    static_cast<std::uint8_t>(options.language));
                if (!selected) {
                    std::cout << "{\"status\":\"cancelled\",\"stage\":\"language\"}\n";
                    return 0;
                }
                options.language = *selected;
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                if(session_log)session_log->event("language="+std::to_string(options.language));
#endif
            }
            present_loading_screen(*window,static_cast<std::uint8_t>(options.language));
            move_in_title = ui_strings.get(132, 0,
                static_cast<std::uint8_t>(options.language), "Purchase House?");
            move_in_question = ui_strings.get(132, 1,
                static_cast<std::uint8_t>(options.language),
                "Are you sure you want to buy this house?");
            move_out_title = ui_strings.get(131, 2,
                static_cast<std::uint8_t>(options.language), "Evict Family?");
            move_out_question = ui_strings.get(131, 3,
                static_cast<std::uint8_t>(options.language),
                "Are you sure you want to evict the %s family? Their net worth will be %s.");
            confirm_yes = ui_strings.get(142, 2,
                static_cast<std::uint8_t>(options.language), "Yes");
            confirm_no = ui_strings.get(142, 3,
                static_cast<std::uint8_t>(options.language), "No");
            family_name_prompt = ui_strings.get(129, 12,
                static_cast<std::uint8_t>(options.language), options.language==14?"Digite o sobrenome:":"Enter Last Name:");
            family_picker_action = ui_strings.get(128, 3,
                static_cast<std::uint8_t>(options.language), "Move In Family");
            family_picker_title = ui_strings.get(128, 8,
                static_cast<std::uint8_t>(options.language), "SELECT A FAMILY");
            no_family_title = ui_strings.get(132, 2,
                static_cast<std::uint8_t>(options.language), "Sorry");
            no_family_message = ui_strings.get(132, 3,
                static_cast<std::uint8_t>(options.language), "You can't afford it.");
            family_picker_cancel = ui_strings.get(128, 5,
                static_cast<std::uint8_t>(options.language), "Cancel");
            family_picker_net_worth = ui_strings.get(128, 10,
                static_cast<std::uint8_t>(options.language), "Net Worth");
            family_picker_friends = ui_strings.get(128, 11,
                static_cast<std::uint8_t>(options.language), "Family Friends");
            family_picker_archive = ui_strings.get(128, 4,
                static_cast<std::uint8_t>(options.language), "Delete Family");
            family_archive_title = ui_strings.get(128, 6,
                static_cast<std::uint8_t>(options.language), "Delete Family?");
            family_archive_question = ui_strings.get(128, 7,
                static_cast<std::uint8_t>(options.language),
                "Are you sure you want to delete the \"%s\" family?");
            neighborhood_title = ui_strings.get(145, 1,
                static_cast<std::uint8_t>(options.language), "Neighborhood");
            neighborhood_cancel = ui_strings.get(128, 5,
                static_cast<std::uint8_t>(options.language), "Cancel");
            lot_panel_accept = ui_strings.get(142, 0,
                static_cast<std::uint8_t>(options.language), "OK");
            lot_panel_back = ui_strings.get(142, 1,
                static_cast<std::uint8_t>(options.language), "Cancel");
            map = odh_host::prepare_neighborhood_map(userdata, graphics, options.mode,
                family_overlay ? &*family_overlay : nullptr,
                static_cast<std::uint8_t>(options.language));
        }
#else
        if (!options.plan_only && !options.headless_ticks)
            throw odh::FormatError("interactive game entry requires a build with X11 support");
        if (!options.house)
            throw odh::FormatError("--house is required for --plan-only without X11 support");
#endif

        unsigned house = options.house.value_or(0);
#ifdef ODH_HAVE_X11
        const auto run_management_map = [&]() -> bool {
            if(cursors)(void)cursors->select(odh_host::OriginalCursorAsset::Arrow);
            // Enter only before a runtime exists, or after it saved and unwound.
            // Management can change authority: always discard a prospective view.
            pending_move_in.reset();
            family_overlay = odh_host::read_linux_family_overlay_for_neighborhood(userdata / "Neighborhood.iff",storage);
            map = odh_host::prepare_neighborhood_map(userdata, graphics, options.mode,
                family_overlay ? &*family_overlay : nullptr, static_cast<std::uint8_t>(options.language));
#ifdef ODH_HAVE_LINUX_PRIVATE_FAMILY
            const auto rename_family = [&](const odh::FamilyEntry& selected_family) -> bool {
                // Keep the snapshot presented before editing: a later writer
                // must never be silently accepted as this operation's base.
                const auto expected_overlay = family_overlay;
                const auto neighborhood = userdata / "Neighborhood.iff";
                const auto identity = odh_host::linux_neighborhood_identity(neighborhood);
                const auto selected_identity = odh::family_overlay_identity(selected_family);
                const auto checked_map = odh_host::prepare_neighborhood_map(userdata, graphics,
                    options.mode, expected_overlay ? &*expected_overlay : nullptr,
                    static_cast<std::uint8_t>(options.language));
                const auto checked = std::find_if(checked_map.effective_families.begin(),
                    checked_map.effective_families.end(), [&](const auto& entry) {
                        return entry.chunk_id == selected_family.chunk_id;
                    });
                if (checked == checked_map.effective_families.end() ||
                    odh::family_overlay_identity(*checked) != selected_identity ||
                    odh_host::linux_neighborhood_identity(neighborhood) != identity)
                    throw odh::FormatError("selected family changed before name editing");
                const auto family_id = selected_family.chunk_id;
                const auto name = odh_host::show_family_name_editor(*window,
                    selected_family.display_name, static_cast<std::uint8_t>(options.language),
                    options.language == 14 ? "Renomear família" : "Rename Family",
                    lot_panel_accept, family_picker_cancel, graphics, family_name_prompt);
                if (!name) {
                    const auto reason = window->quit_reason();
                    if (reason == odh_host::WindowQuitReason::window_manager_close ||
                        reason == odh_host::WindowQuitReason::window_destroyed)
                        return false;
                    window->clear_escape_quit_reason();
                    return true;
                }
                if (odh_host::linux_neighborhood_identity(neighborhood) != identity)
                    throw odh::FormatError("Neighborhood.iff changed during family name editing");
                family_overlay = odh_host::rename_linux_private_family(neighborhood,
                    odh_host::linux_family_overlay_path(identity,storage), identity, expected_overlay,
                    family_id, *name, static_cast<std::uint8_t>(options.language));
                map = odh_host::prepare_neighborhood_map(userdata, graphics, options.mode,
                    &*family_overlay, static_cast<std::uint8_t>(options.language));
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                if (session_log) session_log->event("private household display name updated");
#endif
                return true;
            };
#endif
#ifdef ODH_HAVE_LINUX_CAS_SCREEN
            const auto create_family=[&]()->bool{
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                if(session_log)session_log->stage("cas");
#endif
                const auto draft = odh_host::show_linux_cas_family_creator(*window,
                    odh_host::scan_linux_cas_catalog(game),
                    static_cast<std::uint8_t>(options.language),game);
                if (!draft) {
                    if(window->quit_reason()==odh_host::WindowQuitReason::window_manager_close||
                       window->quit_reason()==odh_host::WindowQuitReason::window_destroyed)return false;
                    window->clear_escape_quit_reason();
                    return true;
                }
                if (player_character_directory.empty())
                    throw odh::FormatError("player data directory is unavailable for CAS characters");
                const auto objects_far = require_file(game,
                    game / "GameData/Objects/Objects.far");
                std::vector<std::uint8_t> template_person;
                with_far_entry(objects_far.string(), R"(People\TemplatePerson.iff)",
                    [&](const odh::View& view) {
                        constexpr std::uint64_t maximum_template_size = 16u * 1024u * 1024u;
                        if (view.size() > maximum_template_size)
                            throw odh::FormatError("TemplatePerson.iff exceeds the 16 MiB CAS limit");
                        template_person.resize(static_cast<std::size_t>(view.size()));
                        if (!template_person.empty())
                            view.read(0, template_person.data(), template_person.size());
                    });
                std::vector<odh_host::LinuxPrivateFamilyMemberDraft> members;
                members.reserve(draft->members.size());
                for (const auto& member : draft->members)
                    members.push_back(odh_host::make_simitone_cas_member(member));
                const auto neighborhood = userdata / "Neighborhood.iff";
                const auto identity = odh_host::linux_neighborhood_identity(neighborhood);
                (void)odh_host::create_linux_private_family(template_person,
                    userdata / "Characters", player_character_directory, neighborhood,
                    odh_host::linux_family_overlay_path(identity,storage), draft->family_name_utf8,
                    members);
                family_overlay = odh_host::read_linux_family_overlay_for_neighborhood(neighborhood,storage);
                map = odh_host::prepare_neighborhood_map(userdata, *graphics_resource,
                    options.mode, family_overlay ? &*family_overlay : nullptr,
                    static_cast<std::uint8_t>(options.language));
                return true;
            };
#endif
            const auto show_notice=[&](const std::string& title,const std::string& message)->bool{
                    odh::DialogState notice;
                    notice.prompt.title=title;notice.prompt.message=message;
                    odh_host::OriginalUiArt art(game,{"Shared/Sys/WinBtn.bmp","CPanel/Backgrounds/PopupInfoTiles.bmp"});
                    odh_host::VmDialogAppearance appearance;
                    appearance.button_strip=&art.at("Shared/Sys/WinBtn.bmp");
                    appearance.panel_tiles=&art.at("CPanel/Backgrounds/PopupInfoTiles.bmp");
                    appearance.ok=lot_panel_accept;
                    auto frame=map->frame;
                    frame.pixels=window->readback();
                    (void)odh_host::draw_vm_dialog(frame,notice,static_cast<std::uint8_t>(options.language),0,appearance);
                    window->present(frame);
                    odh_host::CasPointer pointer;
                    for(;;){
                        const auto input=window->poll();
                        if(input.quit){
                            if(input.quit_reason!=odh_host::WindowQuitReason::escape_key)return false;
                            window->clear_escape_quit_reason();break;
                        }
                        if(input.confirm)break;
                        const auto layout=odh_host::vm_message_dialog_layout(frame.width,frame.height,notice,
                            static_cast<std::uint8_t>(options.language),appearance);
                        const auto button=[&](int x,int y)->std::optional<std::uint8_t>{
                            return layout?odh_host::vm_dialog_button_hit(*layout,0,x,y):std::nullopt;
                        };
                        const auto click=pointer.click_on(input,button);
                        if(click&&button((*click)[0],(*click)[1]))break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                return true;
            };
            std::optional<odh_host::MoveInFamilySelection> selected_for_move;
            bool reopen_family_picker=false;
            odh_host::NeighborhoodPortraits portrait_resources(game,userdata/"Characters",player_character_directory);
            const auto portraits=[&](std::uint32_t guid)->const odh::BitmapImage*{
                const auto unique=map->unique_neighbors.find(guid);
                if(unique==map->unique_neighbors.end()||!unique->second)return nullptr;
                if(!std::any_of(map->effective_families.begin(),map->effective_families.end(),[&](const auto& f){
                    return std::find(f.family.member_guids.begin(),f.family.member_guids.end(),guid)!=f.family.member_guids.end();
                }))return nullptr;
                return portrait_resources.get(guid);
            };
#ifdef ODH_HAVE_LINUX_SESSION_LOG
          if(session_log)session_log->stage("neighborhood-picker");
#endif
          for (;;) {
#ifdef ODH_HAVE_LINUX_CAS_SCREEN
            constexpr bool allow_create_family = true;
#else
            constexpr bool allow_create_family = false;
#endif
            const auto hover_text=[&](std::uint16_t hovered_house){
                odh_host::NeighborhoodHoverText text;
                if(!ui_string_catalog)return text;
                const auto language=static_cast<std::uint8_t>(options.language);
                const odh::FamilyContext families(map->effective_families);
                const auto* family=families.by_house(static_cast<std::int16_t>(hovered_house));
                if(!family)return text;
                text.address=neighborhood_lot_text(game,userdata,hovered_house,language,*ui_string_catalog).first;
                const auto name=family->display_name.empty()?ui_string_catalog->get(134,22,language,"No Name"):
                    family->display_name;
                const auto worth=static_cast<std::int64_t>(family->family.value_in_arch)+family->family.budget;
                text.family_summary=format_game_ui_string(ui_string_catalog->get(134,0,language,
                    "The %s Family\r\nNet Worth: %s\r\nFriends: %d"),
                    {name,"\xc2\xa7"+std::to_string(worth),std::to_string(family->family.family_friends)});
                return text;
            };
            const auto available_neighborhood=[&](unsigned number){
                if(number<1||number>8)return false;
                try{
                    const odh_host::NeighborhoodStorage scope(number);
                    const auto selected_data=require_directory(game,game/scope.user_data_name());
                    if(selected_data!=game/scope.user_data_name())return false;
                    (void)require_file(game,selected_data/"Neighborhood.iff");
                    (void)require_file(game,selected_data/"LotLocations.iff");
                    (void)require_directory(game,selected_data/"Characters");
                    return true;
                }catch(const odh::FormatError&){return false;}
            };
            const auto selected = reopen_family_picker?
                std::optional<odh_host::NeighborhoodPickerResult>{{odh_host::NeighborhoodPickerAction::select_family,0,0,0}}:
                odh_host::show_neighborhood_picker_with_actions(*window, *map,
                neighborhood_title, neighborhood_cancel,
                static_cast<std::uint8_t>(options.language), allow_create_family,
                ui_string_catalog?ui_string_catalog->get(129,9,static_cast<std::uint8_t>(options.language),"Create a family"):"Create a family",hover_text,player_character_directory,portraits,storage.number(),
                    available_neighborhood(storage.number()-1),available_neighborhood(storage.number()+1));
            reopen_family_picker=false;
            if (!selected) {
                if(selected_for_move&&window->quit_reason()==odh_host::WindowQuitReason::escape_key){
                    selected_for_move.reset();window->clear_escape_quit_reason();continue;
                }
                std::cout << "{\"status\":\"cancelled\",\"stage\":\"neighborhood\",\"cancellation_reason\":\""
                          << odh_host::window_quit_reason_name(window->quit_reason()) << "\"}\n";
                return false;
            }
            if (selected->action == odh_host::NeighborhoodPickerAction::switch_mode) {
                selected_for_move.reset();
                if (!odh::neighborhood_mode_supported(selected->mode))
                    throw odh::FormatError("neighborhood map switch requested an unsupported mode");
                options.mode = selected->mode;
                map = odh_host::prepare_neighborhood_map(userdata, graphics, options.mode,
                    family_overlay ? &*family_overlay : nullptr,
                    static_cast<std::uint8_t>(options.language));
                continue;
            }
#ifdef ODH_HAVE_LINUX_CAS_SCREEN
            if (selected->action == odh_host::NeighborhoodPickerAction::create_family) {
                if(!create_family())return false;
                portrait_resources.invalidate();selected_for_move.reset();reopen_family_picker=true;continue;
            }
#endif
            if(selected->action==odh_host::NeighborhoodPickerAction::switch_neighborhood){
                const int next=static_cast<int>(storage.number())+selected->neighborhood_delta;
                if((selected->neighborhood_delta!=-1&&selected->neighborhood_delta!=1)||next<1||next>8||
                   !available_neighborhood(static_cast<unsigned>(next)))throw odh::FormatError("neighborhood selection changed");
                storage=odh_host::NeighborhoodStorage(static_cast<unsigned>(next));
                userdata=require_directory(game,game/storage.user_data_name());
                if(userdata!=game/storage.user_data_name())throw odh::FormatError("neighborhood directory changed");
                options.neighborhood=storage.number();options.mode=1;
                player_character_directory=odh_host::linux_player_character_directory(storage);
                pending_move_in.reset();selected_for_move.reset();
                family_overlay=odh_host::read_linux_family_overlay_for_neighborhood(userdata/"Neighborhood.iff",storage);
                map=odh_host::prepare_neighborhood_map(userdata,graphics,options.mode,
                    family_overlay?&*family_overlay:nullptr,static_cast<std::uint8_t>(options.language));
                portrait_resources.reset(game,userdata/"Characters",player_character_directory);
#ifdef ODH_HAVE_LINUX_USER_CONFIG
                odh_host::LinuxUserConfig settings;
                settings.preferences=options.preferences;
                settings.language_code=static_cast<int>(options.language);settings.data_root=game;
                settings.neighborhood_number=storage.number();std::string settings_error;
                if(!odh_host::save_linux_user_config(settings,&settings_error))throw odh::FormatError(settings_error);
#endif
                continue;
            }
            const bool family_screen=selected->action==odh_host::NeighborhoodPickerAction::select_family;
            if(family_screen)selected_for_move.reset();
            house = family_screen?0:selected->house_number;
            unsigned selected_home_mode=options.mode;
            const auto marker = std::find_if(map->markers.begin(), map->markers.end(),
                [&](const auto& value) { return value.location.house == house; });
            if (!family_screen&&marker == map->markers.end())
                throw odh::FormatError("selected lot is absent from the prepared neighborhood");
            // A normal click enters an occupied home directly. Household
            // management is an explicit map tool, never an entry confirmation.
            if(!family_screen){
                if(options.mode==1&&map->visual_mode==4){
                    bool in_base=false;
                    with_iff_file(require_file(game,userdata/"LotLocations.iff"),[&](const odh::View& view){
                        bool found=false;
                        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
                            if(chunk.type!="STR#"||chunk.id!=1)return;
                            if(found)throw odh::FormatError("ambiguous base home locations");
                            found=true;const auto locations=odh::read_lot_locations(chunk.payload,1);
                            in_base=std::any_of(locations.lots.begin(),locations.lots.end(),[&](const auto& item){return item.house==house;});
                        });
                    });
                    if(!in_base)selected_home_mode=4;
                }
                std::optional<odh::LotZoning> zoning;
                with_iff_file(require_file(game,userdata/"LotZoning.iff"),[&](const odh::View& view){
                    odh::iff_chunks(view,[&](const odh::Chunk& chunk){
                        if(chunk.type!="STR#"||chunk.id!=1)return;
                        if(zoning)throw odh::FormatError("ambiguous neighborhood zoning");
                        zoning=odh::read_lot_zoning(odh::read_strings(chunk.payload));
                    });
                });
                const auto zone=zoning?zoning->find(static_cast<std::int16_t>(house)):odh::LotZoning::const_iterator{};
                if(!zoning)throw odh::FormatError("selected lot has no authenticated zoning table");
                const auto lot_use=odh_host::neighborhood_lot_use(static_cast<std::uint16_t>(house),
                    zone==zoning->end()?std::nullopt:std::optional<std::int16_t>{zone->second});
                if(lot_use!=odh_host::NeighborhoodLotUse::residential){
                    if(selected_for_move){
                        if(!show_notice(options.language==14?"Lote comunitário":"Community lot",
                            options.language==14?"Escolha um lote residencial para mudar sua família.":
                            "Choose a residential lot to move your family in."))return false;
                        continue;
                    }
                    const auto edit=community_lot_editor_options(community_lot_store_options(
                        game,userdata,storage,selected_home_mode,house),options.language,player_character_directory);
                    const auto editor_result=odh_host::show_community_lot_editor(*window,edit,[&](const std::string& event){
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                        if(session_log)session_log->event(event);
#else
                        (void)event;
#endif
                    });
                    if(editor_result.exit_application||window->quit_reason()!=odh_host::WindowQuitReason::none)return false;
                    continue;
                }

            }
            const bool occupied=!family_screen&&marker->occupied;
            if(occupied&&selected->action==odh_host::NeighborhoodPickerAction::lot){
                if(selected_for_move)continue;
                options.mode=selected_home_mode;break;
            }
            const std::int32_t selected_lot_price=family_screen?0:occupied?-1:
                current_house_purchase_price(static_cast<std::uint16_t>(house),family_overlay);
            if (occupied) {
                const odh::FamilyContext family_context(map->effective_families);
                const auto* current_family = family_context.by_house(static_cast<std::int16_t>(house));
                if (!current_family)
                    throw odh::FormatError("occupied lot has no matching family record");
                const auto neighborhood_identity = odh_host::linux_neighborhood_identity(
                    userdata / "Neighborhood.iff");
                bool can_move_out = false;
                try {
                    const auto replay = family_overlay ? std::optional<odh::FamilyOverlayReplay>{
                        odh::replay_family_overlay(map->source_families, neighborhood_identity, *family_overlay)} : std::nullopt;
                    const auto managed = replay && std::any_of(replay->states.begin(), replay->states.end(),
                        [&](const auto& state) { return state.family_chunk_id == current_family->chunk_id && state.managed; });
                    if (managed) {
                        odh_host::FamilySessionJournal journal(map->source_families, neighborhood_identity,
                            family_overlay, static_cast<std::uint16_t>(house), current_family->chunk_id,std::nullopt,storage);
                        if (!journal.checkpoint_path() || !prepare_lot_transfer(
                                {neighborhood_identity,map->source_families}, *family_overlay,
                                current_family->chunk_id, static_cast<std::uint16_t>(house),selected_home_mode))
                            throw odh::FormatError("save the current household before moving it out");
                        (void)odh::plan_family_managed_move_out_overlay(map->source_families,
                            neighborhood_identity, &*family_overlay, current_family->chunk_id);
                    } else (void)odh::plan_family_move_out_overlay(map->source_families,
                            neighborhood_identity, family_overlay ? &*family_overlay : nullptr,
                            current_family->chunk_id);
                    can_move_out = true;
                } catch (const odh::FormatError&) {
                    // Families without a verified source refund can still enter
                    // their house; the move-out option remains unavailable.
                }
                // The original bulldozer tool opens the eviction question
                // directly for an occupied home; normal clicks already enter.
                if (!can_move_out) {
                    if (!show_notice(move_out_title, options.language==14 ?
                            "Não foi possível confirmar os dados desta família para a mudança." :
                            "This family's move-out data could not be verified.")) return false;
                    continue;
                }
                {
                    if (!odh_host::show_move_out_confirmation(*window, *current_family,
                            static_cast<std::uint16_t>(house),
                            static_cast<std::uint8_t>(options.language), move_out_title,
                            confirm_yes, confirm_no, graphics,
                            format_game_ui_string(move_out_question,
                                {current_family->display_name.empty() ?
                                     ui_string_catalog->get(134,22,static_cast<std::uint8_t>(options.language),"No Name") :
                                     current_family->display_name,
                                 "§" + std::to_string(static_cast<std::int64_t>(current_family->family.budget) +
                                                        current_family->family.value_in_arch)}))) {
                        std::cout << "{\"status\":\"cancelled\",\"stage\":\"move-out-confirmation\"}\n";
                        continue;
                    }
                    const auto latest_overlay = odh_host::read_linux_family_overlay_for_neighborhood(
                        userdata / "Neighborhood.iff",storage);
                    if (latest_overlay.has_value() != family_overlay.has_value() ||
                        (latest_overlay && odh::encode_family_overlay(*latest_overlay) != odh::encode_family_overlay(*family_overlay)))
                        throw odh::FormatError("player state changed during move-out confirmation");
                    const auto latest_map = odh_host::prepare_neighborhood_map(userdata, graphics,
                        options.mode, latest_overlay ? &*latest_overlay : nullptr,
                        static_cast<std::uint8_t>(options.language));
                    const auto latest_neighborhood_identity = odh_host::linux_neighborhood_identity(
                        userdata / "Neighborhood.iff");
                    if (latest_neighborhood_identity != neighborhood_identity)
                        throw odh::FormatError("Neighborhood.iff changed during move-out confirmation");
                    const auto latest_marker = std::find_if(latest_map.markers.begin(),
                        latest_map.markers.end(), [&](const auto& value) {
                            return value.location.house == house;
                        });
                    if (latest_marker == latest_map.markers.end() || !latest_marker->occupied)
                        throw odh::FormatError("selected family no longer occupies this house");
                    const odh::FamilyContext latest_context(latest_map.effective_families);
                    const auto* latest_family = latest_context.by_house(
                        static_cast<std::int16_t>(house));
                    if (!latest_family || latest_family->chunk_id != current_family->chunk_id ||
                        odh::family_overlay_identity(*latest_family) !=
                            odh::family_overlay_identity(*current_family))
                        throw odh::FormatError("selected family changed before move-out was saved");
                    bool managed = false;
                    if (latest_overlay) {
                        const auto replay = odh::replay_family_overlay(latest_map.source_families,
                            latest_neighborhood_identity, *latest_overlay);
                        managed = std::any_of(replay.states.begin(), replay.states.end(),
                            [&](const auto& state) { return state.family_chunk_id == latest_family->chunk_id && state.managed; });
                    }
                    if (managed) {
                        odh_host::FamilySessionJournal journal(latest_map.source_families, latest_neighborhood_identity,
                            latest_overlay, static_cast<std::uint16_t>(house), latest_family->chunk_id,std::nullopt,storage);
                        if (!journal.checkpoint_path() || !prepare_lot_transfer(
                                {latest_neighborhood_identity,latest_map.source_families}, *latest_overlay,
                                latest_family->chunk_id, static_cast<std::uint16_t>(house),selected_home_mode))
                            throw odh::FormatError("managed move-out requires a current transferable checkpoint");
                    }
                    if (managed) {
                        // Confirmation is complete, but no occupancy/refund has been
                        // published. Reuse the same isolated restored runtime and
                        // typed property release as the CLI, including real cleanup.
                        odh_host::FamilySessionJournal journal(latest_map.source_families, latest_neighborhood_identity,
                            latest_overlay, static_cast<std::uint16_t>(house), latest_family->chunk_id,std::nullopt,storage);
                        const auto checkpoint=journal.checkpoint_path();
                        if(!checkpoint||gui_property_release)
                            throw odh::FormatError("GUI property release requires one current checkpoint and no pending release");
                        journal.verify_current();
                        gui_property_release=GuiPropertyReleaseRequest{options,*latest_overlay,latest_neighborhood_identity,
                            latest_family->chunk_id,static_cast<std::uint16_t>(house),*checkpoint};
                        Options release_options;
                        release_options.game_root=game;release_options.mode=selected_home_mode;
                        release_options.neighborhood=storage.number();release_options.language=options.language;
                        release_options.preferences=options.preferences;release_options.lot_window_size=options.lot_window_size;
                        release_options.headless_ticks=0;release_options.audio_disabled=true;
                        release_options.house=house;release_options.load_session=*checkpoint;
                        release_options.headless_move_out_family=latest_family->chunk_id;
                        options=std::move(release_options);
                        return true;
                    }
                    const auto next_overlay = odh::plan_family_move_out_overlay(latest_map.source_families, latest_neighborhood_identity,
                            latest_overlay ? &*latest_overlay : nullptr, latest_family->chunk_id);
                    odh_host::compare_exchange_linux_family_overlay_atomic(
                        odh_host::linux_family_overlay_path(latest_neighborhood_identity,storage),
                        latest_overlay, next_overlay);
                    family_overlay = next_overlay;
                    map = odh_host::prepare_neighborhood_map(userdata, graphics, options.mode,
                        &*family_overlay, static_cast<std::uint8_t>(options.language));
                    const auto released_marker = std::find_if(map->markers.begin(),
                        map->markers.end(), [&](const auto& value) {
                            return value.location.house == house;
                        });
                    if (released_marker == map->markers.end() || released_marker->occupied)
                        throw odh::FormatError("saved move-out did not release the selected house");
                    continue;
                }
                options.mode=selected_home_mode;break;
            }
            if (!occupied) {
                const auto purchase_price = selected_lot_price;
                std::optional<odh::Nbrs> picker_neighbors;
                with_iff_file(userdata / "Neighborhood.iff", [&](const auto& view) {
                    odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
                        if (chunk.type != "NBRS" || chunk.id != 1) return;
                        if (picker_neighbors) throw odh::FormatError("ambiguous NBRS1 neighbor table");
                        picker_neighbors = odh::read_nbrs(chunk.payload);
                    });
                });
                const auto picker_neighborhood_identity = odh_host::linux_neighborhood_identity(
                    userdata / "Neighborhood.iff");
                std::vector<std::uint16_t> deletable_family_ids;
                if (picker_neighbors) for (const auto& visible : map->effective_families) {
                    if ((static_cast<std::uint32_t>(visible.family.house_number) & 0xffffU) != 0 ||
                        visible.family.member_guids.empty()) continue;
                    try {
                        const bool player_created = family_overlay && std::any_of(
                            family_overlay->creations.begin(), family_overlay->creations.end(),
                            [&](const auto& creation) { return creation.family_chunk_id == visible.chunk_id; });
                        if (player_created)
                            (void)odh::plan_family_archive_overlay(map->source_families,
                                *picker_neighbors, picker_neighborhood_identity,
                                family_overlay ? &*family_overlay : nullptr, visible.chunk_id);
                        else
                            (void)odh::plan_family_delete_overlay(map->source_families,
                                *picker_neighbors, picker_neighborhood_identity,
                                family_overlay ? &*family_overlay : nullptr, visible.chunk_id);
                        deletable_family_ids.push_back(visible.chunk_id);
                    } catch (const odh::FormatError&) {
                        // Keep the action unavailable when source ownership or roster links are uncertain.
                    }
                }
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                if(session_log)session_log->stage("move-in");
#endif
                std::optional<odh_host::MoveInFamilySelection> family_choice;
                for (;;) {
                    if(!family_screen&&selected_for_move)family_choice=selected_for_move;
                    else family_choice = odh_host::show_move_in_family_picker(*window,
                        map->effective_families, static_cast<std::uint16_t>(house), purchase_price,
                        static_cast<std::uint8_t>(options.language), family_picker_action,
                        family_picker_title, no_family_title, no_family_message, family_picker_cancel,
                        graphics, family_picker_net_worth, family_picker_friends,
                        family_picker_archive, deletable_family_ids,portraits);
                    if (!family_choice || family_choice->action != odh_host::MoveInFamilySelection::Action::rename)
                        break;
                    const auto selected_family = std::find_if(map->effective_families.begin(),
                        map->effective_families.end(), [&](const auto& entry) {
                            return entry.chunk_id == family_choice->family_chunk_id;
                        });
                    if (selected_family == map->effective_families.end() ||
                        odh::family_overlay_identity(*selected_family) != family_choice->family_identity)
                        throw odh::FormatError("selected family changed before name editing");
#ifdef ODH_HAVE_LINUX_PRIVATE_FAMILY
                    if (!rename_family(*selected_family)) return false;
#else
                    throw odh::FormatError("private family naming service is unavailable");
#endif
                }
                if (!family_choice) {
                    const auto reason = window->quit_reason();
                    if (reason == odh_host::WindowQuitReason::window_manager_close ||
                        reason == odh_host::WindowQuitReason::window_destroyed) {
                        std::cout << "{\"status\":\"cancelled\",\"stage\":\"family\",\"cancellation_reason\":\"window_close\"}\n";
                        return false;
                    }
                    continue;
                }
                if(family_choice->action==odh_host::MoveInFamilySelection::Action::create_family){
#ifdef ODH_HAVE_LINUX_CAS_SCREEN
                    if(!create_family())return false;
                portrait_resources.invalidate();selected_for_move.reset();reopen_family_picker=true;continue;
#else
                    throw odh::FormatError("family creation is unavailable");
#endif
                }
                const auto chosen_family = std::find_if(map->effective_families.begin(),
                    map->effective_families.end(), [&](const auto& entry) {
                        return entry.chunk_id == family_choice->family_chunk_id;
                    });
                if (chosen_family == map->effective_families.end() ||
                    odh::family_overlay_identity(*chosen_family) != family_choice->family_identity)
                    throw odh::FormatError("selected family changed before move-in confirmation");
                if (family_choice->action == odh_host::MoveInFamilySelection::Action::archive) {
                    const auto family_name = chosen_family->display_name.empty() ?
                        ui_string_catalog->get(134, 22,
                            static_cast<std::uint8_t>(options.language), "No Name") :
                        chosen_family->display_name;
                    const auto archive_question = format_game_ui_string(
                        family_archive_question, {family_name});
                    if (!odh_host::show_family_archive_confirmation(*window, *chosen_family,
                            static_cast<std::uint8_t>(options.language), family_archive_title,
                            archive_question, confirm_yes, confirm_no, graphics)) {
                        window->clear_escape_quit_reason();
                        continue;
                    }

                    const auto latest_overlay = odh_host::read_linux_family_overlay_for_neighborhood(
                        userdata / "Neighborhood.iff",storage);
                    const auto latest_map = odh_host::prepare_neighborhood_map(userdata, graphics,
                        options.mode, latest_overlay ? &*latest_overlay : nullptr,
                        static_cast<std::uint8_t>(options.language));
                    const auto neighborhood_identity = odh_host::linux_neighborhood_identity(
                        userdata / "Neighborhood.iff");
                    const auto latest_family = std::find_if(latest_map.effective_families.begin(),
                        latest_map.effective_families.end(), [&](const auto& entry) {
                            return entry.chunk_id == family_choice->family_chunk_id;
                        });
                    if (latest_family == latest_map.effective_families.end() ||
                        odh::family_overlay_identity(*latest_family) != family_choice->family_identity ||
                        (static_cast<std::uint32_t>(latest_family->family.house_number) & 0xffffU) != 0)
                        throw odh::FormatError("selected family changed before archive was saved");
                    std::optional<odh::Nbrs> latest_neighbors;
                    with_iff_file(userdata / "Neighborhood.iff", [&](const auto& view) {
                        odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
                            if (chunk.type != "NBRS" || chunk.id != 1) return;
                            if (latest_neighbors)
                                throw odh::FormatError("ambiguous NBRS1 neighbor table");
                            latest_neighbors = odh::read_nbrs(chunk.payload);
                        });
                    });
                    if (!latest_neighbors)
                        throw odh::FormatError("Neighborhood.iff has no readable NBRS1 table");
                    const bool player_created = latest_overlay && std::any_of(
                        latest_overlay->creations.begin(), latest_overlay->creations.end(),
                        [&](const auto& creation) {
                            return creation.family_chunk_id == family_choice->family_chunk_id;
                        });
                    const auto next_overlay = player_created ?
                        odh::plan_family_archive_overlay(latest_map.source_families,
                            *latest_neighbors, neighborhood_identity,
                            latest_overlay ? &*latest_overlay : nullptr,
                            family_choice->family_chunk_id) :
                        odh::plan_family_delete_overlay(latest_map.source_families,
                            *latest_neighbors, neighborhood_identity,
                            latest_overlay ? &*latest_overlay : nullptr,
                            family_choice->family_chunk_id);
                    odh_host::compare_exchange_linux_family_overlay_atomic(
                        odh_host::linux_family_overlay_path(neighborhood_identity,storage),
                        latest_overlay, next_overlay);
                    family_overlay = next_overlay;
                    map = odh_host::prepare_neighborhood_map(userdata, graphics, options.mode,
                        &*family_overlay, static_cast<std::uint8_t>(options.language));
                    if (std::any_of(map->effective_families.begin(), map->effective_families.end(),
                        [&](const auto& entry) { return entry.chunk_id == family_choice->family_chunk_id; }))
                        throw odh::FormatError("archived family remained visible after map refresh");
                    continue;
                }
                if(family_screen){selected_for_move=family_choice;continue;}
                if(chosen_family->family.budget<purchase_price){
                    if(!show_notice(no_family_title,no_family_message))return false;
                    continue;
                }
                if (!odh_host::show_move_in_confirmation(*window, *chosen_family,
                        static_cast<std::uint16_t>(house), purchase_price,
                        static_cast<std::uint8_t>(options.language), move_in_title,
                        move_in_question, confirm_yes, confirm_no, graphics)) {
                    std::cout << "{\"status\":\"cancelled\",\"stage\":\"move-in-confirmation\"}\n";
                    continue;
                }

                // Re-read both player state and commercial inputs after the
                // confirmation, so stale selections cannot silently spend money.
                const auto latest_overlay = odh_host::read_linux_family_overlay_for_neighborhood(
                    userdata / "Neighborhood.iff",storage);
                const auto latest_map = odh_host::prepare_neighborhood_map(userdata, graphics,
                    options.mode, latest_overlay ? &*latest_overlay : nullptr,
                    static_cast<std::uint8_t>(options.language));
                const auto latest_price = current_house_purchase_price(static_cast<std::uint16_t>(house),latest_overlay);
                if(selected_home_mode==1||selected_home_mode==4){
                    std::optional<odh::LotZoning> zoning;
                    with_iff_file(require_file(game,userdata/"LotZoning.iff"),[&](const odh::View& view){
                        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
                            if(chunk.type!="STR#"||chunk.id!=1)return;
                            if(zoning)throw odh::FormatError("ambiguous move-in zoning");
                            zoning=odh::read_lot_zoning(odh::read_strings(chunk.payload));
                        });
                    });
                    if(!zoning||!zoning->count(static_cast<std::int16_t>(house))||zoning->at(static_cast<std::int16_t>(house))!=0)
                        throw odh::FormatError("selected lot is no longer residential");
                }
                if (latest_price != purchase_price)
                    throw odh::FormatError("selected lot price changed before move-in was saved");
                const auto latest_marker = std::find_if(latest_map.markers.begin(),
                    latest_map.markers.end(), [&](const auto& value) { return value.location.house == house; });
                if (latest_marker == latest_map.markers.end() || latest_marker->occupied)
                    throw odh::FormatError("selected lot is no longer vacant");
                const auto latest_family = std::find_if(latest_map.effective_families.begin(),
                    latest_map.effective_families.end(), [&](const auto& entry) {
                        return entry.chunk_id == family_choice->family_chunk_id;
                    });
                if (latest_family == latest_map.effective_families.end() ||
                    odh::family_overlay_identity(*latest_family) != family_choice->family_identity ||
                    (static_cast<std::uint32_t>(latest_family->family.house_number) & 0xffffU) != 0)
                    throw odh::FormatError("selected family is no longer available for move-in");

                const auto neighborhood_identity = odh_host::linux_neighborhood_identity(
                    userdata / "Neighborhood.iff");
                const auto next_overlay = odh::plan_family_managed_move_in_overlay(
                    latest_map.source_families, neighborhood_identity,
                    latest_overlay ? &*latest_overlay : nullptr, family_choice->family_chunk_id,
                    static_cast<std::uint16_t>(house), latest_price);
                // Charge only after the destination runtime and any portable
                // household transfer have both been prepared successfully.
                if (options.single_resident_session)
                    throw odh::FormatError("purchases require the complete household runtime");
                (void)prepare_lot_transfer({neighborhood_identity,latest_map.source_families},
                    next_overlay, family_choice->family_chunk_id, static_cast<std::uint16_t>(house),selected_home_mode);
                options.mode=selected_home_mode;selected_for_move.reset();
                pending_move_in = std::make_shared<odh_host::FamilySessionJournal>(latest_map.source_families,
                    neighborhood_identity, latest_overlay, static_cast<std::uint16_t>(house),
                    family_choice->family_chunk_id, next_overlay,storage);
                family_overlay = next_overlay;
                map = odh_host::prepare_neighborhood_map(userdata, graphics, options.mode,
                    &*family_overlay, static_cast<std::uint8_t>(options.language));
                const auto committed_marker = std::find_if(map->markers.begin(), map->markers.end(),
                    [&](const auto& value) { return value.location.house == house; });
                if (committed_marker == map->markers.end() || !committed_marker->occupied)
                    throw odh::FormatError("saved move-in did not appear on the refreshed neighborhood map");
            }
            break;
          }
            return true;
        };
        if (map && !options.house && !run_management_map()) return 0;
#endif

        // Only small launch options and archive paths survive each runtime. Every
        // destination rebuilds its session, resource plan and runtime from scratch.
        Options travel_options;
        travel_options.game_root = game;
        travel_options.mode = options.mode;
        travel_options.neighborhood=storage.number();
        travel_options.language = options.language;
        travel_options.preferences=options.preferences;
        travel_options.lot_window_size=options.lot_window_size;
        travel_options.single_resident_session = options.single_resident_session;
        travel_options.instruction_budget = options.instruction_budget;
        travel_options.audio_enabled=options.audio_enabled;
        travel_options.audio_disabled=options.audio_disabled;
        travel_options.audio_drain_ms=options.audio_drain_ms;
        travel_options.headless_travel_tick=options.headless_travel_tick;
        std::map<unsigned, std::filesystem::path> saved_lots;
        std::uint8_t presentation_speed=1; // Survives runtime reloads, separate from saves and Options rollback.
        std::optional<Options> rollback_options;
        std::size_t headless_travel_index = 0;
        travel_options.headless_ticks = options.headless_ticks;
        travel_options.headless_travel_houses = options.headless_travel_houses;
        travel_options.save_on_exit = options.save_on_exit;
#ifdef __linux__
#ifdef ODH_HAVE_TS1_TRAVEL_CATALOG
        constexpr bool temporary_travel_host_ready=true;
#else
        constexpr bool temporary_travel_host_ready=false;
#endif
#ifdef ODH_TEST_PINNED_TRAVEL_LIFECYCLE
        constexpr bool all_travel_destinations=true;
#else
        constexpr bool all_travel_destinations=false;
#endif
        struct TemporaryTravelLoop {
            odh::Ts1TravelState origin,state;
            std::vector<std::uint8_t> checkpoint,suspended_home,departure;
            std::filesystem::path home_path;
            std::optional<odh_host::TravelCheckpointDigest> head;
            std::shared_ptr<odh_host::FamilySessionJournal> journal;
            std::shared_ptr<odh_host::PrivateVacationSession> private_vacation;
            std::shared_ptr<const odh::PreparedTs1TravelReturn> returning;
            bool arrival=false;
        };
        std::optional<TemporaryTravelLoop> temporary_trip;
        const auto fresh_origin=[&](const odh::Ts1TravelState& claim,
                const std::shared_ptr<odh_host::FamilySessionJournal>& journal){
            const auto overlay=journal?journal->runtime_overlay():odh_host::read_linux_family_overlay_for_neighborhood(userdata/"Neighborhood.iff",storage);
            if(claim.family_roster_revision){
                if(!journal||!overlay||!overlay->character_family)throw odh::FormatError("TRIP4 lacks current managed character/family authority");
                journal->verify_current();const auto lease=journal->lease();
                if(lease.family_chunk_id!=claim.family_chunk_id||lease.occupancy_epoch!=claim.occupancy_epoch||
                   lease.family_identity!=claim.family_identity||lease.neighborhood_identity!=claim.neighborhood_identity)
                    throw odh::FormatError("TRIP4 permanent lease changed");
                const auto* row=odh::find_ts1_family_roster(*overlay->character_family,claim.family_chunk_id);
                if(!row||row->revision!=*claim.family_roster_revision)throw odh::FormatError("TRIP4 current FAMI revision changed");
                auto checked=claim;odh::synchronize_ts1_travel_family_roster(checked,*overlay->character_family,claim.permanent_roster);
                const auto home=odh::load_session_entry(userdata,claim.home_mode,claim.home_house,
                    static_cast<std::uint8_t>(options.language),&*overlay,player_character_directory,nullptr,true);
                if(home.family_chunk_id!=claim.family_chunk_id||home.family_number!=claim.family_number)
                    throw odh::FormatError("TRIP4 home assignment changed");
                std::optional<odh::Nbrs> raw;
                with_iff_file((userdata/"Neighborhood.iff").string(),[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
                    if(chunk.type=="NBRS"&&chunk.id==1){if(raw)throw odh::FormatError("ambiguous travel NBRS1");raw=odh::read_nbrs(chunk.payload);}
                });});
                if(!raw)throw odh::FormatError("TRIP4 immutable neighbor source missing");
                const auto owned=odh::family_overlay_owned_neighbor_seeds(*overlay);
                std::set<std::int16_t> retired;for(const auto& member:odh::retired_family_members(*overlay))retired.insert(member.neighbor_id);
                const auto* catalog=overlay->personal_state&&overlay->personal_state->catalog?&*overlay->personal_state->catalog:nullptr;
                const odh::NeighborContext neighbors(catalog?odh::hydrate_owned_native_personal_neighbors(*raw,*catalog,owned,retired):odh::merge_owned_neighbor_seeds(*raw,owned));
                std::uint64_t remaining=64u*1024u*1024u;
                std::set<std::uint32_t> verified;
                const auto verify=[&](const odh::Ts1TravelMember& member){
                    if(!verified.insert(member.guid).second)return;
                    const auto* neighbor=neighbors.by_id(member.neighbor_id);
                    if(!neighbor||neighbor->guid!=member.guid||retired.count(member.neighbor_id))throw odh::FormatError("TRIP4 neighbor identity changed or retired");
                    const auto locator=odh::native_personal_resource_locator(member.character_asset);
                    std::filesystem::path path;
                    if(locator.character_catalog){
                        const std::filesystem::path logical(locator.source);const auto parent=logical.parent_path().generic_string();
                        const auto root=parent=="UserData/Characters"?userdata/"Characters":parent=="PlayerData/Characters"?player_character_directory:std::filesystem::path{};
                        if(root.empty()||std::filesystem::is_symlink(root/logical.filename()))throw odh::FormatError("TRIP4 character catalog path is invalid");
                        path=std::filesystem::canonical(root/logical.filename());
                        if(path.parent_path()!=std::filesystem::canonical(root))throw odh::FormatError("TRIP4 character escaped its catalog");
                    }else path=require_file(game,game/locator.source);
                    const auto inspect=[&](const auto& view){
                        if(view.size()>remaining||view.size()>16u*1024u*1024u)throw odh::FormatError("TRIP4 resource verification exceeds bounds");remaining-=view.size();
                        std::string bytes(static_cast<std::size_t>(view.size()),'\0');view.read(0,bytes.data(),bytes.size());
                        if(odh::sha256(bytes.data(),bytes.size())!=member.character_fingerprint)throw odh::FormatError("TRIP4 resource bytes changed");
                        unsigned found=0;odh::iff_chunks(view,[&](const auto& chunk){if(chunk.type=="OBJD"&&chunk.id==member.definition_chunk_id){
                            const auto definition=odh::read_object_definition(chunk.payload);
                            if(definition.guid!=member.guid||definition.object_type!=2)throw odh::FormatError("TRIP4 OBJD identity changed");++found;
                        }});if(found!=1)throw odh::FormatError("TRIP4 OBJD missing or ambiguous");
                    };
                    if(locator.asset.empty())with_iff_file(path.string(),inspect);else with_far_entry(path.string(),locator.asset,inspect);
                };
                for(const auto& member:claim.permanent_roster)verify(member);
                for(const auto& member:claim.travelers)verify(member);
                auto origin=claim;origin.current_house=origin.home_house;origin.current_mode=origin.home_mode;
                origin.pending.reset();origin.family_activated=false;origin.transit_info=0;
                return origin;
            }
            const auto entry=odh::load_session_entry(userdata,claim.home_mode,claim.home_house,
                static_cast<std::uint8_t>(options.language),overlay?&*overlay:nullptr,player_character_directory);
            odh_host::IntegratedSessionContext context;context.family_journal=journal;
            context.game_root=game;context.neighborhood_number=storage.number();context.installed_user_data=userdata;
            context.house_number=entry.house_number;context.neighborhood_mode=static_cast<std::uint8_t>(entry.neighborhood_mode);
            context.family_chunk_id=entry.family_chunk_id;context.family_number=entry.family_number;
            const auto caller=std::find_if(entry.members.begin(),entry.members.end(),[&](const auto& m){return m.guid==claim.caller_guid&&m.neighbor_id==claim.caller_neighbor_id;});
            if(caller==entry.members.end())throw odh::FormatError("temporary travel caller is no longer in the permanent household");
            context.resident_guid=caller->guid;context.resident_neighbor_id=caller->neighbor_id;
            for(const auto& member:entry.members){std::filesystem::path character;
                if(member.character_source=="UserData/Characters")character=require_file(game,userdata/"Characters"/member.character_asset);
                else if(member.character_source=="PlayerData/Characters")character=std::filesystem::canonical(player_character_directory/member.character_asset);
                else throw odh::FormatError("temporary travel origin has unknown character resource source");
                context.household_members.push_back({character,member.guid,member.neighbor_id,member.object_definition_id,member.character_source+"/"+member.character_asset});}
            auto origin=identify_travel_origin(context,userdata/"Neighborhood.iff");
            origin.protocol=claim.protocol;
            if(!same_travel_origin(origin,claim))throw odh::FormatError("temporary travel permanent home or resource roster changed");return origin;
        };
        const auto authenticate_trip=[&](const std::vector<std::uint8_t>& bytes,
                const std::shared_ptr<odh_host::FamilySessionJournal>& journal){
            if(journal)journal->validate_restore(bytes); // Authenticate residential head before examining visited payload.
            const auto outer=odh::decode_session_bundle(bytes);if(!outer.travel)throw odh::FormatError("temporary travel checkpoint has no required TRIP authority");
            TemporaryTravelLoop trip;trip.origin=fresh_origin(*outer.travel,journal);trip.journal=journal;
            const auto digest=odh::sha256(bytes.data(),bytes.size());
            odh_host::TravelSessionCheckpoint checkpoint;
            std::optional<std::filesystem::path> legacy_trip;
            if(journal){journal->validate_restore(bytes);trip.home_path=*journal->checkpoint_path();checkpoint=odh_host::reopen_travel_session_checkpoint(*journal,bytes);}
            else {
                trip.home_path=permanent_travel_checkpoint_path(game,trip.origin.neighborhood_identity,
                    trip.origin.home_mode,trip.origin.home_house,trip.origin.family_number,storage);
                if(storage.number()==1&&!std::filesystem::exists(trip.home_path))
                    legacy_trip=trip.home_path.parent_path().parent_path()/trip.home_path.filename();
                const auto source=odh::load_neighborhood_family_source(userdata/"Neighborhood.iff");
                const auto overlay=odh_host::read_linux_family_overlay_for_neighborhood(userdata/"Neighborhood.iff",storage);
                if(overlay&&overlay->family_vacation&&odh::family_vacation_private_head(*overlay->family_vacation,trip.origin.family_chunk_id)){
                    trip.private_vacation=std::make_shared<odh_host::PrivateVacationSession>(source.families,source.identity,overlay,
                        trip.origin.family_chunk_id,trip.origin.home_house,trip.origin.home_mode,std::nullopt,std::nullopt,storage);
                    trip.private_vacation->validate_restore(bytes);
                    trip.home_path=*trip.private_vacation->checkpoint_path();legacy_trip.reset();
                    const auto saved=odh::decode_session_bundle(bytes);
                    checkpoint=saved.travel_visited_session?odh_host::reopen_travel_session_envelope(saved):
                        odh_host::TravelSessionCheckpoint{bytes,saved};
                }else checkpoint=odh_host::reopen_private_travel_session_checkpoint(legacy_trip.value_or(trip.home_path),digest,trip.origin,{},storage);
            }
            trip.head=digest;trip.checkpoint=bytes;trip.state=*checkpoint.active.travel;trip.suspended_home=std::move(checkpoint.suspended_home);
#ifdef ODH_HAVE_TS1_TRAVEL_CATALOG
            odh_host::validate_ts1_travel_source(game,userdata,trip.state,
                static_cast<std::uint8_t>(options.language),all_travel_destinations);
#else
            throw odh::FormatError("temporary travel resource catalog is unavailable in this build");
#endif
            // Resource ownership of the suspended home is checked independently
            // of the visited lot's manifest and before any VM is constructed.
            const auto home_bundle=odh::decode_session_bundle(trip.suspended_home);
            const auto overlay=journal?journal->runtime_overlay():odh_host::read_linux_family_overlay_for_neighborhood(userdata/"Neighborhood.iff",storage);
            auto home_resources=home_bundle;
            if(outer.character_family)odh::project_session_character_family(home_resources,*outer.character_family);
            const auto home_entry=odh::load_session_entry(userdata,trip.origin.home_mode,trip.origin.home_house,static_cast<std::uint8_t>(options.language),overlay?&*overlay:nullptr,player_character_directory,&home_resources);
            if(trip.origin.family_roster_revision){
                if(home_bundle.unanchored_family_lot||home_bundle.npc_primary){
                    // The accepted source TRIP claim owns its caller/party. A
                    // suspended HOME's lexical metadata resource or HOST0 does
                    // not select a caller, revive a retired body or clear party.
                    if(!checkpoint.active.travel||
                       odh::encode_ts1_travel_state(*checkpoint.active.travel)!=odh::encode_ts1_travel_state(*outer.travel))
                        throw odh::FormatError("metadata HOME source TRIP claim differs from authenticated envelope");
                }else{
                    // Preserve legacy HHRD HOME reconstruction. Only that
                    // profile has a genuine HOST resident and permanent graph.
                    trip.origin.travelers.clear();for(const auto& member:home_bundle.household_roster)
                        trip.origin.travelers.push_back({member.guid,member.neighbor_id,member.object_definition_id,member.character_asset,member.character_fingerprint});
                    trip.origin.caller_guid=home_bundle.host.resident_guid;trip.origin.caller_neighbor_id=home_bundle.host.resident_neighbor_id;
                }
                odh::validate_ts1_travel_state(trip.origin);
            }
            const auto selected_member=std::find_if(home_entry.members.begin(),home_entry.members.end(),[&](const auto& m){return m.guid==home_bundle.host.resident_guid&&m.neighbor_id==home_bundle.host.resident_neighbor_id;});
            const odh::SessionEntryMember* selected=home_entry.metadata_bootstrap?&home_entry.metadata_bootstrap->resource:
                home_entry.npc_primary?&home_entry.npc_primary->resource:
                selected_member!=home_entry.members.end()?&*selected_member:nullptr;
            if(!selected)throw odh::FormatError("suspended home has no exact resident or historical metadata resource");
            auto home_options=travel_options;home_options.mode=trip.origin.home_mode;home_options.house=trip.origin.home_house;
            const auto home_plan=build_runtime_arguments(home_options,game,trip.origin.home_house,home_entry,*selected,graphics_resource,player_character_directory,userdata,overlay?&*overlay:nullptr);
            const auto identity=odh_host::identify_session_resources(game,home_plan.resources,static_cast<std::uint8_t>(options.language),home_plan.expansion_mask);
            if(odh::encode_session_resource_identity(home_bundle.resources)!=odh::encode_session_resource_identity(identity))throw odh::FormatError("suspended home resource manifest changed during temporary travel");
            // A return-pending or onward request can skip ordinary active-lot
            // restore. Authenticate the SOURCE visited manifest and actual
            // subset here as well, before permitting that immediate transition.
            if(trip.state.current_house!=trip.origin.home_house){
                const auto& active=checkpoint.active;
                std::vector<std::uint32_t> travelers;for(const auto& member:trip.state.travelers)travelers.push_back(member.guid);
                const auto source_entry=odh::load_visit_session_entry(userdata,trip.origin.home_mode,trip.origin.home_house,
                    trip.state.current_mode,trip.state.current_house,travelers,static_cast<std::uint8_t>(options.language),overlay?&*overlay:nullptr,player_character_directory,&active);
                const auto source_member=std::find_if(source_entry.members.begin(),source_entry.members.end(),[&](const auto& member){return member.guid==active.host.resident_guid&&member.neighbor_id==active.host.resident_neighbor_id;});
                const auto* source_resident=source_entry.metadata_bootstrap?&source_entry.metadata_bootstrap->resource:
                    source_entry.npc_primary?&source_entry.npc_primary->resource:source_member!=source_entry.members.end()?&*source_member:nullptr;
                if(!source_resident||active.household_roster.size()!=source_entry.members.size()||active.resources.language_code!=options.language||active.host.neighborhood_mode!=trip.state.current_mode||active.host.house_number!=trip.state.current_house||active.host.family_chunk_id!=source_entry.family_chunk_id||active.host.family_number!=source_entry.family_number)
                    throw odh::FormatError("source visited checkpoint no longer matches its originating family, selected traveler, language or lot");
                // This helper resolves and fingerprints resources only; no
                // household transfer, occupancy ticket or move is performed.
                const auto source_members=source_entry.members.empty()?std::vector<odh::HouseholdTransferIdentity>{}:odh_host::identify_transfer_household(source_entry,game,player_character_directory,storage);
                for(const auto& saved:active.household_roster){
                    const auto member=std::find_if(source_members.begin(),source_members.end(),[&](const auto& value){return value.guid==saved.guid&&value.neighbor_id==saved.neighbor_id;});
                    if(member==source_members.end()||member->object_definition_id!=saved.object_definition_id||member->character_asset!=saved.character_asset||member->character_fingerprint!=saved.character_fingerprint)
                        throw odh::FormatError("source visited traveler resource identity changed before deferred travel");
                }
                auto source_options=travel_options;source_options.mode=trip.state.current_mode;source_options.house=trip.state.current_house;
                const auto source_plan=build_runtime_arguments(source_options,game,trip.state.current_house,source_entry,*source_resident,graphics_resource,player_character_directory,userdata,overlay?&*overlay:nullptr);
                const auto source_identity=odh_host::identify_session_resources(game,source_plan.resources,static_cast<std::uint8_t>(options.language),source_plan.expansion_mask);
                if(odh::encode_session_resource_identity(active.resources)!=odh::encode_session_resource_identity(source_identity))
                    throw odh::FormatError("source visited resources or expansion profile changed before deferred travel");
            }
            if(legacy_trip){
#ifdef ODH_HAVE_TS1_TRAVEL_CATALOG
                if(trip.state.pending)(void)odh_host::resolve_ts1_travel_destination(game,userdata,trip.state,
                    trip.state.pending->destination,static_cast<std::uint8_t>(options.language),all_travel_destinations);
#endif
                odh_host::adopt_private_checkpoint_namespace(*legacy_trip,trip.home_path,digest,{},storage);
            }
            return trip;
        };
        // Legacy HOME archives carry the neighborhood in the hash of the whole
        // resource manifest. Numeric lot/family identifiers alone are not enough.
        const auto legacy_matches_home=[&](const odh::SessionBundle& saved,const odh::SessionEntry& identity_entry,
                const std::array<std::uint8_t,32>& neighborhood){
            const auto session=odh::load_session_entry(userdata,identity_entry.neighborhood_mode,identity_entry.house_number,
                static_cast<std::uint8_t>(options.language),family_overlay?&*family_overlay:nullptr,player_character_directory);
            if(saved.family_lease||saved.host.house_number!=session.house_number||
               saved.host.neighborhood_mode!=session.neighborhood_mode||saved.host.family_chunk_id!=session.family_chunk_id||
               saved.host.family_number!=session.family_number||saved.household_roster.size()!=session.members.size())return false;
            if(saved.travel)return saved.travel->neighborhood_identity==neighborhood;
            const auto roster=odh_host::identify_transfer_household(session,game,player_character_directory,storage);
            for(const auto& resident:saved.household_roster){
                const auto match=std::find_if(roster.begin(),roster.end(),[&](const auto& member){
                    return member.guid==resident.guid&&member.neighbor_id==resident.neighbor_id&&
                        member.object_definition_id==resident.object_definition_id&&member.character_asset==resident.character_asset&&
                        member.character_fingerprint==resident.character_fingerprint;
                });
                if(match==roster.end())return false;
            }
            const auto member=std::find_if(session.members.begin(),session.members.end(),[&](const auto& value){
                return value.guid==saved.host.resident_guid&&value.neighbor_id==saved.host.resident_neighbor_id;
            });
            if(member==session.members.end())return false;
            auto probe=travel_options;probe.mode=session.neighborhood_mode;probe.house=session.house_number;
            probe.language=saved.resources.language_code;
            const auto plan=build_runtime_arguments(probe,game,session.house_number,session,*member,graphics_resource,player_character_directory,userdata);
            const auto identity=odh_host::identify_session_resources(game,plan.resources,saved.resources.language_code,plan.expansion_mask);
            return odh::encode_session_resource_identity(saved.resources)==odh::encode_session_resource_identity(identity);
        };
        const auto stage_temporary_request=[&](){
            auto& trip=*temporary_trip;
            if(!trip.state.pending)throw odh::FormatError("temporary travel transition lacks a sealed deferred lot request");
            if(!trip.head)throw odh::FormatError("temporary travel source has no accepted checkpoint head");
            const auto checkpoint=odh::decode_session_source_checkpoint(trip.checkpoint,*trip.head);
            const auto& source=checkpoint.active();
            if(!source.travel||odh::encode_ts1_travel_state(*source.travel)!=odh::encode_ts1_travel_state(trip.state))throw odh::FormatError("temporary travel request differs from its current durable checkpoint");
            const auto requested=trip.state.pending->destination;
            const auto destination=requested==-1?trip.origin.home_house:static_cast<std::uint16_t>(requested);
#ifdef ODH_HAVE_TS1_TRAVEL_CATALOG
            const auto mode=odh_host::resolve_ts1_travel_destination(game,userdata,trip.state,requested,
                static_cast<std::uint8_t>(options.language),all_travel_destinations);
#else
            constexpr unsigned mode=0;
            throw odh::FormatError("temporary travel resource catalog is unavailable in this build");
#endif
            rollback_options.reset();options=travel_options;options.house=destination;options.mode=mode;options.load_session.reset();options.load_session_advisory=false;
            if(destination==trip.origin.home_house){
                std::optional<odh::Ts1TravelPersonInheritanceSource> inheritance;
                with_iff_file(userdata/"Neighborhood.iff",[&](const auto& view){
                    inheritance.emplace(odh::read_ts1_travel_person_inheritance(view));
                });
                trip.returning=std::make_shared<odh::PreparedTs1TravelReturn>(
                    odh::prepare_ts1_travel_return(checkpoint,{},&*inheritance));
                // home() deliberately rejects unresolved original personal
                // persistence; the return-pending envelope remains the head.
                const auto& candidate=trip.returning->home();options.restore_archive=odh::encode_session_bundle(candidate);options.restore_bundle=candidate;
                options.resident_id=static_cast<unsigned>(candidate.host.resident_neighbor_id);trip.arrival=false;trip.departure.clear();
            }else{
                trip.departure=checkpoint.active_bytes();
                // Consume the sealed next party only after authenticating the
                // source lot. It is distinct from that lot's admitted roster.
                auto members=odh::ts1_travel_departure_members(trip.state);
                trip.state.transit_info=odh::ts1_travel_departure_transit(trip.state);
                const auto selected=trip.state.protocol==odh::Ts1TravelProtocol::original_6e598?
                    std::find_if(trip.state.pending->departure_members.begin(),trip.state.pending->departure_members.end(),[&](const auto& m){const auto& source=trip.state.pending->source_frame.context;return m.avatar.id==source.caller&&m.avatar.generation==source.caller_generation;}):trip.state.pending->departure_members.begin();
                if(trip.state.protocol==odh::Ts1TravelProtocol::original_6e598){
                    if(selected==trip.state.pending->departure_members.end())throw odh::FormatError("original departure lost source caller identity");
                    trip.state.caller_guid=selected->member.guid;trip.state.caller_neighbor_id=selected->member.neighbor_id;
                }else{trip.state.caller_guid=members.front().guid;trip.state.caller_neighbor_id=members.front().neighbor_id;}
                trip.state.travelers=std::move(members);
                trip.state.current_house=destination;trip.state.current_mode=static_cast<std::uint8_t>(mode);trip.state.pending.reset();trip.state.family_activated=false;
                trip.arrival=true;trip.returning.reset();options.resident_id=static_cast<unsigned>(trip.state.caller_neighbor_id);
            }
            house=destination;
        };
#endif
        for (;;) {
            try {
                if (!house || house > 98)
                    throw odh::FormatError("travel destination is outside the supported lot range");
#ifdef __linux__
                auto family_journal=temporary_trip?temporary_trip->journal:std::move(pending_move_in);
                const auto occupancy_house=temporary_trip?temporary_trip->origin.home_house:
                    (options.restore_bundle&&options.restore_bundle->travel?options.restore_bundle->travel->home_house:house);
#else
                auto family_journal=std::move(pending_move_in);
#endif
#ifdef __linux__
                // Every visit pins a fresh view, including the save just committed
                // by the previous runtime. A per-house path cache has no authority.
                if (family_journal) family_overlay = family_journal->runtime_overlay();
                else family_overlay = odh_host::read_linux_family_overlay_for_neighborhood(userdata / "Neighborhood.iff",storage);
                if (family_overlay && !family_journal) {
                    const auto source = odh::load_neighborhood_family_source(userdata / "Neighborhood.iff");
                    const auto replay = odh::replay_family_overlay(source.families, source.identity, *family_overlay);
                    const auto occupant = std::find_if(replay.states.begin(), replay.states.end(),
                        [&](const auto& state) { return state.house_number == occupancy_house; });
                    if (occupant != replay.states.end() && occupant->managed) {
                        if (options.single_resident_session)
                            throw odh::FormatError("managed family saves require the complete household runtime");
                        family_journal = std::make_shared<odh_host::FamilySessionJournal>(source.families,
                            source.identity, family_overlay, static_cast<std::uint16_t>(occupancy_house), occupant->family_chunk_id,std::nullopt,storage);
                        if (options.load_session_advisory) {
                            options.load_session.reset();
                            options.restore_bundle.reset();
                            options.restore_archive.clear();
                            options.resident_id.reset();
                            options.load_session_advisory = false;
                        }
                        if (!options.load_session && !options.plan_only && !temporary_trip) {
                            if (const auto checkpoint = family_journal->checkpoint_path()) {
                                options.load_session = *checkpoint;
                                options.house = house;
                            }
                        }
                    }
                }
#endif
                if (options.load_session && !options.restore_bundle)
                    load_travel_session(options);
#ifdef __linux__
                if(!temporary_trip&&options.restore_bundle&&options.restore_bundle->travel){
                    temporary_trip=authenticate_trip(options.restore_archive,family_journal);
                    if(temporary_trip->state.pending){stage_temporary_request();continue;}
                    const auto outer=odh::decode_session_bundle(temporary_trip->checkpoint);
                    options.restore_archive=*outer.travel_visited_session;options.restore_bundle=odh::decode_session_bundle(options.restore_archive);
                    options.mode=temporary_trip->state.current_mode;options.house=temporary_trip->state.current_house;house=*options.house;
                    if(options.restore_bundle->unanchored_family_lot)options.resident_id.reset();
                    else options.resident_id=static_cast<unsigned>(options.restore_bundle->host.resident_neighbor_id);
                }
                if(temporary_trip){
                    (void)fresh_origin(temporary_trip->origin,temporary_trip->journal);
                    const auto path=temporary_trip->journal?temporary_trip->journal->checkpoint_path():
                        temporary_trip->private_vacation?temporary_trip->private_vacation->checkpoint_path():std::optional<std::filesystem::path>{temporary_trip->home_path};
                    if(!path)throw odh::FormatError("temporary travel has lost its permanent home checkpoint");
                    const auto bytes=read_travel_checkpoint_bytes(*path);
                    if(odh::sha256(bytes.data(),bytes.size())!=*temporary_trip->head)throw odh::FormatError("temporary travel checkpoint head changed before candidate preparation");
                    if(temporary_trip->journal)temporary_trip->journal->validate_restore(bytes);
                    else if(temporary_trip->private_vacation)temporary_trip->private_vacation->validate_restore(bytes);
                    else (void)odh_host::reopen_private_travel_session_checkpoint(temporary_trip->home_path,*temporary_trip->head,temporary_trip->origin,{},storage);
                }
                if (family_journal) {
                    family_journal->verify_current();
                    if (!options.restore_archive.empty()&&!temporary_trip) family_journal->validate_restore(options.restore_archive);
                    else if (!options.plan_only && family_journal->checkpoint_path()&&!temporary_trip)
                        throw odh::FormatError("managed family checkpoint must be restored before playing");
                } else if (options.restore_bundle && options.restore_bundle->family_lease) {
                    throw odh::FormatError("managed family save has no current occupancy authority");
                }
#endif
                auto session = [&](){
#ifdef __linux__
                    if(temporary_trip&&!temporary_trip->returning&&temporary_trip->state.current_house!=temporary_trip->origin.home_house){
                        if(temporary_trip->arrival){
                            auto departure=odh::decode_session_bundle(temporary_trip->departure);
                            if(departure.character_family){
                                // Resources and execution anchoring retain their origin.
                                // Current FAMI may also name packaged NPC travelers.
                                const auto& party=temporary_trip->state.travelers;
                                departure.household_roster.erase(std::remove_if(departure.household_roster.begin(),departure.household_roster.end(),[&](const auto& member){
                                    return std::none_of(party.begin(),party.end(),[&](const auto& traveler){return traveler.guid==member.guid&&traveler.neighbor_id==member.neighbor_id;});
                                }),departure.household_roster.end());
                                if(departure.household_roster.empty()){
                                    const auto npc=std::find_if(departure.npc_avatars.begin(),departure.npc_avatars.end(),[&](const auto& value){return value.origin.guid==temporary_trip->state.caller_guid&&value.origin.neighbor_id==temporary_trip->state.caller_neighbor_id;});
                                    if(npc==departure.npc_avatars.end())throw odh::FormatError("NPC-only departure lacks its exact caller origin");
                                    departure.npc_primary=odh::WorldEntityReference{npc->origin.id,npc->origin.generation};
                                    departure.runtime_entities.clear();departure.host.runtime_avatar_id=npc->origin.id;
                                    departure.host.resident_guid=npc->origin.guid;departure.host.resident_neighbor_id=npc->origin.neighbor_id;
                                    departure.host.house_number=static_cast<std::uint16_t>(house);departure.host.neighborhood_mode=static_cast<std::uint8_t>(options.mode);
                                    departure.travel=temporary_trip->state;options.resident_id=static_cast<unsigned>(npc->origin.neighbor_id);
                                    return odh::load_visit_session_entry(userdata,temporary_trip->origin.home_mode,temporary_trip->origin.home_house,
                                        options.mode,static_cast<std::uint16_t>(house),{},static_cast<std::uint8_t>(options.language),family_overlay?&*family_overlay:nullptr,player_character_directory,&departure);
                                }
                                const auto& anchor=departure.household_roster.front();
                                departure.host.runtime_avatar_id=anchor.id;departure.host.resident_guid=anchor.guid;
                                departure.host.resident_neighbor_id=anchor.neighbor_id;
                                departure.host.house_number=static_cast<std::uint16_t>(house);
                                departure.host.neighborhood_mode=static_cast<std::uint8_t>(options.mode);
                                options.resident_id=static_cast<unsigned>(anchor.neighbor_id);
                                std::vector<std::uint32_t> execution;for(const auto& member:departure.household_roster)execution.push_back(member.guid);
                                return odh::load_visit_session_entry(userdata,temporary_trip->origin.home_mode,temporary_trip->origin.home_house,
                                    options.mode,static_cast<std::uint16_t>(house),execution,static_cast<std::uint8_t>(options.language),family_overlay?&*family_overlay:nullptr,player_character_directory,&departure);
                            }
                        }
                        std::vector<std::uint32_t> travelers;
                        const auto& resources=temporary_trip->arrival?temporary_trip->origin.permanent_roster:temporary_trip->state.travelers;
                        for(const auto& member:resources)travelers.push_back(member.guid);
                        return odh::load_visit_session_entry(userdata,temporary_trip->origin.home_mode,temporary_trip->origin.home_house,
                            options.mode,static_cast<std::uint16_t>(house),travelers,static_cast<std::uint8_t>(options.language),family_overlay?&*family_overlay:nullptr,player_character_directory,options.restore_bundle?&*options.restore_bundle:nullptr);
                    }
#endif
                    return odh::load_session_entry(userdata, options.mode,static_cast<std::uint16_t>(house),static_cast<std::uint8_t>(options.language),family_overlay?&*family_overlay:nullptr,player_character_directory,nullptr,true);
                }();
                std::shared_ptr<const odh::InstalledOccupancyRetirement> occupancy_retirement;
#ifdef __linux__
                auto property_lot=family_journal&&family_journal->pending_purchase()&&options.restore_archive.empty()?
                    load_current_property(static_cast<std::uint16_t>(house),family_journal->expected_overlay()):std::optional<CurrentProperty>{};
#endif
#ifdef __linux__
                std::filesystem::path private_home_path;
                std::shared_ptr<odh_host::PrivateVacationSession> private_vacation_session;
                std::optional<odh_host::TravelCheckpointDigest> private_home_head;
                std::optional<std::filesystem::path> legacy_home;
                std::optional<odh_host::TravelCheckpointDigest> private_neighborhood;
                if(!family_journal&&!temporary_trip&&!options.single_resident_session&&!options.plan_only){
                    const auto neighborhood=odh_host::linux_neighborhood_identity(userdata/"Neighborhood.iff");
                    private_neighborhood=neighborhood;
                    private_home_path=permanent_travel_checkpoint_path(game,neighborhood,options.mode,house,session.family_number,storage);
                    if(family_overlay&&family_overlay->family_vacation&&
                       odh::family_vacation_private_head(*family_overlay->family_vacation,session.family_chunk_id)){
                        const auto source=odh::load_neighborhood_family_source(userdata/"Neighborhood.iff");
                        private_vacation_session=std::make_shared<odh_host::PrivateVacationSession>(source.families,source.identity,
                            family_overlay,session.family_chunk_id,static_cast<std::uint16_t>(house),static_cast<std::uint8_t>(options.mode),
                            std::nullopt,std::nullopt,storage);
                    }
                    const bool scoped=private_vacation_session||std::filesystem::exists(private_home_path);
                    const auto candidate=private_vacation_session?*private_vacation_session->checkpoint_path():
                        (scoped||storage.number()!=1)?private_home_path:private_home_path.parent_path().parent_path()/private_home_path.filename();
                    std::optional<odh::SessionBundle> accepted;
                    std::vector<std::uint8_t> current;
                    if(scoped||std::filesystem::is_regular_file(std::filesystem::symlink_status(candidate))){
                        try{
                            current=private_vacation_session?private_vacation_session->read_current():read_travel_checkpoint_bytes(candidate);
                            auto saved=odh::decode_session_bundle(current);
                            if(scoped||legacy_matches_home(saved,session,neighborhood)){
                                accepted=std::move(saved);
                                if(!scoped)legacy_home=candidate;
                            }
                        }catch(const std::exception& error){
                            if(scoped||options.load_session==candidate)throw;
                            std::cerr<<"Unrecognized legacy home save left in place: "<<error.what()<<'\n';
                        }
                    }
                    if(accepted){
                        const auto& saved=*accepted;
                        if(options.load_session&&!options.load_session_advisory&&options.restore_archive!=current)
                            throw odh::FormatError("explicit save differs from the current permanent home checkpoint");
                        if(saved.family_lease||saved.host.house_number!=house||saved.host.neighborhood_mode!=options.mode||
                           saved.host.family_chunk_id!=session.family_chunk_id||saved.host.family_number!=session.family_number||
                           saved.resources.language_code!=options.language||(saved.household_roster.empty()&&!saved.npc_primary&&!saved.unanchored_family_lot))
                            throw odh::FormatError("permanent home checkpoint differs from the selected residence or language");
                        if(options.resident_id&&!options.load_session_advisory&&
                           *options.resident_id!=static_cast<unsigned>(saved.host.resident_neighbor_id))
                            throw odh::FormatError("explicit resident differs from the permanent home checkpoint");
                        private_home_head=odh::sha256(current.data(),current.size());
                        options.load_session=candidate;
                        options.load_session_advisory=false;
                        if(saved.unanchored_family_lot)options.resident_id.reset();
                        else options.resident_id=static_cast<unsigned>(saved.host.resident_neighbor_id);
                        options.restore_archive=std::move(current);
                        options.restore_bundle=std::move(accepted);
                        if(options.restore_bundle->travel)continue;
                    }
                }
                if(!temporary_trip || temporary_trip->returning)
                    session=odh::load_session_entry(userdata,options.mode,static_cast<std::uint16_t>(house),
                        static_cast<std::uint8_t>(options.language),family_overlay?&*family_overlay:nullptr,
                        // Only the household profile selects execution members
                        // through GENX/HHRD. The explicitly requested legacy
                        // single-resident profile keeps installed selection and
                        // is authenticated by SingleResidentSessionRestore.
                        player_character_directory,options.restore_bundle&&!options.single_resident_session?
                            &*options.restore_bundle:nullptr);
                if (family_journal&&!property_lot&&(!temporary_trip||temporary_trip->returning)) {
                    const auto source = odh::load_neighborhood_family_source(userdata / "Neighborhood.iff");
                    auto plan = odh::prepare_installed_occupancy_retirement(source, *family_overlay,
                        family_journal->lease(), static_cast<std::uint16_t>(house),
                        odh_host::fingerprint_transfer_resource(require_file(game, session.house_file)));
                    if (plan) occupancy_retirement = std::make_shared<odh::InstalledOccupancyRetirement>(std::move(*plan));
                }
#endif
                std::shared_ptr<const odh::PreparedHouseholdTransfer> family_transfer;
#ifdef __linux__
                if (family_journal && !temporary_trip && family_journal->carry_ticket() && !family_journal->checkpoint_path() && !options.plan_only) {
                    const auto source = odh::load_neighborhood_family_source(userdata / "Neighborhood.iff");
                    const auto roster = odh_host::identify_transfer_household(session, game, player_character_directory,storage);
                    auto transfer = odh_host::load_family_transfer(source, *family_overlay, session.family_chunk_id, roster,
                        userdata / "Neighborhood.iff",storage);
                    if (!transfer) throw odh::FormatError("family move has no portable checkpoint");
                    family_transfer = std::make_shared<odh::PreparedHouseholdTransfer>(std::move(*transfer));
                }
#endif
                if (!options.single_resident_session && session.members.size() > 8)
                    throw odh::FormatError("integrated household exceeds the eight-resident TS1 runtime profile");
                const odh::SessionEntryMember* resident = session.metadata_bootstrap?&session.metadata_bootstrap->resource:
                    session.npc_primary?&session.npc_primary->resource:nullptr;
                if(session.metadata_bootstrap&&options.resident_id)
                    throw odh::FormatError("unanchored family lot cannot select a primary resident");
                if(resident&&options.resident_id&&resident->neighbor_id!=*options.resident_id)
                    throw odh::FormatError("explicit resident differs from the NPC execution anchor");
                if (!resident&&options.resident_id) {
                    const auto found = std::find_if(session.members.begin(), session.members.end(),
                        [&](const auto& member) { return member.neighbor_id == *options.resident_id; });
                    if (found == session.members.end()) throw odh::FormatError("resident is not in the selected family");
                    resident = &*found;
                }
                // Fresh households choose their first resident automatically;
                // a restored checkpoint has already supplied resident_id above.
                else if(!resident) resident = &session.members.front();
#ifdef ODH_HAVE_X11
                if (window) {
                    window->resize(800,600);
                    present_loading_screen(*window,static_cast<std::uint8_t>(options.language));
                }
#endif
                auto plan = build_runtime_arguments(options, game, house, session, *resident,
                    graphics_resource, player_character_directory,userdata,family_overlay?&*family_overlay:nullptr);
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                if(session_log) {
                    session_log->stage("runtime-plan-ready");
                    session_log->event("language="+std::to_string(options.language)+
                        " expansion_mask="+std::to_string(plan.expansion_mask));
                }
#endif
                if (headless_travel_index < options.headless_travel_houses.size())
                    append(plan.arguments, {"--headless-window-neighborhood-tick",
                        std::to_string(options.headless_travel_tick.value_or(1)),
                        "--headless-window-neighborhood-relative", "--headless-window-neighborhood-house",
                        std::to_string(options.headless_travel_houses[headless_travel_index])});
                if(options.headless_resident_go_here){
                    const auto& action=*options.headless_resident_go_here;
                    append(plan.arguments,{"--headless-resident-go-here",std::to_string(action.entity_id),
                        std::to_string(action.x),std::to_string(action.y),std::to_string(action.level),
                        std::to_string(action.tick)});
                }
                // Authenticate the ACTUAL selected catalog for every native
                // entry, including fresh original HOME/destinations. The
                // runtime's pre-dispatch recovery gate needs this authority;
                // plan-only/save-time fingerprinting cannot supply it later.
                const std::optional<odh::SessionResourceIdentity> verified_identity =
                    odh_host::identify_session_resources(game,plan.resources,
                        static_cast<std::uint8_t>(options.language),plan.expansion_mask);
#ifdef __linux__
                if(property_lot){
                    // Fresh reoccupation has no HOME restore_bundle. Establish
                    // its resource authority from the ACTUAL selected plan now,
                    // before runtime preparation; never borrow the document's META.
                    if(!property_lot->first.document.objects)
                        throw odh::FormatError("vacant property lacks its resource-bearing object lifetime");
                    const auto& expected=property_lot->first.document.objects->resources;
                    if(odh::encode_session_resource_identity(*verified_identity)!=odh::encode_session_resource_identity(expected))
                        throw odh::FormatError("vacant property selected resource identity differs: actual_language="+
                            std::to_string(verified_identity->language_code)+" doc_language="+std::to_string(expected.language_code)+
                            " actual_expansions="+std::to_string(verified_identity->expansion_mask)+" doc_expansions="+std::to_string(expected.expansion_mask)+
                            " actual_sha="+digest_hex(verified_identity->resource_fingerprint)+" doc_sha="+digest_hex(expected.resource_fingerprint));
                }
#endif
                if (options.restore_bundle) {
                    const auto& bundle = *options.restore_bundle;
                    if (odh::encode_session_resource_identity(bundle.resources) !=
                        odh::encode_session_resource_identity(*verified_identity))
                        throw odh::FormatError("saved session game resources do not match the selected installation");
                    if (bundle.host.family_chunk_id != session.family_chunk_id ||
                        bundle.host.family_number != session.family_number ||
                        (session.metadata_bootstrap?(!bundle.unanchored_family_lot||bundle.host.resident_guid||bundle.host.resident_neighbor_id||bundle.host.runtime_avatar_id):bundle.host.resident_guid != resident->guid))
                        throw odh::FormatError("saved session family or resident no longer matches the selected lot");
                    if (!bundle.household_roster.empty()) {
                        if (bundle.household_roster.size() != session.members.size())
                            throw odh::FormatError("saved household roster differs from the authenticated execution cohort");
                        for (const auto& saved_member : bundle.household_roster) {
                            const auto found = std::find_if(session.members.begin(), session.members.end(),
                                [&](const auto& member) { return member.neighbor_id == saved_member.neighbor_id; });
                            if (found == session.members.end() || found->guid != saved_member.guid ||
                                found->object_definition_id != saved_member.object_definition_id ||
                                std::filesystem::path(found->character_source + "/" + found->character_asset) !=
                                    std::filesystem::path(saved_member.character_asset))
                                throw odh::FormatError("saved household member identity differs from the authenticated execution cohort");
                        }
                    }
                }
#ifdef __linux__
                if(legacy_home){
                    if(odh_host::linux_neighborhood_identity(userdata/"Neighborhood.iff")!=*private_neighborhood)
                        throw odh::FormatError("neighborhood changed during legacy home save discovery");
                    odh_host::adopt_private_checkpoint_namespace(*legacy_home,private_home_path,*private_home_head,{},storage);
                }
#endif
                if (options.plan_only) {
                    print_plan(options, house, session, *resident, plan, *verified_identity, game);
                    return 0;
                }
#ifdef ODH_HAVE_LINUX_USER_CONFIG
                odh_host::LinuxUserConfig saved;
                saved.preferences=options.preferences;
                saved.language_code = static_cast<int>(options.language);
                saved.data_root = game;
                saved.neighborhood_number=storage.number();
                std::string config_error;
                if (!options.load_session && !options.headless_ticks && !odh_host::save_linux_user_config(saved, &config_error))
                    throw odh::FormatError("cannot save user settings: " + config_error);
#endif
                std::vector<char*> pointers;
                pointers.reserve(plan.arguments.size());
                for (const auto& argument : plan.arguments) pointers.push_back(const_cast<char*>(argument.c_str()));
                odh_host::IntegratedSessionContext session_context;
#ifdef ODH_HAVE_X11
                session_context.cursors=cursors;
                if(cursors)(void)cursors->select(odh_host::OriginalCursorAsset::Arrow);
#endif
                session_context.preferences=options.preferences;
#ifdef ODH_HAVE_LINUX_USER_CONFIG
                session_context.save_preferences=[&](const odh_host::PlayerPreferences& next,std::string& error){
                    odh_host::LinuxUserConfig settings;
                    settings.language_code=static_cast<int>(options.language);settings.data_root=game;
                    settings.neighborhood_number=options.neighborhood;settings.preferences=next;
                    if(!odh_host::save_linux_user_config(settings,&error))return false;
                    options.preferences=next;travel_options.preferences=next;
                    if(rollback_options)rollback_options->preferences=next;
                    return true;
                };
#endif
                session_context.family_journal = std::move(family_journal);
#ifdef __linux__
                if(property_lot){
                    if(!session_context.family_journal||!session_context.family_journal->pending_purchase())
                        throw odh::FormatError("vacant property activation lost its pending occupancy authority");
                    session_context.property_lot=std::make_shared<const odh_host::CommunityLotDocument>(std::move(property_lot->first.document));
                    session_context.property_lot_reference=std::make_shared<const odh::FamilyPropertyRelease>(property_lot->second);
                    session_context.property_release_source=std::make_shared<const odh::NeighborhoodFamilySource>(
                        odh::load_neighborhood_family_source(userdata/"Neighborhood.iff"));
                    const auto source_path=session_context.family_journal->archive_directory()/
                        (digest_hex(property_lot->second.source_checkpoint_sha256)+".odhs");
                    session_context.property_lot_source_archive=odh::encode_session_archive(odh_host::read_session_archive(source_path));
                }
#endif
                if(options.headless_move_out_family){
#ifdef ODH_HAVE_X11
                    if(gui_property_release){
                        const auto& request=*gui_property_release;
                        const auto source=odh::load_neighborhood_family_source(userdata/"Neighborhood.iff");
                        const auto current=odh_host::read_linux_family_overlay_for_neighborhood(userdata/"Neighborhood.iff",storage);
                        if(house!=request.house||*options.headless_move_out_family!=request.family||
                           source.identity!=request.neighborhood_identity||!current||
                           odh::encode_family_overlay(*current)!=odh::encode_family_overlay(request.expected_overlay)||
                           !session_context.family_journal||!session_context.family_journal->expected_overlay()||
                           odh::encode_family_overlay(*session_context.family_journal->expected_overlay())!=
                               odh::encode_family_overlay(request.expected_overlay)||
                           session_context.family_journal->checkpoint_path()!=std::optional<std::filesystem::path>{request.checkpoint})
                            throw odh::FormatError("confirmed GUI property release source changed before runtime preparation");
                        session_context.family_journal->verify_current();
                    }
#endif
                    if(!session_context.family_journal||session.family_chunk_id!=*options.headless_move_out_family||
                       temporary_trip||options.single_resident_session)
                        throw odh::FormatError("property release selection differs from its current occupied family");
                    session_context.prepare_property_release=true;
                    session_context.property_release_source=std::make_shared<const odh::NeighborhoodFamilySource>(
                        odh::load_neighborhood_family_source(userdata/"Neighborhood.iff"));
                    session_context.property_release_identity=std::make_shared<const odh_host::CommunityLotIdentity>(
                        odh_host::community_lot_identity(community_lot_store_options(game,userdata,storage,options.mode,house)));
                }
                session_context.family_transfer = std::move(family_transfer);
                session_context.occupancy_retirement = std::move(occupancy_retirement);
                const bool source_native_profile=options.restore_bundle.has_value();
                const bool source_original_profile=options.restore_bundle&&options.restore_bundle->neighbor_inventory&&options.restore_bundle->neighbor_inventory->version==2;
                options.restore_bundle.reset(); // Runtime revalidates its candidate independently.
                session_context.resources = verified_identity;
                session_context.restore_archive = std::move(options.restore_archive);
                session_context.save_on_exit = options.save_on_exit;
                session_context.presentation_speed=presentation_speed;
                session_context.start_buy_mode=options.resume_buy_mode;
                session_context.paused_before_buy_mode=options.paused_before_buy_mode;
                session_context.start_build_mode=options.resume_build_mode;
                session_context.paused_before_build_mode=options.paused_before_build_mode;
                session_context.build_presentation=std::move(options.build_presentation);
                options.build_presentation.reset();
                options.resume_buy_mode=false;
                options.paused_before_buy_mode=false;
                options.resume_build_mode=false;
                options.paused_before_build_mode=false;
                session_context.single_resident_session = options.single_resident_session;
                session_context.npc_execution_primary=session.npc_primary.has_value();
                session_context.unanchored_family_lot=session.metadata_bootstrap.has_value();
                if(session.metadata_bootstrap)session_context.metadata_bootstrap_reference=session.metadata_bootstrap->reference;
                if (!options.single_resident_session) {
                    const auto characters = require_directory(game, userdata / "Characters");
                    for (const auto& member : session.members) {
                        std::filesystem::path character;
                        if(member.character_source=="UserData/Characters")
                            character=require_file(game,characters/member.character_asset);
                        else if(member.character_source=="PlayerData/Characters")
                            character=std::filesystem::canonical(player_character_directory/member.character_asset);
                        else throw odh::FormatError("character catalog returned an unknown resource source");
                        session_context.household_members.push_back({character,
                            member.guid, static_cast<std::int16_t>(member.neighbor_id),
                            member.object_definition_id,member.character_source+"/"+member.character_asset});
                    }
                }
                session_context.object_command=options.object_command;
                session_context.architecture_command=options.architecture_command;
                session_context.bounded_headless = options.headless_ticks.has_value();
                session_context.game_root = game;
                session_context.neighborhood_number=storage.number();
                session_context.installed_user_data=userdata;
                if(!session_context.property_lot)if(auto architecture=odh_host::load_private_community_architecture(
                        community_lot_store_options(game,userdata,storage,options.mode,house)))
                    session_context.community_architecture=std::make_shared<const odh::ArchitectureSnapshot>(std::move(*architecture));
                session_context.player_character_root = player_character_directory;
                session_context.resource_paths = plan.resources;
                session_context.language_code = static_cast<std::uint8_t>(options.language);
                session_context.expansion_mask = plan.expansion_mask;
                session_context.neighborhood_mode = static_cast<std::uint8_t>(options.mode);
                session_context.house_number = static_cast<std::uint16_t>(house);
                session_context.family_chunk_id = session.family_chunk_id;
                session_context.family_number = session.family_number;
                session_context.resident_guid = session.metadata_bootstrap?0:resident->guid;
                session_context.resident_neighbor_id = session.metadata_bootstrap?0:resident->neighbor_id;
#ifdef __linux__
                session_context.temporary_travel_enabled=temporary_travel_host_ready&&!session_context.single_resident_session;
                session_context.original_companion_protocol_fresh=options.original_companion_protocol;
                const bool saved_original=source_original_profile;
                if(options.original_companion_protocol&&source_native_profile&&!saved_original)
                    throw odh::FormatError("original companion profile cannot migrate a legacy native checkpoint; select a fresh original import");
                session_context.travel_protocol=temporary_trip?temporary_trip->state.protocol:
                    (saved_original||options.original_companion_protocol?odh::Ts1TravelProtocol::original_6e598:odh::Ts1TravelProtocol::legacy_pin);
                session_context.all_travel_destinations=all_travel_destinations;
                if(temporary_trip){
                    session_context.travel_state=temporary_trip->returning?temporary_trip->origin:temporary_trip->state;
                    session_context.suspended_home=temporary_trip->returning?std::vector<std::uint8_t>{}:temporary_trip->suspended_home;
                    session_context.temporary_travel_arrival=temporary_trip->arrival;
                    session_context.travel_departure_archive=temporary_trip->departure;
                    session_context.travel_checkpoint_path=temporary_trip->home_path;
                    session_context.travel_checkpoint_head=temporary_trip->head;
                    session_context.travel_return=temporary_trip->returning;
                    session_context.private_vacation_session=temporary_trip->private_vacation;
                }else if(!session_context.single_resident_session){
                    session_context.travel_state=identify_travel_origin(session_context,userdata/"Neighborhood.iff");
                    session_context.travel_state->protocol=session_context.travel_protocol;
                    if(!session_context.family_journal){
                        if(private_neighborhood!=session_context.travel_state->neighborhood_identity)
                            throw odh::FormatError("neighborhood changed during private home save discovery");
                        session_context.travel_checkpoint_path=private_home_path;
                        session_context.travel_checkpoint_head=private_home_head;
                    }
                }
                if(!temporary_trip)session_context.private_vacation_session=private_vacation_session;
                auto running_family_journal=session_context.family_journal;
                auto running_private_vacation=session_context.private_vacation_session;
#endif
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                if(session_log)session_log->stage("lot-runtime");
#endif
#ifdef ODH_HAVE_X11
                if (window) window->resize(800, 600);
                odh_host::NeighborhoodResumeCallback neighborhood_resume;
                if (map) {
                    const auto active_house = static_cast<std::uint16_t>(house);
                    neighborhood_resume = [&window, &map, active_house]() {
                        window->resize(800, 600);
                        try {
                            const auto selected = odh_host::show_neighborhood_resume_picker(*window, *map, active_house);
                            window->resize(800, 600);
                            return selected;
                        } catch (...) {
                            window->resize(800, 600);
                            throw;
                        }
                    };
                }
                const auto result = odh_person_run(static_cast<int>(pointers.size()), pointers.data(), window.get(),
                                      std::move(neighborhood_resume),std::move(session_context),session_log);
#else
                const auto result = odh_person_run(static_cast<int>(pointers.size()), pointers.data(), nullptr,{},
                                      std::move(session_context),session_log);
#endif
#ifdef __linux__
                if(result.family_journal)running_family_journal=result.family_journal;
                if(result.private_vacation_session)running_private_vacation=result.private_vacation_session;
#endif
                if (result.exit_code != 0) {
#ifdef __linux__
                    if(temporary_trip)throw odh::FormatError("temporary destination runtime failed with exit code "+std::to_string(result.exit_code));
#endif
                    if (!rollback_options) return result.exit_code;
                    throw odh::FormatError("destination runtime failed with exit code " + std::to_string(result.exit_code));
                }
                if(options.headless_move_out_family){
                    if(!result.prepared_property_release||result.transition||result.temporary_travel)
                        throw odh::FormatError("property runtime did not return its unique prepared release");
                    const auto& prepared=*result.prepared_property_release;
                    const auto source=odh::load_neighborhood_family_source(userdata/"Neighborhood.iff");
                    const auto publication=odh_host::publish_property_release_session(source,
                        community_lot_store_options(game,userdata,storage,options.mode,house),prepared);
                    // This runtime has retired its residents. Whether durability
                    // is confirmed or uncertain, never run it or refund again.
                    std::cout<<"{\"status\":\"moved_out\",\"family_chunk_id\":"<<*options.headless_move_out_family
                        <<",\"refund\":"<<prepared.refund()<<",\"vacant_purchase_value\":"<<prepared.release().vacant_purchase_value
                        <<",\"deleted_roots\":"<<prepared.deleted_roots()<<",\"durability_uncertain\":"
                        <<(publication.durability_uncertain?"true":"false")<<"}\n";
#ifdef ODH_HAVE_X11
                    if(gui_property_release){
                        // The retired runtime is gone and its publication is final,
                        // including exact-readback uncertain durability. Never replay
                        // this request or roll back to the old occupancy.
                        auto previous=std::move(gui_property_release->previous_options);
                        gui_property_release.reset();rollback_options.reset();saved_lots.clear();
                        options=std::move(previous);options.house.reset();options.load_session.reset();
                        options.restore_bundle.reset();options.restore_archive.clear();options.load_session_advisory=false;
                        travel_options=options;
                        if(!run_management_map())return 0;
                        travel_options.mode=options.mode;travel_options.neighborhood=storage.number();
                        options.house=house;continue;
                    }
#endif
                    return 0;
                }
                if(result.prepared_property_release)
                    throw odh::FormatError("unexpected property release without a launcher-owned request");
                if(result.presentation_speed){
                    if(!odh_host::valid_simulation_speed(*result.presentation_speed))
                        throw odh::FormatError("runtime returned an invalid presentation speed");
                    presentation_speed=*result.presentation_speed;
                }
#ifdef __linux__
                if(result.temporary_travel){
                    if(result.transition)throw odh::FormatError("temporary transport cannot also request a residential neighborhood transition");
                    const auto& request=*result.temporary_travel;
                    const auto origin=fresh_origin(request.state,running_family_journal);
                    const auto authority=running_family_journal?running_family_journal->checkpoint_path():
                        running_private_vacation?running_private_vacation->checkpoint_path():
                        std::optional<std::filesystem::path>{permanent_travel_checkpoint_path(game,origin.neighborhood_identity,origin.home_mode,origin.home_house,origin.family_number,storage)};
                    if(!authority||request.saved_session_path.lexically_normal()!=authority->lexically_normal())throw odh::FormatError("temporary travel result does not identify its canonical permanent-home checkpoint");
                    const auto bytes=read_travel_checkpoint_bytes(*authority);auto accepted=authenticate_trip(bytes,running_family_journal);
                    if(odh::encode_ts1_travel_state(accepted.state)!=odh::encode_ts1_travel_state(request.state)||
                        (!request.suspended_home.empty()&&request.suspended_home!=accepted.suspended_home))throw odh::FormatError("temporary travel result has stale state or substituted suspended home");
                    temporary_trip=std::move(accepted);stage_temporary_request();continue;
                }
                if(temporary_trip&&temporary_trip->returning){
                    // The runtime validated/restored the receipt candidate and
                    // committed its home checkpoint before its first tick.
                    // An unresolved receipt could never reach this point.
                    const auto& expected_home=temporary_trip->returning->home();
                    const auto path=running_family_journal?running_family_journal->checkpoint_path():
                        running_private_vacation?running_private_vacation->checkpoint_path():std::optional<std::filesystem::path>{temporary_trip->home_path};
                    if(!path)throw odh::FormatError("validated temporary return did not publish a home checkpoint");
                    const auto committed=read_travel_checkpoint_bytes(*path);
                    if(running_family_journal)running_family_journal->validate_restore(committed);
                    else if(running_private_vacation)running_private_vacation->validate_restore(committed);
                    const auto home=odh::decode_session_bundle(committed);
                    if(home.travel||home.host.house_number!=temporary_trip->origin.home_house||home.host.family_chunk_id!=expected_home.host.family_chunk_id||home.host.family_number!=expected_home.host.family_number||odh::sha256(committed.data(),committed.size())==temporary_trip->returning->source_checkpoint_sha256())throw odh::FormatError("temporary return did not replace the authenticated trip head with its permanent home");
                    temporary_trip.reset();
                }
                if(temporary_trip&&result.transition)throw odh::FormatError("a temporary stay cannot mutate permanent residential occupancy");
#endif
                if(result.transition&&result.transition->kind==odh_host::NeighborhoodRequestKind::reload_committed_session){
                    // A property command already published a new journal head.
                    // Discard the old runtime and every decoded old checkpoint;
                    // the next loop validates this archive against the live lease.
                    // A failure here must not roll back to the pre-purchase state.
                    rollback_options.reset();
                    if(result.transition->destination_house!=house||result.transition->saved_session_path.empty())
                        throw odh::FormatError("committed object command requested an invalid same-house reload");
                    if(result.transition->build_presentation&&
                       (!result.transition->resume_build_mode||result.transition->resume_buy_mode))
                        throw odh::FormatError("build presentation requires an exclusive same-house Build reload");
                    options.build_presentation=std::move(result.transition->build_presentation);
                    options.object_command.reset(); // Never charge/replay the command after reopening.
                    options.architecture_command.reset();
                    options.resume_buy_mode=result.transition->resume_buy_mode;
                    options.paused_before_buy_mode=result.transition->paused_before_buy_mode;
                    options.resume_build_mode=result.transition->resume_build_mode;
                    options.paused_before_build_mode=result.transition->paused_before_build_mode;
                    options.load_session=result.transition->saved_session_path;
                    options.load_session_advisory=false;
                    options.restore_bundle.reset();
                    options.restore_archive.clear();
                    saved_lots[house]=result.transition->saved_session_path;
                    continue;
                }
                const bool scripted_transition_expected =
                    headless_travel_index < options.headless_travel_houses.size();
                if(result.return_to_neighborhood){
#ifdef ODH_HAVE_X11
                    if(scripted_transition_expected||options.headless_ticks)
                        throw odh::FormatError("headless travel cannot return through the player UI");
                    rollback_options.reset();saved_lots.clear();
                    options=travel_options;options.house.reset();options.load_session.reset();
                    options.restore_bundle.reset();options.restore_archive.clear();options.load_session_advisory=false;
                    if(!run_management_map())return 0;
                    travel_options.mode=options.mode;travel_options.neighborhood=storage.number();
                    options.house=house;continue;
#else
                    throw odh::FormatError("returning to the neighborhood requires a window");
#endif
                }
                if (!result.transition) {
                    if (scripted_transition_expected)
                        throw odh::FormatError("headless travel was not accepted by the active lot");
                    rollback_options.reset();
                    return result.exit_code;
                }
                if (result.transition->kind == odh_host::NeighborhoodRequestKind::manage_families) {
                    if (scripted_transition_expected)
                        throw odh::FormatError("scripted lot travel cannot request family management");
#ifdef ODH_HAVE_X11
                    // The old runtime no longer exists. Its checkpoint remains
                    // on disk, but management may expire its lease; never install
                    // an automatic rollback to that old occupancy afterward.
                    rollback_options.reset();
                    saved_lots.clear();
                    options = travel_options;
                    options.house.reset();
                    if (!run_management_map()) return 0;
                    travel_options.mode = options.mode;
        travel_options.neighborhood=storage.number();
                    options.house = house;
                    continue;
#else
                    throw odh::FormatError("family management requires a window backend");
#endif
                }
                if (scripted_transition_expected) {
                    const auto expected_house = options.headless_travel_houses[headless_travel_index];
                    if (result.transition->destination_house != expected_house)
                        throw odh::FormatError("headless travel selected an unexpected destination");
                    ++headless_travel_index;
                }
                rollback_options.reset();

                // The runtime has saved and unwound before reaching this point. Retain
                // that archive even when destination preparation or execution fails.
                saved_lots[house] = result.transition->saved_session_path;
                rollback_options = travel_options;
                rollback_options->house = house;
                rollback_options->load_session = result.transition->saved_session_path;
                const auto destination = result.transition->destination_house;
                options = travel_options;
                options.house = destination;
                const auto cached = saved_lots.find(destination);
                if (cached != saved_lots.end()) {
                    options.load_session = cached->second;
                    options.load_session_advisory = true;
                }
                house = destination;
            } catch (const std::exception& transition_error) {
#ifdef __linux__
                if(temporary_trip){
                    const auto current=temporary_trip->journal?temporary_trip->journal->checkpoint_path():std::optional<std::filesystem::path>{};
                    const auto& recovery=current?*current:temporary_trip->home_path;
                    throw odh::FormatError(std::string("temporary travel stopped; durable recovery retained at ")+recovery.string()+": "+transition_error.what());
                }
#endif
                if (!rollback_options) throw;
                std::cerr << "odh-person: lot transition failed: " << transition_error.what()
                          << "; restoring previous lot from " << rollback_options->load_session->string() << '\n';
                options = std::move(*rollback_options);
                rollback_options.reset(); // A failed rollback terminates; never retry recursively.
                house = *options.house;
            }
        }
    } catch (const std::exception& error) {
#ifdef ODH_HAVE_LINUX_SESSION_LOG
        if(session_log)session_log->stage("launcher-failed");
#endif
        std::cerr << "odh-person: " << error.what() << '\n';
        return 2;
    }
}
