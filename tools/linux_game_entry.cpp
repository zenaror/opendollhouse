#include "person_runtime.hpp"
#ifdef ODH_HAVE_X11
#include "neighborhood_screen.hpp"
#include "linux_text_renderer.hpp"
#include "linux_ui_strings.hpp"
#ifdef ODH_HAVE_LINUX_CAS_SCREEN
#include "linux_cas_screen.hpp"
#include "linux_simitone_cas_member.hpp"
#include "linux_private_family.hpp"
#endif
#endif
#include "resource.hpp"
#ifdef ODH_HAVE_LINUX_SESSION_LOG
#include "linux_session_log.hpp"
#endif

#include "odh/session_entry.hpp"
#include "odh/animation.hpp"
#include "odh/session_bundle.hpp"
#include "odh/family.hpp"
#include "odh/family_overlay.hpp"
#include "odh/lot.hpp"
#ifdef __linux__
#include "linux_session_archive_store.hpp"
#include "linux_family_overlay_store.hpp"
#endif
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
#include <array>
#include <charconv>
#include <cctype>
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
    struct ResidentInteraction {unsigned actor_id,object_id,tick;};
    struct ResidentStand {unsigned actor_id,tick;};
    std::filesystem::path game_root;
    unsigned mode = 0;
    unsigned language = 1;
    std::optional<unsigned> house;
    std::optional<unsigned> resident_id;
    bool plan_only = false, choose_language = false;
    std::optional<std::filesystem::path> load_session;
    std::optional<unsigned> headless_ticks;
    std::optional<unsigned> instruction_budget;
    std::optional<std::filesystem::path> status_frame;
    std::optional<unsigned> status_tick;
    std::vector<unsigned> headless_travel_houses;
    std::optional<unsigned> headless_travel_tick;
    bool save_on_exit = false;
    bool audio_enabled=false,audio_disabled=false;
    unsigned audio_drain_ms=0;
    bool single_resident_session = false;
    std::optional<unsigned> headless_sit_object;
    std::optional<unsigned> headless_stand_tick;
    std::optional<ResidentSit> headless_resident_sit;
    std::optional<ResidentInteraction> headless_resident_interaction;
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

