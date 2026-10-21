#include "odh/interaction.hpp"
#include "odh/sleep.hpp"
#include "odh/tuning.hpp"
#include "odh/lot.hpp"
#include "odh/lot_context.hpp"
#include "odh/random.hpp"
#include "odh/avatar_body.hpp"
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
#include "odh/objm.hpp"
#include "odh/playback.hpp"
#include "odh/sound.hpp"
#include "odh/look_towards.hpp"
#include "odh/world_room_info.hpp"
#include "odh/saved_attempt_walk_obstacles.hpp"
#include "runtime_attempt_walk_probe.hpp"
#include "odh/world_avatar_place_audit.hpp"
#include "odh/entry_point5_readonly.hpp"
#include "odh/floor_placement.hpp"
#include "odh/route_geometry.hpp"
#include "odh/rect_router.hpp"
#include "odh/routing.hpp"
#include "odh/world_room_route_grid.hpp"
#include "animation_catalog.hpp"
#include "resource.hpp"
#include "json.hpp"
#include "saved_tail_json.hpp"
#include "saved_appearance.hpp"
#include "saved_resources.hpp"
#include "saved_interactions.hpp"
#include "saved_wall_appearance.hpp"
#include "saved_lot_renderer.hpp"
#ifdef ODH_HAVE_X11
#include "linux_window.hpp"
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
#include <thread>

