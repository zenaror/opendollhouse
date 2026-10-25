#include "odh/interaction.hpp"
#include "odh/pie_menu.hpp"
#include "odh/ui_bitmap.hpp"
#include "odh/entity_scheduler.hpp"
#include "odh/entity_thread_controllers.hpp"
#include "odh/sleep.hpp"
#include "odh/snap.hpp"
#include "odh/object_type.hpp"
#include "odh/notify.hpp"
#include "odh/tuning.hpp"
#include "odh/lot.hpp"
#include "odh/lot_context.hpp"
#include "odh/random.hpp"
#include "odh/room_score.hpp"
#include "odh/motive_change.hpp"
#include "odh/ts1_budget.hpp"
#include "odh/avatar_body.hpp"
#include "odh/avatar_startup.hpp"
#include "odh/object_startup.hpp"
#include "odh/create_object_instance.hpp"
#include "odh/object_runtime_resources.hpp"
#include "odh/neighbor_context.hpp"
#include "odh/avatar_neighbor.hpp"
#include "odh/relationship.hpp"
#include "odh/bootstrap.hpp"
#include "odh/entity_next.hpp"
#include "odh/inventory_find.hpp"
#include "odh/saved_objects.hpp"
#include "odh/definition_index.hpp"
#include "odh/objm_common.hpp"
#include "odh/objm_tail.hpp"
#include "odh/entry_points.hpp"
#include "odh/family.hpp"
#include "odh/session_entry.hpp"
#include "odh/session_bundle.hpp"
#include "odh/single_resident_session_restore.hpp"
#include "odh/household_session_restore.hpp"
#include "odh/resident_selection.hpp"
#include "odh/sha256.hpp"
#include "odh/seated_session_continuation.hpp"
#include "odh/objm.hpp"
#include "odh/playback.hpp"
#include "odh/sound.hpp"
#include "odh/look_towards.hpp"
#include "odh/distance_to.hpp"
#include "odh/direction_to.hpp"
#include "odh/change_suit.hpp"
#include "odh/slot_scoring.hpp"
#include "odh/world_room_info.hpp"
#include "odh/saved_attempt_walk_obstacles.hpp"
#include "odh/saved_collision_candidates.hpp"
#include "odh/saved_slot_goal_selection.hpp"
#include "odh/collision.hpp"
#include "runtime_attempt_walk_probe.hpp"
#include "odh/world_avatar_place_audit.hpp"
#include "odh/entry_point5_readonly.hpp"
#include "odh/floor_placement.hpp"
#include "odh/avatar_candidate_preflight.hpp"
#include "odh/route_geometry.hpp"
#include "odh/rect_router.hpp"
#include "odh/routing.hpp"
#include "odh/route.hpp"
#include "odh/world_room_route_grid.hpp"
#include "household_go_here_session.hpp"
#include "room_portal_session.hpp"
#include "household_runtime_arrival.hpp"
#include "animation_catalog.hpp"
#include "resource.hpp"
#include "person_session.hpp"
#include "status_hud.hpp"
#include "json.hpp"
#include "saved_tail_json.hpp"
#include "saved_appearance.hpp"
#include "saved_resources.hpp"
#include "saved_interactions.hpp"
#include "saved_wall_appearance.hpp"
#include "saved_portal.hpp"
#include "saved_lot_renderer.hpp"
#include "person_runtime.hpp"
#ifdef __linux__
#include "linux_family_overlay_store.hpp"
#endif
#include "linux_window.hpp"
#include "session_resource_manifest.hpp"
#ifdef __linux__
#include "linux_session_archive_store.hpp"
#endif
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
#include "linux_text_codec.hpp"
#endif
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
#include "linux_text_renderer.hpp"
#include "linux_motive_panel.hpp"
#include "linux_household_selector.hpp"
#endif
#ifdef ODH_HAVE_LINUX_SESSION_LOG
#include "linux_session_log.hpp"
#endif
#include <iostream>
#include <memory>
#include <charconv>
#include <algorithm>
#include <limits>
#include <map>
#include <tuple>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <thread>
#include <sstream>
#include <string_view>

namespace {
struct Resource {std::string archive,entry;bool standalone=false;std::shared_ptr<const std::string> frozen{};};
struct PersonDefinitionSource {std::string path,entry;bool standalone=false;};
constexpr std::size_t person_definition_source_limit=128;
template<class F>void with_resource(const Resource& resource,F read){
    if(resource.frozen){std::istringstream input(*resource.frozen,std::ios::binary);read(odh::View(input,resource.frozen->size()));}
    else if(resource.standalone)with_iff_file(resource.archive,read);else with_far_entry(resource.archive,resource.entry,read);
}
struct Loaded {
    odh::RoutineSet routines;
    odh::TuningTables tuning;
    std::optional<odh::SlotTable> routing_slots;
    std::optional<odh::ObjdBinding> binding;
    std::optional<odh::ObjdFunctions> functions;
    std::optional<odh::ObjdStartup> startup;
    std::optional<odh::EntryPointTable> entry_points;
    std::optional<odh::Glob> glob;
    std::optional<std::uint16_t> object_type;
};
void different_render_path(const std::string& output,const std::string& input) {
    if(std::filesystem::weakly_canonical(output)==std::filesystem::weakly_canonical(input)||
       (std::filesystem::exists(output)&&std::filesystem::exists(input)&&std::filesystem::equivalent(output,input)))
        throw odh::FormatError("person frame output aliases an input");
}
std::string fold(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return s;}
#ifdef ODH_HAVE_X11
std::string loading_lot_message(std::uint8_t language){
    static constexpr std::array<std::string_view,20> messages{
        "Loading lot...","Loading lot...","Chargement du terrain...","Grundstück wird geladen...",
        "Caricamento del lotto...","Cargando el solar...","Kavel wordt geladen...","Indlæser grund...",
        "Laddar tomten...","Laster tomten...","Tonttia ladataan...","טוען את המגרש...",
        "Загрузка участка...","Carregando o lote...","区画を読み込んでいます...","Wczytywanie parceli...",
        "正在加载地段...","正在載入地段...","กำลังโหลดพื้นที่...","부지를 불러오는 중..."};
    return language>=1&&language<=messages.size()?std::string(messages[language-1]):std::string("Loading lot...");
}
odh::RasterFrame make_loading_lot_frame(std::uint32_t width,std::uint32_t height){
    odh::RasterFrame frame{};frame.width=width;frame.height=height;
    const auto count=static_cast<std::size_t>(width)*height;
    frame.pixels.assign(count,{21,31,41,255});
    frame.depth.assign(count,std::numeric_limits<float>::infinity());
    const auto rectangle=[&](std::int32_t x,std::int32_t y,std::int32_t w,std::int32_t h,
                             std::array<std::uint8_t,4> color){
        const auto left=std::clamp<std::int32_t>(x,0,static_cast<std::int32_t>(width));
        const auto top=std::clamp<std::int32_t>(y,0,static_cast<std::int32_t>(height));
        const auto right=std::clamp<std::int32_t>(x+w,0,static_cast<std::int32_t>(width));
        const auto bottom=std::clamp<std::int32_t>(y+h,0,static_cast<std::int32_t>(height));
        for(auto row=top;row<bottom;++row)for(auto column=left;column<right;++column)
            frame.pixels[static_cast<std::size_t>(row)*width+static_cast<std::size_t>(column)]=color;
    };
    const auto card_width=std::min<std::uint32_t>(width,480);
    const auto card_height=std::min<std::uint32_t>(height,88);
    const auto card_x=static_cast<std::int32_t>((width-card_width)/2);
    const auto card_y=static_cast<std::int32_t>((height-card_height)/2);
    rectangle(card_x,card_y,static_cast<std::int32_t>(card_width),static_cast<std::int32_t>(card_height),
              {39,52,63,255});
    rectangle(card_x,card_y,static_cast<std::int32_t>(card_width),3,{82,186,170,255});
    return frame;
}
void present_loading_lot(odh_host::LinuxWindow& window,std::uint8_t language){
    auto frame=make_loading_lot_frame(window.width(),window.height());
    const auto message=loading_lot_message(language);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    const auto card_width=std::min<std::uint32_t>(frame.width,480);
    const auto card_height=std::min<std::uint32_t>(frame.height,88);
    odh_host::TextOverlayStyle style;
    style.x=static_cast<std::int32_t>((frame.width-card_width)/2+20);
    style.y=static_cast<std::int32_t>((frame.height-card_height)/2+(card_height-36)/2);
    style.width=static_cast<std::int32_t>(card_width-40);style.height=36;style.font_size_px=22;
    style.padding=2;
    style.background={39,52,63,0};style.foreground={240,245,247,255};
    (void)odh_host::draw_text_overlay(frame,message,language,style);
#endif
    window.set_title("OpenDollhouse | "+message);
    window.present(frame);
}
#endif
odh::SessionActionRoutineSource load_session_action_source(
    const odh::DefinitionCandidate& candidate,std::uint16_t root,
    const std::optional<odh::SessionLinkedSemiglobal>& expected_link,
    const std::filesystem::path& game_root,const std::optional<Resource>& globals,
    const odh::DefinitionIndex* definitions) {
    const auto scope=odh::routine_key(root).scope;
    if(scope==odh::RoutineScope::private_object){
        if(expected_link)throw odh::FormatError("private seated action unexpectedly declares a semiglobal bank");
        return {load_saved_action_routines(candidate,root),{}};
    }
    if(scope!=odh::RoutineScope::semiglobal)
        throw odh::FormatError("seated action root must belong to the exact private or linked semiglobal bank");
    if(!globals||globals->standalone||!definitions)
        throw odh::FormatError("seated semiglobal action needs the explicit Global FAR and definition index");
    auto catalog=load_saved_tree_name_catalog(candidate);
    auto semiglobal_name=unique_saved_semiglobal_name(catalog);
    if(!semiglobal_name&&catalog.semiglobal_bindings==0&&
       candidate.definition.tree_table_id==0xffff&&candidate.definition.master_id!=0){
        if(const auto* master=definitions->unique_master(candidate)){
            catalog=load_saved_tree_name_catalog(*master);
            semiglobal_name=unique_saved_semiglobal_name(catalog);
        }
    }
    if(!semiglobal_name||semiglobal_name->empty()||
       semiglobal_name->find_first_of("/\\:")!=std::string::npos||
       semiglobal_name->find('\0')!=std::string::npos||semiglobal_name->find('.')!=std::string::npos)
        throw odh::FormatError("seated semiglobal action has no unique flat GLOB link");
    const auto entry=*semiglobal_name+".iff";
    auto bank=load_saved_semiglobal_code_bank(globals->archive,entry);
    if(bank.name!=*semiglobal_name||bank.entry!=entry||!bank.routines.count(odh::routine_key(root)))
        throw odh::FormatError("seated action root is absent from its exact GLOB-linked bank");
    std::error_code error;
    const auto root_path=std::filesystem::canonical(game_root,error);
    if(error)throw odh::FormatError("cannot resolve selected game root for seated code provenance");
    const auto archive_path=std::filesystem::canonical(globals->archive,error);
    if(error)throw odh::FormatError("cannot resolve linked semiglobal archive");
    const auto relative=archive_path.lexically_relative(root_path);
    if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")
        throw odh::FormatError("linked semiglobal archive is outside the selected game root");
    odh::SessionLinkedSemiglobal link{*semiglobal_name,relative.generic_string(),entry};
    if(expected_link&&*expected_link!=link)
        throw odh::FormatError("seated semiglobal GLOB or archive provenance changed");
    auto routines=load_saved_private_routines(candidate,256);
    if(bank.routines.size()>512||routines.size()+bank.routines.size()>768)
        throw odh::FormatError("seated private/semiglobal owner exceeds routine budget");
    for(const auto& [key,routine]:bank.routines)
        if(!routines.emplace(key,routine).second)
            throw odh::FormatError("private and linked semiglobal seated routines collide");
    return {std::move(routines),std::move(link)};
}
const char* room_portal_route_status_name(odh::RoomPortalRouteStatus status) {
    switch (status) {
    case odh::RoomPortalRouteStatus::found: return "found";
    case odh::RoomPortalRouteStatus::no_path: return "no_path";
    case odh::RoomPortalRouteStatus::invalid_input: return "invalid_input";
    case odh::RoomPortalRouteStatus::budget_exhausted: return "budget_exhausted";
    }
    return "invalid_input";
}
Loaded load(const Resource& resource,odh::RoutineScope role,std::optional<std::uint16_t> definition={},bool startup=false) {
    Loaded result;
    with_resource(resource,[&](const auto& view){
        std::optional<odh::View> objd_payload,objf_payload;std::size_t objf_count=0;
        odh::iff_chunks(view,[&](const auto& chunk){
            if(chunk.type=="BCON") {
                if(!result.tuning.emplace(chunk.id,odh::read_bcon(chunk.payload)).second)throw odh::FormatError("ambiguous BCON ID in explicit resource");
            }
            if(chunk.type=="BHAV") {
                const auto key=odh::routine_key(chunk.id);
                if(key.scope!=role)throw odh::FormatError("BHAV outside explicit resource role");
                if(!result.routines.emplace(key,odh::read_bhav(chunk.payload)).second)throw odh::FormatError("ambiguous BHAV");
            }
            if(role==odh::RoutineScope::global&&chunk.type=="SLOT"&&chunk.id==100){
                if(result.routing_slots)throw odh::FormatError("ambiguous global SLOT100");
                result.routing_slots=odh::read_slot(chunk.payload);
            }
            if(definition&&chunk.type=="OBJf"&&chunk.id==*definition){
                ++objf_count;if(!objf_payload)objf_payload=chunk.payload;
            }
            if(definition&&chunk.type=="OBJD"&&chunk.id==*definition){
                if(result.binding)throw odh::FormatError("ambiguous OBJD");
                objd_payload=chunk.payload;
                result.binding=odh::read_objd_binding(chunk.payload);result.functions=odh::read_objd_functions(chunk.payload);
                if(startup)result.startup=odh::read_objd_startup(chunk.payload);
                if(result.binding->version==138)result.object_type=chunk.payload.le16(18);
            }
            if(role==odh::RoutineScope::private_object&&chunk.type=="GLOB"){
                if(result.glob)throw odh::FormatError("ambiguous GLOB link");result.glob=odh::read_glob(chunk.payload);
            }
        });
        if(result.functions){
            if(result.functions->uses_function_table!=0){
                if(objf_count!=1)throw odh::FormatError(objf_count?"ambiguous selected OBJf":"selected OBJf absent");
                result.entry_points=odh::read_objf(*objf_payload);
            }else if(startup||(result.binding->version==138&&objd_payload->size()>=162))result.entry_points=odh::read_objd_entry_points(*objd_payload);
        }
    });
    if(definition&&(!result.binding||!result.functions))throw odh::FormatError("selected OBJD absent");
    if(result.binding&&result.binding->master_id!=0)throw odh::FormatError("multitile identity unsupported");
    return result;
}
std::map<std::uint16_t,odh::SoundEvent> load_sounds(const Resource& resource) {
    std::map<std::uint16_t,odh::SoundEvent> result;
    with_resource(resource,[&](const auto& view){
        odh::iff_chunks(view,[&](const auto& chunk){
            if(chunk.type=="FWAV"&&!result.emplace(chunk.id,odh::read_fwav(chunk.payload)).second)
                throw odh::FormatError("ambiguous FWAV ID in explicit resource");
        });
    });
    return result;
}
odh::TuningTables load_saved_action_tuning(const odh::DefinitionCandidate& candidate){
    odh::TuningTables result;
    const Resource resource{candidate.source,candidate.asset,candidate.asset.empty()};
    with_resource(resource,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type=="BCON"&&!result.emplace(chunk.id,odh::read_bcon(chunk.payload)).second)
            throw odh::FormatError("ambiguous BCON ID in selected action resource");
    });});
    return result;
}
std::map<std::uint16_t,odh::StringTable> load_strings(const Resource& resource,const std::vector<std::uint16_t>& ids){
    std::map<std::uint16_t,odh::StringTable> result;
    with_resource(resource,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type!="STR#"||std::find(ids.begin(),ids.end(),chunk.id)==ids.end())return;
        if(!result.emplace(chunk.id,odh::read_strings(chunk.payload)).second)throw odh::FormatError("ambiguous startup STR# ID");
    });});return result;
}
std::int16_t signed_word(std::uint16_t v){return static_cast<std::int16_t>(v<32768?static_cast<int>(v):static_cast<int>(v)-65536);}
std::uint64_t seed_selector(const std::string& text){
    if(text.empty())throw odh::FormatError("empty RNG seed");
    const bool hex=text.size()>2&&text[0]=='0'&&(text[1]=='x'||text[1]=='X');
    std::uint64_t value=0;const auto result=std::from_chars(text.data()+(hex?2:0),text.data()+text.size(),value,hex?16:10);
    if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size())throw odh::FormatError("RNG seed outside uint64 profile");
    return value;
}
std::uint32_t guid_selector(const std::string& text){
    if(text.empty())throw odh::FormatError("empty saved resident GUID");
    std::uint32_t value=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size()||value==0)
        throw odh::FormatError("saved resident GUID outside uint32 profile");
    return value;
}
std::int32_t coordinate_selector(const char* text){
    const std::string value=text;
    if(value.empty())throw odh::FormatError("empty placement candidate coordinate");
    std::int32_t parsed=0;const auto result=std::from_chars(value.data(),value.data()+value.size(),parsed);
    if(result.ec!=std::errc{}||result.ptr!=value.data()+value.size()||parsed<-(1<<20)||parsed>(1<<20))
        throw odh::FormatError("placement candidate coordinate outside signed bounded profile");
    return parsed;
}
const char* floor_result_name(odh::FloorPlacementResult result){
    switch(result){
    case odh::FloorPlacementResult::allowed:return "allowed";
    case odh::FloorPlacementResult::cant_place_on_water:return "cant_place_on_water";
    case odh::FloorPlacementResult::must_place_on_pool:return "must_place_on_pool";
    case odh::FloorPlacementResult::must_place_on_water:return "must_place_on_water";
    case odh::FloorPlacementResult::not_allowed_on_terrain:return "not_allowed_on_terrain";
    case odh::FloorPlacementResult::cant_place_in_air:return "cant_place_in_air";
    case odh::FloorPlacementResult::not_allowed_on_floor:return "not_allowed_on_floor";
    }
    return "invalid_result";
}
std::string bytes_hex(const std::vector<std::uint8_t>& bytes){
    constexpr char digits[]="0123456789abcdef";
    std::string result;result.reserve(bytes.size()*2);
    for(const auto byte:bytes){result.push_back(digits[byte>>4]);result.push_back(digits[byte&15]);}
    return result;
}
}
odh_host::PersonRunResult odh_person_run(int argc,char** argv,odh_host::LinuxWindow* shared_window,
                   odh_host::NeighborhoodResumeCallback neighborhood_resume,
                   std::optional<odh_host::IntegratedSessionContext> session_context) {
#ifdef ODH_HAVE_LINUX_SESSION_LOG
 odh_host::LinuxSessionLog session_log;
#endif
 try {
    const bool profile_startup=std::getenv("ODH_PROFILE_STARTUP")!=nullptr;
    const auto profile_started=std::chrono::steady_clock::now();
    auto profile_previous=profile_started;
    const auto profile_mark=[&](const char* phase){
#ifdef ODH_HAVE_LINUX_SESSION_LOG
        session_log.stage(phase);
#endif
        if(!profile_startup)return;
        const auto now=std::chrono::steady_clock::now();
        const auto phase_ms=std::chrono::duration_cast<std::chrono::milliseconds>(now-profile_previous).count();
        const auto total_ms=std::chrono::duration_cast<std::chrono::milliseconds>(now-profile_started).count();
        std::cerr<<"[odh-profile] phase="<<phase<<" elapsed_ms="<<phase_ms<<" total_ms="<<total_ms<<'\n';
        profile_previous=now;
    };
    std::optional<Resource> avatar,semi,globals,action,second_avatar;
    std::optional<std::uint16_t> second_avatar_def;
    std::vector<PersonDefinitionSource> definition_sources;
    std::vector<std::string> animation_sources;
    std::vector<odh::ArchitectureResourceSource> architecture_sources;
    std::optional<std::string> lot_path,zoning_path,neighborhood_path;
    std::optional<std::uint16_t> lot_id;
    std::optional<std::uint64_t> seed;
    std::optional<std::uint16_t> avatar_def,action_def,action_routine,flags,age,routine;
    std::optional<std::uint16_t> render_tick;
    std::optional<std::uint16_t> render_saved_id;
    std::optional<std::uint32_t> render_saved_guid;
    std::optional<odh::EntityPosition> placement_candidate;
    std::optional<odh::RectRoutePoint> attempt_walk_destination;
    struct HeadlessFixtureAvatar {std::uint16_t id;odh::EntityPosition position;};
    std::optional<HeadlessFixtureAvatar> headless_fixture_avatar;
    std::optional<HeadlessFixtureAvatar> headless_fixture_occupant_avatar;
    struct HeadlessSecondMain {HeadlessFixtureAvatar avatar;std::uint16_t routine;};
    std::optional<HeadlessSecondMain> headless_second_main;
    bool second_saved_main=false;
    std::optional<std::uint16_t> headless_second_main_arg0;
    std::optional<std::uint16_t> headless_fixture_main_arg0;
    std::optional<std::int16_t> headless_stack_object;
    struct HeadlessEntityMove {std::int16_t id;odh::EntityPosition position;std::uint64_t tick;};
    std::optional<HeadlessEntityMove> headless_entity_move;
    bool headless_entity_move_applied=false;
    struct HeadlessInteraction {std::int16_t id;std::uint64_t tick;};
    struct HeadlessWindowMenuStep {
        enum class Kind {open_category,back,select_action};
        Kind kind;std::size_t index=0;
    };
    std::vector<HeadlessWindowMenuStep> headless_window_menu_steps;
    std::size_t headless_window_menu_step_index=0;
    bool headless_window_menu_script_complete=false;
    std::vector<std::vector<std::string>> headless_window_menu_path_history;
    std::optional<std::int16_t> headless_window_interaction;
    std::optional<std::uint64_t> headless_window_stand_tick;
    struct HeadlessWindowGoHere {std::uint32_t x,y;std::uint8_t level;std::uint64_t tick;};
    std::optional<HeadlessWindowGoHere> headless_window_go_here;
    bool headless_window_go_here_input_sent=false;
    std::optional<std::array<std::int32_t,2>> headless_window_go_here_click;
    std::optional<SavedGroundSelection> headless_window_go_here_ground;
    std::optional<std::uint64_t> headless_window_neighborhood_tick;
    bool headless_window_stand_input_sent=false,headless_window_neighborhood_input_sent=false;
    std::optional<std::uint16_t> headless_window_neighborhood_house;
    bool neighborhood_resume_requested=false,neighborhood_resume_same_lot=false;
    std::uint64_t neighborhood_resume_tick_before=0,neighborhood_resume_tick_after=0;
    std::uint64_t neighborhood_resume_vm_steps_before=0,neighborhood_resume_vm_steps_after=0;
    bool neighborhood_resume_world_unchanged=false,neighborhood_resume_relationships_unchanged=false;
    std::optional<std::int16_t> neighborhood_resume_posture_before,neighborhood_resume_posture_after;
    std::optional<std::int16_t> neighborhood_resume_attachment_before,neighborhood_resume_attachment_after;
    // Synthetic WindowInput events enter the same dispatch as the X11 window;
    // they do not emulate Xlib's native key/button translation.
    std::vector<odh_host::WindowInput> headless_window_inputs;
    std::size_t headless_window_input_index=0;
    std::optional<std::array<std::int32_t,2>> headless_window_object_click_pixel;
    bool headless_window_object_click_queued=false;
    std::size_t headless_window_menu_click_count=0;
    bool headless_window_menu_empty=false;
    struct HeadlessGoHere {std::uint32_t x,y;std::uint8_t level;std::uint64_t tick;};
    struct HeadlessResidentGoHere {
        std::int16_t actor;std::uint32_t x,y;std::uint8_t level;std::uint64_t tick;bool applied=false;
    };
    std::vector<HeadlessResidentGoHere> headless_resident_go_here;
    std::optional<std::uint64_t> headless_go_here_interrupt_tick;
    std::optional<std::uint64_t> headless_go_here_interrupt_after_movements;
    bool headless_go_here_interrupt_applied=false,headless_go_here_interrupt_accepted=false;
    std::optional<HeadlessInteraction> headless_interaction;
    std::optional<std::int16_t> headless_interaction_actor;
    bool headless_interaction_applied=false;
    std::optional<HeadlessGoHere> headless_go_here;
    bool headless_go_here_applied=false;
    std::optional<HeadlessInteraction> headless_portal;
    bool headless_portal_applied=false;
    std::string headless_portal_status="not_requested",headless_portal_diagnostic;
    std::optional<std::uint16_t> headless_portal_action;
    std::optional<odh::CodeOwner> headless_portal_owner;
    std::optional<std::uint64_t> headless_portal_previous_completion;
    std::optional<bool> headless_portal_result;
    std::optional<odh::EntityPosition> headless_portal_actor_start_position;
    std::optional<odh::EntityPosition> headless_portal_start_position;
    std::optional<HeadlessInteraction> headless_stand;
    bool headless_stand_applied=false,headless_stand_precondition_met=false;
    std::string stand_trigger;
    std::uint64_t stand_request_tick=0;
    std::int16_t stand_request_target=0;
    std::string headless_stand_status="not_requested",headless_stand_diagnostic;
    std::optional<std::uint16_t> headless_stand_action;
    std::optional<odh::CodeOwner> headless_stand_owner;
    std::optional<std::uint64_t> headless_stand_previous_completion;
    std::optional<bool> headless_stand_result;
    std::optional<std::int16_t> headless_stand_posture_before,headless_stand_attachment_before;
    bool render_window=false,saved_world_main=false,headless_main=false;
    std::uint8_t game_language=1;bool game_language_set=false;
    std::optional<std::string> render_output,render_animation_far,render_textures_far,render_skeleton_entry,render_skeleton_name;
    std::optional<std::string> ui_graphics_far;
    std::optional<std::array<std::uint32_t,2>> render_size;
    bool render_hide_lot_objects=false;
    std::optional<std::string> render_floors,render_walls;
    SavedLotView render_lot_view;
    bool render_level_set=false,render_wall_mode_set=false;
    std::uint16_t ticks_limit=256;bool ticks_set=false,initialize=false,activate_family=false;
    std::optional<std::uint64_t> probe_instruction_limit;
    for(int n=1;n<argc;++n){const std::string option=argv[n];
        if(option=="--avatar"&&!avatar&&n+3<argc){avatar=Resource{argv[n+1],argv[n+2]};avatar_def=selector(argv[n+3]);n+=3;}
        else if(option=="--avatar-file"&&!avatar&&n+2<argc){avatar=Resource{argv[n+1],"",true};avatar_def=selector(argv[n+2]);n+=2;}
        else if(option=="--semiglobal"&&!semi&&n+2<argc){semi=Resource{argv[n+1],argv[n+2]};n+=2;}
        else if(option=="--global"&&!globals&&n+2<argc){globals=Resource{argv[n+1],argv[n+2]};n+=2;}
        else if(option=="--action"&&!action&&n+4<argc){action=Resource{argv[n+1],argv[n+2]};action_def=selector(argv[n+3]);action_routine=selector(argv[n+4]);n+=4;}
        else if(option=="--ticks"&&!ticks_set&&n+1<argc){ticks_limit=selector(argv[++n]);ticks_set=true;}
        else if(option=="--instruction-budget"&&!probe_instruction_limit&&n+1<argc){
            const std::string text=argv[++n];std::uint64_t value=0;
            const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
            if(text.empty()||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||
               value==0||value>odh_host::PersonSessionPolicy::max_probe_instructions)
                throw odh::FormatError("instruction budget must be within the bounded range 1..1000000");
            probe_instruction_limit=value;
        }
        else if(option=="--language"&&!game_language_set&&n+1<argc){const auto value=selector(argv[++n]);
            if(value<1||value>20)throw odh::FormatError("language code outside the available STR range 1..20");
            game_language=static_cast<std::uint8_t>(value);game_language_set=true;}
        else if(option=="--flags"&&!flags&&n+1<argc)flags=selector(argv[++n]);
        else if(option=="--age"&&!age&&n+1<argc)age=selector(argv[++n]);
        else if(option=="--routine"&&!routine&&n+1<argc)routine=selector(argv[++n]);
        else if(option=="--lot"&&!lot_path&&n+2<argc){lot_path=argv[n+1];lot_id=selector(argv[n+2]);n+=2;}
        else if(option=="--zoning"&&!zoning_path&&n+1<argc)zoning_path=argv[++n];
        else if(option=="--seed"&&!seed&&n+1<argc)seed=seed_selector(argv[++n]);
        else if(option=="--initialize"&&!initialize)initialize=true;
        else if(option=="--activate-family"&&!activate_family)activate_family=true;
        else if(option=="--neighborhood"&&!neighborhood_path&&n+1<argc)neighborhood_path=argv[++n];
        else if(option=="--animation-source"&&n+1<argc){
            if(animation_sources.size()>=128)throw odh::FormatError("animation source count exceeds bounded host profile");
            animation_sources.emplace_back(argv[++n]);
            if(animation_sources.back().size()>4096)throw odh::FormatError("animation source path exceeds bounded host profile");
        }
        else if(option=="--architecture-resource"&&n+1<argc){
            if(architecture_sources.size()>=256)
                throw odh::FormatError("architecture resource source count exceeds bounded host profile");
            const std::string path=argv[++n];
            if(path.empty()||path.size()>4096)
                throw odh::FormatError("architecture resource path outside bounded host profile");
            const auto extension=fold(std::filesystem::path(path).extension().string());
            if(extension!=".far"&&extension!=".flr"&&extension!=".wll")
                throw odh::FormatError("architecture resource requires an explicit .flr, .wll or .far file");
            architecture_sources.push_back({path,extension==".far"?
                odh::ArchitectureResourceSourceKind::far1_archive:odh::ArchitectureResourceSourceKind::loose_file});
        }
        else if(option=="--render-frame"&&!render_output&&n+1<argc){render_output=argv[++n];}
        else if(option=="--render-hide-lot-objects"&&!render_hide_lot_objects){render_hide_lot_objects=true;}
        else if(option=="--render-size"&&!render_size&&n+2<argc){
            const auto width=selector(argv[++n]),height=selector(argv[++n]);
            if(width<160||width>1024||height<120||height>1024||
               static_cast<std::uint64_t>(width)*height>1048576)
                throw odh::FormatError("render size outside bounded viewport (160..1024 by 120..1024, at most 1048576 pixels)");
            render_size=std::array<std::uint32_t,2>{width,height};
        }
        else if(option=="--render-animation-far"&&!render_animation_far&&n+1<argc){render_animation_far=argv[++n];}
        else if(option=="--render-textures"&&!render_textures_far&&n+1<argc){render_textures_far=argv[++n];}
        else if(option=="--render-skeleton"&&!render_skeleton_entry&&n+2<argc){render_skeleton_entry=argv[n+1];render_skeleton_name=argv[n+2];n+=2;}
        else if(option=="--render-tick"&&!render_tick&&n+1<argc){render_tick=selector(argv[++n]);}
        else if(option=="--render-saved-avatar"&&!render_saved_id&&n+1<argc){render_saved_id=selector(argv[++n]);}
        else if(option=="--render-saved-guid"&&!render_saved_guid&&n+1<argc){render_saved_guid=guid_selector(argv[++n]);}
        else if(option=="--headless-fixture-avatar"&&!headless_fixture_avatar&&n+5<argc){
            const auto id=selector(argv[++n]);
            const auto x=coordinate_selector(argv[++n]),y=coordinate_selector(argv[++n]);
            const auto level=selector(argv[++n]),direction=selector(argv[++n]);
            if(id==0||id>32767||x<0||x>32767||y<0||y>32767||level<1||level>2||direction>7)
                throw odh::FormatError("headless fixture avatar is outside the supported World profile");
            headless_fixture_avatar=HeadlessFixtureAvatar{static_cast<std::uint16_t>(id),
                {x,y,static_cast<std::int16_t>(level),static_cast<std::uint8_t>(direction)}};
        }
        else if(option=="--headless-fixture-occupant-avatar"&&!headless_fixture_occupant_avatar&&n+5<argc){
            const auto id=selector(argv[++n]);
            const auto x=coordinate_selector(argv[++n]),y=coordinate_selector(argv[++n]);
            const auto level=selector(argv[++n]),direction=selector(argv[++n]);
            if(id==0||id>32767||x<0||x>32767||y<0||y>32767||level<1||level>2||direction>7)
                throw odh::FormatError("headless occupant avatar is outside the supported World profile");
            headless_fixture_occupant_avatar=HeadlessFixtureAvatar{static_cast<std::uint16_t>(id),
                {x,y,static_cast<std::int16_t>(level),static_cast<std::uint8_t>(direction)}};
        }
        else if(option=="--headless-second-main"&&!headless_second_main&&n+6<argc){
            const auto id=selector(argv[++n]);
            const auto x=coordinate_selector(argv[++n]),y=coordinate_selector(argv[++n]);
            const auto level=selector(argv[++n]),direction=selector(argv[++n]),main_routine=selector(argv[++n]);
            if(id==0||id>32767||x<0||x>32767||y<0||y>32767||level<1||level>2||direction>7||main_routine==0)
                throw odh::FormatError("headless second avatar Main is outside the supported World profile");
            headless_second_main=HeadlessSecondMain{{static_cast<std::uint16_t>(id),
                {x,y,static_cast<std::int16_t>(level),static_cast<std::uint8_t>(direction)}},main_routine};
        }
        else if(option=="--headless-second-saved-main"&&!headless_second_main&&n+2<argc){
            const auto id=selector(argv[++n]),main=selector(argv[++n]);
            if(id==0||id>32767||main==0)throw odh::FormatError("second saved Main ID or routine invalid");
            headless_second_main=HeadlessSecondMain{{id,{}},main};second_saved_main=true;
        }
        else if(option=="--headless-second-avatar-file"&&!second_avatar&&n+2<argc){
            second_avatar=Resource{argv[++n],{},true};second_avatar_def=selector(argv[++n]);
        }
        else if(option=="--headless-second-main-arg0"&&!headless_second_main_arg0&&n+1<argc){
            const auto value=selector(argv[++n]);
            if(value>32767)throw odh::FormatError("second avatar Main Arg0 is outside 0..32767");
            headless_second_main_arg0=value;
        }
        else if(option=="--headless-fixture-main-arg0"&&!headless_fixture_main_arg0&&n+1<argc){
            const auto ticks=selector(argv[++n]);
            if(ticks>32767)throw odh::FormatError("fixture Main Arg0 is outside 0..32767");
            headless_fixture_main_arg0=ticks;
        }
        else if(option=="--attempt-walk-destination"&&!attempt_walk_destination&&n+2<argc){
            attempt_walk_destination=odh::RectRoutePoint{coordinate_selector(argv[++n]),coordinate_selector(argv[++n])};
        }
        else if(option=="--placement-candidate"&&!placement_candidate&&n+4<argc){
            const auto x=coordinate_selector(argv[++n]),y=coordinate_selector(argv[++n]);
            const auto level=selector(argv[++n]),direction=selector(argv[++n]);
            if(level<1||level>2||direction>7)throw odh::FormatError("placement candidate level/direction outside profile");
            placement_candidate=odh::EntityPosition{x,y,static_cast<std::int16_t>(level),static_cast<std::uint8_t>(direction)};
        }
        else if(option=="--render-window"&&!render_window){render_window=true;}
        else if(option=="--ui-graphics"&&!ui_graphics_far&&n+1<argc){
            ui_graphics_far=argv[++n];
            if(ui_graphics_far->empty())throw odh::FormatError("UI graphics archive path is empty");
        }
        else if(option=="--render-floors"&&!render_floors&&n+1<argc){render_floors=argv[++n];}
        else if(option=="--render-walls"&&!render_walls&&n+1<argc){render_walls=argv[++n];}
        else if(option=="--render-level"&&!render_level_set&&n+1<argc){
            const auto level=selector(argv[++n]);if(level<1||level>2)throw odh::FormatError("render level outside profile");
            render_lot_view.level=static_cast<std::uint8_t>(level);render_level_set=true;
        }
        else if(option=="--render-wall-mode"&&!render_wall_mode_set&&n+1<argc){
            const std::string mode=argv[++n];if(mode!="up"&&mode!="down")throw odh::FormatError("render wall mode must be up or down");
            render_lot_view.walls_down=mode=="down";render_wall_mode_set=true;
        }
        else if(option=="--render-terrain-theme"&&!render_lot_view.terrain_theme&&n+1<argc){
            const auto theme=selector(argv[++n]);if(theme>=8)throw odh::FormatError("render terrain theme outside profile");
            render_lot_view.terrain_theme=static_cast<odh::TerrainTheme>(theme);
        }
        else if(option=="--saved-world-main"&&!saved_world_main){saved_world_main=true;}
        else if(option=="--headless-main"&&!saved_world_main){saved_world_main=true;headless_main=true;}
        else if(option=="--headless-stack-object"&&!headless_stack_object&&n+1<argc){
            const auto value=selector(argv[++n]);
            if(value==0||value>32767)throw odh::FormatError("headless StackObject ID is outside the supported World range");
            headless_stack_object=static_cast<std::int16_t>(value);
        }
        else if(option=="--headless-interaction"&&!headless_interaction&&n+2<argc){
            const auto id=selector(argv[++n]),tick=selector(argv[++n]);
            if(id==0||id>32767||tick==0)throw odh::FormatError("headless interaction target/tick is outside the supported profile");
            headless_interaction=HeadlessInteraction{static_cast<std::int16_t>(id),tick};
        }
        else if(option=="--headless-resident-interaction"&&!headless_interaction&&n+3<argc){
            const auto actor=selector(argv[++n]),id=selector(argv[++n]),tick=selector(argv[++n]);
            if(actor==0||actor>32767||id==0||id>32767||tick==0)
                throw odh::FormatError("headless resident interaction actor/target/tick is outside the supported profile");
            headless_interaction_actor=static_cast<std::int16_t>(actor);
            headless_interaction=HeadlessInteraction{static_cast<std::int16_t>(id),tick};
        }
        else if(option=="--headless-window-interaction"&&!headless_window_interaction&&n+1<argc){
            const auto id=selector(argv[++n]);
            if(id==0||id>32767)throw odh::FormatError("headless window interaction target is outside the supported profile");
            headless_window_interaction=static_cast<std::int16_t>(id);
        }
        else if((option=="--headless-window-menu-open"||option=="--headless-window-menu-select")&&n+1<argc){
            const auto index=selector(argv[++n]);
            if(index>31||headless_window_menu_steps.size()>=32)
                throw odh::FormatError("headless menu script exceeds its bounded 32-step/index profile");
            headless_window_menu_steps.push_back({option=="--headless-window-menu-open"?
                HeadlessWindowMenuStep::Kind::open_category:HeadlessWindowMenuStep::Kind::select_action,
                static_cast<std::size_t>(index)});
        }
        else if(option=="--headless-window-menu-back"){
            if(headless_window_menu_steps.size()>=32)
                throw odh::FormatError("headless menu script exceeds its bounded 32-step profile");
            headless_window_menu_steps.push_back({HeadlessWindowMenuStep::Kind::back,0});
        }
        else if(option=="--headless-window-stand-tick"&&!headless_window_stand_tick&&n+1<argc){
            const auto tick=selector(argv[++n]);
            if(!tick)throw odh::FormatError("headless window Stand tick must be positive");
            headless_window_stand_tick=tick;
        }
        else if(option=="--headless-window-go-here"&&!headless_window_go_here&&n+4<argc){
            const auto x=coordinate_selector(argv[++n]),y=coordinate_selector(argv[++n]);
            const auto level=selector(argv[++n]),tick=selector(argv[++n]);
            if(x<0||y<0||x>2046||y>2046||level<1||level>2||tick==0)
                throw odh::FormatError("headless window Go Here tile/level/tick is outside the supported profile");
            headless_window_go_here=HeadlessWindowGoHere{static_cast<std::uint32_t>(x),
                static_cast<std::uint32_t>(y),static_cast<std::uint8_t>(level),tick};
        }
        else if(option=="--headless-window-neighborhood-tick"&&!headless_window_neighborhood_tick&&n+1<argc){
            const auto tick=selector(argv[++n]);
            if(!tick)throw odh::FormatError("headless window neighborhood tick must be positive");
            headless_window_neighborhood_tick=tick;
        }
        else if(option=="--headless-window-neighborhood-house"&&!headless_window_neighborhood_house&&n+1<argc){
            const auto house=selector(argv[++n]);
            if(!house||house>32767)throw odh::FormatError("headless window neighborhood house is outside the supported profile");
            headless_window_neighborhood_house=static_cast<std::uint16_t>(house);
        }
        else if(option=="--headless-go-here"&&!headless_go_here&&n+4<argc){
            const auto x=coordinate_selector(argv[++n]),y=coordinate_selector(argv[++n]);
            const auto level=selector(argv[++n]),tick=selector(argv[++n]);
            if(x<0||y<0||x>2046||y>2046||level<1||level>2||tick==0)
                throw odh::FormatError("headless Go Here tile/level/tick is outside the supported profile");
            headless_go_here=HeadlessGoHere{static_cast<std::uint32_t>(x),static_cast<std::uint32_t>(y),
                static_cast<std::uint8_t>(level),tick};
        }
        else if(option=="--headless-resident-go-here"&&n+5<argc){
            const auto id=selector(argv[++n]);
            const auto x=coordinate_selector(argv[++n]),y=coordinate_selector(argv[++n]);
            const auto level=selector(argv[++n]),tick=selector(argv[++n]);
            if(!id||id>32767||x<0||y<0||x>2046||y>2046||level<1||level>2||tick==0||
               headless_resident_go_here.size()>=7)
                throw odh::FormatError("resident Go Here identity/tile/level/tick is outside the supported profile");
            const auto actor=static_cast<std::int16_t>(id);
            if(std::any_of(headless_resident_go_here.begin(),headless_resident_go_here.end(),
                [&](const auto& action){return action.actor==actor;}))
                throw odh::FormatError("resident Go Here actor was scheduled more than once");
            headless_resident_go_here.push_back({actor,static_cast<std::uint32_t>(x),
                static_cast<std::uint32_t>(y),static_cast<std::uint8_t>(level),tick,false});
        }
        else if(option=="--headless-go-here-interrupt"&&!headless_go_here_interrupt_tick&&n+1<argc){
            const auto tick=selector(argv[++n]);
            if(tick==0)throw odh::FormatError("Go Here interrupt tick must be nonzero");
            headless_go_here_interrupt_tick=tick;
        }
        else if(option=="--headless-go-here-interrupt-after-movements"&&
                !headless_go_here_interrupt_after_movements&&n+1<argc){
            const auto count=selector(argv[++n]);
            if(count==0||count>128)
                throw odh::FormatError("Go Here movement cancel count must be between 1 and 128");
            headless_go_here_interrupt_after_movements=count;
        }
        else if(option=="--headless-portal"&&!headless_portal&&n+2<argc){
            const auto id=selector(argv[++n]),tick=selector(argv[++n]);
            if(id==0||id>32767||tick==0)throw odh::FormatError("headless Portal15 target/tick is outside the supported profile");
            headless_portal=HeadlessInteraction{static_cast<std::int16_t>(id),tick};
        }
        else if(option=="--headless-stand"&&!headless_stand&&n+2<argc){
            const auto id=selector(argv[++n]),tick=selector(argv[++n]);
            if(id==0||id>32767||tick==0)throw odh::FormatError("headless Stand target/tick is outside the supported profile");
            headless_stand=HeadlessInteraction{static_cast<std::int16_t>(id),tick};
        }
        else if(option=="--headless-move-entity"&&!headless_entity_move&&n+6<argc){
            const auto id=selector(argv[++n]);
            const auto x=coordinate_selector(argv[++n]),y=coordinate_selector(argv[++n]);
            const auto level=selector(argv[++n]),direction=selector(argv[++n]),tick=selector(argv[++n]);
            if(id==0||id>32767||x<0||x>32767||y<0||y>32767||level<1||level>2||direction>7||tick==0)
                throw odh::FormatError("headless entity move is outside the supported World profile");
            headless_entity_move=HeadlessEntityMove{static_cast<std::int16_t>(id),
                {x,y,static_cast<std::uint8_t>(level),static_cast<std::uint8_t>(direction)},tick};
        }
        else if(option=="--object-definition-entry"&&n+2<argc){
            if(definition_sources.size()>=person_definition_source_limit)throw odh::FormatError("definition source count exceeds bounded host profile");
            PersonDefinitionSource source;source.path=argv[++n];source.entry=argv[++n];
            if(source.path.empty()||source.path.size()>4096||source.entry.empty()||source.entry.size()>4096)
                throw odh::FormatError("object definition archive/entry path outside bounded host profile");
            definition_sources.push_back(std::move(source));
        }
        else if((option=="--object-definitions"||option=="--object-definition-file")&&n+1<argc){
            if(definition_sources.size()>=person_definition_source_limit)throw odh::FormatError("definition source count exceeds bounded host profile");
            PersonDefinitionSource source;source.path=argv[++n];source.standalone=option=="--object-definition-file";
            if(source.path.empty()||source.path.size()>4096)throw odh::FormatError("definition source path outside bounded host profile");
            definition_sources.push_back(std::move(source));
        }
        else throw odh::FormatError("invalid or repeated person host option");
    }
    if(!architecture_sources.empty()&&!render_floors)
        throw odh::FormatError("--architecture-resource requires --render-floors");
    if((render_floors&&(!saved_world_main||render_floors->empty()||render_floors->size()>4096))||
       (render_walls&&(!render_floors||render_walls->empty()||render_walls->size()>4096))||
       (render_level_set&&!render_floors)||(render_wall_mode_set&&!render_walls)||
       (render_lot_view.terrain_theme&&!render_floors))
        throw odh::FormatError("render lot options require --saved-world-main --render-floors; walls/view/theme need their explicit inputs");
    if(headless_interaction&&(!headless_main||headless_interaction->tick>ticks_limit))
        throw odh::FormatError("--headless-interaction requires --headless-main and a tick within --ticks");
    if(headless_interaction_actor&&(!session_context||session_context->single_resident_session||
       session_context->household_members.empty()))
        throw odh::FormatError("--headless-resident-interaction requires an integrated whole-household session");
    const bool go_here_after_stand=headless_go_here&&headless_interaction&&headless_stand&&
        !headless_portal&&headless_interaction->tick<headless_stand->tick&&
        headless_stand->tick<headless_go_here->tick;
    const bool standalone_go_here=headless_go_here&&!headless_interaction&&!headless_portal&&!headless_stand;
    if(headless_go_here&&(!headless_main||headless_go_here->tick>ticks_limit||
       (!go_here_after_stand&&!standalone_go_here)))
        throw odh::FormatError("--headless-go-here requires an idle action probe or a later tick after Sit/Stand");
    if(!headless_resident_go_here.empty()){
        if(!headless_main||!saved_world_main||!session_context||session_context->single_resident_session||
           session_context->household_members.empty()||headless_interaction||headless_stand||headless_portal)
            throw odh::FormatError("--headless-resident-go-here requires a fresh whole-household Main probe");
        for(const auto& action:headless_resident_go_here)
            if(action.tick>ticks_limit)
                throw odh::FormatError("--headless-resident-go-here tick exceeds the runtime limit");
    }
    if(headless_go_here_interrupt_tick&&(!headless_main||!headless_go_here||
       *headless_go_here_interrupt_tick>ticks_limit||
       *headless_go_here_interrupt_tick<headless_go_here->tick||headless_entity_move))
        throw odh::FormatError("--headless-go-here-interrupt requires an in-range tick at or after Go Here, without a world mutation");
    if(headless_go_here_interrupt_after_movements&&(!headless_main||!headless_go_here||
       headless_entity_move||headless_go_here_interrupt_tick))
        throw odh::FormatError("--headless-go-here-interrupt-after-movements requires Go Here and no tick cancel or world mutation");
    if(headless_portal&&(!headless_main||headless_portal->tick>ticks_limit||headless_interaction||headless_stand||headless_go_here))
        throw odh::FormatError("--headless-portal requires --headless-main, an in-range tick, and an otherwise idle action probe");
    if(headless_stand&&(!headless_main||
       (!headless_interaction&&(!session_context||session_context->restore_archive.empty()))||
       headless_stand->tick>ticks_limit||
       (headless_interaction&&headless_stand->tick<=headless_interaction->tick)))
        throw odh::FormatError("--headless-stand requires --headless-main, an in-range tick, and either a preceding Sit request or a seated restored session");
    if(headless_window_interaction&&(!headless_main||!render_saved_id||!render_output||!render_floors||
       headless_interaction||headless_stand||headless_go_here||headless_portal||render_tick||ticks_limit<2))
        throw odh::FormatError("--headless-window-interaction requires a saved-avatar rendered Main session, a lot pick buffer and at least two ticks; other scheduled actions are not allowed");
    if(!headless_window_menu_steps.empty()&&!headless_window_interaction)
        throw odh::FormatError("headless window menu steps require --headless-window-interaction");
    if(headless_window_stand_tick&&(!headless_window_interaction||*headless_window_stand_tick>=ticks_limit))
        throw odh::FormatError("--headless-window-stand-tick requires --headless-window-interaction and a tick below the limit");
    if(headless_window_go_here&&(!headless_window_interaction||!headless_window_stand_tick||
       headless_window_go_here->tick<=*headless_window_stand_tick||
       headless_window_go_here->tick>=ticks_limit||headless_go_here||headless_interaction||headless_stand||headless_portal))
        throw odh::FormatError("--headless-window-go-here requires a later tick after window Sit/Stand and an in-range target");
    if(headless_window_neighborhood_tick&&(!headless_main||!lot_id||*headless_window_neighborhood_tick>=ticks_limit))
        throw odh::FormatError("--headless-window-neighborhood-tick requires a headless Main lot and a tick below the limit");
    if(headless_window_neighborhood_house&&!headless_window_neighborhood_tick)
        throw odh::FormatError("--headless-window-neighborhood-house requires --headless-window-neighborhood-tick");
    if(ui_graphics_far&&!render_window)
        throw odh::FormatError("--ui-graphics requires --render-window");
    const bool render_saved_selector=render_saved_id.has_value()||render_saved_guid.has_value();
    const bool render_requested=render_output||render_size||render_animation_far||render_textures_far||render_skeleton_entry||render_skeleton_name||render_tick||render_window||(render_saved_selector&&!headless_main);
    if(render_hide_lot_objects&&(!render_output||!render_floors||!saved_world_main))
        throw odh::FormatError("--render-hide-lot-objects requires a saved-world frame capture");
    if(render_size&&!render_output&&!render_window)
        throw odh::FormatError("--render-size requires --render-frame or --render-window");
    if(!avatar||!semi||!globals||ticks_limit>4096||(age&&*age>32767)||(lot_id&&(*lot_id==0||*lot_id>32767))||(zoning_path&&!lot_path)||(initialize&&(routine||age||flags))||(activate_family&&((!initialize&&!saved_world_main)||!lot_path||!neighborhood_path))||
       (render_requested&&((!render_output&&!render_window)||(render_output&&render_window)||!render_animation_far||!render_textures_far||!render_skeleton_entry||!render_skeleton_name||(!initialize&&!saved_world_main)||animation_sources.empty()||(render_tick&&*render_tick>ticks_limit)||(render_window&&(render_tick||!render_saved_selector))))||
       (render_saved_selector&&((!render_output&&!render_window&&!headless_main)||!lot_path))||
       (render_saved_id&&(*render_saved_id==0||*render_saved_id>32767))||
       (render_saved_guid&&(!saved_world_main||*render_saved_guid==0))||
       (render_saved_id&&render_saved_guid)||
       (headless_fixture_avatar&&(!saved_world_main||!headless_main||(!headless_portal&&!headless_go_here)||render_saved_selector))||
       (headless_fixture_occupant_avatar&&(!headless_fixture_avatar||!saved_world_main||!headless_main||
        !headless_portal||render_saved_selector||headless_fixture_occupant_avatar->id==headless_fixture_avatar->id))||
       (headless_second_main&&(initialize||action||
        (!second_saved_main&&(saved_world_main||headless_main||render_saved_selector||headless_second_main->avatar.id==1))||
        odh::routine_key(headless_second_main->routine).scope!=odh::RoutineScope::private_object))||
       (second_saved_main&&(!saved_world_main||!headless_main||!render_saved_id||!second_avatar||
        headless_second_main->avatar.id==*render_saved_id||headless_second_main_arg0))||
       (headless_second_main_arg0&&!headless_second_main)||
       (second_avatar&&!headless_second_main)||
       (headless_fixture_main_arg0&&!headless_fixture_avatar)||
       (saved_world_main&&(!lot_path||(!render_saved_selector&&!headless_fixture_avatar)||initialize||action||(!render_requested&&!headless_main)||definition_sources.empty()))||
       (headless_main&&(!render_floors||(render_requested&&(!render_output||render_window))))||
       (headless_stack_object&&!headless_main&&!headless_second_main)||
       (headless_entity_move&&(!headless_main||headless_entity_move->tick>ticks_limit))||
       (placement_candidate&&(!saved_world_main||!render_saved_selector||!render_floors))||
       (render_animation_far&&std::find(animation_sources.begin(),animation_sources.end(),*render_animation_far)==animation_sources.end()))
        throw odh::FormatError("usage: (--avatar FAR ENTRY OBJD | --avatar-file IFF OBJD) --semiglobal FAR ENTRY --global FAR ENTRY [--instruction-budget 1..1000000 for bounded headless runs] [--language 1..20] [--initialize | --routine ID] [--headless-second-main ID X Y LEVEL DIRECTION ROUTINE [--headless-second-main-arg0 VALUE] [--headless-second-avatar-file IFF OBJD]] [--headless-second-saved-main OBJECT_ID ROUTINE --headless-second-avatar-file IFF OBJD] [--action FAR ENTRY OBJD ROUTINE] [--lot HOUSE_IFF LOT_ID] [--zoning IFF] [--neighborhood IFF] [--activate-family] [--object-definitions FAR | --object-definition-file IFF | --object-definition-entry FAR ENTRY] [--animation-source FAR] [--seed UINT64] [--flags WORD] [--age YEARS] [--ticks 0..4096] [--render-frame NEW.ppm [--render-size WIDTH HEIGHT] --render-animation-far FAR --render-textures FAR --render-skeleton ENTRY NAME [--render-tick 0..TICKS] [--render-saved-avatar OBJECT_ID | --render-saved-guid GUID] | --render-window [--render-size WIDTH HEIGHT] --render-saved-avatar OBJECT_ID|GUID --render-animation-far FAR --render-textures FAR --render-skeleton ENTRY NAME | --headless-main (--render-saved-avatar OBJECT_ID | --headless-fixture-avatar ID X Y LEVEL DIRECTION [--headless-fixture-main-arg0 VALUE]) --render-floors IFF [--headless-stack-object ID] [--headless-interaction ID TICK | --headless-window-interaction OBJECT_ID [--headless-window-stand-tick TICK [--headless-window-go-here TILE_X TILE_Y LEVEL TICK]] | --headless-go-here TILE_X TILE_Y LEVEL TICK [--headless-go-here-interrupt TICK] | --headless-portal ID TICK] [--headless-stand ID TICK] [--headless-move-entity ID X Y LEVEL DIRECTION TICK]] [--saved-world-main --render-floors IFF [--placement-candidate X Y LEVEL DIRECTION] [--attempt-walk-destination X Y] [--architecture-resource FILE ...] [--render-walls IFF] [--render-hide-lot-objects for diagnostic captures] [--render-level 1..2] [--render-wall-mode up|down] [--render-terrain-theme 0..7]]");
    if(probe_instruction_limit&&(!ticks_set||render_window))
        throw odh::FormatError("--instruction-budget requires a bounded headless run with --ticks");
    std::optional<odh::BitmapImage> pie_menu_background,pie_menu_button;
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    std::optional<odh::BitmapImage> motive_panel_background;
#endif
    if(ui_graphics_far){
        odh::Limits far_limits;far_limits.far_entries=16384;far_limits.far_name_length=512;
        far_limits.far_name_bytes=1024*1024;
        odh::BitmapLimits bitmap_limits;bitmap_limits.width=256;bitmap_limits.height=256;
        bitmap_limits.pixels=65536;bitmap_limits.rle_bytes=1024*1024;
        with_far_entry(*ui_graphics_far,"Shared\\Sys\\PieBack.bmp",[&](const auto& view){
            pie_menu_background=odh::read_bmp(view,bitmap_limits);
        },far_limits);
        with_far_entry(*ui_graphics_far,"Shared\\Sys\\PieButt.bmp",[&](const auto& view){
            pie_menu_button=odh::read_bmp(view,bitmap_limits);
        },far_limits);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        odh::BitmapLimits panel_bitmap_limits;
        panel_bitmap_limits.width=1024;panel_bitmap_limits.height=128;
        panel_bitmap_limits.pixels=131072;panel_bitmap_limits.rle_bytes=1024*1024;
        with_far_entry(*ui_graphics_far,"cpanel\\Backgrounds\\PanelBack.bmp",[&](const auto& view){
            motive_panel_background=odh::read_bmp(view,panel_bitmap_limits);
        },far_limits);
#endif
        if(!pie_menu_background||!pie_menu_button)
            throw odh::FormatError("UI graphics archive did not provide both pie menu images");
    }
#ifndef ODH_HAVE_X11
    if(render_window)throw odh::FormatError("X11 window support is not built; configure ODH_X11 and X11 development files");
    if(headless_window_interaction)throw odh::FormatError("--headless-window-interaction requires an X11-enabled build, but does not require a running display");
#endif
    const odh_host::PersonSessionPolicy session_policy{render_window,ticks_set,ticks_limit,
        probe_instruction_limit.value_or(odh_host::PersonSessionPolicy::instructions)};
    odh_host::PersonDiagnosticRetention diagnostics;
    using Diagnostic=odh_host::PersonDiagnostic;
    profile_mark("arguments-parsed");
#ifdef ODH_HAVE_LINUX_SESSION_LOG
    session_log.event("selected language code="+std::to_string(game_language));
#endif
    if(render_output){
        // Treat every supplied resource as input before catalog or payload reads.
        // Canonical paths and equivalent files cover lexical/symlink/hardlink aliases.
        std::vector<std::string> inputs;
        for(const auto* resource:{&avatar,&semi,&globals,&action})
            if(*resource)inputs.push_back((*resource)->archive);
        for(const auto& source:definition_sources)inputs.push_back(source.path);
        inputs.insert(inputs.end(),animation_sources.begin(),animation_sources.end());
        for(const auto& source:architecture_sources)inputs.push_back(source.path);
        for(const auto* path:{&lot_path,&zoning_path,&neighborhood_path,&render_animation_far,
                             &render_textures_far,&render_floors,&render_walls})
            if(*path)inputs.push_back(**path);
        for(const auto& input:inputs)different_render_path(*render_output,input);
    }
    struct PinnedCharacter {std::shared_ptr<const std::string> bytes;std::array<std::uint8_t,32> fingerprint;};
    std::map<std::filesystem::path,PinnedCharacter> pinned_characters;
    if(session_context&&!session_context->single_resident_session){
        std::uint64_t remaining=64u*1024u*1024u;
        for(const auto& member:session_context->household_members){
            const auto path=std::filesystem::canonical(member.character_path);
            const auto size=std::filesystem::file_size(path);
            if(!std::filesystem::is_regular_file(path)||size>remaining)
                throw odh::FormatError("household character resources exceed the bounded loaded-byte budget");
            auto bytes=std::make_shared<std::string>(static_cast<std::size_t>(size),'\0');
            std::ifstream input(path,std::ios::binary);input.read(bytes->data(),static_cast<std::streamsize>(size));
            if(!input||input.peek()!=std::char_traits<char>::eof())throw odh::FormatError("household character changed while loading");
            const auto digest=odh::sha256(bytes->data(),bytes->size());remaining-=size;
            if(!pinned_characters.emplace(path,PinnedCharacter{bytes,digest}).second)
                throw odh::FormatError("duplicate household character resource");
        }
        const auto primary=pinned_characters.find(std::filesystem::canonical(avatar->archive));
        if(primary==pinned_characters.end())throw odh::FormatError("primary character is absent from the complete household");
        avatar->frozen=primary->second.bytes;
#ifdef ODH_HAVE_LINUX_SESSION_LOG
        session_log.event("household character resources pinned");
#endif
    }
    std::optional<SavedLotMaterialOptions> render_materials;
    if(!architecture_sources.empty()){
        render_materials.emplace();
        render_materials->catalog=odh::discover_architecture_resources(architecture_sources);
    }
    profile_mark("architecture-catalog");
    std::optional<odh::Simi> simi;
    std::optional<odh::Objm> objm;std::optional<odh::LotZoning> zoning;
    std::optional<odh::Objt> objt;
    std::optional<std::vector<odh::SavedObjectPrefix>> saved_prefixes;
    std::optional<std::vector<odh::ObjmCommonState>> saved_common;
    std::optional<std::vector<std::optional<odh::ObjmTailState>>> saved_tails;
    std::optional<odh::DefinitionIndex> definition_index;
    if(!definition_sources.empty()){
        std::vector<odh::DefinitionCandidate> candidates;
        std::uint64_t provenance_available=1024*1024;
        const auto add_iff=[&](const auto& view,const std::string& source,const std::string& asset){
            odh::Limits limits;limits.iff_chunks=4096;
            odh::iff_chunks(view,[&](const auto& chunk){
                if(chunk.type!="OBJD")return;
                if(candidates.size()>=8192)
                    throw odh::FormatError("definition candidate budget exceeded");
                if(source.size()>provenance_available||asset.size()>provenance_available-source.size())
                    throw odh::FormatError("definition provenance byte budget exceeded");
                auto definition=odh::read_object_definition(chunk.payload);
                provenance_available-=source.size()+asset.size();
                candidates.push_back({source,asset,chunk.id,definition});
            },limits);
        };
        for(const auto& source:definition_sources){
            if(source.standalone){
                Resource resource{source.path,{},true};
                if(!pinned_characters.empty()){
                    std::error_code error;
                    const auto canonical=std::filesystem::canonical(source.path,error);
                    if(error)throw odh::FormatError("cannot open IFF file");
                    const auto pinned=pinned_characters.find(canonical);
                    if(pinned!=pinned_characters.end())resource.frozen=pinned->second.bytes;
                }
                with_resource(resource,[&](const auto& view){add_iff(view,source.path,"");});continue;
            }
            if(!source.entry.empty()){
                with_far_entry(source.path,source.entry,[&](const auto& view){add_iff(view,source.path,source.entry);});
                continue;
            }
            std::ifstream stream(source.path,std::ios::binary);
            if(!stream)throw odh::FormatError("cannot open explicit definition archive");
            const odh::View archive(stream,std::filesystem::file_size(source.path));
            odh::Limits limits;limits.far_entries=8192;limits.far_name_length=4096;limits.far_name_bytes=256*1024;
            for(const auto& entry:odh::far1_index(archive,limits)){
                const auto name=fold(entry.name);
                if(name.size()<4||name.compare(name.size()-4,4,".iff")!=0)continue;
                add_iff(archive.subview(entry.offset,entry.length),source.path,entry.name);
            }
        }
        definition_index.emplace(candidates);
    }
    if(lot_path)with_iff_file(*lot_path,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type=="objt"&&chunk.id==0){
            if(objt)throw odh::FormatError("ambiguous objt0");objt=odh::read_objt(chunk.payload);
        }
        if(chunk.type=="ObjM"&&chunk.id==1){
            if(objm)throw odh::FormatError("ambiguous ObjM1");objm=odh::read_objm(chunk.payload);
        }
        if(chunk.type!="SIMI"||chunk.id!=1)return;
        if(simi)throw odh::FormatError("ambiguous SIMI1");simi=odh::read_simi(chunk.payload);
    });});
    if(lot_path&&!simi)throw odh::FormatError("SIMI1 absent in selected house");
    if(objm){
        if(!objt)throw odh::FormatError("objt0 absent for saved ObjM prefixes");
        saved_prefixes=odh::read_saved_object_prefixes(*objm,*objt);
        std::vector<odh::ObjmCommonState> common;
        common.reserve(saved_prefixes->size());
        std::uint64_t common_words_available=65536;
        for(std::size_t n=0;n<saved_prefixes->size();++n){
            odh::ObjmCommonLimits limits;limits.decoded_words=common_words_available;
            auto state=odh::read_objm_common(objm->blocks[n],(*saved_prefixes)[n].prefix,limits);
            common_words_available-=state.decoded_words;
            common.push_back(std::move(state));
        }
        saved_common=std::move(common);
        if(definition_index){
            std::vector<std::optional<odh::ObjmTailState>> tails;
            tails.reserve(saved_prefixes->size());
            std::uint64_t tail_words_available=65536,string_bytes_available=256*1024;
            for(std::size_t n=0;n<saved_prefixes->size();++n){
                const auto& prefix=(*saved_prefixes)[n];
                const auto* definition=definition_index->unique(prefix.guid);
                if(!definition){tails.emplace_back();continue;}
                const auto* type=objt->entry_for_type(prefix.type_id);
                if(!type||!type->metadata)throw odh::FormatError("saved World object type unavailable for object "+
                    std::to_string(prefix.object_id)+" type "+std::to_string(prefix.type_id));
                odh::ObjmTailLimits limits;limits.decoded_words=tail_words_available;limits.string_bytes=string_bytes_available;
                auto tail=odh::read_objm_tail(objm->blocks[n],(*saved_common)[n],objm->version,
                    definition->definition,*type,limits);
                tail_words_available-=tail.decoded_words;string_bytes_available-=tail.string_bytes;
                tails.emplace_back(std::move(tail));
            }
            saved_tails=std::move(tails);
        }
    }
    if(render_saved_selector&&(!saved_prefixes||!objt))
        throw odh::FormatError("render saved avatar requires ObjM1 and objt0 in selected house");
    profile_mark("saved-object-index");
    if(zoning_path)with_iff_file(*zoning_path,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type!="STR#"||chunk.id!=1)return;
        if(zoning)throw odh::FormatError("ambiguous zoning STR#1");zoning=odh::read_lot_zoning(odh::read_strings(chunk.payload));
    });});
    if(zoning_path&&!zoning)throw odh::FormatError("zoning STR#1 absent");
    std::optional<odh::LotContext> lot;
    if(simi)lot.emplace(static_cast<std::int16_t>(*lot_id),simi->global_data,std::move(zoning));
    enum class GlobalSource {lot,bootstrap,family,clock};
    std::optional<odh::Ts1Clock> simulation_clock;
    std::map<std::int16_t,odh::Ts1MotiveDecay> simulation_motives;
    std::map<std::int16_t,std::string> simulation_motive_status;
    std::uint64_t simulation_skipped_updates=0;
    std::string simulation_profile="unavailable_without_saved_lot";
    struct GlobalRead {std::int16_t index,value;GlobalSource source;};
    std::vector<GlobalRead> global_reads;
    std::optional<odh::Ts1BootstrapGlobals> bootstrap_globals;
    if(initialize||saved_world_main)bootstrap_globals.emplace();
    std::vector<odh::LotZoneResult> zoning_reads;
    std::optional<odh::Nbrs> nbrs;
    std::optional<odh::Ngbh> ngbh;
    std::optional<std::uint16_t> ngbh_id;
    std::vector<odh::FamilyEntry> family_records;
    std::uint32_t family_members_available=65536;
    if(neighborhood_path)with_iff_file(*neighborhood_path,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type=="NGBH"&&!ngbh){ngbh=odh::read_ngbh(chunk.payload);ngbh_id=chunk.id;}
        if(activate_family&&chunk.type=="FAMI"){
            if(family_records.size()>=4096)throw odh::FormatError("family count exceeds bounded host profile");
            odh::FamiLimits family_limits;family_limits.members=std::min<std::uint32_t>(256,family_members_available);
            auto family=odh::read_fami(chunk.payload,family_limits);
            family_members_available-=static_cast<std::uint32_t>(family.member_guids.size());
            family_records.push_back({chunk.id,std::move(family)});
        }
        if(chunk.type!="NBRS"||chunk.id!=1)return;
        if(nbrs)throw odh::FormatError("ambiguous NBRS1");nbrs=odh::read_nbrs(chunk.payload);
    });});
    if(neighborhood_path&&!nbrs)throw odh::FormatError("NBRS1 absent");
#ifdef __linux__
    if(activate_family&&neighborhood_path){
        const auto overlay=odh_host::read_linux_family_overlay_for_neighborhood(*neighborhood_path);
        if(overlay){
            family_records=odh::apply_family_overlay(family_records,
                overlay->neighborhood_identity,*overlay);
            if(!overlay->creations.empty())
                *nbrs=odh::apply_neighbor_overlay(*nbrs,
                    overlay->neighborhood_identity,*overlay);
        }
    }
#endif
    std::optional<odh::NeighborContext> neighbors;
    if(nbrs)neighbors.emplace(std::move(*nbrs));
    profile_mark("neighborhood-context");
    std::optional<odh::FamilyContext> families;
    odh::FamilyEntry* active_family=nullptr;
    std::optional<std::int16_t> family_global;
    if(activate_family){
        families.emplace(family_records);
        active_family=families->by_house(static_cast<std::int16_t>(*lot_id));
        if(!active_family)throw odh::FormatError("selected house has no FAMI assignment");
        family_global=signed_word(active_family->chunk_id);
    }
    std::vector<odh::NeighborNext> neighbor_next;
    std::vector<odh::NeighborDataValue> neighbor_reads;
    auto person=load(*avatar,odh::RoutineScope::private_object,*avatar_def,initialize||saved_world_main);
    if(person.binding->version!=138||person.object_type!=2)throw odh::FormatError("person constructor profile requires OBJD138 ObjectType2");
    if(active_family&&(!odh::FamilyContext::contains(*active_family,person.binding->guid)||!neighbors->id_for_guid(person.binding->guid)))
        throw odh::FormatError("avatar GUID is not a bound member of selected FAMI");
    if(!person.glob||person.glob->name.empty()||person.glob->name.find_first_of("/\\:")!=std::string::npos||
       person.glob->name.find('\0')!=std::string::npos||fold(person.glob->name+".iff")!=fold(semi->entry))
        throw odh::FormatError("semiglobal does not match flat Pascal GLOB link");
    const auto avatar_main=person.entry_points?(person.entry_points->entries.size()>1?person.entry_points->entries[1].action:std::uint16_t{0}):person.functions->main;
    const auto root=routine.value_or(avatar_main);
    if(root==0||odh::routine_key(root).scope!=odh::RoutineScope::private_object||person.routines.find(odh::routine_key(root))==person.routines.end())
        throw odh::FormatError("avatar root routine absent from explicit private owner");
    auto semi_data=load(*semi,odh::RoutineScope::semiglobal);
    person.routines.insert(semi_data.routines.begin(),semi_data.routines.end());
    std::optional<Loaded> second_person;
    if(second_avatar){
        second_person=load(*second_avatar,odh::RoutineScope::private_object,*second_avatar_def);
        if(second_person->binding->version!=138||second_person->object_type!=2)
            throw odh::FormatError("second person constructor profile requires OBJD138 ObjectType2");
        if(!second_person->glob||second_person->glob->name.empty()||
           second_person->glob->name.find_first_of("/\\:")!=std::string::npos||
           second_person->glob->name.find('\0')!=std::string::npos||
           fold(second_person->glob->name+".iff")!=fold(semi->entry))
            throw odh::FormatError("second semiglobal does not match flat Pascal GLOB link");
        const auto main=second_person->entry_points?(second_person->entry_points->entries.size()>1?
            second_person->entry_points->entries[1].action:std::uint16_t{0}):second_person->functions->main;
        if(main==0||main!=headless_second_main->routine)
            throw odh::FormatError("second avatar Main does not match selected OBJD entry point");
        second_person->routines.insert(semi_data.routines.begin(),semi_data.routines.end());
    }
    const auto& second_data=second_person?*second_person:person;
    std::optional<odh::RoutineSet> second_main_routines;
    if(headless_second_main){
        const auto second_key=odh::routine_key(headless_second_main->routine);
        if(second_data.routines.find(second_key)==second_data.routines.end())
            throw odh::FormatError("second avatar Main routine absent from the selected avatar resource set");
        second_main_routines=second_data.routines;
    }
    auto shared=load(*globals,odh::RoutineScope::global);
    const auto person_sounds=load_sounds(*avatar);
    const auto second_person_sounds=second_avatar?load_sounds(*second_avatar):person_sounds;
    const auto semiglobal_sounds=load_sounds(*semi);
    const auto global_sounds=load_sounds(*globals);
    profile_mark("person-and-global-resources");
    std::optional<odh::AvatarBodyStrings> body;
    std::optional<odh::StringTable> body_walk,body_swim,private_walk;
    if(initialize||saved_world_main){
        if(initialize&&(!person.entry_points||person.entry_points->entries.size()<=8))throw odh::FormatError("startup requires entry points0/1/8");
        if(!person.startup)throw odh::FormatError("avatar body derivation requires startup OBJD fields");
        auto strings=load_strings(*avatar,{person.startup->body_strings,150});
        const auto table=strings.find(person.startup->body_strings);
        if(table==strings.end())throw odh::FormatError("startup BodySTR absent");
        body=odh::parse_avatar_body_strings(table->second);
        if(body->avatar_type!="adult"&&(!saved_world_main||body->avatar_type!="child"))
            throw odh::FormatError("startup constructor profile requires adult BodySTR type or saved child");
        const auto special=strings.find(150);if(special!=strings.end())private_walk=std::move(special->second);
        const bool child=*body->age<18;
        const std::uint16_t walk_id=child?151:150,swim_id=child?160:158;
        auto animation_strings=load_strings(*globals,{walk_id,swim_id});
        if(!animation_strings.count(walk_id)||!animation_strings.count(swim_id))throw odh::FormatError("startup global walk/swim STR# absent");
        body_walk=std::move(animation_strings.at(walk_id));body_swim=std::move(animation_strings.at(swim_id));
        if(initialize)for(const auto id:{person.entry_points->entries[0].action,person.entry_points->entries[8].action})if(id>255){
            const auto key=odh::routine_key(id);const auto& bank=key.scope==odh::RoutineScope::global?shared.routines:person.routines;
            if(!bank.count(key))throw odh::FormatError("startup routine absent from explicit owner");
        }
    }
    std::optional<Loaded> action_data;
    if(action){action_data=load(*action,odh::RoutineScope::private_object,*action_def);
        if(odh::routine_key(*action_routine).scope!=odh::RoutineScope::private_object||action_data->routines.find(odh::routine_key(*action_routine))==action_data->routines.end())
            throw odh::FormatError("action routine absent from explicit private owner");
        if(action_data->glob&&!action_data->glob->name.empty())throw odh::FormatError("action semiglobal not supplied by this host profile");
    }
    std::unique_ptr<odh::SavedWorld> restored_world;
    std::unique_ptr<odh::World> world_owner;
    std::vector<odh::SavedWorldEntity> restored_records;
    std::map<std::uint32_t,SavedTreeNameCatalog> saved_tree_names;
    std::int16_t runtime_avatar_id=1;
    if(saved_world_main){
        if(!definition_index)throw odh::FormatError("saved World Main requires explicit object definitions");
        if(!objm||!objt||!saved_prefixes||!saved_common||!saved_tails)
            throw odh::FormatError("saved World decoded lot state unavailable");
        restored_records=saved_world_records(*objm,*objt,std::move(*saved_prefixes),std::move(*saved_common),
            std::move(*saved_tails),*definition_index);
        if(render_saved_guid){
            const auto matches=std::count_if(restored_records.begin(),restored_records.end(),[&](const auto& record){
                return record.saved.guid==*render_saved_guid&&record.tail.person&&record.serialized_object_type==2;
            });
            if(matches>1)throw odh::FormatError("saved resident GUID identifies more than one avatar in selected lot");
            // A newly-created household member is not present in the house file
            // yet. Keep its GUID as the selection key and resolve it after the
            // household arrival path has prepared the restored World.
            if(matches==1){
                const auto selected=std::find_if(restored_records.begin(),restored_records.end(),[&](const auto& record){
                    return record.saved.guid==*render_saved_guid&&record.tail.person&&record.serialized_object_type==2;
                });
                render_saved_id=selected->saved.object_id;
            }
        }
        if(render_saved_id){
            const auto saved=std::find_if(restored_records.begin(),restored_records.end(),[&](const auto& record){return record.saved.object_id==*render_saved_id;});
            if(saved==restored_records.end()||!saved->tail.person||saved->serialized_object_type!=2||saved->saved.guid!=person.binding->guid)
                throw odh::FormatError("selected saved World entity is not the matching person resource");
        }
        // EntryPoint tables are retained alongside saved World banks. A real
        // House02 save needs 81,415 logical words after those tables are bound,
        // so give this saved-lot host a bounded 131,072-word budget while
        // keeping the smaller default for ordinary synthetic Worlds.
        odh::SavedWorldLimits saved_world_limits;
        saved_world_limits.world.words=131072;
        restored_world=odh::make_saved_world(std::move(restored_records),*definition_index,
            neighbors?&*neighbors:nullptr,saved_world_limits);
        // Bind the exact immutable OBJD/OBJf tables to every restored entity.
        // This is static metadata only; action function IDs are not BHAV results.
        std::map<std::uint32_t,std::vector<odh::EntryPoint>> entry_point_cache;
        for(const auto& record:restored_world->records){
            const auto id=static_cast<std::int16_t>(record.saved.object_id);
            const auto* entity=restored_world->world->find(id);
            if(!entity||!entity->definition||entity->definition->guid!=record.saved.guid)
                throw odh::FormatError("saved World EntryPoint entity identity unavailable");
            auto cached=entry_point_cache.find(entity->definition->guid);
            if(cached==entry_point_cache.end()){
                if(entry_point_cache.size()>=256)
                    throw odh::FormatError("saved World EntryPoint definition cache budget exceeded");
                const auto* candidate=definition_index->unique(entity->definition->guid);
                if(!candidate)throw odh::FormatError("saved World EntryPoint definition unavailable or ambiguous");
                // Every restored entity needs its exact EntryPoint table, but
                // only a Portal15 plan needs to decode that asset's GLOB link.
                auto resource=saved_entry_point_resource(*candidate,false);
                if(resource.chunk_id!=candidate->chunk_id||resource.table.entries.size()>256)
                    throw odh::FormatError("saved World EntryPoint resource identity or size invalid");
                cached=entry_point_cache.emplace(entity->definition->guid,std::move(resource.table.entries)).first;
            }
            if(entity->kind==odh::EntityKind::object&&!saved_tree_names.count(entity->definition->guid)){
                if(saved_tree_names.size()>=256)
                    throw odh::FormatError("saved World TreeByName definition cache budget exceeded");
                const auto* candidate=definition_index->unique(entity->definition->guid);
                if(!candidate)throw odh::FormatError("saved World TreeByName definition unavailable or ambiguous");
                auto catalog=load_saved_tree_name_catalog(*candidate);
                if(!catalog.semiglobal_name&&candidate->definition.tree_table_id==0xffff&&
                   candidate->definition.master_id!=0){
                    if(const auto* master=definition_index->unique_master(*candidate))
                        catalog.semiglobal_name=load_saved_tree_name_catalog(*master).semiglobal_name;
                }
                saved_tree_names.emplace(entity->definition->guid,std::move(catalog));
            }
            if(!restored_world->world->define_entry_points(id,cached->second))
                throw odh::FormatError("saved World EntryPoint binding exceeds World limits");
        }
        if(render_saved_id){
            runtime_avatar_id=static_cast<std::int16_t>(*render_saved_id);
            const auto* restored_avatar=restored_world->world->find(runtime_avatar_id);
            if(!restored_avatar||restored_avatar->kind!=odh::EntityKind::avatar||!restored_avatar->position)
                throw odh::FormatError("selected saved World avatar is not positioned in the restored World");
        }else runtime_avatar_id=static_cast<std::int16_t>(headless_fixture_avatar->id);
    }else world_owner=std::make_unique<odh::World>(odh::WorldLimits{},initialize?odh::AttributePolicy::ts1_bounded:odh::AttributePolicy::fixed);
    profile_mark("saved-world-restore-and-bindings");
    odh::RandomState random(seed.value_or(1));
    std::map<std::int16_t,odh::VmSnapshot> materialized_main_states;
    if(session_context&&!session_context->single_resident_session&&saved_world_main&&active_family){
        const auto source=restored_world->world->snapshot();
        const auto reconciliation=odh::reconcile_session_household(active_family->family.member_guids,*restored_world->world);
        const bool missing=std::any_of(reconciliation.begin(),reconciliation.end(),[](const auto& member){
            return member.presence==odh::HouseholdMemberPresence::absent;
        });
        if(missing){
            if(!neighbors||session_context->household_members.empty())
                throw odh::FormatError("missing household resident requires prepared architecture and exact character resources");
            // An installation with no external .flr/.wll providers has an
            // explicitly empty alias catalog. The renderer must use the same
            // prepared geometry as the reconstruction even in that case.
            if(!render_materials)render_materials.emplace();
            odh::Ts1Clock construction_clock;
            const auto& construction_globals=simi->global_data;
            if(construction_globals.size()<=8||construction_globals[0]<0||construction_globals[1]<0||
               construction_globals[5]<0||construction_globals[6]<0||construction_globals[7]<0||
               !construction_clock.initialize_simi(static_cast<unsigned>(construction_globals[0]),
                   static_cast<unsigned>(construction_globals[1]),static_cast<unsigned>(construction_globals[5]),
                   static_cast<unsigned>(construction_globals[6]),static_cast<unsigned>(construction_globals[7]),
                   construction_globals[8]))
                throw odh::FormatError("missing household resident requires a supported SIMI clock");
            const auto architecture=saved_architecture_resource(*lot_path);
            const odh::ArchitectureMap empty;
            const auto* floor_map=selected_architecture_map(architecture,"FLRm");
            const auto* wall_map=selected_architecture_map(architecture,"WALm");
            const auto aliases=odh::resolve_architecture_aliases(floor_map?*floor_map:empty,wall_map?*wall_map:empty,
                render_materials->catalog.floors(),render_materials->catalog.walls(),render_materials->alias_limits);
            if(!aliases.floor_map_complete()||!aliases.wall_map_complete())
                throw odh::FormatError("household arrival architecture aliases are unresolved");
            const auto prepared=odh::prepare_saved_architecture(architecture.architecture,aliases.aliases,
                render_materials->preparation,render_materials->preparation_limits);
            std::optional<odh::EntityPosition> mailbox;
            for(const auto& entity:source.entities){
                if(!entity.definition||(entity.definition->guid!=0xEF121974&&entity.definition->guid!=0x1D95C9B0))continue;
                if(mailbox||entity.kind!=odh::EntityKind::object||!entity.position||entity.attachment||entity.ui_ghost)
                    throw odh::FormatError("household arrival requires one positioned singleton mailbox");
                mailbox=entity.position;
            }
            if(!mailbox)throw odh::FormatError("household arrival requires one positioned singleton mailbox");
            std::vector<odh::HouseholdSpawnRequest> requests;
            for(const auto guid:active_family->family.member_guids){
                const auto neighbor=neighbors->id_for_guid(guid);
                if(!neighbor)throw odh::FormatError("household arrival member has no exact NBRS record");
                requests.push_back({guid,neighbors->by_id(*neighbor)});
            }
            auto candidate_random=random;
            const auto prepare=[&](odh::World& candidate,std::int16_t id,std::uint32_t guid){
                const auto member=std::find_if(session_context->household_members.begin(),session_context->household_members.end(),
                    [&](const auto& value){return value.guid==guid;});
                if(member==session_context->household_members.end())return false;
                const auto pinned=pinned_characters.find(std::filesystem::canonical(member->character_path));
                if(pinned==pinned_characters.end())return false;
                Resource resource{member->character_path.string(),{},true,pinned->second.bytes};
                auto data=load(resource,odh::RoutineScope::private_object,member->object_definition_id,true);
                const auto* definition=definition_index->unique(guid);
                if(!definition||definition->definition.is_multitile||data.binding->version!=138||data.object_type!=2||
                   data.binding->guid!=guid||!data.entry_points||data.entry_points->entries.size()<=9||
                   data.entry_points->entries[9].action!=0||!data.startup||!data.glob||
                   fold(data.glob->name+".iff")!=fold(semi->entry))
                    throw odh::FormatError("missing household resident requires exact singleton adult resource with absent placement EP9");
                const auto main=data.entry_points->entries[1].action;
                if(!main||odh::routine_key(main).scope!=odh::RoutineScope::private_object||!data.routines.count(odh::routine_key(main)))
                    throw odh::FormatError("missing household resident Main is unavailable");
                auto strings=load_strings(resource,{data.startup->body_strings,150});
                const auto body_table=strings.find(data.startup->body_strings);
                if(body_table==strings.end())throw odh::FormatError("missing household resident BodySTR unavailable");
                const auto descriptor=odh::parse_avatar_body_strings(body_table->second);
                if(descriptor.avatar_type!="adult"||!descriptor.age||*descriptor.age<18)
                    throw odh::FormatError("missing household resident constructor supports adults only");
                const auto tables=load_strings(*globals,{150,158});
                if(!tables.count(150)||!tables.count(158))throw odh::FormatError("missing household resident walk/swim tables unavailable");
                if(!candidate.define_entry_points(id,data.entry_points->entries)||
                   !candidate.declare_definition_word(id,9,2)||!candidate.declare_object_word(id,29,-1))return false;
                data.routines.insert(semi_data.routines.begin(),semi_data.routines.end());
                // Startup runs against candidate World memory, exact BCON/global
                // reads, and only the zone-query and random-number primitives.
                // Other primitive effects and global writes remain unsupported.
                auto startup_memory=candidate.vm_memory();
                const auto read_startup_word=startup_memory.read;
                const auto write_startup_word=startup_memory.write;
                odh::TuningResources startup_tuning_resources;
                startup_tuning_resources.globals=shared.tuning;
                startup_tuning_resources.owners.emplace(id,odh::TuningOwner{
                    data.tuning,semi_data.tuning,odh::TuningSemiState::loaded});
                const odh::BconTuning startup_tuning(std::move(startup_tuning_resources));
                startup_memory.read=[read_startup_word,&startup_tuning,&construction_clock,&lot,
                    &bootstrap_globals,&family_global](const odh::VmFrame& frame,
                    std::uint8_t scope,std::int16_t index)->std::optional<std::int16_t> {
                    if(scope==26){
                        const auto value=startup_tuning.read(frame.context.code_owner,index);
                        return value?std::optional<std::int16_t>{value->value}:std::nullopt;
                    }
                    if(scope==6){
                        const auto clock=construction_clock.global(index);
                        const auto runtime=bootstrap_globals?bootstrap_globals->read(index):std::nullopt;
                        const auto family_value=index==9?family_global:std::nullopt;
                        if(clock)return clock;
                        if(family_value)return family_value;
                        if(runtime)return runtime;
                        return lot?lot->read(index):std::nullopt;
                    }
                    return read_startup_word(frame,scope,index);
                };
                std::optional<std::string> rejected_flags_write;
                startup_memory.write=[read_startup_word,write_startup_word,&rejected_flags_write,&candidate,id](const odh::VmFrame& frame,
                    std::uint8_t scope,std::int16_t index,std::int16_t value) {
                    if((scope==3||scope==4)&&index==8) {
                        const auto old=read_startup_word(frame,scope,index);
                        if(!old){
                            rejected_flags_write="Flags write rejected: old=unknown new="+std::to_string(value)+" changed_mask=unknown";
                            return false;
                        }
                        const auto changed_mask=static_cast<std::uint16_t>(*old)^static_cast<std::uint16_t>(value);
                        constexpr std::uint16_t can_walk=0x0008;
                        constexpr std::uint16_t dynamic_footprint=0x8000;
                        constexpr std::uint16_t supported_flags=can_walk|dynamic_footprint;
                        // Simitone SetValue(Flags) also changes registered
                        // footprint, AvatarSolid, and sometimes the live thread.
                        // CanWalk is a plain stored bit; the out-of-world avatar
                        // can also change dynamic-footprint class before it has a
                        // registered position. Other bits fail closed here.
                        if(changed_mask&&(scope!=3||frame.context.caller!=id||
                           (changed_mask&static_cast<std::uint16_t>(~supported_flags))!=0)){
                            rejected_flags_write="Flags write rejected: old="+std::to_string(*old)+
                                " new="+std::to_string(value)+" changed_mask="+std::to_string(changed_mask);
                            return false;
                        }
                        if(changed_mask&dynamic_footprint){
                            const bool is_static=(static_cast<std::uint16_t>(value)&dynamic_footprint)==0;
                            if(!candidate.set_static_footprint(id,is_static))return false;
                        }
                    }
                    return write_startup_word(frame,scope,index,value);
                };
                const auto startup_lot_primitive=lot?lot->primitive():odh::VmPrimitive{};
                const auto startup_random_primitive=odh::random_primitive(candidate_random,startup_memory);
                const odh::VmPrimitive startup_primitive=[startup_lot_primitive,startup_random_primitive](
                    const odh::VmFrame& frame,const odh::Instruction& instruction,
                    const std::array<std::int16_t,20>& temps,const odh::VmDispatch& dispatch){
                    if(instruction.opcode==1&&instruction.operands[0]==28)
                        return startup_lot_primitive?startup_lot_primitive(frame,instruction,temps,dispatch):odh::PrimitiveOutcome{};
                    if(instruction.opcode==8)return startup_random_primitive(frame,instruction,temps,dispatch);
                    return odh::PrimitiveOutcome{};
                };
                odh::Vm startup(shared.routines,std::move(startup_memory),{},startup_primitive,
                    {{static_cast<odh::CodeOwner>(id),data.routines}});
                std::optional<std::string> startup_primitive_trace;
                const auto started=odh::run_avatar_startup(startup,id,data.entry_points->entries[0].action,
                    data.entry_points->entries[8].action,10000,[&startup_primitive_trace](const auto& phase,const odh::VmTrace& record){
                        if(record.instruction.opcode<256&&record.instruction.opcode!=0&&record.instruction.opcode!=2){
                            std::ostringstream detail;
                            detail<<phase<<" BHAV "<<record.key.id<<" node "<<record.pc<<" primitive "
                                <<record.instruction.opcode<<" operands";
                            for(const auto byte:record.instruction.operands)
                                detail<<' '<<static_cast<unsigned>(byte);
                            startup_primitive_trace=detail.str();
                        }
                    });
                if(!started.completed)throw odh::FormatError("missing household resident startup unsupported: "+startup.diagnostic()+
                    (rejected_flags_write?"; "+*rejected_flags_write:std::string{})+
                    (startup_primitive_trace?"; "+*startup_primitive_trace:std::string{}));
                startup.start(odh::routine_key(main),{id,id,0,static_cast<odh::CodeOwner>(id)},std::vector<std::int16_t>(4));
                if(startup.status()==odh::VmStatus::fault)throw odh::FormatError("missing household resident Main could not be stacked");
                materialized_main_states.emplace(id,startup.snapshot());
                const auto special=strings.find(150);
                odh::apply_avatar_body(candidate,id,candidate_random,descriptor,tables.at(150),tables.at(158),
                    special==strings.end()?std::optional<odh::StringTable>{}:std::optional<odh::StringTable>{special->second});
                // CreateObjectInstance.SetBirthTime uses the lot's construction
                // clock, not wall time or the later ODH resume clock. These
                // ObjectData writes commute with the subsequent NBRS-only
                // PersonData inheritance; Main remains stacked but unexecuted.
                const auto& birth=construction_clock.snapshot();
                for(const auto& field:std::array<std::pair<std::int16_t,std::int16_t>,5>{{
                    {45,static_cast<std::int16_t>(birth.year)},{46,birth.month},{47,birth.day},
                    {21,birth.minute},{24,birth.hour}}})
                    if(!candidate.declare_object_word(id,field.first,field.second))return false;
                return true;
            };
            odh::WorldLimits limits;limits.words=131072;
            auto result=odh::verify_household_members(source,requests,active_family->chunk_id,prepare,mailbox,
                static_cast<std::uint16_t>(prepared.width),static_cast<std::uint16_t>(prepared.height),
                [&](odh::World& candidate,std::int16_t id,const odh::EntityPosition& position){
                    return attempt_household_runtime_arrival(candidate,id,position,*definition_index,prepared);
                },[](odh::World& candidate,std::int16_t id,std::uint32_t,bool positioned){
                    const auto* avatar=candidate.find(id);
                    return positioned&&avatar&&avatar->person_data.size()>68&&avatar->person_data[0]==0&&
                        avatar->person_data[68]==0;
                },limits);
            if(!result.candidate||!restored_world->world->publish_snapshot(result.candidate->snapshot()))
                throw odh::FormatError("verified household arrival candidate could not be published");
            random=candidate_random;
        }
    }
    if(render_saved_guid){
        if(*render_saved_guid!=person.binding->guid)
            throw odh::FormatError("selected resident GUID differs from the loaded person resource");
        const bool serialized_selection=std::any_of(restored_world->records.begin(),restored_world->records.end(),
            [&](const auto& record){return record.saved.guid==*render_saved_guid&&record.tail.person&&
                record.serialized_object_type==2&&render_saved_id==record.saved.object_id;});
        if(!serialized_selection&&(!active_family||!odh::FamilyContext::contains(*active_family,*render_saved_guid)))
            throw odh::FormatError("selected resident GUID is not the active household person");
        const odh::Entity* selected=nullptr;
        for(const auto& entity:restored_world->world->snapshot().entities){
            if(entity.kind!=odh::EntityKind::avatar||!entity.definition||
               entity.definition->guid!=*render_saved_guid)continue;
            if(selected)throw odh::FormatError("selected household GUID identifies more than one live avatar");
            selected=restored_world->world->find(entity.id);
        }
        if(!selected||!selected->position)
            throw odh::FormatError("selected household resident is not positioned in the prepared World");
        if(render_saved_id&&*render_saved_id!=static_cast<std::uint16_t>(selected->id))
            throw odh::FormatError("selected saved resident differs from the active household avatar");
        render_saved_id=static_cast<std::uint16_t>(selected->id);
        runtime_avatar_id=selected->id;
    }
    std::optional<odh::WorldSnapshot> session_baseline;
    if(session_context&&saved_world_main&&restored_world)session_baseline=restored_world->world->snapshot();
    const auto capture_household_roster=[&](bool verify_current=false) {
        std::vector<odh::SessionHouseholdMember> roster;
        if(!session_context||session_context->single_resident_session)return roster;
        if(!session_baseline||!active_family||!neighbors||session_context->household_members.empty()||
           session_context->household_members.size()!=active_family->family.member_guids.size())
            throw odh::FormatError("household session requires the complete selected FAMI roster");
        const auto game=std::filesystem::canonical(session_context->game_root);
        std::filesystem::path player_root;
        if(!session_context->player_character_root.empty()){
            std::error_code root_error;
            const auto status=std::filesystem::symlink_status(session_context->player_character_root,root_error);
            if(!root_error&&!std::filesystem::is_symlink(status)&&std::filesystem::is_directory(status))
                player_root=std::filesystem::canonical(session_context->player_character_root);
        }
        for(const auto& member:session_context->household_members){
            const odh::Entity* actor=nullptr;
            for(const auto& entity:session_baseline->entities)if(entity.kind==odh::EntityKind::avatar&&
                entity.definition&&entity.definition->guid==member.guid){
                if(actor)throw odh::FormatError("household GUID matches more than one World resident");
                actor=&entity;
            }
            if(!actor||!actor->position||!odh::FamilyContext::contains(*active_family,member.guid)||
               neighbors->id_for_guid(member.guid)!=std::optional<std::int16_t>{member.neighbor_id})
                throw odh::FormatError("household session member differs from FAMI, NBRS or original World");
            const auto path=std::filesystem::canonical(member.character_path);
            const auto game_relative=path.lexically_relative(game);
            const auto player_relative=player_root.empty()?std::filesystem::path{}:path.lexically_relative(player_root);
            const bool in_game=!game_relative.empty()&&!game_relative.is_absolute()&&*game_relative.begin()!="..";
            const bool in_player=!player_relative.empty()&&!player_relative.is_absolute()&&*player_relative.begin()!=".."&&
                player_relative.parent_path().empty();
            if((!in_game&&!in_player)||!std::filesystem::is_regular_file(path)||
               std::filesystem::file_size(path)>64u*1024u*1024u)
                throw odh::FormatError("household character resource is outside the bounded game or player catalog");
            const auto logical_asset=member.character_asset.empty()?
                (in_game?game_relative.generic_string():"PlayerData/Characters/"+path.filename().string()):
                member.character_asset;
            const auto expected_asset=(in_game?std::string("UserData/Characters/"):std::string("PlayerData/Characters/"))+path.filename().string();
            if(logical_asset!=expected_asset ||
               (in_game&&game_relative.generic_string()!=expected_asset))
                throw odh::FormatError("household character logical identity differs from its catalog location");
            const auto pinned=pinned_characters.find(path);
            if(pinned==pinned_characters.end())throw odh::FormatError("household character has no loaded-byte provenance");
            if(verify_current){
                if(std::filesystem::file_size(path)!=pinned->second.bytes->size())
                    throw odh::FormatError("household character resource changed since it was loaded");
                std::ifstream input(path,std::ios::binary);odh::Sha256 digest;std::array<char,65536> buffer{};
                while(input){input.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
                    if(input.gcount()>0)digest.update(buffer.data(),static_cast<std::size_t>(input.gcount()));}
                if(!input.eof()||digest.finish()!=pinned->second.fingerprint)
                    throw odh::FormatError("household character resource changed since it was loaded");
            }
            odh::SessionHouseholdMember entry;entry.id=actor->id;entry.generation=actor->generation;
            entry.guid=member.guid;entry.neighbor_id=member.neighbor_id;entry.object_definition_id=member.object_definition_id;
            entry.character_asset=logical_asset;entry.character_fingerprint=pinned->second.fingerprint;roster.push_back(std::move(entry));
        }
        std::sort(roster.begin(),roster.end(),[](const auto& a,const auto& b){return a.id<b.id;});return roster;
    };
    std::unique_ptr<odh::SingleResidentSessionRestore> session_restore;
    std::unique_ptr<odh::HouseholdSessionRestore> household_restore;
    if(session_context&&!session_context->restore_archive.empty()){
        if(!saved_world_main||!restored_world||!lot||!bootstrap_globals||!session_context->resources||
           headless_second_main||headless_fixture_avatar||initialize||action||seed||age||flags||routine)
            throw odh::FormatError("session restore requires an unchanged integrated saved-lot host");
        odh::SessionHostState selection;
        selection.neighborhood_mode=session_context->neighborhood_mode;
        selection.house_number=session_context->house_number;
        selection.family_chunk_id=session_context->family_chunk_id;
        selection.family_number=session_context->family_number;
        selection.resident_guid=session_context->resident_guid;
        selection.resident_neighbor_id=session_context->resident_neighbor_id;
        selection.runtime_avatar_id=runtime_avatar_id;
        // This launch remains unpublished until all execution graphs are restored.
        if(session_context->single_resident_session){
            session_restore=odh::SingleResidentSessionRestore::prepare(session_context->restore_archive,
                *session_context->resources,selection,*session_baseline,*lot,*bootstrap_globals,family_global);
            *lot=session_restore->lot();*bootstrap_globals=session_restore->bootstrap();
            family_global=session_restore->family_global();
            if(active_family&&session_restore->family_budget())
                active_family->family.budget=*session_restore->family_budget();
            auto candidate=session_restore->take_world();
            if(!candidate||!restored_world->world->publish_snapshot(candidate->snapshot()))
                throw odh::FormatError("single-resident session World candidate could not be published");
        }else{
            household_restore=odh::HouseholdSessionRestore::prepare(session_context->restore_archive,
                *session_context->resources,selection,capture_household_roster(),*session_baseline,*lot,*bootstrap_globals,family_global);
            *lot=household_restore->lot();*bootstrap_globals=household_restore->bootstrap();
            family_global=household_restore->family_global();
            if(active_family&&household_restore->family_budget())
                active_family->family.budget=*household_restore->family_budget();
            auto candidate=household_restore->take_world();
            if(!candidate||!restored_world->world->publish_snapshot(candidate->snapshot()))
                throw odh::FormatError("household session World candidate could not be published");
        }
        std::vector<std::uint8_t>{}.swap(session_context->restore_archive); // Release encoded input.
    }
    const bool restoring_session=bool(session_restore)||bool(household_restore);
    odh::World& world=saved_world_main?*restored_world->world:*world_owner;
    if(saved_world_main && simi && simi->global_data.size()>8) {
        const auto& g=simi->global_data;
        odh::Ts1Clock clock;
        if(g[0]>=0 && g[1]>=0 && g[5]>=0 && g[6]>=0 && g[7]>=0 &&
           clock.initialize_simi(static_cast<unsigned>(g[0]),static_cast<unsigned>(g[1]),
               static_cast<unsigned>(g[5]),static_cast<unsigned>(g[6]),static_cast<unsigned>(g[7]),g[8])) {
            simulation_clock=clock;simulation_profile="Simitone_default_TS1_reference";
        } else simulation_profile="unsupported_SIMI_clock";
        const auto* saved_sim=session_restore?&session_restore->simulation():household_restore?&household_restore->simulation():nullptr;
        if(saved_sim) {
            if(*saved_sim){
                if(!simulation_clock)simulation_clock.emplace();
                if(!simulation_clock->restore((**saved_sim).clock))throw odh::FormatError("invalid restored simulation clock");
                for(const auto& a:(**saved_sim).avatars){odh::Ts1MotiveDecay decay;
                    if(!decay.restore(a.decay))throw odh::FormatError("invalid restored motive carries");
                    simulation_motives.emplace(a.id,decay);}
                simulation_profile="Simitone_default_TS1_reference";
            }else {simulation_clock.reset();simulation_profile="legacy_session_without_simulation_state";}
        }
        if(simulation_clock){
            simulation_motives.try_emplace(runtime_avatar_id);
        }
    }

    std::optional<odh::EntityPosition> paired_primary_start;
    const auto entity_count=[&]{std::size_t count=0;std::int16_t cursor=0;
        while(const auto next=world.next_entity(cursor)){++count;cursor=*next;}return count;};
    const auto paired_entity_count=second_saved_main?entity_count():0;
    if(second_saved_main){
        const auto id=static_cast<std::int16_t>(headless_second_main->avatar.id);
        const auto* resident=world.find(id);
        const auto record=std::find_if(restored_world->records.begin(),restored_world->records.end(),
            [&](const auto& item){return item.saved.object_id==headless_second_main->avatar.id;});
        if(record==restored_world->records.end()||!record->tail.person||record->serialized_object_type!=2||
           record->saved.guid!=second_data.binding->guid||!resident||resident->kind!=odh::EntityKind::avatar||
           !resident->definition||resident->definition->guid!=second_data.binding->guid||!resident->position)
            throw odh::FormatError("second saved World avatar does not match exact positioned person resource");
        if(active_family&&!odh::FamilyContext::contains(*active_family,second_data.binding->guid))
            throw odh::FormatError("second saved avatar is not a member of selected FAMI");
        headless_second_main->avatar.position=*resident->position;
        paired_primary_start=world.find(runtime_avatar_id)->position;
    }
    std::optional<std::vector<odh::ReconciledHouseholdMember>> family_world_reconciliation;
    if(saved_world_main&&active_family){
        family_world_reconciliation=odh::reconcile_session_household(active_family->family.member_guids,world);
    }
    struct HouseholdRuntime {
        Resource resource;
        Loaded data;
        std::int16_t id=0;
        std::uint16_t main=0;
        odh::EntityPosition start;
        std::map<std::uint16_t,odh::SoundEvent> sounds;
        std::unique_ptr<odh::EntityThreadControllers> controllers;
        std::optional<odh::Playback> playback;
        std::optional<odh::AvatarBodyDescriptor> body_state;
        std::unique_ptr<SceneRenderer> scene_renderer;
        std::unique_ptr<odh::Vm> vm;
        std::optional<odh::WorldRoomRouteGrid> route_grid;
        std::unique_ptr<odh::Routing> routing;
        odh_host::RoomPortalSession room_portals;
        std::unique_ptr<odh_host::HouseholdGoHereSession> go_here;
        std::map<std::tuple<std::string,std::string,std::uint16_t>,odh::CodeOwner> action_code_owners;
        std::optional<SavedObjectSelection> seated_action_target;
        std::optional<std::uint64_t> seated_action_actor_generation;
        std::optional<std::uint16_t> seated_action_code;
        std::optional<std::uint64_t> route_spatial_revision;
        std::optional<std::int16_t> route_ep5_unknown_caller,route_ep5_unknown_stack_object;
        std::optional<std::uint16_t> route_ep5_unknown_action;
        std::size_t route_movement_callbacks=0,route_snapshot_refreshes=0;
        bool go_here_cancel_pending=false;
        std::string ui_go_here_status="not_requested",ui_go_here_diagnostic;
        std::string route_reason="runtime_route_context_not_prepared";
    };
    std::vector<std::unique_ptr<HouseholdRuntime>> household_runtime;
    const auto household_entity_count=entity_count();
    if(session_context&&!session_context->household_members.empty()){
        if(!saved_world_main||!restored_world||!active_family||!family_world_reconciliation||
           headless_second_main||session_restore||session_context->single_resident_session)
            throw odh::FormatError("household execution requires a fresh integrated restored family World");
        const auto& members=session_context->household_members;
        if(members.size()>8||members.size()!=active_family->family.member_guids.size())
            throw odh::FormatError("household execution requires every family member within the eight-resident profile");
        std::set<std::uint32_t> guids;
        bool primary_found=false;
        for(const auto& member:members){
            if(!member.guid||!guids.insert(member.guid).second||
               !odh::FamilyContext::contains(*active_family,member.guid))
                throw odh::FormatError("household execution contains a duplicate or mismatching family member");
            const auto found=std::find_if(family_world_reconciliation->begin(),family_world_reconciliation->end(),
                [&](const auto& item){return item.guid==member.guid;});
            if(found==family_world_reconciliation->end()||
               found->presence!=odh::HouseholdMemberPresence::present_with_position||!found->object_id||!found->position)
                throw odh::FormatError("household member is absent or unpositioned in the restored World; placement is not implemented");
            Resource resource{member.character_path.string(),{},true};
            const auto pinned=pinned_characters.find(std::filesystem::canonical(member.character_path));
            if(pinned==pinned_characters.end())throw odh::FormatError("household member lacks loaded-byte provenance");
            resource.frozen=pinned->second.bytes;
            auto data=load(resource,odh::RoutineScope::private_object,member.object_definition_id,true);
            if(data.binding->version!=138||data.object_type!=2||data.binding->guid!=member.guid||
               !data.glob||data.glob->name.empty()||data.glob->name.find_first_of("/\\:")!=std::string::npos||
               data.glob->name.find('\0')!=std::string::npos||fold(data.glob->name+".iff")!=fold(semi->entry))
                throw odh::FormatError("household member does not match its exact person resource and semiglobal");
            const auto main=data.entry_points?(data.entry_points->entries.size()>1?
                data.entry_points->entries[1].action:std::uint16_t{0}):data.functions->main;
            if(!main||odh::routine_key(main).scope!=odh::RoutineScope::private_object||
               !data.routines.count(odh::routine_key(main)))
                throw odh::FormatError("household member Main is absent from its exact private owner");
            if(*found->object_id==runtime_avatar_id){
                if(member.guid!=person.binding->guid||main!=root||
                   std::filesystem::canonical(member.character_path)!=std::filesystem::canonical(avatar->archive))
                    throw odh::FormatError("selected household resident resource does not match the primary runtime");
                primary_found=true;
                continue;
            }
            data.routines.insert(semi_data.routines.begin(),semi_data.routines.end());
            auto runtime=std::make_unique<HouseholdRuntime>();
            runtime->resource=std::move(resource);runtime->data=std::move(data);
            runtime->id=*found->object_id;runtime->main=main;runtime->start=*found->position;
            runtime->sounds=load_sounds(runtime->resource);
            household_runtime.push_back(std::move(runtime));
        }
        if(!primary_found)throw odh::FormatError("selected resident is absent from household execution");
        std::sort(household_runtime.begin(),household_runtime.end(),
            [](const auto& left,const auto& right){return left->id<right->id;});
    }
    const odh::GeometricRoomMap* vm_room_map=nullptr;
    profile_mark("tree-by-name-strings");
    if(!saved_world_main){
        if(!world.create(runtime_avatar_id,odh::EntityKind::avatar,{0,101,3,16})||!world.define_type(runtime_avatar_id,person.binding->guid))throw odh::FormatError("avatar fixture construction failed");
        // Explicit reference constructor subset, not completed native Init.
        for(std::int16_t index=0;index<79;++index) {
            if(index==1||index==2||index==3||index==11||index==25||index==26||index==29||index==41||index==62||index==67)continue;
            if(!world.declare_object_word(runtime_avatar_id,index,0))throw odh::FormatError("reference constructor object word failed");
        }
        if(!world.declare_object_word(runtime_avatar_id,8,signed_word(flags.value_or(256))))throw odh::FormatError("flags fixture construction failed");
    }
    if(headless_fixture_avatar){
        if(!world.create(runtime_avatar_id,odh::EntityKind::avatar,{0,101,3,16})||
           !world.define_type(runtime_avatar_id,person.binding->guid))
            throw odh::FormatError("headless fixture avatar ID collides or World budget is exhausted");
        for(std::int16_t index=0;index<79;++index){
            if(index==1||index==2||index==3||index==11||index==25||index==26||index==29||index==41||index==62||index==67)continue;
            if(!world.declare_object_word(runtime_avatar_id,index,0))throw odh::FormatError("fixture avatar object word failed");
        }
        if(!world.declare_object_word(runtime_avatar_id,8,256)||!world.set_position(runtime_avatar_id,headless_fixture_avatar->position))
            throw odh::FormatError("fixture avatar initialization failed");
    }
    if(headless_fixture_occupant_avatar){
        const auto id=static_cast<std::int16_t>(headless_fixture_occupant_avatar->id);
        if(!world.create(id,odh::EntityKind::avatar,{0,101,3,16})||
           !world.define_type(id,person.binding->guid)||!world.declare_object_word(id,8,256)||
           !world.set_position(id,headless_fixture_occupant_avatar->position))
            throw odh::FormatError("headless occupant avatar initialization failed");
        if(person.entry_points&&!world.define_entry_points(id,person.entry_points->entries))
            throw odh::FormatError("headless occupant avatar EntryPoint binding failed");
    }
    if(headless_second_main&&!second_saved_main){
        const auto id=static_cast<std::int16_t>(headless_second_main->avatar.id);
        if(!world.create(id,odh::EntityKind::avatar,{0,101,3,16})||
           !world.define_type(id,second_data.binding->guid)||!world.set_position(id,headless_second_main->avatar.position))
            throw odh::FormatError("second Main avatar fixture construction failed");
        for(std::int16_t index=0;index<79;++index){
            if(index==1||index==2||index==3||index==11||index==25||index==26||index==29||index==41||index==62||index==67)continue;
            if(!world.declare_object_word(id,index,0))throw odh::FormatError("second Main avatar object word initialization failed");
        }
        if(!world.declare_object_word(id,8,256)||
           (second_data.entry_points&&!world.define_entry_points(id,second_data.entry_points->entries)))
            throw odh::FormatError("second Main avatar fixture metadata initialization failed");
    }
    if(age&&(!saved_world_main||headless_fixture_avatar)&&
       !world.set_person_data(runtime_avatar_id,58,static_cast<std::int16_t>(*age)))
        throw odh::FormatError("age fixture construction failed");
    if(person.entry_points&&!world.define_entry_points(runtime_avatar_id,person.entry_points->entries))throw odh::FormatError("avatar entry point binding failed");
    std::map<odh::CodeOwner,odh::TuningTables> saved_action_tuning;
    odh::TuningResources tuning_resources;
    tuning_resources.globals=std::move(shared.tuning);
    auto second_main_tuning=headless_second_main?std::optional<odh::TuningOwner>{
        odh::TuningOwner{second_data.tuning,semi_data.tuning,odh::TuningSemiState::loaded}}:std::nullopt;
    for(const auto& member:household_runtime)
        tuning_resources.owners.emplace(member->id,odh::TuningOwner{
            member->data.tuning,semi_data.tuning,odh::TuningSemiState::loaded});
    tuning_resources.owners.emplace(runtime_avatar_id,odh::TuningOwner{std::move(person.tuning),std::move(semi_data.tuning),odh::TuningSemiState::loaded});
    if(second_main_tuning)tuning_resources.owners.emplace(headless_second_main->avatar.id,
        std::move(*second_main_tuning));
    if(action)tuning_resources.owners.emplace(2,odh::TuningOwner{std::move(action_data->tuning),{},odh::TuningSemiState::absent});
    const odh::BconTuning tuning(std::move(tuning_resources));
    std::vector<odh::TuningValue> tuning_reads;
    const auto world_memory=world.vm_memory();auto memory=world_memory;
    memory.read=[&](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t index)->std::optional<std::int16_t> {
        if(scope==32){const auto value=neighbors?neighbors->person_data(frame.context.stack_object,index):std::nullopt;
            if(!value)return {};
            if(diagnostics.retain(Diagnostic::neighbor_reads,neighbor_reads.size(),20000))neighbor_reads.push_back(*value);
            return value->value;}
        if(scope==6){
            const auto clock=simulation_clock?simulation_clock->global(index):std::nullopt;
            const auto runtime=bootstrap_globals?bootstrap_globals->read(index):std::nullopt;
            const auto family_value=index==9?family_global:std::nullopt;
            const auto value=clock?clock:family_value?family_value:runtime?runtime:(lot?lot->read(index):std::nullopt);
            if(value&&diagnostics.retain(Diagnostic::global_reads,global_reads.size(),20000))
                global_reads.push_back({index,*value,clock?GlobalSource::clock:family_value?GlobalSource::family:runtime?GlobalSource::bootstrap:GlobalSource::lot});
            return value;}
        if(scope!=26)return world_memory.read(frame,scope,index);
        const auto supplemental=saved_action_tuning.find(frame.context.code_owner);
        if(supplemental!=saved_action_tuning.end()){
            const auto address=odh::BconTuning::address(index);
            if(address.scope==odh::RoutineScope::private_object){
                const auto table=supplemental->second.find(address.table);
                // This map entry means the exact owner resource was loaded.
                // Simitone's private tuning lookup defaults a missing BCON/key
                // to zero; an owner absent from the map remains unsupported.
                if(table==supplemental->second.end()||address.index>=table->second.constants.size())return 0;
                const auto raw=table->second.constants[address.index];
                const auto signed_value=raw<0x8000?static_cast<std::int16_t>(raw):
                    static_cast<std::int16_t>(static_cast<std::int32_t>(raw)-0x10000);
                odh::TuningValue value{signed_value,address,frame.context.code_owner,false};
                if(diagnostics.retain(Diagnostic::tuning_reads,tuning_reads.size(),20000))tuning_reads.push_back(value);
                return signed_value;
            }
        }
        const auto value=tuning.read(frame.context.code_owner,index);
        if(!value)return {};
        if(diagnostics.retain(Diagnostic::tuning_reads,tuning_reads.size(),20000))tuning_reads.push_back(*value);
        return value->value;
    };
    memory.room_by_temp0=[&](const odh::VmFrame&,std::int16_t temp0,std::int16_t index)->std::optional<std::int16_t> {
        return vm_room_map?odh::room_by_temp0_data(*vm_room_map,temp0,index):std::nullopt;
    };
    memory.write=[&](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t index,std::int16_t value){
        if(scope==6){
            if(index==9&&family_global){family_global=value;return true;}
            if(bootstrap_globals&&bootstrap_globals->read(index))return bootstrap_globals->write(index,value);
            return lot&&lot->write(index,value);}
        return world_memory.write(frame,scope,index,value);
    };
    odh::RoutineOwners owners{{runtime_avatar_id,std::move(person.routines)}};
    if(action){owners.emplace(2,std::move(action_data->routines));
        if(!world.create(2,odh::EntityKind::object)||!world.define_type(2,action_data->binding->guid))throw odh::FormatError("action target construction failed");
        if(action_data->entry_points&&!world.define_entry_points(2,action_data->entry_points->entries))throw odh::FormatError("action entry point binding failed");}
    std::uint64_t rng_requests=0;
    std::vector<odh::RandomRecord> random_reads;
    odh::EntityThreadControllers thread_controllers(world,runtime_avatar_id,
        [&]{++rng_requests;random.next(1);});
    std::unique_ptr<odh::EntityThreadControllers> second_main_thread;
    if(headless_second_main)second_main_thread=std::make_unique<odh::EntityThreadControllers>(world,
        static_cast<std::int16_t>(headless_second_main->avatar.id),[&]{++rng_requests;random.next(1);});
    for(auto& member:household_runtime)
        member->controllers=std::make_unique<odh::EntityThreadControllers>(world,member->id,
            [&]{++rng_requests;random.next(1);});
    const auto any_interaction_references_target=[&](std::int16_t target){
        if(thread_controllers.interactions().references_target(target))return true;
        if(second_main_thread&&second_main_thread->interactions().references_target(target))return true;
        return std::any_of(household_runtime.begin(),household_runtime.end(),[&](const auto& member){
            return member->controllers&&member->controllers->interactions().references_target(target);
        });
    };
    odh::EntityVmScheduler entity_scheduler(64);
    struct PendingCreateObject {
        odh::Vm* vm = nullptr;
        std::uint64_t ticket = 0;
        odh::CreateObjectInstanceRequest request;
        std::int16_t stack_object = 0;
    };
    struct PendingRemoveObject {
        odh::Vm* vm = nullptr;
        std::uint64_t ticket = 0;
        std::int16_t object = 0;
        std::uint64_t generation = 0;
        bool defer_until_unreferenced = false;
    };
    struct RuntimeObjectThread {
        std::unique_ptr<odh::Vm> vm;
        std::unique_ptr<odh::Sleep> sleep;
        odh::CodeOwner owner = 0;
    };
    std::deque<PendingCreateObject> pending_create_objects;
    std::deque<PendingRemoveObject> pending_remove_objects;
    std::map<std::int16_t,std::unique_ptr<RuntimeObjectThread>> runtime_object_threads;
    std::map<odh::CodeOwner,Resource> saved_action_resources;
    std::map<std::pair<odh::CodeOwner,std::uint16_t>,odh::StringTable> tree_name_local_strings;
    std::set<std::pair<odh::CodeOwner,std::uint16_t>> tree_name_local_string_attempted;
    auto tree_name_global_strings=saved_world_main?load_strings(*globals,{303}):
        std::map<std::uint16_t,odh::StringTable>{};
    std::string last_create_object_diagnostic;
    std::string last_remove_object_diagnostic;
    auto& sleep=thread_controllers.sleep();
    auto& queue=thread_controllers.interactions();
    const auto random_handler=odh::random_primitive(random,memory,[&](const odh::RandomRecord& value){
        if(diagnostics.retain(Diagnostic::random_reads,random_reads.size(),10000))random_reads.push_back(value);
    });
    const auto sleep_handler=thread_controllers.sleep_primitive();
    const auto queue_handler=thread_controllers.interaction_primitive();
    const auto lot_handler=lot?lot->primitive([&](const auto& value){
        if(diagnostics.retain(Diagnostic::zoning_reads,zoning_reads.size(),10000))zoning_reads.push_back(value);
    }):odh::VmPrimitive{};
    const auto neighbor_handler=neighbors?neighbors->primitive(memory,[&](const auto& value){
        if(diagnostics.retain(Diagnostic::neighbor_next,neighbor_next.size(),10000))neighbor_next.push_back(value);
    }):odh::VmPrimitive{};
    std::vector<odh::EntityNext> entity_next;
    std::vector<std::vector<std::int16_t>> multitile_groups;
    if(restored_world){
        multitile_groups.reserve(restored_world->multitile_groups.size());
        for(const auto& group:restored_world->multitile_groups)multitile_groups.push_back(group.members);
    }
    const auto entity_handler=odh::entity_next_primitive(world,memory,[&](const auto& value){
        if(diagnostics.retain(Diagnostic::entity_next,entity_next.size(),10000))entity_next.push_back(value);
    },std::move(multitile_groups));
    const odh::VmPrimitive grab_handler=[&](const odh::VmFrame& frame,const odh::Instruction& instruction,
        const std::array<std::int16_t,20>&,const odh::VmDispatch&)->odh::PrimitiveOutcome{
        if(instruction.opcode!=4)return {};
        const auto* actor=world.find(frame.context.caller);
        if(!actor||!actor->position)return {odh::PrimitiveDisposition::goto_false,{}};
        return {world.move_to_slot(actor->id,0,frame.context.stack_object,actor->position->direction)?
            odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::goto_false,{}};
    };
    std::optional<odh::RelationshipState> original_relationships;
    if(neighbors&&!restoring_session)original_relationships.emplace(*neighbors);
    auto* relationships=household_restore?&household_restore->relationships():session_restore?&session_restore->relationships():
        original_relationships?&*original_relationships:nullptr;
    std::vector<odh::RelationshipRecord> relationship_records;
    const auto relationship_handler=relationships?relationships->primitive(world,memory,[&](const auto& value){
        if(diagnostics.retain(Diagnostic::relationship,relationship_records.size(),10000))relationship_records.push_back(value);
    }):odh::VmPrimitive{};
    std::vector<odh::InventoryFind> inventory_records;
    const auto inventory_handler=ngbh?odh::inventory_find_primitive(*ngbh,world,[&](const auto& value){
        if(diagnostics.retain(Diagnostic::inventory,inventory_records.size(),10000))inventory_records.push_back(value);
    }):odh::VmPrimitive{};
    std::optional<odh::AvatarBodyDescriptor> body_state;
    std::optional<odh::host::AnimationCatalog> animation_catalog;
    std::map<std::uint16_t,odh::StringTable> global_animation_tables;
    std::map<std::int16_t,std::map<std::uint16_t,odh::StringTable>> private_animation_tables;
    std::map<std::int16_t,std::uint16_t> animation_table_ids;
    std::string animation_resolution_diagnostic;
    std::vector<odh::PlaybackEvent> animation_events;
    std::uint64_t animation_event_bytes=0;
    std::optional<odh::Playback> playback;
    std::optional<odh::Playback> second_main_playback;
    std::function<bool(std::int16_t)> bind_world_object_tables;
    std::set<std::int16_t> saved_stair_portal_endpoint_ids;
    if(!animation_sources.empty()){
        animation_catalog.emplace(animation_sources);
        global_animation_tables=load_strings(*globals,{128,130,150,151,156,157});
        const auto bind_tables=[&](std::int16_t owner,const Resource& resource,const Loaded& data){
            const auto base=data.binding->animation_table;
            animation_table_ids.emplace(owner,base);
            private_animation_tables.emplace(owner,load_strings(resource,{base,static_cast<std::uint16_t>(base+1),129,130}));
        };
        bind_tables(runtime_avatar_id,*avatar,person);
        if(headless_second_main)bind_tables(static_cast<std::int16_t>(headless_second_main->avatar.id),
            second_avatar?*second_avatar:*avatar,second_data);
        for(const auto& member:household_runtime)bind_tables(member->id,member->resource,member->data);
        if(action)bind_tables(2,*action,*action_data);
        bind_world_object_tables=[&](std::int16_t owner){
            if(private_animation_tables.count(owner))return true;
            const auto* entity=world.find(owner);
            if(!entity||entity->kind!=odh::EntityKind::object||!entity->definition||!definition_index)return false;
            const auto* candidate=definition_index->unique(entity->definition->guid);
            if(!candidate)return false;
            const Resource resource{candidate->source,candidate->asset,candidate->asset.empty()};
            std::optional<odh::ObjdBinding> binding;
            with_resource(resource,[&](const auto& view){
                odh::Limits limits;limits.iff_chunks=4096;
                odh::iff_chunks(view,[&](const auto& chunk){
                    if(chunk.type=="OBJD"&&chunk.id==candidate->chunk_id){
                        if(binding)throw odh::FormatError("ambiguous animation-owner OBJD");
                        binding=odh::read_objd_binding(chunk.payload);
                    }
                },limits);
            });
            const bool valid_saved_group=binding&&entity->definition->master_guid&&restored_world&&
                odh::saved_world_has_multitile_member(*restored_world,owner,*entity->definition->master_guid);
            if(!binding||binding->guid!=candidate->definition.guid||
               binding->master_id!=candidate->definition.master_id||
               binding->sub_index!=candidate->definition.sub_index||
               (binding->master_id!=0&&!valid_saved_group))return false;
            const auto base=binding->animation_table;
            auto tables=load_strings(resource,{base,static_cast<std::uint16_t>(base+1),129,130});
            animation_table_ids.emplace(owner,base);
            private_animation_tables.emplace(owner,std::move(tables));
            return true;
        };
    }
    const auto resolve_animation=[&](const odh::VmFrame& frame,std::uint32_t source,std::uint16_t id,std::optional<std::int16_t> posture)
        ->std::shared_ptr<const odh::AnimationHeader>{
        animation_resolution_diagnostic.clear();
        try{
            const auto* actor=world.find(frame.context.caller);
            if(!actor||actor->person_data.size()<=58)throw odh::FormatError("animation actor age unavailable");
            if(posture&&*posture==-1){
                auto resource=animation_catalog->resolve("a2o-rarm-carry-loop");
                const auto* header=&resource->header;
                return std::shared_ptr<const odh::AnimationHeader>(std::move(resource),header);
            }
            const odh::AvatarBodyDescriptor* actor_body=body_state?&*body_state:nullptr;
            if(frame.context.caller!=runtime_avatar_id){
                actor_body=nullptr;
                for(const auto& member:household_runtime)if(member->id==frame.context.caller&&member->body_state)
                    actor_body=&*member->body_state;
                if(posture&&!actor_body)
                    throw odh::FormatError("additional resident posture animation requires its own derived body descriptor");
            }
            const bool child=actor_body?actor_body->child:actor->person_data[58]<18;
            const odh::StringTable* table=nullptr;
            const auto find_table=[](const auto& tables,std::uint16_t key)->const odh::StringTable*{
                const auto i=tables.find(key);return i==tables.end()?nullptr:&i->second;
            };
            if(posture&&actor_body){
                auto resource=animation_catalog->resolve(actor_body->walk.at(static_cast<std::size_t>(*posture)));
                const auto* header=&resource->header;
                return std::shared_ptr<const odh::AnimationHeader>(std::move(resource),header);
            }
            if(posture){table=find_table(global_animation_tables,child?151:150);id=static_cast<std::uint16_t>(*posture);}
            else if(source==0||source==65536){
                auto owner=source==0?frame.context.code_owner:frame.context.stack_object;
                auto tables=private_animation_tables.find(owner);
                if(tables==private_animation_tables.end()&&source==0){
                    // A saved external frame uses a resource CodeOwner ID, while
                    // FreeSO's Object animation scope reads the executing
                    // GameObject's resource. The callee is that object for an
                    // object-owned callback such as a portal's stair BHAV.
                    const auto bind_entity=[&](std::int32_t entity_id){
                        if(entity_id<=0||entity_id>32767)return false;
                        const auto object_id=static_cast<std::int16_t>(entity_id);
                        if(!bind_world_object_tables||!bind_world_object_tables(object_id))return false;
                        owner=static_cast<std::uint32_t>(object_id);
                        tables=private_animation_tables.find(object_id);
                        return tables!=private_animation_tables.end();
                    };
                    if(!bind_entity(frame.context.callee))
                        bind_entity(static_cast<std::int32_t>(frame.context.stack_object));
                }
                if(tables==private_animation_tables.end()&&source==65536&&frame.context.stack_object>0&&
                   frame.context.stack_object<=32767){
                    const auto stack_owner=static_cast<std::int16_t>(frame.context.stack_object);
                    if(bind_world_object_tables&&bind_world_object_tables(stack_owner)){
                        owner=static_cast<std::uint32_t>(stack_owner);
                        tables=private_animation_tables.find(stack_owner);
                    }
                }
                const auto base=animation_table_ids.find(owner);
                if(tables==private_animation_tables.end()||base==animation_table_ids.end())throw odh::FormatError("animation owner not supplied by host");
                table=find_table(tables->second,static_cast<std::uint16_t>(base->second+(child?1:0)));
                if(!table)table=find_table(tables->second,child?130:129);
            }else if(source==1)table=find_table(global_animation_tables,128);
            else if(source==2)table=find_table(global_animation_tables,130);
            else if(source==3)table=find_table(global_animation_tables,child?157:156);
            else throw odh::FormatError("animation source outside explicit host profile");
            if(!table)throw odh::FormatError("animation STR# table absent including fallback");
            const auto* entry=odh::string_in_language(*table,1,id);
            if(!entry)throw odh::FormatError("animation STR# index absent in explicit language 1");
            auto resource=animation_catalog->resolve(entry->value);
            const auto* header=&resource->header;
            return std::shared_ptr<const odh::AnimationHeader>(std::move(resource),header);
        }catch(const odh::FormatError& e){animation_resolution_diagnostic=e.what();return {};}
    };
    if(animation_catalog)playback.emplace(world,
        [&](const auto& frame,std::uint32_t source,std::uint16_t id){return resolve_animation(frame,source,id,{});},odh::PlaybackLimits{},
        [&](const auto& frame,std::int16_t posture){return resolve_animation(frame,0,0,posture);});
    if(animation_catalog&&headless_second_main)second_main_playback.emplace(world,
        [&](const auto& frame,std::uint32_t source,std::uint16_t id){return resolve_animation(frame,source,id,{});},odh::PlaybackLimits{},
        [&](const auto& frame,std::int16_t posture){return resolve_animation(frame,0,0,posture);});
    if(animation_catalog)for(auto& member:household_runtime)
        member->playback.emplace(world,
            [&](const auto& frame,std::uint32_t source,std::uint16_t id){return resolve_animation(frame,source,id,{});},odh::PlaybackLimits{},
            [&](const auto& frame,std::int16_t posture){return resolve_animation(frame,0,0,posture);});
    const auto animation_handler=playback?playback->primitive():odh::VmPrimitive{};
    struct SoundRecord {odh::SoundRequest request;std::string status;};
    std::vector<SoundRecord> sound_records;
    const auto sound_handler=odh::sound_primitive(world,[&](const odh::VmFrame& frame,std::uint16_t id)
        ->std::optional<odh::SoundEvent>{
        for(const auto& member:household_runtime)if(frame.context.code_owner==member->id){
            const auto sound=member->sounds.find(id);
            if(sound!=member->sounds.end())return sound->second;
            const auto semi_sound=semiglobal_sounds.find(id);
            if(semi_sound!=semiglobal_sounds.end())return semi_sound->second;
            const auto global_sound=global_sounds.find(id);
            return global_sound!=global_sounds.end()?std::optional<odh::SoundEvent>{global_sound->second}:std::nullopt;
        }
        const bool second_owner=headless_second_main&&frame.context.code_owner==headless_second_main->avatar.id;
        const auto& private_sounds=second_owner?second_person_sounds:person_sounds;
        const auto private_sound=private_sounds.find(id);
        if((frame.context.code_owner==runtime_avatar_id||second_owner)&&
           private_sound!=private_sounds.end())return private_sound->second;
        const auto semi_sound=semiglobal_sounds.find(id);
        if(semi_sound!=semiglobal_sounds.end())return semi_sound->second;
        const auto global_sound=global_sounds.find(id);
        if(global_sound!=global_sounds.end())return global_sound->second;
        return {};
    },{},[&](const odh::SoundRequest& request,const std::string& status){
        if(diagnostics.retain(Diagnostic::sound,sound_records.size(),4096))sound_records.push_back({request,status});
    });
    const auto look_towards_handler=odh::look_towards_primitive(world);
    const auto distance_to_handler=odh::get_distance_to_primitive(world);
    const auto direction_to_handler=odh::get_direction_to_primitive(world,memory);
    const auto object_type_handler=odh::object_type_primitive(world);
    const auto notify_handler=odh::notify_idle_primitive(world,
        [&](std::int16_t id){return entity_scheduler.interrupt(id);});
    struct SnapRuntime {
        odh::SlotTable slots;
        std::unique_ptr<odh::Snap> controller;
        odh::VmPrimitive primitive;
    };
    // Snap controllers retain their VM binding; residents share the World and
    // SLOT target, but must not reuse another resident's bound controller.
    std::map<std::tuple<std::int16_t,const odh::Vm*,bool>,std::unique_ptr<SnapRuntime>> snap_runtimes;
    std::optional<odh::SlotTable> global_snap_slots;
    bool global_snap_slots_attempted=false;
    std::string global_snap_slots_diagnostic;
    const auto global_snap_slot_table=[&]() -> const odh::SlotTable* {
        if(global_snap_slots_attempted)return global_snap_slots?&*global_snap_slots:nullptr;
        global_snap_slots_attempted=true;
        if(!globals){global_snap_slots_diagnostic="global resource is unavailable";return nullptr;}
        try{
            with_resource(*globals,[&](const auto& view){
                std::optional<odh::SlotTable> selected;
                odh::Limits limits;limits.iff_chunks=4096;
                odh::iff_chunks(view,[&](const auto& chunk){
                    if(chunk.type=="SLOT"&&chunk.id==100){
                        if(selected)throw odh::FormatError("ambiguous global SLOT100 resource");
                        selected=odh::read_slot(chunk.payload);
                    }
                },limits);
                global_snap_slots=std::move(selected);
            });
            if(!global_snap_slots)global_snap_slots_diagnostic="global SLOT100 resource is absent";
        }catch(const std::exception& error){global_snap_slots_diagnostic=error.what();}
        return global_snap_slots?&*global_snap_slots:nullptr;
    };
    std::function<bool(std::int16_t,const odh::EntityPosition&)> snap_placement_validator;
    std::function<odh::Snap::StairPlacementStatus(std::int16_t,std::int16_t,const odh::EntityPosition&)>
        snap_stair_placement_validator;
    std::string snap_stair_placement_diagnostic;
    odh::VmPrimitive snap_handler=[&](const odh::VmFrame& frame,const odh::Instruction& instruction,
                                      const auto& tuning,const odh::VmDispatch& dispatch) {
        const auto stack_object=frame.context.stack_object;
        if(stack_object<=0||stack_object>32767)
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"Snap stack object is outside the World ID range"};
        const auto object_id=static_cast<std::int16_t>(stack_object);
        const auto mode=static_cast<std::uint16_t>(instruction.operands[2]|(instruction.operands[3]<<8));
        const bool global_slot_scope=mode==4;
        const auto cache_key=std::make_tuple(object_id,dispatch.origin,global_slot_scope);
        auto runtime=snap_runtimes.find(cache_key);
        if(runtime==snap_runtimes.end()) {
            const auto* entity=world.find(object_id);
            if(!entity)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "Snap StackObject "+std::to_string(object_id)+" is absent from World"};
            std::optional<odh::ObjdBinding> binding;
            std::optional<odh::SlotTable> slots;
            std::uint16_t slot_id=0;
            if(global_slot_scope){
                if(entity->kind!=odh::EntityKind::avatar||object_id!=frame.context.caller)
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                        "global SLOT Snap is supported only when the avatar is its own StackObject"};
                if(object_id==runtime_avatar_id&&person.binding)binding=*person.binding;
                else for(const auto& member:household_runtime)
                    if(member->id==object_id&&member->data.binding){binding=*member->data.binding;break;}
                if(!binding)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                    "global SLOT Snap avatar definition is unavailable"};
                const auto* global_slots=global_snap_slot_table();
                if(!global_slots)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                    "global SLOT100 could not be loaded: "+global_snap_slots_diagnostic};
                slots=*global_slots;slot_id=100;
            }else{
                if(entity->kind!=odh::EntityKind::object)
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                        "object SLOT Snap requires a game-object StackObject"};
                if(!entity->definition)
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                        "Snap StackObject "+std::to_string(object_id)+" has no object definition"};
                if(!entity->position)
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                        "Snap StackObject "+std::to_string(object_id)+" has no World position"};
                if(!definition_index)
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                        "Snap object definition catalog is unavailable"};
                const auto* candidate=definition_index->unique(entity->definition->guid);
                if(!candidate)
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"Snap object definition is missing or ambiguous"};
                const Resource resource{candidate->source,candidate->asset,candidate->asset.empty()};
                try {
                    with_resource(resource,[&](const auto& view){
                        odh::Limits limits;limits.iff_chunks=4096;
                        odh::iff_chunks(view,[&](const auto& chunk){
                            if(chunk.type=="OBJD"&&chunk.id==candidate->chunk_id) {
                                if(binding)throw odh::FormatError("ambiguous Snap OBJD resource");
                                binding=odh::read_objd_binding(chunk.payload);
                            }
                        },limits);
                        bool matched_saved_group=binding&&entity->definition->master_guid&&restored_world&&
                            odh::saved_world_has_multitile_member(*restored_world,object_id,
                                *entity->definition->master_guid);
                        if(!binding||binding->guid!=candidate->definition.guid||
                           binding->master_id!=candidate->definition.master_id||
                           binding->sub_index!=candidate->definition.sub_index||
                           (binding->master_id!=0&&!matched_saved_group))
                            throw odh::FormatError("Snap OBJD identity or supported multi-tile profile mismatch");
                        if(binding->slot_table==0)throw odh::FormatError("Snap OBJD has no SLOT binding");
                        with_resource(resource,[&](const auto& second_view){
                            odh::iff_chunks(second_view,[&](const auto& chunk){
                                if(chunk.type=="SLOT"&&chunk.id==binding->slot_table) {
                                    if(slots)throw odh::FormatError("ambiguous Snap SLOT resource");
                                    slots=odh::read_slot(chunk.payload);
                                }
                            },limits);
                        });
                    });
                } catch(const std::exception& error) {
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                        std::string("Snap resource/profile unsupported: ")+error.what()};
                }
                if(!binding)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"Snap OBJD resource is absent"};
                slot_id=binding->slot_table;
            }
            if(!slots)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"Snap SLOT resource is absent"};
            const bool validated_stair_endpoint=saved_stair_portal_endpoint_ids.count(object_id)!=0;
            auto entry=std::make_unique<SnapRuntime>();entry->slots=std::move(*slots);
            entry->controller=std::make_unique<odh::Snap>(world,object_id,*binding,entry->slots,slot_id,
                [&](std::int16_t actor,const odh::EntityPosition& position){
                    return snap_placement_validator&&snap_placement_validator(actor,position);
                },[&,validated_stair_endpoint](std::int16_t actor,std::int16_t target,const odh::EntityPosition& position){
                    return validated_stair_endpoint&&snap_stair_placement_validator?
                        snap_stair_placement_validator(actor,target,position):odh::Snap::StairPlacementStatus::unsupported;
                },global_slot_scope);
            if(!entry->controller->bind(*dispatch.origin))
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"Snap controller could not bind to VM"};
            entry->primitive=entry->controller->primitive();
            runtime=snap_runtimes.emplace(cache_key,std::move(entry)).first;
        }
        return runtime->second->primitive(frame,instruction,tuning,dispatch);
    };
    std::string route_runtime_state=saved_world_main?"unavailable":"not_applicable";
    std::string route_runtime_reason=saved_world_main?"runtime_route_context_not_prepared":"requires_saved_world_main";
    std::string route_goal_validation_diagnostic;
    std::size_t route_runtime_movement_callbacks=0;
    std::optional<std::int16_t> route_ep5_unknown_caller,route_ep5_unknown_stack_object;
    std::optional<std::uint16_t> route_ep5_unknown_action;
    std::vector<odh::EntityPosition> route_runtime_movement_positions;
    std::size_t route_runtime_snapshot_refreshes=0;
    std::optional<std::uint64_t> route_runtime_spatial_revision;
    std::optional<odh::EntityPosition> route_runtime_start_position;
    std::optional<odh::WorldRoomRouteGrid> runtime_route_grid;
    std::optional<odh::AttemptWalkObstacleSnapshot> runtime_route_obstacle_snapshot;
    std::size_t runtime_route_static_obstacle_count=0;
    std::vector<odh::SavedCollisionCandidate> runtime_route_collision_candidates;
    std::size_t route_stack_slot_resolutions=0,route_stack_slot_loads=0;
    std::map<std::pair<std::uint32_t,std::uint16_t>,std::unique_ptr<odh::SlotTable>> route_stack_slot_cache;
    const auto route_stack_slot_table=[&](const odh::VmFrame& frame)->const odh::SlotTable* {
        ++route_stack_slot_resolutions;
        const auto object_id=frame.context.stack_object;
        if(object_id<=0||object_id>32767||!definition_index)return nullptr;
        const auto* entity=world.find(static_cast<std::int16_t>(object_id));
        if(!entity||entity->kind!=odh::EntityKind::object||!entity->definition)return nullptr;
        const auto guid=entity->definition->guid;
        const auto* candidate=definition_index->unique(guid);
        if(!candidate)return nullptr;
        const auto* master=candidate->definition.master_id!=0?
            definition_index->unique_master(*candidate):nullptr;
        std::optional<odh::ObjdBinding> binding;
        try {
            const Resource resource{candidate->source,candidate->asset,candidate->asset.empty()};
            odh::Limits limits;limits.iff_chunks=4096;
            with_resource(resource,[&](const auto& view){
                odh::iff_chunks(view,[&](const auto& chunk){
                    if(chunk.type=="OBJD"&&chunk.id==candidate->chunk_id) {
                        if(binding)throw odh::FormatError("ambiguous Route45 StackObject OBJD");
                        binding=odh::read_objd_binding(chunk.payload);
                    }
                },limits);
            });
            if(!binding||binding->guid!=guid||
               binding->master_id!=candidate->definition.master_id||
               binding->sub_index!=candidate->definition.sub_index||binding->slot_table==0) {
                route_runtime_reason="runtime_stackobject_OBJD_binding_mismatch";
                return nullptr;
            }
            if(binding->master_id!=0) {
                if(!master||!entity->definition->master_guid||
                   *entity->definition->master_guid!=master->definition.guid) {
                    route_runtime_reason="runtime_stackobject_multitile_master_missing_ambiguous_or_mismatched";
                    return nullptr;
                }
            } else if(entity->definition->master_guid) {
                route_runtime_reason="runtime_stackobject_unexpected_multitile_master";
                return nullptr;
            }
            const auto key=std::make_pair(guid,binding->slot_table);
            const auto cached=route_stack_slot_cache.find(key);
            if(cached!=route_stack_slot_cache.end())return cached->second.get();
            std::optional<odh::SlotTable> slots;
            with_resource(resource,[&](const auto& view){
                odh::iff_chunks(view,[&](const auto& chunk){
                    if(chunk.type=="SLOT"&&chunk.id==binding->slot_table) {
                        if(slots)throw odh::FormatError("ambiguous Route45 StackObject SLOT");
                        slots=odh::read_slot(chunk.payload);
                    }
                },limits);
            });
            if(!slots||route_stack_slot_cache.size()>=256)return nullptr;
            auto owned=std::make_unique<odh::SlotTable>(std::move(*slots));
            const auto* result=owned.get();
            route_stack_slot_cache.emplace(key,std::move(owned));
            ++route_stack_slot_loads;
            return result;
        } catch(const std::exception&) {
            route_runtime_reason="runtime_stackobject_SLOT_missing_ambiguous_or_invalid";
            return nullptr;
        }
    };
    std::unique_ptr<odh::Routing> runtime_routing;
    odh::VmPrimitive routing_handler=[&](const auto&,const auto&,const auto&,const auto&){
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
            "routing runtime unavailable: "+route_runtime_reason};
    };
    const auto motive_change_handler=odh::ts1_motive_change_primitive(world,memory);
    const auto ts1_budget_handler=odh::ts1_budget_primitive(memory,
        [&](const odh::VmFrame&)->std::optional<std::int32_t>{
            return active_family?std::optional<std::int32_t>{active_family->family.budget}:std::nullopt;
        },[&](const odh::VmFrame&,std::int32_t value){
            if(!active_family)return false;
            active_family->family.budget=value;return true;
        });
    std::map<odh::CodeOwner,odh::StringTable> saved_action_string304;
    std::set<odh::CodeOwner> saved_action_string304_attempted;
    const auto change_suit_handler=odh::ts1_change_suit_or_accessory(world,
        [&](odh::CodeOwner owner,std::uint16_t index)->std::optional<std::string>{
            auto table=saved_action_string304.find(owner);
            if(table==saved_action_string304.end()){
                if(!saved_action_string304_attempted.insert(owner).second)return std::nullopt;
                const auto resource=saved_action_resources.find(owner);
                if(resource==saved_action_resources.end())return std::nullopt;
                try{
                    auto strings=load_strings(resource->second,{304});
                    const auto found=strings.find(304);
                    if(found==strings.end()||found->second.state!=odh::StringTableState::decoded)return std::nullopt;
                    table=saved_action_string304.emplace(owner,std::move(found->second)).first;
                }catch(const std::exception&){return std::nullopt;}
            }
            const auto* entry=odh::string_in_language(table->second,game_language,index);
            return entry?std::optional<std::string>{entry->value}:std::nullopt;
        });
    const auto code_owner_resource=[&](odh::CodeOwner owner)->std::optional<Resource>{
        const auto action=saved_action_resources.find(owner);
        if(action!=saved_action_resources.end())return action->second;
        if(owner==static_cast<odh::CodeOwner>(runtime_avatar_id))
            return Resource{avatar->archive,avatar->entry,avatar->standalone};
        for(const auto& member:household_runtime)
            if(owner==static_cast<odh::CodeOwner>(member->id))return member->resource;
        return std::nullopt;
    };
    const odh::VmPrimitive tree_by_name_handler=[&](const odh::VmFrame& frame,const odh::Instruction& instruction,
        const std::array<std::int16_t,20>& temps,const odh::VmDispatch&)->odh::PrimitiveOutcome{
        if(instruction.opcode!=28)return {};
        const auto table_id=static_cast<std::uint16_t>(instruction.operands[0]|(instruction.operands[1]<<8));
        const auto string_scope=instruction.operands[2];
        const auto string_id=instruction.operands[4];
        const auto destination=instruction.operands[5];
        if(string_scope>1||destination>2)
            return {odh::PrimitiveDisposition::unsupported,"RunTreeByName scope/destination is invalid"};
        if(destination!=2)
            return {odh::PrimitiveDisposition::unsupported,"RunTreeByName currently supports destination2 subroutines only"};
        if(string_id==0)return {odh::PrimitiveDisposition::goto_false,{}};

        const odh::StringTable* table=nullptr;
        if(string_scope==1){
            auto found=tree_name_global_strings.find(table_id);
            if(found==tree_name_global_strings.end()&&globals){
                try{
                    auto loaded=load_strings(*globals,{table_id});
                    const auto item=loaded.find(table_id);
                    if(item!=loaded.end())found=tree_name_global_strings.emplace(table_id,std::move(item->second)).first;
                }catch(const std::exception& error){
                    return {odh::PrimitiveDisposition::unsupported,
                        std::string("RunTreeByName global string table could not be read: ")+error.what()};
                }
            }
            if(found!=tree_name_global_strings.end())table=&found->second;
        }else{
            const auto key=std::make_pair(frame.context.code_owner,table_id);
            auto found=tree_name_local_strings.find(key);
            if(found==tree_name_local_strings.end()&&tree_name_local_string_attempted.insert(key).second){
                const auto resource=code_owner_resource(frame.context.code_owner);
                if(resource){
                    try{
                        auto loaded=load_strings(*resource,{table_id});
                        const auto item=loaded.find(table_id);
                        if(item!=loaded.end())found=tree_name_local_strings.emplace(key,std::move(item->second)).first;
                    }catch(const std::exception& error){
                        return {odh::PrimitiveDisposition::unsupported,
                            std::string("RunTreeByName local string table could not be read: ")+error.what()};
                    }
                }
            }
            if(found!=tree_name_local_strings.end())table=&found->second;
        }
        if(!table)return {odh::PrimitiveDisposition::goto_false,{}};
        const auto* name=odh::string_in_language(*table,game_language,static_cast<std::uint32_t>(string_id-1));
        if(!name)return {odh::PrimitiveDisposition::goto_false,{}};

        const auto* target=world.find(frame.context.stack_object);
        if(!target)return {odh::PrimitiveDisposition::goto_false,{}};
        if(!target->definition)return {odh::PrimitiveDisposition::unsupported,"RunTreeByName target definition is unavailable"};
        const auto catalog=saved_tree_names.find(target->definition->guid);
        if(catalog==saved_tree_names.end())
            return {odh::PrimitiveDisposition::unsupported,"RunTreeByName target catalog is unavailable"};
        std::optional<odh::RoutineKey> routine;
        const auto private_tree=catalog->second.private_bhav_ids.find(name->value);
        if(private_tree!=catalog->second.private_bhav_ids.end())
            routine=odh::routine_key(private_tree->second);
        if(!routine&&catalog->second.semiglobal_name&&globals&&!globals->standalone){
            const auto entry=*catalog->second.semiglobal_name+".iff";
            const auto bank=load_saved_semiglobal_code_bank(globals->archive,entry);
            const auto named=bank.named_routines.find(name->value);
            if(named!=bank.named_routines.end())routine=odh::routine_key(named->second);
        }
        if(!routine)return {odh::PrimitiveDisposition::goto_false,{}};
        const auto target_thread=runtime_object_threads.find(frame.context.stack_object);
        if(target_thread==runtime_object_threads.end()||!target_thread->second||!target_thread->second->owner)
            return {odh::PrimitiveDisposition::unsupported,"RunTreeByName target has no loaded runtime code owner"};
        auto call_context=frame.context;
        call_context.code_owner=target_thread->second->owner;
        std::vector<std::int16_t> arguments(temps.begin(),temps.begin()+4);
        return {odh::PrimitiveDisposition::call,{}, {},
            odh::VmCall{*routine,call_context,std::move(arguments)}};
    };
    const odh::VmPrimitive push_interaction_handler=[&](const odh::VmFrame& frame,
        const odh::Instruction& instruction,const std::array<std::int16_t,20>&,
        const odh::VmDispatch&)->odh::PrimitiveOutcome{
        if(instruction.opcode!=13)return {};
        const auto interaction_index=instruction.operands[0];
        const auto object_location=instruction.operands[1];
        const auto priority=instruction.operands[2];
        const auto flags=instruction.operands[3];
        const auto icon_location=instruction.operands[4];
        constexpr std::uint8_t supported_flags=0x87; // custom icon, local source, head/tail continuation
        if(priority!=0||(flags&static_cast<std::uint8_t>(~supported_flags))!=0)
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "PushInteraction priority/flags outside the inherited Food continuation profile"};
        const bool source_is_local=(flags&2)!=0;
        const auto& source_bank=source_is_local?frame.locals:frame.arguments;
        if(object_location>=source_bank.size()||((flags&1)!=0&&icon_location>=frame.locals.size()))
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "PushInteraction object/icon local index outside its BHAV frame"};
        if(frame.context.stack_object!=runtime_avatar_id)
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "PushInteraction currently supports the primary avatar StackObject queue only"};
        const auto source_id=source_bank[object_location];
        const auto* source_object=world.find(source_id);
        if(source_id<=0||!source_object||source_object->kind!=odh::EntityKind::object||
           !source_object->definition||!definition_index)
            return {odh::PrimitiveDisposition::goto_false,{}};
        const auto* candidate=definition_index->unique(source_object->definition->guid);
        if(!candidate||candidate->definition.guid!=source_object->definition->guid)
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "PushInteraction source definition is missing or ambiguous"};
        const auto source_thread=runtime_object_threads.find(source_id);
        if(source_thread==runtime_object_threads.end()||!source_thread->second)
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "PushInteraction source object has no installed runtime code owner"};
        const auto owner_resource=saved_action_resources.find(source_thread->second->owner);
        if(owner_resource==saved_action_resources.end()||owner_resource->second.archive!=candidate->source||
           owner_resource->second.entry!=candidate->asset)
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "PushInteraction exact source resource does not match its installed code owner"};
        try{
            auto interactions=saved_object_interactions(*candidate,game_language);
            if(!interactions.table||interactions.table->status!=odh::TtabStatus::decoded||
               interaction_index>=interactions.table->interactions.size())
                return {odh::PrimitiveDisposition::goto_false,{}};
            const auto& row=interactions.table->interactions[interaction_index];
            const auto action_key=odh::routine_key(row.action);
            const auto test_key=odh::routine_key(row.test);
            if(action_key.scope!=odh::RoutineScope::private_object||
               (row.test!=0&&test_key.scope!=odh::RoutineScope::private_object))
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                    "PushInteraction action/test is outside the exact private Food owner profile"};
            const auto action_routines=load_saved_action_routines(*candidate,row.action);
            if(!action_routines.count(action_key))return {odh::PrimitiveDisposition::goto_false,{}};
            if(row.test!=0){
                const auto test_routines=load_saved_action_routines(*candidate,row.test);
                if(!test_routines.count(test_key))return {odh::PrimitiveDisposition::goto_false,{}};
            }
            const odh::VmContext queued_context{runtime_avatar_id,source_id,source_id,
                source_thread->second->owner};
            const bool push_head=(flags&4)!=0;
            if(!thread_controllers.interactions().enqueue(action_key,queued_context,{},push_head))
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                    "PushInteraction could not enqueue the exact Food action: "+
                    thread_controllers.interactions().diagnostic()};
            return {odh::PrimitiveDisposition::goto_true,{}};
        }catch(const std::exception& error){
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                std::string("PushInteraction exact Food table/action could not be read: ")+error.what()};
        }
    };
    const odh::VmPrimitive person_primitive=[&](const auto& f,const auto& i,const auto& t,const auto& d){
        if(i.opcode==42){
            if(!d.origin||!d.ticket||pending_create_objects.size()>=64)
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                    "CreateObjectInstance queue is missing a VM ticket or is full"};
            try{
                pending_create_objects.push_back({const_cast<odh::Vm*>(d.origin),d.ticket,
                    odh::decode_create_object_instance(i),f.context.stack_object});
            }catch(const std::exception& error){
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                    std::string("CreateObjectInstance request could not be queued: ")+error.what()};
            }
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
        }
        if(i.opcode==6)return change_suit_handler(f,i,t,d);
        if(i.opcode==0)return sleep_handler(f,i,t,d);
        if(i.opcode==29)return motive_change_handler(f,i,t,d);
        if(i.opcode==25)return ts1_budget_handler(f,i,t,d);
        if(i.opcode==16){
            // The current House07 Food auxiliary object's EP0 uses the
            // ordinary signed GreaterThan comparison over its own attributes.
            // Keep other Expression operators/scopes closed until their
            // 32-bit reads and writes have dedicated coverage.
            const auto op=i.operands[5];
            const auto lhs_scope=i.operands[6],rhs_scope=i.operands[7];
            if(op!=0||(lhs_scope!=0&&lhs_scope!=1)||(rhs_scope!=0&&rhs_scope!=1))
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                    "Expression supports GreaterThan over explicit object attributes only"};
            const auto lhs=memory.read?memory.read(f,lhs_scope,
                static_cast<std::int16_t>(static_cast<std::uint16_t>(i.operands[0])|
                    (static_cast<std::uint16_t>(i.operands[1])<<8))):std::nullopt;
            const auto rhs=memory.read?memory.read(f,rhs_scope,
                static_cast<std::int16_t>(static_cast<std::uint16_t>(i.operands[2])|
                    (static_cast<std::uint16_t>(i.operands[3])<<8))):std::nullopt;
            if(!lhs||!rhs)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "Expression object attribute is unavailable"};
            return odh::PrimitiveOutcome{*lhs>*rhs?odh::PrimitiveDisposition::goto_true:
                odh::PrimitiveDisposition::goto_false,{}};
        }
        if(i.opcode==7){
            const auto refresh_type=static_cast<std::uint16_t>(i.operands[2])|
                (static_cast<std::uint16_t>(i.operands[3])<<8);
            if(refresh_type==2){
                const auto target_id=i.operands[0]==0?f.context.caller:
                    i.operands[0]==1?f.context.stack_object:std::int16_t{0};
                const auto* target=world.find(target_id);
                if(target&&!target->position)
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            }
            if(refresh_type!=0)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "Refresh type needs a positioned target and resolved room resources"};
            // SavedLotRenderer reads the live Graphic ObjectData word on each
            // capture and resolves its DGRP then; it has no cached per-instance
            // graphic binding to invalidate. FreeSO's Refresh primitive returns
            // GOTO_TRUE even when the selected target is not a game object.
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        }
        if(i.opcode==17)return queue_handler(f,i,t,d);
        if(i.opcode==28)return tree_by_name_handler(f,i,t,d);
        if(i.opcode==13)return push_interaction_handler(f,i,t,d);
        if(i.opcode==18){
            const auto target=i.operands[0]==0&&i.operands[1]==0?f.context.caller:f.context.stack_object;
            const auto flags=i.operands[2];
            if(flags!=0)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                "RemoveObjectInstance ReturnImmediately/CleanupAll flags are outside the deferred removal profile"};
            if(!d.origin||!d.ticket||pending_remove_objects.size()>=64)
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                    "RemoveObjectInstance queue is missing a VM ticket or is full"};
            const auto* object=world.find(target);
            if(!object)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            if(object->kind!=odh::EntityKind::object)
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                    "RemoveObjectInstance avatar deletion is outside the object cleanup profile"};
            // A VM deleting its own object must not leave a completion ticket
            // pointing into the RuntimeObjectThread that the safe-point service
            // is about to destroy. Defer that case without a VM ticket.
            const bool defer=target==f.context.caller||any_interaction_references_target(target);
            try{pending_remove_objects.push_back({defer?nullptr:const_cast<odh::Vm*>(d.origin),
                defer?0:d.ticket,target,object->generation,defer});}
            catch(const std::exception& error){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                std::string("RemoveObjectInstance request could not be queued: ")+error.what()};}
            if(defer)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
        }
        if(i.opcode==4)return grab_handler(f,i,t,d);
        if(i.opcode==50){
            // AddChangeActionString only renames the active queue label. This
            // headless runner has no action-list UI to update; Simitone returns
            // true even when the requested string table is unavailable.
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        }
        if(i.opcode==48){
            // This Linux host does not start audio playback, so there are no
            // live sound threads to cancel. Keep sound diagnostics as history.
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        }
        if(i.opcode==8)return random_handler(f,i,t,d);
        if(i.opcode==1&&i.operands[0]==15){
            // Simitone's TS1 GenericCall treats MyRoutingFootprintEqualsTemp0
            // as a successful no-op; its source still marks the state change
            // as unresolved. Preserve that reference behavior without
            // pretending to update the route footprint.
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        }
        if(i.opcode==1&&i.operands[0]==28&&lot_handler)return lot_handler(f,i,t,d);
        if(i.opcode==31&&((i.operands[4]&127)<=4||(i.operands[4]&127)==8||
                           (i.operands[4]&127)==9||(i.operands[4]&127)==12))return entity_handler(f,i,t,d);
        if(i.opcode==31&&neighbor_handler)return neighbor_handler(f,i,t,d);
        if(i.opcode==32)return object_type_handler(f,i,t,d);
        if(i.opcode==49)return notify_handler(f,i,t,d);
        if((i.opcode==26||i.opcode==24)&&relationship_handler)return relationship_handler(f,i,t,d);
        if(i.opcode==51&&inventory_handler)return inventory_handler(f,i,t,d);
        if(i.opcode==44&&animation_handler)return animation_handler(f,i,t,d);
        if(i.opcode==23)return sound_handler(f,i,t,d);
        if(i.opcode==22)return look_towards_handler(f,i,t,d);
        if(i.opcode==11)return distance_to_handler(f,i,t,d);
        if(i.opcode==12)return direction_to_handler(f,i,t,d);
        if(i.opcode==27||i.opcode==45)return routing_handler(f,i,t,d);
        if(i.opcode==46)return snap_handler(f,i,t,d);
        return odh::PrimitiveOutcome{};
    };
    auto shared_program=std::make_shared<const odh::VmProgram>(std::move(shared.routines));
    odh::Vm vm(shared_program,odh::ShareVmProgramTag{},memory,{},person_primitive,std::move(owners));
    std::unique_ptr<odh::Vm> second_main_vm;
    if(headless_second_main){
        auto second_owners=odh::RoutineOwners{{headless_second_main->avatar.id,std::move(*second_main_routines)}};
        const auto second_sleep=second_main_thread->sleep_primitive();
        const auto second_queue=second_main_thread->interaction_primitive();
        const auto second_animation=second_main_playback?second_main_playback->primitive():odh::VmPrimitive{};
        const auto second_primitive=[&,second_sleep,second_queue,second_animation](
            const auto& f,const auto& i,const auto& t,const auto& d)->odh::PrimitiveOutcome{
            if(i.opcode==0)return second_sleep(f,i,t,d);
            if(i.opcode==17)return second_queue(f,i,t,d);
            if(i.opcode==44){
                if(second_animation)return second_animation(f,i,t,d);
                return {odh::PrimitiveDisposition::unsupported,"second avatar animation playback is unavailable in this host profile"};
            }
            if(i.opcode==27||i.opcode==45||i.opcode==46)
                return {odh::PrimitiveDisposition::unsupported,"second avatar routing and Snap controllers are outside this host profile"};
            return person_primitive(f,i,t,d);
        };
        second_main_vm=std::make_unique<odh::Vm>(shared_program,odh::ShareVmProgramTag{},memory,odh::VmLimits{},
            second_primitive,std::move(second_owners));
    }
    for(auto& member:household_runtime){
        auto* runtime=member.get();
        const auto member_sleep=runtime->controllers->sleep_primitive();
        const auto member_queue=runtime->controllers->interaction_primitive();
        const auto member_animation=runtime->playback?runtime->playback->primitive():odh::VmPrimitive{};
        const auto primitive=[&,runtime,member_sleep,member_queue,member_animation](
            const auto& f,const auto& i,const auto& t,const auto& d)->odh::PrimitiveOutcome{
            if(i.opcode==0)return member_sleep(f,i,t,d);
            if(i.opcode==17)return member_queue(f,i,t,d);
            if(i.opcode==44){
                if(member_animation)return member_animation(f,i,t,d);
                return {odh::PrimitiveDisposition::unsupported,"household resident animation playback is unavailable"};
            }
            if(i.opcode==27||i.opcode==45){
                if(runtime->routing){
                    const auto route=runtime->routing->primitive();
                    return route(f,i,t,d);
                }
                return {odh::PrimitiveDisposition::unsupported,
                    "household resident routing unavailable: "+runtime->route_reason};
            }
            return person_primitive(f,i,t,d);
        };
        runtime->vm=std::make_unique<odh::Vm>(shared_program,odh::ShareVmProgramTag{},memory,odh::VmLimits{},primitive,
            odh::RoutineOwners{{runtime->id,std::move(runtime->data.routines)}});
        if(!restoring_session&&materialized_main_states.count(runtime->id)&&
           !runtime->vm->restore_snapshot(materialized_main_states.at(runtime->id)))
            throw odh::FormatError("materialized household Main state could not bind to runtime");
        if(!runtime->controllers->bind(*runtime->vm)||(runtime->playback&&!runtime->playback->bind(*runtime->vm)))
            throw odh::FormatError("household resident controllers could not bind");
        if(!entity_scheduler.add(runtime->id,*runtime->vm,
            [runtime](std::uint64_t){return runtime->controllers->observe();},
            [runtime]{return runtime->controllers->observe();},[runtime]{return runtime->controllers->notify();}))
            throw odh::FormatError("household resident could not join the entity scheduler");
    }
    if(!thread_controllers.bind(vm))throw odh::FormatError("person controllers could not bind");
    if(playback&&!playback->bind(vm))throw odh::FormatError("person animation controller could not bind");
    if(second_main_vm&&(!second_main_thread->bind(*second_main_vm)||
       (second_main_playback&&!second_main_playback->bind(*second_main_vm))))
        throw odh::FormatError("second person controllers could not bind");
    if(!entity_scheduler.add(runtime_avatar_id,vm,
        [&](std::uint64_t){return thread_controllers.observe();},[&]{return thread_controllers.observe();},
        [&]{return thread_controllers.notify();}))
        throw odh::FormatError("person VM could not join the entity scheduler");
    if(second_main_vm&&!entity_scheduler.add(static_cast<std::int16_t>(headless_second_main->avatar.id),*second_main_vm,
        [&](std::uint64_t){return second_main_thread->observe();},[&]{return second_main_thread->observe();},
        [&]{return second_main_thread->notify();}))
        throw odh::FormatError("second person VM could not join the entity scheduler");
    std::vector<odh::VmTrace> trace;std::optional<odh::VmTrace> latest_trace;
    bool host_failed=false;std::string host_diagnostic;
    std::vector<std::string> trace_phases;
    std::vector<odh::AvatarStartupStage> stages;
    std::uint64_t startup_steps=0;
    std::optional<odh::NeighborInheritance> inherited;
    std::optional<std::uint64_t> body_seed_before,body_seed_after;
    const auto prepare_household_bodies=[&]{
        for(auto& member:household_runtime){
            if(!member->data.startup)
                throw odh::FormatError("household avatar body derivation requires startup OBJD fields");
            auto strings=load_strings(member->resource,{member->data.startup->body_strings,150});
            const auto table=strings.find(member->data.startup->body_strings);
            if(table==strings.end())throw odh::FormatError("household startup BodySTR absent");
            auto member_body=odh::parse_avatar_body_strings(table->second);
            if(member_body.avatar_type!="adult"&&member_body.avatar_type!="child")
                throw odh::FormatError("household descriptor profile requires adult or child BodySTR type");
            const bool child=*member_body.age<18;
            const std::uint16_t walk_id=child?151:150,swim_id=child?160:158;
            const auto tables=load_strings(*globals,{walk_id,swim_id});
            if(!tables.count(walk_id)||!tables.count(swim_id))
                throw odh::FormatError("household global walk/swim STR# absent");
            const auto special=strings.find(150);
            const auto overrides=special==strings.end()?std::optional<odh::StringTable>{}:
                std::optional<odh::StringTable>{std::move(special->second)};
            member->body_state=odh::derive_avatar_body_descriptor(world,member->id,random,member_body,
                tables.at(walk_id),tables.at(swim_id),overrides);
        }
    };
    const auto trace_in=[&](const std::string& phase){return [&,phase](const odh::VmTrace& record){
        latest_trace=record;
        if(diagnostics.retain(Diagnostic::trace,trace.size(),10000)){
            trace.push_back(record);trace_phases.push_back(phase);
        }
    };};
    if(initialize){
        const auto startup=odh::run_avatar_startup(vm,runtime_avatar_id,
            person.entry_points->entries[0].action,person.entry_points->entries[8].action,10000,
            [&](const std::string& phase,const odh::VmTrace& record){trace_in(phase)(record);});
        stages=startup.stages;
        for(const auto& stage:stages)
            if(stage.status==odh::VmStatus::returned_true||stage.status==odh::VmStatus::returned_false)
                startup_steps+=stage.steps;
        if(!startup.completed){host_failed=true;host_diagnostic=startup.diagnostic;}
    }
    if(!host_failed&&!restoring_session){
        // Stack Main before post-body initialization, then dispatch its first
        // instruction only after the body data stage has completed.
        std::vector<std::int16_t> root_arguments(4);
        if(headless_fixture_main_arg0)root_arguments[0]=static_cast<std::int16_t>(*headless_fixture_main_arg0);
        vm.start(odh::routine_key(root),{runtime_avatar_id,runtime_avatar_id,
            headless_stack_object.value_or(static_cast<std::int16_t>(initialize||saved_world_main?0:1)),
            static_cast<odh::CodeOwner>(runtime_avatar_id)},root_arguments);
        if(second_main_vm){
            std::vector<std::int16_t> arguments(4);
            if(headless_second_main_arg0)arguments[0]=static_cast<std::int16_t>(*headless_second_main_arg0);
            const auto id=static_cast<std::int16_t>(headless_second_main->avatar.id);
            second_main_vm->start(odh::routine_key(headless_second_main->routine),
                {id,id,0,static_cast<odh::CodeOwner>(id)},arguments);
        }
        for(auto& member:household_runtime)
            if(!materialized_main_states.count(member->id))
                member->vm->start(odh::routine_key(member->main),
                    {member->id,member->id,0,static_cast<odh::CodeOwner>(member->id)},std::vector<std::int16_t>(4));
        if(initialize&&vm.status()!=odh::VmStatus::fault){
            try{body_seed_before=random.seed();body_state=odh::apply_avatar_body(world,runtime_avatar_id,random,body,*body_walk,*body_swim,private_walk);body_seed_after=random.seed();}
            catch(const std::exception& e){host_failed=true;host_diagnostic=e.what();}
        }
        else if(saved_world_main){
            try{body_state=odh::derive_avatar_body_descriptor(world,runtime_avatar_id,random,body,*body_walk,*body_swim,private_walk);}
            catch(const std::exception& e){host_failed=true;host_diagnostic=e.what();}
        }
        if(initialize&&!saved_world_main&&neighbors&&!host_failed&&vm.status()!=odh::VmStatus::fault){
            if(const auto id=neighbors->id_for_guid(person.binding->guid)){
                try{inherited=odh::inherit_neighbor(world,runtime_avatar_id,*neighbors->by_id(*id),active_family?std::optional<std::uint16_t>{active_family->chunk_id}:std::nullopt);}
                catch(const std::exception& e){host_failed=true;host_diagnostic=e.what();}
            }
        }
        if(action&&!host_failed&&vm.status()!=odh::VmStatus::fault&&!queue.enqueue(odh::routine_key(*action_routine),{runtime_avatar_id,2,2,2}))throw odh::FormatError(queue.diagnostic());
    }
    if(!restoring_session)prepare_household_bodies();
    std::uint64_t ticks=0;
    if(session_restore){
        if(!playback)throw odh::FormatError("session restore needs the matching animation catalog");
        random.reset(session_restore->host().random_seed);
        body_state=odh::derive_avatar_body_descriptor(world,runtime_avatar_id,random,body,*body_walk,*body_swim,private_walk);
        if(const auto& seated=session_restore->seated_continuation();seated){
            const auto* target=world.find(seated->target_id);
            if(!target||!target->definition||target->definition->guid!=seated->target_guid||
               !target->entry_points||!definition_index)
                throw odh::FormatError("seated continuation target or definition index is unavailable");
            const auto* candidates=definition_index->candidates(seated->target_guid);
            if(!candidates)throw odh::FormatError("seated continuation object definition is absent");
            const odh::DefinitionCandidate* exact=nullptr;
            for(const auto& candidate:*candidates){
                if(candidate.source==seated->source&&candidate.asset==seated->asset&&
                   candidate.chunk_id==seated->definition_chunk_id&&
                   candidate.definition.tree_table_id==seated->tree_table_id){
                    if(exact)throw odh::FormatError("seated continuation object definition is ambiguous");
                    exact=&candidate;
                }
            }
            if(!exact)throw odh::FormatError("seated continuation object provenance no longer matches the selected resources");
            session_restore->install_seated_continuation_owners(vm,*exact,*target->entry_points,
                [&](const odh::DefinitionCandidate& definition,std::uint16_t root){
                    return load_session_action_source(definition,root,seated->linked_semiglobal,
                        session_context->game_root,globals,definition_index?&*definition_index:nullptr);
                });
        }
        const odh::PlaybackRestoreResolver resolver=[&](const odh::PlaybackResourceKey& key){
            odh::VmFrame frame;frame.context={key.caller,key.callee,key.stack_object,key.code_owner};
            return resolve_animation(frame,key.source,key.id,key.kind==odh::PlaybackResourceKey::Kind::posture_reset
                ?std::optional<std::int16_t>{key.posture}:std::nullopt);
        };
        session_restore->restore_execution(vm,thread_controllers,*playback,entity_scheduler,resolver,random,ticks);
        profile_mark("session-restored-before-main");
#ifdef ODH_HAVE_LINUX_SESSION_LOG
        session_log.event("session resumed saved_tick="+std::to_string(ticks));
#endif
    }
    if(household_restore){
        if(!playback)throw odh::FormatError("household restore needs the matching animation catalog");
        random.reset(household_restore->host().random_seed);
        body_state=odh::derive_avatar_body_descriptor(world,runtime_avatar_id,random,body,*body_walk,*body_swim,private_walk);
        prepare_household_bodies();
        const odh::PlaybackRestoreResolver resolver=[&](const odh::PlaybackResourceKey& key){
            odh::VmFrame frame;frame.context={key.caller,key.callee,key.stack_object,key.code_owner};
            return resolve_animation(frame,key.source,key.id,key.kind==odh::PlaybackResourceKey::Kind::posture_reset
                ?std::optional<std::int16_t>{key.posture}:std::nullopt);
        };
        std::vector<odh::SessionExecutionBinding> bindings{{runtime_avatar_id,&vm,&thread_controllers,&*playback,resolver}};
        for(auto& member:household_runtime){
            if(!member->playback)throw odh::FormatError("household resident animation catalog is unavailable");
            bindings.push_back({member->id,member->vm.get(),member->controllers.get(),&*member->playback,resolver});
        }
        std::sort(bindings.begin(),bindings.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        household_restore->restore_execution(bindings,entity_scheduler,random,ticks);
        profile_mark("household-session-restored-before-main");
    }
    if(simulation_clock && restoring_session && simulation_clock->snapshot().ticks!=ticks)
        throw odh::FormatError("simulation clock differs from restored host tick");
    if(simulation_clock)for(const auto& member:household_runtime)simulation_motives.try_emplace(member->id);
    const auto resumed_tick=ticks;
    std::optional<std::uint64_t> scheduled_interaction_tick;
    if(headless_interaction){
        if(restoring_session&&headless_interaction->tick>std::numeric_limits<std::uint64_t>::max()-resumed_tick)
            throw odh::FormatError("resumed interaction tick overflows the session clock");
        scheduled_interaction_tick=headless_interaction->tick+(restoring_session?resumed_tick:0);
    }
    std::optional<std::uint64_t> scheduled_stand_tick;
    if(headless_stand){
        if(session_restore&&headless_stand->tick>std::numeric_limits<std::uint64_t>::max()-resumed_tick)
            throw odh::FormatError("resumed Stand tick overflows the session clock");
        scheduled_stand_tick=headless_stand->tick+(session_restore?resumed_tick:0);
    }
    const auto resumed_steps=restoring_session?vm.steps():0;
    std::uint64_t resumed_household_steps=0;
    if(household_restore)for(const auto& member:household_runtime)resumed_household_steps+=member->vm->steps();
    struct RenderEvidence {
        std::string clip;float frame=0;std::int16_t actor=0;std::uint64_t tick=0;
        std::uint64_t actor_generation=0,vm_generation=0,cfp_samples=0;
        std::uint64_t vertices=0,triangles=0,texture_bytes=0,tested_samples=0,shaded_samples=0;
        std::uint64_t actor_raster_pixels=0,actor_composited_pixels=0;
        bool lot_composited=true,lot_objects_included=true;
    };
    struct RoomInfoProbe {
        std::uint16_t room;
        odh::CollisionBox candidate;
        odh::RoomInfoObstacleQuery query;
        odh::RoomInfoObstacleSnapshot inventory;
        std::optional<RuntimeAttemptWalkProbe> attempt_walk;
        std::optional<odh::RectRoutePoint> attempt_walk_destination;
        std::optional<odh::RoomPortalRoute> attempt_walk_room_route;
        std::optional<odh::RectRouteResult> attempt_walk_route;
        std::optional<odh::RectRouteLineResult> attempt_walk_lines;
        std::string attempt_walk_route_state="not_requested";
        std::string attempt_walk_lines_state="not_run";
    };
struct PlacementCandidateProbe {
    odh::EntityPosition position;
    bool inside_lot=false;
    std::optional<std::uint16_t> room,floor,placement_flags,level_offset;
    std::optional<odh::CollisionBox> box;
    std::optional<odh::FloorPlacementResult> floor_result;
    std::string floor_reason;
    std::optional<odh::RoomInfoObstacleQuery> query;
    std::optional<odh::WorldAvatarPlaceAuditResult> overlap_audit;
    std::optional<RuntimeAttemptWalkProbe> architecture_audit;
    std::optional<bool> architecture_walkable;
    std::string architecture_walkability_state="not_evaluated";
    std::string architecture_walkability_reason;
    std::string route_audit_state="not_evaluated";
    std::string route_audit_reason;
    std::optional<odh::RoutePath> route_audit_path;
    std::optional<odh::RoutePoint> route_audit_start,route_audit_goal;
    std::string route_audit_scope;
    std::string room_info_reason;
};
    struct EntryPoint5BhvEvidence {
        std::int16_t caller=0,stack_object=0;
        std::uint16_t action=0;
        std::string source,asset,status="unsupported",reason;
        std::optional<bool> result;
        std::uint64_t steps=0;
    };
    std::vector<EntryPoint5BhvEvidence> entry_point5_bhav_evidence;
    using EntryPoint5CacheKey=std::pair<std::uint32_t,std::uint16_t>;
    struct EntryPoint5Program {odh::RoutineSet owner;};
    std::map<EntryPoint5CacheKey,EntryPoint5Program> entry_point5_bhav_cache;
    std::map<EntryPoint5CacheKey,std::string> entry_point5_bhav_errors;
    std::map<std::string,SavedSemiglobalCodeBank> entry_point5_semiglobal_cache;
    std::optional<SavedGlobalCodeBank> entry_point5_global_cache;
    std::string entry_point5_global_error;
    const odh::RoutineSet entry_point5_empty_globals;
    bool entry_point5_global_loaded=false;
    std::size_t entry_point5_evaluations=0;
    const odh::AttemptWalkEntryPoint5Resolver resolve_read_only_ep5=
        [&](std::int16_t caller_id,std::int16_t stack_object_id)->std::optional<bool>{
        EntryPoint5BhvEvidence evidence;
        evidence.caller=caller_id;evidence.stack_object=stack_object_id;
        const auto* caller=world.find(caller_id);
        const auto* stack_object=world.find(stack_object_id);
        if(!caller||!caller->definition||!caller->entry_points||caller->entry_points->size()<=5){
            evidence.reason="caller_entry_point5_or_definition_unavailable";
            if(diagnostics.retain(Diagnostic::entry_point5,entry_point5_bhav_evidence.size(),4096))entry_point5_bhav_evidence.push_back(evidence);
            return std::nullopt;
        }
        evidence.action=(*caller->entry_points)[5].action;
        const auto* definition=definition_index?definition_index->unique(caller->definition->guid):nullptr;
        if(!definition){
            evidence.reason="caller_definition_missing_or_ambiguous";
            if(diagnostics.retain(Diagnostic::entry_point5,entry_point5_bhav_evidence.size(),4096))entry_point5_bhav_evidence.push_back(evidence);
            return std::nullopt;
        }
        evidence.source=definition->source;evidence.asset=definition->asset;
        const EntryPoint5CacheKey key{caller->definition->guid,evidence.action};
        auto routine=entry_point5_bhav_cache.find(key);
        if(routine==entry_point5_bhav_cache.end()&&entry_point5_bhav_errors.find(key)==entry_point5_bhav_errors.end()){
            try{
                if(evidence.action<4096||evidence.action>=8192)
                    throw odh::FormatError("EntryPoint 5 root is not a private BHAV");
                if(entry_point5_bhav_cache.size()>=256)
                    throw odh::FormatError("EntryPoint 5 program cache budget exceeded");

                if(!entry_point5_global_loaded){
                    entry_point5_global_loaded=true;
                    if(globals&&!globals->standalone){
                        try{entry_point5_global_cache=load_saved_global_code_bank(globals->archive,globals->entry);}
                        catch(const std::exception& error){entry_point5_global_error=error.what();}
                    }
                }
                auto owner_routines=load_saved_action_routines(*definition,evidence.action);
                const auto root=odh::routine_key(evidence.action);
                if(!owner_routines.count(root))
                    throw odh::FormatError("EntryPoint 5 root missing from exact private action closure");

                auto catalog=load_saved_tree_name_catalog(*definition);
                auto semiglobal_name=unique_saved_semiglobal_name(catalog);
                if(!semiglobal_name&&catalog.semiglobal_bindings!=0)
                    throw odh::FormatError("EntryPoint 5 object has an empty GLOB link");
                if(!semiglobal_name&&catalog.semiglobal_bindings==0&&
                   definition->definition.tree_table_id==0xffff&&
                   definition->definition.master_id!=0){
                    if(const auto* master=definition_index->unique_master(*definition)){
                        catalog=load_saved_tree_name_catalog(*master);
                        semiglobal_name=unique_saved_semiglobal_name(catalog);
                        if(!semiglobal_name&&catalog.semiglobal_bindings!=0)
                            throw odh::FormatError("EntryPoint 5 master object has an empty GLOB link");
                    }
                }
                if(semiglobal_name){
                    if(semiglobal_name->empty()||semiglobal_name->find_first_of("/\\:.")!=std::string::npos||
                       semiglobal_name->find('\0')!=std::string::npos)
                        throw odh::FormatError("EntryPoint 5 GLOB link is not a flat resource name");
                    if(!globals||globals->standalone)
                        throw odh::FormatError("EntryPoint 5 linked semiglobal requires the selected Global FAR");
                    const auto entry=*semiglobal_name+".iff";
                    const auto cache_key=globals->archive+"\n"+entry;
                    auto linked=entry_point5_semiglobal_cache.find(cache_key);
                    if(linked==entry_point5_semiglobal_cache.end()){
                        if(entry_point5_semiglobal_cache.size()>=64)
                            throw odh::FormatError("EntryPoint 5 semiglobal cache budget exceeded");
                        linked=entry_point5_semiglobal_cache.emplace(cache_key,
                            load_saved_semiglobal_code_bank(globals->archive,entry)).first;
                    }
                    if(linked->second.name!=*semiglobal_name||linked->second.entry!=entry)
                        throw odh::FormatError("EntryPoint 5 semiglobal does not match exact GLOB link");
                    for(const auto& [code_key,code]:linked->second.routines)
                        if(!owner_routines.emplace(code_key,code).second)
                            throw odh::FormatError("duplicate routine across private and linked semiglobal banks");
                }
                routine=entry_point5_bhav_cache.emplace(key,
                    EntryPoint5Program{std::move(owner_routines)}).first;
            }catch(const std::exception& error){entry_point5_bhav_errors.emplace(key,error.what());}
        }
        if(entry_point5_bhav_errors.find(key)!=entry_point5_bhav_errors.end()){
            evidence.reason="selected_asset_bhav_unavailable: "+entry_point5_bhav_errors.at(key);
            if(diagnostics.retain(Diagnostic::entry_point5,entry_point5_bhav_evidence.size(),4096))entry_point5_bhav_evidence.push_back(evidence);
            return std::nullopt;
        }
        if(routine==entry_point5_bhav_cache.end()||!stack_object){
            evidence.reason="stack_object_identity_unavailable";
            if(diagnostics.retain(Diagnostic::entry_point5,entry_point5_bhav_evidence.size(),4096))entry_point5_bhav_evidence.push_back(evidence);
            return std::nullopt;
        }
        if(entry_point5_evaluations>=1024){
            evidence.reason="entry_point5_evaluation_budget_exhausted";
            if(diagnostics.retain(Diagnostic::entry_point5,entry_point5_bhav_evidence.size(),4096))entry_point5_bhav_evidence.push_back(evidence);
            return std::nullopt;
        }
        ++entry_point5_evaluations;
        odh::ReadOnlyBhavLimits limits;
        // Saved lots already use this bounded ceiling when restoring their World.
        limits.world.words=131072;
        const auto& default_routines=entry_point5_global_cache
            ? entry_point5_global_cache->routines : entry_point5_empty_globals;
        const auto execution=odh::evaluate_read_only_entry_point5_bhav(world.snapshot(),
            default_routines,routine->second.owner,evidence.action,caller_id,
            stack_object_id,{},256,limits);
        switch(execution.state){
        case odh::ReadOnlyBhavState::evaluated_true:
            evidence.status="evaluated_true";evidence.result=true;break;
        case odh::ReadOnlyBhavState::evaluated_false:
            evidence.status="evaluated_false";evidence.result=false;break;
        case odh::ReadOnlyBhavState::unsupported:
            evidence.status="unsupported";break;
        }
        evidence.reason=execution.diagnostic.empty()?"read_only_bhav_completed":execution.diagnostic;
        if(execution.state==odh::ReadOnlyBhavState::unsupported&&!entry_point5_global_error.empty())
            evidence.reason+="; global bank unavailable: "+entry_point5_global_error;
        evidence.steps=execution.steps;
        if(diagnostics.retain(Diagnostic::entry_point5,entry_point5_bhav_evidence.size(),4096))entry_point5_bhav_evidence.push_back(evidence);
        return evidence.result;
    };
    std::optional<RenderEvidence> render_evidence;
    std::optional<RoomInfoProbe> room_info_probe;
    std::string room_info_probe_unavailable;
    std::optional<PlacementCandidateProbe> placement_candidate_probe;
    std::string placement_candidate_probe_unavailable;
    std::unique_ptr<SavedAppearanceResources> render_appearances;
    std::unique_ptr<SceneRenderer> render_renderer;
    std::unique_ptr<SavedLotRenderer> render_lot;
    std::unique_ptr<SavedGraphicsResources> render_graphics;
    std::optional<SavedLotFrame> render_lot_evidence;
    std::optional<SavedObjectSelection> render_selection;
    std::optional<SavedGroundSelection> render_ground_selection;
    std::optional<std::array<std::int32_t,2>> render_menu_anchor;
    std::int16_t active_household_actor=runtime_avatar_id;
    bool household_selector_open=false;
    const auto selected_household_member=[&]() -> HouseholdRuntime* {
        const auto found=std::find_if(household_runtime.begin(),household_runtime.end(),
            [&](const auto& member){return member->id==active_household_actor;});
        return found==household_runtime.end()?nullptr:found->get();
    };
    const auto actor_vm_for=[&](std::int16_t actor_id)->odh::Vm* {
        if(actor_id==runtime_avatar_id)return &vm;
        const auto found=std::find_if(household_runtime.begin(),household_runtime.end(),
            [&](const auto& member){return member->id==actor_id;});
        return found==household_runtime.end()?nullptr:(*found)->vm.get();
    };
    const auto actor_queue_for=[&](std::int16_t actor_id)->odh::InteractionQueue* {
        if(actor_id==runtime_avatar_id)return &queue;
        const auto found=std::find_if(household_runtime.begin(),household_runtime.end(),
            [&](const auto& member){return member->id==actor_id;});
        return found==household_runtime.end()||!(*found)->controllers?nullptr:&(*found)->controllers->interactions();
    };
    std::optional<SavedObjectInteractions> render_interactions;
    std::optional<SavedSemiglobalCodeBank> render_interaction_semiglobal;
    std::optional<std::size_t> render_selected_interaction_row;
    std::vector<std::size_t> render_pie_menu_path;
    std::size_t render_pie_menu_selected=0;
    std::vector<SavedTestFunctionResult> render_test_results;
    std::uint64_t render_selection_clicks=0,render_selection_tick=0;
    bool render_interaction_requested=false,render_interaction_enqueued=false,saved_owner_ids_exhausted=false;
    std::string render_interaction_status="not_requested",render_interaction_diagnostic;
    std::optional<std::uint16_t> render_interaction_action;
    std::optional<odh::CodeOwner> render_interaction_owner;
    std::optional<SavedObjectSelection> render_interaction_target;
    std::optional<std::uint64_t> render_interaction_actor_generation;
    std::optional<std::int16_t> render_interaction_actor_id;
    std::optional<std::int16_t> stand_actor_id;
    // Retain the successful seated action independently of menu selection, so
    // clicking another object and then the chair does not lose the Stand gate.
    std::optional<SavedObjectSelection> seated_action_target;
    std::optional<std::uint64_t> seated_action_actor_generation;
    std::optional<std::uint16_t> seated_action_code;
    std::optional<bool> render_interaction_result;
    std::map<std::tuple<std::string,std::string,std::uint16_t>,odh::CodeOwner> saved_code_owners;
    std::map<std::tuple<std::string,std::string,std::uint16_t>,odh::CodeOwner> saved_portal_code_owners;
    std::set<odh::CodeOwner> saved_stair_portal_code_owners;
    odh::CodeOwner next_saved_code_owner=0x80000000U;
    if(session_restore&&session_restore->seated_continuation()){
        const auto& seated=*session_restore->seated_continuation();
        const auto* target=world.find(seated.target_id);
        if(!target||!target->definition||target->definition->guid!=seated.target_guid||
           target->generation!=seated.target_generation)
            throw odh::FormatError("restored seated target identity changed before input binding");
        render_interaction_target=SavedObjectSelection{seated.target_id,seated.target_guid,
            seated.target_generation,seated.tree_table_id};
        render_interaction_action=seated.action;render_interaction_result=true;
        render_interaction_status="restored_sit";
        seated_action_target=render_interaction_target;
        seated_action_actor_generation=seated.actor_generation;
        seated_action_code=seated.action;
    }
    using SavedOwnerMap=std::map<std::tuple<std::string,std::string,std::uint16_t>,odh::CodeOwner>;
    const auto actor_owner_map_for=[&](std::int16_t actor_id)->SavedOwnerMap* {
        if(actor_id==runtime_avatar_id)return &saved_code_owners;
        const auto found=std::find_if(household_runtime.begin(),household_runtime.end(),
            [&](const auto& member){return member->id==actor_id;});
        return found==household_runtime.end()?nullptr:&(*found)->action_code_owners;
    };
    const auto actor_seated_binding=[&](std::int16_t actor_id)
        ->std::tuple<std::optional<SavedObjectSelection>*,std::optional<std::uint64_t>*,
            std::optional<std::uint16_t>*> {
        if(actor_id==runtime_avatar_id)return {&seated_action_target,&seated_action_actor_generation,&seated_action_code};
        const auto found=std::find_if(household_runtime.begin(),household_runtime.end(),
            [&](const auto& member){return member->id==actor_id;});
        if(found==household_runtime.end())return {nullptr,nullptr,nullptr};
        return {&(*found)->seated_action_target,&(*found)->seated_action_actor_generation,&(*found)->seated_action_code};
    };
    std::optional<std::uint64_t> previous_completion_uid;
    std::string go_here_status="not_requested",go_here_route_status="not_requested",go_here_diagnostic;
    bool go_here_cancel_pending=false;
    std::optional<std::uint16_t> go_here_action;
    std::optional<odh::CodeOwner> go_here_owner;
    std::optional<std::int16_t> go_here_marker_id;
    std::optional<std::uint64_t> go_here_marker_generation,go_here_previous_completion_uid;
    std::optional<odh::EntityPosition> go_here_position,go_here_start_position;
    std::optional<bool> go_here_result;
    if(session_restore&&session_restore->seated_continuation()){
        for(const auto& binding:session_restore->seated_continuation()->code_owners){
            const auto key=std::make_tuple(binding.source,binding.asset,binding.root_routine);
            if(!saved_code_owners.emplace(key,binding.owner).second)
                throw odh::FormatError("restored seated action owner identity is duplicated");
            if(binding.owner>=next_saved_code_owner){
                if(binding.owner==std::numeric_limits<odh::CodeOwner>::max())saved_owner_ids_exhausted=true;
                else next_saved_code_owner=binding.owner+1;
            }
        }
    }
    const auto load_interaction_semiglobal=[&](const odh::DefinitionCandidate& candidate)
        ->std::optional<SavedSemiglobalCodeBank>{
        std::optional<SavedTreeNameCatalog> inherited_catalog;
        const SavedTreeNameCatalog* catalog=nullptr;
        const auto found=saved_tree_names.find(candidate.definition.guid);
        if(found!=saved_tree_names.end())catalog=&found->second;
        // A multitile child can inherit its TTAB from a master whose GLOB
        // carries the semiglobal link; the child resource alone may not.
        if((!catalog||!catalog->semiglobal_name)&&definition_index&&
           candidate.definition.tree_table_id==0xffff&&candidate.definition.master_id!=0){
            if(const auto* master=definition_index->unique_master(candidate)){
                inherited_catalog=load_saved_tree_name_catalog(*master);
                catalog=&*inherited_catalog;
            }
        }
        if(catalog&&catalog->semiglobal_name&&globals&&!globals->standalone){
            const auto entry=*catalog->semiglobal_name+".iff";
            return load_saved_semiglobal_code_bank(globals->archive,entry);
        }
        return {};
    };
    const auto evaluate_interaction_tests=[&](const odh::DefinitionCandidate& candidate,
        std::int16_t actor,std::int16_t target,const std::vector<std::uint16_t>& tests,
        const odh::RoutineSet* linked_semiglobal=nullptr){
        std::vector<std::vector<std::int16_t>> groups;
        if(restored_world){groups.reserve(restored_world->multitile_groups.size());
            for(const auto& group:restored_world->multitile_groups)groups.push_back(group.members);}
        const auto read_saved_test_global=[&](std::int16_t index)->std::optional<std::int16_t>{
            // Match Main's read-only precedence: clock, active family, TS1
            // activation defaults, then static selected-lot globals.
            if(simulation_clock)if(const auto value=simulation_clock->global(index))return value;
            if(index==9&&family_global)return family_global;
            if(bootstrap_globals)if(const auto value=bootstrap_globals->read(index))return value;
            return lot?lot->read(index):std::nullopt;
        };
        odh::TuningResources test_tuning_resources;
        test_tuning_resources.owners.emplace(static_cast<odh::CodeOwner>(target),odh::TuningOwner{
            load_saved_action_tuning(candidate),{},odh::TuningSemiState::unknown});
        return evaluate_saved_test_functions(candidate,world,actor,target,tests,4096,128,{0},
            linked_semiglobal,groups,relationships,ngbh?&*ngbh:nullptr,read_saved_test_global,
            globals?&shared_program->routines():nullptr,lot?&*lot:nullptr,
            neighbors?&*neighbors:nullptr,
            active_family?std::optional<std::int32_t>{active_family->family.budget}:std::nullopt,
            &test_tuning_resources);
    };
    const auto prepare_selected_object=[&](std::optional<SavedObjectSelection> selection){
        render_selection=std::move(selection);
        render_menu_anchor.reset();
        render_interactions.reset();render_interaction_semiglobal.reset();
        render_test_results.clear();render_selected_interaction_row.reset();
        render_pie_menu_path.clear();render_pie_menu_selected=0;
        if(!render_selection||!definition_index)return;
        const auto* candidate=definition_index->unique(render_selection->guid);
        if(!candidate)return;
        render_interactions=saved_object_interactions(*candidate,*definition_index,game_language);
        if(!render_interactions->table||render_interactions->table->status!=odh::TtabStatus::decoded)return;
        const auto& rows=render_interactions->table->interactions;
        std::vector<std::uint16_t> tests;
        tests.reserve(std::min<std::size_t>(rows.size(),32));
        for(std::size_t n=0;n<std::min<std::size_t>(rows.size(),32);++n)tests.push_back(rows[n].test);
        const auto* actor=world.find(active_household_actor);
        try{render_interaction_semiglobal=load_interaction_semiglobal(*candidate);}
        catch(const std::exception& error){
            render_test_results.assign(tests.size(),{SavedTestFunctionResult::State::unsupported,0,0,error.what()});
        }
        if(render_test_results.empty())render_test_results=evaluate_interaction_tests(*candidate,
            active_household_actor,render_selection->id,tests,
            render_interaction_semiglobal?&render_interaction_semiglobal->routines:nullptr);
        const auto eligible=eligible_saved_interaction_rows(*render_interactions,render_test_results,
            actor,render_interaction_semiglobal?&render_interaction_semiglobal->routines:nullptr);
        if(!eligible.empty())render_selected_interaction_row=eligible.front();
        for(std::size_t n=render_test_results.size();n<rows.size();++n)
            render_test_results.push_back({SavedTestFunctionResult::State::unsupported,rows[n].test,0,
                "interaction row exceeds bounded selection-time probe profile"});
    };
    const auto request_selected_interaction=[&] {
        if(render_interaction_enqueued)return;
        if(!saved_world_main||!render_selection||!render_interactions){
            render_interaction_status="unsupported";render_interaction_diagnostic="select a saved-lot object with a decoded TTAB first";return;
        }
        const auto actor_id=active_household_actor;
        auto* actor_queue=actor_queue_for(actor_id);
        auto* actor_vm=actor_vm_for(actor_id);
        auto* owner_map=actor_owner_map_for(actor_id);
        if(!actor_queue||!actor_vm||!owner_map||actor_queue->size()!=0||actor_queue->active()){
            render_interaction_status="rejected";render_interaction_diagnostic="manual preview action requires this resident's available VM and an empty interaction queue";return;
        }
        const auto& selected=*render_selection;
        const auto* actor=world.find(actor_id);
        const auto* target=world.find(selected.id);
        if(!actor||actor->kind!=odh::EntityKind::avatar||!target||target->kind!=odh::EntityKind::object||
           target->generation!=selected.generation||!target->definition||target->definition->guid!=selected.guid){
            render_interaction_status="unsupported";render_interaction_diagnostic="selected actor/target identity is stale";return;
        }
        const auto* candidate=definition_index?definition_index->unique(selected.guid):nullptr;
        if(!candidate||candidate->source!=render_interactions->source||candidate->asset!=render_interactions->asset||
           candidate->definition.tree_table_id!=selected.tree_table_id){
            render_interaction_status="unsupported";render_interaction_diagnostic="selected definition provenance is ambiguous or changed";return;
        }
        if(!render_interactions->table||render_interactions->table->status!=odh::TtabStatus::decoded){
            render_interaction_status="unsupported";render_interaction_diagnostic="selected object has no decoded TTAB";return;
        }
        // Re-evaluate against the current World at the input boundary; an
        // earlier click-time TestFunction result can become stale as Main ticks.
        std::vector<std::uint16_t> current_tests;
        current_tests.reserve(std::min<std::size_t>(render_interactions->table->interactions.size(),32));
        for(std::size_t n=0;n<std::min<std::size_t>(render_interactions->table->interactions.size(),32);++n)
            current_tests.push_back(render_interactions->table->interactions[n].test);
        std::optional<SavedSemiglobalCodeBank> linked_bank;
        try{linked_bank=load_interaction_semiglobal(*candidate);}
        catch(const std::exception& error){
            render_interaction_status="unsupported";render_interaction_diagnostic=error.what();return;
        }
        const auto* linked_routines=linked_bank?&linked_bank->routines:nullptr;
        render_test_results=evaluate_interaction_tests(*candidate,actor_id,selected.id,current_tests,linked_routines);
        const auto eligible_rows=eligible_saved_interaction_rows(*render_interactions,render_test_results,actor,linked_routines);
        if(eligible_rows.empty()){
            render_interaction_status="unsupported";
            render_interaction_diagnostic="no TTAB row passed its TestFunction";
            for(const auto& test:render_test_results){
                render_interaction_diagnostic+="; BHAV"+std::to_string(test.routine)+" ";
                if(test.state==SavedTestFunctionResult::State::ineligible)
                    render_interaction_diagnostic+=(test.diagnostic.empty()?std::string("returned false"):test.diagnostic);
                else if(test.state==SavedTestFunctionResult::State::unsupported)
                    render_interaction_diagnostic+="unsupported: "+test.diagnostic;
                else render_interaction_diagnostic+="eligible but row permission or action profile rejected it";
            }
            return;
        }
        if(!render_selected_interaction_row||std::find(eligible_rows.begin(),eligible_rows.end(),
            *render_selected_interaction_row)==eligible_rows.end()){
            render_interaction_status="ineligible";
            render_interaction_diagnostic="selected TTAB action is no longer eligible at the input boundary";return;
        }
        const auto& row=render_interactions->table->interactions[*render_selected_interaction_row];
        const auto owner_key=std::make_tuple(candidate->source,candidate->asset,row.action);
        auto owner=owner_map->find(owner_key);
        if(owner==owner_map->end()){
            if(saved_owner_ids_exhausted){
                render_interaction_status="unsupported";render_interaction_diagnostic="saved object code-owner identity space exhausted";return;
            }
            odh::RoutineSet routines;
            odh::TuningTables action_tuning;
            const auto key=odh::routine_key(row.action);
            try{
                // A shared action BHAV still reads private tuning from the
                // selected object's resource (Simitone's ScopeResource is the
                // code owner's object resource, not the semiglobal bank).
                action_tuning=load_saved_action_tuning(*candidate);
                if(key.scope==odh::RoutineScope::private_object){
                    routines=load_saved_action_routines(*candidate,row.action);
                }
                else if(key.scope==odh::RoutineScope::semiglobal&&linked_bank&&
                        linked_bank->routines.count(key)){
                    routines=load_saved_private_routines(*candidate);
                    for(const auto& [linked_key,routine]:linked_bank->routines)
                        routines.emplace(linked_key,routine);
                }
            }
            catch(const std::exception& error){
                render_interaction_status="unsupported";render_interaction_diagnostic=error.what();return;
            }
            if(!routines.count(key)){
                render_interaction_status="unsupported";render_interaction_diagnostic="selected ActionFunction is absent from its exact private or linked semiglobal owner";return;
            }
            const auto code_owner=next_saved_code_owner;
            if(!actor_vm->install_owner(code_owner,std::move(routines))){
                render_interaction_status="unsupported";render_interaction_diagnostic="selected resident VM rejected saved object code owner (duplicate, capacity or unsafe point)";return;
            }
            saved_action_resources.emplace(code_owner,Resource{candidate->source,candidate->asset,candidate->asset.empty()});
            saved_action_tuning.emplace(code_owner,std::move(action_tuning));
            if(next_saved_code_owner==std::numeric_limits<odh::CodeOwner>::max())saved_owner_ids_exhausted=true;
            else ++next_saved_code_owner;
            owner=owner_map->emplace(owner_key,code_owner).first;
        }
        actor=world.find(actor_id);target=world.find(selected.id);
        if(!actor||actor->kind!=odh::EntityKind::avatar||!target||target->generation!=selected.generation||
           !target->definition||target->definition->guid!=selected.guid){
            render_interaction_status="unsupported";render_interaction_diagnostic="live actor/target identity changed while loading action owner";return;
        }
        saved_action_resources.emplace(owner->second,Resource{candidate->source,candidate->asset,candidate->asset.empty()});
        const odh::VmContext context{actor_id,selected.id,selected.id,owner->second};
        if(!actor_queue->enqueue(odh::routine_key(row.action),context)){
            render_interaction_status="rejected";render_interaction_diagnostic=actor_queue->diagnostic();return;
        }
        render_interaction_enqueued=true;render_interaction_status="queued";render_interaction_action=row.action;
        render_interaction_owner=owner->second;
        render_interaction_target=selected;
        render_interaction_actor_generation=actor->generation;
        render_interaction_actor_id=actor_id;
        previous_completion_uid=actor_queue->completion()?std::optional<std::uint64_t>{actor_queue->completion()->uid}:std::optional<std::uint64_t>{};
        render_interaction_diagnostic.clear();
    };
    // Stand is an explicit host control, not a fabricated TTAB row. Its
    // supported precondition is a successful action in this session that left
    // this exact avatar attached to this exact selected object.
    const auto stand_selection_is_current=[&](const SavedObjectSelection& selected){
        const auto actor_id=active_household_actor;
        const auto* actor=world.find(actor_id);
        const auto* target=world.find(selected.id);
        const auto* actor_queue=actor_queue_for(actor_id);
        const auto [seated_target,seated_generation,seated_code]=actor_seated_binding(actor_id);
        return saved_world_main&&actor_queue&&actor_queue->size()==0&&!actor_queue->active()&&
            seated_target&&*seated_target&&(*seated_target)->id==selected.id&&
            (*seated_target)->generation==selected.generation&&(*seated_target)->guid==selected.guid&&
            seated_generation&&*seated_generation&&seated_code&&*seated_code&&
            actor&&actor->generation==**seated_generation&&
            actor->kind==odh::EntityKind::avatar&&
            !actor->person_data.empty()&&actor->person_data[0]==1&&actor->attachment&&
            actor->attachment->container==selected.id&&target&&target->generation==selected.generation&&
            target->kind==odh::EntityKind::object&&target->definition&&target->definition->guid==selected.guid;
    };
    const auto request_stand=[&](const SavedObjectSelection& selected,const std::string& trigger,std::uint64_t request_tick){
        if(headless_stand_status=="queued")return;
        const auto actor_id=active_household_actor;
        auto* actor_queue=actor_queue_for(actor_id);
        auto* actor_vm=actor_vm_for(actor_id);
        auto* owner_map=actor_owner_map_for(actor_id);
        if(!actor_queue||!actor_vm||!owner_map){
            headless_stand_applied=true;stand_trigger=trigger;stand_request_tick=request_tick;
            headless_stand_status="unsupported";headless_stand_diagnostic="selected resident VM/controller is unavailable";return;
        }
        headless_stand_applied=true;stand_trigger=trigger;stand_request_tick=request_tick;stand_request_target=selected.id;
        stand_actor_id=actor_id;
        headless_stand_precondition_met=false;headless_stand_action.reset();headless_stand_owner.reset();
        headless_stand_result.reset();headless_stand_diagnostic.clear();
        headless_stand_posture_before.reset();headless_stand_attachment_before.reset();
        const auto fail_stand=[&](const std::string& reason){headless_stand_status="unsupported";headless_stand_diagnostic=reason;};
        const auto completion=actor_queue->completion();
        const auto* actor=world.find(actor_id);
        const auto [seated_target,seated_generation,seated_code]=actor_seated_binding(actor_id);
        const auto* target=world.find(selected.id);
        if(actor&&actor->person_data.size()>0)headless_stand_posture_before=actor->person_data[0];
        if(actor&&actor->attachment)headless_stand_attachment_before=actor->attachment->container;
        if(!completion||!seated_target||!seated_generation||!seated_code||!*seated_target||
           !*seated_generation||!*seated_code)
            fail_stand("no successful seated action is available in this session");
        else if(!stand_selection_is_current(selected))
            fail_stand("Stand requires the same seated avatar and selected object generations, attachment and an idle queue");
        else if(!target||target->kind!=odh::EntityKind::object||!target->entry_points||target->entry_points->size()<=27)
            fail_stand("Stand target has no bound entry point 27");
        else {
            const auto stand_action=(*target->entry_points)[27].action;
            const auto* candidate=target->definition?definition_index->unique(target->definition->guid):nullptr;
            const auto stand_scope=stand_action?odh::routine_key(stand_action).scope:odh::RoutineScope::private_object;
            if(!stand_action||!candidate||(stand_scope!=odh::RoutineScope::private_object&&
               stand_scope!=odh::RoutineScope::semiglobal))
                fail_stand("Stand entry point 27 has no unique private or linked semiglobal function owner");
            else {
                const auto owner_key=std::make_tuple(candidate->source,candidate->asset,stand_action);
                auto owner=owner_map->find(owner_key);
                if(owner==owner_map->end()){
                    if(saved_owner_ids_exhausted)fail_stand("saved object code-owner identity space exhausted");
                    else {
                        try {
                            odh::RoutineSet routines;
                            if(stand_scope==odh::RoutineScope::private_object)
                                routines=load_saved_action_routines(*candidate,stand_action);
                            else {
                                const auto linked_bank=load_interaction_semiglobal(*candidate);
                                if(!linked_bank)throw odh::FormatError("Stand semiglobal has no exact GLOB-linked bank");
                                routines=load_saved_private_routines(*candidate);
                                for(const auto& [key,routine]:linked_bank->routines)
                                    if(!routines.emplace(key,routine).second)
                                        throw odh::FormatError("duplicate private and linked semiglobal Stand routine");
                            }
                            const auto key=odh::routine_key(stand_action);
                            if(!routines.count(key))fail_stand("Stand function is absent from its exact private or linked semiglobal owner");
                            else {
                                const auto code_owner=next_saved_code_owner;
                                if(!actor_vm->install_owner(code_owner,std::move(routines)))
                                    fail_stand("selected resident VM rejected saved Stand code owner");
                                else {
                                    if(next_saved_code_owner==std::numeric_limits<odh::CodeOwner>::max())saved_owner_ids_exhausted=true;
                                    else ++next_saved_code_owner;
                                    owner=owner_map->emplace(owner_key,code_owner).first;
                                }
                            }
                        } catch(const std::exception& error){fail_stand(error.what());}
                    }
                }
                if(owner!=owner_map->end()){
                    saved_action_resources.emplace(owner->second,Resource{candidate->source,candidate->asset,candidate->asset.empty()});
                    const odh::VmContext context{actor_id,selected.id,selected.id,owner->second};
                    if(!stand_selection_is_current(selected))
                        fail_stand("Stand actor/target identity or attachment changed while loading its function");
                    else if(!actor_queue->enqueue(odh::routine_key(stand_action),context))
                        fail_stand(actor_queue->diagnostic());
                    else {
                        headless_stand_precondition_met=true;headless_stand_action=stand_action;
                        headless_stand_owner=owner->second;headless_stand_status="queued";
                        headless_stand_previous_completion=completion->uid;
                    }
                }
            }
        }
    };
    std::optional<std::string> current_overlay_text;
    std::uint8_t current_overlay_language=game_language;
    const auto interaction_window_title=[&]{
        current_overlay_text.reset();
        const std::string title="OpenDollhouse";
        if(!render_selection&&render_ground_selection){
            current_overlay_text="Go here";
        }
        if(render_selection&&stand_selection_is_current(*render_selection)){
            current_overlay_text="Stand up";
        }
        return title;
    };
    const auto current_interaction_pie_menu=[&]{
        std::vector<odh::PieMenuChoice> choices;
        if(!render_selection||!render_interactions||!render_interactions->table)
            return std::vector<odh::PieMenuItem>{};
        const auto eligible=eligible_saved_interaction_rows(*render_interactions,render_test_results,
            world.find(active_household_actor),
            render_interaction_semiglobal?&render_interaction_semiglobal->routines:nullptr);
        choices.reserve(eligible.size());
        for(const auto row:eligible){
            std::optional<std::string> label;
            if(row<render_interactions->row_labels.size())label=render_interactions->row_labels[row];
            const auto language=row<render_interactions->row_label_languages.size()
                ?render_interactions->row_label_languages[row]:game_language;
            choices.push_back({row,std::move(label),language});
        }
        return odh::make_pie_menu_model(choices);
    };
    const auto active_pie_menu_items=[&](const std::vector<odh::PieMenuItem>& root)
        ->const std::vector<odh::PieMenuItem>*{
        auto* current=&root;
        std::size_t valid=0;
        for(;valid<render_pie_menu_path.size();++valid){
            const auto index=render_pie_menu_path[valid];
            if(index>=current->size()||(*current)[index].children.empty())break;
            current=&(*current)[index].children;
        }
        if(valid<render_pie_menu_path.size()){
            render_pie_menu_path.resize(valid);render_pie_menu_selected=0;
        }
        if(render_pie_menu_selected>=current->size())render_pie_menu_selected=0;
        return current;
    };
    const auto pie_menu_item_label=[](const odh::PieMenuItem& item,bool indicate_category){
        auto label=item.label;
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
        label=odh_host::legacy_text_to_utf8(label,item.label_language,2048);
#endif
        if(indicate_category&&(label.size()<3||label.compare(label.size()-3,3,"...")!=0))label+="...";
        return label;
    };
    const auto current_pie_menu_path_labels=[&]{
        std::vector<std::string> labels;
        const auto model=current_interaction_pie_menu();
        const std::vector<odh::PieMenuItem>* items=&model;
        for(const auto index:render_pie_menu_path){
            if(index>=items->size())break;
            const auto& item=(*items)[index];
            labels.push_back(pie_menu_item_label(item,item.interaction_row==std::nullopt&&!item.children.empty()));
            items=&item.children;
        }
        return labels;
    };
    const auto activate_pie_menu_selection=[&]{
        auto model=current_interaction_pie_menu();
        const auto* items=active_pie_menu_items(model);
        if(!items||items->empty())return std::string("no eligible actions");
        const auto item=(*items)[render_pie_menu_selected];
        if(item.interaction_row){
            render_selected_interaction_row=*item.interaction_row;
            render_interaction_requested=true;
            request_selected_interaction();
            return std::string("interaction ")+render_interaction_status;
        }
        if(!item.children.empty()){
            render_pie_menu_path.push_back(render_pie_menu_selected);
            render_pie_menu_selected=0;
            return std::string("opened ")+item.label;
        }
        return std::string("empty menu item");
    };
    const auto draw_interaction_menu=[&](odh::RasterFrame& image){
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if(!render_selection||!render_interactions||!render_interactions->table||render_interaction_enqueued)return;
        const auto model=current_interaction_pie_menu();
        const auto* items=active_pie_menu_items(model);
        if(!items||items->empty())return;
        const auto anchor=render_menu_anchor.value_or(std::array<std::int32_t,2>{8,8});
        const auto layout=odh::layout_pie_menu(image.width,image.height,anchor,items->size(),
            !render_pie_menu_path.empty());
        if(!layout)return;
        constexpr std::array<std::uint8_t,3> pie_color_key{255,0,255};
        if(pie_menu_background){
            const auto x=layout->center[0]-static_cast<std::int32_t>(pie_menu_background->width/2);
            const auto y=layout->center[1]-static_cast<std::int32_t>(pie_menu_background->height/2);
            (void)odh::blit_color_keyed_bitmap(image,*pie_menu_background,x,y,pie_color_key);
        }
        for(std::size_t index=0;index<items->size();++index){
            const auto& item=(*items)[index];
            const bool category=!item.interaction_row&&!item.children.empty();
            const auto label=pie_menu_item_label(item,category);
            const auto& rect=layout->items[index];
            const auto icon_x=rect.x+3;
            const auto icon_y=rect.y+(rect.height-17)/2;
            if(pie_menu_button)
                (void)odh::blit_color_keyed_bitmap(image,*pie_menu_button,icon_x,icon_y,pie_color_key);
            odh_host::TextOverlayStyle style;
            style.x=icon_x+19;style.y=rect.y;style.width=std::max(1,rect.width-22);style.height=rect.height;
            style.font_size_px=13;style.padding=4;
            style.foreground={250,248,230,255};
            style.background=index==render_pie_menu_selected
                ?std::array<std::uint8_t,4>{42,91,121,238}
                :category?std::array<std::uint8_t,4>{44,57,50,236}
                         :std::array<std::uint8_t,4>{24,27,31,218};
            (void)odh_host::draw_text_overlay(image,
                (index==render_pie_menu_selected?"> ":"  ")+label,item.label_language,style);
        }
        if(layout->back_button){
            const odh::PieMenuItem* parent=nullptr;
            const std::vector<odh::PieMenuItem>* siblings=&model;
            for(const auto index:render_pie_menu_path){
                if(index>=siblings->size())break;
                parent=&(*siblings)[index];siblings=&parent->children;
            }
            const auto label=parent?pie_menu_item_label(*parent,false):std::string("Back");
            odh_host::TextOverlayStyle style;
            style.x=layout->back_button->x;style.y=layout->back_button->y;
            style.width=layout->back_button->width;style.height=layout->back_button->height;
            style.font_size_px=12;style.padding=4;style.foreground={250,248,230,255};
            style.background={93,68,39,242};
            (void)odh_host::draw_text_overlay(image,"< "+label, parent?parent->label_language:game_language,style);
        }
#else
        (void)image;
#endif
    };
#ifdef ODH_HAVE_X11
    std::unique_ptr<odh_host::LinuxWindow> owned_render_window;
    odh_host::LinuxWindow* render_window_host=nullptr;
#endif
    odh::WorldProjection render_camera;render_camera.width=320;render_camera.height=240;render_camera.center_tile={0,0,0.75f};
    if(render_size){render_camera.width=(*render_size)[0];render_camera.height=(*render_size)[1];}
    const auto handle_interaction_menu_click=[&](std::int32_t x,std::int32_t y){
        if(!render_selection||!render_interactions||!render_interactions->table||render_interaction_enqueued)
            return false;
        auto* action_queue=actor_queue_for(active_household_actor);
        if(!action_queue||action_queue->size()!=0||action_queue->active())return false;
        const auto model=current_interaction_pie_menu();
        const auto* items=active_pie_menu_items(model);
        if(!items||items->empty())return false;
        const auto layout=odh::layout_pie_menu(render_camera.width,render_camera.height,
            render_menu_anchor.value_or(std::array<std::int32_t,2>{8,8}),items->size(),
            !render_pie_menu_path.empty());
        if(!layout)return false;
        if(layout->back_button&&layout->back_button->contains(x,y)){
            render_pie_menu_path.pop_back();render_pie_menu_selected=0;
            return true;
        }
        for(std::size_t index=0;index<layout->items.size();++index){
            if(!layout->items[index].contains(x,y))continue;
            render_pie_menu_selected=index;
            const auto& item=(*items)[index];
            if(item.interaction_row){
                render_selected_interaction_row=*item.interaction_row;
                render_interaction_requested=true;request_selected_interaction();
            }else if(!item.children.empty()){
                render_pie_menu_path.push_back(index);render_pie_menu_selected=0;
            }
            return true;
        }
        return false;
    };
    std::array<float,3> render_position{0,0,0};std::uint8_t render_direction=0;
    std::optional<std::uint16_t> presented_saved_id;std::optional<std::uint32_t> presented_saved_guid;
    std::optional<std::uint16_t> presented_runtime_entity_id;
    std::optional<std::uint32_t> presented_avatar_guid;
    if(render_saved_id){
        const odh::SavedObjectPrefix* saved=nullptr;
        const odh::Entity* live_selected=nullptr;
        if(saved_world_main){
            const auto found=std::find_if(restored_world->records.begin(),restored_world->records.end(),
                [&](const auto& record){return record.saved.object_id==*render_saved_id;});
            if(found!=restored_world->records.end())saved=&found->saved;
            live_selected=world.find(static_cast<std::int16_t>(*render_saved_id));
            if(!saved&&(!live_selected||live_selected->kind!=odh::EntityKind::avatar||!live_selected->definition||
               live_selected->definition->guid!=render_saved_guid.value_or(person.binding->guid)||!live_selected->position))
                throw odh::FormatError("selected household avatar is neither serialized nor positioned in the prepared World");
        }else{
            const auto found=std::find_if(saved_prefixes->begin(),saved_prefixes->end(),
                [&](const auto& value){return value.object_id==*render_saved_id;});
            if(found!=saved_prefixes->end())saved=&*found;
        }
        if(!saved&&!saved_world_main)throw odh::FormatError("render saved avatar ID absent from selected house");
        if(saved){
            const auto* type=objt->entry_for_type(saved->type_id);
            if(saved->guid!=person.binding->guid||!type||!type->metadata||type->metadata->objd_type!=2)
                throw odh::FormatError("render saved avatar identity does not match the selected person resource");
        }
        const auto& p=saved?saved->prefix:odh::ObjmPrefix{};
        if(saved_world_main){
            const auto* entity=world.find(runtime_avatar_id);
            if(!entity||!entity->position)throw odh::FormatError("render saved avatar has no live restored World position");
            if(entity->position){
                render_position={static_cast<float>(entity->position->x)/16.0f,
                    static_cast<float>(entity->position->y)/16.0f,
                    static_cast<float>(entity->position->level-1)*2.95f};
                render_direction=entity->position->direction;
            }
        }else{
            if((p.x==-16&&p.y==-16)||p.x<0||p.y<0||p.x>32767||p.y>32767||(p.level!=1&&p.level!=2)||p.object_data[1]<0||p.object_data[1]>7)
                throw odh::FormatError("render saved avatar has no supported lot position");
            render_position={static_cast<float>(p.x)/16.0f,static_cast<float>(p.y)/16.0f,static_cast<float>(p.level-1)*2.95f};
            render_direction=static_cast<std::uint8_t>(p.object_data[1]);
        }
        render_camera.center_tile={render_position[0],render_position[1],render_position[2]+0.75f};
        presented_runtime_entity_id=*render_saved_id;
        presented_avatar_guid=saved?saved->guid:live_selected->definition->guid;
        if(saved){presented_saved_id=*render_saved_id;presented_saved_guid=saved->guid;}
    }
    if(render_requested){
        if(!body_state||!body_state->body||!playback||!animation_catalog)
            throw odh::FormatError("person frame export requires initialized BodySTR and explicit AnimationCatalog");
        if(render_output){
            if(render_output->empty()||render_output->size()>4096||render_output->find('\0')!=std::string::npos)
                throw odh::FormatError("person frame output path outside bounded profile");
            std::error_code path_error;
            if(std::filesystem::exists(*render_output,path_error)||path_error)
                throw odh::FormatError("person frame output must be a new file; existing output preserved");
        }
        auto game_root=session_context&&!session_context->game_root.empty()
            ?std::filesystem::path(session_context->game_root)
            :std::filesystem::path(*render_animation_far).parent_path().parent_path().parent_path();
        auto skins_directory=game_root/"GameData"/"Skins";
        std::error_code skin_path_error;
        const auto loose_skins=std::filesystem::is_directory(skins_directory,skin_path_error)&&!skin_path_error
            ?skins_directory.string():std::string{};
        render_appearances=std::make_unique<SavedAppearanceResources>(*render_animation_far,*render_textures_far,
            *render_skeleton_entry,*render_skeleton_name,loose_skins);
        auto scene_config=render_appearances->resolve_body(*body_state->body);
        if(const auto* live_avatar=world.find(runtime_avatar_id))
            render_appearances->append_accessories(scene_config,live_avatar->bound_appearances);
        const SceneCfpResolver provider=[&](const odh::AnimationHeader& selected){
            auto decoded=animation_catalog->decode(selected);
            const auto* planes=&decoded->planes;
            return std::shared_ptr<const odh::CfpPlanes>(std::move(decoded),planes);
        };
        render_renderer=std::make_unique<SceneRenderer>(scene_config,provider);
        for(auto& member:household_runtime){
            if(!member->body_state||!member->body_state->body||!member->playback)continue;
            const auto member_scene_config=render_appearances->resolve_body(*member->body_state->body);
            member->scene_renderer=std::make_unique<SceneRenderer>(member_scene_config,provider);
        }
        if(render_floors){
            render_lot=std::make_unique<SavedLotRenderer>(saved_architecture_resource(*lot_path),
                *restored_world,*definition_index,*render_floors,render_walls,render_lot_view,render_materials);
        }else if(saved_world_main)render_graphics=std::make_unique<SavedGraphicsResources>(*definition_index);
        if(saved_world_main){
            const auto graphics=render_lot?(headless_main?render_lot->entity_graphics(runtime_avatar_id):
                render_lot->avatar(runtime_avatar_id)):render_graphics->entity(world,runtime_avatar_id);
            render_position=graphics.tile_position;render_direction=graphics.direction;
            render_camera.center_tile={render_position[0],render_position[1],render_position[2]+0.75f};
        }
#ifdef ODH_HAVE_X11
        if(render_window){
            std::string title=render_lot?
                "OpenDollhouse Main preview: click selects object or ground; Space/Return interacts; S saves experimental state; Backspace cancels Go Here, P pause, arrows rotate, +/- zoom, Escape close":
                "OpenDollhouse Main preview: S saves experimental state; Backspace cancels Go Here, P pause, arrows rotate, +/- zoom, Escape close";
            if(neighborhood_resume)title+=" | M: neighborhood";
            if(shared_window){
                shared_window->resize(render_camera.width,render_camera.height);
                shared_window->set_title(title);
                render_window_host=shared_window;
            }else{
                owned_render_window=std::make_unique<odh_host::LinuxWindow>(
                    render_camera.width,render_camera.height,title);
                render_window_host=owned_render_window.get();
            }
            if(render_window_host)present_loading_lot(*render_window_host,game_language);
        }
#else
        if(render_window&&shared_window)
            throw odh::FormatError("shared Linux window requested but this build has no X11 backend");
#endif
    }
    if(headless_main&&!render_lot){
        render_lot=std::make_unique<SavedLotRenderer>(saved_architecture_resource(*lot_path),
            *restored_world,*definition_index,*render_floors,render_walls,render_lot_view,render_materials);
    }
    if(render_lot&&render_lot->runtime_room_map())vm_room_map=&*render_lot->runtime_room_map();
    if(saved_world_main){
        const auto* actor=world.find(runtime_avatar_id);
        if(!render_lot)route_runtime_reason="prepared_lot_renderer_required";
        else if(!shared.routing_slots)route_runtime_reason="global_SLOT100_unavailable";
        else if(!render_lot->prepared_architecture())route_runtime_reason="prepared_architecture_unavailable";
        else if(!actor||!actor->position)route_runtime_reason="runtime_avatar_position_unavailable";
        else if(actor->position->level<1||actor->position->level>2)route_runtime_reason="runtime_avatar_level_unsupported";
        else {
            const auto room=render_lot->runtime_room_at(*actor->position);
            if(!room)route_runtime_reason="runtime_avatar_room_unresolved";
            else {
                const auto probe=build_runtime_attempt_walk_probe(*restored_world,world,*definition_index,
                    render_lot->architecture_resource(),*render_lot->prepared_architecture(),*room,
                    static_cast<std::uint8_t>(actor->position->level-1),runtime_avatar_id,
                    true,{},resolve_read_only_ep5);
                if(probe.snapshot.state!=odh::AttemptWalkObstacleState::ready){
                    route_runtime_reason=std::string("attempt_walk_snapshot_")+odh::attempt_walk_obstacle_reason_name(probe.snapshot.reason);
                    route_ep5_unknown_caller=probe.snapshot.entry_point5_unknown_caller;
                    route_ep5_unknown_stack_object=probe.snapshot.entry_point5_unknown_stack_object;
                    route_ep5_unknown_action=probe.snapshot.entry_point5_unknown_action;
                }else {
                    runtime_route_obstacle_snapshot=probe.snapshot;
                    runtime_route_static_obstacle_count=probe.roughbalanced_architecture_rectangles+4;
                    runtime_route_collision_candidates.clear();
                    runtime_route_collision_candidates.reserve(restored_world->records.size());
                    bool collision_inventory_complete=true;
                    for(const auto& record:restored_world->records) {
                        if(record.saved.object_id>static_cast<std::uint16_t>(std::numeric_limits<std::int16_t>::max())) {
                            collision_inventory_complete=false;break;
                        }
                        const auto id=static_cast<std::int16_t>(record.saved.object_id);
                        runtime_route_collision_candidates.push_back(
                            odh::saved_collision_candidate(record,world.find(id)));
                    }
                    if(!collision_inventory_complete)
                        runtime_route_collision_candidates.clear();
                    saved_stair_portal_endpoint_ids=probe.stair_lower_portal_ids;
                    saved_stair_portal_endpoint_ids.insert(probe.stair_upper_portal_ids.begin(),
                        probe.stair_upper_portal_ids.end());
                    auto grid=odh::build_world_room_route_grid(probe.snapshot);
                    if(grid.reason==odh::WorldRoomRouteGridReason::route_grid_over_budget){
                        // Match the bounded refresh path below: large rooms keep
                        // their full obstacle inventory while allocating only a
                        // local window around the Sim.
                        constexpr std::int32_t radius=128;
                        const auto x=actor->position->x,y=actor->position->y;
                        grid=odh::build_world_room_route_grid(probe.snapshot,{},
                            odh::WorldRoomRouteGridWindow{{x-radius,y-radius},{x+radius,y+radius}});
                    }
                    if(grid.state!=odh::WorldRoomRouteGridState::ready||!grid.grid)
                        route_runtime_reason=std::string("route_grid_")+odh::world_room_route_grid_reason_name(grid.reason);
                    else {
                        runtime_route_grid.emplace(std::move(grid));
                        odh::PreRankedSlotGoalProvider range_slot_goals=[&](const auto& table,std::size_t index,
                            const auto& caller,const auto& target) {
                            const bool no_sitting_target=odh::is_child_shy_sitting_fallback_slot(table,index);
                            const auto* slot=odh::slot_of_type(table,3,index);
                            if(!slot||!runtime_route_grid||!runtime_route_grid->grid)return std::vector<odh::PreRankedSlotGoal>{};
                            const auto target_room=render_lot->runtime_room_at(target);
                            // This route grid describes one room only. IgnoreRooms cannot
                            // make coordinates in another room safe to route on it.
                            if(!target_room||*target_room!=runtime_route_grid->room_id)
                                return std::vector<odh::PreRankedSlotGoal>{};
                            const auto candidate_in_room=[&](odh::RoutePoint point) {
                                auto candidate=target;candidate.x=point.x;candidate.y=point.y;
                                const auto candidate_room=render_lot->runtime_room_at(candidate);
                                return candidate_room&&*candidate_room==runtime_route_grid->room_id;
                            };
                            const auto candidate_position_valid=[&](odh::RoutePoint point) {
                                return runtime_route_grid->grid->contains(point)&&
                                       runtime_route_grid->grid->walkable(point);
                            };
                            const auto candidate_sitting_object=[&](odh::RoutePoint point,
                                std::uint8_t facing)->std::optional<std::int16_t> {
                                if(!odh::is_child_shy_sitting_fallback_slot(table,index)||!restored_world||
                                   !render_lot->runtime_room_map()||!runtime_route_obstacle_snapshot||
                                   runtime_route_obstacle_snapshot->state!=odh::AttemptWalkObstacleState::ready||
                                   runtime_route_static_obstacle_count>runtime_route_obstacle_snapshot->obstacles.size()||
                                   runtime_route_collision_candidates.size()!=restored_world->records.size())return {};
                                const auto* source_slot=odh::slot_of_type(table,3,index);
                                if(!source_slot||table.records.empty())return {};
                                auto geometry_slots=table;
                                const auto record_index=static_cast<std::size_t>(source_slot-table.records.data());
                                if(record_index>=geometry_slots.records.size())return {};
                                // The footprint selector accepts the matching standing profile. Its
                                // result reports which object overlaps; EntryPoint26 and facing are
                                // checked below before the SLOT score treats it as a seat.
                                geometry_slots.records[record_index].sitting=0;
                                auto* actor=world.find(runtime_avatar_id);
                                if(!actor||!actor->position)return {};
                                const auto actor_candidate=std::find_if(runtime_route_collision_candidates.begin(),
                                    runtime_route_collision_candidates.end(),[&](const auto& item) {
                                        return item.object_id==runtime_avatar_id;
                                    });
                                if(actor_candidate==runtime_route_collision_candidates.end())return {};
                                actor_candidate->position=actor->position;
                                auto candidate_position=target;
                                candidate_position.x=point.x;candidate_position.y=point.y;
                                candidate_position.direction=facing;
                                const auto shape=odh::avatar_candidate_collision_shape(candidate_position);
                                if(shape.state!=odh::CollisionShapeState::present)return {};
                                for(std::size_t n=0;n<runtime_route_static_obstacle_count;++n) {
                                    const auto& obstacle=runtime_route_obstacle_snapshot->obstacles[n];
                                    if(odh::collision_boxes_intersect(shape.box,
                                        {obstacle.x1,obstacle.y1,obstacle.x2,obstacle.y2}))return {};
                                }
                                const auto selection=odh::assess_saved_slot_goal_selection(
                                    geometry_slots,index,*render_lot->runtime_room_map(),runtime_avatar_id,
                                    candidate_position,runtime_route_collision_candidates,
                                    restored_world->multitile_groups,true);
                                if(selection.state!=odh::SavedSlotGoalSelectionState::object_overlap_rejected||
                                   selection.object_overlaps!=1||selection.avatar_overlaps!=0||!selection.blocker)return {};
                                const auto* chair=world.find(*selection.blocker);
                                if(!chair||chair->kind!=odh::EntityKind::object||!chair->position||
                                   !chair->entry_points||chair->entry_points->size()<=26||
                                   chair->entry_points->at(26).action==0)return {};
                                const auto delta=std::abs(static_cast<int>(facing)-
                                                          static_cast<int>(chair->position->direction));
                                if(std::min(delta,8-delta)>1)return {};
                                return selection.blocker;
                            };
                            const auto candidate_sitting_approach=[&](odh::RoutePoint occupied)
                                ->std::optional<odh::RoutePoint> {
                                if(!runtime_route_grid||!runtime_route_grid->grid)return {};
                                const auto* actor=world.find(runtime_avatar_id);
                                if(!actor||!actor->position)return {};
                                for(std::int32_t radius=1;radius<=16;++radius) {
                                    std::optional<odh::RoutePoint> best;
                                    std::int64_t best_distance=std::numeric_limits<std::int64_t>::max();
                                    for(std::int32_t dx=-radius;dx<=radius;++dx) {
                                        const auto dy=radius-std::abs(dx);
                                        for(const auto sign:{-1,1}) {
                                            if(dy==0&&sign==1)continue;
                                            const auto x=static_cast<std::int64_t>(occupied.x)+dx;
                                            const auto y=static_cast<std::int64_t>(occupied.y)+dy*sign;
                                            if(x<std::numeric_limits<std::int32_t>::min()||
                                               x>std::numeric_limits<std::int32_t>::max()||
                                               y<std::numeric_limits<std::int32_t>::min()||
                                               y>std::numeric_limits<std::int32_t>::max())continue;
                                            const odh::RoutePoint point{static_cast<std::int32_t>(x),
                                                                        static_cast<std::int32_t>(y)};
                                            if(!candidate_position_valid(point)||!candidate_in_room(point))continue;
                                            const auto ddx=x-actor->position->x,ddy=y-actor->position->y;
                                            const auto distance=ddx*ddx+ddy*ddy;
                                            if(distance<best_distance){best=point;best_distance=distance;}
                                        }
                                    }
                                    if(best)return best;
                                }
                                return {};
                            };
                            return odh::rank_reference_range_slot(table,index,caller,target,random,
                                no_sitting_target,candidate_in_room,candidate_position_valid,
                                candidate_sitting_object,candidate_sitting_approach);
                        };
                        runtime_routing=std::make_unique<odh::Routing>(world,*runtime_route_grid->grid,
                            *shared.routing_slots);
                        runtime_routing->set_ranged_slot_goal_provider(std::move(range_slot_goals));
                        runtime_routing->set_sitting_action_provider([&](std::int16_t actor_id,
                            std::int16_t chair_id)->std::optional<odh::VmCall> {
                            try {
                                if(!restored_world||!definition_index||!globals||globals->standalone||
                                   actor_id!=runtime_avatar_id||chair_id<=0||saved_owner_ids_exhausted)
                                    return {};
                                const auto record=std::find_if(restored_world->records.begin(),restored_world->records.end(),
                                    [&](const auto& item){return item.saved.object_id==static_cast<std::uint16_t>(chair_id);});
                                const auto* chair=world.find(chair_id);
                                const auto* candidate=chair&&chair->definition?definition_index->unique(chair->definition->guid):nullptr;
                                if(record==restored_world->records.end()||!chair||chair->kind!=odh::EntityKind::object||
                                   !chair->position||!candidate)
                                    throw odh::FormatError("selected chair identity or saved definition is unavailable");
                                const auto function=saved_entry_point_function(*candidate,26);
                                const auto owner_key=std::make_tuple(candidate->source,candidate->asset,function.action);
                                auto owner=saved_portal_code_owners.find(owner_key);
                                const auto code_owner=owner==saved_portal_code_owners.end()?next_saved_code_owner:owner->second;
                                std::optional<SavedSemiglobalCodeBank> semiglobal_bank;
                                if(function.semiglobal_name)
                                    semiglobal_bank=load_saved_semiglobal_code_bank(globals->archive,
                                        *function.semiglobal_name+".iff");
                                const auto global_bank=load_saved_global_code_bank(globals->archive,globals->entry);
                                bool condition_passed=false;
                                if(function.condition!=0) {
                                    const auto condition_key=odh::routine_key(function.condition);
                                    const odh::Bhav* condition=nullptr;
                                    odh::RoutineSet private_bank;
                                    switch(condition_key.scope) {
                                    case odh::RoutineScope::private_object:
                                        private_bank=load_saved_private_routines(*candidate);
                                        if(const auto found=private_bank.find(condition_key);found!=private_bank.end())
                                            condition=&found->second;
                                        break;
                                    case odh::RoutineScope::semiglobal:
                                        if(semiglobal_bank)if(const auto found=semiglobal_bank->routines.find(condition_key);
                                            found!=semiglobal_bank->routines.end())condition=&found->second;
                                        break;
                                    case odh::RoutineScope::global:
                                        if(const auto found=global_bank.routines.find(condition_key);
                                            found!=global_bank.routines.end())condition=&found->second;
                                        break;
                                    }
                                    // Simitone permits an entry point when its optional condition
                                    // routine is absent from the linked banks.
                                    condition_passed=!condition||evaluate_saved_entry_point_condition(function.condition,*condition,world,
                                        {actor_id,chair_id,chair_id,code_owner});
                                    if(!condition_passed)return {};
                                }
                                const auto plan=saved_avatar_entry_point_vm_call_plan(*record,world,*definition_index,
                                    actor_id,chair_id,26,code_owner,
                                    semiglobal_bank?&*semiglobal_bank:nullptr,&global_bank,condition_passed);
                                if(owner==saved_portal_code_owners.end()) {
                                    if(!install_saved_entry_point_vm_call_plan(vm,plan))
                                        throw odh::FormatError("Main VM rejected the EntryPoint26 code owner");
                                    if(next_saved_code_owner==std::numeric_limits<odh::CodeOwner>::max())
                                        saved_owner_ids_exhausted=true;
                                    else ++next_saved_code_owner;
                                    saved_portal_code_owners.emplace(owner_key,code_owner);
                                }
                                return plan.call;
                            } catch(const std::exception& error) {
                                route_runtime_reason=std::string("sitting_entry_point_26_")+error.what();
                                return {};
                            }
                        });
                        runtime_routing->set_sitting_slot_candidate_preflight([&](const auto& table,
                            std::size_t index,const auto&,const auto& target) {
                            return odh::child_shy_sitting_candidate_inventory_preflight(world,table,index,target);
                        });
                        runtime_routing->set_stack_object_slot_table_provider(route_stack_slot_table);
                        runtime_routing->set_stair_goto_relative_validator([&](const odh::VmFrame& frame,
                            const odh::Instruction& instruction) {
                            (void)instruction;
                            const bool direct_portal=headless_portal&&headless_portal_applied&&
                                headless_portal_status=="queued"&&headless_portal_owner&&
                                frame.context.code_owner==*headless_portal_owner;
                            const bool go_here_portal=go_here_status=="queued"&&
                                saved_stair_portal_code_owners.count(frame.context.code_owner)!=0;
                            if((!direct_portal&&!go_here_portal)||frame.key.id!=4112||
                               frame.context.caller!=runtime_avatar_id||
                               (direct_portal&&frame.context.stack_object!=headless_portal->id))return false;
                            const auto* endpoint=world.find(frame.context.stack_object);
                            if(!endpoint||!endpoint->definition)return false;
                            return saved_stair_portal_endpoint_ids.count(endpoint->id)!=0;
                        });
                        runtime_routing->set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint point) {
                            const auto* route_actor=world.find(frame.context.caller);
                            if(!route_actor||!route_actor->position||!render_lot||!runtime_route_grid) {
                                route_goal_validation_diagnostic="route actor, position, lot, or prepared room unavailable";
                                return false;
                            }
                            const auto actor_room=render_lot->runtime_room_at(*route_actor->position);
                            if(!actor_room||*actor_room!=runtime_route_grid->room_id) {
                                route_goal_validation_diagnostic="actor room "+
                                    (actor_room?std::to_string(*actor_room):std::string("unresolved"))+
                                    " differs from prepared route room "+
                                    std::to_string(runtime_route_grid->room_id);
                                return false;
                            }
                            auto goal=*route_actor->position;goal.x=point.x;goal.y=point.y;
                            if(!runtime_route_grid->grid->walkable(point)) {
                                route_goal_validation_diagnostic="candidate lies outside the prepared room grid or inside an obstacle";
                                return false;
                            }
                            route_goal_validation_diagnostic.clear();
                            return true;
                        });
                        if(!runtime_routing->bind(vm))route_runtime_reason="route_controller_vm_binding_failed";
                        else {
                            routing_handler=runtime_routing->primitive();
                            route_runtime_state="ready_dynamic_single_room_grid";
                            route_runtime_reason.clear();
                            route_runtime_start_position=actor->position;
                        }
                    }
                }
            }
        }
    }
    const auto refresh_route_obstacles=[&](bool preserve_local_room=false) -> bool {
        const auto revision=world.spatial_revision();
        if(!runtime_route_grid||!runtime_route_grid->grid||!render_lot||!restored_world||!definition_index){
            route_runtime_reason="runtime_route_refresh_context_unavailable";
            return false;
        }
        const auto* actor=world.find(runtime_avatar_id);
        if(!actor||!actor->position||actor->position->level<1||actor->position->level>2){
            route_runtime_reason="runtime_route_refresh_avatar_position_unavailable";
            return false;
        }
        // A local path was checked against this snapshot's complete obstacle
        // domain. Geometric room labels can change inside its padded bounds;
        // retain that domain until arrival, while still refreshing mutations.
        const auto room=preserve_local_room
            ? std::optional<std::uint16_t>{runtime_route_grid->room_id}
            : render_lot->runtime_room_at(*actor->position);
        if(!room){
            route_runtime_reason="runtime_route_refresh_room_unresolved";
            return false;
        }
        const auto level=static_cast<std::uint8_t>(actor->position->level-1);
        if(*room==runtime_route_grid->room_id&&level==runtime_route_grid->level&&
           route_runtime_spatial_revision&&*route_runtime_spatial_revision==revision)return true;
        const auto probe=build_runtime_attempt_walk_probe(*restored_world,world,*definition_index,
            render_lot->architecture_resource(),*render_lot->prepared_architecture(),*room,
            level,runtime_avatar_id,true,{},resolve_read_only_ep5);
        if(probe.snapshot.state!=odh::AttemptWalkObstacleState::ready){
            route_runtime_reason=std::string("runtime_route_refresh_snapshot_")+
                odh::attempt_walk_obstacle_reason_name(probe.snapshot.reason);
            route_ep5_unknown_caller=probe.snapshot.entry_point5_unknown_caller;
            route_ep5_unknown_stack_object=probe.snapshot.entry_point5_unknown_stack_object;
            route_ep5_unknown_action=probe.snapshot.entry_point5_unknown_action;
            return false;
        }
        auto updated=odh::build_world_room_route_grid(probe.snapshot);
        if(updated.reason==odh::WorldRoomRouteGridReason::route_grid_over_budget){
            // Large outdoor rooms need only a bounded local search domain.
            // Preserve the complete room obstacle inventory and all budgets;
            // candidate goals beyond this 16-tile window remain unavailable.
            constexpr std::int32_t radius=128;
            const auto x=actor->position->x,y=actor->position->y;
            updated=odh::build_world_room_route_grid(probe.snapshot,{},
                odh::WorldRoomRouteGridWindow{{x-radius,y-radius},{x+radius,y+radius}});
        }
        if(updated.state!=odh::WorldRoomRouteGridState::ready||!updated.grid||
           updated.room_id!=*room||updated.level!=level){
            route_runtime_reason=std::string("runtime_route_refresh_grid_")+
                odh::world_room_route_grid_reason_name(updated.reason);
            return false;
        }
        runtime_route_obstacle_snapshot=probe.snapshot;
        runtime_route_static_obstacle_count=probe.roughbalanced_architecture_rectangles+4;
        runtime_route_collision_candidates.clear();
        runtime_route_collision_candidates.reserve(restored_world->records.size());
        bool collision_inventory_complete=true;
        for(const auto& record:restored_world->records) {
            if(record.saved.object_id>static_cast<std::uint16_t>(std::numeric_limits<std::int16_t>::max())) {
                collision_inventory_complete=false;
                break;
            }
            const auto id=static_cast<std::int16_t>(record.saved.object_id);
            runtime_route_collision_candidates.push_back(
                odh::saved_collision_candidate(record,world.find(id)));
        }
        if(!collision_inventory_complete)runtime_route_collision_candidates.clear();
        *runtime_route_grid->grid=std::move(*updated.grid);
        runtime_route_grid->windowed=updated.windowed;
        runtime_route_grid->room_id=updated.room_id;
        runtime_route_grid->level=updated.level;
        runtime_route_grid->obstacle_rectangles=updated.obstacle_rectangles;
        runtime_route_grid->rasterized_cells=updated.rasterized_cells;
        runtime_route_grid->newly_blocked_cells=updated.newly_blocked_cells;
        ++route_runtime_snapshot_refreshes;
        route_runtime_spatial_revision=world.spatial_revision();
        route_runtime_reason.clear();
        return true;
    };
    if(runtime_routing)runtime_routing->set_route_grid_refresh([&](const odh::EntityPosition&) {
        return refresh_route_obstacles();
    });
    odh_host::RoomPortalSession primary_room_portals;
    // All providers capture stable resident state and their own VM. Ticket
    // numbers and resource keys are local to a VM, never household identities.
    const auto configure_room_portals=[&](odh::Routing& routing,odh::Vm& resident_vm,
        std::int16_t actor_id,odh_host::RoomPortalSession& state,
        odh_host::RoomPortalOwnerMap& owners){
        routing.set_room_pair_provider([&](const odh::EntityPosition& actor,
            const odh::EntityPosition& target)
            ->std::optional<std::pair<std::uint16_t,std::uint16_t>> {
            if(!render_lot)return std::nullopt;
            const auto source=render_lot->runtime_room_at(actor);
            const auto destination=render_lot->runtime_room_at(target);
            if(!source||!destination)return std::nullopt;
            return std::make_pair(*source,*destination);
        });
        routing.set_room_portal_context_end_provider([state=&state](std::uint64_t ticket){
            state->contexts.erase(ticket);
        });
        routing.set_room_portal_step_provider([&,resident_vm=&resident_vm,actor_id,state=&state,owners=&owners](std::uint64_t ticket,
            const odh::EntityPosition& actor,const odh::EntityPosition& target,
            const std::optional<odh::VmCall>& previous_call,
            const std::optional<bool>& previous_result,std::size_t portal_step)
            ->odh::RoutePortalStepPlan {
            const auto fail=[&](const std::string& reason){
                state->contexts.erase(ticket);
                state->diagnostic=reason;
                return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::unavailable,{},reason};
            };
            state->prune(*resident_vm);
            const auto* resident=world.find(actor_id);
            if(!resident||resident->kind!=odh::EntityKind::avatar||!resident->position||
               !(*resident->position==actor))
                return fail("room portal planner actor differs from its resident VM");
            if(!ticket||portal_step>32||previous_call.has_value()!=previous_result.has_value())
                return fail("room portal planner received invalid ticket, result, or step budget");
            if(!render_lot||!render_lot->prepared_architecture()||!restored_world||!definition_index||
               actor.level<1||actor.level>2||target.level<1||target.level>2)
                return fail("room portal planner has no prepared live lot context");
            const auto source_room=render_lot->runtime_room_at(actor);
            const auto destination_room=render_lot->runtime_room_at(target);
            if(!source_room||!destination_room)
                return fail("room portal planner could not resolve actor or destination room");
            auto context=state->contexts.find(ticket);
            if(!previous_call){
                if(context!=state->contexts.end()||portal_step!=0)
                    return fail("room portal planner initial context was reused");
                if(state->contexts.size()>=16)
                    return fail("room portal planner context budget exhausted");
                context=state->contexts.emplace(ticket,odh_host::RoomPortalContext{}).first;
            }else{
                if(context==state->contexts.end()||!context->second.pending_edge||
                   !context->second.pending_call)
                    return fail("room portal planner has no matching pending edge");
                const auto& edge=*context->second.pending_edge;
                const auto& expected=*context->second.pending_call;
                const auto* portal=world.find(edge.portal_id);
                if(previous_call->key.scope!=expected.key.scope||previous_call->key.id!=expected.key.id||
                   previous_call->context.caller!=expected.context.caller||
                   previous_call->context.callee!=expected.context.callee||
                   previous_call->context.stack_object!=expected.context.stack_object||
                   previous_call->context.code_owner!=expected.context.code_owner||
                   previous_call->arguments!=expected.arguments||
                   previous_call->return_mode!=odh::VmCallReturnMode::ordinary||!portal||
                   portal->generation!=context->second.portal_generation||!portal->position||
                   !(*portal->position==edge.position))
                    return fail("returned room portal changed its call or live edge identity");
                ++state->returns;
                if(*previous_result){
                    if(*source_room!=edge.target_room)
                        return fail("Portal15 returned true without reaching its planned destination room");
                    ++state->transitions;
                }else{
                    if(context->second.ignored.size()>=32)
                        return fail("room portal ignored-edge budget exhausted");
                    const odh::IgnoredRoomPortal ignored{edge.portal_id,edge.target_room};
                    if(std::none_of(context->second.ignored.begin(),context->second.ignored.end(),[&](const auto& item){
                        return item.portal_id==ignored.portal_id&&item.target_room==ignored.target_room;
                    }))context->second.ignored.push_back(ignored);
                }
                context->second.pending_edge.reset();context->second.pending_call.reset();
            }
            if(*source_room==*destination_room){
                state->contexts.erase(ticket);state->diagnostic.clear();
                return {odh::RoutePortalStepStatus::destination_reached,{},{}};
            }
            try {
                const auto probe=build_runtime_attempt_walk_probe(*restored_world,world,*definition_index,
                    render_lot->architecture_resource(),*render_lot->prepared_architecture(),*source_room,
                    static_cast<std::uint8_t>(actor.level-1),actor_id,true,{},resolve_read_only_ep5);
                if(!probe.portal_inventory_complete||probe.snapshot.state!=odh::AttemptWalkObstacleState::ready)
                    return fail("room portal planner requires a complete live portal/obstacle inventory");
                odh::RoomPortalInventory inventory;inventory.complete=probe.portal_inventory_complete;
                inventory.edges=probe.portal_edges;
                const auto route=odh::find_room_portal_route(inventory,*source_room,*destination_room,
                    actor,target,context->second.ignored);
                if(route.status!=odh::RoomPortalRouteStatus::found||route.portals.empty())
                    return fail(std::string("room portal graph route ")+room_portal_route_status_name(route.status));
                const auto edge=route.portals.front();
                const auto* portal=world.find(edge.portal_id);
                const auto record=std::find_if(restored_world->records.begin(),restored_world->records.end(),[&](const auto& item){
                    return item.saved.object_id==static_cast<std::uint16_t>(edge.portal_id);
                });
                const auto* candidate=portal&&portal->definition?
                    definition_index->unique(portal->definition->guid):nullptr;
                if(!portal||!candidate||record==restored_world->records.end()||!portal->position||
                   !(*portal->position==edge.position)||edge.source_room!=*source_room)
                    return fail("planned room portal has no matching live saved resource identity");
                if(!globals||globals->standalone)
                    return fail("room portal requires an explicit Global.iff FAR resource");
                const auto function=saved_portal_function(*candidate);
                const auto owner_key=std::make_tuple(candidate->source,candidate->asset,function.action);
                auto owner=owners->find(owner_key);
                if(owner==owners->end()&&saved_owner_ids_exhausted)
                    return fail("room portal code-owner identity space exhausted");
                const auto code_owner=owner==owners->end()?next_saved_code_owner:owner->second;
                if(actor_id==runtime_avatar_id&&candidate->asset=="stair.iff"&&saved_stair_portal_endpoint_ids.count(edge.portal_id)!=0)
                    saved_stair_portal_code_owners.insert(code_owner);
                std::optional<SavedSemiglobalCodeBank> semiglobal_bank;
                if(function.semiglobal_name)
                    semiglobal_bank=load_saved_semiglobal_code_bank(globals->archive,*function.semiglobal_name+".iff");
                // Main's default bank was loaded from this same exact resource.
                // The plan verifies the transitive global/semiglobal dependencies.
                const auto global_bank=load_saved_global_code_bank(globals->archive,globals->entry);
                const auto plan=saved_portal_vm_call_plan(*record,world,*definition_index,actor_id,
                    edge.portal_id,code_owner,semiglobal_bank?&*semiglobal_bank:nullptr,&global_bank);
                if(owner==owners->end()){
                    if(!install_saved_portal_vm_call_plan(*resident_vm,plan))
                        return fail("resident VM rejected the planned Portal15 code owner at its safe point");
                    if(next_saved_code_owner==std::numeric_limits<odh::CodeOwner>::max())saved_owner_ids_exhausted=true;
                    else ++next_saved_code_owner;
                    owners->emplace(owner_key,code_owner);
                }
                context->second.pending_edge=edge;context->second.pending_call=plan.call;
                context->second.portal_generation=portal->generation;
                ++state->planned_calls;state->diagnostic.clear();
                return {odh::RoutePortalStepStatus::call_ready,plan.call,{}};
            }catch(const std::exception& error){return fail(error.what());}
        });
    };
    if(runtime_routing)configure_room_portals(*runtime_routing,vm,runtime_avatar_id,
        primary_room_portals,saved_portal_code_owners);
    const auto refresh_household_route=[&](HouseholdRuntime& runtime,bool preserve_local_room=false)->bool {
        if(!runtime.route_grid||!runtime.route_grid->grid||!render_lot||!restored_world||!definition_index){
            runtime.route_reason="runtime_route_refresh_context_unavailable";return false;
        }
        const auto* actor=world.find(runtime.id);
        if(!actor||!actor->position||actor->position->level<1||actor->position->level>2){
            runtime.route_reason="runtime_route_refresh_resident_position_unavailable";return false;
        }
        const auto revision=world.spatial_revision();
        // Match the primary route's checked obstacle domain while a local
        // path crosses geometric labels inside its padded room bounds.
        const auto room=preserve_local_room
            ? std::optional<std::uint16_t>{runtime.route_grid->room_id}
            : render_lot->runtime_room_at(*actor->position);
        if(!room){runtime.route_reason="runtime_route_refresh_resident_room_unresolved";return false;}
        const auto level=static_cast<std::uint8_t>(actor->position->level-1);
        if(*room==runtime.route_grid->room_id&&level==runtime.route_grid->level&&
           runtime.route_spatial_revision&&*runtime.route_spatial_revision==revision)return true;
        const auto probe=build_runtime_attempt_walk_probe(*restored_world,world,*definition_index,
            render_lot->architecture_resource(),*render_lot->prepared_architecture(),*room,level,
            runtime.id,true,{},resolve_read_only_ep5);
        if(probe.snapshot.state!=odh::AttemptWalkObstacleState::ready){
            runtime.route_reason=std::string("runtime_route_refresh_snapshot_")+
                odh::attempt_walk_obstacle_reason_name(probe.snapshot.reason);return false;
        }
        auto updated=odh::build_world_room_route_grid(probe.snapshot);
        if(updated.reason==odh::WorldRoomRouteGridReason::route_grid_over_budget){
            constexpr std::int32_t radius=128;
            const auto x=actor->position->x,y=actor->position->y;
            updated=odh::build_world_room_route_grid(probe.snapshot,{},
                odh::WorldRoomRouteGridWindow{{x-radius,y-radius},{x+radius,y+radius}});
        }
        if(updated.state!=odh::WorldRoomRouteGridState::ready||!updated.grid||
           updated.room_id!=*room||updated.level!=level){
            runtime.route_reason=std::string("runtime_route_refresh_grid_")+
                odh::world_room_route_grid_reason_name(updated.reason);return false;
        }
        constexpr std::size_t household_route_cell_budget=262144;
        std::size_t other_cells=0;
        for(const auto& member:household_runtime)
            if(member.get()!=&runtime&&member->route_grid&&member->route_grid->grid)
                other_cells+=member->route_grid->grid->cells();
        if(other_cells+updated.grid->cells()>household_route_cell_budget){
            runtime.route_reason="household_route_grid_aggregate_cell_budget_exceeded";return false;
        }
        *runtime.route_grid->grid=std::move(*updated.grid);
        runtime.route_grid->windowed=updated.windowed;runtime.route_grid->room_id=updated.room_id;
        runtime.route_grid->level=updated.level;
        runtime.route_grid->obstacle_rectangles=updated.obstacle_rectangles;
        runtime.route_grid->rasterized_cells=updated.rasterized_cells;
        runtime.route_grid->newly_blocked_cells=updated.newly_blocked_cells;
        runtime.route_spatial_revision=world.spatial_revision();
        ++runtime.route_snapshot_refreshes;runtime.route_reason.clear();return true;
    };
    for(auto& member:household_runtime){
        auto& runtime=*member;
        const auto* actor=world.find(runtime.id);
        if(!render_lot||!render_lot->prepared_architecture()||!shared.routing_slots||!actor||
           !actor->position||actor->position->level<1||actor->position->level>2){
            runtime.route_reason="prepared_lot_world_or_global_SLOT_unavailable";continue;
        }
        const auto room=render_lot->runtime_room_at(*actor->position);
        if(!room){runtime.route_reason="household_resident_room_unresolved";continue;}
        const auto probe=build_runtime_attempt_walk_probe(*restored_world,world,*definition_index,
            render_lot->architecture_resource(),*render_lot->prepared_architecture(),*room,
            static_cast<std::uint8_t>(actor->position->level-1),runtime.id,true,{},resolve_read_only_ep5);
        if(probe.snapshot.state!=odh::AttemptWalkObstacleState::ready){
            runtime.route_reason=std::string("attempt_walk_snapshot_")+
                odh::attempt_walk_obstacle_reason_name(probe.snapshot.reason);
            runtime.route_ep5_unknown_caller=probe.snapshot.entry_point5_unknown_caller;
            runtime.route_ep5_unknown_stack_object=probe.snapshot.entry_point5_unknown_stack_object;
            runtime.route_ep5_unknown_action=probe.snapshot.entry_point5_unknown_action;
            continue;
        }
        auto grid=odh::build_world_room_route_grid(probe.snapshot);
        if(grid.reason==odh::WorldRoomRouteGridReason::route_grid_over_budget){
            constexpr std::int32_t radius=128;
            const auto x=actor->position->x,y=actor->position->y;
            grid=odh::build_world_room_route_grid(probe.snapshot,{},
                odh::WorldRoomRouteGridWindow{{x-radius,y-radius},{x+radius,y+radius}});
        }
        if(grid.state!=odh::WorldRoomRouteGridState::ready||!grid.grid){
            runtime.route_reason=std::string("route_grid_")+odh::world_room_route_grid_reason_name(grid.reason);continue;
        }
        constexpr std::size_t household_route_cell_budget=262144;
        std::size_t existing_cells=0;
        for(const auto& other:household_runtime)
            if(other.get()!=&runtime&&other->route_grid&&other->route_grid->grid)
                existing_cells+=other->route_grid->grid->cells();
        if(existing_cells+grid.grid->cells()>household_route_cell_budget){
            runtime.route_reason="household_route_grid_aggregate_cell_budget_exceeded";continue;
        }
        runtime.route_grid.emplace(std::move(grid));
        runtime.route_spatial_revision=world.spatial_revision();
        runtime.routing=std::make_unique<odh::Routing>(world,*runtime.route_grid->grid,*shared.routing_slots);
        runtime.routing->set_ranged_slot_goal_provider([&](const auto& table,std::size_t index,
            const auto& caller,const auto& target){
                const bool no_sitting_target=odh::is_child_shy_sitting_fallback_slot(table,index);
                const auto* slot=odh::slot_of_type(table,3,index);
                if(!slot||!runtime.route_grid||!runtime.route_grid->grid)
                    return std::vector<odh::PreRankedSlotGoal>{};
                const auto target_room=render_lot->runtime_room_at(target);
                // A one-room route grid must never accept a goal from another room,
                // even when the SLOT itself carries IgnoreRooms.
                if(!target_room||*target_room!=runtime.route_grid->room_id)
                    return std::vector<odh::PreRankedSlotGoal>{};
                const auto candidate_in_room=[&](odh::RoutePoint point) {
                    auto candidate=target;candidate.x=point.x;candidate.y=point.y;
                    const auto candidate_room=render_lot->runtime_room_at(candidate);
                    return candidate_room&&*candidate_room==runtime.route_grid->room_id;
                };
                const auto candidate_position_valid=[&](odh::RoutePoint point) {
                    return runtime.route_grid->grid->contains(point)&&
                           runtime.route_grid->grid->walkable(point);
                };
                return odh::rank_reference_range_slot(table,index,caller,target,random,
                    no_sitting_target,candidate_in_room,candidate_position_valid);
            });
        runtime.routing->set_sitting_slot_candidate_preflight([&](const auto& table,std::size_t index,
            const auto&,const auto& target) {
            return odh::child_shy_sitting_candidate_inventory_preflight(world,table,index,target);
        });
        runtime.routing->set_stack_object_slot_table_provider(route_stack_slot_table);
        runtime.routing->set_route_goal_validator([&,resident_id=runtime.id](const odh::VmFrame& frame,
            odh::RoutePoint point){
            const auto* route_actor=world.find(frame.context.caller);
            const auto resident=std::find_if(household_runtime.begin(),household_runtime.end(),
                [&](const auto& item){return item->id==resident_id;});
            if(frame.context.caller!=resident_id||resident==household_runtime.end()||
               !route_actor||!route_actor->position||!render_lot||!(*resident)->route_grid||
               !(*resident)->route_grid->grid)return false;
            const auto actor_room=render_lot->runtime_room_at(*route_actor->position);
            return actor_room&&*actor_room==(*resident)->route_grid->room_id&&
                (*resident)->route_grid->grid->walkable(point);
        });
        runtime.routing->set_route_grid_refresh([&,resident=&runtime](const odh::EntityPosition&){
            return refresh_household_route(*resident);
        });
        if(!runtime.vm||!runtime.routing->bind(*runtime.vm)){
            runtime.route_reason="route_controller_vm_binding_failed";runtime.routing.reset();continue;
        }
        configure_room_portals(*runtime.routing,*runtime.vm,runtime.id,
            runtime.room_portals,runtime.room_portals.code_owners);
        runtime.go_here=std::make_unique<odh_host::HouseholdGoHereSession>(world,runtime.id,
            *runtime.vm,*runtime.controllers,*runtime.routing);
        runtime.route_reason.clear();
    }
    for(const auto& action:headless_resident_go_here){
        const auto resident=std::find_if(household_runtime.begin(),household_runtime.end(),
            [&](const auto& item){return item->id==action.actor;});
        if(resident==household_runtime.end())
            throw odh::FormatError("resident Go Here actor is not an additional household member");
        const auto* actor=world.find(action.actor);
        const odh::RoutePoint goal{static_cast<std::int32_t>(action.x*16+8),
            static_cast<std::int32_t>(action.y*16+8)};
        const odh::EntityPosition goal_position{goal.x,goal.y,action.level,
            actor&&actor->position?actor->position->direction:std::uint8_t{0}};
        const auto actor_room=actor&&actor->position&&render_lot?
            render_lot->runtime_room_at(*actor->position):std::nullopt;
        const auto goal_room=render_lot?render_lot->runtime_room_at(goal_position):std::nullopt;
        if(!actor||!actor->position||!(*resident)->route_grid||!(*resident)->route_grid->grid||
           !(*resident)->routing||!(*resident)->go_here||
           actor->position->level<1||actor->position->level>2||
           action.level<1||action.level>2||
           static_cast<unsigned>((*resident)->route_grid->level)+1U!=actor->position->level||
           !actor_room||!goal_room||*actor_room!=(*resident)->route_grid->room_id||
           (actor->position->level==action.level&&*actor_room==*goal_room&&
               (!(*resident)->route_grid->grid->contains(goal)||
               !(*resident)->route_grid->grid->walkable(goal))))
            throw odh::FormatError("resident Go Here target is outside the known route context "
                "(route_reason="+(*resident)->route_reason+", room="+
                ((*resident)->route_grid?std::to_string((*resident)->route_grid->room_id):"unavailable")+
                ", floor="+((*resident)->route_grid?std::to_string((*resident)->route_grid->level):"unavailable")+
                ", target="+std::to_string(goal.x)+","+std::to_string(goal.y)+")");
    }
    const auto request_go_here=[&](const odh::EntityPosition& destination){
        if(auto* member=selected_household_member()){
            const auto reject=[&](const std::string& reason){
                member->ui_go_here_status="unsupported";
                member->ui_go_here_diagnostic=reason;
                if(member->go_here&&member->go_here->status()!="queued")member->go_here->fail(reason);
            };
            if(member->go_here&&member->go_here->status()=="queued")return;
            if(!saved_world_main||!render_lot||!member->routing||!member->route_grid||
               !member->route_grid->grid||!definition_index||!member->controllers||!member->vm){
                reject("Go Here requires this resident's prepared route grid and controller");return;
            }
            if(member->controllers->interactions().size()!=0){
                reject("Go Here requires an idle interaction queue for the selected resident");return;
            }
            const auto* actor=world.find(member->id);
            if(!actor||actor->kind!=odh::EntityKind::avatar||!actor->position||
               actor->position->level<1||actor->position->level>2||
               destination.level<1||destination.level>2){
                reject("Go Here requires a supported floor for the selected resident and destination");return;
            }
            const auto actor_room=render_lot->runtime_room_at(*actor->position);
            const auto destination_room=render_lot->runtime_room_at(destination);
            if(!actor_room||!destination_room||*actor_room!=member->route_grid->room_id){
                reject("Go Here requires a known current room and destination room for the selected resident");return;
            }
            const odh::RoutePoint goal{destination.x,destination.y};
            if(*destination_room==*actor_room&&!member->route_grid->grid->contains(goal)){
                reject("the selected ground cell is outside this resident's prepared route grid");return;
            }
            const auto* candidate=definition_index->unique(0x7c4);
            if(!candidate){reject("the unique saved Destination object definition 0x7C4 is unavailable");return;}
            if(saved_owner_ids_exhausted){reject("saved object code-owner identity space exhausted");return;}
            odh::RoutineSet routines;
            try{routines=load_saved_action_routines(*candidate,4096);}
            catch(const std::exception& error){reject(error.what());return;}
            const auto key=odh::routine_key(4096);
            if(key.scope!=odh::RoutineScope::private_object||!routines.count(key)){
                reject("Go Here BHAV4096 is absent from the exact Destination resource");return;
            }
            if(!member->go_here||member->go_here->status()!="not_requested")
                member->go_here=std::make_unique<odh_host::HouseholdGoHereSession>(world,member->id,
                    *member->vm,*member->controllers,*member->routing);
            member->ui_go_here_status="not_requested";member->ui_go_here_diagnostic.clear();
            const auto owner=next_saved_code_owner;
            if(next_saved_code_owner==std::numeric_limits<odh::CodeOwner>::max())saved_owner_ids_exhausted=true;
            else ++next_saved_code_owner;
            if(member->go_here->request(destination,owner,std::move(routines),4096))
                member->ui_go_here_status="queued";
            else {
                member->ui_go_here_status=member->go_here->status();
                member->ui_go_here_diagnostic=member->go_here->diagnostic();
            }
            return;
        }
        const auto reject=[&](const std::string& reason){
            go_here_status="unsupported";go_here_diagnostic=reason;
        };
        if(go_here_status=="queued")return;
        if(!saved_world_main||!render_lot||!runtime_routing||!runtime_route_grid||
           !runtime_route_grid->grid||!definition_index){
            reject("Go Here requires the restored saved World and a prepared route grid");return;
        }
        if(queue.size()!=0){reject("Go Here requires an idle interaction queue");return;}
        const auto* actor=world.find(runtime_avatar_id);
        if(!actor||actor->kind!=odh::EntityKind::avatar||!actor->position||
           actor->position->level<1||actor->position->level>2||destination.level<1||destination.level>2){
            reject("Go Here requires a supported floor for the avatar and destination");return;
        }
        const auto actor_room=render_lot->runtime_room_at(*actor->position);
        const auto destination_room=render_lot->runtime_room_at(destination);
        if(!actor_room||!destination_room||*actor_room!=runtime_route_grid->room_id){
            reject("Go Here requires a known current room and destination room");return;
        }
        const odh::RoutePoint goal{destination.x,destination.y};
        if(*destination_room==*actor_room&&!runtime_route_grid->grid->contains(goal)){
            reject("the selected ground cell is outside the prepared route grid");return;
        }
        const auto* candidate=definition_index->unique(0x7c4);
        if(!candidate){reject("the unique saved Destination object definition 0x7C4 is unavailable");return;}
        if(saved_owner_ids_exhausted){reject("saved object code-owner identity space exhausted");return;}
        std::int16_t marker_id=0;
        for(std::int32_t id=32767;id>0;--id)
            if(!world.find(static_cast<std::int16_t>(id))){marker_id=static_cast<std::int16_t>(id);break;}
        if(!marker_id){reject("no free World identity is available for the temporary destination");return;}
        if(!world.create(marker_id,odh::EntityKind::object)||!world.define_type(marker_id,0x7c4)){
            if(world.find(marker_id))world.remove(marker_id);
            reject("the temporary Destination object could not be created");return;
        }
        bool marker_ready=true;
        for(std::int16_t word=0;word<=78;++word){
            if(word==1||word==2||word==3||word==11||word==67)continue;
            const auto value=word==8?static_cast<std::int16_t>(256):std::int16_t{0};
            if(!world.declare_object_word(marker_id,word,value)){marker_ready=false;break;}
        }
        marker_ready=marker_ready&&world.set_position(marker_id,destination)&&
            world.set_static_footprint(marker_id,false)&&world.set_ui_ghost(marker_id,true);
        if(!marker_ready){world.remove(marker_id);reject("temporary Destination initial ObjectData or position was rejected");return;}
        const auto owner_key=std::make_tuple(candidate->source,candidate->asset,std::uint16_t{4096});
        auto owner=saved_code_owners.find(owner_key);
        if(owner==saved_code_owners.end()){
            odh::RoutineSet routines;
            try{routines=load_saved_action_routines(*candidate,4096);}
            catch(const std::exception& error){world.remove(marker_id);reject(error.what());return;}
            const auto key=odh::routine_key(4096);
            if(key.scope!=odh::RoutineScope::private_object||!routines.count(key)){
                world.remove(marker_id);reject("Go Here BHAV4096 is absent from the exact Destination resource");return;
            }
            const auto code_owner=next_saved_code_owner;
            if(!vm.install_owner(code_owner,std::move(routines))){
                world.remove(marker_id);reject("Main VM rejected the saved Destination code owner");return;
            }
            if(next_saved_code_owner==std::numeric_limits<odh::CodeOwner>::max())saved_owner_ids_exhausted=true;
            else ++next_saved_code_owner;
            owner=saved_code_owners.emplace(owner_key,code_owner).first;
        }
        const auto context=odh::VmContext{runtime_avatar_id,marker_id,marker_id,owner->second};
        if(!queue.enqueue(odh::routine_key(4096),context,{0,0,0,0})){
            world.remove(marker_id);reject(queue.diagnostic());return;
        }
        go_here_marker_id=marker_id;go_here_marker_generation=world.find(marker_id)->generation;
        go_here_position=destination;go_here_start_position=actor->position;go_here_previous_completion_uid=
            queue.completion()?std::optional<std::uint64_t>{queue.completion()->uid}:std::nullopt;
        go_here_action=4096;go_here_owner=owner->second;go_here_result.reset();
        go_here_status="queued";go_here_route_status="pending";go_here_diagnostic.clear();
    };
    const auto remove_go_here_marker=[&]{
        if(!go_here_marker_id||!go_here_marker_generation)return;
        const auto* marker=world.find(*go_here_marker_id);
        if(marker&&marker->generation==*go_here_marker_generation)world.remove(*go_here_marker_id);
    };
    snap_placement_validator=[&](std::int16_t actor_id,const odh::EntityPosition& destination){
        const auto* actor=world.find(actor_id);
        if(!actor||actor->kind!=odh::EntityKind::avatar||destination.level<1||destination.level>2||
           destination.direction>7||!runtime_route_grid||!runtime_route_grid->grid||!render_lot||
           !render_lot->runtime_room_map_available()||!refresh_route_obstacles())return false;
        const auto room=render_lot->runtime_room_at(destination);
        if(!room||*room!=runtime_route_grid->room_id||
           static_cast<std::uint8_t>(destination.level-1)!=runtime_route_grid->level)return false;
        return runtime_route_grid->grid->walkable({destination.x,destination.y});
    };
    snap_stair_placement_validator=[&](std::int16_t actor_id,std::int16_t target_id,
                                        const odh::EntityPosition& destination){
        snap_stair_placement_diagnostic.clear();
        const auto unsupported=[&](const std::string& reason){
            snap_stair_placement_diagnostic=reason;return odh::Snap::StairPlacementStatus::unsupported;
        };
        const auto* actor=world.find(actor_id);const auto* target=world.find(target_id);
        if(!actor||actor->kind!=odh::EntityKind::avatar||!actor->position||actor->attachment||
           !target||target->kind!=odh::EntityKind::object||!target->position||target->attachment||
           destination.level<1||destination.level>2||destination.direction>7||
           destination.x!=target->position->x||destination.y!=target->position->y||
           destination.level!=target->position->level||
           std::abs(static_cast<int>(actor->position->level)-static_cast<int>(destination.level))!=1||
           !restored_world||!definition_index||!render_lot||!render_lot->prepared_architecture()||
           !render_lot->runtime_room_map_available())
            return unsupported("stairs actor, target, or architecture identity is incomplete");
        const auto room=render_lot->runtime_room_at(destination);
        if(!room)return unsupported("stairs destination room is unresolved");
        std::vector<std::int16_t> avatars_to_consider;
        auto avatar_cursor=std::int16_t{0};
        while(const auto next=world.next_entity(avatar_cursor,odh::EntityKind::avatar)){
            avatar_cursor=*next;
            if(avatar_cursor==actor_id)continue;
            const auto* other=world.find(avatar_cursor);
            if(!other||!other->position)
                return unsupported("stairs avatar obstacle has no verified position");
            const auto other_room=render_lot->runtime_room_at(*other->position);
            if(!other_room)
                return unsupported("stairs avatar obstacle room is unresolved");
            if(*other_room==*room)avatars_to_consider.push_back(avatar_cursor);
        }
        const auto probe=build_runtime_attempt_walk_probe(*restored_world,world,*definition_index,
            render_lot->architecture_resource(),*render_lot->prepared_architecture(),*room,
            static_cast<std::uint8_t>(destination.level-1),actor_id,true,avatars_to_consider,
            resolve_read_only_ep5,destination);
        if(probe.snapshot.state!=odh::AttemptWalkObstacleState::ready)
            return unsupported(std::string("stairs destination obstacle snapshot is incomplete: ")+
                odh::attempt_walk_obstacle_reason_name(probe.snapshot.reason));
        auto grid=odh::build_world_room_route_grid(probe.snapshot);
        if(grid.state!=odh::WorldRoomRouteGridState::ready||!grid.grid)
            return unsupported("stairs destination walkability grid is incomplete");
        if(!grid.grid->contains({destination.x,destination.y}))
            return unsupported("stairs destination lies outside the prepared walkability grid");
        if(!grid.grid->walkable({destination.x,destination.y})){
            snap_stair_placement_diagnostic="stairs destination tile is blocked";
            return odh::Snap::StairPlacementStatus::blocked;
        }
        snap_stair_placement_diagnostic="stairs destination tile passed the prepared walkability checks";
        return odh::Snap::StairPlacementStatus::accepted;
    };
    bool session_save_requested=false;
    const auto save_live_session=[&]()->std::string {
#ifdef __linux__
        if(!session_context||session_context->game_root.empty()||session_context->resource_paths.empty())
            throw odh::FormatError("saving requires a selected game installation");
        if(second_main_vm)throw odh::FormatError("diagnostic second-Main sessions cannot be saved");
        if(!session_context->single_resident_session&&session_context->household_members.empty())
            throw odh::FormatError("household saving requires the complete integrated FAMI roster");
        if(runtime_routing&&runtime_routing->active())
            throw odh::FormatError("wait until the Sim finishes walking before saving");
        if(go_here_status=="queued")
            throw odh::FormatError("wait until Go Here finishes before saving");
        if(std::any_of(household_runtime.begin(),household_runtime.end(),[](const auto& member){
            return (member->routing&&member->routing->active())||
                (member->go_here&&member->go_here->status()=="queued")||
                (member->controllers&&(member->controllers->interactions().size()!=0||
                    member->controllers->interactions().active()));
        }))
            throw odh::FormatError("wait until every household Sim finishes walking and acting before saving");
        if(queue.size()!=0||queue.active())
            throw odh::FormatError("wait until the primary resident finishes acting before saving");
        if(!saved_portal_code_owners.empty()||std::any_of(household_runtime.begin(),
            household_runtime.end(),[](const auto& member){
                return !member->room_portals.code_owners.empty();
            }))
            throw odh::FormatError("this session has portal actions that experimental saves cannot yet resume safely");
        const auto* save_actor=world.find(runtime_avatar_id);
        if(!save_actor||save_actor->person_data.empty())
            throw odh::FormatError("this session cannot yet be restored: resident posture is unavailable");
        const bool seated=save_actor->person_data[0]==1||save_actor->attachment.has_value();
        if(!session_context->single_resident_session&&seated)
            throw odh::FormatError("household session cannot yet restore seated continuation or attachment");
        if(!session_context->single_resident_session&&std::any_of(household_runtime.begin(),
            household_runtime.end(),[&](const auto& member){
                const auto* resident=world.find(member->id);
                return !resident||resident->person_data.empty()||resident->person_data[0]!=0||
                    resident->attachment.has_value();
            }))
            throw odh::FormatError("household session cannot yet restore a seated or attached additional resident");
        // Installed action banks can outlive a completed interaction. The
        // HouseholdSessionRestore candidate below validates references in every
        // captured VM/controller/Playback, including carry, before any write;
        // unreferenced cached banks do not need to survive a fresh process.
        if(seated&&(!seated_action_target||!seated_action_actor_generation||!seated_action_code||
           !save_actor->attachment||
           save_actor->person_data[0]!=1||save_actor->attachment->container!=seated_action_target->id||
           save_actor->generation!=*seated_action_actor_generation||!*seated_action_code))
            throw odh::FormatError("this seated state has no verified completed Sit action to resume safely");
        if(!seated&&(save_actor->attachment||save_actor->person_data[0]!=0))
            throw odh::FormatError("this session cannot yet be restored: the resident posture is unsupported");
        if(!session_baseline||!lot||!bootstrap_globals)
            throw odh::FormatError("this session cannot yet be restored: original lot baseline is unavailable");
        const char* data_home=std::getenv("XDG_DATA_HOME");
        std::filesystem::path directory;
        if(data_home&&*data_home)directory=data_home;
        else {
            const char* home=std::getenv("HOME");
            if(!home||!*home)throw odh::FormatError("XDG_DATA_HOME or HOME is required to store saves");
            directory=std::filesystem::path(home)/".local"/"share";
        }
        if(!directory.is_absolute())
            throw odh::FormatError("save data directory must be an absolute path");
        directory/="opendollhouse";directory/="saves";
        std::error_code path_error;
        directory=std::filesystem::weakly_canonical(directory,path_error);
        if(path_error)throw odh::FormatError("cannot resolve the user save directory");
        const auto is_within=[](const std::filesystem::path& root,const std::filesystem::path& candidate){
            const auto relative=candidate.lexically_relative(root);
            return relative.empty()||(!relative.is_absolute()&&*relative.begin()!="..");
        };
        const auto game_root=std::filesystem::canonical(session_context->game_root,path_error);
        if(path_error||is_within(game_root,directory))
            throw odh::FormatError("save directory must be outside the selected game installation");
        auto checkout=std::filesystem::current_path(path_error);
        if(!path_error){
            for(;;){
                if(std::filesystem::is_regular_file(checkout/".git")||
                   std::filesystem::is_regular_file(checkout/".git"/"HEAD")){
                    checkout=std::filesystem::canonical(checkout,path_error);
                    if(!path_error&&is_within(checkout,directory))
                        throw odh::FormatError("save directory must be outside the source checkout");
                    break;
                }
                const auto parent=checkout.parent_path();
                if(parent==checkout||parent.empty())break;
                checkout=parent;
            }
        }
        const auto save_path=directory/("mode-"+std::to_string(session_context->neighborhood_mode)+
            "-house-"+std::to_string(session_context->house_number)+
            "-family-"+std::to_string(session_context->family_number)+".odhs");
        odh::SessionBundle bundle;
        bundle.resources=odh_host::identify_session_resources(session_context->game_root,
            session_context->resource_paths,session_context->language_code,session_context->expansion_mask);
        if(!session_context->single_resident_session)bundle.household_roster=capture_household_roster(true);
        bundle.host.neighborhood_mode=session_context->neighborhood_mode;
        bundle.host.house_number=session_context->house_number;
        bundle.host.family_chunk_id=session_context->family_chunk_id;
        bundle.host.family_number=session_context->family_number;
        bundle.host.resident_guid=session_context->resident_guid;
        bundle.host.resident_neighbor_id=session_context->resident_neighbor_id;
        bundle.host.runtime_avatar_id=runtime_avatar_id;
        bundle.host.tick=ticks;
        bundle.host.random_seed=random.seed();
        if(active_family)bundle.host.family_budget=active_family->family.budget;
        if(simulation_clock){
            odh::SessionSimulationState sim;sim.clock=simulation_clock->snapshot();
            for(const auto& a:simulation_motives)sim.avatars.push_back({a.first,a.second.snapshot()});
            bundle.simulation=std::move(sim);
        }
        std::map<std::uint16_t,std::int16_t> globals_to_save;
        for(std::int16_t index=0;index<38;++index){
            std::optional<std::int16_t> value;
            if(simulation_clock)value=simulation_clock->global(index);
            if(!value&&index==9&&family_global)value=family_global;
            if(!value&&bootstrap_globals)value=bootstrap_globals->read(index);
            if(!value&&lot)value=lot->read(index);
            if(value)globals_to_save.emplace(static_cast<std::uint16_t>(index),*value);
        }
        for(const auto& entry:globals_to_save)bundle.host.global_values.push_back({entry.first,entry.second});
        bundle.world=world.snapshot();
        if(relationships)bundle.relationships=relationships->snapshot();
        const auto capture_runtime_entity=[&](std::int16_t id,const odh::Vm& resident_vm,
            const odh::EntityThreadControllers& controllers,const std::optional<odh::Playback>& resident_playback){
            odh::SessionRuntimeEntity entity;entity.id=id;
            entity.vm=resident_vm.snapshot();entity.controllers=controllers.snapshot();
            if(resident_playback){std::string why;
                if(!resident_playback->capture_snapshot(entity.playback,&why))
                    throw odh::FormatError("cannot save animation state: "+why);}
            bundle.runtime_entities.push_back(std::move(entity));
        };
        capture_runtime_entity(runtime_avatar_id,vm,thread_controllers,playback);
        if(second_main_vm&&second_main_thread)
            capture_runtime_entity(static_cast<std::int16_t>(headless_second_main->avatar.id),
                *second_main_vm,*second_main_thread,second_main_playback);
        for(const auto& member:household_runtime)
            capture_runtime_entity(member->id,*member->vm,*member->controllers,member->playback);
        std::sort(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
            [](const auto& a,const auto& b){return a.id<b.id;});
        bundle.scheduler=entity_scheduler.snapshot();
        if(seated){
            const auto& selected=*seated_action_target;
            const auto* target=world.find(selected.id);
            const auto* candidate=definition_index?definition_index->unique(selected.guid):nullptr;
            const auto completion=queue.completion();
            if(!target||target->generation!=selected.generation||target->kind!=odh::EntityKind::object||
               !target->definition||target->definition->guid!=selected.guid||!target->entry_points||
               target->entry_points->size()<=27||!completion||!completion->result||
               !seated_action_code||!candidate||
               candidate->definition.tree_table_id!=selected.tree_table_id)
                throw odh::FormatError("this seated state is missing its exact completed Sit/Stand object binding");
            odh::SessionSeatedContinuation continuation;
            continuation.actor_id=runtime_avatar_id;continuation.actor_generation=*seated_action_actor_generation;
            continuation.target_id=selected.id;continuation.target_generation=selected.generation;
            continuation.target_guid=selected.guid;
            continuation.target_slot=static_cast<std::uint32_t>(save_actor->attachment->slot);
            continuation.action=*seated_action_code;continuation.completion=*completion;
            continuation.source=candidate->source;continuation.asset=candidate->asset;
            continuation.definition_chunk_id=candidate->chunk_id;
            continuation.tree_table_id=candidate->definition.tree_table_id;
            continuation.stand_entry_point=27;
            continuation.stand_condition=(*target->entry_points)[27].condition;
            continuation.stand_action=(*target->entry_points)[27].action;
            const auto game_root=session_context->game_root;
            const auto* definitions=definition_index?&*definition_index:nullptr;
            const auto sit_source=load_session_action_source(*candidate,continuation.action,{},
                game_root,globals,definitions);
            const auto stand_source=load_session_action_source(*candidate,continuation.stand_action,{},
                game_root,globals,definitions);
            if(sit_source.linked_semiglobal!=stand_source.linked_semiglobal)
                throw odh::FormatError("Sit and Stand do not resolve through the same exact GLOB-linked bank");
            continuation.linked_semiglobal=sit_source.linked_semiglobal;
            continuation.sit_routine_fingerprint=odh::session_action_routine_fingerprint(sit_source.routines);
            continuation.stand_routine_fingerprint=odh::session_action_routine_fingerprint(stand_source.routines);
            std::set<odh::CodeOwner> referenced_owners;
            const auto add_owner=[&](odh::CodeOwner owner){
                if(owner!=static_cast<odh::CodeOwner>(runtime_avatar_id))referenced_owners.insert(owner);
            };
            const auto& runtime=bundle.runtime_entities.front();
            for(const auto& frame:runtime.vm.stack)add_owner(frame.context.code_owner);
            if(runtime.vm.pending)add_owner(runtime.vm.pending->context.code_owner);
            for(const auto& pending:runtime.vm.suspended_pending)add_owner(pending.context.code_owner);
            if(runtime.vm.interaction_return)add_owner(runtime.vm.interaction_return->code_owner);
            if(runtime.vm.external_frame_return)add_owner(runtime.vm.external_frame_return->code_owner);
            for(const auto& action:runtime.controllers.interactions.actions)add_owner(action.call.context.code_owner);
            if(runtime.playback.state)add_owner(runtime.playback.state->resource.code_owner);
            if(runtime.playback.carry)add_owner(runtime.playback.carry->code_owner);
            for(const auto owner:referenced_owners){
                const auto found=std::find_if(saved_code_owners.begin(),saved_code_owners.end(),
                    [&](const auto& entry){return entry.second==owner&&std::get<2>(entry.first)==continuation.action&&
                        std::get<0>(entry.first)==continuation.source&&std::get<1>(entry.first)==continuation.asset;});
                if(found==saved_code_owners.end())
                    throw odh::FormatError("seated animation or VM references an unverified dynamic action owner");
                odh::SessionActionCodeOwner binding;binding.owner=owner;binding.source=continuation.source;
                binding.asset=continuation.asset;binding.root_routine=continuation.action;
                binding.routine_fingerprint=continuation.sit_routine_fingerprint;
                continuation.code_owners.push_back(std::move(binding));
            }
            std::sort(continuation.code_owners.begin(),continuation.code_owners.end(),
                [](const auto& a,const auto& b){return a.owner<b.owner;});
            bundle.seated_continuation=std::move(continuation);
        }
        const auto encoded=odh::encode_session_bundle(bundle);
        // Apply the loader's complete supported-profile validation before any
        // filesystem mutation. A previous usable save must survive rejection.
        try{
            if(session_context->single_resident_session){
                auto candidate=odh::SingleResidentSessionRestore::prepare(encoded,bundle.resources,bundle.host,
                    *session_baseline,*lot,*bootstrap_globals,family_global);
            }else{
                auto candidate=odh::HouseholdSessionRestore::prepare(encoded,bundle.resources,bundle.host,
                    bundle.household_roster,*session_baseline,*lot,*bootstrap_globals,family_global);
            }
        }catch(const std::exception& error){
            throw odh::FormatError(std::string("this session cannot yet be restored: ")+error.what());
        }
        std::filesystem::create_directories(directory);
        odh_host::write_session_archive_atomic(save_path,odh::decode_session_archive(encoded));
#ifdef ODH_HAVE_LINUX_SESSION_LOG
        session_log.event("experimental session save written tick="+std::to_string(ticks)+
            " path="+save_path.string());
#endif
        return save_path.string();
#else
        throw odh::FormatError("session saves are currently available only on Linux");
#endif
    };
    std::optional<odh_host::StatusHud> captured_status_hud;
    const auto household_selector_residents=[&] {
        std::vector<odh_host::HouseholdSelectorResident> residents;
        if(!saved_world_main)return residents;
        residents.reserve(household_runtime.size()+1);
        const auto append=[&](std::int16_t runtime_id,std::uint32_t guid) {
            std::string label;
            if(saved_prefixes&&objt) {
                const odh::SavedObjectPrefix* match=nullptr;
                for(const auto& prefix:*saved_prefixes)if(prefix.guid==guid) {
                    if(match){match=nullptr;break;}
                    match=&prefix;
                }
                const auto* record=match?objt->entry_for_type(match->type_id):nullptr;
                if(record&&record->guid==guid&&!record->name.empty()) {
#ifdef ODH_HAVE_LINUX_TEXT_CODEC
                    try { label=odh_host::legacy_text_to_utf8(record->name,game_language,256); }
                    catch(const std::exception&) { label.clear(); }
#endif
                    label.erase(std::remove_if(label.begin(),label.end(),[](unsigned char byte) {
                        return byte<0x20||byte==0x7f;
                    }),label.end());
                }
            }
            if(label.empty())label="Sim "+std::to_string(runtime_id);
            residents.push_back({runtime_id,std::move(label)});
        };
        append(runtime_avatar_id,person.binding->guid);
        for(const auto& member:household_runtime)
            append(member->id,member->data.binding->guid);
        return residents;
    };
    std::vector<std::string> rendered_bound_appearances;
    const auto capture_frame=[&]{
        if(!render_requested||(!render_window&&render_evidence))return;
        if(render_appearances&&body_state&&body_state->body){
            const auto* live_avatar=world.find(runtime_avatar_id);
            const auto current=live_avatar?live_avatar->bound_appearances:std::vector<std::string>{};
            if(current!=rendered_bound_appearances){
                auto scene_config=render_appearances->resolve_body(*body_state->body);
                render_appearances->append_accessories(scene_config,current);
                const SceneCfpResolver provider=[&](const odh::AnimationHeader& selected){
                    auto decoded=animation_catalog->decode(selected);
                    const auto* planes=&decoded->planes;
                    return std::shared_ptr<const odh::CfpPlanes>(std::move(decoded),planes);
                };
                render_renderer=std::make_unique<SceneRenderer>(scene_config,provider);
                rendered_bound_appearances=current;
            }
        }
        if(!render_window&&render_tick&&ticks-resumed_tick!=*render_tick)return;
        const auto sample=playback->sample();if(!sample)return;
        const auto* sample_actor=world.find(sample->actor);
        if(!sample_actor||sample_actor->generation!=sample->actor_generation||sample->actor!=runtime_avatar_id)
            throw odh::FormatError("person frame sample actor identity is stale or outside this host");
        if(saved_world_main){
            const bool stair_anchor_view=headless_main&&render_output&&sample->clip&&
                (sample->clip->name=="a2o-stair-climb"||sample->clip->name=="a2o-stair-descend")&&
                headless_portal&&render_lot;
            const auto graphics=render_lot?(stair_anchor_view?
                render_lot->entity_graphics(sample->actor):render_lot->avatar(sample->actor)):
                render_graphics->entity(world,sample->actor);
            render_position=graphics.tile_position;render_direction=graphics.direction;
            render_camera.center_tile={render_position[0],render_position[1],render_position[2]+0.75f};
            // The stair A2O pose is authored relative to its stair object.
            // For a headless diagnostic, look at that real object anchor while
            // leaving the avatar's World position and animation untouched.
            if(stair_anchor_view){
                const auto anchor=render_lot->entity_graphics(headless_portal->id);
                render_camera.center_tile={anchor.tile_position[0],anchor.tile_position[1],
                                           anchor.tile_position[2]+0.75f};
            }
            // A headless click probe may target an object far from the saved
            // avatar. Focus its diagnostic frame on that object so the same
            // production pick path can select it without changing the World.
            if(headless_window_interaction){
                const auto target=render_lot->entity_graphics(*headless_window_interaction);
                render_camera.center_tile={target.tile_position[0],target.tile_position[1],
                                           target.tile_position[2]+0.75f};
            }
        }
        SceneState scene_state;scene_state.animation=sample->clip.get();scene_state.frame=sample->frame;
        scene_state.hands=sample->hands;scene_state.avatar_direction=render_direction;scene_state.avatar_tile=render_position;
        const bool track_actor_pixels=(headless_main||saved_world_main)&&render_output;
        if(track_actor_pixels)scene_state.owner_id=std::numeric_limits<std::uint16_t>::max();
        else if(render_window||headless_window_interaction)
            scene_state.owner_id=static_cast<std::uint16_t>(runtime_avatar_id);
        odh::RasterOptions scene_options;
        scene_options.record_owner_ids=track_actor_pixels||render_window||headless_window_interaction.has_value();
        auto frame=render_renderer->render(scene_state,render_camera,scene_options);
        std::uint64_t actor_raster_pixels=0;
        if(track_actor_pixels&&frame.image.owner_ids.size()==frame.image.pixels.size())
            actor_raster_pixels=std::count(frame.image.owner_ids.begin(),frame.image.owner_ids.end(),scene_state.owner_id);
        std::vector<SavedLotAvatarLayer> avatar_layers;
        if(render_lot){
            avatar_layers.push_back({std::move(frame.image),
                static_cast<std::uint16_t>(render_window||headless_window_interaction||track_actor_pixels?
                    runtime_avatar_id:0)});
        }
        if((render_window||headless_window_interaction)&&render_lot){
            for(auto& member:household_runtime){
                if(!member->scene_renderer||!member->playback)continue;
                const auto member_sample=member->playback->sample();
                if(!member_sample||!member_sample->clip)continue;
                const auto* live_member=world.find(member->id);
                if(!live_member||live_member->generation!=member_sample->actor_generation||
                   member_sample->actor!=member->id||!live_member->position)continue;
                const auto graphics=render_lot->entity_graphics(member->id);
                SceneState member_state;member_state.animation=member_sample->clip.get();
                member_state.frame=member_sample->frame;member_state.hands=member_sample->hands;
                member_state.avatar_direction=graphics.direction;member_state.avatar_tile=graphics.tile_position;
                member_state.owner_id=static_cast<std::uint16_t>(member->id);
                auto member_options=scene_options;
                member_options.record_owner_ids=render_window||headless_window_interaction.has_value();
                auto member_frame=member->scene_renderer->render(member_state,render_camera,member_options);
                avatar_layers.push_back({std::move(member_frame.image),static_cast<std::uint16_t>(member->id)});
            }
        }
        if(render_lot){
            auto lot=render_lot->render(render_camera,avatar_layers,
                render_window||headless_window_interaction.has_value()||track_actor_pixels,
                !render_hide_lot_objects);
            frame.image=std::move(lot.image);render_lot_evidence=std::move(lot);
        }
        std::uint64_t actor_composited_pixels=0;
        if(track_actor_pixels){
            if(render_lot_evidence){
                const auto& avatar_ids=render_lot_evidence->avatar_pick_ids;
                if(avatar_ids.size()==frame.image.pixels.size())
                    actor_composited_pixels=std::count(avatar_ids.begin(),avatar_ids.end(),
                        static_cast<std::uint16_t>(runtime_avatar_id));
            }else if(frame.image.owner_ids.size()==frame.image.pixels.size())
                actor_composited_pixels=std::count(frame.image.owner_ids.begin(),frame.image.owner_ids.end(),
                    scene_state.owner_id);
        }
        if(saved_world_main&&render_lot){
            odh_host::StatusHud hud;hud.actor=active_household_actor;
            if(simulation_clock)hud.clock=simulation_clock->snapshot();
            if(const auto* active=world.find(active_household_actor);active&&active->motives.size()>=16){
                hud.needs.emplace();
                for(std::size_t i=0;i<odh::ts1_need_count;++i)
                    (*hud.needs)[i]=active->motives[odh::ts1_motive_index(static_cast<odh::Ts1Need>(i))];
                hud.room_score=active->motives[13];
            }
            captured_status_hud=hud;
            if(!render_window)odh_host::draw_status_hud(frame.image,hud);
        }
#ifdef ODH_HAVE_X11
        if(render_window_host){
            (void)interaction_window_title();
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
            if(saved_world_main&&captured_status_hud)
                (void)odh_host::draw_motive_panel(frame.image,*captured_status_hud,game_language,
                    motive_panel_background?&*motive_panel_background:nullptr);
            if(current_overlay_text){
                const auto text_result=odh_host::draw_text_overlay(frame.image,*current_overlay_text,current_overlay_language);
                if(text_result.missing_glyphs)
                    render_window_host->set_title(interaction_window_title()+" | missing font glyphs: "+
                                                  std::to_string(text_result.missing_glyphs));
            }
            draw_interaction_menu(frame.image);
            if(saved_world_main&&captured_status_hud){
                odh_host::draw_household_selector_button(frame.image,game_language);
                if(household_selector_open){
                    const auto residents=household_selector_residents();
                    odh_host::draw_household_selector(frame.image,residents,active_household_actor,game_language);
                }
            }
#endif
            render_window_host->present(frame.image);
        }
#endif
        if(render_output){
            std::ofstream ppm(*render_output,std::ios::binary|std::ios::out);
            if(!ppm)throw odh::FormatError("person frame output cannot be created");
            ppm<<"P6\n"<<frame.image.width<<' '<<frame.image.height<<"\n255\n";
            for(const auto& pixel:frame.image.pixels)for(unsigned channel=0;channel<3;++channel)
                ppm.put(static_cast<char>(pixel[channel]));
            ppm.close();if(!ppm)throw odh::FormatError("person frame output write failed");
        }
        const auto usage=animation_catalog->usage();
        render_evidence=RenderEvidence{sample->clip->name,sample->frame,sample->actor,ticks,
            sample->actor_generation,sample->vm_generation,usage.cfp_samples,render_renderer->vertices(),
            render_renderer->triangles(),render_renderer->texture_bytes(),frame.image.tested_samples,frame.image.shaded_samples,
            actor_raster_pixels,actor_composited_pixels,
            render_lot!=nullptr,render_lot!=nullptr&&!render_hide_lot_objects};
    };
    const auto total_steps=[&]{
        auto steps=startup_steps+vm.steps()-resumed_steps+(second_main_vm?second_main_vm->steps():0);
        for(const auto& member:household_runtime)steps+=member->vm->steps();
        for(const auto& [id,runtime]:runtime_object_threads){(void)id;steps+=runtime->vm->steps();}
        return steps-resumed_household_steps;
    };
    const auto process_pending_create_objects=[&]{
        while(!pending_create_objects.empty()){
            auto pending=std::move(pending_create_objects.front());
            pending_create_objects.pop_front();
            if(!pending.vm||pending.vm->status()!=odh::VmStatus::waiting||!pending.vm->pending()||
               pending.vm->pending()->ticket!=pending.ticket)continue;

            std::optional<std::int16_t> created_id;
            std::optional<odh::CodeOwner> resource_owner;
            bool scheduler_added=false;
            try{
                if(!definition_index)throw odh::FormatError("object definition index is unavailable");
                const auto* candidate=definition_index->unique(pending.request.guid);
                if(!candidate)throw odh::FormatError("CreateObjectInstance GUID is missing or ambiguous");
                if(candidate->definition.master_id!=0)
                    throw odh::FormatError("CreateObjectInstance multitile definitions are outside the current profile");
                if(saved_owner_ids_exhausted)
                    throw odh::FormatError("runtime object code-owner identity space exhausted");

                if(!saved_tree_names.count(candidate->definition.guid)){
                    if(saved_tree_names.size()>=256)
                        throw odh::FormatError("runtime TreeByName definition cache budget exceeded");
                    auto catalog=load_saved_tree_name_catalog(*candidate);
                    if(!catalog.semiglobal_name&&candidate->definition.tree_table_id==0xffff&&
                       candidate->definition.master_id!=0){
                        if(const auto* master=definition_index->unique_master(*candidate))
                            catalog.semiglobal_name=load_saved_tree_name_catalog(*master).semiglobal_name;
                    }
                    saved_tree_names.emplace(candidate->definition.guid,std::move(catalog));
                }

                const Resource object_resource{candidate->source,candidate->asset,candidate->asset.empty()};
                std::optional<odh::ObjectRuntimeResources> object_resources;
                with_resource(object_resource,[&](const auto& view){
                    object_resources=odh::read_object_runtime_resources(view,*candidate);
                });
                if(!object_resources)throw odh::FormatError("object runtime resources were not loaded");
                if(object_resources->semiglobal_name)
                    throw odh::FormatError("object GLOB-linked runtime banks are outside the current startup profile");
                auto private_tuning=load_saved_action_tuning(*candidate);

                std::int32_t candidate_id=1;
                while(candidate_id<=32767&&world.find(static_cast<std::int16_t>(candidate_id)))++candidate_id;
                if(candidate_id>32767)throw odh::FormatError("runtime object ID range exhausted");
                const auto object_id=static_cast<std::int16_t>(candidate_id);
                const auto materialized=pending.request.position()==odh::CreateObjectPosition::out_of_world?
                    odh::create_object_instance_out_of_world(world,object_id,pending.request,*object_resources):
                    odh::create_object_instance_in_slot0(
                        world,object_id,pending.stack_object,pending.request,*object_resources);
                if(materialized.status!=odh::CreateObjectInstanceStatus::created||!materialized.object_id)
                    throw odh::FormatError("CreateObjectInstance placement was rejected by the World");
                created_id=*materialized.object_id;

                const auto owner=next_saved_code_owner;
                resource_owner=owner;
                if(next_saved_code_owner==std::numeric_limits<odh::CodeOwner>::max())saved_owner_ids_exhausted=true;
                else ++next_saved_code_owner;
                saved_action_resources.emplace(owner,object_resource);
                saved_action_tuning.emplace(owner,std::move(private_tuning));
                auto caller_object_routines=object_resources->private_routines;

                const auto entry_points=object_resources->entry_points;
                auto runtime=std::make_unique<RuntimeObjectThread>();
                runtime->owner=owner;
                runtime->sleep=std::make_unique<odh::Sleep>(world,[&]{++rng_requests;random.next(1);});
                const auto object_sleep=runtime->sleep->primitive();
                const odh::VmPrimitive object_primitive=[&,object_sleep](const auto& frame,
                    const auto& instruction,const auto& temp,const auto& dispatch){
                    if(instruction.opcode==0)return object_sleep(frame,instruction,temp,dispatch);
                    if(instruction.opcode==17)
                        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
                            "runtime object interaction queues are not implemented"};
                    return person_primitive(frame,instruction,temp,dispatch);
                };
                odh::RoutineOwners object_owners{{owner,std::move(object_resources->private_routines)}};
                runtime->vm=std::make_unique<odh::Vm>(shared_program,odh::ShareVmProgramTag{},memory,
                    odh::VmLimits{},object_primitive,std::move(object_owners));
                if(!runtime->sleep->bind(*runtime->vm))
                    throw odh::FormatError("runtime object sleep controller could not bind");
                const auto startup=odh::run_object_startup(*runtime->vm,object_id,owner,entry_points,10000,
                    [&](const std::string& phase,const odh::VmTrace& record){
                        trace_in("object_"+phase)(record);
                    });
                startup_steps+=startup.steps;
                if(!startup.completed)
                    throw odh::FormatError(startup.diagnostic.empty()?
                        "runtime object startup did not complete":startup.diagnostic);
                if(!pending.vm->install_owner(owner,std::move(caller_object_routines)))
                    throw odh::FormatError("CreateObjectInstance code owner could not be installed in the caller VM");

                const auto main=entry_points.entries.size()>1?entry_points.entries[1].action:0;
                if(main>255){
                    const odh::VmContext context{object_id,object_id,0,owner};
                    runtime->vm->start(odh::routine_key(main),context,{0,0,0,0});
                    if(runtime->vm->status()==odh::VmStatus::fault)
                        throw odh::FormatError(runtime->vm->diagnostic());
                    if(!runtime->sleep->join_at(ticks))
                        throw odh::FormatError("runtime object sleep clock could not join the current tick");
                    auto* bound=runtime.get();
                    runtime_object_threads.emplace(object_id,std::move(runtime));
                    if(!entity_scheduler.add(object_id,*bound->vm,
                        [bound](std::uint64_t now){return bound->sleep->tick(now);}, {},
                        [bound]{return bound->sleep->interrupt();}))
                        throw odh::FormatError("runtime object Main could not join the entity scheduler");
                    scheduler_added=true;
                }

                if(!pending.vm->complete(pending.ticket,true,{},odh::VmStackObjectWrite{object_id}))
                    throw odh::FormatError("CreateObjectInstance parent wait became stale before completion");
                last_create_object_diagnostic="created object "+std::to_string(object_id)+
                    " for GUID "+std::to_string(pending.request.guid);
            }catch(const std::exception& error){
                if(scheduler_added&&created_id)entity_scheduler.remove(*created_id);
                if(created_id){runtime_object_threads.erase(*created_id);world.remove(*created_id);}
                if(resource_owner){saved_action_resources.erase(*resource_owner);saved_action_tuning.erase(*resource_owner);}
                last_create_object_diagnostic=error.what();
                (void)pending.vm->complete(pending.ticket,false);
            }
        }
    };
    const auto process_pending_remove_objects=[&]{
        auto attempts=pending_remove_objects.size();
        while(attempts--&&!pending_remove_objects.empty()){
            const auto pending=pending_remove_objects.front();
            pending_remove_objects.pop_front();
            if(pending.defer_until_unreferenced&&any_interaction_references_target(pending.object)){
                pending_remove_objects.push_back(pending);
                continue;
            }
            last_remove_object_diagnostic.clear();
            if(!pending.defer_until_unreferenced&&(!pending.vm||pending.vm->status()!=odh::VmStatus::waiting||
               !pending.vm->pending()||pending.vm->pending()->ticket!=pending.ticket))continue;
            const auto* object=world.find(pending.object);
            bool removed=object&&object->kind==odh::EntityKind::object&&
                object->generation==pending.generation;
            const auto runtime=runtime_object_threads.find(pending.object);
            if(removed&&runtime!=runtime_object_threads.end()){
                if(!entity_scheduler.remove(pending.object)){
                    removed=false;
                    last_remove_object_diagnostic="object VM could not leave the scheduler at its safe point";
                }else runtime_object_threads.erase(runtime);
            }
            if(removed)removed=world.remove(pending.object);
            if(!removed&&last_remove_object_diagnostic.empty())
                last_remove_object_diagnostic="RemoveObjectInstance target identity became stale";
            if(pending.vm&&!pending.vm->complete(pending.ticket,removed)){
                host_failed=true;
                host_diagnostic="RemoveObjectInstance completion ticket became stale";
                return;
            }
            if(removed)last_remove_object_diagnostic="removed object "+std::to_string(pending.object);
        }
    };
    const auto execute=[&](bool scheduled_tick) {
        if(host_failed)return;
        const auto budget=session_policy.entity_budget(total_steps());
        if(scheduled_tick) {
            const auto phase_trace=trace_in(initialize?"main":"root_probe");
            const odh::EntityVmScheduler::Trace scheduled_trace=[&](std::int16_t entity,const odh::VmTrace& record){
                if(headless_second_main&&entity==headless_second_main->avatar.id){
                    trace_in("second_main")(record);
                }else if(entity!=runtime_avatar_id)trace_in("household_main")(record);
                else phase_trace(record);
            };
            if(!entity_scheduler.tick(ticks,session_policy.tick_budget(total_steps(),entity_scheduler.size()),budget,scheduled_trace)) {
                host_failed=true;
                host_diagnostic=entity_scheduler.diagnostic();
            }
            if(!host_failed)process_pending_create_objects();
            if(!host_failed)process_pending_remove_objects();
            return;
        }
        const auto run_initial=[&](odh::Vm& target,odh::EntityThreadControllers& controllers,
                                   const std::string& phase)->bool{
            if(!controllers.observe()){
                host_failed=true;host_diagnostic=phase=="second_main"
                    ?"second avatar queue observation failed":queue.diagnostic();
                return false;
            }
            const auto remaining=session_policy.entity_budget(total_steps());
            target.run(remaining,trace_in(phase));
            if(!controllers.observe()){
                host_failed=true;host_diagnostic=phase=="second_main"
                    ?"second avatar queue observation failed":queue.diagnostic();
                return false;
            }
            if(target.status()==odh::VmStatus::fault){host_failed=true;host_diagnostic=target.diagnostic();return false;}
            return true;
        };
        // The initial dispatch follows the scheduler's ascending entity-ID order.
        std::map<std::int16_t,std::tuple<odh::Vm*,odh::EntityThreadControllers*,std::string>> initial;
        initial.emplace(runtime_avatar_id,std::make_tuple(&vm,&thread_controllers,initialize?"main":"root_probe"));
        if(second_main_vm)initial.emplace(static_cast<std::int16_t>(headless_second_main->avatar.id),
            std::make_tuple(second_main_vm.get(),second_main_thread.get(),"second_main"));
        for(auto& member:household_runtime)initial.emplace(member->id,
            std::make_tuple(member->vm.get(),member->controllers.get(),"household_main"));
        for(const auto& item:initial)
            if(!run_initial(*std::get<0>(item.second),*std::get<1>(item.second),std::get<2>(item.second)))return;
        if(!host_failed)process_pending_create_objects();
        if(!host_failed)process_pending_remove_objects();
    };
    profile_mark("host-runtime-ready");
#ifdef ODH_HAVE_LINUX_SESSION_LOG
    session_log.event(std::string("runtime session started mode=")+(render_window?"window":"finite_probe")+
        " tick_limit="+std::to_string(session_policy.tick_limit()));
    if(session_context){
        session_log.event("integrated session context mode="+std::to_string(session_context->neighborhood_mode)+
            " house="+std::to_string(session_context->house_number)+
            " family_chunk="+std::to_string(session_context->family_chunk_id)+
            " family_number="+std::to_string(session_context->family_number)+
            " resident_guid="+std::to_string(session_context->resident_guid)+
            " resident_neighbor_id="+std::to_string(session_context->resident_neighbor_id)+
            " language="+std::to_string(session_context->language_code)+
            " expansion_mask="+std::to_string(session_context->expansion_mask)+
            " resource_files="+std::to_string(session_context->resource_paths.size()));
    }
#endif
    if(!restoring_session)execute(false); // Restored Main resumes only at saved tick + 1.
    profile_mark("first-main-execution");
    capture_frame();
    const auto queue_headless_window_interaction=[&]{
        if(!headless_window_interaction||headless_window_menu_empty||headless_window_input_index<headless_window_inputs.size()||
           !render_lot||!render_lot_evidence)return;
        if(!headless_window_object_click_queued){
            const auto& picks=render_lot_evidence->object_pick_ids;
            if(!render_lot_evidence->pick_width||picks.empty())
                throw odh::FormatError("headless window interaction render has no object-pick pixels");
            const auto pixel=std::find(picks.begin(),picks.end(),static_cast<std::uint16_t>(*headless_window_interaction));
            if(pixel==picks.end())throw odh::FormatError("headless window interaction target is not visible in the captured lot frame");
            const auto index=static_cast<std::size_t>(pixel-picks.begin());
            const auto x=static_cast<std::int32_t>(index%render_lot_evidence->pick_width);
            const auto y=static_cast<std::int32_t>(index/render_lot_evidence->pick_width);
            headless_window_object_click_pixel=std::array<std::int32_t,2>{x,y};
            odh_host::WindowInput click;click.action=true;click.primary_click=std::array<std::int32_t,2>{x,y};
            headless_window_inputs.push_back(click);
            headless_window_object_click_queued=true;
            return;
        }
        if(!render_selection||!render_interactions)return;
        if(!headless_window_menu_steps.empty()){
            const auto path=current_pie_menu_path_labels();
            if(headless_window_menu_path_history.empty()||headless_window_menu_path_history.back()!=path)
                headless_window_menu_path_history.push_back(path);
            if(headless_window_menu_step_index>=headless_window_menu_steps.size()){
                headless_window_menu_script_complete=true;
                return;
            }
        }
        if(render_interaction_enqueued)return;
        if(headless_window_menu_click_count>=32)
            throw odh::FormatError("headless pie-menu interaction exceeded 32 selections");
        const auto model=current_interaction_pie_menu();
        const auto* items=active_pie_menu_items(model);
        if(!items||items->empty()){
            headless_window_menu_empty=true;
            render_interaction_requested=true;
            render_interaction_status="no_eligible_action";
            render_interaction_diagnostic="selected object has no eligible visible interaction";
            if(render_window_host)render_window_host->set_title(interaction_window_title());
            return;
        }
        const auto layout=odh::layout_pie_menu(render_camera.width,render_camera.height,
            render_menu_anchor.value_or(headless_window_object_click_pixel.value_or(std::array<std::int32_t,2>{8,8})),
            items->size(),!render_pie_menu_path.empty());
        if(!layout)throw odh::FormatError("headless pie menu has no visible choice");
        odh_host::WindowInput click;
        if(!headless_window_menu_steps.empty()){
            const auto& step=headless_window_menu_steps[headless_window_menu_step_index];
            if(step.kind==HeadlessWindowMenuStep::Kind::back){
                if(render_pie_menu_path.empty()||!layout->back_button)
                    throw odh::FormatError("headless menu script back step has no open category");
                click.primary_click=std::array<std::int32_t,2>{layout->back_button->x+layout->back_button->width/2,
                    layout->back_button->y+layout->back_button->height/2};
            }else{
                if(step.index>=items->size())throw odh::FormatError("headless menu script index is outside the active menu");
                const auto& selected=(*items)[step.index];
                if(step.kind==HeadlessWindowMenuStep::Kind::open_category&&selected.children.empty())
                    throw odh::FormatError("headless menu open step selected an item without a submenu");
                if(step.kind==HeadlessWindowMenuStep::Kind::select_action&&!selected.interaction_row)
                    throw odh::FormatError("headless menu select step selected a category instead of an action");
                const auto& item=layout->items[step.index];
                click.primary_click=std::array<std::int32_t,2>{item.x+item.width/2,item.y+item.height/2};
            }
            ++headless_window_menu_step_index;
        }else{
            const auto& item=layout->items[render_pie_menu_selected];
            click.primary_click=std::array<std::int32_t,2>{item.x+item.width/2,item.y+item.height/2};
        }
        headless_window_inputs.push_back(click);
        ++headless_window_menu_click_count;
    };
    queue_headless_window_interaction();
    const auto vm_active=[](odh::VmStatus state){return state!=odh::VmStatus::returned_true&&
        state!=odh::VmStatus::returned_false&&state!=odh::VmStatus::fault;};
    const auto update_simulation_motives=[&]{
        if(!simulation_clock)return;
        const auto& time=simulation_clock->snapshot();
        const auto motive_contributions=odh::room_score_contributions(world);
        for(auto& entry:simulation_motives){
            const auto* actor=world.find(entry.first);
            odh::Ts1MotiveInputs in;
            if(!actor || actor->person_data.size()<=6 || actor->motives.size()<16 || !actor->object_words.count(34)){
                simulation_motive_status[entry.first]="unsupported_missing_avatar_banks";++simulation_skipped_updates;continue;
            }
            if(!vm_room_map){
                simulation_motive_status[entry.first]="unsupported_room_map_unavailable";++simulation_skipped_updates;continue;
            }
            if(!actor->position){
                simulation_motive_status[entry.first]="unsupported_avatar_position";++simulation_skipped_updates;continue;
            }
            const auto room_score=odh::calculate_room_score(*vm_room_map,motive_contributions,
                actor->position->x,actor->position->y,actor->position->level);
            if(!room_score){
                simulation_motive_status[entry.first]="unsupported_room_score_unresolved";++simulation_skipped_updates;continue;
            }
            if(!world.set_motive(entry.first,13,room_score->score))
                throw odh::FormatError("simulation room-score write failed");
            in.active_personality=actor->person_data[3];in.outgoing_personality=actor->person_data[6];
            in.sleep_state=actor->motives[11];in.room_score=room_score->score;in.hidden=actor->object_words.at(34)>0;
            for(std::size_t i=0;i<odh::ts1_need_count;++i){
                const auto motive=static_cast<std::int16_t>(odh::ts1_motive_index(static_cast<odh::Ts1Need>(i)));
                in.needs[i]=actor->motives[static_cast<std::size_t>(motive)];
                in.active_changes[i]=world.has_motive_change(entry.first,motive);
            }
            const auto result=entry.second.update(time.hour,time.minute,in);
            simulation_motive_status[entry.first]=result.status==odh::Ts1MotiveStatus::updated?"updated":
                result.status==odh::Ts1MotiveStatus::unchanged?"unchanged":"unsupported_input_profile";
            if(result.status==odh::Ts1MotiveStatus::unsupported){++simulation_skipped_updates;continue;}
            if(result.status==odh::Ts1MotiveStatus::updated){
                for(std::size_t i=0;i<odh::ts1_need_count;++i)
                    if(!world.set_motive(entry.first,static_cast<std::int16_t>(odh::ts1_motive_index(static_cast<odh::Ts1Need>(i))),result.needs[i]))
                        throw odh::FormatError("simulation motive write failed");
                if(!world.set_motive(entry.first,11,result.sleep_state)||!world.set_motive(entry.first,3,result.mood))
                    throw odh::FormatError("simulation sleep/mood write failed");
            }
        }
#ifdef ODH_HAVE_X11
        if(render_window_host && (ticks==1 || ticks%30==0)){
            std::string title="OpenDollhouse | "+std::to_string(time.hour)+":"+(time.minute<10?"0":"")+std::to_string(time.minute);
            title+=" | Sim "+std::to_string(active_household_actor);
            if(const auto* actor=world.find(active_household_actor);actor&&actor->motives.size()>=16)
                title+=" | Hunger "+std::to_string(actor->motives[7])+" Comfort "+std::to_string(actor->motives[6])+
                    " Hygiene "+std::to_string(actor->motives[8])+" Bladder "+std::to_string(actor->motives[9])+
                    " Energy "+std::to_string(actor->motives[5])+" Fun "+std::to_string(actor->motives[15])+" Social "+std::to_string(actor->motives[14]);
            render_window_host->set_title(title+" | P pause, S save, Escape close");
        }
#endif
    };
    bool window_paused=false,window_closed=false;
    std::optional<odh_host::NeighborhoodTransitionRequest> neighborhood_transition;
    auto window_deadline=std::chrono::steady_clock::now();
    enum class NeighborhoodRequestResult { cancelled, same_lot, transitioned };
    const auto process_neighborhood_request=[&](std::optional<std::uint16_t> selected){
        const auto* actor_before=world.find(runtime_avatar_id);
        const auto actor_generation_before=actor_before?actor_before->generation:0;
        const auto actor_position_before=actor_before?actor_before->position:std::nullopt;
        const auto actor_person_data_before=actor_before?actor_before->person_data:std::vector<std::int16_t>{};
        const auto actor_attachment_before=actor_before?actor_before->attachment:std::nullopt;
        if(actor_before&&!actor_before->person_data.empty())
            neighborhood_resume_posture_before=actor_before->person_data[0];
        if(actor_attachment_before)neighborhood_resume_attachment_before=actor_attachment_before->container;
        const auto relationship_sources_before=relationships?relationships->source_count():0;
        const auto relationship_pairs_before=relationships?relationships->pair_count():0;
        const auto relationship_values_before=relationships?relationships->value_count():0;
        neighborhood_resume_tick_before=ticks;
        neighborhood_resume_vm_steps_before=vm.steps();
        neighborhood_resume_requested=true;
        if(!selected)return NeighborhoodRequestResult::cancelled;
        neighborhood_resume_tick_after=ticks;
        neighborhood_resume_vm_steps_after=vm.steps();
        const auto* actor_after=world.find(runtime_avatar_id);
        if(actor_after&&!actor_after->person_data.empty())
            neighborhood_resume_posture_after=actor_after->person_data[0];
        if(actor_after&&actor_after->attachment)
            neighborhood_resume_attachment_after=actor_after->attachment->container;
        neighborhood_resume_world_unchanged=actor_before&&actor_after&&
            actor_after->generation==actor_generation_before&&actor_after->position==actor_position_before&&
            actor_after->person_data==actor_person_data_before&&
            ((!actor_attachment_before&&!actor_after->attachment)||
             (actor_attachment_before&&actor_after->attachment&&
              actor_attachment_before->container==actor_after->attachment->container&&
              actor_attachment_before->slot==actor_after->attachment->slot&&
              actor_attachment_before->positioned==actor_after->attachment->positioned));
        neighborhood_resume_relationships_unchanged=relationship_sources_before==(relationships?relationships->source_count():0)&&
            relationship_pairs_before==(relationships?relationships->pair_count():0)&&
            relationship_values_before==(relationships?relationships->value_count():0);
        const auto active_house=lot_id.value_or(0);
        neighborhood_resume_same_lot=active_house&&*selected==active_house;
        if(neighborhood_resume_same_lot)return NeighborhoodRequestResult::same_lot;
        const auto save_path=save_live_session();
        neighborhood_transition=odh_host::NeighborhoodTransitionRequest{
            *selected,std::filesystem::path(save_path)};
#ifdef ODH_HAVE_LINUX_SESSION_LOG
        session_log.event("neighborhood transition requested from house="+
            std::to_string(active_house)+" to house="+std::to_string(*selected)+
            " saved_session="+save_path);
#endif
        return NeighborhoodRequestResult::transitioned;
    };
    while(!host_failed) {
#ifdef ODH_HAVE_X11
        if(render_window_host||headless_window_interaction||headless_window_neighborhood_tick){
            odh_host::WindowInput input;
            if(render_window_host)input=render_window_host->poll();
            else if(headless_window_input_index<headless_window_inputs.size())
                input=headless_window_inputs[headless_window_input_index++];
            else if(headless_window_interaction&&!render_evidence)input.redraw=true;
            if(headless_window_stand_tick&&!headless_window_stand_input_sent&&render_selection&&!input.primary_click&&
               ticks>=*headless_window_stand_tick){
                input.stand=true;headless_window_stand_input_sent=true;
            }
            if(headless_window_go_here&&!headless_window_go_here_input_sent&&!input.primary_click&&
               ticks>=headless_window_go_here->tick&&headless_stand_status=="returned_true"){
                if(!headless_window_go_here_click){
                    std::optional<std::array<std::int32_t,2>> pixel;
                    std::optional<SavedGroundSelection> selected_ground;
                    std::uint64_t best_distance=std::numeric_limits<std::uint64_t>::max();
                    if(render_lot&&render_lot_evidence){
                        const auto* actor=world.find(active_household_actor);
                        const auto actor_room=actor&&actor->position?render_lot->runtime_room_at(*actor->position):std::nullopt;
                        const odh::WorldRoomRouteGrid* route_grid=nullptr;
                        if(auto* member=selected_household_member();member&&member->route_grid)
                            route_grid=&*member->route_grid;
                        else if(runtime_route_grid)route_grid=&*runtime_route_grid;
                        const auto width=render_lot_evidence->pick_width;
                        const auto height=render_lot_evidence->pick_height;
                        if(actor_room&&route_grid&&route_grid->grid&&width&&height&&
                           std::uint64_t{width}*height==render_lot_evidence->ground_pick_ids.size()&&
                           render_lot_evidence->avatar_pick_ids.size()==render_lot_evidence->ground_pick_ids.size()&&
                           render_lot_evidence->object_pick_ids.size()==render_lot_evidence->ground_pick_ids.size()){
                            for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x){
                                const auto pixel_index=std::size_t{y}*width+x;
                                if(render_lot_evidence->avatar_pick_ids[pixel_index]||
                                   render_lot_evidence->object_pick_ids[pixel_index])continue;
                                const auto ground=render_lot->pick_ground(*render_lot_evidence,
                                    static_cast<std::int32_t>(x),static_cast<std::int32_t>(y));
                                if(!ground||ground->level!=headless_window_go_here->level||
                                   ground->room!=*actor_room||ground->level!=actor->position->level)continue;
                                const odh::RoutePoint point{static_cast<std::int32_t>(ground->x*16+8),
                                    static_cast<std::int32_t>(ground->y*16+8)};
                                if(!route_grid->grid->contains(point)||!route_grid->grid->walkable(point)||
                                   (actor->position->x==point.x&&actor->position->y==point.y))continue;
                                const auto dx=ground->x>headless_window_go_here->x?
                                    ground->x-headless_window_go_here->x:headless_window_go_here->x-ground->x;
                                const auto dy=ground->y>headless_window_go_here->y?
                                    ground->y-headless_window_go_here->y:headless_window_go_here->y-ground->y;
                                const auto distance=std::uint64_t{dx}+dy;
                                if(distance<best_distance){
                                    best_distance=distance;selected_ground=ground;
                                    pixel=std::array<std::int32_t,2>{static_cast<std::int32_t>(x),
                                        static_cast<std::int32_t>(y)};
                                }
                            }
                        }
                    }
                    if(!pixel){
                        host_failed=true;host_diagnostic="headless window Go Here target is not visible in the lot pick buffer";
                        break;
                    }
                    input.primary_click=*pixel;
                    headless_window_go_here_click=*pixel;
                    headless_window_go_here_ground=selected_ground;
                }else{
                    input.action=true;headless_window_go_here_input_sent=true;
                }
            }
            if(headless_window_neighborhood_tick&&!headless_window_neighborhood_input_sent&&
               !input.primary_click&&ticks>=*headless_window_neighborhood_tick){
                input.neighborhood=true;headless_window_neighborhood_input_sent=true;
            }
            if(input.quit){
                window_closed=true;
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                session_log.event("runtime window closed tick="+std::to_string(ticks));
#endif
                break;
            }
            if(input.save)session_save_requested=true;
            if(input.neighborhood){
                if(neighborhood_resume || headless_window_neighborhood_tick){
                    if(render_window_host)render_window_host->resize(800,600);
                    if(!neighborhood_resume&&headless_window_neighborhood_tick&&lot_id){
                        const auto selected=headless_window_neighborhood_house.value_or(*lot_id);
                        neighborhood_resume=[selected](){return std::optional<std::uint16_t>{selected};};
                    }
                    NeighborhoodRequestResult result;
                    try{
                        result=process_neighborhood_request(
                            neighborhood_resume?neighborhood_resume():std::nullopt);
                    }catch(const std::exception& error){
                        if(render_window_host)render_window_host->set_title(
                            std::string("OpenDollhouse | cannot switch lots: ")+error.what());
                        capture_frame();
                        window_deadline=std::chrono::steady_clock::now();
                        continue;
                    }
                    if(result==NeighborhoodRequestResult::cancelled){
                        window_closed=true;
                        break;
                    }
                    if(render_window_host)render_window_host->resize(render_camera.width,render_camera.height);
                    if(result==NeighborhoodRequestResult::transitioned)break;
                    capture_frame();
                    if(render_window_host)render_window_host->set_title(interaction_window_title());
                    window_deadline=std::chrono::steady_clock::now();
                    continue;
                }
            }
            if(input.pause){
                window_paused=!window_paused;
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                session_log.event(std::string("runtime session ")+(window_paused?"paused":"resumed")+
                    " tick="+std::to_string(ticks));
#endif
            }
            if(input.interrupt){
                if(!render_pie_menu_path.empty()){
                    render_pie_menu_path.pop_back();render_pie_menu_selected=0;
                }else if(auto* member=selected_household_member();member&&member->go_here&&
                   member->go_here->status()=="queued"){
                    member->go_here_cancel_pending=true;
                    if(render_window_host)render_window_host->set_title("OpenDollhouse: cancellation requested for resident "+
                        std::to_string(member->id));
                }else if(go_here_status=="queued"){
                    go_here_cancel_pending=true;
                    go_here_route_status="cancelling";
                    if(render_window_host)render_window_host->set_title("OpenDollhouse Main preview: Go Here cancellation requested; finishing the game's failure action | Escape close");
                }
            }
            bool household_selector_click_consumed=false;
            std::optional<std::int16_t> selector_resident_pick;
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
            if(input.primary_click&&saved_world_main&&render_lot&&render_lot_evidence){
                const auto click_x=(*input.primary_click)[0],click_y=(*input.primary_click)[1];
                if(odh_host::household_selector_button_hit(render_lot_evidence?render_lot_evidence->pick_width:0,
                                                           render_lot_evidence?render_lot_evidence->pick_height:0,
                                                           click_x,click_y)){
                    household_selector_open=!household_selector_open;
                    household_selector_click_consumed=true;
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                    session_log.event(std::string("UI household selector ")+
                        (household_selector_open?"opened":"closed"));
#endif
                }else if(household_selector_open){
                    const auto panel=odh_host::household_selector_panel_rect(render_lot_evidence->pick_width,
                                                                             render_lot_evidence->pick_height);
                    if(click_x>=panel.x&&click_y>=panel.y&&click_x<panel.x+panel.width&&
                       click_y<panel.y+panel.height){
                        const auto residents=household_selector_residents();
                        selector_resident_pick=odh_host::household_selector_pick(residents,
                            render_lot_evidence->pick_width,render_lot_evidence->pick_height,click_x,click_y);
                        household_selector_open=false;
                        household_selector_click_consumed=true;
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                        session_log.event(std::string("UI household selector click x=")+std::to_string(click_x)+
                            " y="+std::to_string(click_y)+" residents="+std::to_string(residents.size())+
                            " selected="+(selector_resident_pick?std::to_string(*selector_resident_pick):"none"));
#endif
                    }else household_selector_open=false;
                }
            }
#endif
            const bool interaction_menu_click_consumed=!household_selector_click_consumed&&input.primary_click&&
                handle_interaction_menu_click((*input.primary_click)[0],(*input.primary_click)[1]);
            if(input.primary_click&&render_lot&&render_lot_evidence&&
               (selector_resident_pick||(!household_selector_click_consumed&&!interaction_menu_click_consumed))){
                const auto click_x=(*input.primary_click)[0],click_y=(*input.primary_click)[1];
                std::optional<std::int16_t> picked_resident=selector_resident_pick;
                const auto& avatar_picks=render_lot_evidence->avatar_pick_ids;
                if(!picked_resident&&click_x>=0&&click_y>=0&&
                   static_cast<std::uint32_t>(click_x)<render_lot_evidence->pick_width&&
                   static_cast<std::uint32_t>(click_y)<render_lot_evidence->pick_height){
                    const auto index=static_cast<std::size_t>(click_y)*render_lot_evidence->pick_width+
                        static_cast<std::size_t>(click_x);
                    if(index<avatar_picks.size()){
                        std::vector<std::int16_t> resident_ids;
                        resident_ids.reserve(household_runtime.size());
                        for(const auto& member:household_runtime)resident_ids.push_back(member->id);
                        picked_resident=odh::resolve_resident_pick(avatar_picks[index],runtime_avatar_id,resident_ids);
                    }
                }
                if(picked_resident){
                    const bool manual_action_pending=(render_interaction_enqueued&&
                        render_interaction_status=="queued")||headless_stand_status=="queued";
                    if(!manual_action_pending)active_household_actor=*picked_resident;
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                    if(selector_resident_pick)
                        session_log.event("UI active Sim changed to "+std::to_string(active_household_actor));
#endif
                    if(manual_action_pending&&*picked_resident!=active_household_actor){
                        if(render_window_host)render_window_host->set_title(
                            "OpenDollhouse: finish the current resident action before changing Sims");
                        continue;
                    }
                    render_ground_selection.reset();
                    prepare_selected_object(std::nullopt);
                    const bool any_interaction_active=queue.size()!=0||std::any_of(household_runtime.begin(),
                        household_runtime.end(),[](const auto& member){return member->controllers&&
                            (member->controllers->interactions().size()!=0||
                             member->controllers->interactions().active());});
                    const bool any_actor_action_active=any_interaction_active||queue.active();
                    if(!headless_window_go_here&&(!render_interaction_enqueued||!any_actor_action_active)){
                        render_interaction_requested=false;render_interaction_enqueued=false;
                        render_interaction_status="not_requested";render_interaction_diagnostic.clear();
                        render_interaction_action.reset();render_interaction_owner.reset();render_interaction_target.reset();
                        render_interaction_result.reset();render_interaction_actor_id.reset();render_interaction_actor_generation.reset();
                    }
                }else{
                    auto object=render_lot->pick(*render_lot_evidence,click_x,click_y);
                    render_ground_selection.reset();
                    if(!object)render_ground_selection=render_lot->pick_ground(*render_lot_evidence,click_x,click_y);
                    prepare_selected_object(std::move(object));
                    if(render_selection)render_menu_anchor=std::array<std::int32_t,2>{click_x,click_y};
                    const bool any_interaction_active=queue.size()!=0||queue.active()||
                        std::any_of(household_runtime.begin(),household_runtime.end(),[](const auto& member){
                            return member->controllers&&(member->controllers->interactions().size()!=0||
                                member->controllers->interactions().active());});
                    if(!headless_window_go_here&&(!render_interaction_enqueued||!any_interaction_active)){
                        render_interaction_requested=false;render_interaction_enqueued=false;
                        render_interaction_status="not_requested";render_interaction_diagnostic.clear();
                        render_interaction_action.reset();render_interaction_owner.reset();render_interaction_target.reset();
                        render_interaction_result.reset();render_interaction_actor_id.reset();render_interaction_actor_generation.reset();
                    }
                }
                ++render_selection_clicks;render_selection_tick=ticks;
                if(render_window_host)render_window_host->set_title(interaction_window_title());
            }
        if(input.interaction_selection_delta&&!input.primary_click&&render_selection&&render_interactions&&
               actor_queue_for(active_household_actor)&&actor_queue_for(active_household_actor)->size()==0&&
               !render_interaction_enqueued){
                const auto* candidate=definition_index?definition_index->unique(render_selection->guid):nullptr;
                std::vector<std::uint16_t> tests;
                if(candidate&&candidate->source==render_interactions->source&&
                   candidate->asset==render_interactions->asset&&render_interactions->table&&
                   render_interactions->table->status==odh::TtabStatus::decoded){
                    const auto count=std::min<std::size_t>(render_interactions->table->interactions.size(),32);
                    tests.reserve(count);
                    for(std::size_t n=0;n<count;++n)tests.push_back(render_interactions->table->interactions[n].test);
                    std::optional<SavedSemiglobalCodeBank> linked_bank;
                    try{linked_bank=load_interaction_semiglobal(*candidate);}
                    catch(const std::exception& error){
                        render_test_results.assign(tests.size(),{SavedTestFunctionResult::State::unsupported,0,0,error.what()});
                    }
                    render_test_results=evaluate_interaction_tests(*candidate,active_household_actor,
                        render_selection->id,tests,linked_bank?&linked_bank->routines:nullptr);
                    const auto model=current_interaction_pie_menu();
                    const auto* items=active_pie_menu_items(model);
                    if(items&&!items->empty()){
                        const auto count=static_cast<std::int64_t>(items->size());
                        auto next=(static_cast<std::int64_t>(render_pie_menu_selected)+
                            input.interaction_selection_delta)%count;
                        if(next<0)next+=count;
                        render_pie_menu_selected=static_cast<std::size_t>(next);
                        const auto& item=(*items)[render_pie_menu_selected];
                        if(item.interaction_row)render_selected_interaction_row=*item.interaction_row;
                    }
                }else render_test_results.clear();
                if(render_window_host)render_window_host->set_title(interaction_window_title());
            }
            if(input.stand&&!input.primary_click&&render_selection){
                request_stand(*render_selection,headless_window_interaction?"headless_window_input":"window_input",ticks);
                if(render_window_host)render_window_host->set_title("OpenDollhouse: Stand "+headless_stand_status+
                    (headless_stand_diagnostic.empty()?"":" | "+headless_stand_diagnostic));
            }
            if(input.action&&!input.primary_click){
                std::string title="OpenDollhouse Main preview: ";
                if(render_ground_selection){
                    const auto& ground=*render_ground_selection;
                    const odh::EntityPosition destination{
                        static_cast<std::int32_t>(ground.x*16+8),
                        static_cast<std::int32_t>(ground.y*16+8),ground.level,0};
                    request_go_here(destination);
                    if(auto* member=selected_household_member();member){
                        const auto& status=member->go_here?member->go_here->status():member->ui_go_here_status;
                        const auto& diagnostic=member->go_here?member->go_here->diagnostic():member->ui_go_here_diagnostic;
                        title+="Go Here "+status;
                        if(!diagnostic.empty())title+=" | "+diagnostic;
                    }else{
                        title+="Go Here "+go_here_status;
                        if(!go_here_diagnostic.empty())title+=" | "+go_here_diagnostic;
                    }
                }else{
                    title+=activate_pie_menu_selection();
                    if(render_interaction_action)title+=" BHAV"+std::to_string(*render_interaction_action);
                    if(!render_interaction_diagnostic.empty())title+=" | "+render_interaction_diagnostic;
                }
                if(render_window_host)render_window_host->set_title(title+" | Backspace cancels Go Here, P pause, arrows rotate, +/- zoom, Escape close");
            }
            if(session_save_requested){
                session_save_requested=false;
                std::string status;
                try{status=save_live_session();}
                catch(const std::exception& error){status=std::string("save failed: ")+error.what();
#ifdef ODH_HAVE_LINUX_SESSION_LOG
                    session_log.event("experimental session save failed tick="+std::to_string(ticks)+
                        " reason="+error.what());
#endif
                }
                if(render_window_host)render_window_host->set_title("OpenDollhouse | "+status+
                    " | S save (experimental), Escape close");
            }
            render_camera.rotation=static_cast<std::uint8_t>((int(render_camera.rotation)+input.rotation%4+4)%4);
            const auto zoom=std::clamp(int(render_camera.zoom)+input.zoom,1,3);
            render_camera.zoom=static_cast<std::uint8_t>(zoom);
            if(input.rotation||input.zoom||input.redraw)capture_frame();
            queue_headless_window_interaction();
            const bool runtime_finished=session_policy.limit_reached(ticks-resumed_tick,total_steps())||
                (!vm_active(vm.status())&&(!second_main_vm||!vm_active(second_main_vm->status()))&&std::none_of(household_runtime.begin(),household_runtime.end(),
                    [&](const auto& member){return vm_active(member->vm->status());}));
            if(runtime_finished&&!render_evidence)
                throw odh::FormatError("person window preview reached runtime limit without a live Playback sample");
            if(!render_window_host&&(runtime_finished||session_policy.limit_reached(ticks-resumed_tick,total_steps())))break;
            if(render_window_host){
                if(window_paused||runtime_finished){
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));window_deadline=std::chrono::steady_clock::now();continue;
                }
                // Preview pacing only; original wall-clock cadence is not established.
                window_deadline+=std::chrono::milliseconds(40);
                std::this_thread::sleep_until(window_deadline);
            }
        }else
#endif
#ifndef ODH_HAVE_X11
        // CLI-driven lot travel uses the same scheduled neighborhood request
        // as the X11 test input, but must not depend on the window backend.
        if(headless_window_neighborhood_tick&&!headless_window_neighborhood_input_sent&&
           ticks>=*headless_window_neighborhood_tick){
            headless_window_neighborhood_input_sent=true;
            const auto selected=headless_window_neighborhood_house.value_or(lot_id.value_or(0));
            const auto result=process_neighborhood_request(
                selected?std::optional<std::uint16_t>{selected}:std::nullopt);
            if(result==NeighborhoodRequestResult::cancelled){
                window_closed=true;
                break;
            }
            if(result==NeighborhoodRequestResult::transitioned)break;
            continue;
        }
#endif
        if(session_policy.limit_reached(ticks-resumed_tick,total_steps())||
           (!vm_active(vm.status())&&(!second_main_vm||!vm_active(second_main_vm->status()))&&std::none_of(household_runtime.begin(),household_runtime.end(),
                    [&](const auto& member){return vm_active(member->vm->status());})))break;
        ++ticks;
        if(simulation_clock){
            if(!simulation_clock->tick(true))throw odh::FormatError("simulation clock exhausted");
        }
        if(headless_interaction&&!headless_interaction_applied&&ticks==scheduled_interaction_tick){
            headless_interaction_applied=true;
            render_interaction_requested=true;
            bool scheduled_actor_valid=true;
            if(headless_interaction_actor){
                const auto resident=std::find_if(household_runtime.begin(),household_runtime.end(),
                    [&](const auto& member){return member->id==*headless_interaction_actor;});
                if(*headless_interaction_actor==runtime_avatar_id||resident==household_runtime.end()){
                    scheduled_actor_valid=false;
                    render_interaction_status="unsupported";
                    render_interaction_diagnostic="scheduled action actor is not an additional member of the loaded household";
                }else{
                    active_household_actor=*headless_interaction_actor;
                }
            }
            if(scheduled_actor_valid){
            // Keep diagnostics attributed to the selected household resident
            // even when the target or its TestFunctions reject the action.
            render_interaction_actor_id=active_household_actor;
            const auto* target=world.find(headless_interaction->id);
            const auto* candidate=target&&target->definition&&definition_index?
                definition_index->unique(target->definition->guid):nullptr;
            if(!target||target->kind!=odh::EntityKind::object||!target->definition){
                render_interaction_status="unsupported";
                render_interaction_diagnostic="scheduled target is not a defined live object";
            }else if(!candidate){
                render_interaction_status="unsupported";
                render_interaction_diagnostic="scheduled target definition is missing or ambiguous";
            }else{
                prepare_selected_object(SavedObjectSelection{target->id,target->definition->guid,
                    target->generation,candidate->definition.tree_table_id});
                request_selected_interaction();
            }
            }
        }
        if(headless_go_here&&!headless_go_here_applied&&ticks==headless_go_here->tick){
            headless_go_here_applied=true;
            const odh::EntityPosition destination{
                static_cast<std::int32_t>(headless_go_here->x*16+8),
                static_cast<std::int32_t>(headless_go_here->y*16+8),
                headless_go_here->level,0};
            if(go_here_after_stand&&(!headless_stand_result||!*headless_stand_result)){
                go_here_status="unsupported";
                go_here_diagnostic="chained Go Here requires the preceding Sit and Stand actions to return true";
            }else request_go_here(destination);
        }
        for(auto& scheduled:headless_resident_go_here){
            if(scheduled.applied||ticks!=scheduled.tick)continue;
            scheduled.applied=true;
            auto resident=std::find_if(household_runtime.begin(),household_runtime.end(),
                [&](const auto& item){return item->id==scheduled.actor;});
            if(resident==household_runtime.end())continue; // rejected during host setup
            const auto reject=[&](const std::string& reason){(*resident)->go_here->fail(reason);};
            const auto* actor=world.find(scheduled.actor);
            const odh::EntityPosition destination{static_cast<std::int32_t>(scheduled.x*16+8),
                static_cast<std::int32_t>(scheduled.y*16+8),scheduled.level,0};
            const odh::RoutePoint goal{destination.x,destination.y};
            const auto actor_room=actor&&actor->position&&render_lot?
                render_lot->runtime_room_at(*actor->position):std::nullopt;
            const auto destination_room=render_lot?render_lot->runtime_room_at(destination):std::nullopt;
            const auto* candidate=definition_index?definition_index->unique(0x7c4):nullptr;
            if(!actor||!actor->position||actor->position->level<1||actor->position->level>2||
               scheduled.level<1||scheduled.level>2||
               !(*resident)->route_grid||!(*resident)->route_grid->grid||
               static_cast<unsigned>((*resident)->route_grid->level)+1U!=actor->position->level||
               !actor_room||!destination_room||*actor_room!=(*resident)->route_grid->room_id||
               (actor->position->level==scheduled.level&&*actor_room==*destination_room&&
                   (!(*resident)->route_grid->grid->contains(goal)||
                   !(*resident)->route_grid->grid->walkable(goal))))
                reject("resident Go Here destination no longer matches its known route context");
            else if(!candidate)reject("saved Destination definition is missing or ambiguous");
            else if(saved_owner_ids_exhausted)reject("saved object code-owner identity space exhausted");
            else try{
                auto routines=load_saved_action_routines(*candidate,4096);
                const auto key=odh::routine_key(4096);
                if(key.scope!=odh::RoutineScope::private_object||!routines.count(key))
                    reject("saved Destination ActionFunction is absent from its exact private owner");
                else {
                    const auto owner=next_saved_code_owner;
                    if((*resident)->go_here->request(destination,owner,std::move(routines),4096)){
                        if(next_saved_code_owner==std::numeric_limits<odh::CodeOwner>::max())saved_owner_ids_exhausted=true;
                        else ++next_saved_code_owner;
                    }
                }
            }catch(const std::exception& error){reject(error.what());}
        }
        if(headless_portal&&!headless_portal_applied&&ticks==headless_portal->tick){
            headless_portal_applied=true;
            const auto fail_portal=[&](const std::string& reason){
                headless_portal_status="unsupported";headless_portal_diagnostic=reason;
            };
            if(const auto* actor=world.find(runtime_avatar_id);actor&&actor->position)
                headless_portal_actor_start_position=*actor->position;
            const auto* target=world.find(headless_portal->id);
            if(target&&target->position)headless_portal_start_position=*target->position;
            const auto record=std::find_if(restored_world->records.begin(),restored_world->records.end(),[&](const auto& item){
                return item.saved.object_id==static_cast<std::uint16_t>(headless_portal->id);
            });
            const auto* candidate=target&&target->definition&&definition_index?
                definition_index->unique(target->definition->guid):nullptr;
            if(!target||target->kind!=odh::EntityKind::object||!target->definition||!candidate||
               record==restored_world->records.end())fail_portal("scheduled portal identity or saved resource is missing/ambiguous");
            else if(queue.size()!=0)fail_portal("Portal15 probe requires an empty interaction queue");
            else if(!globals||globals->standalone)fail_portal("explicit Global.iff FAR resource is unavailable");
            else if(saved_owner_ids_exhausted)fail_portal("saved object code-owner identity space exhausted");
            else try {
                const auto function=saved_portal_function(*candidate);
                const auto owner_key=std::make_tuple(candidate->source,candidate->asset,function.action);
                auto owner=saved_portal_code_owners.find(owner_key);
                const auto code_owner=owner==saved_portal_code_owners.end()?next_saved_code_owner:owner->second;
                std::optional<SavedSemiglobalCodeBank> semiglobal_bank;
                if(function.semiglobal_name)
                    semiglobal_bank=load_saved_semiglobal_code_bank(globals->archive,
                        *function.semiglobal_name+".iff");
                const auto global_bank=load_saved_global_code_bank(globals->archive,globals->entry);
                const auto plan=saved_portal_vm_call_plan(*record,world,*definition_index,
                    runtime_avatar_id,headless_portal->id,code_owner,
                    semiglobal_bank?&*semiglobal_bank:nullptr,&global_bank);
                if(owner==saved_portal_code_owners.end()){
                    if(!install_saved_portal_vm_call_plan(vm,plan))
                        fail_portal("Main VM rejected Portal15 code owner (duplicate, capacity, or unsafe point)");
                    else {
                        if(next_saved_code_owner==std::numeric_limits<odh::CodeOwner>::max())saved_owner_ids_exhausted=true;
                        else ++next_saved_code_owner;
                        owner=saved_portal_code_owners.emplace(owner_key,code_owner).first;
                    }
                }
                if(headless_portal_status!="unsupported"){
                    headless_portal_action=plan.function.action;headless_portal_owner=owner->second;
                    headless_portal_previous_completion=queue.completion()?
                        std::optional<std::uint64_t>{queue.completion()->uid}:std::nullopt;
                    if(!queue.enqueue(plan.call.key,plan.call.context,plan.call.arguments))
                        fail_portal(queue.diagnostic());
                    else {headless_portal_status="queued";headless_portal_diagnostic.clear();}
                }
            } catch(const std::exception& error){fail_portal(error.what());}
        }
        if(headless_stand&&!headless_stand_applied&&ticks==*scheduled_stand_tick){
            if(render_interaction_target&&render_interaction_target->id==headless_stand->id)
                request_stand(*render_interaction_target,"headless_stand_request",ticks);
            else {
                headless_stand_applied=true;stand_trigger="headless_stand_request";
                stand_request_tick=ticks;stand_request_target=headless_stand->id;
                headless_stand_status="unsupported";headless_stand_diagnostic="Stand target differs from the preceding action";
            }
        }
        if(headless_entity_move&&!headless_entity_move_applied&&ticks==headless_entity_move->tick){
            const auto* moving_entity=world.find(headless_entity_move->id);
            if(!moving_entity||!moving_entity->position||
               !world.set_position(headless_entity_move->id,headless_entity_move->position)){
                host_failed=true;
                host_diagnostic="headless scheduled entity move rejected";
                break;
            }
            headless_entity_move_applied=true;
        }
        if(headless_go_here_interrupt_tick&&!headless_go_here_interrupt_applied&&
           ticks>=*headless_go_here_interrupt_tick&&go_here_status=="queued"){
            headless_go_here_interrupt_applied=true;
            go_here_cancel_pending=true;
        }
        if(go_here_cancel_pending&&go_here_status=="queued"&&runtime_routing&&
           runtime_routing->active()&&runtime_routing->interrupt()){
            go_here_cancel_pending=false;
            if(headless_go_here_interrupt_tick)headless_go_here_interrupt_accepted=true;
        }
        if(runtime_routing&&runtime_routing->active()){
            if(!runtime_routing->external_frame_active()&&
               !refresh_route_obstacles(!runtime_routing->waiting_for_room_portal_step())){
                host_failed=true;
                host_diagnostic=route_runtime_reason;
                break;
            }
            const bool route_ok=runtime_routing->tick(ticks,[&](const odh::RouteMovement& movement){
                ++route_runtime_movement_callbacks;
                if(diagnostics.retain(Diagnostic::route_movement,route_runtime_movement_positions.size(),128))
                    route_runtime_movement_positions.push_back(movement.position);
            });
            if(headless_go_here_interrupt_after_movements&&
               !headless_go_here_interrupt_applied&&go_here_status=="queued"&&
               route_runtime_movement_callbacks>=*headless_go_here_interrupt_after_movements){
                headless_go_here_interrupt_applied=true;
                go_here_cancel_pending=true;
            }
            if(go_here_cancel_pending&&go_here_status=="queued"&&runtime_routing->active()&&
               runtime_routing->interrupt()){
                go_here_cancel_pending=false;
                if(headless_go_here_interrupt_tick||headless_go_here_interrupt_after_movements)
                    headless_go_here_interrupt_accepted=true;
            }
            // A local SLOT may cross the geometric room boundary. Rebuild the
            // arrival-room grid before Main can select its next route.
            if(!runtime_routing->active()){
                if(!refresh_route_obstacles()){
                    host_failed=true;host_diagnostic=route_runtime_reason;break;
                }
            }else route_runtime_spatial_revision=world.spatial_revision();
            if(!route_ok&&runtime_routing->active()){
                host_failed=true;
                host_diagnostic=runtime_routing->diagnostic().empty()?
                    "Route45 scheduler rejected a host tick":runtime_routing->diagnostic();
                break;
            }
        }
        for(auto& member:household_runtime){
            auto& runtime=*member;
            if(!runtime.routing||!runtime.routing->active())continue;
            if(runtime.go_here_cancel_pending&&runtime.go_here->status()=="queued"&&
               runtime.routing->interrupt())runtime.go_here_cancel_pending=false;
            if(!runtime.routing->external_frame_active()&&
               !refresh_household_route(runtime,!runtime.routing->waiting_for_room_portal_step())){
                host_failed=true;host_diagnostic=runtime.route_reason;break;
            }
            const bool route_ok=runtime.routing->tick(ticks,[&](const odh::RouteMovement&){
                ++runtime.route_movement_callbacks;
            });
            if(!runtime.routing->active()){
                if(!refresh_household_route(runtime)){
                    host_failed=true;host_diagnostic=runtime.route_reason;break;
                }
            }else runtime.route_spatial_revision=world.spatial_revision();
            if(!route_ok&&runtime.routing->active()){
                host_failed=true;host_diagnostic=runtime.routing->diagnostic().empty()?
                    "household resident routing rejected a host tick":runtime.routing->diagnostic();
                break;
            }
        }
        if(host_failed){
            for(auto& member:household_runtime)
                if(member->go_here)member->go_here->observe(true,host_diagnostic);
            break;
        }
        if(!thread_controllers.advance(ticks)){
            host_failed=true;
            host_diagnostic=!queue.diagnostic().empty()?queue.diagnostic():
                !sleep.diagnostic().empty()?sleep.diagnostic():
                "resident thread controller rejected host tick "+std::to_string(ticks);
            break;
        }
        if(second_main_thread&&!second_main_thread->advance(ticks)){
            host_failed=true;host_diagnostic="second avatar thread controller rejected tick";break;
        }
        const auto record_animation_event=[&](const odh::PlaybackEvent& event){
            std::uint64_t bytes=sizeof(odh::PlaybackEvent);
            for(const auto& pair:event.properties)bytes+=sizeof(pair)+pair.first.size()+pair.second.size();
            if(diagnostics.retain(Diagnostic::animation,animation_events.size(),10000,
                                  bytes,animation_event_bytes,256*1024)){
                animation_events.push_back(event);animation_event_bytes+=bytes;
            }
        };
        if(playback)playback->tick(ticks,record_animation_event);
        if(second_main_playback)second_main_playback->tick(ticks,record_animation_event);
        for(auto& member:household_runtime){
            if(!member->controllers->advance(ticks)){
                host_failed=true;host_diagnostic="household resident controller rejected tick";break;
            }
            if(member->playback)member->playback->tick(ticks,record_animation_event);
        }
        if(host_failed){
            for(auto& member:household_runtime)
                if(member->go_here)member->go_here->observe(true,host_diagnostic);
            break;
        }
        execute(true);
        if(!host_failed){
            if(!world.tick_motive_changes())throw odh::FormatError("continuous motive change tick failed");
            update_simulation_motives();
        }
        for(auto& member:household_runtime)
            if(member->go_here)member->go_here->observe(host_failed,host_diagnostic);
        if(go_here_status=="queued"){
            if(queue.completion()&&(!go_here_previous_completion_uid||
               queue.completion()->uid!=*go_here_previous_completion_uid)){
                go_here_result=queue.completion()->result;
                go_here_status=*go_here_result?"returned_true":"returned_false";
                const auto route_diagnostic=runtime_routing?runtime_routing->diagnostic():std::string{};
                go_here_route_status=route_diagnostic.empty()?"arrived":
                    route_diagnostic=="route interrupted by player"?"cancelled":"failed";
                go_here_diagnostic.clear();
            }else if(vm.status()==odh::VmStatus::fault){
                go_here_status="vm_fault";go_here_diagnostic=vm.diagnostic();
            }else if(host_failed){
                go_here_status="host_failed";go_here_diagnostic=host_diagnostic;
            }
#ifdef ODH_HAVE_X11
            if(render_window_host&&go_here_status!="queued"){
                std::string title="OpenDollhouse Main preview: Go Here ";
                title+=go_here_route_status=="arrived"?"arrived":
                    go_here_route_status=="cancelled"?"cancelled by player":"could not reach the selected ground";
                if(go_here_route_status=="failed"&&runtime_routing&&!runtime_routing->diagnostic().empty())
                    title+=" | "+runtime_routing->diagnostic();
                render_window_host->set_title(title+" | Backspace cancels Go Here, P pause, arrows rotate, +/- zoom, Escape close");
            }
#endif
            if(go_here_status!="queued"){
                go_here_cancel_pending=false;
                remove_go_here_marker();
            }
        }
        if(headless_go_here_applied&&go_here_status!="queued")break;
        if(headless_portal_applied&&headless_portal_status=="queued"&&queue.completion()&&
           (!headless_portal_previous_completion||queue.completion()->uid!=*headless_portal_previous_completion)){
            headless_portal_result=queue.completion()->result;
            headless_portal_status=*headless_portal_result?"returned_true":"returned_false";
            headless_portal_diagnostic.clear();
        }else if(headless_portal_applied&&headless_portal_status=="queued"&&vm.status()==odh::VmStatus::fault){
            headless_portal_status="vm_fault";headless_portal_diagnostic=vm.diagnostic();
            if(!snap_stair_placement_diagnostic.empty())
                headless_portal_diagnostic+=" ("+snap_stair_placement_diagnostic+")";
        }else if(headless_portal_applied&&headless_portal_status=="queued"&&host_failed){
            headless_portal_status="host_failed";headless_portal_diagnostic=host_diagnostic;
        }
        // This mode diagnoses one scheduled Portal15 request. Stop at its
        // terminal result so later Main instructions cannot overwrite the
        // route diagnostic or append unrelated movement to the probe.
        if(headless_portal_applied&&headless_portal_status!="queued")break;
        const auto stand_actor=stand_actor_id.value_or(runtime_avatar_id);
        auto* stand_queue=actor_queue_for(stand_actor);
        auto* stand_vm=actor_vm_for(stand_actor);
        if(headless_stand_applied&&headless_stand_status=="queued"&&stand_queue&&stand_queue->completion()&&
           (!headless_stand_previous_completion||stand_queue->completion()->uid!=*headless_stand_previous_completion)){
            headless_stand_result=stand_queue->completion()->result;
            headless_stand_status=*headless_stand_result?"returned_true":"returned_false";
            if(*headless_stand_result){
                const auto* resident=world.find(stand_actor);
                if(resident&&(!resident->person_data.empty()&&resident->person_data[0]!=1)&&!resident->attachment){
                    const auto [seated_target,seated_generation,seated_code]=actor_seated_binding(stand_actor);
                    if(seated_target&&seated_generation&&seated_code){
                        seated_target->reset();seated_generation->reset();seated_code->reset();}
                }
            }
        }else if(headless_stand_applied&&headless_stand_status=="queued"&&stand_vm&&stand_vm->status()==odh::VmStatus::fault){
            headless_stand_status="vm_fault";headless_stand_diagnostic=stand_vm->diagnostic();
        }else if(headless_stand_applied&&headless_stand_status=="queued"&&host_failed){
            headless_stand_status="host_failed";headless_stand_diagnostic=host_diagnostic;
        }
        if(render_interaction_requested){
            const auto status_before=render_interaction_status;
            const auto action_actor=render_interaction_actor_id.value_or(runtime_avatar_id);
            auto* action_queue=actor_queue_for(action_actor);
            auto* action_vm=actor_vm_for(action_actor);
            if(render_interaction_result){
                // Preserve the action's completion even if the parent Main
                // faults on a later tick after the interaction was removed.
            }else if(host_failed){
                render_interaction_status="host_failed";render_interaction_diagnostic=host_diagnostic;
            }else if(action_queue&&action_queue->completion()&&
                     (!previous_completion_uid||action_queue->completion()->uid!=*previous_completion_uid)){
                render_interaction_result=action_queue->completion()->result;
                render_interaction_status=*render_interaction_result?"returned_true":"returned_false";
                render_interaction_diagnostic.clear();
                const auto* seated_actor=world.find(action_actor);
                if(*render_interaction_result&&render_interaction_target&&render_interaction_actor_generation&&
                   seated_actor&&seated_actor->generation==*render_interaction_actor_generation&&
                   !seated_actor->person_data.empty()&&seated_actor->person_data[0]==1&&seated_actor->attachment&&
                   seated_actor->attachment->container==render_interaction_target->id){
                    const auto [seated_target,seated_generation,seated_code]=actor_seated_binding(action_actor);
                    if(seated_target&&seated_generation&&seated_code&&render_interaction_action){
                        *seated_target=render_interaction_target;
                        *seated_generation=render_interaction_actor_generation;
                        *seated_code=render_interaction_action;
                    }
                }
            }else if(action_vm&&action_vm->status()==odh::VmStatus::fault){
                render_interaction_status="vm_fault";render_interaction_diagnostic=action_vm->diagnostic();
            }
#ifdef ODH_HAVE_X11
            if(render_window_host&&render_interaction_status!=status_before){
                std::string title="OpenDollhouse Main preview: interaction "+render_interaction_status;
                if(render_interaction_action)title+=" BHAV"+std::to_string(*render_interaction_action);
                if(!render_interaction_diagnostic.empty())title+=" | "+render_interaction_diagnostic;
                if(render_selection&&stand_selection_is_current(*render_selection))title+=" | U to stand";
                render_window_host->set_title(title+" | Backspace cancels Go Here, P pause, arrows rotate, +/- zoom, Escape close");
            }
#endif
        }
        capture_frame();
    }
    if(host_failed){
        // Every host-failure exit must finalize queued resident requests before
        // their state is serialized to JSON; some failures break before the
        // in-loop observer runs.
        for(auto& member:household_runtime)
            if(member->go_here)member->go_here->observe(true,host_diagnostic);
    }else{
        const bool exhausted=ticks-resumed_tick>=session_policy.tick_limit();
        for(auto& member:household_runtime){
            if(!member->go_here||member->go_here->status()!="queued")continue;
            const auto terminal=window_closed?"window_closed":exhausted?"tick_limit":"interrupted";
            const auto reason=window_closed?"window closed before resident Go Here completed":
                exhausted?"resident Go Here remained queued when the tick budget ended":
                          "host loop stopped before resident Go Here completed";
            member->go_here->finish_incomplete(terminal,reason);
        }
    }
    if(go_here_status=="queued"){
        if(host_failed){
            go_here_status="host_failed";
            go_here_route_status="host_failed";
            go_here_diagnostic=host_diagnostic;
        }else if(window_closed){
            go_here_status="window_closed";
            go_here_route_status="interrupted";
            go_here_diagnostic="window closed before Go Here completed";
        }else if(vm.status()==odh::VmStatus::fault){
            go_here_status="vm_fault";
            go_here_route_status="host_failed";
            go_here_diagnostic=vm.diagnostic();
        }else if(ticks-resumed_tick>=session_policy.tick_limit()){
            go_here_status="tick_limit";
            go_here_route_status="incomplete";
            go_here_diagnostic="Go Here remained queued when the tick budget ended";
        }else{
            go_here_status="interrupted";
            go_here_route_status="incomplete";
            go_here_diagnostic="host loop stopped before Go Here completed";
        }
        remove_go_here_marker();
    }
    if(!saved_world_main) room_info_probe_unavailable="requires_saved_world_main";
    else if(!render_lot) room_info_probe_unavailable="requires_loaded_lot_renderer";
    else if(!render_lot->runtime_room_map_available()) room_info_probe_unavailable="prepared_runtime_room_map_unavailable";
    else {
        const auto* actor=world.find(runtime_avatar_id);
        if(!actor || !actor->position) room_info_probe_unavailable="selected_avatar_position_unavailable";
        else {
            const auto room=render_lot->runtime_room_at(*actor->position);
            const auto candidate=odh::avatar_candidate_collision_shape(*actor->position);
            if(!room) room_info_probe_unavailable="selected_avatar_room_unresolved";
            else if(candidate.state!=odh::CollisionShapeState::present)
                room_info_probe_unavailable="selected_avatar_candidate_geometry_unsupported";
            else {
                const odh::RuntimeRoomResolver room_for=[&](odh::EntityPosition position) {
                    return render_lot->runtime_room_at(position);
                };
                const odh::AvatarKillTimeoutResolver kill_timeout_for=[&](std::int16_t id,std::uint64_t generation)
                    ->std::optional<std::int32_t> {
                    const auto* entity=world.find(id);
                    if(!entity || entity->generation!=generation || entity->kind!=odh::EntityKind::avatar)
                        return std::nullopt;
                    return entity->kill_timeout;
                };
                room_info_probe=RoomInfoProbe{*room,candidate.box,
                    odh::query_world_room_info(world,*definition_index,room_for,kill_timeout_for,
                                               *room,candidate.box,runtime_avatar_id),
                    odh::snapshot_world_room_info(world,*definition_index,room_for,kill_timeout_for,*room)};
                if(restored_world&&definition_index&&lot_path&&render_lot->prepared_architecture()){
                    auto architecture=saved_architecture_resource(*lot_path);
                    room_info_probe->attempt_walk=build_runtime_attempt_walk_probe(
                        *restored_world,world,*definition_index,architecture,
                        *render_lot->prepared_architecture(),*room,
                        static_cast<std::uint8_t>(actor->position->level-1),runtime_avatar_id,
                        true,{},resolve_read_only_ep5);
                    if(attempt_walk_destination){
                        room_info_probe->attempt_walk_destination=attempt_walk_destination;
                        const auto& snapshot=room_info_probe->attempt_walk->snapshot;
                        if(snapshot.state!=odh::AttemptWalkObstacleState::ready){
                            room_info_probe->attempt_walk_route_state="snapshot_unsupported";
                        }else{
                            const auto goal_room=render_lot->runtime_room_at(odh::EntityPosition{
                                attempt_walk_destination->x,attempt_walk_destination->y,
                                actor->position->level,actor->position->direction});
                            if(!goal_room) room_info_probe->attempt_walk_route_state="goal_room_unknown";
                            else if(*goal_room!=*room){
                                odh::RoomPortalInventory portal_graph;
                                portal_graph.complete=room_info_probe->attempt_walk->portal_inventory_complete;
                                portal_graph.edges=room_info_probe->attempt_walk->portal_edges;
                                room_info_probe->attempt_walk_room_route=odh::find_room_portal_route(
                                    portal_graph,*room,*goal_room,*actor->position,
                                    {attempt_walk_destination->x,attempt_walk_destination->y,
                                     actor->position->level,actor->position->direction});
                                switch(room_info_probe->attempt_walk_room_route->status){
                                case odh::RoomPortalRouteStatus::found:
                                    room_info_probe->attempt_walk_route_state="cross_room_portal_plan_ready";break;
                                case odh::RoomPortalRouteStatus::no_path:
                                    room_info_probe->attempt_walk_route_state="cross_room_no_portal_path";break;
                                case odh::RoomPortalRouteStatus::invalid_input:
                                    room_info_probe->attempt_walk_route_state="cross_room_portal_inventory_unsupported";break;
                                case odh::RoomPortalRouteStatus::budget_exhausted:
                                    room_info_probe->attempt_walk_route_state="cross_room_portal_budget_exhausted";break;
                                }
                            }
                            else{
                                const auto direction=actor->position->direction;
                                const auto half=static_cast<unsigned>(direction/2);
                                const auto heading=static_cast<std::uint8_t>(
                                    (half+((direction%2!=0&&half%2!=0)?1u:0u))%4u);
                                std::vector<odh::RectRouteObstacle> obstacles;
                                obstacles.reserve(snapshot.obstacles.size());
                                for(const auto& obstacle:snapshot.obstacles)
                                    obstacles.push_back({obstacle.x1,obstacle.y1,obstacle.x2,obstacle.y2});
                                room_info_probe->attempt_walk_route=odh::route_rectangles(obstacles,
                                    {actor->position->x,actor->position->y},*attempt_walk_destination,heading);
                                room_info_probe->attempt_walk_route_state=odh::rect_route_status_name(
                                    room_info_probe->attempt_walk_route->status);
                                if(room_info_probe->attempt_walk_route->status==odh::RectRouteStatus::found){
                                    if(room_info_probe->attempt_walk_route->corridor.empty())
                                        room_info_probe->attempt_walk_lines_state="not_applicable_turn_only";
                                    else{
                                        room_info_probe->attempt_walk_lines=odh::optimize_rect_route_lines(
                                            room_info_probe->attempt_walk_route->corridor,*attempt_walk_destination);
                                        room_info_probe->attempt_walk_lines_state=odh::rect_route_status_name(
                                            room_info_probe->attempt_walk_lines->status);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if(placement_candidate){
        if(!saved_world_main)placement_candidate_probe_unavailable="requires_saved_world_main";
        else if(!render_lot||!render_lot->prepared_architecture())placement_candidate_probe_unavailable="requires_prepared_architecture";
        else if(!render_lot->runtime_room_map_available())placement_candidate_probe_unavailable="prepared_runtime_room_map_unavailable";
        else {
            const auto* actor=world.find(runtime_avatar_id);
            const auto* prepared=&*render_lot->prepared_architecture();
            if(!actor||actor->kind!=odh::EntityKind::avatar||!actor->definition)
                placement_candidate_probe_unavailable="selected_avatar_identity_unavailable";
            else {
                PlacementCandidateProbe probe;probe.position=*placement_candidate;
                probe.inside_lot=probe.position.x>=0&&probe.position.y>=0&&
                    static_cast<std::uint32_t>(probe.position.x/16)<prepared->width&&
                    static_cast<std::uint32_t>(probe.position.y/16)<prepared->height;
                if(!probe.inside_lot){
                    probe.floor_reason="candidate_outside_lot_bounds";
                    probe.room_info_reason="candidate_outside_lot_bounds";
                }else{
                    probe.room=render_lot->runtime_room_at(probe.position);
                    const auto shape=odh::avatar_candidate_collision_shape(probe.position);
                    if(shape.state==odh::CollisionShapeState::present)probe.box=shape.box;
                    else probe.room_info_reason="candidate_avatar_geometry_unsupported";

                    const auto floor=odh::prepared_precise_floor(*prepared,probe.position.x,probe.position.y,probe.position.level);
                    if(!floor)probe.floor_reason="precise_floor_unavailable";
                    else {
                        probe.floor=*floor;
                        if(!prepared->floor_identity_unambiguous())probe.floor_reason="floor_identity_ambiguous";
                        else {
                            const auto* definition=definition_index?definition_index->unique(actor->definition->guid):nullptr;
                            const odh::VmFrame frame{odh::routine_key(4096),0,{runtime_avatar_id,runtime_avatar_id,runtime_avatar_id,0},{},{}};
                            // Simitone's PlacementFlags is ObjectData[42], distinct from Flags at [8].
                            const auto live_flags=world.read(frame,3,42);
                            if(!definition)probe.floor_reason="avatar_definition_missing_or_ambiguous";
                            else if(!definition->definition.level_offset)probe.floor_reason="avatar_level_offset_unavailable";
                            else if(!live_flags)probe.floor_reason="live_placement_flags_unavailable";
                            else {
                                probe.level_offset=*definition->definition.level_offset;
                                probe.placement_flags=static_cast<std::uint16_t>(*live_flags);
                                probe.floor_result=odh::floor_change_valid(*floor,static_cast<std::int8_t>(probe.position.level),
                                    *probe.placement_flags,false,*probe.level_offset);
                            }
                        }
                    }
                    if(!probe.room)probe.room_info_reason="candidate_room_unresolved";
                    else if(probe.box){
                        const odh::RuntimeRoomResolver room_for=[&](odh::EntityPosition position){return render_lot->runtime_room_at(position);};
                        const odh::AvatarKillTimeoutResolver kill_timeout_for=[&](std::int16_t id,std::uint64_t generation)
                            ->std::optional<std::int32_t>{
                            const auto* entity=world.find(id);
                            if(!entity||entity->generation!=generation||entity->kind!=odh::EntityKind::avatar)return std::nullopt;
                            return entity->kill_timeout;
                        };
                        probe.query=odh::query_world_room_info(world,*definition_index,room_for,kill_timeout_for,
                            *probe.room, *probe.box,runtime_avatar_id);
                        const auto same_saved_group=[&](std::int16_t actor_id,std::int16_t blocker_id)
                            ->std::optional<bool>{
                            if(!restored_world)return std::nullopt;
                            for(const auto& group:restored_world->multitile_groups){
                                bool actor_member=false,blocker_member=false;
                                for(const auto member:group.members){
                                    actor_member=actor_member||member==actor_id;
                                    blocker_member=blocker_member||member==blocker_id;
                                }
                                if(actor_member&&blocker_member)return true;
                                if(actor_member||blocker_member)return false;
                            }
                            return false;
                        };
                        // A preview has no explicit place-request flags. The reference default
                        // is zero, so AllAvatarsSolid=false; this remains overlap-only.
                        probe.overlap_audit=odh::audit_world_avatar_place_overlaps(
                            world,runtime_avatar_id,*probe.query,false,same_saved_group,resolve_read_only_ep5);
                    }
                    if(probe.room&&*probe.room!=0&&restored_world&&definition_index){
                        std::vector<std::int16_t> other_avatars;
                        bool avatar_inventory_ready=true;
                        for(auto cursor=std::int16_t{0};avatar_inventory_ready;){
                            const auto next=world.next_entity(cursor,odh::EntityKind::avatar);
                            if(!next)break;
                            cursor=*next;
                            if(cursor==runtime_avatar_id)continue;
                            const auto* other=world.find(cursor);
                            if(!other||!other->position){
                                avatar_inventory_ready=false;
                                probe.architecture_walkability_reason="other_avatar_position_unknown";
                                break;
                            }
                            const auto other_room=render_lot->runtime_room_at(*other->position);
                            if(!other_room){
                                avatar_inventory_ready=false;
                                probe.architecture_walkability_reason="other_avatar_room_unknown";
                                break;
                            }
                            if(*other_room==*probe.room)other_avatars.push_back(cursor);
                        }
                        if(!avatar_inventory_ready){
                            probe.architecture_walkability_state="unsupported";
                        }else{
                            probe.architecture_audit=build_runtime_attempt_walk_probe(
                                *restored_world,world,*definition_index,render_lot->architecture_resource(),
                                *prepared,*probe.room,static_cast<std::uint8_t>(probe.position.level-1),
                                runtime_avatar_id,true,other_avatars,resolve_read_only_ep5,probe.position);
                            const auto& snapshot=probe.architecture_audit->snapshot;
                            if(snapshot.state!=odh::AttemptWalkObstacleState::ready){
                                probe.architecture_walkability_state="unsupported";
                                probe.architecture_walkability_reason=odh::attempt_walk_obstacle_reason_name(snapshot.reason);
                            }else{
                                const auto x=static_cast<std::int32_t>(probe.position.x);
                                const auto y=static_cast<std::int32_t>(probe.position.y);
                                const odh::WorldRoomRouteGridWindow window{{x-128,y-128},{x+128,y+128}};
                                const auto grid=odh::build_world_room_route_grid(snapshot,{},window);
                                if(grid.state!=odh::WorldRoomRouteGridState::ready||!grid.grid){
                                    probe.architecture_walkability_state="unsupported";
                                    probe.architecture_walkability_reason=odh::world_room_route_grid_reason_name(grid.reason);
                                }else if(!grid.grid->contains({x,y})){
                                    probe.architecture_walkability_state="candidate_outside_grid";
                                    probe.architecture_walkability_reason="candidate_cell_outside_bounded_grid";
                                }else{
                                    probe.architecture_walkable=grid.grid->walkable({x,y});
                                    probe.architecture_walkability_state=*probe.architecture_walkable?"clear":"blocked";
                                    probe.architecture_walkability_reason=*probe.architecture_walkable?
                                        "candidate_cell_walkable":"candidate_cell_blocked";
                                }
                            }

                            // A destination-cell probe uses an actor-position override. It
                            // must not be reused as the route's starting snapshot: EntryPoint 5
                            // can depend on where the actor is standing. Build a second,
                            // read-only snapshot from the actor's actual position.
                            if(!actor->position){
                                probe.route_audit_state="unsupported";
                                probe.route_audit_reason="actor_position_unavailable";
                            }else{
                                probe.route_audit_start=odh::RoutePoint{actor->position->x,actor->position->y};
                                probe.route_audit_goal=odh::RoutePoint{probe.position.x,probe.position.y};
                            }
                            if(actor->position&&actor->position->level!=probe.position.level){
                                probe.route_audit_state="not_evaluated";
                                probe.route_audit_reason="different_floor_requires_stair_transition";
                            }else if(actor->position){
                                const auto start_room=render_lot->runtime_room_at(*actor->position);
                                if(!start_room){
                                    probe.route_audit_state="unsupported";
                                    probe.route_audit_reason="actor_start_room_unresolved";
                                }else if(*start_room==0){
                                    probe.route_audit_state="unsupported";
                                    probe.route_audit_reason="actor_start_outside_routable_room";
                                }else if(*start_room!=*probe.room){
                                    probe.route_audit_state="not_evaluated";
                                    probe.route_audit_reason="different_room_requires_portal_transition";
                                }else{
                                    const auto start_probe=build_runtime_attempt_walk_probe(
                                        *restored_world,world,*definition_index,render_lot->architecture_resource(),
                                        *prepared,*start_room,static_cast<std::uint8_t>(actor->position->level-1),
                                        runtime_avatar_id,true,other_avatars,resolve_read_only_ep5);
                                    const auto& start_snapshot=start_probe.snapshot;
                                    if(start_snapshot.state!=odh::AttemptWalkObstacleState::ready){
                                        probe.route_audit_state="unsupported";
                                        probe.route_audit_reason=odh::attempt_walk_obstacle_reason_name(start_snapshot.reason);
                                    }else{
                                        auto route_grid=odh::build_world_room_route_grid(start_snapshot);
                                        if(route_grid.reason==odh::WorldRoomRouteGridReason::route_grid_over_budget){
                                            const auto start_x=static_cast<std::int32_t>(actor->position->x);
                                            const auto start_y=static_cast<std::int32_t>(actor->position->y);
                                            const auto goal_x=static_cast<std::int32_t>(probe.position.x);
                                            const auto goal_y=static_cast<std::int32_t>(probe.position.y);
                                            constexpr std::int64_t margin=128;
                                            const auto min_x=std::min<std::int64_t>(start_x,goal_x)-margin;
                                            const auto min_y=std::min<std::int64_t>(start_y,goal_y)-margin;
                                            const auto max_x=std::max<std::int64_t>(start_x,goal_x)+margin+1;
                                            const auto max_y=std::max<std::int64_t>(start_y,goal_y)+margin+1;
                                            const odh::WorldRoomRouteGridWindow route_window{
                                                {static_cast<std::int32_t>(min_x),static_cast<std::int32_t>(min_y)},
                                                {static_cast<std::int32_t>(max_x),static_cast<std::int32_t>(max_y)}};
                                            route_grid=odh::build_world_room_route_grid(start_snapshot,{},route_window);
                                        }
                                        if(route_grid.state!=odh::WorldRoomRouteGridState::ready||!route_grid.grid){
                                            probe.route_audit_state="unsupported";
                                            probe.route_audit_reason=odh::world_room_route_grid_reason_name(route_grid.reason);
                                        }else{
                                            const odh::RoutePoint start{actor->position->x,actor->position->y};
                                            const odh::RoutePoint goal{probe.position.x,probe.position.y};
                                            probe.route_audit_scope=route_grid.windowed?"bounded_window":"complete_room_grid";
                                            if(!route_grid.grid->contains(start)||!route_grid.grid->contains(goal)){
                                                probe.route_audit_state="unsupported";
                                                probe.route_audit_reason="start_or_candidate_outside_route_grid";
                                            }else{
                                                probe.route_audit_path=odh::find_route(*route_grid.grid,start,{goal});
                                                switch(probe.route_audit_path->status){
                                                case odh::RouteStatus::found:probe.route_audit_state="found";break;
                                                case odh::RouteStatus::no_path:
                                                    probe.route_audit_state=route_grid.windowed?"bounded_no_path":"no_path";break;
                                                case odh::RouteStatus::invalid_input:probe.route_audit_state="invalid_input";break;
                                                case odh::RouteStatus::budget_exhausted:probe.route_audit_state="budget_exhausted";break;
                                                }
                                                if(probe.route_audit_state=="found")probe.route_audit_reason="route_found_in_read_only_room_grid";
                                                else if(probe.route_audit_state=="bounded_no_path")probe.route_audit_reason="no_path_inside_bounded_window_only";
                                                else if(probe.route_audit_state=="no_path")probe.route_audit_reason="no_path_in_complete_room_grid";
                                                else probe.route_audit_reason="route_search_did_not_complete";
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }else if(probe.room&&*probe.room==0){
                        probe.architecture_walkability_state="unsupported";
                        probe.architecture_walkability_reason="candidate_room_zero";
                    }
                }
                placement_candidate_probe=std::move(probe);
            }
        }
    }
    if(render_interaction_requested){
        const auto action_actor_id=render_interaction_actor_id.value_or(runtime_avatar_id);
        auto* action_queue=actor_queue_for(action_actor_id);
        auto* action_vm=actor_vm_for(action_actor_id);
        if(render_interaction_result){
            // The interaction result is independent from a later parent-Main fault.
        }else if(host_failed){render_interaction_status="host_failed";render_interaction_diagnostic=host_diagnostic;}
        else if(action_queue&&action_queue->completion()&&(!previous_completion_uid||
                                      action_queue->completion()->uid!=*previous_completion_uid)){
            render_interaction_result=action_queue->completion()->result;
            render_interaction_status=*render_interaction_result?"returned_true":"returned_false";
            render_interaction_diagnostic.clear();
        }else if(action_vm&&action_vm->status()==odh::VmStatus::fault){
            render_interaction_status="vm_fault";render_interaction_diagnostic=action_vm->diagnostic();
        }
    }
    std::optional<std::string> saved_session_path;
    if(!neighborhood_transition&&session_context&&session_context->save_on_exit){
        if(host_failed||vm.status()==odh::VmStatus::fault||vm.status()==odh::VmStatus::budget_exhausted||
           session_policy.instruction_limit_reached(total_steps()))
            throw odh::FormatError("failed or instruction-limited session will not replace the previous save");
        saved_session_path=save_live_session();
    }
    const auto status=vm.status();
    const auto unfinished=[](odh::VmStatus value){return value==odh::VmStatus::waiting||
        value==odh::VmStatus::yielded||value==odh::VmStatus::budget_exhausted;};
    const bool tick_exhausted=!host_failed&&ticks-resumed_tick==session_policy.tick_limit()&&
        (unfinished(status)||(second_main_vm&&unfinished(second_main_vm->status()))||std::any_of(household_runtime.begin(),household_runtime.end(),
            [&](const auto& member){return unfinished(member->vm->status());}));
    const bool action_incomplete=action.has_value() && (!queue.completion() || queue.size()!=0);
    const bool instruction_exhausted=session_policy.instruction_limit_reached(total_steps()) &&
        (unfinished(status)||(second_main_vm&&unfinished(second_main_vm->status()))||std::any_of(household_runtime.begin(),household_runtime.end(),
            [&](const auto& member){return unfinished(member->vm->status());}));
    if(render_requested&&!render_evidence){
        if(render_window&&host_failed)throw odh::FormatError("person window preview host failed before a live Playback sample: "+host_diagnostic);
        const std::string reason=render_window?(window_closed?"person window preview closed before a live Playback sample was available":
                                                              "person window preview ended before a live Playback sample was available"):
            render_tick?"person frame export observed no live Playback sample at requested tick; no frame written":
                        "person frame export observed no live Playback sample; no frame written";
        std::string failure=reason+" (VM status "+std::string(odh::vm_status_name(vm.status()))+
            (vm.diagnostic().empty()?std::string{}:"; "+vm.diagnostic());
        if(latest_trace){
            const auto& last=*latest_trace;
            failure+="; last instruction BHAV"+std::to_string(last.key.id)+" pc"+std::to_string(last.pc)+
                " opcode"+std::to_string(last.instruction.opcode)+" operands[";
            for(std::size_t n=0;n<last.instruction.operands.size();++n){
                if(n)failure+=',';
                failure+=std::to_string(last.instruction.operands[n]);
            }
            failure+=']';
        }
        throw odh::FormatError(failure+")");
    }
    const auto emit_simulation=[&]{
        std::cout<<",\"simulation\":{\"profile\":"<<json_bytes(simulation_profile)<<",\"skipped_updates\":"<<simulation_skipped_updates<<",\"clock\":";
        if(simulation_clock){const auto& c=simulation_clock->snapshot();
            std::cout<<"{\"ticks\":"<<c.ticks<<",\"minute_fraction\":"<<c.minute_fraction<<",\"minute\":"<<unsigned(c.minute)<<",\"hour\":"<<unsigned(c.hour)<<",\"day\":"<<unsigned(c.day)<<",\"month\":"<<unsigned(c.month)<<",\"year\":"<<c.year<<"}";
        }else std::cout<<"null";
        std::cout<<",\"avatars\":[";bool first=true;
        for(const auto& a:simulation_motives){if(!first)std::cout<<',';first=false;const auto* actor=world.find(a.first);
            std::cout<<"{\"id\":"<<a.first<<",\"status\":"<<json_bytes(simulation_motive_status[a.first])<<",\"needs\":[";
            for(std::size_t i=0;i<odh::ts1_need_count;++i){if(i)std::cout<<',';std::cout<<(actor&&actor->motives.size()>=16?actor->motives[odh::ts1_motive_index(static_cast<odh::Ts1Need>(i))]:0);}
            std::cout<<"],\"fractions\":[";for(std::size_t i=0;i<odh::ts1_need_count;++i){if(i)std::cout<<',';std::cout<<a.second.snapshot().fractions[i];}
            std::cout<<"],\"last_two_minute_bucket\":"<<unsigned(a.second.snapshot().last_two_minute_bucket)
                <<",\"sleep_state\":"<<(actor&&actor->motives.size()>=16?actor->motives[11]:0)<<",\"room_score\":"<<(actor&&actor->motives.size()>=16?actor->motives[13]:0)<<'}';
        }std::cout<<"]}";
    };
    std::cout<<"{\"evidence\":"<<json_bytes(restoring_session?"ODH_session_resumed":saved_world_main?"ODH_saved_World_new_Main_entry":"ODH_native_BHAV_constructed_person_host")<<",\"presentation_language\":"<<static_cast<unsigned>(game_language)
        <<",\"session_loaded\":"<<(restoring_session?"true":"false")
        <<",\"resumed_tick\":"<<resumed_tick<<",\"saved_session_path\":"
        <<(saved_session_path?json_bytes(*saved_session_path):"null")
        <<",\"integrated_session_context\":";
    if(session_context){
        std::cout<<"{\"neighborhood_mode\":"<<static_cast<unsigned>(session_context->neighborhood_mode)
            <<",\"house_number\":"<<session_context->house_number
            <<",\"family_chunk_id\":"<<session_context->family_chunk_id
            <<",\"family_number\":"<<session_context->family_number
            <<",\"resident_guid\":"<<session_context->resident_guid
            <<",\"resident_neighbor_id\":"<<session_context->resident_neighbor_id
            <<",\"language_code\":"<<static_cast<unsigned>(session_context->language_code)
            <<",\"expansion_mask\":"<<static_cast<unsigned>(session_context->expansion_mask)
            <<",\"resource_identity_meta_hex\":";
        if(session_context->resources){
            const auto meta=odh::encode_session_resource_identity(*session_context->resources);
            std::cout<<json_bytes(bytes_hex(meta));
        }else std::cout<<"null";
        std::cout<<"}";
    }else std::cout<<"null";
    emit_simulation();
#ifdef ODH_HAVE_LINUX_SESSION_LOG
    std::cout<<",\"session_log\":"<<(session_log.enabled()?json_bytes(session_log.path().string()):"null")
#else
    std::cout<<",\"session_log\":null"
#endif
        <<",\"runtime_policy\":"<<json_bytes(render_window?"window_per_tick_per_entity":"finite_probe_total_budget")
        <<",\"runtime_tick_limit\":"<<session_policy.tick_limit()
        <<",\"runtime_instruction_budget\":"<<session_policy.probe_instructions
        <<",\"window_closed\":"<<(window_closed?"true":"false")
        <<",\"neighborhood_resume_runtime\":";
    if(!neighborhood_resume_requested)std::cout<<"null";
    else std::cout<<"{\"selected_same_lot\":"<<(neighborhood_resume_same_lot?"true":"false")
        <<",\"tick_before\":"<<neighborhood_resume_tick_before<<",\"tick_after\":"<<neighborhood_resume_tick_after
        <<",\"tick_held\":"<<(neighborhood_resume_tick_before==neighborhood_resume_tick_after?"true":"false")
        <<",\"vm_steps_before\":"<<neighborhood_resume_vm_steps_before<<",\"vm_steps_after\":"<<neighborhood_resume_vm_steps_after
        <<",\"vm_held\":"<<(neighborhood_resume_vm_steps_before==neighborhood_resume_vm_steps_after?"true":"false")
        <<",\"world_held\":"<<(neighborhood_resume_world_unchanged?"true":"false")
        <<",\"relationship_counts_held\":"<<(neighborhood_resume_relationships_unchanged?"true":"false")
        <<",\"posture_before\":"<<(neighborhood_resume_posture_before?std::to_string(*neighborhood_resume_posture_before):"null")
        <<",\"posture_after\":"<<(neighborhood_resume_posture_after?std::to_string(*neighborhood_resume_posture_after):"null")
        <<",\"attachment_before\":"<<(neighborhood_resume_attachment_before?std::to_string(*neighborhood_resume_attachment_before):"null")
        <<",\"attachment_after\":"<<(neighborhood_resume_attachment_after?std::to_string(*neighborhood_resume_attachment_after):"null")<<"}";
    std::cout<<",\"diagnostics_truncated\":"<<(diagnostics.truncated()?"true":"false")
        <<",\"diagnostics_dropped\":{";
    for(std::size_t n=0;n<diagnostics.dropped().size();++n){
        if(n)std::cout<<',';
        std::cout<<json_bytes(odh_host::PersonDiagnosticRetention::names[n])<<':'<<diagnostics.dropped()[n];
    }
    std::cout<<'}'
        <<",\"initialization\":"<<json_bytes(restoring_session?
        "ODH session restored before Main dispatch; next logical tick continues the saved VM and controllers":saved_world_main?
        "saved World banks/position restored; new Main entry starts; saved BHAV stacks and queues remain dormant":initialize?
        "ordered Init0/entry8/Main stack/postBodySTR reference subset; appearance resources, special tuning and full native save restoration not initialized":
        "full Init/entry8/body-string pipeline not executed; explicit root may be Init")<<",\"startup_requested\":"<<(initialize?"true":"false")
        <<",\"startup_steps\":"<<startup_steps<<",\"startup_stages\":[";
    for(std::size_t n=0;n<stages.size();++n){if(n)std::cout<<',';const auto& stage=stages[n];
        std::cout<<"{\"phase\":"<<json_bytes(stage.phase)<<",\"routine\":"<<stage.routine<<",\"status\":"<<json_bytes(odh::vm_status_name(stage.status))<<",\"steps\":"<<stage.steps<<",\"diagnostic\":"<<json_bytes(stage.diagnostic)<<'}';}
    std::cout<<"],\"body_initialized\":"<<(body_state?"true":"false")<<",\"body_state\":";
    if(body_state){const auto& state=*body_state;
        std::cout<<"{\"age\":"<<*state.body->age<<",\"gender\":";
        if(state.body->gender)std::cout<<*state.body->gender;else std::cout<<"null";
        std::cout<<",\"avatar_type\":"<<json_bytes(state.body->avatar_type)<<",\"body\":"<<json_bytes(state.body->body)<<",\"head\":"<<json_bytes(state.body->head)
            <<",\"phenotype\":\"TS1 light reference policy\",\"appearance_resources_resolved\":"<<(render_evidence?"true":"false")<<",\"selected_name\":"<<(state.selected_name?json_bytes(*state.selected_name):"null")
            <<",\"seed_before\":"<<(body_seed_before?std::to_string(*body_seed_before):"null")<<",\"seed_after\":"<<(body_seed_after?std::to_string(*body_seed_after):"null")<<",\"walk_table\":"<<state.global_walk_id<<",\"swim_table\":"<<state.global_swim_id<<",\"hands\":[";
        for(std::size_t n=0;n<6;++n){if(n)std::cout<<',';std::cout<<json_bytes(state.body->hands[n]);}
        std::cout<<"],\"walk\":[";for(std::size_t n=0;n<50;++n){if(n)std::cout<<',';std::cout<<json_bytes(state.walk[n]);}
        std::cout<<"],\"swim\":[";for(std::size_t n=0;n<50;++n){if(n)std::cout<<',';std::cout<<json_bytes(state.swim[n]);}std::cout<<"]}";
    }else std::cout<<"null";
    std::cout<<",\"entry_point_table\":";
    if(person.entry_points){const auto& table=*person.entry_points;
        std::cout<<"{\"source\":"<<json_bytes(table.source==odh::EntryPointSource::objd_legacy?"OBJD138 generated33":"OBJf matching OBJD chunk ID")
            <<",\"version\":"<<table.version<<",\"remaining_bytes\":"<<table.remaining_bytes<<",\"entries\":[";
        for(std::size_t n=0;n<table.entries.size();++n){if(n)std::cout<<',';std::cout<<"{\"condition\":"<<table.entries[n].condition<<",\"action\":"<<table.entries[n].action<<'}';}
        std::cout<<']'<<'}';
    }else std::cout<<"null";
    std::cout<<",\"family_activation\":";
    if(active_family){const auto& v=*active_family;
        std::cout<<"{\"chunk_id\":"<<v.chunk_id<<",\"house_number\":"<<v.family.house_number<<",\"family_number\":"<<v.family.family_number
            <<",\"version\":"<<v.family.version<<",\"budget\":"<<v.family.budget<<",\"current_global9\":"<<*family_global
            <<",\"profile\":"<<json_bytes(!materialized_main_states.empty()?
                "CurrentFamily/global9 selected; missing adults constructed before restore with bounded mailbox arrival":saved_world_main?
                "CurrentFamily/global9 selected; restored members compared by exact GUID; no resident construction required":
                "CurrentFamily/global9 selected; no SelectWholeFamily/VerifyFamily or saved resident restoration")<<",\"members\":[";
        for(std::size_t n=0;n<v.family.member_guids.size();++n){if(n)std::cout<<',';std::cout<<v.family.member_guids[n];}std::cout<<"]}";
    }else std::cout<<"null";
    std::cout<<",\"family_world_reconciliation\":";
    if(family_world_reconciliation){
        std::cout<<"{\"evidence\":"<<json_bytes(materialized_main_states.empty()?
            "restored_World_exact_avatar_GUID_match_only":"restored_and_constructed_World_exact_avatar_GUID_match")
            <<",\"creates_or_moves_Sims\":"<<(!materialized_main_states.empty()?"true":"false")<<",\"members\":[";
        for(std::size_t n=0;n<family_world_reconciliation->size();++n){
            if(n)std::cout<<',';
            const auto& member=family_world_reconciliation->at(n);
            const char* presence=member.presence==odh::HouseholdMemberPresence::absent?"absent":
                member.presence==odh::HouseholdMemberPresence::present_without_position?"present_without_position":"present_with_position";
            std::cout<<"{\"guid\":"<<member.guid<<",\"presence\":"<<json_bytes(presence)<<",\"object_id\":";
            if(member.object_id)std::cout<<*member.object_id;else std::cout<<"null";
            std::cout<<",\"position\":";
            if(member.position)std::cout<<"{\"x\":"<<member.position->x<<",\"y\":"<<member.position->y
                <<",\"level\":"<<static_cast<unsigned>(member.position->level)<<",\"direction\":"<<static_cast<unsigned>(member.position->direction)<<'}';
            else std::cout<<"null";
            std::cout<<'}';
        }
        std::cout<<"]}";
    }else std::cout<<"null";
    std::cout<<",\"neighbor_inheritance\":";
    if(inherited){const auto& v=*inherited;
        std::cout<<"{\"neighbor_id\":"<<v.neighbor_id<<",\"guid\":"<<v.guid
            <<",\"current_family\":"<<(v.current_family?std::to_string(*v.current_family):"null")
            <<",\"family_policy\":"<<json_bytes(active_family?"activated FAMI chunk comparison":"no activated FAMI; visitor classification")<<",\"neighbor_family\":"<<v.neighbor_family
            <<",\"bank_words_before\":"<<v.bank_words_before<<",\"bank_words_after\":"<<v.bank_words_after
            <<",\"saved_data_present\":"<<(v.saved_data_present?"true":"false")
            <<",\"retained_gender\":"<<v.retained_gender<<",\"retained_person_type\":"<<v.retained_person_type
            <<",\"retained_visitor_schedule\":"<<v.retained_visitor_schedule
            <<",\"selected_person_type\":"<<v.selected_person_type<<",\"ghost\":"<<v.ghost<<'}';
    }else std::cout<<"null";
    std::cout<<",\"neighbor_binding_policy\":"<<json_bytes(!initialize?"not requested for explicit root probe":
        !neighbors?"neighborhood not loaded":host_failed?"startup or inheritance failed":
        inherited?"GUID inheritance before first Main instruction":"no active neighbor matching avatar GUID");
    std::cout<<",\"attribute_policy\":"<<json_bytes(initialize?"TS1 zero reads and bounded growth in scopes0/1":"fixed banks")
        <<",\"avatar\":{\"resource_kind\":"<<json_bytes(avatar->standalone?"standalone_iff":"far_entry")
        <<",\"file\":"<<(avatar->standalone?json_bytes(avatar->archive):"null")
        <<",\"archive\":"<<(avatar->standalone?"null":json_bytes(avatar->archive))<<",\"entry\":"<<json_bytes(avatar->entry)
        <<",\"objd_id\":"<<*avatar_def<<",\"guid\":"<<person.binding->guid<<",\"owner\":"<<runtime_avatar_id<<",\"main\":"<<avatar_main<<",\"objd_main\":"<<person.functions->main<<",\"root\":"<<root
        <<",\"semiglobal_archive\":"<<json_bytes(semi->archive)<<",\"semiglobal_entry\":"<<json_bytes(semi->entry)<<",\"glob_name\":"<<json_bytes(person.glob->name)
        <<"},\"global\":{\"archive\":"<<json_bytes(globals->archive)<<",\"entry\":"<<json_bytes(globals->entry)<<"},\"trace\":[";
    for(std::size_t n=0;n<trace.size();++n){if(n)std::cout<<',';const auto& t=trace[n];
        std::cout<<"{\"phase\":"<<json_bytes(trace_phases[n])<<",\"step\":"<<t.step<<",\"depth\":"<<t.depth<<",\"owner\":"<<t.code_owner<<",\"routine\":"<<t.key.id<<",\"pc\":"<<t.pc
            <<",\"opcode\":"<<t.instruction.opcode<<",\"true\":"<<static_cast<unsigned>(t.instruction.on_true)<<",\"false\":"<<static_cast<unsigned>(t.instruction.on_false)<<",\"operands\":[";
        for(std::size_t j=0;j<8;++j){if(j)std::cout<<',';std::cout<<static_cast<unsigned>(t.instruction.operands[j]);}std::cout<<"]}";
    }
    std::cout<<"],\"status\":"<<json_bytes(odh::vm_status_name(status))<<",\"diagnostic\":"<<json_bytes(vm.diagnostic())
        <<",\"host_failed\":"<<(host_failed?"true":"false")<<",\"host_diagnostic\":"<<json_bytes(host_diagnostic)<<",\"steps\":"<<total_steps()<<",\"vm_steps\":"<<vm.steps()<<",\"ticks\":"<<ticks
        <<",\"create_object_instance\":{\"pending_requests\":"<<pending_create_objects.size()
        <<",\"last_diagnostic\":"<<json_bytes(last_create_object_diagnostic)<<",\"runtime_objects\":[";
    bool first_runtime_object=true;
    for(const auto& [id,runtime]:runtime_object_threads){
        if(!first_runtime_object)std::cout<<',';
        first_runtime_object=false;
        const auto* entity=world.find(id);
        std::cout<<"{\"id\":"<<id<<",\"guid\":";
        if(entity&&entity->definition)std::cout<<entity->definition->guid;else std::cout<<"null";
        std::cout<<",\"status\":"<<json_bytes(odh::vm_status_name(runtime->vm->status()))
            <<",\"main_steps\":"<<runtime->vm->steps()<<",\"position\":";
        if(entity&&entity->position)std::cout<<'['<<entity->position->x<<','<<entity->position->y<<','
            <<entity->position->level<<','<<unsigned(entity->position->direction)<<']';
        else std::cout<<"null";
        std::cout<<",\"attachment\":";
        if(entity&&entity->attachment)std::cout<<'['<<entity->attachment->container<<','<<entity->attachment->slot<<']';
        else std::cout<<"null";
        std::cout<<'}';
    }
    std::cout<<"],\"remove_object_instance\":{\"pending_requests\":"<<pending_remove_objects.size()
        <<",\"last_diagnostic\":"<<json_bytes(last_remove_object_diagnostic)<<"}},\"second_main\":";
    if(second_main_vm){
        const auto* second_entity=world.find(static_cast<std::int16_t>(headless_second_main->avatar.id));
        std::cout<<"{\"entity_id\":"<<headless_second_main->avatar.id<<",\"guid\":";
        if(second_entity&&second_entity->definition)std::cout<<second_entity->definition->guid;
        else std::cout<<"null";
        std::cout<<",\"routine\":"<<headless_second_main->routine
        <<",\"status\":"<<json_bytes(odh::vm_status_name(second_main_vm->status()))
        <<",\"diagnostic\":"<<json_bytes(second_main_vm->diagnostic())<<",\"steps\":"<<second_main_vm->steps()
        <<",\"stack_depth\":"<<second_main_vm->stack().size()<<'}';
    }
    else std::cout<<"null";
    if(session_context){
        std::cout<<",\"household_main\":{\"mode\":"<<json_bytes(session_context->single_resident_session?
            "single_resident_session":"whole_household")<<",\"scheduler_entities\":"<<entity_scheduler.size()
            <<",\"entities_before\":"<<household_entity_count<<",\"entities_after\":"<<entity_count()<<",\"additional_residents\":[";
        for(std::size_t i=0;i<household_runtime.size();++i){
            const auto& member=*household_runtime[i];if(i)std::cout<<',';
            const auto* entity=world.find(member.id);
            const auto position_json=[&](const std::optional<odh::EntityPosition>& p){
                if(p)std::cout<<'['<<p->x<<','<<p->y<<','<<p->level<<','<<unsigned(p->direction)<<']';
                else std::cout<<"null";
            };
            std::cout<<"{\"entity_id\":"<<member.id<<",\"guid\":"<<member.data.binding->guid
                <<",\"file\":"<<json_bytes(member.resource.archive)<<",\"routine\":"<<member.main
                <<",\"status\":"<<json_bytes(odh::vm_status_name(member.vm->status()))
                <<",\"diagnostic\":"<<json_bytes(member.vm->diagnostic())<<",\"steps\":"<<member.vm->steps()
                <<",\"flags\":"<<(entity&&entity->object_words.count(8)?std::to_string(entity->object_words.at(8)):"null")
                <<",\"static_footprint\":"<<(entity?(entity->static_footprint?"true":"false"):"null")
                <<",\"start_position\":";position_json(member.start);
            std::cout<<",\"position\":";position_json(entity?entity->position:std::nullopt);
            std::cout<<",\"routing\":{\"reason\":"<<json_bytes(member.route_reason)
                <<",\"active\":"<<(member.routing&&member.routing->active()?"true":"false")
                <<",\"entry_point5_unknown\":";
            if(member.route_ep5_unknown_caller&&member.route_ep5_unknown_stack_object){
                std::cout<<"{\"caller\":"<<*member.route_ep5_unknown_caller
                    <<",\"stack_object\":"<<*member.route_ep5_unknown_stack_object
                    <<",\"action\":";
                if(member.route_ep5_unknown_action)std::cout<<*member.route_ep5_unknown_action;else std::cout<<"null";
                std::cout<<'}';
            }else std::cout<<"null";
            std::cout
                <<",\"movement_callbacks\":"<<member.route_movement_callbacks
                <<",\"snapshot_refreshes\":"<<member.route_snapshot_refreshes
                <<",\"portal_planned_calls\":"<<member.room_portals.planned_calls
                <<",\"portal_returns\":"<<member.room_portals.returns
                <<",\"portal_room_transitions\":"<<member.room_portals.transitions
                <<",\"portal_planner_diagnostic\":"<<json_bytes(member.room_portals.diagnostic)
                <<",\"room_id\":";
            if(member.route_grid)std::cout<<member.route_grid->room_id;else std::cout<<"null";
            std::cout<<",\"floor\":";
            if(member.route_grid)std::cout<<static_cast<unsigned>(member.route_grid->level);else std::cout<<"null";
            std::cout<<"},\"go_here\":";
            if(!member.go_here)std::cout<<"null";
            else {
                const auto destination=member.go_here->destination();
                const auto start=member.go_here->start();
                std::cout<<"{\"status\":"<<json_bytes(member.go_here->status())
                    <<",\"route_status\":"<<json_bytes(member.go_here->route_status())
                    <<",\"diagnostic\":"<<json_bytes(member.go_here->diagnostic())
                    <<",\"marker_id\":"<<(member.go_here->marker_id()?std::to_string(*member.go_here->marker_id()):"null")
                    <<",\"marker_alive\":"<<(member.go_here->marker_alive()?"true":"false")
                    <<",\"code_owner\":"<<(member.go_here->owner()?std::to_string(*member.go_here->owner()):"null")
                    <<",\"result\":"<<(member.go_here->result()?(*member.go_here->result()?"true":"false"):"null")
                    <<",\"start_position\":";
                if(start)std::cout<<'['<<start->x<<','<<start->y<<','<<start->level<<','<<unsigned(start->direction)<<']';
                else std::cout<<"null";
                std::cout<<",\"target_position\":";
                if(destination)std::cout<<'['<<destination->x<<','<<destination->y<<','<<destination->level<<','<<unsigned(destination->direction)<<']';
                else std::cout<<"null";
                std::cout<<'}';
            }
            std::cout<<",\"attachment\":";
            if(entity&&entity->attachment)std::cout<<'['<<entity->attachment->container<<','<<entity->attachment->slot<<']';
            else std::cout<<"null";
            std::cout<<'}';
        }
        std::cout<<"],\"snap_targets\":[";
        std::set<std::int16_t> reported_snap_targets;
        for(const auto& entry:snap_runtimes){
            const auto id=std::get<0>(entry.first);
            if(!reported_snap_targets.insert(id).second)continue;
            if(reported_snap_targets.size()>1)std::cout<<',';
            const auto* target=world.find(id);
            std::cout<<"{\"entity_id\":"<<id<<",\"slots\":[";
            if(target)for(std::size_t i=0;i<target->slots.size();++i){if(i)std::cout<<',';std::cout<<target->slots[i];}
            std::cout<<"]}";
        }
        std::cout<<"]}";
    }
    if(second_saved_main){
        const auto position_json=[&](const std::optional<odh::EntityPosition>& p){
            if(p)std::cout<<'['<<p->x<<','<<p->y<<','<<p->level<<','<<unsigned(p->direction)<<']';
            else std::cout<<"null";
        };
        const auto current_position=[&](std::int16_t id)->std::optional<odh::EntityPosition>{
            const auto* entity=world.find(id);return entity?entity->position:std::nullopt;};
        std::cout<<",\"paired_saved_main\":{\"second_file\":"<<json_bytes(second_avatar->archive)
            <<",\"entities_before\":"<<paired_entity_count<<",\"entities_after\":"<<entity_count()
            <<",\"primary_start_position\":";position_json(paired_primary_start);
        std::cout<<",\"primary_position\":";position_json(current_position(runtime_avatar_id));
        std::cout<<",\"second_start_position\":";position_json(headless_second_main->avatar.position);
        std::cout<<",\"second_position\":";position_json(current_position(static_cast<std::int16_t>(headless_second_main->avatar.id)));
        std::cout<<'}';
    }
    std::cout<<",\"tick_budget_exhausted\":"<<(tick_exhausted?"true":"false")
        <<",\"instruction_budget_exhausted\":"<<(instruction_exhausted?"true":"false")<<",\"stack_depth\":"<<vm.stack().size()<<",\"rng_cycle_requests\":"<<rng_requests
        <<",\"rng_backend\":\"FreeSO xorshift64star reference profile\",\"rng_seed_initial\":"<<seed.value_or(1)<<",\"rng_seed_final\":"<<random.seed()
        <<",\"rng_seed_source\":"<<json_bytes(seed?"explicit host fixture; original saved seed unavailable":"host default fixture1; original saved seed unavailable")
        <<",\"random_reads\":[";
    for(std::size_t n=0;n<random_reads.size();++n){if(n)std::cout<<',';const auto& value=random_reads[n];
        std::cout<<"{\"owner\":"<<value.context.code_owner<<",\"range\":"<<value.range<<",\"result\":"<<value.result
            <<",\"destination_scope\":"<<static_cast<unsigned>(value.destination_scope)<<",\"destination_index\":"<<value.destination_data
            <<",\"seed_before\":"<<value.seed_before<<",\"seed_after\":"<<value.seed_after<<'}';}
    std::cout<<"],\"queue_size\":"<<queue.size()<<",\"queue_active\":"<<(queue.active()?"true":"false")<<",\"action_incomplete\":"<<(action_incomplete?"true":"false")<<",\"action\":";
    if(action)std::cout<<"{\"archive\":"<<json_bytes(action->archive)<<",\"entry\":"<<json_bytes(action->entry)<<",\"objd_id\":"<<*action_def<<",\"guid\":"<<action_data->binding->guid<<",\"owner\":2,\"routine\":"<<*action_routine<<'}';else std::cout<<"null";
    std::cout<<",\"completion\":";
    if(queue.completion()){const auto& c=*queue.completion();std::cout<<"{\"uid\":"<<c.uid<<",\"ticket\":"<<c.ticket<<",\"tick\":"<<c.tick<<",\"result\":"<<(c.result?"true":"false")<<'}';}else std::cout<<"null";
    std::cout<<",\"interaction_runtime\":";
    if(!render_interaction_requested)std::cout<<"null";
    else {
        const auto actor_id=render_interaction_actor_id.value_or(runtime_avatar_id);
        const auto* actor=world.find(actor_id);
        auto* actor_queue=actor_queue_for(actor_id);
        auto* actor_vm=actor_vm_for(actor_id);
        const auto* household_actor=actor_id==runtime_avatar_id?nullptr:selected_household_member();
        const auto animation=actor_id==runtime_avatar_id?(playback?playback->progress():std::nullopt):
            (household_actor&&household_actor->playback?household_actor->playback->progress():std::nullopt);
        std::cout<<"{\"trigger\":"<<json_bytes(headless_interaction?(headless_stand?"headless_sit_schedule":"headless_schedule"):
            headless_window_interaction?"headless_window_input":"window_input")
            <<",\"scheduled_tick\":"<<(scheduled_interaction_tick?std::to_string(*scheduled_interaction_tick):"null")
            <<",\"scheduled_target\":"<<(headless_interaction?std::to_string(headless_interaction->id):"null")
            <<",\"enqueued\":"<<(render_interaction_enqueued?"true":"false")
            <<",\"status\":"<<json_bytes(render_interaction_status)
            <<",\"diagnostic\":"<<json_bytes(render_interaction_diagnostic)
            <<",\"main_vm_status\":"<<json_bytes(odh::vm_status_name(vm.status()))
            <<",\"main_vm_diagnostic\":"<<json_bytes(vm.diagnostic())
            <<",\"actor_vm_status\":"<<(actor_vm?json_bytes(odh::vm_status_name(actor_vm->status())):"null")
            <<",\"actor_vm_diagnostic\":"<<(actor_vm?json_bytes(actor_vm->diagnostic()):"null")
            <<",\"action\":"<<(render_interaction_action?std::to_string(*render_interaction_action):"null")
            <<",\"code_owner\":"<<(render_interaction_owner?std::to_string(*render_interaction_owner):"null")
            <<",\"result\":"<<(render_interaction_result?(*render_interaction_result?"true":"false"):"null")
            <<",\"queue_size\":"<<(actor_queue?actor_queue->size():0)
            <<",\"queue_active\":"<<(actor_queue&&actor_queue->active()?"true":"false")
            <<",\"actor\":{\"id\":"<<actor_id<<",\"generation\":"<<(actor?actor->generation:0)
            <<",\"position\":";
        if(actor&&actor->position)std::cout<<'['<<actor->position->x<<','<<actor->position->y<<','
            <<actor->position->level<<','<<unsigned(actor->position->direction)<<']';
        else std::cout<<"null";
        std::cout<<",\"attachment\":";
        if(actor&&actor->attachment)std::cout<<"{\"container\":"<<actor->attachment->container
            <<",\"slot\":"<<actor->attachment->slot<<",\"positioned\":"
            <<(actor->attachment->positioned?"true":"false")<<'}';
        else std::cout<<"null";
        std::cout<<"},\"animation\":";
        if(animation)std::cout<<"{\"name\":"<<json_bytes(animation->name)<<",\"frame\":"<<animation->frame
            <<",\"frames\":"<<animation->frames<<",\"end\":"<<(animation->end?"true":"false")
            <<",\"loop\":"<<(animation->loop?"true":"false")<<'}';
        else std::cout<<"null";
        std::cout<<'}';
    }
    std::cout<<",\"go_here_runtime\":";
    if(go_here_status=="not_requested")std::cout<<"null";
    else {
        const auto* live_actor=world.find(runtime_avatar_id);
        const auto* live_marker=go_here_marker_id?world.find(*go_here_marker_id):nullptr;
        const bool marker_alive=live_marker&&go_here_marker_generation&&
            live_marker->generation==*go_here_marker_generation;
        std::cout<<"{\"trigger\":"<<json_bytes(headless_window_go_here?"headless_window_input":
            headless_go_here?"headless_schedule":"window_input")
            <<",\"scheduled_tick\":"<<(headless_window_go_here?std::to_string(headless_window_go_here->tick):
                headless_go_here?std::to_string(headless_go_here->tick):"null")
            <<",\"ground_click_sent\":"<<(headless_window_go_here_click?"true":"false")
            <<",\"action_input_sent\":"<<(headless_window_go_here_input_sent?"true":"false")
            <<",\"requested_tile\":["<<(headless_window_go_here?std::to_string(headless_window_go_here->x):"null")
            <<','<<(headless_window_go_here?std::to_string(headless_window_go_here->y):"null")
            <<','<<(headless_window_go_here?std::to_string(headless_window_go_here->level):"null")<<']'
            <<",\"selected_ground_tile\":";
        if(headless_window_go_here_ground)std::cout<<'['<<headless_window_go_here_ground->x<<','
            <<headless_window_go_here_ground->y<<','<<unsigned(headless_window_go_here_ground->level)<<']';
        else std::cout<<"null";
        std::cout
            <<",\"tile\":";
        if(go_here_position)std::cout<<'['<<go_here_position->x/16<<','<<go_here_position->y/16<<']';
        else std::cout<<"null";
        std::cout<<",\"marker_id\":"<<(go_here_marker_id?std::to_string(*go_here_marker_id):"null")
            <<",\"marker_generation\":"<<(go_here_marker_generation?std::to_string(*go_here_marker_generation):"null")
            <<",\"marker_alive\":"<<(marker_alive?"true":"false")
            <<",\"status\":"<<json_bytes(go_here_status)
            <<",\"route_status\":"<<json_bytes(go_here_route_status)
            <<",\"diagnostic\":"<<json_bytes(go_here_diagnostic)
            <<",\"action\":"<<(go_here_action?std::to_string(*go_here_action):"null")
            <<",\"code_owner\":"<<(go_here_owner?std::to_string(*go_here_owner):"null")
            <<",\"result\":"<<(go_here_result?(*go_here_result?"true":"false"):"null")
            <<",\"start_position\":";
        if(go_here_start_position)std::cout<<'['<<go_here_start_position->x<<','<<go_here_start_position->y<<','
            <<go_here_start_position->level<<','<<unsigned(go_here_start_position->direction)<<']';
        else std::cout<<"null";
        std::cout<<",\"end_position\":";
        if(live_actor&&live_actor->position)std::cout<<'['<<live_actor->position->x<<','<<live_actor->position->y<<','
            <<live_actor->position->level<<','<<unsigned(live_actor->position->direction)<<']';
        else std::cout<<"null";
        std::cout<<",\"queue_size\":"<<queue.size()<<",\"queue_active\":"<<(queue.active()?"true":"false")<<'}';
    }
    std::cout<<",\"portal_runtime\":";
    if(!headless_portal)std::cout<<"null";
    else {
        const auto* actor=world.find(runtime_avatar_id);
        const auto* target=world.find(headless_portal->id);
        std::cout<<"{\"trigger\":\"diagnostic_headless_portal15\",\"scheduled_tick\":"<<headless_portal->tick
            <<",\"portal_id\":"<<headless_portal->id<<",\"action\":"
            <<(headless_portal_action?std::to_string(*headless_portal_action):"null")<<",\"code_owner\":"
            <<(headless_portal_owner?std::to_string(*headless_portal_owner):"null")<<",\"status\":"
            <<json_bytes(headless_portal_status)<<",\"diagnostic\":"<<json_bytes(headless_portal_diagnostic)
            <<",\"result\":"<<(headless_portal_result?(*headless_portal_result?"true":"false"):"null")
            <<",\"actor_start_position\":";
        if(headless_portal_actor_start_position)std::cout<<'['<<headless_portal_actor_start_position->x<<','
            <<headless_portal_actor_start_position->y<<','<<headless_portal_actor_start_position->level<<','
            <<unsigned(headless_portal_actor_start_position->direction)<<']';
        else std::cout<<"null";
        std::cout<<",\"actor_position\":";
        if(actor&&actor->position)std::cout<<'['<<actor->position->x<<','<<actor->position->y<<','
            <<actor->position->level<<','<<unsigned(actor->position->direction)<<']';
        else std::cout<<"null";
        std::cout<<",\"actor_level_changed\":";
        if(headless_portal_actor_start_position&&actor&&actor->position)
            std::cout<<(headless_portal_actor_start_position->level!=actor->position->level?"true":"false");
        else std::cout<<"null";
        std::cout<<",\"portal_start_position\":";
        if(headless_portal_start_position)std::cout<<'['<<headless_portal_start_position->x<<','
            <<headless_portal_start_position->y<<','<<headless_portal_start_position->level<<','
            <<unsigned(headless_portal_start_position->direction)<<']';
        else std::cout<<"null";
        std::cout<<",\"portal_position\":";
        if(target&&target->position)std::cout<<'['<<target->position->x<<','<<target->position->y<<','
            <<target->position->level<<','<<unsigned(target->position->direction)<<']';
        else std::cout<<"null";
        std::cout<<"}";
    }
    std::cout<<",\"stand_runtime\":";
    if(!headless_stand_applied)std::cout<<"null";
    else {
        const auto actor_id=stand_actor_id.value_or(runtime_avatar_id);
        const auto* actor=world.find(actor_id);
        std::cout<<"{\"trigger\":"<<json_bytes(stand_trigger)<<",\"actor_id\":"<<actor_id
            <<",\"scheduled_tick\":"<<stand_request_tick
            <<",\"scheduled_target\":"<<stand_request_target<<",\"precondition_met\":"
            <<(headless_stand_precondition_met?"true":"false")<<",\"entry_point\":27,\"action\":"
            <<(headless_stand_action?std::to_string(*headless_stand_action):"null")<<",\"code_owner\":"
            <<(headless_stand_owner?std::to_string(*headless_stand_owner):"null")<<",\"status\":"
            <<json_bytes(headless_stand_status)<<",\"diagnostic\":"<<json_bytes(headless_stand_diagnostic)
            <<",\"result\":"<<(headless_stand_result?(*headless_stand_result?"true":"false"):"null")
            <<",\"precondition_posture\":"<<(headless_stand_posture_before?std::to_string(*headless_stand_posture_before):"null")
            <<",\"precondition_attachment\":"<<(headless_stand_attachment_before?std::to_string(*headless_stand_attachment_before):"null")
            <<",\"actor_posture\":";
        if(actor&&actor->person_data.size()>0)std::cout<<actor->person_data[0];else std::cout<<"null";
        std::cout<<",\"actor_attached\":"<<(actor&&actor->attachment?"true":"false")<<'}';
    }
    std::cout<<",\"tuning_profile\":\"explicit BCON only; no OTF or entity replacement backend; missing remains unsupported\",\"tuning_reads\":[";
    for(std::size_t n=0;n<tuning_reads.size();++n){if(n)std::cout<<',';const auto& v=tuning_reads[n];
        const auto scope=v.address.scope==odh::RoutineScope::global?"global":v.address.scope==odh::RoutineScope::semiglobal?"semiglobal":"private";
        std::cout<<"{\"owner\":"<<v.owner<<",\"scope\":"<<json_bytes(scope)<<",\"table\":"<<v.address.table<<",\"index\":"<<static_cast<unsigned>(v.address.index)
            <<",\"value\":"<<v.value<<",\"semiglobal_fallback\":"<<(v.semiglobal_fallback?"true":"false")<<'}';
    }
    const auto saved_prefix_count=saved_world_main?restored_world->records.size():
        saved_prefixes?saved_prefixes->size():0;
    const auto saved_common_count=saved_world_main?restored_world->records.size():
        saved_common?saved_common->size():0;
    const auto saved_tail_count=saved_world_main?restored_world->records.size():
        saved_tails?saved_tails->size():0;
    const auto saved_prefix_at=[&](std::size_t n)->const odh::SavedObjectPrefix&{
        return saved_world_main?restored_world->records.at(n).saved:saved_prefixes->at(n);
    };
    const auto saved_common_at=[&](std::size_t n)->const odh::ObjmCommonState&{
        return saved_world_main?restored_world->records.at(n).common:saved_common->at(n);
    };
    const auto saved_tail_at=[&](std::size_t n)->const odh::ObjmTailState*{
        if(saved_world_main)return &restored_world->records.at(n).tail;
        const auto& tail=saved_tails->at(n);return tail?&*tail:nullptr;
    };
    std::cout<<"],\"lot\":";
    if(lot)std::cout<<"{\"house\":"<<json_bytes(*lot_path)<<",\"selected_id\":"<<*lot_id<<",\"serialized_id\":"<<simi->global_data[10]
        <<",\"simi_version\":"<<simi->version<<",\"serialized_globals\":"<<simi->global_data.size()<<",\"remaining_bytes\":"<<simi->remaining_bytes
        <<",\"profile\":\"SIMI globals with bounded Simitone reference clock when supported; selected lot overrides10; bootstrap globals provided separately\",\"zoning\":"<<(zoning_path?json_bytes(*zoning_path):"null")<<'}';
    else std::cout<<"null";
    std::cout<<",\"saved_objm_container\":";
    if(objm){std::uint64_t bytes=0;for(const auto& block:objm->blocks)bytes+=block.bytes.size();
        std::cout<<"{\"version\":"<<objm->version<<",\"compression\":"<<static_cast<unsigned>(objm->compression)
            <<",\"mapped_ids\":"<<objm->map_records.size()<<",\"blocks\":"<<objm->blocks.size()<<",\"owned_block_bytes\":"<<bytes
            <<",\"remaining_bytes\":"<<objm->remaining_bytes
            <<",\"known_reference_schema_decoded\":"<<(saved_world_main||(saved_tails&&std::all_of(saved_tails->begin(),saved_tails->end(),[](const auto& tail){return tail.has_value();}))?"true":"false")
            <<",\"instance_state_decoded\":false,\"world_entities_restored\":"<<(saved_world_main?"true":"false")<<"}";
    }else std::cout<<"null";
    std::cout<<",\"saved_object_prefixes\":";
    if(saved_prefixes||saved_world_main){
        std::cout<<"{\"profile\":\"owned prefix snapshot; raw objt GUID mapping; OBJD and complete instance state unresolved\",\"objt_version\":"<<objt->version
            <<",\"objt_entries\":"<<objt->entries.size()<<",\"world_entities_restored\":"<<(saved_world_main?"true":"false")<<",\"records\":[";
        for(std::size_t n=0;n<saved_prefix_count;++n){if(n)std::cout<<',';const auto& v=saved_prefix_at(n);const auto& p=v.prefix;
            std::cout<<"{\"object_id\":"<<v.object_id<<",\"type_id\":"<<v.type_id<<",\"guid\":"<<v.guid
                <<",\"block_offset\":"<<p.block_offset<<",\"odd_offset\":"<<(p.odd_offset?"true":"false")
                <<",\"consumed_bits\":"<<p.consumed_bits<<",\"remaining_bits\":"<<p.remaining_bits
                <<",\"x\":"<<p.x<<",\"y\":"<<p.y<<",\"level\":"<<p.level
                <<",\"out_of_world\":"<<(p.x==-16&&p.y==-16?"true":"false")
                <<",\"footprint\":["<<p.footprint.min_y<<','<<p.footprint.min_x<<','<<p.footprint.max_y<<','<<p.footprint.max_x
                <<"],\"unknown_data\":"<<p.unknown_data<<",\"attributes\":[";
            for(std::size_t i=0;i<p.attributes.size();++i){if(i)std::cout<<',';std::cout<<p.attributes[i];}
            std::cout<<"],\"temps\":[";for(std::size_t i=0;i<p.temps.size();++i){if(i)std::cout<<',';std::cout<<p.temps[i];}
            std::cout<<"],\"object_data\":[";for(std::size_t i=0;i<p.object_data.size();++i){if(i)std::cout<<',';std::cout<<p.object_data[i];}
            std::cout<<"],\"extras\":[";for(std::size_t i=0;i<p.extras.size();++i){if(i)std::cout<<',';std::cout<<p.extras[i];}
            std::cout<<"]}";
        }
        std::cout<<"]}";
    }else std::cout<<"null";
    std::cout<<",\"definition_catalog\":";
    if(definition_index)std::cout<<"{\"profile\":\"explicit sources; unique GUID only; original precedence unproven\",\"guids\":"
        <<definition_index->guid_count()<<",\"candidates\":"<<definition_index->candidate_count()<<'}';
    else std::cout<<"null";
    std::cout<<",\"saved_definition_records\":[";
    if((saved_prefixes||saved_world_main)&&definition_index)for(std::size_t n=0;n<saved_prefix_count;++n){
        if(n)std::cout<<',';const auto& p=saved_prefix_at(n);const auto* matches=definition_index->candidates(p.guid);
        const auto* unique=definition_index->unique(p.guid);
        std::cout<<"{\"object_id\":"<<p.object_id<<",\"guid\":"<<p.guid<<",\"status\":"
            <<json_bytes(unique?"unique":matches?"ambiguous":"missing")<<",\"candidates\":[";
        if(matches)for(std::size_t i=0;i<matches->size();++i){if(i)std::cout<<',';const auto& c=(*matches)[i];const auto& d=c.definition;
            std::cout<<"{\"source\":"<<json_bytes(c.source)<<",\"asset\":"<<json_bytes(c.asset)<<",\"chunk_id\":"<<c.chunk_id
                <<",\"version\":"<<d.version<<",\"object_type\":"<<d.object_type<<",\"master_id\":"<<d.master_id
                <<",\"sub_index\":"<<d.sub_index<<",\"is_multitile\":"<<(d.is_multitile?"true":"false")
                <<",\"header_extent\":"<<d.header_extent<<",\"remaining_bytes\":"<<d.remaining_bytes<<'}';
        }
        std::cout<<"]}";
    }
    std::cout<<"],\"saved_common_records\":";
    if(saved_common||saved_world_main){
        std::cout<<'[';const auto bank=[&](const auto& values){std::cout<<'[';for(std::size_t i=0;i<values.size();++i){if(i)std::cout<<',';std::cout<<values[i];}std::cout<<']';};
        for(std::size_t n=0;n<saved_common_count;++n){if(n)std::cout<<',';const auto& s=saved_common_at(n);
            std::cout<<"{\"object_id\":"<<saved_prefix_at(n).object_id<<",\"block_offset\":"<<s.block_offset
                <<",\"start_bits\":"<<s.start_bits<<",\"consumed_bits\":"<<s.consumed_bits<<",\"remaining_bits\":"<<s.remaining_bits
                <<",\"decoded_words\":"<<s.decoded_words<<",\"stack_count\":"<<s.stack_count<<",\"stack_flags\":"<<s.stack_flags<<",\"stack\":[";
            for(std::size_t i=0;i<s.stack.size();++i){if(i)std::cout<<',';const auto& f=s.stack[i];
                std::cout<<"{\"stack_object_id\":"<<f.stack_object_id<<",\"tree_id\":"<<f.tree_id<<",\"node_id\":"<<f.node_id
                    <<",\"local_count\":"<<static_cast<unsigned>(f.local_count)<<",\"parameter_count\":"<<static_cast<unsigned>(f.parameter_count)
                    <<",\"parameters\":";bank(f.parameters);std::cout<<",\"locals\":";bank(f.locals);
                std::cout<<",\"primitive_state\":"<<f.primitive_state<<",\"code_owner_obj_type\":"<<f.code_owner_obj_type<<'}';
            }
            std::cout<<"],\"relationship_flag\":"<<s.relationship_flag<<",\"relationship_count\":"<<s.relationship_count<<",\"relationships\":[";
            for(std::size_t i=0;i<s.relationships.size();++i){if(i)std::cout<<',';const auto& r=s.relationships[i];
                std::cout<<"{\"is_present\":"<<r.is_present<<",\"target_id\":"<<r.target_id<<",\"value_count\":"<<r.value_count<<",\"values\":";
                bank(r.values);std::cout<<'}';
            }
            std::cout<<"],\"slot_count\":"<<s.slot_count<<",\"linked_slot\":";
            if(s.linked_slot)std::cout<<'['<<s.linked_slot->unknown<<','<<s.linked_slot->object_id<<']';else std::cout<<"null";
            std::cout<<",\"slots\":[";for(std::size_t i=0;i<s.slots.size();++i){if(i)std::cout<<',';std::cout<<'['<<s.slots[i].unknown<<','<<s.slots[i].object_id<<']';}
            std::cout<<"],\"dynamic_sprite_flag_count\":"<<s.dynamic_sprite_flag_count<<",\"dynamic_sprite_flags\":";bank(s.dynamic_sprite_flags);
            std::cout<<",\"resource_dependent_state_decoded\":false,\"world_entities_restored\":false}";
        }
        std::cout<<']';
    }else std::cout<<"null";
    std::cout<<",\"saved_tail_records\":";
    if(saved_tails||saved_world_main){
        std::cout<<'[';
        for(std::size_t n=0;n<saved_tail_count;++n){if(n)std::cout<<',';const auto* tail=saved_tail_at(n);
            std::cout<<"{\"object_id\":"<<saved_prefix_at(n).object_id<<",\"status\":"
                <<json_bytes(tail?"decoded_known_reference_schema":"definition_unavailable")
                <<",\"world_entities_restored\":false,\"saved_threads_restored\":false,\"state\":";
            if(tail)odh_tools::saved_tail(std::cout,*tail);else std::cout<<"null";
            std::cout<<'}';
        }
        std::cout<<']';
    }else std::cout<<"null";
    std::cout<<",\"global_bootstrap\":";
    if(bootstrap_globals){
        std::cout<<"{\"profile\":\"FreeSO TS1 Init/IFF activation; all-expansions policy; not detected installation or saved VM restore\",\"globals\":[";
        for(std::size_t n=0;n<odh::Ts1BootstrapGlobals::indices.size();++n){if(n)std::cout<<',';
            const auto index=odh::Ts1BootstrapGlobals::indices[n];
            std::cout<<"{\"index\":"<<index<<",\"initial\":"<<odh::Ts1BootstrapGlobals::initial[n]<<",\"current\":"<<*bootstrap_globals->read(index)<<'}';}
        std::cout<<"]}";
    }else std::cout<<"null";
    std::cout<<",\"global_reads\":[";
    for(std::size_t n=0;n<global_reads.size();++n){if(n)std::cout<<',';std::cout<<"{\"index\":"<<global_reads[n].index<<",\"value\":"<<global_reads[n].value
        <<",\"source\":"<<json_bytes(global_reads[n].source==GlobalSource::clock?"simulation_clock":global_reads[n].source==GlobalSource::family?"family_activation":global_reads[n].source==GlobalSource::bootstrap?"runtime_bootstrap":"lot_context")<<'}';}
    std::cout<<"],\"zoning_reads\":[";
    for(std::size_t n=0;n<zoning_reads.size();++n){if(n)std::cout<<',';const auto& value=zoning_reads[n];
        std::cout<<"{\"input\":"<<value.input<<",\"value\":"<<value.value<<",\"fallback\":"<<(value.fallback?"true":"false")<<'}';}
    std::cout<<"],\"neighborhood\":";
    if(neighbors)std::cout<<"{\"file\":"<<json_bytes(*neighborhood_path)<<",\"version\":"<<neighbors->source().version
        <<",\"entries\":"<<neighbors->source().entries.size()<<",\"active\":"<<neighbors->active_count()<<",\"remaining_bytes\":"<<neighbors->source().remaining_bytes<<'}';
    else std::cout<<"null";
    std::cout<<",\"neighbor_next\":[";
    for(std::size_t n=0;n<neighbor_next.size();++n){if(n)std::cout<<',';const auto& value=neighbor_next[n];
        std::cout<<"{\"current\":"<<value.current<<",\"result\":"<<value.result<<",\"success\":"<<(value.success?"true":"false")<<'}';}
    std::cout<<"],\"entity_population\":"<<json_bytes(saved_world_main?"saved World restored; saved threads dormant":"constructed actor and optional action target; saved lot entities not restored")<<",\"entity_next\":[";
    for(std::size_t n=0;n<entity_next.size();++n){if(n)std::cout<<',';const auto& v=entity_next[n];
        std::cout<<"{\"caller\":"<<v.context.caller<<",\"stack_object\":"<<v.context.stack_object<<",\"owner\":"<<v.context.code_owner
            <<",\"guid\":"<<v.guid<<",\"mode\":"<<static_cast<unsigned>(v.mode)<<",\"current\":"<<v.current<<",\"result\":"<<(v.result?std::to_string(*v.result):"null")
            <<",\"success\":"<<(v.success?"true":"false")<<",\"scope\":"<<static_cast<unsigned>(v.scope)<<",\"index\":"<<v.index<<'}';}
    std::cout<<"],\"neighbor_reads\":[";
    for(std::size_t n=0;n<neighbor_reads.size();++n){if(n)std::cout<<',';const auto& value=neighbor_reads[n];
        const auto source=value.source==odh::NeighborDataSource::serialized?"serialized":value.source==odh::NeighborDataSource::legacy_zero_extension?"legacy_zero_extension":
            value.source==odh::NeighborDataSource::missing_neighbor_zero?"missing_neighbor_reference_zero":"absent_person_data_reference_zero";
        std::cout<<"{\"neighbor\":"<<value.neighbor<<",\"index\":"<<value.index<<",\"value\":"<<value.value<<",\"source\":"<<json_bytes(source)<<'}';}
    std::cout<<']';
    const auto* actor=world.find(runtime_avatar_id);
    std::cout<<",\"relationship_state\":";
    if(relationships)std::cout<<"{\"profile\":\"TS1 runtime copy; multiplier1; original NBRS unchanged; object matrices are not restored from ObjM\",\"sources\":"<<relationships->source_count()
        <<",\"pairs\":"<<relationships->pair_count()<<",\"values\":"<<relationships->value_count()<<'}';
    else std::cout<<"null";
    std::cout<<",\"relationship_records\":[";
    for(std::size_t n=0;n<relationship_records.size();++n){if(n)std::cout<<',';const auto& v=relationship_records[n];
        std::cout<<"{\"caller\":"<<v.context.caller<<",\"stack_object\":"<<v.context.stack_object
            <<",\"owner\":"<<v.context.code_owner<<",\"matrix\":"<<json_bytes(v.matrix==odh::RelationshipMatrix::neighbor?"neighbor":"object")
            <<",\"source\":"<<v.source<<",\"target\":"<<v.target<<",\"index\":"<<static_cast<unsigned>(v.index)
            <<",\"mode\":"<<static_cast<unsigned>(v.mode)<<",\"set_mode\":"<<static_cast<unsigned>(v.set_mode)
            <<",\"value_before\":"<<(v.value_before?std::to_string(*v.value_before):"null")
            <<",\"value_after\":"<<(v.value_after?std::to_string(*v.value_after):"null")
            <<",\"created\":"<<(v.created?"true":"false")<<",\"grown\":"<<(v.grown?"true":"false")
            <<",\"success\":"<<(v.success?"true":"false")<<",\"skipped_entities\":"<<(v.skipped_entities?"true":"false")<<",\"diff_multiplier\":1}";
    }
    std::cout<<']';
    std::cout<<",\"inventory_state\":";
    if(ngbh){
        std::size_t items=0;for(const auto& pair:ngbh->inventory_by_id)items+=pair.second.size();
        std::cout<<"{\"chunk_id\":"<<*ngbh_id<<",\"version\":"<<ngbh->version
            <<",\"inventory_extension\":"<<(ngbh->inventory_present?"true":"false")
            <<",\"inventories\":"<<ngbh->inventory_by_id.size()<<",\"items\":"<<items
            <<",\"remaining_bytes\":"<<ngbh->remaining_bytes<<",\"profile\":\"loaded NGBH; read-only FindToken3; no inventory mutation/persistence\"}";
    }else std::cout<<"null";
    std::cout<<",\"inventory_records\":[";
    for(std::size_t n=0;n<inventory_records.size();++n){if(n)std::cout<<',';const auto& v=inventory_records[n];
        std::cout<<"{\"caller\":"<<v.context.caller<<",\"stack_object\":"<<v.context.stack_object
            <<",\"owner\":"<<v.context.code_owner<<",\"target\":"<<v.target<<",\"neighbor\":"<<v.neighbor
            <<",\"guid\":"<<v.guid<<",\"type\":"<<static_cast<unsigned>(v.type)
            <<",\"count_temp\":"<<static_cast<unsigned>(v.count_temp)<<",\"index_temp\":"<<static_cast<unsigned>(v.index_temp)
            <<",\"write_index\":"<<(v.write_index?"true":"false")<<",\"success\":"<<(v.success?"true":"false")
            <<",\"found_index\":"<<(v.found_index?std::to_string(*v.found_index):"null")
            <<",\"count\":"<<(v.count?std::to_string(*v.count):"null")<<'}';
    }
    std::cout<<']';
    std::cout<<",\"constructor_profile\":"<<json_bytes(saved_world_main?"imported saved World banks; no synthetic constructor writes":"FreeSO 4c6b3e8 subset; PersonData101/MotiveData16/Contained3; raw ObjectData0..78 except derived getters; World identity/container/slot only")<<",\"flags_initial\":"<<flags.value_or(256)
        <<",\"age_initial\":"<<age.value_or(0)<<",\"actor_state\":{\"flags\":"<<actor->object_words.at(8)<<",\"age\":"<<actor->person_data[58]
        <<",\"autonomy\":"<<actor->person_data[36]<<",\"weight\":"<<actor->object_words.at(27)<<",\"support_strength\":"<<actor->object_words.at(28)
        <<",\"rotation_notches\":"<<actor->object_words.at(23)<<",\"flag_field2\":"<<actor->object_words.at(40)<<",\"room_placement\":"<<actor->object_words.at(30)<<",\"hunger\":"<<actor->motives[7]<<",\"person_data\":[";
    for(std::size_t n=0;n<actor->person_data.size();++n){if(n)std::cout<<',';std::cout<<actor->person_data[n];}
    std::cout<<"],\"attributes\":[";
    for(std::size_t n=0;n<actor->attributes.size();++n){if(n)std::cout<<',';std::cout<<actor->attributes[n];}
    std::cout<<"],\"motives\":[";for(std::size_t n=0;n<actor->motives.size();++n){if(n)std::cout<<',';std::cout<<actor->motives[n];}
    std::cout<<"],\"attachment\":";
    if(actor->attachment)std::cout<<'['<<actor->attachment->container<<','<<actor->attachment->slot<<']';
    else std::cout<<"null";
    std::cout<<'}';
    std::cout<<",\"stack\":[";for(std::size_t n=0;n<vm.stack().size();++n){if(n)std::cout<<',';const auto& f=vm.stack()[n];
        std::cout<<"{\"owner\":"<<f.context.code_owner<<",\"routine\":"<<f.key.id<<",\"pc\":"<<f.pc<<",\"caller\":"<<f.context.caller<<",\"callee\":"<<f.context.callee<<",\"stack_object\":"<<f.context.stack_object<<'}';}
    std::cout<<']';
    if(playback){
        std::cout<<",\"animation_profile\":"<<json_bytes(render_evidence?
            (render_window?"silent primary Playback in paced X11 presentation window; saved position is presentation-only; no audio or saved continuation":
             "silent primary Playback with one headless presentation-frame export; saved position is presentation-only; no audio or saved continuation"):
            "silent primary Playback; explicit FAR catalog and STR language1; no rig/render/audio or saved continuation")
            <<",\"animation_resolution_diagnostic\":"<<json_bytes(animation_resolution_diagnostic)
            <<",\"animation_progress\":";
        const auto progress=playback->progress();
        if(progress)std::cout<<"{\"name\":"<<json_bytes(progress->name)<<",\"frame\":"<<progress->frame
            <<",\"frames\":"<<progress->frames<<",\"queued_events\":"<<progress->queued_events
            <<",\"end\":"<<(progress->end?"true":"false")<<",\"loop\":"<<(progress->loop?"true":"false")<<'}';
        else std::cout<<"null";
        std::cout<<",\"animation_hands\":";
        const auto hands=playback->hands();
        if(hands)std::cout<<"{\"left\":"<<static_cast<unsigned>(hands->left)<<",\"right\":"<<static_cast<unsigned>(hands->right)<<'}';
        else std::cout<<"null";
        std::cout<<",\"animation_events\":[";
        for(std::size_t n=0;n<animation_events.size();++n){if(n)std::cout<<',';const auto& event=animation_events[n];
            std::cout<<"{\"tick\":"<<event.tick<<",\"actor\":"<<event.actor<<",\"time_id\":"<<event.time_id<<",\"properties\":[";
            for(std::size_t p=0;p<event.properties.size();++p){if(p)std::cout<<',';
                std::cout<<'['<<json_bytes(event.properties[p].first)<<','<<json_bytes(event.properties[p].second)<<']';}
            std::cout<<"]}";
        }std::cout<<']';
    }
    std::cout<<",\"sound_profile\":\"BHAV PlaySound opcode23 resolves explicit FWAV IDs; no audio sink, accepted requests are silent\",\"sound_events\":[";
    for(std::size_t n=0;n<sound_records.size();++n){if(n)std::cout<<',';const auto& record=sound_records[n];
        std::cout<<"{\"event_id\":"<<record.request.event_id<<",\"name\":"<<json_bytes(record.request.name)
            <<",\"source\":"<<record.request.source<<",\"status\":"<<json_bytes(record.status)<<'}';
    }
    std::cout<<"],\"route_runtime\":{\"state\":"<<json_bytes(route_runtime_state)
        <<",\"reason\":"<<json_bytes(route_runtime_reason)
        <<",\"entry_point5_unknown\":";
    if(route_ep5_unknown_caller&&route_ep5_unknown_stack_object){
        std::cout<<"{\"caller\":"<<*route_ep5_unknown_caller
            <<",\"stack_object\":"<<*route_ep5_unknown_stack_object<<",\"action\":";
        if(route_ep5_unknown_action)std::cout<<*route_ep5_unknown_action;else std::cout<<"null";
        std::cout<<'}';
    }else std::cout<<"null";
    std::cout<<",\"goal_validation_diagnostic\":"<<json_bytes(runtime_routing&&
            (runtime_routing->diagnostic().rfind("host goal validator rejected",0)==0||
             runtime_routing->diagnostic().rfind("GotoRelativePosition target rejected by host",0)==0)?
                route_goal_validation_diagnostic:std::string{})
        <<",\"stackobject_slot_resolutions\":"<<route_stack_slot_resolutions
        <<",\"stackobject_slot_loads\":"<<route_stack_slot_loads
        <<",\"movement_callbacks\":"<<route_runtime_movement_callbacks
        <<",\"snapshot_refreshes\":"<<route_runtime_snapshot_refreshes
        <<",\"snapshot_refresh_policy\":\"on_world_spatial_revision_change\""
        <<",\"grid_windowed\":"<<(runtime_route_grid&&runtime_route_grid->windowed?"true":"false")
        <<",\"active\":"<<(runtime_routing&&runtime_routing->active()?"true":"false")
        <<",\"dynamic_obstacle_refresh\":true,\"cross_room_portal_scheduler\":"<<(runtime_routing?"true":"false")
        <<",\"cross_room_portal_execution\":"<<(primary_room_portals.returns?"true":"false")
        <<",\"portal_planned_calls\":"<<primary_room_portals.planned_calls
        <<",\"portal_returns\":"<<primary_room_portals.returns
        <<",\"portal_room_transitions\":"<<primary_room_portals.transitions
        <<",\"portal_planner_diagnostic\":"<<json_bytes(primary_room_portals.diagnostic)
        <<",\"room_id\":";
    if(runtime_route_grid)std::cout<<runtime_route_grid->room_id;else std::cout<<"null";
    std::cout<<",\"floor\":";
    if(runtime_route_grid)std::cout<<static_cast<unsigned>(runtime_route_grid->level);else std::cout<<"null";
    std::cout<<",\"start_position\":";
    if(route_runtime_start_position)std::cout<<'['<<route_runtime_start_position->x<<','
        <<route_runtime_start_position->y<<','<<route_runtime_start_position->level<<','
        <<static_cast<unsigned>(route_runtime_start_position->direction)<<']';
    else std::cout<<"null";
    const auto* route_runtime_actor=world.find(runtime_avatar_id);
    std::cout<<",\"end_position\":";
    if(route_runtime_actor&&route_runtime_actor->position)std::cout<<'['<<route_runtime_actor->position->x<<','
        <<route_runtime_actor->position->y<<','<<route_runtime_actor->position->level<<','
        <<static_cast<unsigned>(route_runtime_actor->position->direction)<<']';
    else std::cout<<"null";
    std::cout<<",\"movement_positions\":[";
    for(std::size_t i=0;i<route_runtime_movement_positions.size();++i){
        if(i)std::cout<<',';
        const auto& position=route_runtime_movement_positions[i];
        std::cout<<'['<<position.x<<','<<position.y<<','<<position.level<<','
            <<static_cast<unsigned>(position.direction)<<']';
    }
    std::cout<<"],\"movement_positions_truncated\":"
        <<(route_runtime_movement_callbacks>route_runtime_movement_positions.size()?"true":"false")
        <<",\"headless_go_here_interrupt\":";
    if(headless_go_here_interrupt_tick)std::cout<<"{\"tick\":"<<*headless_go_here_interrupt_tick
        <<",\"applied\":"<<(headless_go_here_interrupt_applied?"true":"false")
        <<",\"accepted\":"<<(headless_go_here_interrupt_accepted?"true":"false")<<'}';
    else if(headless_go_here_interrupt_after_movements)
        std::cout<<"{\"after_movement_callbacks\":"<<*headless_go_here_interrupt_after_movements
        <<",\"applied\":"<<(headless_go_here_interrupt_applied?"true":"false")
        <<",\"accepted\":"<<(headless_go_here_interrupt_accepted?"true":"false")<<'}';
    else std::cout<<"null";
    std::cout<<",\"scheduled_world_mutation_applied\":"
        <<(headless_entity_move_applied?"true":"false")
        <<",\"scheduled_world_mutation\":";
    if(headless_entity_move)std::cout<<"{\"entity\":"<<headless_entity_move->id<<",\"tick\":"
        <<headless_entity_move->tick<<",\"position\":["<<headless_entity_move->position.x<<','
        <<headless_entity_move->position.y<<','<<static_cast<unsigned>(headless_entity_move->position.level)<<','
        <<static_cast<unsigned>(headless_entity_move->position.direction)<<"]}";
    else std::cout<<"null";
    std::cout<<",\"diagnostic\":";
    if(runtime_routing&&!runtime_routing->diagnostic().empty())std::cout<<json_bytes(runtime_routing->diagnostic());
    else std::cout<<"null";
    std::cout<<"},\"room_info_probe\":{";
    if(room_info_probe){
        const auto& probe=*room_info_probe;
        std::cout<<"\"evidence\":\"current_avatar_position_and_complete_room_inventory\",\"profile\":\"immutable_prepared_room_map_plus_live_World_obstacles\",\"status\":"
            <<json_bytes(odh::room_info_obstacle_index_status_name(probe.query.status))
            <<",\"reason\":"<<json_bytes(odh::room_info_obstacle_index_reason_name(probe.query.reason))
            <<",\"room\":"<<probe.room<<",\"candidate_box\":["<<probe.candidate.x1<<','<<probe.candidate.y1<<','
            <<probe.candidate.x2<<','<<probe.candidate.y2<<"],\"intersections\":[";
        for(std::size_t n=0;n<probe.query.intersections.size();++n){
            if(n)std::cout<<',';
            const auto& obstacle=probe.query.intersections[n];
            std::cout<<"{\"owner_id\":"<<obstacle.owner_id<<",\"collection\":"
                <<json_bytes(obstacle.collection==odh::RoomInfoObstacleCollection::static_set?"static":"dynamic")
                <<",\"box\":["<<obstacle.box.x1<<','<<obstacle.box.y1<<','<<obstacle.box.x2<<','<<obstacle.box.y2<<"]}";
        }
        std::cout<<"],\"room_inventory\":{\"status\":"
            <<json_bytes(odh::room_info_obstacle_index_status_name(probe.inventory.status))
            <<",\"reason\":"<<json_bytes(odh::room_info_obstacle_index_reason_name(probe.inventory.reason))
            <<",\"count\":"<<probe.inventory.obstacles.size()<<",\"obstacles\":[";
        for(std::size_t n=0;n<probe.inventory.obstacles.size();++n){
            if(n)std::cout<<',';
            const auto& obstacle=probe.inventory.obstacles[n];
            std::cout<<"{\"owner_id\":"<<obstacle.owner_id<<",\"collection\":"
                <<json_bytes(obstacle.collection==odh::RoomInfoObstacleCollection::static_set?"static":"dynamic")
                <<",\"box\":["<<obstacle.box.x1<<','<<obstacle.box.y1<<','<<obstacle.box.x2<<','<<obstacle.box.y2<<"]}";
        }
        std::cout<<"]}";
        if(probe.attempt_walk){
            const auto& attempt=*probe.attempt_walk;
            const auto& snapshot=attempt.snapshot;
            std::cout<<",\"attempt_walk_snapshot\":{\"evidence\":\"live_World_RoomInfo_plus_prepared_RoomObs_WallObs_and_saved_Portal15\",\"state\":\""
                <<(snapshot.state==odh::AttemptWalkObstacleState::ready?"ready":"unsupported")
                <<"\",\"reason\":"<<json_bytes(odh::attempt_walk_obstacle_reason_name(snapshot.reason))
                <<",\"architecture_complete\":"<<(attempt.architecture_complete?"true":"false")
                <<",\"portal_inventory_complete\":"<<(attempt.portal_inventory_complete?"true":"false")
                <<",\"avatar_consideration_complete\":"<<(attempt.avatar_consideration_complete?"true":"false")
                <<",\"avatar_consideration_mode\":\"isolated_new_root_saved_threads_dormant_empty_set\""
                <<",\"other_avatars_in_room\":"<<attempt.other_avatars_in_room
                <<",\"portal_openings\":"<<attempt.portal_openings
                <<",\"portal_edges\":"<<attempt.portal_edges.size()
                <<",\"room_obstacle_rectangles\":"<<attempt.room_obstacle_rectangles
                <<",\"wall_obstacle_rectangles\":"<<attempt.wall_obstacle_rectangles
                <<",\"roughbalanced_architecture_rectangles\":"<<attempt.roughbalanced_architecture_rectangles
                <<",\"entity_iteration_order_matches_reference\":true"
                <<",\"entity_order_matches_reference\":false"
                <<",\"entity_obstacle_order_matches_reference\":"<<(attempt.entity_obstacle_order_matches_reference?"true":"false")
                <<",\"obstacle_rectangles\":"<<snapshot.obstacles.size()
                <<",\"entity_obstacle_ids\":[";
            for(std::size_t n=0;n<snapshot.entity_obstacle_ids.size();++n){
                if(n)std::cout<<',';
                std::cout<<snapshot.entity_obstacle_ids[n];
            }
            std::cout<<"],\"entry_point5_calls\":"<<snapshot.entry_point5_calls
                <<",\"entry_point5_exemptions\":"<<snapshot.exempted_by_entry_point5
                <<",\"start_intersects\":"<<(snapshot.start_intersects?"true":"false")
                <<",\"route_clearance\":false,\"runtime_door_state_validated\":false,\"main_movement_integrated\":false}";
        }
        if(probe.attempt_walk_destination){
            const bool optimize_lines_applied=probe.attempt_walk_lines&&
                probe.attempt_walk_lines->status==odh::RectRouteStatus::found;
            std::cout<<",\"attempt_walk_route_diagnostic\":{\"evidence\":\"live_World_AttemptWalk_snapshot_and_pinned_corridor_model\",\"goal_selection_validated\":false,\"optimize_lines_applied\":"<<(optimize_lines_applied?"true":"false")
                <<",\"optimize_lines_evidence\":\"bounded_pinned_Simitone_geometry_only\",\"incoming_direction_supplied\":false,\"incoming_direction_context\":\"not_reconstructed\",\"incoming_direction_applied\":"<<(probe.attempt_walk_lines&&probe.attempt_walk_lines->incoming_direction_applied?"true":"false")
                <<",\"route_clearance\":false,\"runtime_door_state_validated\":false,\"main_movement_integrated\":false,\"entity_iteration_order_matches_reference\":true,\"entity_order_matches_reference\":false,\"entity_obstacle_order_matches_reference\":"<<(probe.attempt_walk&&probe.attempt_walk->entity_obstacle_order_matches_reference?"true":"false")<<",\"status\":"
                <<json_bytes(probe.attempt_walk_route_state)<<",\"destination\":["
                <<probe.attempt_walk_destination->x<<','<<probe.attempt_walk_destination->y<<']';
            if(probe.attempt_walk_room_route){
                const auto& route=*probe.attempt_walk_room_route;
                std::cout<<",\"portal_route\":{\"planner\":\"room_graph_only_no_BHAV_or_within_room_movement\",\"status\":"
                    <<json_bytes(room_portal_route_status_name(route.status))
                    <<",\"expanded\":"<<route.expanded<<",\"cost\":"<<route.cost<<",\"sequence\":[";
                for(std::size_t n=0;n<route.portals.size();++n){
                    if(n)std::cout<<',';
                    const auto& portal=route.portals[n];
                    std::cout<<"{\"portal_id\":"<<portal.portal_id
                        <<",\"source_room\":"<<portal.source_room
                        <<",\"target_room\":"<<portal.target_room
                        <<",\"position\":["<<portal.position.x<<','<<portal.position.y
                        <<','<<portal.position.level<<"]}";
                }
                std::cout<<"]}";
            }else std::cout<<",\"portal_route\":null";
            if(probe.attempt_walk_route){
                const auto& route=*probe.attempt_walk_route;
                std::cout<<",\"generated_rectangles\":"<<route.generated_rectangles
                    <<",\"expanded_rectangles\":"<<route.expanded_rectangles
                    <<",\"work_units\":"<<route.work_units<<",\"corridor\":[";
                for(std::size_t n=0;n<route.corridor.size();++n){
                    if(n)std::cout<<',';
                    const auto& step=route.corridor[n];
                    std::cout<<"{\"entry\":["<<step.entry.x<<','<<step.entry.y
                        <<"],\"rect\":["<<step.rectangle.x1<<','<<step.rectangle.y1<<','
                        <<step.rectangle.x2<<','<<step.rectangle.y2<<"]}";
                }
                std::cout<<']';
            }
            if(probe.attempt_walk_lines){
                const auto& optimized=*probe.attempt_walk_lines;
                std::cout<<",\"optimize_lines_status\":"<<json_bytes(odh::rect_route_status_name(optimized.status))
                    <<",\"optimize_lines_work_units\":"<<optimized.work_units<<",\"optimized_corridor\":[";
                for(std::size_t n=0;n<optimized.corridor.size();++n){
                    if(n)std::cout<<',';
                    const auto& step=optimized.corridor[n];
                    std::cout<<"{\"entry\":["<<step.entry.x<<','<<step.entry.y
                        <<"],\"entry_hip\":["<<step.entry_hip.x<<','<<step.entry_hip.y
                        <<"],\"line_id\":"<<step.line_id<<",\"rect\":["
                        <<step.rectangle.x1<<','<<step.rectangle.y1<<','
                        <<step.rectangle.x2<<','<<step.rectangle.y2<<"]}";
                }
                std::cout<<']';
            }else std::cout<<",\"optimize_lines_status\":"<<json_bytes(probe.attempt_walk_lines_state)
                <<",\"optimized_corridor\":null";
            std::cout<<'}';
        }
        std::cout<<'}';
    }else std::cout<<"\"evidence\":\"unavailable\",\"reason\":"<<json_bytes(room_info_probe_unavailable)<<'}';
    if(placement_candidate_probe){
        const auto& probe=*placement_candidate_probe;
        const auto floor_gate=!probe.floor_result?odh::CandidateCellGateStatus::unsupported:
            *probe.floor_result==odh::FloorPlacementResult::allowed?odh::CandidateCellGateStatus::allowed:
            odh::CandidateCellGateStatus::blocked;
        const auto overlap_gate=!probe.overlap_audit?odh::CandidateCellGateStatus::unsupported:
            probe.overlap_audit->rules.status==odh::SavedAvatarPlaceAuditStatus::allowed?odh::CandidateCellGateStatus::allowed:
            probe.overlap_audit->rules.status==odh::SavedAvatarPlaceAuditStatus::blocked?odh::CandidateCellGateStatus::blocked:
            odh::CandidateCellGateStatus::unsupported;
        const auto snapshot_gate=!probe.architecture_audit?odh::CandidateCellGateStatus::unsupported:
            probe.architecture_audit->snapshot.state==odh::AttemptWalkObstacleState::ready?odh::CandidateCellGateStatus::allowed:
            odh::CandidateCellGateStatus::unsupported;
        const auto cell_gate=!probe.inside_lot?odh::CandidateCellGateStatus::blocked:
            !probe.architecture_walkable?odh::CandidateCellGateStatus::unsupported:
            *probe.architecture_walkable?odh::CandidateCellGateStatus::allowed:
            odh::CandidateCellGateStatus::blocked;
        const auto candidate_preflight=odh::evaluate_avatar_candidate_preflight(
            {floor_gate,overlap_gate,snapshot_gate,cell_gate});
        const bool read_only_bhav_executed=std::any_of(entry_point5_bhav_evidence.begin(),entry_point5_bhav_evidence.end(),
            [](const auto& evidence){return evidence.steps!=0;});
        std::cout<<",\"placement_candidate_probe\":{\"evidence\":\"restored_World_floor_RoomInfo_and_bounded_read_only_EP5_probe\",\"candidate_probe_moves_avatar\":false,\"bhav_executed\":"<<(read_only_bhav_executed?"true":"false")<<",\"bhav_execution_scope\":\"explicit_resource_owner; exact_read_only_allowlist; World_writes_denied; unsupported_primitives_fail_closed; ambiguous_ghost_fails_closed\",\"placement_decision\":\"not_evaluated\",\"route_clearance\":false"
            <<",\"position\":["<<probe.position.x<<','<<probe.position.y<<','<<probe.position.level<<','<<unsigned(probe.position.direction)<<']'
            <<",\"inside_lot\":"<<(probe.inside_lot?"true":"false")<<",\"room\":";
        if(probe.room)std::cout<<*probe.room;else std::cout<<"null";
        std::cout<<",\"candidate_box\":";
        if(probe.box)std::cout<<'['<<probe.box->x1<<','<<probe.box->y1<<','<<probe.box->x2<<','<<probe.box->y2<<']';
        else std::cout<<"null";
        std::cout<<",\"floor\":";if(probe.floor)std::cout<<*probe.floor;else std::cout<<"null";
        std::cout<<",\"placement_flags\":";if(probe.placement_flags)std::cout<<*probe.placement_flags;else std::cout<<"null";
        std::cout<<",\"level_offset\":";if(probe.level_offset)std::cout<<*probe.level_offset;else std::cout<<"null";
        std::cout<<",\"floor_change_valid\":";
        if(probe.floor_result)std::cout<<json_bytes(floor_result_name(*probe.floor_result));else std::cout<<"null";
        std::cout<<",\"candidate_cell_preflight\":{\"scope\":\"candidate_cell_only_not_route_or_final_placement\",\"status\":"
            <<json_bytes(odh::avatar_candidate_preflight_status_name(candidate_preflight.status))
            <<",\"blocked_gates\":"<<unsigned(candidate_preflight.blocked_gates)
            <<",\"unsupported_gates\":"<<unsigned(candidate_preflight.unsupported_gates)
            <<",\"gates\":{\"floor\":"<<json_bytes(odh::candidate_cell_gate_status_name(floor_gate))
            <<",\"entity_overlap\":"<<json_bytes(odh::candidate_cell_gate_status_name(overlap_gate))
            <<",\"attempt_walk_snapshot\":"<<json_bytes(odh::candidate_cell_gate_status_name(snapshot_gate))
            <<",\"candidate_cell\":"<<json_bytes(odh::candidate_cell_gate_status_name(cell_gate))<<"}}";
        std::cout<<",\"floor_state\":";
        if(!probe.floor_result)std::cout<<"\"unknown\"";
        else std::cout<<json_bytes(*probe.floor_result==odh::FloorPlacementResult::allowed?"allowed":"rejected");
        std::cout<<",\"floor_reason\":"<<json_bytes(probe.floor_reason)
            <<",\"room_info_status\":";
        if(probe.query)std::cout<<json_bytes(odh::room_info_obstacle_index_status_name(probe.query->status));
        else std::cout<<"\"unavailable\"";
        std::cout<<",\"room_info_reason\":";
        if(probe.query)std::cout<<json_bytes(odh::room_info_obstacle_index_reason_name(probe.query->reason));
        else std::cout<<json_bytes(probe.room_info_reason.empty()?placement_candidate_probe_unavailable:probe.room_info_reason);
        std::cout<<",\"intersections\":[";
        if(probe.query)for(std::size_t n=0;n<probe.query->intersections.size();++n){
            if(n)std::cout<<',';
            const auto& obstacle=probe.query->intersections[n];
            std::cout<<"{\"owner_id\":"<<obstacle.owner_id<<",\"collection\":"
                <<json_bytes(obstacle.collection==odh::RoomInfoObstacleCollection::static_set?"static":"dynamic")
                <<",\"box\":["<<obstacle.box.x1<<','<<obstacle.box.y1<<','<<obstacle.box.x2<<','<<obstacle.box.y2<<"]}";
        }
        std::cout<<']';
        std::cout<<",\"architecture_walkability_audit\":{\"evidence\":\"candidate_position_live_AttemptWalk_snapshot_and_bounded_local_grid\",\"candidate_probe_moves_avatar\":false,\"status\":"
            <<json_bytes(probe.architecture_walkability_state)<<",\"reason\":"
            <<json_bytes(probe.architecture_walkability_reason)<<",\"candidate_walkable\":";
        if(probe.architecture_walkable)std::cout<<(*probe.architecture_walkable?"true":"false");
        else std::cout<<"null";
        if(probe.architecture_audit){
            const auto& attempt=*probe.architecture_audit;
            const auto& snapshot=attempt.snapshot;
            std::cout<<",\"snapshot_state\":"<<json_bytes(snapshot.state==odh::AttemptWalkObstacleState::ready?"ready":"unsupported")
                <<",\"snapshot_reason\":"<<json_bytes(odh::attempt_walk_obstacle_reason_name(snapshot.reason))
                <<",\"architecture_complete\":"<<(attempt.architecture_complete?"true":"false")
                <<",\"portal_inventory_complete\":"<<(attempt.portal_inventory_complete?"true":"false")
                <<",\"other_avatars_in_room\":"<<attempt.other_avatars_in_room
                <<",\"obstacle_rectangles\":"<<snapshot.obstacles.size()
                <<",\"entity_obstacle_ids\":[";
            for(std::size_t n=0;n<snapshot.entity_obstacle_ids.size();++n){
                if(n)std::cout<<',';
                std::cout<<snapshot.entity_obstacle_ids[n];
            }
            std::cout<<"],\"start_intersects\":"<<(snapshot.start_intersects?"true":"false")
                <<",\"entry_point5_calls\":"<<snapshot.entry_point5_calls
                <<",\"entry_point5_exemptions\":"<<snapshot.exempted_by_entry_point5;
        }
        std::cout<<'}';
        std::cout<<",\"same_room_route_audit\":{\"evidence\":\"actual_actor_position_to_candidate_using_fresh_live_AttemptWalk_snapshot\",\"candidate_probe_moves_avatar\":false,\"scope\":"
            <<json_bytes(probe.route_audit_scope)<<",\"status\":"<<json_bytes(probe.route_audit_state)
            <<",\"reason\":"<<json_bytes(probe.route_audit_reason)<<",\"start\":";
        if(probe.route_audit_start)std::cout<<'['<<probe.route_audit_start->x<<','<<probe.route_audit_start->y<<']';
        else std::cout<<"null";
        std::cout<<",\"goal\":";
        if(probe.route_audit_goal)std::cout<<'['<<probe.route_audit_goal->x<<','<<probe.route_audit_goal->y<<']';
        else std::cout<<"null";
        std::cout<<",\"route_clearance\":false,\"path_cost\":";
        if(probe.route_audit_path&&probe.route_audit_path->status==odh::RouteStatus::found)
            std::cout<<probe.route_audit_path->cost;
        else std::cout<<"null";
        std::cout<<",\"path_points\":";
        if(probe.route_audit_path&&probe.route_audit_path->status==odh::RouteStatus::found)
            std::cout<<probe.route_audit_path->points.size();
        else std::cout<<"null";
        std::cout<<"}";
        if(probe.overlap_audit){
            const auto& audit=*probe.overlap_audit;
            const char* status=audit.rules.status==odh::SavedAvatarPlaceAuditStatus::allowed?"allowed":
                audit.rules.status==odh::SavedAvatarPlaceAuditStatus::blocked?"blocked":"unsupported";
            std::cout<<",\"entity_overlap_policy_audit\":{\"evidence\":\"live_World_flags_and_bounded_read_only_EP5\",\"scope\":\"RoomInfo_entity_overlaps_only\",\"all_avatars_solid\":false,\"group_source\":\"restored_save_snapshot\",\"bhav_executed\":"<<(read_only_bhav_executed?"true":"false")<<",\"bhav_execution_scope\":\"explicit_resource_owner; exact_read_only_allowlist; World_writes_denied; unsupported_primitives_fail_closed; ambiguous_ghost_fails_closed\",\"status\":"<<json_bytes(status)
                <<",\"reason\":"<<json_bytes(odh::world_avatar_place_audit_reason_name(audit.reason))
                <<",\"pair_classifications\":"<<audit.rules.pair_classifications
                <<",\"final_blocker_id\":"<<audit.rules.final_blocker_id
                <<",\"early_stop_known\":";
            if(audit.rules.early_stop_known)std::cout<<(*audit.rules.early_stop_known?"true":"false");
            else std::cout<<"null";
            std::cout<<'}';
        }
        std::cout<<",\"entry_point5_bhav_evidence\":[";
        for(std::size_t i=0;i<entry_point5_bhav_evidence.size();++i){
            if(i)std::cout<<',';
            const auto& evidence=entry_point5_bhav_evidence[i];
            std::cout<<"{\"caller\":"<<evidence.caller<<",\"stack_object\":"<<evidence.stack_object
                <<",\"action\":"<<evidence.action<<",\"source\":"<<json_bytes(evidence.source)
                <<",\"asset\":"<<json_bytes(evidence.asset)<<",\"status\":"<<json_bytes(evidence.status)
                <<",\"result\":";
            if(evidence.result)std::cout<<(*evidence.result?"true":"false");else std::cout<<"null";
            std::cout<<",\"steps\":"<<evidence.steps<<",\"reason\":"<<json_bytes(evidence.reason)<<'}';
        }
        std::cout<<']';
        std::cout<<'}';
    }else if(placement_candidate)std::cout<<",\"placement_candidate_probe\":{\"evidence\":\"unavailable\",\"reason\":"
        <<json_bytes(placement_candidate_probe_unavailable)<<'}';
    std::cout<<",\"status_hud\":";
    if(captured_status_hud){const auto& hud=*captured_status_hud;
        std::cout<<"{\"profile\":\"numeric_portable_v2\",\"actor\":"<<hud.actor<<",\"clock\":";
        if(hud.clock)std::cout<<"{\"hour\":"<<unsigned(hud.clock->hour)<<",\"minute\":"<<unsigned(hud.clock->minute)<<'}';
        else std::cout<<"null";
        std::cout<<",\"needs\":";
        if(hud.needs){std::cout<<'[';for(std::size_t i=0;i<odh::ts1_need_count;++i){if(i)std::cout<<',';std::cout<<(*hud.needs)[i];}std::cout<<']';}
        else std::cout<<"null";
        std::cout<<",\"room_score\":"<<(hud.room_score?std::to_string(*hud.room_score):"null");
        std::cout<<'}';
    }else std::cout<<"null";
    std::cout<<",\"render\":";
    if(render_evidence){const auto& v=*render_evidence;
        std::cout<<"{\"kind\":"<<json_bytes(render_window?"windowed_Main_Playback_preview":"headless_Main_Playback_sample")<<",\"output\":"<<(render_output?json_bytes(*render_output):"null")
            <<",\"window_clock\":"<<(render_window?json_bytes("preview_25Hz_unvalidated"):"null")
            <<",\"runtime_position_present\":"<<(saved_world_main?"true":"false")<<",\"presentation_position_source\":"<<json_bytes(saved_world_main?(presented_runtime_entity_id&&!presented_saved_id?"prepared_World_avatar":"restored_World_entity_position"):presented_saved_id?"saved_ObjM_projection":"diagnostic_anchor")
            <<",\"presentation_saved_object_id\":"<<(presented_saved_id?std::to_string(*presented_saved_id):"null")
            <<",\"presentation_saved_guid\":"<<(presented_saved_guid?std::to_string(*presented_saved_guid):"null")
            <<",\"presentation_runtime_entity_id\":"<<(presented_runtime_entity_id?std::to_string(*presented_runtime_entity_id):"null")
            <<",\"presentation_avatar_guid\":"<<(presented_avatar_guid?std::to_string(*presented_avatar_guid):"null")
            <<",\"presentation_position\":["<<render_position[0]<<','<<render_position[1]<<','<<render_position[2]<<"],\"presentation_direction\":"<<unsigned(render_direction)
            <<",\"camera_center_tile\":["<<render_camera.center_tile[0]<<','<<render_camera.center_tile[1]<<','<<render_camera.center_tile[2]<<"],\"tick\":"<<v.tick<<",\"actor\":"<<v.actor<<",\"actor_generation\":"<<v.actor_generation
            <<",\"vm_generation\":"<<v.vm_generation<<",\"clip\":"<<json_bytes(v.clip)<<",\"frame\":"<<v.frame
            <<",\"width\":"<<render_camera.width<<",\"height\":"<<render_camera.height
            <<",\"vertices\":"<<v.vertices<<",\"triangles\":"<<v.triangles
            <<",\"texture_bytes\":"<<v.texture_bytes<<",\"raster_tested_samples\":"<<v.tested_samples<<",\"catalog_cfp_samples\":"<<v.cfp_samples
            <<",\"renderer_cfp_cache_samples\":0,\"shaded_samples\":"<<v.shaded_samples
            <<",\"actor_raster_pixels\":"<<v.actor_raster_pixels
            <<",\"actor_composited_pixels\":"<<v.actor_composited_pixels
            <<",\"lot_composited\":"<<(v.lot_composited?"true":"false")
            <<",\"lot_objects_included\":"<<(v.lot_objects_included?"true":"false")
            <<",\"object_selection\":{\"clicks\":"<<render_selection_clicks<<",\"tick\":"<<render_selection_tick
            <<",\"headless_object_click_pixel\":";
        if(headless_window_object_click_pixel)
            std::cout<<'['<<(*headless_window_object_click_pixel)[0]<<','<<(*headless_window_object_click_pixel)[1]<<']';
        else std::cout<<"null";
        std::cout<<",\"id\":"<<(render_selection?std::to_string(render_selection->id):"null")
            <<",\"guid\":"<<(render_selection?std::to_string(render_selection->guid):"null")
            <<",\"generation\":"<<(render_selection?std::to_string(render_selection->generation):"null")
            <<",\"tree_table_id\":"<<(render_selection?std::to_string(render_selection->tree_table_id):"null")
            <<",\"interaction_requested\":"<<(render_interaction_requested?"true":"false")
            <<",\"interaction_status\":"<<json_bytes(render_interaction_status)
            <<",\"interaction_diagnostic\":"<<json_bytes(render_interaction_diagnostic)
            <<",\"interaction_action\":"<<(render_interaction_action?std::to_string(*render_interaction_action):"null")
            <<",\"interaction_code_owner\":"<<(render_interaction_owner?std::to_string(*render_interaction_owner):"null")
            <<",\"interaction_target\":"<<(render_interaction_target?std::to_string(render_interaction_target->id):"null")
            <<",\"interaction_target_generation\":"<<(render_interaction_target?std::to_string(render_interaction_target->generation):"null")
            <<",\"interaction_target_guid\":"<<(render_interaction_target?std::to_string(render_interaction_target->guid):"null")
            <<",\"interaction_result\":"<<(render_interaction_result?(*render_interaction_result?"true":"false"):"null")
            <<",\"interaction_discovery\":";
        if(!render_interactions)std::cout<<"null";
        else {
            const auto& discovery=*render_interactions;
            const char* state=discovery.state==SavedObjectInteractions::State::no_tree_table_id?"no_tree_table_id":
                discovery.state==SavedObjectInteractions::State::missing_ttab?"missing_ttab":"found";
            std::cout<<"{\"evidence\":\"static_resource_lookup_only\",\"state\":"<<json_bytes(state)
                <<",\"owner\":{\"source\":"<<json_bytes(discovery.source)<<",\"asset\":"<<json_bytes(discovery.asset)<<'}'
                <<",\"table_id\":"<<discovery.table_id<<",\"labels_state\":"
                <<json_bytes(discovery.labels.state==odh::StringTableState::decoded?"decoded":
                    discovery.labels.state==odh::StringTableState::header_only?"header_only":"absent")
                <<",\"ttab_status\":";
            if(!discovery.table)std::cout<<"null";
            else std::cout<<json_bytes(discovery.table->status==odh::TtabStatus::decoded?"decoded":
                discovery.table->status==odh::TtabStatus::empty?"empty":
                discovery.table->status==odh::TtabStatus::legacy_unsupported?"legacy_unsupported":"absent_payload");
            std::cout<<",\"interactions\":[";
            if(discovery.table)for(std::size_t n=0;n<discovery.table->interactions.size();++n){
                if(n)std::cout<<',';const auto& row=discovery.table->interactions[n];
                const auto* test=n<render_test_results.size()?&render_test_results[n]:nullptr;
                std::cout<<"{\"action\":"<<row.action<<",\"test\":"<<row.test<<",\"flags\":"<<row.flags
                    <<",\"label_index\":"<<row.label_index<<",\"label\":";
                if(n<discovery.row_labels.size()&&discovery.row_labels[n])std::cout<<json_bytes(*discovery.row_labels[n]);
                else std::cout<<"null";
                std::cout<<",\"eligibility_evidence\":\"isolated_read_only_ODH_VM\",\"eligibility\":";
                if(!test)std::cout<<"\"unsupported\",\"test_steps\":0,\"test_diagnostic\":\"TestFunction not evaluated\"";
                else {
                    std::cout<<json_bytes(test->state==SavedTestFunctionResult::State::eligible?"eligible":
                        test->state==SavedTestFunctionResult::State::ineligible?"ineligible":"unsupported")
                        <<",\"test_steps\":"<<test->steps<<",\"test_diagnostic\":"<<json_bytes(test->diagnostic);
                }
                std::cout<<'}';
            }
            std::cout<<"]}";
        }
        std::cout<<",\"menu_script\":";
        if(headless_window_menu_steps.empty())std::cout<<"null";
        else {
            const auto current_path=current_pie_menu_path_labels();
            std::cout<<"{\"complete\":"<<(headless_window_menu_script_complete?"true":"false")
                <<",\"steps_completed\":"<<headless_window_menu_step_index
                <<",\"enqueued\":"<<(render_interaction_enqueued?"true":"false")
                <<",\"path_history\":[";
            for(std::size_t path=0;path<headless_window_menu_path_history.size();++path){
                if(path)std::cout<<',';
                std::cout<<'[';
                for(std::size_t item=0;item<headless_window_menu_path_history[path].size();++item){
                    if(item)std::cout<<',';
                    std::cout<<json_bytes(headless_window_menu_path_history[path][item]);
                }
                std::cout<<']';
            }
            std::cout<<"],\"current_path\":[";
            for(std::size_t item=0;item<current_path.size();++item){
                if(item)std::cout<<',';
                std::cout<<json_bytes(current_path[item]);
            }
            std::cout<<"]}";
        }
        std::cout<<'}'
            <<",\"lot\":";
        if(render_lot_evidence){const auto& lot=*render_lot_evidence;
            std::cout<<"{\"same_runtime_World\":true,\"selected_level\":"<<unsigned(render_lot_view.level)
                <<",\"wall_mode\":"<<json_bytes(render_lot_view.walls_down?"down":"up")
                <<",\"terrain_theme\":"<<(render_lot_view.terrain_theme?std::to_string(unsigned(*render_lot_view.terrain_theme)):"null")
                <<",\"floor_draws\":"<<lot.floor_draws<<",\"floor_triangles\":"<<lot.floor_triangles
                <<",\"floor_texture_bytes\":"<<lot.floor_texture_bytes<<",\"terrain_triangles\":"<<lot.terrain_triangles
                <<",\"terrain_reserved_bytes\":"<<lot.terrain_reserved_bytes<<",\"wall_layers\":"<<lot.wall_layers
                <<",\"wall_junctions\":"<<lot.wall_junctions<<",\"wall_cached_frame_bytes\":"<<lot.wall_cached_frame_bytes
                <<",\"wall_tested_samples\":"<<lot.wall_tested_samples<<",\"wall_shaded_samples\":"<<lot.wall_shaded_samples
                <<",\"objects\":"<<lot.objects<<",\"object_layers\":"<<lot.object_layers
                <<",\"object_offscreen_layers\":"<<lot.object_offscreen_layers
                <<",\"object_cached_frame_bytes\":"<<lot.object_cached_frame_bytes
                <<",\"object_tested_samples\":"<<lot.object_tested_samples<<",\"object_shaded_samples\":"<<lot.object_shaded_samples
                <<",\"object_hidden\":"<<lot.object_hidden<<",\"object_above_level\":"<<lot.object_above_level
                <<",\"object_outside\":"<<lot.object_outside<<",\"object_removed\":"<<lot.object_removed
                <<",\"object_pick_buffer_bytes\":"<<lot.object_pick_ids.size()*sizeof(std::uint16_t)
                <<",\"other_avatars_rendered\":false,\"objects_rendered\":"<<(v.lot_objects_included?"true":"false")<<",\"original_visual_equivalence\":false}";
        }else std::cout<<"null";
        std::cout<<'}';
    }else std::cout<<"null";
    std::cout<<"}\n";
    const bool requested_ticks_complete=session_context&&session_context->bounded_headless&&
        ticks-resumed_tick==session_policy.tick_limit();
    const int exit_code=host_failed||status==odh::VmStatus::fault||
        (!render_window&&status==odh::VmStatus::budget_exhausted)||(tick_exhausted&&!requested_ticks_complete)||instruction_exhausted||action_incomplete?3:0;
#ifdef ODH_HAVE_LINUX_SESSION_LOG
    if(host_failed||status==odh::VmStatus::fault)
        session_log.event("runtime session failed tick="+std::to_string(ticks)+" diagnostic="+
            (host_failed?host_diagnostic:vm.diagnostic()));
    if(diagnostics.truncated())session_log.event("runtime diagnostic retention truncated; see diagnostics_dropped in result JSON");
    session_log.finish(exit_code);
#endif
    return {exit_code,std::move(neighborhood_transition)};
 }catch(const std::exception& e){
#ifdef ODH_HAVE_LINUX_SESSION_LOG
    const bool log_available=session_log.enabled();const auto log_path=session_log.path();
    session_log.event(std::string("fatal exception: ")+e.what());session_log.finish(2);
    if(log_available)std::cerr<<"odh-person: log saved to "<<log_path<<'\n';
#endif
    std::cerr<<"odh-person: "<<e.what()<<'\n';return {2,std::nullopt};
 }
}

int main(int argc,char** argv) {
    for(int i=1;i<argc;++i)
        if(argv[i]&&(std::string(argv[i])=="--game-root"||std::string(argv[i])=="--mode"||std::string(argv[i])=="--load-session"))
            return odh_linux_game_entry(argc,argv);
    return odh_person_run(argc,argv,nullptr).exit_code;
}