Options parse_options(int argc, char** argv) {
    Options result;
    bool have_root = false, have_mode = false, have_language = false;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
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
        if(key=="--headless-travel-tick"&&!result.headless_travel_tick&&i+1<argc){
            result.headless_travel_tick=parse_number(argv[++i],4095,"lot switch tick");
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
            const auto tick=parse_number(argv[++i],4096,"resident Stand tick");
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
            const auto tick=parse_number(argv[++i],4096,"resident Go Here tick");
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
            const auto tick=parse_number(argv[++i],4096,"Go Here tick");
            if(!level||!tick)throw odh::FormatError("single-resident Go Here level or tick is outside the supported profile");
            result.headless_single_resident_go_here=Options::SingleResidentGoHere{0,x,y,level,tick};
            continue;
        }
        if (key == "--headless-resident-sit" && !result.headless_resident_sit) {
            if (i + 3 >= argc) throw odh::FormatError("missing resident Sit values");
            const auto actor_id=parse_number(argv[++i],32767,"resident Sit actor ID");
            const auto object_id=parse_number(argv[++i],32767,"resident Sit object ID");
            const auto stand_tick=parse_number(argv[++i],4096,"resident Stand tick");
            if(!actor_id||!object_id||stand_tick<2)
                throw odh::FormatError("resident Sit actor, object, or Stand tick is outside the supported profile");
            result.headless_resident_sit=Options::ResidentSit{actor_id,object_id,stand_tick};
            continue;
        }
        if (key == "--headless-resident-interaction") {
            if (i + 3 >= argc) throw odh::FormatError("missing resident interaction values");
            if(result.headless_resident_interaction&&result.headless_resident_interactions.size()>=31)
                throw odh::FormatError("resident interaction sequence exceeds thirty-two requests");
            const auto actor_id=parse_number(argv[++i],32767,"resident interaction actor ID");
            const auto object_id=parse_number(argv[++i],32767,"resident interaction object ID");
            const auto tick=parse_number(argv[++i],4096,"resident interaction tick");
            if(!actor_id||!object_id||!tick)
                throw odh::FormatError("resident interaction actor, object, or tick is outside the supported profile");
            const Options::ResidentInteraction request{actor_id,object_id,tick};
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
            const auto tick=parse_number(argv[++i],4096,"interaction cancel tick");
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
        } else if (key == "--language" && !have_language) {
            result.language = parse_number(value, 20, "language code");
            have_language = true;
        } else if (key == "--house" && !result.house) {
            result.house = parse_number(value, 98, "house number");
        } else if (key == "--load-session" && !result.load_session) {
            result.load_session = value;
        } else if (key == "--headless-ticks" && !result.headless_ticks) {
            result.headless_ticks = parse_number(value, 4096, "headless tick count");
        } else if (key == "--headless-status-frame" && !result.status_frame) {
            result.status_frame = value;
        } else if (key == "--headless-status-tick" && !result.status_tick) {
            result.status_tick = parse_number(value, 4096, "status capture tick");
        } else if (key == "--instruction-budget" && !result.instruction_budget) {
            result.instruction_budget = parse_number(value, 1000000, "instruction budget");
        } else if (key == "--resident-id" && !result.resident_id) {
            result.resident_id = parse_number(value, 32767, "resident ID");
        } else if (key == "--headless-sit-object" && !result.headless_sit_object) {
            result.headless_sit_object = parse_number(value, 32767, "Sit object ID");
        } else if (key == "--headless-stand-tick" && !result.headless_stand_tick) {
            result.headless_stand_tick = parse_number(value, 4096, "Stand tick");
        } else {
            throw odh::FormatError("unknown or duplicate integrated game option");
        }
    }
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
        if (bundle.household_roster.empty() && !result.single_resident_session)
            throw odh::FormatError("legacy single-resident session requires explicit --single-resident-session");
        if (!bundle.household_roster.empty() && result.single_resident_session)
            throw odh::FormatError("household session cannot use --single-resident-session");
        if (bundle.runtime_entities.size() != (bundle.household_roster.empty() ? 1u : bundle.household_roster.size()))
            throw odh::FormatError("session VM roster does not match its household profile");
        result.mode = bundle.host.neighborhood_mode;
        result.language = bundle.resources.language_code;
        result.house = bundle.host.house_number;
        result.resident_id = static_cast<unsigned>(bundle.host.resident_neighbor_id);
        have_mode = have_language = true;
#else
        throw odh::FormatError("session loading is currently available only on Linux");