namespace {
struct Resource {std::string archive,entry;bool standalone=false;};
struct PersonDefinitionSource {std::string path,entry;bool standalone=false;};
constexpr std::size_t person_definition_source_limit=16;
template<class F>void with_resource(const Resource& resource,F read){
    if(resource.standalone)with_iff_file(resource.archive,read);else with_far_entry(resource.archive,resource.entry,read);
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
            if(role==odh::RoutineScope::private_object&&chunk.type=="GLOB"&&chunk.id==128){
                if(result.glob)throw odh::FormatError("ambiguous GLOB128");result.glob=odh::read_glob(chunk.payload);
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
}
int main(int argc,char** argv) {
 try {
    std::optional<Resource> avatar,semi,globals,action;
    std::vector<PersonDefinitionSource> definition_sources;
    std::vector<std::string> animation_sources;
    std::vector<odh::ArchitectureResourceSource> architecture_sources;
    std::optional<std::string> lot_path,zoning_path,neighborhood_path;
    std::optional<std::uint16_t> lot_id;
    std::optional<std::uint64_t> seed;
    std::optional<std::uint16_t> avatar_def,action_def,action_routine,flags,age,routine;
    std::optional<std::uint16_t> render_tick;
    std::optional<std::uint16_t> render_saved_id;
    std::optional<odh::EntityPosition> placement_candidate;
    std::optional<odh::RectRoutePoint> attempt_walk_destination;
    std::optional<std::int16_t> headless_stack_object;
    struct HeadlessEntityMove {std::int16_t id;odh::EntityPosition position;std::uint64_t tick;};
    std::optional<HeadlessEntityMove> headless_entity_move;
    bool headless_entity_move_applied=false;
    bool render_window=false,saved_world_main=false,headless_main=false;
    std::optional<std::string> render_output,render_animation_far,render_textures_far,render_skeleton_entry,render_skeleton_name;
    std::optional<std::string> render_floors,render_walls;
    SavedLotView render_lot_view;
    bool render_level_set=false,render_wall_mode_set=false;
    std::uint16_t ticks_limit=256;bool ticks_set=false,initialize=false,activate_family=false;
    for(int n=1;n<argc;++n){const std::string option=argv[n];
        if(option=="--avatar"&&!avatar&&n+3<argc){avatar=Resource{argv[n+1],argv[n+2]};avatar_def=selector(argv[n+3]);n+=3;}
        else if(option=="--avatar-file"&&!avatar&&n+2<argc){avatar=Resource{argv[n+1],"",true};avatar_def=selector(argv[n+2]);n+=2;}
        else if(option=="--semiglobal"&&!semi&&n+2<argc){semi=Resource{argv[n+1],argv[n+2]};n+=2;}
        else if(option=="--global"&&!globals&&n+2<argc){globals=Resource{argv[n+1],argv[n+2]};n+=2;}
        else if(option=="--action"&&!action&&n+4<argc){action=Resource{argv[n+1],argv[n+2]};action_def=selector(argv[n+3]);action_routine=selector(argv[n+4]);n+=4;}
        else if(option=="--ticks"&&!ticks_set&&n+1<argc){ticks_limit=selector(argv[++n]);ticks_set=true;}
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
            if(animation_sources.size()>=16)throw odh::FormatError("animation source count exceeds bounded host profile");
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
        else if(option=="--render-animation-far"&&!render_animation_far&&n+1<argc){render_animation_far=argv[++n];}
        else if(option=="--render-textures"&&!render_textures_far&&n+1<argc){render_textures_far=argv[++n];}
        else if(option=="--render-skeleton"&&!render_skeleton_entry&&n+2<argc){render_skeleton_entry=argv[n+1];render_skeleton_name=argv[n+2];n+=2;}
        else if(option=="--render-tick"&&!render_tick&&n+1<argc){render_tick=selector(argv[++n]);}
        else if(option=="--render-saved-avatar"&&!render_saved_id&&n+1<argc){render_saved_id=selector(argv[++n]);}
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
    if(render_window&&!ticks_set)ticks_limit=4096;
    if(!architecture_sources.empty()&&!render_floors)
        throw odh::FormatError("--architecture-resource requires --render-floors");
    if((render_floors&&(!saved_world_main||render_floors->empty()||render_floors->size()>4096))||
       (render_walls&&(!render_floors||render_walls->empty()||render_walls->size()>4096))||
       (render_level_set&&!render_floors)||(render_wall_mode_set&&!render_walls)||
       (render_lot_view.terrain_theme&&!render_floors))
        throw odh::FormatError("render lot options require --saved-world-main --render-floors; walls/view/theme need their explicit inputs");
    const bool render_requested=render_output||render_animation_far||render_textures_far||render_skeleton_entry||render_skeleton_name||render_tick||render_window||(render_saved_id&&!headless_main);
    if(!avatar||!semi||!globals||ticks_limit>4096||(age&&*age>32767)||(lot_id&&(*lot_id==0||*lot_id>32767))||(zoning_path&&!lot_path)||(initialize&&(routine||age||flags))||(activate_family&&(!initialize||!lot_path||!neighborhood_path))||
       (render_requested&&((!render_output&&!render_window)||(render_output&&render_window)||!render_animation_far||!render_textures_far||!render_skeleton_entry||!render_skeleton_name||(!initialize&&!saved_world_main)||animation_sources.empty()||(render_tick&&*render_tick>ticks_limit)||(render_window&&(render_tick||!render_saved_id))))||
       (render_saved_id&&((!render_output&&!render_window&&!headless_main)||!lot_path||*render_saved_id==0||*render_saved_id>32767))||
       (saved_world_main&&(!lot_path||!render_saved_id||initialize||action||(!render_requested&&!headless_main)||definition_sources.empty()))||
       (headless_main&&(!render_floors||render_requested))||
       (headless_stack_object&&!headless_main)||
       (headless_entity_move&&(!headless_main||headless_entity_move->tick>ticks_limit))||
       (placement_candidate&&(!saved_world_main||!render_saved_id||!render_floors))||
       (render_animation_far&&std::find(animation_sources.begin(),animation_sources.end(),*render_animation_far)==animation_sources.end()))
        throw odh::FormatError("usage: (--avatar FAR ENTRY OBJD | --avatar-file IFF OBJD) --semiglobal FAR ENTRY --global FAR ENTRY [--initialize | --routine ID] [--action FAR ENTRY OBJD ROUTINE] [--lot HOUSE_IFF LOT_ID] [--zoning IFF] [--neighborhood IFF] [--activate-family] [--object-definitions FAR | --object-definition-file IFF | --object-definition-entry FAR ENTRY] [--animation-source FAR] [--seed UINT64] [--flags WORD] [--age YEARS] [--ticks 0..4096] [--render-frame NEW.ppm --render-animation-far FAR --render-textures FAR --render-skeleton ENTRY NAME [--render-tick 0..TICKS] [--render-saved-avatar OBJECT_ID] | --render-window --render-saved-avatar OBJECT_ID --render-animation-far FAR --render-textures FAR --render-skeleton ENTRY NAME | --headless-main --render-saved-avatar OBJECT_ID --render-floors IFF [--headless-stack-object ID] [--headless-move-entity ID X Y LEVEL DIRECTION TICK]] [--saved-world-main --render-floors IFF [--placement-candidate X Y LEVEL DIRECTION] [--attempt-walk-destination X Y] [--architecture-resource FILE ...] [--render-walls IFF] [--render-level 1..2] [--render-wall-mode up|down] [--render-terrain-theme 0..7]]");
#ifndef ODH_HAVE_X11
    if(render_window)throw odh::FormatError("X11 window support is not built; configure ODH_X11 and X11 development files");
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
    std::optional<SavedLotMaterialOptions> render_materials;
    if(!architecture_sources.empty()){
        render_materials.emplace();
        render_materials->catalog=odh::discover_architecture_resources(architecture_sources);
    }
    std::optional<odh::Simi> simi;
    std::optional<odh::Objm> objm;std::optional<odh::LotZoning> zoning;
    std::optional<odh::Objt> objt;
    std::optional<std::vector<odh::SavedObjectPrefix>> saved_prefixes;
    std::optional<std::vector<odh::ObjmCommonState>> saved_common;
    std::optional<std::vector<std::optional<odh::ObjmTailState>>> saved_tails;
    std::optional<odh::DefinitionIndex> definition_index;
    if(!definition_sources.empty()){
        std::vector<odh::DefinitionCandidate> candidates;
        std::uint64_t provenance_available=256*1024;
        const auto add_iff=[&](const auto& view,const std::string& source,const std::string& asset){
            odh::Limits limits;limits.iff_chunks=4096;
            odh::iff_chunks(view,[&](const auto& chunk){
                if(chunk.type!="OBJD")return;
                if(candidates.size()>=8192||source.size()>provenance_available||asset.size()>provenance_available-source.size())
                    throw odh::FormatError("definition candidate/provenance budget exceeded");
                auto definition=odh::read_object_definition(chunk.payload);
                provenance_available-=source.size()+asset.size();
                candidates.push_back({source,asset,chunk.id,definition});
            },limits);
        };
        for(const auto& source:definition_sources){
            if(source.standalone){with_iff_file(source.path,[&](const auto& view){add_iff(view,source.path,"");});continue;}
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
                odh::ObjmTailLimits limits;limits.decoded_words=tail_words_available;limits.string_bytes=string_bytes_available;
                auto tail=odh::read_objm_tail(objm->blocks[n],(*saved_common)[n],objm->version,
                    definition->definition,*objt->entry_for_type(prefix.type_id),limits);
                tail_words_available-=tail.decoded_words;string_bytes_available-=tail.string_bytes;
                tails.emplace_back(std::move(tail));
            }
            saved_tails=std::move(tails);
        }
    }
    if(render_saved_id&&(!saved_prefixes||!objt))
        throw odh::FormatError("render saved avatar requires ObjM1 and objt0 in selected house");
    if(zoning_path)with_iff_file(*zoning_path,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type!="STR#"||chunk.id!=1)return;
        if(zoning)throw odh::FormatError("ambiguous zoning STR#1");zoning=odh::read_lot_zoning(odh::read_strings(chunk.payload));
    });});
    if(zoning_path&&!zoning)throw odh::FormatError("zoning STR#1 absent");
    std::optional<odh::LotContext> lot;
    if(simi)lot.emplace(static_cast<std::int16_t>(*lot_id),simi->global_data,std::move(zoning));
    enum class GlobalSource {lot,bootstrap,family};
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
    std::optional<odh::NeighborContext> neighbors;
    if(nbrs)neighbors.emplace(std::move(*nbrs));
    std::optional<odh::FamilyContext> families;
    const odh::FamilyEntry* active_family=nullptr;
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
        throw odh::FormatError("semiglobal does not match flat Pascal GLOB128 binding");
    const auto avatar_main=person.entry_points?(person.entry_points->entries.size()>1?person.entry_points->entries[1].action:std::uint16_t{0}):person.functions->main;
    const auto root=routine.value_or(avatar_main);
    if(root==0||odh::routine_key(root).scope!=odh::RoutineScope::private_object||person.routines.find(odh::routine_key(root))==person.routines.end())
        throw odh::FormatError("avatar root routine absent from explicit private owner");
    auto semi_data=load(*semi,odh::RoutineScope::semiglobal);
    person.routines.insert(semi_data.routines.begin(),semi_data.routines.end());
    auto shared=load(*globals,odh::RoutineScope::global);
    const auto person_sounds=load_sounds(*avatar);
    const auto semiglobal_sounds=load_sounds(*semi);
    const auto global_sounds=load_sounds(*globals);
    std::optional<odh::AvatarBodyStrings> body;
    std::optional<odh::StringTable> body_walk,body_swim,private_walk;
    if(initialize||saved_world_main){
        if(initialize&&(!person.entry_points||person.entry_points->entries.size()<=8))throw odh::FormatError("startup requires entry points0/1/8");
        if(!person.startup)throw odh::FormatError("avatar body derivation requires startup OBJD fields");
        auto strings=load_strings(*avatar,{person.startup->body_strings,150});
        const auto table=strings.find(person.startup->body_strings);
        if(table==strings.end())throw odh::FormatError("startup BodySTR absent");
        body=odh::parse_avatar_body_strings(table->second);
        if(body->avatar_type!="adult")throw odh::FormatError("startup constructor profile requires adult BodySTR type");
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
    std::int16_t runtime_avatar_id=1;
    if(saved_world_main){
        if(!definition_index)throw odh::FormatError("saved World Main requires explicit object definitions");
        auto records=saved_world_records(*lot_path,*definition_index);
        const auto saved=std::find_if(records.begin(),records.end(),[&](const auto& record){return record.saved.object_id==*render_saved_id;});
        if(saved==records.end()||!saved->tail.person||saved->serialized_object_type!=2||saved->saved.guid!=person.binding->guid)
            throw odh::FormatError("selected saved World entity is not the matching person resource");
        restored_world=odh::make_saved_world(records,*definition_index,neighbors?&*neighbors:nullptr);
        // Bind the exact immutable OBJD/OBJf tables to every restored entity.
        // This is static metadata only; action function IDs are not BHAV results.
        std::map<std::uint32_t,std::vector<odh::EntryPoint>> entry_point_cache;
        for(const auto& record:records){
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
                auto resource=saved_entry_point_resource(*candidate);
                if(resource.chunk_id!=candidate->chunk_id||resource.table.entries.size()>256)
                    throw odh::FormatError("saved World EntryPoint resource identity or size invalid");
                cached=entry_point_cache.emplace(entity->definition->guid,std::move(resource.table.entries)).first;
            }
            if(!restored_world->world->define_entry_points(id,cached->second))
                throw odh::FormatError("saved World EntryPoint binding exceeds World limits");
        }
        runtime_avatar_id=static_cast<std::int16_t>(*render_saved_id);
        const auto* restored_avatar=restored_world->world->find(runtime_avatar_id);
        if(!restored_avatar||restored_avatar->kind!=odh::EntityKind::avatar||!restored_avatar->position)
            throw odh::FormatError("selected saved World avatar is not positioned in the restored World");
    }else world_owner=std::make_unique<odh::World>(odh::WorldLimits{},initialize?odh::AttributePolicy::ts1_bounded:odh::AttributePolicy::fixed);
    odh::World& world=saved_world_main?*restored_world->world:*world_owner;
    if(!saved_world_main){
        if(!world.create(runtime_avatar_id,odh::EntityKind::avatar,{0,101,3,16})||!world.define_type(runtime_avatar_id,person.binding->guid))throw odh::FormatError("avatar fixture construction failed");
        // Explicit reference constructor subset, not completed native Init.
        for(std::int16_t index=0;index<79;++index) {
            if(index==1||index==2||index==3||index==11||index==25||index==26||index==29||index==41||index==62||index==67)continue;
            if(!world.declare_object_word(runtime_avatar_id,index,0))throw odh::FormatError("reference constructor object word failed");
        }
        if(!world.declare_object_word(runtime_avatar_id,8,signed_word(flags.value_or(256))))throw odh::FormatError("flags fixture construction failed");
        if(age&&!world.set_person_data(runtime_avatar_id,58,static_cast<std::int16_t>(*age)))throw odh::FormatError("age fixture construction failed");
    }
    if(person.entry_points&&!world.define_entry_points(runtime_avatar_id,person.entry_points->entries))throw odh::FormatError("avatar entry point binding failed");
    odh::TuningResources tuning_resources;
    tuning_resources.globals=std::move(shared.tuning);
    tuning_resources.owners.emplace(runtime_avatar_id,odh::TuningOwner{std::move(person.tuning),std::move(semi_data.tuning),odh::TuningSemiState::loaded});
    if(action)tuning_resources.owners.emplace(2,odh::TuningOwner{std::move(action_data->tuning),{},odh::TuningSemiState::absent});
    const odh::BconTuning tuning(std::move(tuning_resources));
    std::vector<odh::TuningValue> tuning_reads;
    const auto world_memory=world.vm_memory();auto memory=world_memory;
    memory.read=[&](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t index)->std::optional<std::int16_t> {
        if(scope==32){const auto value=neighbors?neighbors->person_data(frame.context.stack_object,index):std::nullopt;
            if(!value)return {};
            if(neighbor_reads.size()>=20000)throw odh::FormatError("neighbor read log exceeds bounded host profile");
            neighbor_reads.push_back(*value);return value->value;}
        if(scope==6){
            const auto runtime=bootstrap_globals?bootstrap_globals->read(index):std::nullopt;
            const auto family_value=index==9?family_global:std::nullopt;
            const auto value=family_value?family_value:runtime?runtime:(lot?lot->read(index):std::nullopt);
            if(value){if(global_reads.size()>=20000)throw odh::FormatError("global read log exceeds bounded host profile");global_reads.push_back({index,*value,family_value?GlobalSource::family:runtime?GlobalSource::bootstrap:GlobalSource::lot});}
            return value;}
        if(scope!=26)return world_memory.read(frame,scope,index);
        const auto value=tuning.read(frame.context.code_owner,index);
        if(!value)return {};
        if(tuning_reads.size()>=20000)throw odh::FormatError("tuning read log exceeds bounded host profile");
        tuning_reads.push_back(*value);return value->value;
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
    odh::RandomState random(seed.value_or(1));std::vector<odh::RandomRecord> random_reads;
    odh::Sleep sleep(world,[&]{++rng_requests;random.next(1);});odh::InteractionQueue queue(world,runtime_avatar_id);
    const auto random_handler=odh::random_primitive(random,memory,[&](const odh::RandomRecord& value){
        if(random_reads.size()>=10000)throw odh::FormatError("random log exceeds bounded host profile");random_reads.push_back(value);
    });
    const auto sleep_handler=sleep.primitive(),queue_handler=queue.primitive();
    const auto lot_handler=lot?lot->primitive([&](const auto& value){
        if(zoning_reads.size()>=10000)throw odh::FormatError("zoning read log exceeds bounded host profile");zoning_reads.push_back(value);
    }):odh::VmPrimitive{};
    const auto neighbor_handler=neighbors?neighbors->primitive(memory,[&](const auto& value){
        if(neighbor_next.size()>=10000)throw odh::FormatError("neighbor enumeration log exceeds bounded host profile");neighbor_next.push_back(value);
    }):odh::VmPrimitive{};
    std::vector<odh::EntityNext> entity_next;
    const auto entity_handler=odh::entity_next_primitive(world,memory,[&](const auto& value){
        if(entity_next.size()>=10000)throw odh::FormatError("entity enumeration log exceeds bounded host profile");
        entity_next.push_back(value);
    });
    std::optional<odh::RelationshipState> relationships;
    if(neighbors)relationships.emplace(*neighbors);
    std::vector<odh::RelationshipRecord> relationship_records;
    const auto relationship_handler=relationships?relationships->primitive(world,memory,[&](const auto& value){
        if(relationship_records.size()>=10000)throw odh::FormatError("relationship log exceeds bounded host profile");
        relationship_records.push_back(value);
    }):odh::VmPrimitive{};
    std::vector<odh::InventoryFind> inventory_records;
    const auto inventory_handler=ngbh?odh::inventory_find_primitive(*ngbh,world,[&](const auto& value){
        if(inventory_records.size()>=10000)throw odh::FormatError("inventory log exceeds bounded host profile");
        inventory_records.push_back(value);
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
    if(!animation_sources.empty()){
        animation_catalog.emplace(animation_sources);
        global_animation_tables=load_strings(*globals,{128,130,150,151,156,157});
        const auto bind_tables=[&](std::int16_t owner,const Resource& resource,const Loaded& data){
            const auto base=data.binding->animation_table;
            animation_table_ids.emplace(owner,base);
            private_animation_tables.emplace(owner,load_strings(resource,{base,static_cast<std::uint16_t>(base+1),129,130}));
        };
        bind_tables(runtime_avatar_id,*avatar,person);if(action)bind_tables(2,*action,*action_data);
    }
    const auto resolve_animation=[&](const odh::VmFrame& frame,std::uint32_t source,std::uint16_t id,std::optional<std::int16_t> posture)
        ->std::shared_ptr<const odh::AnimationHeader>{
        animation_resolution_diagnostic.clear();
        try{
            const auto* actor=world.find(frame.context.caller);
            if(!actor||actor->person_data.size()<=58)throw odh::FormatError("animation actor age unavailable");
            const bool child=actor->person_data[58]<18;
            const odh::StringTable* table=nullptr;
            const auto find_table=[](const auto& tables,std::uint16_t key)->const odh::StringTable*{
                const auto i=tables.find(key);return i==tables.end()?nullptr:&i->second;
            };
            if(posture&&body_state){
                auto resource=animation_catalog->resolve(body_state->walk.at(static_cast<std::size_t>(*posture)));
                const auto* header=&resource->header;
                return std::shared_ptr<const odh::AnimationHeader>(std::move(resource),header);
            }
            if(posture){table=find_table(global_animation_tables,child?151:150);id=static_cast<std::uint16_t>(*posture);}
            else if(source==0||source==65536){
                const auto owner=source==0?frame.context.code_owner:frame.context.stack_object;
                const auto tables=private_animation_tables.find(owner);const auto base=animation_table_ids.find(owner);
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
    const auto animation_handler=playback?playback->primitive():odh::VmPrimitive{};
    struct SoundRecord {odh::SoundRequest request;std::string status;};
    std::vector<SoundRecord> sound_records;
    const auto sound_handler=odh::sound_primitive(world,[&](const odh::VmFrame& frame,std::uint16_t id)
        ->std::optional<odh::SoundEvent>{
        const auto private_sound=person_sounds.find(id);
        if(frame.context.code_owner==runtime_avatar_id&&private_sound!=person_sounds.end())return private_sound->second;
        const auto semi_sound=semiglobal_sounds.find(id);
        if(semi_sound!=semiglobal_sounds.end())return semi_sound->second;
        const auto global_sound=global_sounds.find(id);
        if(global_sound!=global_sounds.end())return global_sound->second;
        return {};
    },{},[&](const odh::SoundRequest& request,const std::string& status){
        if(sound_records.size()<4096)sound_records.push_back({request,status});
    });
    const auto look_towards_handler=odh::look_towards_primitive(world);
    std::string route_runtime_state=saved_world_main?"unavailable":"not_applicable";
    std::string route_runtime_reason=saved_world_main?"runtime_route_context_not_prepared":"requires_saved_world_main";
    std::size_t route_runtime_movement_callbacks=0;
    std::vector<odh::EntityPosition> route_runtime_movement_positions;
    std::size_t route_runtime_snapshot_refreshes=0;
    std::optional<std::uint64_t> route_runtime_spatial_revision;
    std::optional<odh::EntityPosition> route_runtime_start_position;
    std::optional<odh::WorldRoomRouteGrid> runtime_route_grid;
    std::unique_ptr<odh::Routing> runtime_routing;
    odh::VmPrimitive routing_handler=[&](const auto&,const auto&,const auto&,const auto&){
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,
            "Route45 runtime unavailable: "+route_runtime_reason};
    };
    odh::Vm vm(std::move(shared.routines),memory,{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
        if(i.opcode==0)return sleep_handler(f,i,t,d);
        if(i.opcode==17)return queue_handler(f,i,t,d);
        if(i.opcode==8)return random_handler(f,i,t,d);
        if(i.opcode==1&&lot_handler)return lot_handler(f,i,t,d);
        if(i.opcode==31&&((i.operands[4]&127)<=2||(i.operands[4]&127)==4))return entity_handler(f,i,t,d);
        if(i.opcode==31&&neighbor_handler)return neighbor_handler(f,i,t,d);
        if((i.opcode==26||i.opcode==24)&&relationship_handler)return relationship_handler(f,i,t,d);
        if(i.opcode==51&&inventory_handler)return inventory_handler(f,i,t,d);
        if(i.opcode==44&&animation_handler)return animation_handler(f,i,t,d);
        if(i.opcode==23)return sound_handler(f,i,t,d);
        if(i.opcode==22)return look_towards_handler(f,i,t,d);
        if(i.opcode==45)return routing_handler(f,i,t,d);
        return odh::PrimitiveOutcome{};
    },std::move(owners));
    if(!sleep.bind(vm)||!queue.bind(vm))throw odh::FormatError("person controllers could not bind");
    if(playback&&!playback->bind(vm))throw odh::FormatError("person animation controller could not bind");
    std::vector<odh::VmTrace> trace;bool host_failed=false;std::string host_diagnostic;
    std::vector<std::string> trace_phases;
    struct StartupStage {std::string phase;std::uint16_t routine;odh::VmStatus status;std::uint64_t steps;std::string diagnostic;};
    std::vector<StartupStage> stages;
    std::uint64_t startup_steps=0;
    std::optional<odh::NeighborInheritance> inherited;
    std::optional<std::uint64_t> body_seed_before,body_seed_after;
    const auto trace_in=[&](const std::string& phase){return [&,phase](const odh::VmTrace& record){trace.push_back(record);trace_phases.push_back(phase);};};
    if(initialize){
        for(const auto& entry:std::vector<std::pair<std::string,std::uint16_t>>{{"init",person.entry_points->entries[0].action},{"dynamic_multitile",person.entry_points->entries[8].action}}){
            if(entry.second<=255){stages.push_back({entry.first,entry.second,odh::VmStatus::ready,0,"entry ID<=255 skipped by reference"});continue;}
            vm.start(odh::routine_key(entry.second),{runtime_avatar_id,runtime_avatar_id,0,static_cast<odh::CodeOwner>(runtime_avatar_id)},{0,0,0,0});
            vm.run(10000-startup_steps,trace_in(entry.first));
            stages.push_back({entry.first,entry.second,vm.status(),vm.steps(),vm.diagnostic()});
            if(vm.status()!=odh::VmStatus::returned_true&&vm.status()!=odh::VmStatus::returned_false){
                host_failed=true;host_diagnostic="startup entry did not return synchronously";break;
            }
            startup_steps+=vm.steps();
        }
    }
    if(!host_failed){
        // Stack Main before post-body initialization, then dispatch its first
        // instruction only after the body data stage has completed.
        vm.start(odh::routine_key(root),{runtime_avatar_id,runtime_avatar_id,
            headless_stack_object.value_or(static_cast<std::int16_t>(initialize?0:1)),
            static_cast<odh::CodeOwner>(runtime_avatar_id)});
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
    struct RenderEvidence {
        std::string clip;float frame=0;std::int16_t actor=0;std::uint64_t tick=0;
        std::uint64_t actor_generation=0,vm_generation=0,cfp_samples=0;
        std::uint64_t vertices=0,triangles=0,texture_bytes=0,shaded_samples=0;
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
    std::map<EntryPoint5CacheKey,odh::Bhav> entry_point5_bhav_cache;
    std::map<EntryPoint5CacheKey,std::string> entry_point5_bhav_errors;
    const odh::AttemptWalkEntryPoint5Resolver resolve_read_only_ep5=
        [&](std::int16_t caller_id,std::int16_t stack_object_id)->std::optional<bool>{
        EntryPoint5BhvEvidence evidence;
        evidence.caller=caller_id;evidence.stack_object=stack_object_id;
        const auto* caller=world.find(caller_id);
        const auto* stack_object=world.find(stack_object_id);
        if(!caller||!caller->definition||!caller->entry_points||caller->entry_points->size()<=5){
            evidence.reason="caller_entry_point5_or_definition_unavailable";
            if(entry_point5_bhav_evidence.size()<4096)entry_point5_bhav_evidence.push_back(evidence);
            return std::nullopt;
        }
        evidence.action=(*caller->entry_points)[5].action;
        const auto* definition=definition_index?definition_index->unique(caller->definition->guid):nullptr;
        if(!definition){
            evidence.reason="caller_definition_missing_or_ambiguous";
            if(entry_point5_bhav_evidence.size()<4096)entry_point5_bhav_evidence.push_back(evidence);
            return std::nullopt;
        }
        evidence.source=definition->source;evidence.asset=definition->asset;
        const EntryPoint5CacheKey key{caller->definition->guid,evidence.action};
        auto routine=entry_point5_bhav_cache.find(key);
        if(routine==entry_point5_bhav_cache.end()&&entry_point5_bhav_errors.find(key)==entry_point5_bhav_errors.end()){
            try{
                auto loaded=saved_private_entry_point_bhav(*definition,evidence.action);
                routine=entry_point5_bhav_cache.emplace(key,std::move(loaded)).first;
            }catch(const std::exception& error){entry_point5_bhav_errors.emplace(key,error.what());}
        }
        if(entry_point5_bhav_errors.find(key)!=entry_point5_bhav_errors.end()){
            evidence.reason="selected_asset_bhav_unavailable";
            if(entry_point5_bhav_evidence.size()<4096)entry_point5_bhav_evidence.push_back(evidence);
            return std::nullopt;
        }
        if(routine==entry_point5_bhav_cache.end()||!stack_object||!stack_object->definition){
            evidence.reason="stack_object_identity_unavailable";
            if(entry_point5_bhav_evidence.size()<4096)entry_point5_bhav_evidence.push_back(evidence);
            return std::nullopt;
        }
        const odh::VmFrame frame{odh::routine_key(4096),0,{caller_id,caller_id,stack_object_id,0},{},{}};
        const auto attribute16=world.read(frame,1,16);
        const auto execution=odh::evaluate_read_only_entry_point5(evidence.action,routine->second,
            stack_object->definition->guid,stack_object->definition->master_guid,attribute16);
        evidence.status=odh::read_only_entry_point5_state_name(execution.state);
        evidence.reason=execution.reason;evidence.steps=execution.steps;
        if(execution.state==odh::ReadOnlyEntryPoint5State::evaluated_true)evidence.result=true;
        else if(execution.state==odh::ReadOnlyEntryPoint5State::evaluated_false)evidence.result=false;
        if(entry_point5_bhav_evidence.size()<4096)entry_point5_bhav_evidence.push_back(evidence);
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
    std::optional<SavedObjectInteractions> render_interactions;
    std::vector<SavedTestFunctionResult> render_test_results;
    std::uint64_t render_selection_clicks=0,render_selection_tick=0;
    bool render_interaction_requested=false,render_interaction_enqueued=false,saved_owner_ids_exhausted=false;
    std::string render_interaction_status="not_requested",render_interaction_diagnostic;
    std::optional<std::uint16_t> render_interaction_action;
    std::optional<odh::CodeOwner> render_interaction_owner;
    std::optional<SavedObjectSelection> render_interaction_target;
    std::optional<bool> render_interaction_result;
    std::optional<std::uint64_t> previous_completion_uid;
    std::map<std::tuple<std::string,std::string,std::uint16_t>,odh::CodeOwner> saved_code_owners;
    odh::CodeOwner next_saved_code_owner=0x80000000U;
    const auto request_selected_interaction=[&] {
        if(render_interaction_enqueued)return;
        if(!saved_world_main||!render_selection||!render_interactions){
            render_interaction_status="unsupported";render_interaction_diagnostic="select a saved-lot object with a decoded TTAB first";return;
        }
        if(queue.size()!=0){
            render_interaction_status="rejected";render_interaction_diagnostic="manual preview action requires an empty interaction queue";return;
        }
        const auto& selected=*render_selection;
        const auto* actor=world.find(runtime_avatar_id);
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
        render_test_results=evaluate_saved_test_functions(*candidate,world,runtime_avatar_id,selected.id,
            current_tests,4096,128);
        const auto selected_row=first_eligible_saved_interaction(*render_interactions,render_test_results,actor);
        if(!selected_row){
            render_interaction_status="unsupported";render_interaction_diagnostic="no supported private TTAB row has a matching eligible ODH TestFunction";return;
        }
        const auto& row=render_interactions->table->interactions[*selected_row];
        const auto owner_key=std::make_tuple(candidate->source,candidate->asset,row.action);
        auto owner=saved_code_owners.find(owner_key);
        if(owner==saved_code_owners.end()){
            if(saved_owner_ids_exhausted){
                render_interaction_status="unsupported";render_interaction_diagnostic="saved object code-owner identity space exhausted";return;
            }
            odh::RoutineSet routines;
            try{routines=load_saved_action_routines(*candidate,row.action);}
            catch(const std::exception& error){
                render_interaction_status="unsupported";render_interaction_diagnostic=error.what();return;
            }
            const auto key=odh::routine_key(row.action);
            if(key.scope!=odh::RoutineScope::private_object||!routines.count(key)){
                render_interaction_status="unsupported";render_interaction_diagnostic="selected ActionFunction is absent from its exact private owner";return;
            }
            const auto code_owner=next_saved_code_owner;
            if(!vm.install_owner(code_owner,std::move(routines))){
                render_interaction_status="unsupported";render_interaction_diagnostic="Main VM rejected saved object code owner (duplicate, capacity or unsafe point)";return;
            }
            if(next_saved_code_owner==std::numeric_limits<odh::CodeOwner>::max())saved_owner_ids_exhausted=true;
            else ++next_saved_code_owner;
            owner=saved_code_owners.emplace(owner_key,code_owner).first;
        }
        actor=world.find(runtime_avatar_id);target=world.find(selected.id);
        if(!actor||actor->kind!=odh::EntityKind::avatar||!target||target->generation!=selected.generation||
           !target->definition||target->definition->guid!=selected.guid){
            render_interaction_status="unsupported";render_interaction_diagnostic="live actor/target identity changed while loading action owner";return;
        }
        const odh::VmContext context{runtime_avatar_id,selected.id,selected.id,owner->second};
        if(!queue.enqueue(odh::routine_key(row.action),context)){
            render_interaction_status="rejected";render_interaction_diagnostic=queue.diagnostic();return;
        }
        render_interaction_enqueued=true;render_interaction_status="queued";render_interaction_action=row.action;
        render_interaction_owner=owner->second;
        render_interaction_target=selected;
        previous_completion_uid=queue.completion()?std::optional<std::uint64_t>{queue.completion()->uid}:std::optional<std::uint64_t>{};
        render_interaction_diagnostic.clear();
    };
#ifdef ODH_HAVE_X11
    std::unique_ptr<odh_host::LinuxWindow> render_window_host;
#endif
    odh::WorldProjection render_camera;render_camera.width=320;render_camera.height=240;render_camera.center_tile={0,0,0.75f};
    std::array<float,3> render_position{0,0,0};std::uint8_t render_direction=0;
    std::optional<std::uint16_t> presented_saved_id;std::optional<std::uint32_t> presented_saved_guid;
    if(render_saved_id){
        const auto found=std::find_if(saved_prefixes->begin(),saved_prefixes->end(),[&](const auto& saved){return saved.object_id==*render_saved_id;});
        if(found==saved_prefixes->end())throw odh::FormatError("render saved avatar ID absent from selected house");
        const auto* type=objt->entry_for_type(found->type_id);
        if(found->guid!=person.binding->guid||!type||!type->metadata||type->metadata->objd_type!=2)
            throw odh::FormatError("render saved avatar identity does not match the selected person resource");
        const auto& p=found->prefix;
        if(saved_world_main){
            const auto* entity=world.find(runtime_avatar_id);
            if(!entity||!entity->position)throw odh::FormatError("render saved avatar has no live restored World position");
        }else{
            if((p.x==-16&&p.y==-16)||p.x<0||p.y<0||p.x>32767||p.y>32767||(p.level!=1&&p.level!=2)||p.object_data[1]<0||p.object_data[1]>7)
                throw odh::FormatError("render saved avatar has no supported lot position");
            render_position={static_cast<float>(p.x)/16.0f,static_cast<float>(p.y)/16.0f,static_cast<float>(p.level-1)*2.95f};
            render_direction=static_cast<std::uint8_t>(p.object_data[1]);
        }
        render_camera.center_tile={render_position[0],render_position[1],render_position[2]+0.75f};
        presented_saved_id=*render_saved_id;presented_saved_guid=found->guid;
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
        render_appearances=std::make_unique<SavedAppearanceResources>(*render_animation_far,*render_textures_far,
                                                                       *render_skeleton_entry,*render_skeleton_name);
        const auto scene_config=render_appearances->resolve_body(*body_state->body);
        const SceneCfpResolver provider=[&](const odh::AnimationHeader& selected){
            auto decoded=animation_catalog->decode(selected);
            const auto* planes=&decoded->planes;
            return std::shared_ptr<const odh::CfpPlanes>(std::move(decoded),planes);
        };
        render_renderer=std::make_unique<SceneRenderer>(scene_config,provider);
        if(render_floors){
            render_lot=std::make_unique<SavedLotRenderer>(saved_architecture_resource(*lot_path),
                *restored_world,*definition_index,*render_floors,render_walls,render_lot_view,render_materials);
        }else if(saved_world_main)render_graphics=std::make_unique<SavedGraphicsResources>(*definition_index);
        if(saved_world_main){
            const auto graphics=render_lot?render_lot->avatar(runtime_avatar_id):render_graphics->entity(world,runtime_avatar_id);
            render_position=graphics.tile_position;render_direction=graphics.direction;
            render_camera.center_tile={render_position[0],render_position[1],render_position[2]+0.75f};
        }
#ifdef ODH_HAVE_X11
        if(render_window)render_window_host=std::make_unique<odh_host::LinuxWindow>(render_camera.width,render_camera.height,
            render_lot?"OpenDollhouse Main preview: click selects object, Space/Return requests first eligible interaction, P pause, arrows rotate, +/- zoom, Escape close":
                       "OpenDollhouse Main preview: P pause, arrows rotate, +/- zoom, Escape close");
#endif
    }
    if(headless_main){
        render_lot=std::make_unique<SavedLotRenderer>(saved_architecture_resource(*lot_path),
            *restored_world,*definition_index,*render_floors,render_walls,render_lot_view,render_materials);
    }
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
                if(probe.snapshot.state!=odh::AttemptWalkObstacleState::ready)
                    route_runtime_reason=std::string("attempt_walk_snapshot_")+odh::attempt_walk_obstacle_reason_name(probe.snapshot.reason);
                else {
                    auto grid=odh::build_world_room_route_grid(probe.snapshot);
                    if(grid.state!=odh::WorldRoomRouteGridState::ready||!grid.grid)
                        route_runtime_reason=std::string("route_grid_")+odh::world_room_route_grid_reason_name(grid.reason);
                    else {
                        runtime_route_grid.emplace(std::move(grid));
                        runtime_routing=std::make_unique<odh::Routing>(world,*runtime_route_grid->grid,
                                                                       *shared.routing_slots);
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
    const auto refresh_route_obstacles=[&]() -> bool {
        const auto revision=world.spatial_revision();
        if(route_runtime_spatial_revision&&*route_runtime_spatial_revision==revision)return true;
        if(!runtime_route_grid||!runtime_route_grid->grid||!render_lot||!restored_world||!definition_index){
            route_runtime_reason="runtime_route_refresh_context_unavailable";
            return false;
        }
        const auto* actor=world.find(runtime_avatar_id);
        if(!actor||!actor->position||actor->position->level<1||actor->position->level>2){
            route_runtime_reason="runtime_route_refresh_avatar_position_unavailable";
            return false;
        }
        const auto room=render_lot->runtime_room_at(*actor->position);
        if(!room||*room!=runtime_route_grid->room_id||
           static_cast<std::uint8_t>(actor->position->level-1)!=runtime_route_grid->level){
            route_runtime_reason="runtime_route_refresh_room_or_floor_changed";
            return false;
        }
        const auto probe=build_runtime_attempt_walk_probe(*restored_world,world,*definition_index,
            render_lot->architecture_resource(),*render_lot->prepared_architecture(),*room,
            runtime_route_grid->level,runtime_avatar_id,true,{},resolve_read_only_ep5);
        if(probe.snapshot.state!=odh::AttemptWalkObstacleState::ready){
            route_runtime_reason=std::string("runtime_route_refresh_snapshot_")+
                odh::attempt_walk_obstacle_reason_name(probe.snapshot.reason);
            return false;
        }
        auto updated=odh::build_world_room_route_grid(probe.snapshot);
        if(updated.state!=odh::WorldRoomRouteGridState::ready||!updated.grid||
           updated.room_id!=runtime_route_grid->room_id||updated.level!=runtime_route_grid->level){
            route_runtime_reason=std::string("runtime_route_refresh_grid_")+
                odh::world_room_route_grid_reason_name(updated.reason);
            return false;
        }
        *runtime_route_grid->grid=std::move(*updated.grid);
        runtime_route_grid->obstacle_rectangles=updated.obstacle_rectangles;
        runtime_route_grid->rasterized_cells=updated.rasterized_cells;
        runtime_route_grid->newly_blocked_cells=updated.newly_blocked_cells;
        ++route_runtime_snapshot_refreshes;
        route_runtime_spatial_revision=world.spatial_revision();
        return true;
    };
    std::uint64_t ticks=0;
    const auto capture_frame=[&]{
        if(!render_requested||(!render_window&&render_evidence))return;
        if(!render_window&&render_tick&&ticks!=*render_tick)return;
        const auto sample=playback->sample();if(!sample)return;
        const auto* sample_actor=world.find(sample->actor);
        if(!sample_actor||sample_actor->generation!=sample->actor_generation||sample->actor!=runtime_avatar_id)
            throw odh::FormatError("person frame sample actor identity is stale or outside this host");
        if(saved_world_main){
            const auto graphics=render_lot?render_lot->avatar(sample->actor):render_graphics->entity(world,sample->actor);
            render_position=graphics.tile_position;render_direction=graphics.direction;
            render_camera.center_tile={render_position[0],render_position[1],render_position[2]+0.75f};
        }
        SceneState scene_state;scene_state.animation=sample->clip.get();scene_state.frame=sample->frame;
        scene_state.hands=sample->hands;scene_state.avatar_direction=render_direction;scene_state.avatar_tile=render_position;
        auto frame=render_renderer->render(scene_state,render_camera);
        if(render_lot){
            auto lot=render_lot->render(render_camera,frame.image,render_window);
            frame.image=std::move(lot.image);render_lot_evidence=std::move(lot);
        }
#ifdef ODH_HAVE_X11
        if(render_window_host)render_window_host->present(frame.image);
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
            render_renderer->triangles(),render_renderer->texture_bytes(),frame.image.shaded_samples};
    };
    const auto total_steps=[&]{return startup_steps+vm.steps();};
    const auto execute=[&] {
        if(host_failed)return;
        if(!queue.observe()){host_failed=true;host_diagnostic=queue.diagnostic();return;}
        vm.run(total_steps()<10000?10000-total_steps():0,trace_in(initialize?"main":"root_probe"));
        if(!queue.observe()){host_failed=true;host_diagnostic=queue.diagnostic();}
    };
    execute();
    capture_frame();
    bool window_paused=false,window_closed=false;
    auto window_deadline=std::chrono::steady_clock::now();
    while(!host_failed) {
#ifdef ODH_HAVE_X11
        if(render_window_host){
            const auto input=render_window_host->poll();
            if(input.quit){window_closed=true;break;}
            if(input.pause)window_paused=!window_paused;
            if(input.primary_click&&render_lot&&render_lot_evidence){
                render_selection=render_lot->pick(*render_lot_evidence,(*input.primary_click)[0],(*input.primary_click)[1]);
                render_interactions.reset();render_test_results.clear();
                if(!render_interaction_enqueued||queue.size()==0){
                    render_interaction_requested=false;render_interaction_enqueued=false;
                    render_interaction_status="not_requested";render_interaction_diagnostic.clear();
                    render_interaction_action.reset();render_interaction_owner.reset();render_interaction_target.reset();render_interaction_result.reset();
                }
                if(render_selection&&definition_index){
                    const auto* candidate=definition_index->unique(render_selection->guid);
                    if(candidate){
                        render_interactions=saved_object_interactions(*candidate);
                        if(render_interactions->table&&render_interactions->table->status==odh::TtabStatus::decoded){
                            const auto& rows=render_interactions->table->interactions;
                            std::vector<std::uint16_t> tests;tests.reserve(std::min<std::size_t>(rows.size(),32));
                            for(std::size_t n=0;n<std::min<std::size_t>(rows.size(),32);++n)tests.push_back(rows[n].test);
                            render_test_results=evaluate_saved_test_functions(*candidate,world,runtime_avatar_id,
                                render_selection->id,tests,4096,128);
                            for(std::size_t n=render_test_results.size();n<rows.size();++n)
                                render_test_results.push_back({SavedTestFunctionResult::State::unsupported,rows[n].test,0,
                                    "interaction row exceeds bounded click-time probe profile"});
                        }
                    }
                }
                ++render_selection_clicks;render_selection_tick=ticks;
                std::string title="OpenDollhouse Main preview: ";
                if(render_selection)title+="object "+std::to_string(render_selection->id)+" GUID "+std::to_string(render_selection->guid)+
                    " TTAB "+std::to_string(render_selection->tree_table_id);
                else title+="no object selected";
                if(render_selection){
                    const auto row=render_interactions?first_eligible_saved_interaction(
                        *render_interactions,render_test_results,world.find(runtime_avatar_id)):std::nullopt;
                    if(row){
                        title+=" | Space/Return: ";
                        if(*row<render_interactions->row_labels.size()&&render_interactions->row_labels[*row])
                            title+=*render_interactions->row_labels[*row];
                        else title+="eligible interaction";
                    }
                }
                render_window_host->set_title(title+" | P pause, arrows rotate, +/- zoom, Escape close");
            }
            if(input.action&&!input.primary_click){
                render_interaction_requested=true;
                request_selected_interaction();
                std::string title="OpenDollhouse Main preview: interaction "+render_interaction_status;
                if(render_interaction_action)title+=" BHAV"+std::to_string(*render_interaction_action);
                if(!render_interaction_diagnostic.empty())title+=" | "+render_interaction_diagnostic;
                render_window_host->set_title(title+" | P pause, arrows rotate, +/- zoom, Escape close");
            }
            render_camera.rotation=static_cast<std::uint8_t>((int(render_camera.rotation)+input.rotation%4+4)%4);
            const auto zoom=std::clamp(int(render_camera.zoom)+input.zoom,1,3);
            render_camera.zoom=static_cast<std::uint8_t>(zoom);
            if(input.rotation||input.zoom||input.redraw)capture_frame();
            const bool runtime_finished=ticks>=ticks_limit||total_steps()>=10000||
                (vm.status()!=odh::VmStatus::waiting&&vm.status()!=odh::VmStatus::yielded);
            if(runtime_finished&&!render_evidence)
                throw odh::FormatError("person window preview reached runtime limit without a live Playback sample");
            if(window_paused||runtime_finished){
                std::this_thread::sleep_for(std::chrono::milliseconds(10));window_deadline=std::chrono::steady_clock::now();continue;
            }
            // Preview pacing only; original wall-clock cadence is not established.
            window_deadline+=std::chrono::milliseconds(40);
            std::this_thread::sleep_until(window_deadline);
        }else
#endif
        if(ticks>=ticks_limit||total_steps()>=10000||
           (vm.status()!=odh::VmStatus::waiting&&vm.status()!=odh::VmStatus::yielded))break;
        ++ticks;
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
        if(runtime_routing&&runtime_routing->active()){
            if(!refresh_route_obstacles()){
                host_failed=true;
                host_diagnostic=route_runtime_reason;
                break;
            }
            const bool route_ok=runtime_routing->tick(ticks,[&](const odh::RouteMovement& movement){
                ++route_runtime_movement_callbacks;
                if(route_runtime_movement_positions.size()<128)
                    route_runtime_movement_positions.push_back(movement.position);
            });
            route_runtime_spatial_revision=world.spatial_revision();
            if(!route_ok&&runtime_routing->active()){
                host_failed=true;
                host_diagnostic=runtime_routing->diagnostic().empty()?
                    "Route45 scheduler rejected a host tick":runtime_routing->diagnostic();
                break;
            }
        }
        if(!queue.tick(ticks)||!sleep.tick(ticks)){host_failed=true;host_diagnostic=queue.diagnostic().empty()?sleep.diagnostic():queue.diagnostic();break;}
        if(playback)playback->tick(ticks,[&](const odh::PlaybackEvent& event){
            if(animation_events.size()>=10000)throw odh::FormatError("animation event log exceeds bounded host profile");
            std::uint64_t bytes=sizeof(odh::PlaybackEvent);
            for(const auto& pair:event.properties)bytes+=sizeof(pair)+pair.first.size()+pair.second.size();
            if(bytes>256*1024-animation_event_bytes)throw odh::FormatError("animation event log bytes exceed bounded host profile");
            animation_events.push_back(event);animation_event_bytes+=bytes;
        });
        execute();
        if(render_interaction_requested){
            const auto status_before=render_interaction_status;
            if(host_failed){
                render_interaction_status="host_failed";render_interaction_diagnostic=host_diagnostic;
            }else if(vm.status()==odh::VmStatus::fault){
                render_interaction_status="vm_fault";render_interaction_diagnostic=vm.diagnostic();
            }else if(queue.completion()&&(!previous_completion_uid||queue.completion()->uid!=*previous_completion_uid)){
                render_interaction_result=queue.completion()->result;
                render_interaction_status=*render_interaction_result?"returned_true":"returned_false";
            }
            if(render_window_host&&render_interaction_status!=status_before){
                std::string title="OpenDollhouse Main preview: interaction "+render_interaction_status;
                if(render_interaction_action)title+=" BHAV"+std::to_string(*render_interaction_action);
                if(!render_interaction_diagnostic.empty())title+=" | "+render_interaction_diagnostic;
                render_window_host->set_title(title+" | P pause, arrows rotate, +/- zoom, Escape close");
            }
        }
        capture_frame();
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
                            const auto live_flags=world.read(frame,3,8);
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
                }
                placement_candidate_probe=std::move(probe);
            }
        }
    }
    const auto status=vm.status();const bool tick_exhausted=!host_failed&&ticks==ticks_limit&&
        (status==odh::VmStatus::waiting||status==odh::VmStatus::yielded);
    const bool action_incomplete=action.has_value() && (!queue.completion() || queue.size()!=0);
    const bool instruction_exhausted=total_steps()>=10000 && (status==odh::VmStatus::waiting||status==odh::VmStatus::yielded||status==odh::VmStatus::budget_exhausted);
    if(render_requested&&!render_evidence){
        if(render_window&&host_failed)throw odh::FormatError("person window preview host failed before a live Playback sample: "+host_diagnostic);
        const std::string reason=render_window?(window_closed?"person window preview closed before a live Playback sample was available":
                                                              "person window preview ended before a live Playback sample was available"):
            render_tick?"person frame export observed no live Playback sample at requested tick; no frame written":
                        "person frame export observed no live Playback sample; no frame written";
        std::string failure=reason+" (VM status "+std::string(odh::vm_status_name(vm.status()))+
            (vm.diagnostic().empty()?std::string{}:"; "+vm.diagnostic());
        if(!trace.empty()){
            const auto& last=trace.back();
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
    std::cout<<"{\"evidence\":"<<json_bytes(saved_world_main?"ODH_saved_World_new_Main_entry":"ODH_native_BHAV_constructed_person_host")<<",\"initialization\":"<<json_bytes(saved_world_main?
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
            <<",\"profile\":\"CurrentFamily and global9 selected; no SelectWholeFamily/VerifyFamily or saved resident restoration\",\"members\":[";
        for(std::size_t n=0;n<v.family.member_guids.size();++n){if(n)std::cout<<',';std::cout<<v.family.member_guids[n];}std::cout<<"]}";
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
        <<",\"tick_budget_exhausted\":"<<(tick_exhausted?"true":"false")<<",\"instruction_budget_exhausted\":"<<(instruction_exhausted?"true":"false")<<",\"stack_depth\":"<<vm.stack().size()<<",\"rng_cycle_requests\":"<<rng_requests
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
    std::cout<<",\"tuning_profile\":\"explicit BCON only; no OTF or entity replacement backend; missing remains unsupported\",\"tuning_reads\":[";
    for(std::size_t n=0;n<tuning_reads.size();++n){if(n)std::cout<<',';const auto& v=tuning_reads[n];
        const auto scope=v.address.scope==odh::RoutineScope::global?"global":v.address.scope==odh::RoutineScope::semiglobal?"semiglobal":"private";
        std::cout<<"{\"owner\":"<<v.owner<<",\"scope\":"<<json_bytes(scope)<<",\"table\":"<<v.address.table<<",\"index\":"<<static_cast<unsigned>(v.address.index)
            <<",\"value\":"<<v.value<<",\"semiglobal_fallback\":"<<(v.semiglobal_fallback?"true":"false")<<'}';
    }
    std::cout<<"],\"lot\":";
    if(lot)std::cout<<"{\"house\":"<<json_bytes(*lot_path)<<",\"selected_id\":"<<*lot_id<<",\"serialized_id\":"<<simi->global_data[10]
        <<",\"simi_version\":"<<simi->version<<",\"serialized_globals\":"<<simi->global_data.size()<<",\"remaining_bytes\":"<<simi->remaining_bytes
        <<",\"profile\":\"SIMI static globals; selected lot overrides10; clock/entity fields unsupported; bootstrap globals provided separately when initialized\",\"zoning\":"<<(zoning_path?json_bytes(*zoning_path):"null")<<'}';
    else std::cout<<"null";
    std::cout<<",\"saved_objm_container\":";
    if(objm){std::uint64_t bytes=0;for(const auto& block:objm->blocks)bytes+=block.bytes.size();
        std::cout<<"{\"version\":"<<objm->version<<",\"compression\":"<<static_cast<unsigned>(objm->compression)
            <<",\"mapped_ids\":"<<objm->map_records.size()<<",\"blocks\":"<<objm->blocks.size()<<",\"owned_block_bytes\":"<<bytes
            <<",\"remaining_bytes\":"<<objm->remaining_bytes
            <<",\"known_reference_schema_decoded\":"<<(saved_tails&&std::all_of(saved_tails->begin(),saved_tails->end(),[](const auto& tail){return tail.has_value();})?"true":"false")
            <<",\"instance_state_decoded\":false,\"world_entities_restored\":"<<(saved_world_main?"true":"false")<<"}";
    }else std::cout<<"null";
    std::cout<<",\"saved_object_prefixes\":";
    if(saved_prefixes){
        std::cout<<"{\"profile\":\"owned prefix snapshot; raw objt GUID mapping; OBJD and complete instance state unresolved\",\"objt_version\":"<<objt->version
            <<",\"objt_entries\":"<<objt->entries.size()<<",\"world_entities_restored\":"<<(saved_world_main?"true":"false")<<",\"records\":[";
        for(std::size_t n=0;n<saved_prefixes->size();++n){if(n)std::cout<<',';const auto& v=(*saved_prefixes)[n];const auto& p=v.prefix;
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
    if(saved_prefixes&&definition_index)for(std::size_t n=0;n<saved_prefixes->size();++n){
        if(n)std::cout<<',';const auto& p=(*saved_prefixes)[n];const auto* matches=definition_index->candidates(p.guid);
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
    if(saved_common){
        std::cout<<'[';const auto bank=[&](const auto& values){std::cout<<'[';for(std::size_t i=0;i<values.size();++i){if(i)std::cout<<',';std::cout<<values[i];}std::cout<<']';};
        for(std::size_t n=0;n<saved_common->size();++n){if(n)std::cout<<',';const auto& s=(*saved_common)[n];
            std::cout<<"{\"object_id\":"<<(*saved_prefixes)[n].object_id<<",\"block_offset\":"<<s.block_offset
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
    if(saved_tails){
        std::cout<<'[';
        for(std::size_t n=0;n<saved_tails->size();++n){if(n)std::cout<<',';const auto& tail=(*saved_tails)[n];
            std::cout<<"{\"object_id\":"<<(*saved_prefixes)[n].object_id<<",\"status\":"
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
        <<",\"source\":"<<json_bytes(global_reads[n].source==GlobalSource::family?"family_activation":global_reads[n].source==GlobalSource::bootstrap?"runtime_bootstrap":"lot_context")<<'}';}
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
    std::cout<<"],\"motives\":[";for(std::size_t n=0;n<actor->motives.size();++n){if(n)std::cout<<',';std::cout<<actor->motives[n];}std::cout<<"]}";
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
        <<",\"movement_callbacks\":"<<route_runtime_movement_callbacks
        <<",\"snapshot_refreshes\":"<<route_runtime_snapshot_refreshes
        <<",\"snapshot_refresh_policy\":\"on_world_spatial_revision_change\""
        <<",\"active\":"<<(runtime_routing&&runtime_routing->active()?"true":"false")
        <<",\"dynamic_obstacle_refresh\":true,\"cross_room_portal_execution\":false"
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
        <<",\"scheduled_world_mutation_applied\":"
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
                <<",\"route_clearance\":false,\"runtime_door_state_validated\":false,\"main_movement_integrated\":false,\"entity_order_matches_reference\":false,\"entity_obstacle_order_matches_reference\":"<<(probe.attempt_walk&&probe.attempt_walk->entity_obstacle_order_matches_reference?"true":"false")<<",\"status\":"
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
        const bool read_only_bhav_executed=std::any_of(entry_point5_bhav_evidence.begin(),entry_point5_bhav_evidence.end(),
            [](const auto& evidence){return evidence.steps!=0;});
        std::cout<<",\"placement_candidate_probe\":{\"evidence\":\"restored_World_floor_RoomInfo_and_bounded_read_only_EP5_subset\",\"candidate_probe_moves_avatar\":false,\"bhav_executed\":"<<(read_only_bhav_executed?"true":"false")<<",\"bhav_execution_scope\":\"allowlisted_read_only_TestObjectType_and_attribute16_equality\",\"placement_decision\":\"not_evaluated\",\"route_clearance\":false"
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
        if(probe.overlap_audit){
            const auto& audit=*probe.overlap_audit;
            const char* status=audit.rules.status==odh::SavedAvatarPlaceAuditStatus::allowed?"allowed":
                audit.rules.status==odh::SavedAvatarPlaceAuditStatus::blocked?"blocked":"unsupported";
            std::cout<<",\"entity_overlap_policy_audit\":{\"evidence\":\"live_World_flags_and_bound_EntryPoint_ids\",\"scope\":\"RoomInfo_entity_overlaps_only\",\"all_avatars_solid\":false,\"group_source\":\"restored_save_snapshot\",\"bhav_executed\":"<<(read_only_bhav_executed?"true":"false")<<",\"status\":"<<json_bytes(status)
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
    std::cout<<",\"render\":";
    if(render_evidence){const auto& v=*render_evidence;
        std::cout<<"{\"kind\":"<<json_bytes(render_window?"windowed_Main_Playback_preview":"headless_Main_Playback_sample")<<",\"output\":"<<(render_output?json_bytes(*render_output):"null")
            <<",\"window_clock\":"<<(render_window?json_bytes("preview_25Hz_unvalidated"):"null")
            <<",\"runtime_position_present\":"<<(saved_world_main?"true":"false")<<",\"presentation_position_source\":"<<json_bytes(saved_world_main?"restored_World_entity_position":presented_saved_id?"saved_ObjM_projection":"diagnostic_anchor")
            <<",\"presentation_saved_object_id\":"<<(presented_saved_id?std::to_string(*presented_saved_id):"null")
            <<",\"presentation_saved_guid\":"<<(presented_saved_guid?std::to_string(*presented_saved_guid):"null")
            <<",\"presentation_position\":["<<render_position[0]<<','<<render_position[1]<<','<<render_position[2]<<"],\"presentation_direction\":"<<unsigned(render_direction)
            <<",\"camera_center_tile\":["<<render_camera.center_tile[0]<<','<<render_camera.center_tile[1]<<','<<render_camera.center_tile[2]<<"],\"tick\":"<<v.tick<<",\"actor\":"<<v.actor<<",\"actor_generation\":"<<v.actor_generation
            <<",\"vm_generation\":"<<v.vm_generation<<",\"clip\":"<<json_bytes(v.clip)<<",\"frame\":"<<v.frame
            <<",\"width\":320,\"height\":240,\"vertices\":"<<v.vertices<<",\"triangles\":"<<v.triangles
            <<",\"texture_bytes\":"<<v.texture_bytes<<",\"catalog_cfp_samples\":"<<v.cfp_samples
            <<",\"renderer_cfp_cache_samples\":0,\"shaded_samples\":"<<v.shaded_samples
            <<",\"object_selection\":{\"clicks\":"<<render_selection_clicks<<",\"tick\":"<<render_selection_tick
            <<",\"id\":"<<(render_selection?std::to_string(render_selection->id):"null")
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
                <<",\"object_cached_frame_bytes\":"<<lot.object_cached_frame_bytes
                <<",\"object_tested_samples\":"<<lot.object_tested_samples<<",\"object_shaded_samples\":"<<lot.object_shaded_samples
                <<",\"object_hidden\":"<<lot.object_hidden<<",\"object_above_level\":"<<lot.object_above_level
                <<",\"object_outside\":"<<lot.object_outside<<",\"object_removed\":"<<lot.object_removed
                <<",\"object_pick_buffer_bytes\":"<<lot.object_pick_ids.size()*sizeof(std::uint16_t)
                <<",\"other_avatars_rendered\":false,\"objects_rendered\":true,\"original_visual_equivalence\":false}";
        }else std::cout<<"null";
        std::cout<<'}';
    }else std::cout<<"null";
    std::cout<<"}\n";
    return host_failed||status==odh::VmStatus::fault||status==odh::VmStatus::budget_exhausted||tick_exhausted||instruction_exhausted||action_incomplete?3:0;
 }catch(const std::exception& e){std::cerr<<"odh-person: "<<e.what()<<'\n';return 2;}
}