#endif
    }
    if ((result.status_frame || result.status_tick) &&
        (!result.status_frame || result.status_frame->empty() || !result.status_tick || !*result.status_tick ||
         !result.headless_ticks || *result.status_tick > *result.headless_ticks || result.plan_only ||
         !result.headless_travel_houses.empty()))
        throw odh::FormatError("status capture requires --headless-status-frame PATH and --headless-status-tick 1..headless-ticks, without plan/travel");
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
    if (result.headless_resident_interaction && (!result.headless_ticks || result.plan_only ||
        result.single_resident_session || result.headless_stand || result.headless_sit_object || result.headless_resident_sit))
        throw odh::FormatError("--headless-resident-interaction requires a bounded whole-household lot session");
    if (result.headless_resident_interaction && result.headless_resident_interaction->tick > *result.headless_ticks)
        throw odh::FormatError("resident interaction tick must fit within the bounded run");
    for(const auto& request:result.headless_resident_interactions)
        if(request.tick>*result.headless_ticks)
            throw odh::FormatError("resident interaction sequence ticks must fit within the bounded run");
    if (result.headless_resident_interaction && result.load_session &&
        (!result.restore_bundle || result.restore_bundle->household_roster.empty()))
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
            const auto& seats=result.restore_bundle->household_seated_continuations;
            if (std::none_of(seats.begin(),seats.end(),
                    [&](const auto& seat) { return seat.actor_id == request.actor_id; }))
                throw odh::FormatError("resident Stand actor has no verified saved household seat");
        }
    }
    if(result.headless_resident_go_here&&(!result.headless_ticks||result.plan_only||result.load_session||
       result.single_resident_session||result.headless_resident_go_here->tick>*result.headless_ticks))
        throw odh::FormatError("--headless-resident-go-here requires an in-range tick in a new whole-household headless run");
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
    if (result.instruction_budget && (!*result.instruction_budget || !result.headless_ticks ||
        result.plan_only || result.single_resident_session))
        throw odh::FormatError("--instruction-budget requires a bounded whole-household run with --headless-ticks");
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
            "[--headless-sit-object ID --headless-stand-tick N] "
            "[--headless-single-resident-go-here X Y LEVEL TICK] "
            "[--headless-resident-interaction ACTOR TARGET TICK (repeat up to 32, nondecreasing ticks)] "
            "[--headless-resident-stand ACTOR TICK (saved household seats only; repeat up to 8, nondecreasing ticks 1..N)] "
            "[--headless-stand (legacy saved single-resident seat)] "
            "[--audio | --no-audio] [--audio-drain-ms 1..5000] "
            "[--headless-travel-house HOUSE (repeat)] [--headless-travel-tick 1..N-1] "
            "[--load-session PATH] [--plan-only | --headless-ticks 0..4096 [--instruction-budget 1..1000000] [--headless-status-frame PATH --headless-status-tick 1..N] [--save-session-on-exit]]");
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
    if (bundle.household_roster.empty() != options.single_resident_session ||
        bundle.runtime_entities.size() != (bundle.household_roster.empty() ? 1u : bundle.household_roster.size()))
        throw odh::FormatError("travel session does not match the household runtime profile");
    options.resident_id = static_cast<unsigned>(bundle.host.resident_neighbor_id);
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
                result = odh_host::legacy_text_to_utf8(entry->value, language);
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
        const std::filesystem::path& player_character_directory) {
    RuntimePlan plan;
    std::set<std::filesystem::path> unique_resources;
    const auto record_resource = [&](const std::filesystem::path& path) {
        if (unique_resources.insert(path).second) plan.resources.push_back(path);
    };
    const auto userdata = require_directory(game, game / "UserData");
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
    std::vector<std::filesystem::path> character_files;
    for (const auto& member : session.members) {
        const auto character=resolve_character(member);
        if (std::find(character_files.begin(), character_files.end(), character) == character_files.end())
            character_files.push_back(character);
    }
    const auto selected_character=resolve_character(resident);
    if (std::find(character_files.begin(), character_files.end(), selected_character) == character_files.end())
        throw odh::FormatError("selected resident character resource is absent from the resolved family");
    const auto architecture = find_files(gamedata, "", {".flr", ".wll"}, 4096);

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
    for (const auto& character : find_files(characters, "", {".iff"}, 4096))
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
        "PersonGlobals.iff", "--global", global_far.string(), "Global.iff",
        "--lot", house_file.string(), std::to_string(house), "--zoning", zoning.string(),
        "--neighborhood", neighborhood.string(), "--activate-family", "--language",
        std::to_string(options.language), "--object-definitions", objects_far.string()};
    for (const auto& archive : shared)
        append(args, {"--object-definitions", archive.string()});
    for (const auto& archive : expansions)
        append(args, {"--object-definitions", archive.string()});
    for (const auto& archive : download_archives)
        append(args, {"--object-definitions", archive.string()});
    for (const auto& character : character_files)
        append(args, {"--object-definition-file", character.string()});
    for (const auto& iff : download_iffs)
        append(args, {"--object-definition-file", iff.string()});
    append(args, {"--animation-source", animation_far.string()});
    for (const auto& archive : shared) append(args, {"--animation-source", archive.string()});
    for (const auto& archive : expansions) append(args, {"--animation-source", archive.string()});
    for (const auto& archive : download_archives) append(args, {"--animation-source", archive.string()});
    append(args, {options.headless_ticks?"--headless-main":"--saved-world-main", "--render-saved-guid", std::to_string(resident.guid),
        "--render-floors", floors.string(), "--render-walls", walls.string(), "--render-level", "1",
        "--render-wall-mode", "up", "--render-terrain-theme", "0"});
    if(options.audio_enabled||(!options.headless_ticks&&!options.audio_disabled))append(args,{"--session-audio"});
    if(options.audio_drain_ms)append(args,{"--audio-drain-ms",std::to_string(options.audio_drain_ms)});
    if (options.headless_ticks)
        append(args, {"--ticks", std::to_string(*options.headless_ticks)});
    else
        append(args, {"--render-window", "--render-size", "800", "600",
            "--render-animation-far", animation_far.string(),
            "--render-textures", textures_far.string(), "--render-skeleton", "adult-skeleton.cmx.bcf", "adult"});
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
    if (options.headless_resident_interaction)
        append(args, {"--headless-resident-interaction", std::to_string(options.headless_resident_interaction->actor_id),
            std::to_string(options.headless_resident_interaction->object_id),
            std::to_string(options.headless_resident_interaction->tick)});
    for(const auto& request:options.headless_resident_interactions)
        append(args,{"--headless-resident-interaction",std::to_string(request.actor_id),
            std::to_string(request.object_id),std::to_string(request.tick)});
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
        const auto& seats=options.restore_bundle->household_seated_continuations;
        const auto seat=std::find_if(seats.begin(),seats.end(),
            [&](const auto& saved) { return saved.actor_id == request.actor_id; });
        if (seat == seats.end())
            throw odh::FormatError("resident Stand actor has no verified saved household seat");
        append(args,{"--headless-resident-stand",std::to_string(request.actor_id),
            std::to_string(seat->target_id),std::to_string(request.tick)});
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
    std::cout << "{\"status\":\"ready\",\"mode\":" << options.mode
        << ",\"house\":" << house << ",\"family_chunk_id\":" << session.family_chunk_id
        << ",\"resident_guid\":" << resident.guid << ",\"resident_neighbor_id\":"
        << resident.neighbor_id << ",\"resident_asset\":" << json_bytes(resident.character_asset)
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
        const auto userdata = require_directory(game, game / "UserData");
#ifdef ODH_HAVE_LINUX_USER_CONFIG
        const auto player_character_directory=odh_host::linux_player_character_directory();
#else
        const std::filesystem::path player_character_directory;
#endif
#ifdef __linux__
        auto family_overlay = odh_host::read_linux_family_overlay_for_neighborhood(
            userdata / "Neighborhood.iff");
#else
        std::optional<odh::FamilyOverlay> family_overlay;
#endif
        std::optional<std::filesystem::path> graphics_resource;
#ifdef ODH_HAVE_X11
        const auto graphics = require_file(game, game / "UIGraphics/UIGraphics.far");
        graphics_resource = graphics;
#endif

#ifdef ODH_HAVE_X11
        std::optional<odh_host::PreparedNeighborhoodMap> map;
        std::unique_ptr<odh_host::LinuxWindow> window;
        std::unique_ptr<odh_host::GameUiStrings> ui_string_catalog;
        std::string lot_panel_accept = "OK";
        std::string lot_panel_back = "Cancel";
        std::string move_in_title = "Purchase House?";
        std::string move_in_question = "Are you sure you want to buy this house?";
        std::string move_out_title = "Evict Family?";
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
            confirm_yes = ui_strings.get(142, 2,
                static_cast<std::uint8_t>(options.language), "Yes");
            confirm_no = ui_strings.get(142, 3,
                static_cast<std::uint8_t>(options.language), "No");
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
        if (map && !options.house) {
#ifdef ODH_HAVE_LINUX_SESSION_LOG
          if(session_log)session_log->stage("neighborhood-picker");
#endif
          for (;;) {
#ifdef ODH_HAVE_LINUX_CAS_SCREEN
            constexpr bool allow_create_family = true;
#else
            constexpr bool allow_create_family = false;
#endif
            const auto selected = odh_host::show_neighborhood_picker_with_actions(*window, *map,
                neighborhood_title, neighborhood_cancel,
                static_cast<std::uint8_t>(options.language), allow_create_family);
            if (!selected) {
                std::cout << "{\"status\":\"cancelled\",\"stage\":\"neighborhood\",\"cancellation_reason\":\""
                          << odh_host::window_quit_reason_name(window->quit_reason()) << "\"}\n";
                return 0;
            }
            if (selected->action == odh_host::NeighborhoodPickerAction::switch_mode) {
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
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                if(session_log)session_log->stage("cas");
#endif
                const auto draft = odh_host::show_linux_cas_family_creator(*window,
                    odh_host::scan_linux_cas_catalog(game),
                    static_cast<std::uint8_t>(options.language));
                if (!draft) {
                    window->clear_escape_quit_reason();
                    continue;
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
                    odh_host::linux_family_overlay_path(identity), draft->family_name_utf8,
                    members);
                family_overlay = odh_host::read_linux_family_overlay_for_neighborhood(neighborhood);
                map = odh_host::prepare_neighborhood_map(userdata, *graphics_resource,
                    options.mode, family_overlay ? &*family_overlay : nullptr,
                    static_cast<std::uint8_t>(options.language));
                continue;
            }
#endif
            house = selected->house_number;
            const auto marker = std::find_if(map->markers.begin(), map->markers.end(),
                [&](const auto& value) { return value.location.house == house; });
            if (marker == map->markers.end())
                throw odh::FormatError("selected lot is absent from the prepared neighborhood");
            if (!ui_string_catalog)
                throw odh::FormatError("localized lot details are unavailable outside the game UI");
            auto [lot_title, lot_description] = neighborhood_lot_text(game, userdata,
                static_cast<std::uint16_t>(house), static_cast<std::uint8_t>(options.language),
                *ui_string_catalog);
            std::int32_t selected_lot_price = -1;
            std::string lot_summary;
            if (marker->occupied) {
                const odh::FamilyContext family_context(map->effective_families);
                const auto* family = family_context.by_house(static_cast<std::int16_t>(house));
                if (!family)
                    throw odh::FormatError("occupied lot has no matching family record");
                const auto net_worth = static_cast<std::int64_t>(family->family.value_in_arch) +
                    family->family.budget;
                const auto family_name = family->display_name.empty() ?
                    ui_string_catalog->get(134, 22,
                        static_cast<std::uint8_t>(options.language), "No Name") :
                    family->display_name;
                lot_summary = format_game_ui_string(ui_string_catalog->get(134, 0,
                    static_cast<std::uint8_t>(options.language),
                    "The %s Family\r\nNet Worth: %s\r\nFriends: %d"), {
                        family_name, "\xc2\xa7" + std::to_string(net_worth),
                        std::to_string(family->family.family_friends)});
                lot_summary += "\r\n" + ui_string_catalog->get(132, 15,
                    static_cast<std::uint8_t>(options.language),
                    "This house is occupied by another family.");
            } else {
                selected_lot_price = house_purchase_price(game, userdata,
                    static_cast<std::uint16_t>(house));
                lot_summary = format_game_ui_string(ui_string_catalog->get(134, 1,
                    static_cast<std::uint8_t>(options.language),
                    "%s\r\n%s\r\nCost: %s\r\nPick or create a family and move them in to play."), {
                        lot_title, lot_description, "\xc2\xa7" +
                            std::to_string(selected_lot_price)});
                for (int line = 0; line < 2; ++line) {
                    const auto next_line = lot_summary.find_first_of("\r\n");
                    if (next_line == std::string::npos) break;
                    lot_summary.erase(0, next_line + 1);
                    if (!lot_summary.empty() && lot_summary.front() == '\n') lot_summary.erase(0, 1);
                }
            }
            const auto panel_choice = odh_host::show_neighborhood_lot_panel(*window, *map,
                static_cast<std::uint16_t>(house), lot_title,
                marker->occupied ? std::string{} : lot_description, lot_summary,
                lot_panel_accept, lot_panel_back,
                static_cast<std::uint8_t>(options.language));
            if (panel_choice == odh_host::NeighborhoodLotPanelChoice::back_to_map)
                continue;
            if (marker->occupied) {
                const odh::FamilyContext family_context(map->effective_families);
                const auto* current_family = family_context.by_house(static_cast<std::int16_t>(house));
                if (!current_family)
                    throw odh::FormatError("occupied lot has no matching family record");
                const auto neighborhood_identity = odh_host::linux_neighborhood_identity(
                    userdata / "Neighborhood.iff");
                bool can_move_out = false;
                try {
                    (void)odh::plan_family_move_out_overlay(map->source_families,
                        neighborhood_identity, family_overlay ? &*family_overlay : nullptr,
                        current_family->chunk_id);
                    can_move_out = true;
                } catch (const odh::FormatError&) {
                    // Families without a verified source refund can still enter
                    // their house; the move-out option remains unavailable.
                }
                const auto choice = odh_host::show_occupied_lot_choice(*window,
                    *current_family, static_cast<std::uint16_t>(house), can_move_out,
                    static_cast<std::uint8_t>(options.language));
                if (choice == odh_host::OccupiedLotChoice::cancel) {
                    std::cout << "{\"status\":\"cancelled\",\"stage\":\"occupied-lot-choice\"}\n";
                    return 0;
                }
                if (choice == odh_host::OccupiedLotChoice::move_out) {
                    if (!odh_host::show_move_out_confirmation(*window, *current_family,
                            static_cast<std::uint16_t>(house),
                            static_cast<std::uint8_t>(options.language), move_out_title,
                            confirm_yes, confirm_no)) {
                        std::cout << "{\"status\":\"cancelled\",\"stage\":\"move-out-confirmation\"}\n";
                        return 0;
                    }
                    const auto latest_overlay = odh_host::read_linux_family_overlay_for_neighborhood(
                        userdata / "Neighborhood.iff");
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
                    const auto next_overlay = odh::plan_family_move_out_overlay(
                        latest_map.source_families, latest_neighborhood_identity,
                        latest_overlay ? &*latest_overlay : nullptr, latest_family->chunk_id);
                    odh_host::compare_exchange_linux_family_overlay_atomic(
                        odh_host::linux_family_overlay_path(latest_neighborhood_identity),
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
                break;
            }
            if (!marker->occupied) {
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
                const auto family_choice = odh_host::show_move_in_family_picker(*window,
                    map->effective_families, static_cast<std::uint16_t>(house), purchase_price,
                    static_cast<std::uint8_t>(options.language), family_picker_action,
                    family_picker_title, no_family_title, no_family_message, family_picker_cancel,
                    graphics, family_picker_net_worth, family_picker_friends,
                    family_picker_archive, deletable_family_ids);
                if (!family_choice) {
                    const auto reason = window->quit_reason();
                    if (reason == odh_host::WindowQuitReason::window_manager_close ||
                        reason == odh_host::WindowQuitReason::window_destroyed) {
                        std::cout << "{\"status\":\"cancelled\",\"stage\":\"family\",\"cancellation_reason\":\"window_close\"}\n";
                        return 0;
                    }
                    continue;
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
                        userdata / "Neighborhood.iff");
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
                        odh_host::linux_family_overlay_path(neighborhood_identity),
                        latest_overlay, next_overlay);
                    family_overlay = next_overlay;
                    map = odh_host::prepare_neighborhood_map(userdata, graphics, options.mode,
                        &*family_overlay, static_cast<std::uint8_t>(options.language));
                    if (std::any_of(map->effective_families.begin(), map->effective_families.end(),
                        [&](const auto& entry) { return entry.chunk_id == family_choice->family_chunk_id; }))
                        throw odh::FormatError("archived family remained visible after map refresh");
                    continue;
                }
                if (!odh_host::show_move_in_confirmation(*window, *chosen_family,
                        static_cast<std::uint16_t>(house), purchase_price,
                        static_cast<std::uint8_t>(options.language), move_in_title,
                        move_in_question, confirm_yes, confirm_no)) {
                    std::cout << "{\"status\":\"cancelled\",\"stage\":\"move-in-confirmation\"}\n";
                    return 0;
                }

                // Re-read both player state and commercial inputs after the
                // confirmation, so stale selections cannot silently spend money.
                const auto latest_overlay = odh_host::read_linux_family_overlay_for_neighborhood(
                    userdata / "Neighborhood.iff");
                const auto latest_map = odh_host::prepare_neighborhood_map(userdata, graphics,
                    options.mode, latest_overlay ? &*latest_overlay : nullptr,
                    static_cast<std::uint8_t>(options.language));
                const auto latest_price = house_purchase_price(game, userdata,
                    static_cast<std::uint16_t>(house));
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
                const auto next_overlay = odh::plan_family_move_in_overlay(
                    latest_map.source_families, neighborhood_identity,
                    latest_overlay ? &*latest_overlay : nullptr, family_choice->family_chunk_id,
                    static_cast<std::uint16_t>(house), latest_price);
                odh_host::compare_exchange_linux_family_overlay_atomic(
                    odh_host::linux_family_overlay_path(neighborhood_identity),
                    latest_overlay, next_overlay);
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
        }
#endif

        // Only small launch options and archive paths survive each runtime. Every
        // destination rebuilds its session, resource plan and runtime from scratch.
        Options travel_options;
        travel_options.game_root = game;
        travel_options.mode = options.mode;
        travel_options.language = options.language;
        travel_options.single_resident_session = options.single_resident_session;
        travel_options.instruction_budget = options.instruction_budget;
        travel_options.audio_enabled=options.audio_enabled;
        travel_options.audio_disabled=options.audio_disabled;
        travel_options.audio_drain_ms=options.audio_drain_ms;
        travel_options.headless_travel_tick=options.headless_travel_tick;
        std::map<unsigned, std::filesystem::path> saved_lots;
        std::optional<Options> rollback_options;
        std::size_t headless_travel_index = 0;
        travel_options.headless_ticks = options.headless_ticks;
        travel_options.headless_travel_houses = options.headless_travel_houses;
        travel_options.save_on_exit = options.save_on_exit;
        for (;;) {
            try {
                if (!house || house > 98)
                    throw odh::FormatError("travel destination is outside the supported lot range");
                if (options.load_session && !options.restore_bundle)
                    load_travel_session(options);
                const auto session = odh::load_session_entry(userdata, options.mode,
                    static_cast<std::uint16_t>(house), static_cast<std::uint8_t>(options.language),
                    family_overlay ? &*family_overlay : nullptr, player_character_directory);
                if (!options.single_resident_session && session.members.size() > 8)
                    throw odh::FormatError("integrated household exceeds the eight-resident TS1 runtime profile");
                const odh::SessionEntryMember* resident = nullptr;
                if (options.resident_id) {
                    const auto found = std::find_if(session.members.begin(), session.members.end(),
                        [&](const auto& member) { return member.neighbor_id == *options.resident_id; });
                    if (found == session.members.end()) throw odh::FormatError("resident is not in the selected family");
                    resident = &*found;
                }
#ifdef ODH_HAVE_X11
                else if (!options.plan_only && !options.headless_ticks) {
                    const auto selected = odh_host::show_resident_picker(*window, session.members,
                        static_cast<std::uint8_t>(options.language));
                    if (!selected) {
                        if (rollback_options)
                            throw odh::FormatError("destination resident selection was cancelled");
                        std::cout << "{\"status\":\"cancelled\",\"stage\":\"resident\"}\n";
                        return 0;
                    }
                    resident = &session.members[*selected];
                }
#endif
                else resident = &session.members.front();
#ifdef ODH_HAVE_X11
                if (window) {
                    window->resize(800,600);
                    present_loading_screen(*window,static_cast<std::uint8_t>(options.language));
                }
#endif
                auto plan = build_runtime_arguments(options, game, house, session, *resident,
                    graphics_resource, player_character_directory);
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
                std::optional<odh::SessionResourceIdentity> verified_identity;
                if (options.restore_bundle) {
                    verified_identity = odh_host::identify_session_resources(game, plan.resources,
                        static_cast<std::uint8_t>(options.language), plan.expansion_mask);
                    const auto& bundle = *options.restore_bundle;
                    if (odh::encode_session_resource_identity(bundle.resources) !=
                        odh::encode_session_resource_identity(*verified_identity))
                        throw odh::FormatError("saved session game resources do not match the selected installation");
                    if (bundle.host.family_chunk_id != session.family_chunk_id ||
                        bundle.host.family_number != session.family_number || bundle.host.resident_guid != resident->guid)
                        throw odh::FormatError("saved session family or resident no longer matches the selected lot");
                    if (!bundle.household_roster.empty()) {
                        if (bundle.household_roster.size() != session.members.size())
                            throw odh::FormatError("saved household roster differs from the current FAMI family");
                        for (const auto& saved_member : bundle.household_roster) {
                            const auto found = std::find_if(session.members.begin(), session.members.end(),
                                [&](const auto& member) { return member.neighbor_id == saved_member.neighbor_id; });
                            if (found == session.members.end() || found->guid != saved_member.guid ||
                                found->object_definition_id != saved_member.object_definition_id ||
                                std::filesystem::path(found->character_source + "/" + found->character_asset) !=
                                    std::filesystem::path(saved_member.character_asset))
                                throw odh::FormatError("saved household member identity differs from the current FAMI family");
                        }
                    }
                }
                if (options.plan_only) {
                    const auto identity = odh_host::identify_session_resources(game, plan.resources,
                        static_cast<std::uint8_t>(options.language), plan.expansion_mask);
                    print_plan(options, house, session, *resident, plan, identity, game);
                    return 0;
                }
#ifdef ODH_HAVE_LINUX_USER_CONFIG
                odh_host::LinuxUserConfig saved;
                saved.language_code = static_cast<int>(options.language);
                saved.data_root = game;
                std::string config_error;
                if (!options.load_session && !options.headless_ticks && !odh_host::save_linux_user_config(saved, &config_error))
                    throw odh::FormatError("cannot save user settings: " + config_error);
#endif
                std::vector<char*> pointers;
                pointers.reserve(plan.arguments.size());
                for (const auto& argument : plan.arguments) pointers.push_back(const_cast<char*>(argument.c_str()));
                odh_host::IntegratedSessionContext session_context;
                options.restore_bundle.reset(); // Runtime revalidates its candidate independently.
                session_context.resources = verified_identity;
                session_context.restore_archive = std::move(options.restore_archive);
                session_context.save_on_exit = options.save_on_exit;
                session_context.single_resident_session = options.single_resident_session;
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
                session_context.bounded_headless = options.headless_ticks.has_value();
                session_context.game_root = game;
                session_context.player_character_root = player_character_directory;
                session_context.resource_paths = plan.resources;
                session_context.language_code = static_cast<std::uint8_t>(options.language);
                session_context.expansion_mask = plan.expansion_mask;
                session_context.neighborhood_mode = static_cast<std::uint8_t>(options.mode);
                session_context.house_number = static_cast<std::uint16_t>(house);
                session_context.family_chunk_id = session.family_chunk_id;
                session_context.family_number = session.family_number;
                session_context.resident_guid = resident->guid;
                session_context.resident_neighbor_id = resident->neighbor_id;
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
                if (result.exit_code != 0) {
                    if (!rollback_options) return result.exit_code;
                    throw odh::FormatError("destination runtime failed with exit code " + std::to_string(result.exit_code));
                }
                const bool scripted_transition_expected =
                    headless_travel_index < options.headless_travel_houses.size();
                if (!result.transition) {
                    if (scripted_transition_expected)
                        throw odh::FormatError("headless travel was not accepted by the active lot");
                    rollback_options.reset();
                    return result.exit_code;
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
                if (cached != saved_lots.end()) options.load_session = cached->second;
                house = destination;
            } catch (const std::exception& transition_error) {
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
