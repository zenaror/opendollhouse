#include "odh/routing.hpp"
#include "odh/distance_to.hpp"
#include "odh/slot_scoring.hpp"
#include "odh/snap.hpp"
#include "odh/notify.hpp"
#include "odh/playback.hpp"
#include "odh/object_type.hpp"
#include "odh/sound.hpp"
#include "odh/sleep.hpp"
#include "resource.hpp"
#include "probe_view.hpp"
#include "bhav_audio.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

namespace {
struct DefinitionSelection {
    std::uint16_t id;
    std::optional<odh::ObjdBinding> binding;
    std::optional<odh::SlotTable> slots;
    bool load_functions=false;
    std::optional<odh::ObjdFunctions> functions;
};
std::string fold(std::string value) {
    for(auto& c:value) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    return value;
}
std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32 || c >= 127)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c);
        else out << static_cast<char>(c);
    }
    out << '"';
    return out.str();
}
std::uint16_t identifier(const std::string& text) {
    std::size_t end = 0;
    const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    if (text.empty() || text[0] == '-' || text[0] == '+' || text[0] == ' ')
        throw odh::FormatError("invalid routine ID");
    const auto value = std::stoull(text, &end, hex ? 16 : 10);
    if (end != text.size() || value > 65535) throw odh::FormatError("invalid routine ID");
    return static_cast<std::uint16_t>(value);
}
template<class T>T audio_number(const std::string& text) {
    T value{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(text.empty() || parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
        throw odh::FormatError("invalid decimal audio profile value");
    return value;
}
void load(const std::string& path, const std::string& name, odh::RoutineSet& routines,
          bool globals,std::optional<odh::SlotTable>* slots=nullptr,DefinitionSelection* definition=nullptr,
          std::map<std::uint16_t,odh::SoundEvent>* sounds=nullptr,
          std::map<std::uint16_t,odh::Bcon>* tuning=nullptr,
          std::optional<odh::Glob>* binding=nullptr,std::optional<odh::RoutineScope> required_scope={}) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw odh::FormatError("cannot open archive");
    const odh::View archive(stream, std::filesystem::file_size(path));
    std::optional<odh::FarEntry> selected;
    for (const auto& entry : odh::far1_index(archive)) {
        if (entry.name != name) continue;
        if (selected) throw odh::FormatError("ambiguous archive entry");
        selected = entry;
    }
    if (!selected) throw odh::FormatError("exact archive entry not found");
    odh::iff_chunks(archive.subview(selected->offset, selected->length), [&](const odh::Chunk& c) {
        if(binding && c.type=="GLOB" && c.id==128) {
            if(*binding)throw odh::FormatError("ambiguous code-owner GLOB128 binding");
            *binding=odh::read_glob(c.payload);
        }
        if(definition && c.type=="OBJD" && c.id==definition->id) {
            if(definition->binding) throw odh::FormatError("ambiguous selected OBJD");
            definition->binding=odh::read_objd_binding(c.payload);
            if(definition->load_functions)definition->functions=odh::read_objd_functions(c.payload);
        }
        if(slots && c.type=="SLOT" && c.id==100) {
            if(*slots) throw odh::FormatError("ambiguous global SLOT100");
            *slots=odh::read_slot(c.payload);
        }
        if(sounds && c.type=="FWAV") {
            auto payload=c.payload;
            if(!sounds->emplace(c.id,odh::read_fwav(payload)).second)
                throw odh::FormatError("ambiguous FWAV ID in explicit resource");
        }
        if(tuning && c.type=="BCON") {
            if(!tuning->emplace(c.id,odh::read_bcon(c.payload)).second)
                throw odh::FormatError("ambiguous private BCON ID");
        }
        if (c.type != "BHAV") return;
        const auto key = odh::routine_key(c.id);
        if(required_scope && key.scope!=*required_scope)
            throw odh::FormatError("BHAV scope outside explicit resource role");
        if (globals && key.scope != odh::RoutineScope::global)
            throw odh::FormatError("non-global routine in explicit global resource");
        if (!globals && key.scope == odh::RoutineScope::global)
            throw odh::FormatError("global routine in private resource: explicit provenance required");
        if (!routines.emplace(key, odh::read_bhav(c.payload)).second)
            throw odh::FormatError("ambiguous routine ID");
    });
    if(definition) {
        if(!definition->binding) throw odh::FormatError("selected OBJD not found");
        odh::iff_chunks(archive.subview(selected->offset,selected->length),[&](const odh::Chunk& c) {
            if(c.type!="SLOT" || c.id!=definition->binding->slot_table) return;
            if(definition->slots) throw odh::FormatError("ambiguous selected object SLOT");
            definition->slots=odh::read_slot(c.payload);
        });
        if(!definition->slots) throw odh::FormatError("selected OBJD SLOT missing");
    }
}
std::map<std::uint16_t,odh::StringTable> load_tree_name_strings(
    const std::string& path,const std::string& name,const std::set<std::uint16_t>& ids) {
    if(ids.size()>256)throw odh::FormatError("RunTreeByName string-table budget exceeded");
    std::map<std::uint16_t,odh::StringTable> result;
    std::uint64_t payload_bytes=0;
    with_far_entry(path,name,[&](const odh::View& view){odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="STR#"||!ids.count(chunk.id))return;
            if(chunk.payload.size()>8U*1024U*1024U-payload_bytes)
                throw odh::FormatError("RunTreeByName string-table byte budget exceeded");
            payload_bytes+=chunk.payload.size();
            if(!result.emplace(chunk.id,odh::read_strings(chunk.payload)).second)
                throw odh::FormatError("ambiguous RunTreeByName STR# ID");
        },limits);
    });
    return result;
}
void add_tree_names(const std::string& path,const std::string& name,
                    std::map<std::string,std::uint16_t>& trees) {
    with_far_entry(path,name,[&](const odh::View& view){odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="BHAV")return;
            auto label=chunk.label;
            if(const auto nul=label.find('\0');nul!=std::string::npos)label.resize(nul);
            trees.emplace(std::move(label),chunk.id);
        },limits);
    });
}
void instruction(const odh::Instruction& i, std::uint16_t pc) {
    std::cout << "{\"pc\":" << pc << ",\"opcode\":" << i.opcode
              << ",\"true\":" << static_cast<unsigned>(i.on_true)
              << ",\"false\":" << static_cast<unsigned>(i.on_false) << ",\"operands\":[";
    for (std::size_t n = 0; n < i.operands.size(); ++n) {
        if (n) std::cout << ',';
        std::cout << static_cast<unsigned>(i.operands[n]);
    }
    std::cout << "]}";
}
}
int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: odh-bhav ARCHIVE.far ENTRY.iff ROUTINE_ID [--run] [--global ARCHIVE.far ENTRY.iff]\n"
                     "  [--semiglobal ARCHIVE.far ENTRY.iff] validates code-owner Pascal GLOB128 binding.\n"
                     "  [--world-probe [--occupant 0|1|3] [--posture 0..32767] [--autonomous]]\n"
                     "  [--route-probe] requires --world-probe and --global; constructed clearance grid.\n"
                     "  [--snap-probe --object-def ID] also requires --route-probe; explicit OBJD binding.\n"
                     "  [--route-slot-probe --object-def ID [--route-attribute1 0|1]] loads StackObject OBJD/SLOT.\n"
                     "  [--stack-object-word INDEX VALUE] initializes one word on the constructed StackObject.\n"
                     "  [--caller-object-word INDEX VALUE] initializes one word on the constructed Caller.\n"
                     "  [--animation-probe ARCHIVE.far ENTRY.bcf [--age 0..32767]] requires --snap-probe; silent animation.\n"
                     "  [--reset-probe ENTRY.bcf [--post-ticks 0..4096]] requires --animation-probe; explicit global posture loops.\n"
                     "  [--stand-after] requires reset, sound, animation and snap probes; legacy selected OBJD Stand.\n"
                     "  [--main-probe [--hold-ticks 0..4096]] requires --stand-after; constructed live chair Main.\n"
                     "  [--animation-extra ENTRY.bcf] repeat up to 16; explicit same-FAR collections.\n"
                     "  [--sound-probe] requires --animation-probe; FWAV requests are silent by default.\n"
                     "  [--audio-probe HSM HOT HIT (--audio-args R1 R2 R3|--audio-caller-args) --audio-jump free-so|literal32\n"
                     "   --audio-gain-q15 0..32768 --audio-pan-q15 -32768..32768 --audio-choose-first\n"
                     "   (--audio-far FILE|--audio-xa FILE)... [--audio-rate 8000..192000]\n"
                     "   [--audio-native-controls EXTVOL PAN] raw signed int32, separate from DSP Q15.\n"
                     "   [--audio-drain-ms 1..5000]] requires --sound-probe and Linux SDL audio.\n"
                     "  [--view TEXTURES.far --view-skeleton ENTRY NAME --view-part ENTRY NAME BMF TEXTURE]\n"
                     "  [--view-frames DIR --view-stride 1..4096 --view-window --view-auto --view-readback] requires Main probe.\n"
                     "Default: structural dump. --run: bounded VM probe without world services.\n";
        return 2;
    }
    try {
        bool run = false;
        bool world_probe = false, autonomous = false;
        bool route_probe=false,route_slot_probe=false;
        bool snap_probe=false, sound_probe=false, stand_after=false, main_probe=false;
        bool audio_probe=false,audio_args=false,audio_jump=false,audio_gain=false,audio_pan=false;
        bool audio_choose_first=false,audio_option_seen=false,audio_rate=false,audio_native_controls=false;
        std::optional<std::uint16_t> audio_drain_ms;
        odh_host::BhavAudioConfig audio_config;
        std::vector<std::string> animation_extra;
        std::optional<std::string> view_textures,view_frames;
        std::optional<std::pair<std::string,std::string>> view_skeleton;
        std::optional<std::uint16_t> view_stride;
        std::vector<ScenePart> view_parts;
        bool view_window=false,view_auto=false,view_readback=false;
        std::optional<std::uint16_t> object_def,route_attribute1;
        std::optional<std::pair<std::uint16_t,std::uint16_t>> stack_object_word;
        std::optional<std::pair<std::uint16_t,std::uint16_t>> caller_object_word;
        std::optional<std::uint16_t> occupant, posture, age, post_ticks, hold_ticks;
        std::optional<std::string> reset_entry;
        std::optional<std::pair<std::string,std::string>> animation;
        std::optional<std::pair<std::string, std::string>> global,semiglobal;
        for (int n = 4; n < argc; ++n) {
            const std::string option = argv[n];
            if(option=="--view"&&!view_textures&&n+1<argc)view_textures=argv[++n];
            else if(option=="--view-frames"&&!view_frames&&n+1<argc)view_frames=argv[++n];
            else if(option=="--view-stride"&&!view_stride&&n+1<argc)view_stride=identifier(argv[++n]);
            else if(option=="--view-skeleton"&&!view_skeleton&&n+2<argc){view_skeleton=std::make_pair(argv[n+1],argv[n+2]);n+=2;}
            else if(option=="--view-part"&&n+4<argc&&view_parts.size()<16){view_parts.push_back({argv[n+1],argv[n+2],argv[n+3],argv[n+4]});n+=4;}
            else if(option=="--view-window"&&!view_window)view_window=true;
            else if(option=="--view-auto"&&!view_auto)view_auto=true;
            else if(option=="--view-readback"&&!view_readback)view_readback=true;
            else if (option == "--run" && !run) run = true;
            else if (option == "--world-probe" && !world_probe) world_probe = true;
            else if (option == "--route-probe" && !route_probe) route_probe=true;
            else if (option == "--route-slot-probe" && !route_slot_probe) route_slot_probe=true;
            else if (option == "--route-attribute1" && !route_attribute1 && n + 1 < argc) route_attribute1=identifier(argv[++n]);
            else if (option == "--stack-object-word" && !stack_object_word && n + 2 < argc) {
                const auto index=identifier(argv[++n]);
                const auto value=identifier(argv[++n]);
                stack_object_word=std::make_pair(index,value);
            }
            else if (option == "--caller-object-word" && !caller_object_word && n + 2 < argc) {
                const auto index=identifier(argv[++n]);
                const auto value=identifier(argv[++n]);
                caller_object_word=std::make_pair(index,value);
            }
            else if (option == "--sound-probe" && !sound_probe) sound_probe=true;
            else if (option == "--audio-probe" && !audio_probe && n+3<argc) {
                audio_probe=audio_option_seen=true;
                audio_config.hsm_path=argv[++n];audio_config.hot_path=argv[++n];audio_config.hit_path=argv[++n];
            }
            else if (option == "--audio-args" && !audio_args && n+3<argc) {
                audio_args=audio_option_seen=true;
                for(auto& value:audio_config.arguments)value=audio_number<std::int32_t>(argv[++n]);
            }
            else if (option == "--audio-caller-args" && !audio_args) {
                audio_args=audio_option_seen=true;audio_config.caller_arguments=true;
            }
            else if (option == "--audio-native-controls" && !audio_native_controls && n+2<argc) {
                audio_native_controls=audio_option_seen=true;
                std::array<std::int32_t,2> values{};
                for(auto& value:values)value=audio_number<std::int32_t>(argv[++n]);
                audio_config.native_controls=values;
            }
            else if (option == "--audio-jump" && !audio_jump && n+1<argc) {
                audio_jump=audio_option_seen=true;const std::string value=argv[++n];
                if(value=="literal32")audio_config.jump=odh::HitJumpOperand::Literal32;
                else if(value!="free-so")throw odh::FormatError("invalid audio Jump profile");
            }
            else if (option == "--audio-gain-q15" && !audio_gain && n+1<argc) {
                audio_gain=audio_option_seen=true;
                audio_config.controls.gain_q15=audio_number<std::uint32_t>(argv[++n]);
            }
            else if (option == "--audio-pan-q15" && !audio_pan && n+1<argc) {
                audio_pan=audio_option_seen=true;
                audio_config.controls.pan_q15=audio_number<std::int32_t>(argv[++n]);
            }
            else if (option == "--audio-choose-first" && !audio_choose_first)
                audio_choose_first=audio_option_seen=true;
            else if (option == "--audio-far" && n+1<argc && audio_config.sources.size()<16) {
                audio_option_seen=true;audio_config.sources.push_back({argv[++n],odh_host::XaSourceKind::far});
            }
            else if (option == "--audio-xa" && n+1<argc && audio_config.sources.size()<16) {
                audio_option_seen=true;audio_config.sources.push_back({argv[++n],odh_host::XaSourceKind::loose});
            }
            else if (option == "--audio-rate" && !audio_rate && n+1<argc) {
                audio_rate=audio_option_seen=true;audio_config.output_rate=audio_number<std::uint32_t>(argv[++n]);
            }
            else if (option == "--audio-drain-ms" && !audio_drain_ms && n+1<argc) {
                audio_option_seen=true;audio_drain_ms=audio_number<std::uint16_t>(argv[++n]);
            }
            else if (option == "--main-probe" && !main_probe) main_probe=true;
            else if (option == "--hold-ticks" && !hold_ticks && n+1<argc) hold_ticks=identifier(argv[++n]);
            else if (option == "--stand-after" && !stand_after) stand_after=true;
            else if (option == "--animation-extra" && n+1<argc && animation_extra.size()<16) animation_extra.emplace_back(argv[++n]);
            else if (option == "--snap-probe" && !snap_probe) snap_probe=true;
            else if (option == "--object-def" && !object_def && n+1<argc) object_def=identifier(argv[++n]);
            else if (option == "--autonomous" && !autonomous) autonomous = true;
            else if (option == "--occupant" && !occupant && n + 1 < argc) occupant = identifier(argv[++n]);
            else if (option == "--posture" && !posture && n + 1 < argc) posture = identifier(argv[++n]);
            else if (option == "--reset-probe" && !reset_entry && n+1<argc) reset_entry=argv[++n];
            else if (option == "--post-ticks" && !post_ticks && n+1<argc) post_ticks=identifier(argv[++n]);
            else if (option == "--age" && !age && n+1<argc) age=identifier(argv[++n]);
            else if (option == "--animation-probe" && !animation && n+2<argc) {
                animation=std::make_pair(argv[n+1],argv[n+2]);n+=2;
            }
            else if(option=="--semiglobal" && !semiglobal && n+2<argc) {
                semiglobal=std::make_pair(argv[n+1],argv[n+2]);n+=2;
            }
            else if (option == "--global" && !global && n + 2 < argc) {
                global = std::make_pair(argv[n + 1], argv[n + 2]); n += 2;
            } else throw odh::FormatError("invalid or repeated option");
        }
        if ((world_probe && !run) || (!world_probe && (occupant || posture || autonomous)) ||
            (route_probe && (!world_probe || !global || occupant.value_or(0)!=0)) ||
            (snap_probe && (!route_probe || !object_def)) ||
            (route_slot_probe && (!route_probe || !object_def || snap_probe)) ||
            (object_def && !snap_probe && !route_slot_probe) ||
            (route_attribute1 && (!route_slot_probe || *route_attribute1>1)) ||
            (stack_object_word && (!world_probe || stack_object_word->first>67 || stack_object_word->second>32767)) ||
            (caller_object_word && (!world_probe || caller_object_word->first>67 || caller_object_word->second>32767)) ||
            (occupant && *occupant != 0 && *occupant != 1 && *occupant != 3) ||
            (posture && *posture > 32767) || (animation && !snap_probe) ||
            (age && (!animation || *age>32767)) || (sound_probe && !animation) || (reset_entry && !animation) ||
            (post_ticks && (!reset_entry || *post_ticks>4096)) ||
            (main_probe && !stand_after) || (hold_ticks && (!main_probe || *hold_ticks>4096)) ||
            (!animation_extra.empty() && !animation) || (stand_after && (!reset_entry || !sound_probe || !snap_probe || !animation))) throw odh::FormatError("invalid constructed-world options");
        if((audio_option_seen && !audio_probe) || (audio_probe &&
           (!sound_probe || !audio_args || !audio_jump || !audio_gain || !audio_pan || !audio_choose_first ||
            audio_config.hsm_path.empty() || audio_config.hot_path.empty() || audio_config.hit_path.empty() ||
            audio_config.sources.empty() || audio_config.controls.gain_q15>32768 ||
            audio_config.controls.pan_q15< -32768 || audio_config.controls.pan_q15>32768 ||
            audio_config.output_rate<8000 || audio_config.output_rate>192000 ||
            (audio_drain_ms && (!*audio_drain_ms || *audio_drain_ms>5000)))))
            throw odh::FormatError("invalid explicit audio probe profile");
#ifndef ODH_HAVE_SDL_AUDIO
        if(audio_probe)throw odh::FormatError("audio probe requires Linux SDL audio build");
#endif
        if((view_textures&&(!main_probe||!view_skeleton||view_parts.empty()||(!view_frames&&!view_window)))||
           (!view_textures&&(view_frames||view_window||view_skeleton||view_stride||!view_parts.empty()))||
           (view_auto&&!view_window)||(view_readback&&!view_window)||(view_stride&&(!*view_stride||*view_stride>4096))||
           (view_frames&&view_frames->empty()))throw odh::FormatError("invalid live view options");
        const auto key = odh::routine_key(identifier(argv[3]));
        odh::RoutineSet routines;
        std::optional<odh::SlotTable> routing_slots;
        DefinitionSelection definition{object_def.value_or(0),{}, {},stand_after,{}};
        std::map<std::uint16_t,odh::SoundEvent> private_sounds,global_sounds;
        std::map<std::uint16_t,odh::Bcon> private_tuning;
        std::optional<odh::Glob> semiglobal_binding;
        load(argv[1], argv[2], routines, false,nullptr,(snap_probe||route_slot_probe)?&definition:nullptr,sound_probe?&private_sounds:nullptr,main_probe?&private_tuning:nullptr,
             semiglobal?&semiglobal_binding:nullptr,semiglobal?std::optional<odh::RoutineScope>(odh::RoutineScope::private_object):std::nullopt);
        if(semiglobal) {
            if(!semiglobal_binding || semiglobal_binding->name.empty())
                throw odh::FormatError("explicit semiglobal requires nonempty code-owner GLOB128");
            const auto& name=semiglobal_binding->name;
            if(name.find_first_of("/\\:\0",0,4)!=std::string::npos || name=="." || name=="..")
                throw odh::FormatError("GLOB128 name outside flat semiglobal archive profile");
            if(fold(name+".iff")!=fold(semiglobal->second))
                throw odh::FormatError("explicit semiglobal entry does not match code-owner GLOB128");
            load(semiglobal->first,semiglobal->second,routines,false,nullptr,nullptr,nullptr,nullptr,nullptr,odh::RoutineScope::semiglobal);
        }
        const auto emit_semiglobal=[&] {
            if(!semiglobal)return;
            std::cout<<",\"semiglobal_binding\":{\"code_owner_archive\":"<<quote(argv[1])
                     <<",\"code_owner_entry\":"<<quote(argv[2])<<",\"glob_id\":128,\"encoding\":\"pascal_u8\",\"name\":"
                     <<quote(semiglobal_binding->name)<<",\"remaining_bytes\":"<<semiglobal_binding->remaining_bytes
                     <<",\"archive\":"<<quote(semiglobal->first)<<",\"entry\":"<<quote(semiglobal->second)<<'}';
        };
        if (global) load(global->first, global->second, routines, true,route_probe?&routing_slots:nullptr,nullptr,sound_probe?&global_sounds:nullptr);
        if(route_probe && !routing_slots) throw odh::FormatError("route probe requires global SLOT100");
        std::set<std::uint16_t> tree_name_global_tables,tree_name_local_tables,tree_name_semi_tables;
        for(const auto& routine:routines)for(const auto& item:routine.second.instructions){
            if(item.opcode!=28)continue;
            const auto table=static_cast<std::uint16_t>(item.operands[0]|(item.operands[1]<<8));
            if(item.operands[2]==1)tree_name_global_tables.insert(table);
            else if(item.operands[2]==0){
                tree_name_local_tables.insert(table);
                if(routine.first.id>=8192&&semiglobal)tree_name_semi_tables.insert(table);
            }
        }
        std::map<std::uint16_t,odh::StringTable> tree_name_global_strings,tree_name_local_strings,tree_name_semi_strings;
        if(global&&!tree_name_global_tables.empty())
            tree_name_global_strings=load_tree_name_strings(global->first,global->second,tree_name_global_tables);
        if(!tree_name_local_tables.empty())
            tree_name_local_strings=load_tree_name_strings(argv[1],argv[2],tree_name_local_tables);
        if(semiglobal&&!tree_name_semi_tables.empty())
            tree_name_semi_strings=load_tree_name_strings(semiglobal->first,semiglobal->second,tree_name_semi_tables);
        std::map<std::string,std::uint16_t> tree_by_name;
        if(route_slot_probe){
            add_tree_names(argv[1],argv[2],tree_by_name);
            if(semiglobal)add_tree_names(semiglobal->first,semiglobal->second,tree_by_name);
        }
        if(stand_after) {
            const auto& f=*definition.functions;
            if(f.uses_function_table!=0)throw odh::FormatError("OBJD function table dispatch unsupported");
            if(f.stand==0)throw odh::FormatError("selected OBJD legacy Stand is absent");
            const auto stand=odh::routine_key(f.stand);
            if(stand.scope!=odh::RoutineScope::private_object || routines.find(stand)==routines.end())
                throw odh::FormatError("selected OBJD Stand is absent from private code-owner routines");
        }
        if(main_probe) {
            const auto main=odh::routine_key(definition.functions->main);
            if(definition.functions->main==0 || main.scope!=odh::RoutineScope::private_object || routines.find(main)==routines.end())
                throw odh::FormatError("selected OBJD Main is absent from private code-owner routines");
        }
        std::map<std::string,std::shared_ptr<const odh::AnimationHeader>> clips;
        std::map<std::uint16_t,odh::StringTable> animation_tables, reset_tables;
        if(animation) {
            const auto load_clips=[&](const std::string& entry) {
            with_far_entry(animation->first,entry,[&](const auto& view) {
                auto collection=odh::read_bcf_animations(view);
                for(auto& clip:collection.animations) {
                    const auto name=fold(clip.name);
                    if(!clips.emplace(name,std::make_shared<const odh::AnimationHeader>(std::move(clip))).second)
                        throw odh::FormatError("ambiguous ASCII casefold animation name");
                }
            });
            };
            std::vector<std::string> clip_entries{animation->second};
            if(reset_entry && *reset_entry!=animation->second) {
                if(fold(*reset_entry)==fold(animation->second))
                    throw odh::FormatError("duplicate ASCII casefold animation entry selector");
                clip_entries.push_back(*reset_entry);
            }
            for(const auto& entry:animation_extra) {
                for(const auto& prior:clip_entries)if(fold(prior)==fold(entry))
                    throw odh::FormatError("duplicate ASCII casefold animation entry selector");
                clip_entries.push_back(entry);
            }
            for(const auto& entry:clip_entries)load_clips(entry);
            if(reset_entry)with_far_entry(global->first,global->second,[&](const auto& view) {
                odh::iff_chunks(view,[&](const odh::Chunk& c) {
                    if(c.type!="STR#" || (c.id!=150 && c.id!=151))return;
                    if(!reset_tables.emplace(c.id,odh::read_strings(c.payload)).second)
                        throw odh::FormatError("ambiguous global STR# reset table");
                });
            });
            with_far_entry(argv[1],argv[2],[&](const auto& view) {
                odh::iff_chunks(view,[&](const odh::Chunk& c) {
                    const auto base=static_cast<std::uint32_t>(definition.binding->animation_table);
                    if(c.type!="STR#" || (c.id!=129 && c.id!=130 && c.id!=base && c.id!=static_cast<std::uint16_t>(base+1)))return;
                    if(!animation_tables.emplace(c.id,odh::read_strings(c.payload)).second)
                        throw odh::FormatError("ambiguous STR# animation table");
                });
            });
        }
        const auto found = routines.find(key);
        if (found == routines.end()) throw odh::FormatError("routine not found in supplied resources");
        if (!run) {
            const auto& r = found->second;
            std::cout << "{\"evidence\":\"static_structure_only\",\"routine\":" << key.id
                      << ",\"version\":" << r.version << ",\"metadata\":";
            if (r.metadata) std::cout << "{\"arguments\":" << static_cast<unsigned>(r.metadata->arguments)
                << ",\"locals\":" << r.metadata->locals << ",\"type\":"
                << static_cast<unsigned>(r.metadata->type) << ",\"tree_version\":" << r.metadata->tree_version << '}';
            else std::cout << "null";
            std::cout << ",\"header\":[";
            for (std::size_t n = 0; n < r.header.size(); ++n) {
                if (n) std::cout << ',';
                std::cout << static_cast<unsigned>(r.header[n]);
            }
            std::cout << "],\"instructions\":[";
            for (std::size_t n = 0; n < r.instructions.size(); ++n) {
                if (n) std::cout << ',';
                instruction(r.instructions[n], static_cast<std::uint16_t>(n));
            }
            std::cout << "]";emit_semiglobal();std::cout<<"}\n";
            return 0;
        }
        odh::World world;
        std::size_t storage_slots=1;
        if(snap_probe) {storage_slots=0;for(const auto& r:definition.slots->records) if(r.type==0) ++storage_slots;}
        if (world_probe) {
            if (!world.create(1,odh::EntityKind::avatar,{0,88,reset_entry?1u:0u,main_probe?16u:0u}) ||
                !world.create(2,odh::EntityKind::object,{route_slot_probe?2u:0u,0,storage_slots}) ||
                !world.create(3,odh::EntityKind::avatar,{0,88,0}) ||
                !world.set_person_data(1,0,static_cast<std::int16_t>(posture.value_or(0))) ||
                (animation && (!world.set_person_data(1,58,static_cast<std::int16_t>(age.value_or(21))) ||
                               !world.declare_object_word(1,17,0))) ||
                (occupant.value_or(0) && !world.attach(2,0,static_cast<std::int16_t>(*occupant))))
                throw odh::FormatError("cannot construct probe world");
            if(route_slot_probe) {
                odh::VmFrame object_frame{key,0,{1,2,2},{},{},odh::VmCallReturnMode::ordinary,0};
                if(!world.write(object_frame,1,1,static_cast<std::int16_t>(route_attribute1.value_or(0))))
                    throw odh::FormatError("cannot initialize constructed StackObject attribute 1");
            }
            if(stack_object_word&&!world.declare_object_word(2,static_cast<std::int16_t>(stack_object_word->first),
                    static_cast<std::int16_t>(stack_object_word->second)))
                throw odh::FormatError("cannot initialize constructed StackObject word");
            if(caller_object_word&&!world.declare_object_word(1,static_cast<std::int16_t>(caller_object_word->first),
                    static_cast<std::int16_t>(caller_object_word->second)))
                throw odh::FormatError("cannot initialize constructed Caller word");
        }
        if(main_probe && (!world.declare_definition_word(1,9,2) || !world.set_motive(1,6,0)))
            throw odh::FormatError("cannot construct avatar motive and definition profile");
        odh::RouteGrid grid(128,128);
        if(route_probe) {
            if(!world.set_position(1,{16,64,1,0}) || !world.set_position(2,{64,64,1,0}))
                throw odh::FormatError("cannot position route probe entities");
            for(int y=55;y<=73;++y) for(int x=55;x<=73;++x) grid.block({x,y});
            for(int y=0;y<128;++y) if(y<50 || y>55) grid.wall({39,y},{40,y});
        }
        const odh::SlotTable empty_slots{0,10,{},0};
        odh::RandomState route_slot_random{1};
        const auto route_slot_random_handler=odh::random_primitive(route_slot_random);
        odh::PreRankedSlotGoalProvider route_slot_goals=[&](const auto& table,std::size_t index,
            const auto& actor,const auto& target) {
            return odh::rank_reference_range_slot(table,index,actor,target,route_slot_random);
        };
        odh::Routing routing(world,grid,routing_slots?*routing_slots:empty_slots);
        if(route_slot_probe)routing.set_ranged_slot_goal_provider(route_slot_goals);
        std::size_t stackobject_slot_resolutions=0;
        if(route_slot_probe) {
            routing.set_stack_object_slot_table_provider([&](const odh::VmFrame& frame)->const odh::SlotTable* {
                if(frame.context.stack_object!=2 || !definition.slots)return nullptr;
                ++stackobject_slot_resolutions;
                return &*definition.slots;
            });
            routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint point) {
                const auto* actor=world.find(frame.context.caller);
                const auto* target=world.find(frame.context.stack_object);
                return actor&&target&&actor->position&&target->position&&
                    actor->position->level==target->position->level&&grid.contains(point)&&grid.walkable(point);
            });
        }
        std::optional<odh::Snap> snap;
        if(snap_probe) snap.emplace(world,2,*definition.binding,*definition.slots,definition.binding->slot_table,
            [&](std::int16_t,const odh::EntityPosition& position) {
                return position.level==1 && position.direction<8 && grid.walkable({position.x,position.y});
            });
        std::optional<odh::Playback> playback;
        std::string resolution_diagnostic;
        odh::AnimationResetResolver reset_resolver;
        if(reset_entry)reset_resolver=[&](const odh::VmFrame& frame,std::int16_t normalized_posture)
            -> std::shared_ptr<const odh::AnimationHeader> {
            resolution_diagnostic.clear();
            const auto fail=[&](const std::string& text)->std::shared_ptr<const odh::AnimationHeader> {
                resolution_diagnostic=text;return {};
            };
            const auto* actor=world.find(frame.context.caller);
            if(!actor || actor->person_data.size()<=58)return fail("reset actor age unavailable");
            const auto table_id=static_cast<std::uint16_t>(actor->person_data[58]<18?151:150);
            const auto table=reset_tables.find(table_id);
            if(table==reset_tables.end())return fail("reset global STR# table absent");
            const auto* entry=odh::string_in_language(table->second,1,static_cast<std::uint16_t>(normalized_posture));
            if(!entry)return fail("reset global STR# index missing in explicit language 1");
            const auto clip=clips.find(fold(entry->value));
            if(clip==clips.end())return fail("reset clip missing from supplied BCF entries");
            return clip->second;
        };
        if(animation) playback.emplace(world,[&](const odh::VmFrame& frame,std::uint32_t source,std::uint16_t id)
            -> std::shared_ptr<const odh::AnimationHeader> {
            resolution_diagnostic.clear();
            const auto fail=[&](const std::string& text)->std::shared_ptr<const odh::AnimationHeader> {
                resolution_diagnostic=text;return {};
            };
            if(source!=0 && source!=65536)return fail("animation scope not supplied by explicit code owner");
            if(source==65536 && frame.context.stack_object!=2)
                return fail("animation StackObject does not match loaded definition");
            const auto* actor=world.find(frame.context.caller);
            if(!actor || actor->person_data.size()<=58)return fail("animation actor age unavailable");
            const std::uint32_t child=actor->person_data[58]<18?1:0;
            const auto table_id=static_cast<std::uint16_t>(definition.binding->animation_table+child);
            auto table=animation_tables.find(table_id);
            if(table==animation_tables.end())table=animation_tables.find(static_cast<std::uint16_t>(129+child));
            if(table==animation_tables.end())return fail("animation STR# table absent including fallback");
            const auto* entry=odh::string_in_language(table->second,1,id);
            if(!entry)return fail("animation STR# index missing in explicit language 1");
            const auto clip=clips.find(fold(entry->value));
            if(clip==clips.end())return fail("animation clip missing from supplied BCF");
            return clip->second;
        },odh::PlaybackLimits{},reset_resolver);
        if(snap_probe) {
            if(definition.binding->master_id!=0)
                throw odh::FormatError("probe requires single-part OBJD: master resource not supplied");
            if(!world.define_type(2,definition.binding->guid))
                throw odh::FormatError("cannot bind probe object type");
        }
        std::unique_ptr<ProbeView> view;
        if(view_textures){
            ProbeViewConfig config;config.scene={animation->first,view_skeleton->first,view_skeleton->second,*view_textures,argv[1],argv[2],*object_def,view_parts};
            config.frames_directory=view_frames.value_or("");config.stride=view_stride.value_or(view_window?1:8);
            config.window=view_window;config.automatic=view_auto;config.readback=view_readback;
            view=std::make_unique<ProbeView>(std::move(config));
        }
        struct ObservedSound {
            odh::SoundRequest request;std::string status;
            std::optional<odh::HitArguments> arguments;
            std::optional<odh_host::BhavNativeControlReceipt> native_controls;
        };
        std::vector<ObservedSound> sound_requests;
        bool sound_events_truncated=false,sound_event_limit_reached=false;
#ifdef ODH_HAVE_SDL_AUDIO
        std::unique_ptr<odh_host::BhavAudio> audio_service;
        if(audio_probe)audio_service=std::make_unique<odh_host::BhavAudio>(world,std::move(audio_config));
#endif
        odh::SoundSink sound_sink;
#ifdef ODH_HAVE_SDL_AUDIO
        if(audio_service)sound_sink=[&](const odh::SoundRequest& request){return audio_service->submit(request);};
#endif
        const auto sound_handler=sound_probe?odh::sound_primitive(world,
            [&](const odh::VmFrame&,std::uint16_t id)->std::optional<odh::SoundEvent> {
                auto selected=private_sounds.find(id);
                if(selected!=private_sounds.end())return selected->second;
                selected=global_sounds.find(id);
                if(selected!=global_sounds.end())return selected->second;
                return {};
            },sound_sink,[&](const odh::SoundRequest& request,const std::string& status) {
                std::optional<odh::HitArguments> arguments;
                std::optional<odh_host::BhavNativeControlReceipt> native_controls;
#ifdef ODH_HAVE_SDL_AUDIO
                if(audio_service && (status=="submitted" || status=="backend_rejected")){
                    arguments=audio_service->last_arguments();
                    native_controls=audio_service->last_native_controls();
                }
#endif
                if(sound_requests.size()<4096)sound_requests.push_back({request,status,arguments,native_controls});
                else sound_events_truncated=true;
                if(sound_requests.size()==4096)sound_event_limit_reached=true;
            }):odh::VmPrimitive{};
        const auto type_handler=snap_probe?odh::object_type_primitive(world):odh::VmPrimitive{};
        std::uint64_t rng_cycles=0;
        odh::Sleep main_sleep(world,[&](){++rng_cycles;});
        std::vector<std::pair<std::int16_t,std::string>> notifications;
        const auto notify_handler=snap_probe?odh::notify_idle_primitive(world,main_probe?odh::IdleNotifySink{[&](auto id){return id==2 && main_sleep.interrupt();}}:odh::IdleNotifySink{},[&](auto id,const auto& status){notifications.emplace_back(id,status);}):odh::VmPrimitive{};
        const auto animation_handler=animation?playback->primitive():odh::VmPrimitive{};
        const auto route_handler=routing.primitive();
        const auto distance_to_handler=odh::get_distance_to_primitive(world);
        const auto snap_handler=snap_probe?snap->primitive():odh::VmPrimitive{};
        const auto tree_by_name_handler=[&](const odh::VmFrame& frame,const odh::Instruction& item,
            const std::array<std::int16_t,20>&,const odh::VmDispatch&)->odh::PrimitiveOutcome{
            if(item.opcode!=28)return {};
            if(!world.find(frame.context.stack_object))return {odh::PrimitiveDisposition::goto_false,{}};
            if(item.operands[2]>1)
                return {odh::PrimitiveDisposition::unsupported,"RunTreeByName string scope is outside the supported TS1 profile"};
            const auto table_id=static_cast<std::uint16_t>(item.operands[0]|(item.operands[1]<<8));
            const auto string_id=item.operands[4];
            if(string_id==0)return {odh::PrimitiveDisposition::goto_false,{}};
            const odh::StringTable* table=nullptr;
            if(item.operands[2]==1){
                const auto found=tree_name_global_strings.find(table_id);
                if(found!=tree_name_global_strings.end())table=&found->second;
            }else{
                if(frame.key.id>=8192){
                    const auto semi=tree_name_semi_strings.find(table_id);
                    if(semi!=tree_name_semi_strings.end())table=&semi->second;
                }
                if(!table){const auto local=tree_name_local_strings.find(table_id);
                    if(local!=tree_name_local_strings.end())table=&local->second;}
            }
            if(!table)return {odh::PrimitiveDisposition::goto_false,{}};
            const auto* tree_name=odh::string_in_language(*table,1,static_cast<std::uint32_t>(string_id-1));
            if(!tree_name)return {odh::PrimitiveDisposition::goto_false,{}};
            if(!route_slot_probe||!definition.binding)
                return {odh::PrimitiveDisposition::unsupported,"RunTreeByName target TreeByName catalog is not available"};
            const auto tree=tree_by_name.find(tree_name->value);
            if(tree==tree_by_name.end())return {odh::PrimitiveDisposition::goto_false,{}};
            return {odh::PrimitiveDisposition::unsupported,"RunTreeByName found a named BHAV but subroutine dispatch is not installed"};
        };
        odh::VmPrimitive handler;
        if(route_probe) handler=[&](const auto& f,const auto& i,const auto& t,const auto& d) {
            if(i.opcode==49 && notify_handler)return notify_handler(f,i,t,d);
            if(i.opcode==32 && type_handler)return type_handler(f,i,t,d);
            if(i.opcode==23 && sound_handler)return sound_handler(f,i,t,d);
            if(i.opcode==44 && animation_handler)return animation_handler(f,i,t,d);
            if(i.opcode==11)return distance_to_handler(f,i,t,d);
            if(i.opcode==8&&route_slot_probe)return route_slot_random_handler(f,i,t,d);
            if(i.opcode==28)return tree_by_name_handler(f,i,t,d);
            return i.opcode==46 && snap_handler?snap_handler(f,i,t,d):route_handler(f,i,t,d);
        };
        const auto world_memory=world.vm_memory();
        auto main_memory=world_memory;
        main_memory.read=[&](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t index)->std::optional<std::int16_t> {
            if(scope!=26)return world_memory.read(frame,scope,index);
            const auto data=static_cast<std::uint16_t>(index);
            if(data>=8192)return {};
            const auto table=private_tuning.find(static_cast<std::uint16_t>(4096+(data>>7)));
            const auto item=data&127;
            if(table==private_tuning.end() || item>=table->second.constants.size())return {};
            const auto word=table->second.constants[item];
            return static_cast<std::int16_t>(word<=32767?word:static_cast<std::int32_t>(word)-65536);
        };
        const auto sleep_handler=main_sleep.primitive();
        std::unique_ptr<odh::Vm> main_vm;
        if(main_probe) {
            main_vm=std::make_unique<odh::Vm>(routines,main_memory,odh::VmLimits{},
                [&](const auto& f,const auto& i,const auto& t,const auto& d) {
                    if(i.opcode==0)return sleep_handler(f,i,t,d);
                    if(i.opcode==32)return type_handler(f,i,t,d);
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"unsupported object Main primitive"};
                });
            if(!main_sleep.bind(*main_vm))throw odh::FormatError("cannot bind chair Main Sleep");
            main_vm->start(odh::routine_key(definition.functions->main),{2,2,2});
        }
        odh::Vm vm(std::move(routines),world_probe ? world.vm_memory() : odh::VmMemory{}, {},
                   handler);
        routing.bind(vm);
        if(snap_probe) snap->bind(vm);
        if(animation && !playback->bind(vm))throw odh::FormatError("cannot bind animation probe");
        vm.start(key,world_probe ? odh::VmContext{1,2,2} : odh::VmContext{},
                 {static_cast<std::int16_t>(autonomous ? 1 : 0)});
        std::cout << "{\"evidence\":" << quote(audio_probe?"ODH_VM_diagnostic_explicit_XA_PCM_audio_world":sound_probe?"ODH_VM_constructed_route_snap_silent_animation_sound_requests_world":animation?"ODH_VM_constructed_route_snap_silent_animation_world":snap_probe?"ODH_VM_constructed_route_snap_world":
                                                       route_probe?"ODH_VM_constructed_route_world":
                                                       world_probe ? "ODH_VM_constructed_world" : "ODH_VM_probe_without_world");
        if(route_slot_probe) {
            std::cout<<",\"stackobject_slot_binding\":{\"objd_id\":"<<definition.id
                <<",\"guid\":"<<definition.binding->guid<<",\"master_id\":"<<definition.binding->master_id
                <<",\"sub_index\":"<<definition.binding->sub_index<<",\"slot_table\":"<<definition.binding->slot_table
                <<",\"initial_attribute_1\":"<<route_attribute1.value_or(0)
                <<",\"attribute_source\":\"constructed StackObject probe parameter\"}";
        }
        if(stack_object_word)std::cout<<",\"stackobject_word\":["<<stack_object_word->first<<','<<stack_object_word->second<<']';
        if(caller_object_word)std::cout<<",\"caller_word\":["<<caller_object_word->first<<','<<caller_object_word->second<<']';
        if (world_probe) std::cout << ",\"initial_world\":{\"caller\":1,\"callee\":2,\"stack_object\":2,\"occupant\":"
            << occupant.value_or(0) << ",\"posture\":" << posture.value_or(0)
            << ",\"age\":" << (animation?age.value_or(21):0)
            << ",\"autonomous\":" << (autonomous ? "true" : "false") << '}';
        std::cout << ",\"trace\":[";
        bool first = true;
        std::size_t phase=0, previous_steps=0;
        std::vector<odh::VmTrace> main_trace;
        const auto main_steps=[&](){return main_vm?main_vm->steps():0;};
        const auto total_steps=[&](){return previous_steps+vm.steps()+main_steps();};
        const auto main_failed=[&](){return main_vm && (main_vm->status()==odh::VmStatus::fault || main_vm->status()==odh::VmStatus::budget_exhausted);};
        const auto pump_main=[&]() {
            if(!main_vm || main_failed())return;
            const auto state=main_vm->status();
            if(state==odh::VmStatus::running)main_vm->run(total_steps()<10000?10000-total_steps():0,
                [&](const auto& event){if(main_trace.size()<10000)main_trace.push_back(event);});
        };
        const auto trace=[&](const odh::VmTrace& event) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"step\":" << event.step << ",\"phase\":" << phase << ",\"total_step\":" << previous_steps+event.step+main_steps() << ",\"depth\":" << event.depth
                      << ",\"routine\":" << event.key.id << ",\"instruction\":";
            instruction(event.instruction, event.pc);
            std::cout << '}';
        };
        const auto run_probe=[&]() {
            if(main_failed())return vm.status();
            if(!sound_probe)return vm.run(total_steps()<10000?10000-total_steps():0,trace);
            auto state=vm.status();
            if(total_steps()>=10000 && (state==odh::VmStatus::running || state==odh::VmStatus::budget_exhausted))
                return vm.run(0,trace);
            while((state==odh::VmStatus::running || state==odh::VmStatus::budget_exhausted) &&
                  total_steps()<10000 && !sound_event_limit_reached)state=vm.run(1,trace);
            return state;
        };
        auto status=vm.status();
        std::uint64_t ticks=0;
        std::vector<odh::RouteMovement> movements;
        std::vector<odh::PlaybackEvent> animation_events;
        bool animation_events_truncated=false, animation_event_limit_reached=false;
        struct Phase {std::uint16_t routine;odh::VmStatus status;std::size_t steps;std::uint64_t ticks;
                      odh::EntityPosition position;std::int16_t container,posture,comfort;};
        std::vector<Phase> phases;
        bool actor_enabled=true;
        const auto world_tick=[&]() {
            if(view&&!view->ready(world,*playback,*definition.slots,ticks,phase))return false;
            ++ticks;
            if(main_probe)main_sleep.tick(ticks);
            routing.tick(ticks,[&](const auto& movement){movements.push_back(movement);});
            if(animation)playback->tick(ticks,[&](const auto& event){
                if(animation_events.size()<4096)animation_events.push_back(event);
                else animation_events_truncated=true;
            });
            if(animation_events.size()==4096)animation_event_limit_reached=true;
            status=vm.status();
            if(actor_enabled&&(status==odh::VmStatus::running || status==odh::VmStatus::waiting))status=run_probe();
            pump_main();
#ifdef ODH_HAVE_SDL_AUDIO
            if(audio_service)audio_service->simulation_tick();
#endif
            if(view)view->draw(world,*playback,*definition.slots,ticks,phase);
            return true;
        };
        const auto execute_phase=[&](std::uint16_t routine) {
            const auto begin_tick=ticks;
            if(!main_failed()&&(!view||(!view->quit()&&!view->failed())))status=run_probe();
            pump_main();
#ifdef ODH_HAVE_SDL_AUDIO
            if(audio_service)audio_service->simulation_tick();
#endif
            while(route_probe && status==odh::VmStatus::waiting && ticks<4096 &&
                  total_steps()<10000 && !main_failed() && !animation_event_limit_reached && !sound_event_limit_reached && (!view||(!view->quit()&&!view->failed())))
                world_tick();
            if(view)view->draw(world,*playback,*definition.slots,ticks,phase,true);
            if(stand_after) {
                const auto& actor=*world.find(1);
                phases.push_back({routine,status,vm.steps(),ticks-begin_tick,*actor.position,
                                  static_cast<std::int16_t>(actor.attachment?actor.attachment->container:0),actor.person_data[0],
                                  main_probe?actor.motives[6]:static_cast<std::int16_t>(0)});
            }
        };
        pump_main(); // Object Main reaches its first idle request at clock zero.
        if(view){view->draw(world,*playback,*definition.slots,ticks,phase,true);
            if(view->manual()){actor_enabled=false;view->clear_action();
                while(!view->action()&&!view->quit()&&!view->failed()&&ticks<4096&&!main_failed())world_tick();
                actor_enabled=true;
            }
        }
        if(!view||(ticks<4096&&!view->quit()&&!view->failed()&&(!view->manual()||view->action())))execute_phase(key.id);
        std::uint16_t hold_ticks_run=0;
        if(stand_after && status==odh::VmStatus::returned_true && !main_failed() && (!view||(!view->quit()&&!view->failed()))) {
            while(hold_ticks_run<hold_ticks.value_or(0) && (!view||ticks<4096) && total_steps()<10000 && !main_failed() &&
                  !animation_event_limit_reached && !sound_event_limit_reached && (!view||(!view->quit()&&!view->failed()))) {
                if(world_tick())++hold_ticks_run;
            }
            if(view&&view->manual()){view->clear_action();
                while(!view->action()&&!view->quit()&&!view->failed()&&ticks<4096&&!main_failed()&&total_steps()<10000&&!animation_event_limit_reached&&!sound_event_limit_reached){if(world_tick())++hold_ticks_run;}
            }
            if((!view||(ticks<4096&&!view->quit()&&!view->failed()&&(!view->manual()||view->action())))&&(!main_probe || (!main_failed() && total_steps()<10000 && !animation_event_limit_reached && !sound_event_limit_reached))) {
                previous_steps+=vm.steps();++phase;
                vm.start(odh::routine_key(definition.functions->stand),{1,2,2},{0,0,0,0});
                execute_phase(definition.functions->stand);
            }
        }
        const auto execution_ticks=ticks;
        std::uint16_t post_ticks_run=0;
        const bool terminal=status==odh::VmStatus::returned_true || status==odh::VmStatus::returned_false || status==odh::VmStatus::fault;
        while(reset_entry && terminal && post_ticks_run<post_ticks.value_or(0) && !main_failed() &&
              (!view||ticks<4096) && (!view||(!view->quit()&&!view->failed())) && (!main_probe || total_steps()<10000) && !animation_event_limit_reached && !sound_event_limit_reached) {
            if(world_tick())++post_ticks_run;
        }
#ifdef ODH_HAVE_SDL_AUDIO
        if(audio_service)audio_service->drain(audio_drain_ms.value_or(0));
#endif
        if(view)view->draw(world,*playback,*definition.slots,ticks,phase,true);
        std::cout << "],\"status\":" << quote(odh::vm_status_name(status))
                  << ",\"diagnostic\":" << quote(vm.diagnostic()) << ",\"steps\":" << vm.steps() << ",\"total_steps\":" << total_steps()
                  << ",\"stack_depth\":" << vm.stack().size();
        emit_semiglobal();
        if(stand_after) {
            const auto& functions=*definition.functions;
            std::cout<<",\"stand_selection\":{\"objd_id\":"<<definition.id<<",\"version\":"<<functions.version
                <<",\"main\":"<<functions.main<<",\"sit\":"<<functions.sit<<",\"stand\":"<<functions.stand
                <<",\"uses_function_table\":"<<functions.uses_function_table
                <<",\"archive\":"<<quote(argv[1])<<",\"entry\":"<<quote(argv[2])<<"},\"phases\":[";
            for(std::size_t n=0;n<phases.size();++n) {
                if(n)std::cout<<',';
                const auto& p=phases[n];
                std::cout<<"{\"routine\":"<<p.routine<<",\"status\":"<<quote(odh::vm_status_name(p.status))
                    <<",\"steps\":"<<p.steps<<",\"ticks\":"<<p.ticks<<",\"position\":["<<p.position.x<<','
                    <<p.position.y<<','<<p.position.level<<','<<static_cast<unsigned>(p.position.direction)
                    <<"],\"container\":"<<p.container<<",\"posture\":"<<p.posture;
                if(main_probe)std::cout<<",\"comfort\":"<<p.comfort;
                std::cout<<'}';
            }
            std::cout<<']';
        }
        if(route_probe) {
            std::cout<<",\"placement_validation\":\"constructed raw grid walkability and floor bounds; no dynamic avatar clearance\",\"route_ticks\":"<<ticks<<",\"route_diagnostic\":"<<quote(routing.diagnostic())
                     <<",\"stackobject_slot_resolutions\":"<<stackobject_slot_resolutions
                     <<",\"movements\":[";
            for(std::size_t n=0;n<movements.size();++n) {
                if(n) std::cout<<',';
                const auto& m=movements[n];
                std::cout<<"{\"tick\":"<<m.tick<<",\"entity\":"<<m.entity<<",\"x\":"<<m.position.x
                         <<",\"y\":"<<m.position.y<<",\"direction\":"<<static_cast<unsigned>(m.position.direction)<<'}';
            }
            const auto& p=*world.find(1)->position;
            std::cout<<"],\"final_position\":["<<p.x<<','<<p.y<<','<<p.level<<','<<static_cast<unsigned>(p.direction)
                     <<"],\"route_entry_flags\":"<<world.find(1)->person_data[37];
        }
        if(snap_probe) {
            const auto& actor=*world.find(1);
            std::cout<<",\"idle_notifications\":[";
            for(std::size_t n=0;n<notifications.size();++n){if(n)std::cout<<',';
                std::cout<<"{\"entity\":"<<notifications[n].first<<",\"status\":"<<quote(notifications[n].second)<<'}';}
            std::cout<<']';
            std::cout<<",\"object_binding\":{\"objd_id\":"<<definition.id<<",\"guid\":"<<definition.binding->guid
                <<",\"master_id\":"<<definition.binding->master_id<<",\"sub_index\":"<<definition.binding->sub_index
                <<",\"slot_table\":"<<definition.binding->slot_table<<",\"animation_table\":"<<definition.binding->animation_table
                <<"},\"container\":"<<(actor.attachment?actor.attachment->container:0)
                <<",\"container_slot\":"<<(actor.attachment?static_cast<std::int64_t>(actor.attachment->slot):-1)
                <<",\"posture\":"<<actor.person_data[0];
        }
        if(animation) {
            std::cout<<",\"animation_resources\":[";
            std::vector<std::string> entries{animation->second};
            if(reset_entry && *reset_entry!=animation->second)entries.push_back(*reset_entry);
            entries.insert(entries.end(),animation_extra.begin(),animation_extra.end());
            for(std::size_t n=0;n<entries.size();++n){if(n)std::cout<<',';
                std::cout<<"{\"archive\":"<<quote(animation->first)<<",\"entry\":"<<quote(entries[n])<<'}';}
            std::cout<<']';
            if(reset_entry)std::cout<<",\"reset_resource\":{\"archive\":"<<quote(animation->first)
                <<",\"entry\":"<<quote(*reset_entry)<<",\"global_archive\":"<<quote(global->first)
                <<",\"global_entry\":"<<quote(global->second)<<",\"walk_table_id\":"<<(age.value_or(21)<18?151:150)
                <<",\"language\":1,\"avatar_resource_override\":false,\"hand_slot\":0,\"hand_empty\":true,\"head_seek\":0}"
                <<",\"execution_ticks\":"<<execution_ticks<<",\"post_ticks\":"<<post_ticks_run
                <<",\"requested_post_ticks\":"<<post_ticks.value_or(0);
            std::cout<<",\"simulation_ticks\":"<<ticks<<",\"age\":"<<age.value_or(21)
                <<",\"animation_code_owner\":{\"archive\":"<<quote(argv[1])<<",\"entry\":"<<quote(argv[2])<<",\"language\":1}"
                <<",\"animation_diagnostic\":"<<quote(resolution_diagnostic.empty()?playback->diagnostic():resolution_diagnostic)
                <<",\"animation_event_limit_reached\":"<<(animation_event_limit_reached?"true":"false")
                <<",\"animation_events_truncated\":"<<(animation_events_truncated?"true":"false")
                <<",\"animation_events\":[";
            for(std::size_t n=0;n<animation_events.size();++n) {
                if(n)std::cout<<',';
                const auto& e=animation_events[n];
                std::cout<<"{\"tick\":"<<e.tick<<",\"actor\":"<<e.actor<<",\"time_id\":"<<e.time_id<<",\"properties\":[";
                for(std::size_t j=0;j<e.properties.size();++j) {
                    if(j)std::cout<<',';
                    std::cout<<'['<<quote(e.properties[j].first)<<','<<quote(e.properties[j].second)<<']';
                }
                std::cout<<"]}";
            }
            std::cout<<"],\"animation_progress\":";
            const auto progress=playback->progress();
            if(progress)std::cout<<"{\"name\":"<<quote(progress->name)<<",\"frame\":"<<std::setprecision(9)<<progress->frame
                <<",\"frames\":"<<std::setprecision(9)<<progress->frames<<",\"queued_events\":"<<progress->queued_events
                <<",\"loop\":"<<(progress->loop?"true":"false")
                <<",\"end\":"<<(progress->end?"true":"false")<<'}';
            else std::cout<<"null";
        }
        if(sound_probe) {
            std::cout<<",\"sound_backend\":"<<quote(audio_probe?"linux_pcm_diagnostic":"none")<<",\"sound_code_owner\":{\"archive\":"<<quote(argv[1])<<",\"entry\":"<<quote(argv[2])<<'}'
                <<",\"sound_event_limit_reached\":"<<(sound_event_limit_reached?"true":"false")
                <<",\"sound_events_truncated\":"<<(sound_events_truncated?"true":"false")<<",\"sound_requests\":[";
            for(std::size_t n=0;n<sound_requests.size();++n) {
                if(n)std::cout<<',';
                const auto& e=sound_requests[n];const auto& r=e.request;
                std::cout<<"{\"event_id\":"<<r.event_id<<",\"name\":"<<quote(r.name)<<",\"source\":"<<r.source
                    <<",\"pan\":"<<(r.pan?"true":"false")<<",\"zoom\":"<<(r.zoom?"true":"false")
                    <<",\"loop\":"<<(r.loop?"true":"false")<<",\"sample_rate\":"<<r.sample_rate
                    <<",\"flags\":"<<static_cast<unsigned>(r.flags)<<",\"volume\":"<<static_cast<unsigned>(r.volume)
                    <<",\"status\":"<<quote(e.status);
                if(audio_probe){
                    std::cout<<",\"hit_arguments\":";
                    if(e.arguments)std::cout<<'['<<(*e.arguments)[0]<<','<<(*e.arguments)[1]<<','<<(*e.arguments)[2]<<']';
                    else std::cout<<"null";
                }
                if(audio_native_controls){
                    std::cout<<",\"hit_native_controls\":{";
                    for(std::size_t i=0;i<odh_host::native_control_receipt_ids.size();++i){
                        if(i)std::cout<<',';
                        std::cout<<'"'<<static_cast<unsigned>(odh_host::native_control_receipt_ids[i])<<"\":";
                        if(e.native_controls && (*e.native_controls)[i])std::cout<<*(*e.native_controls)[i];
                        else std::cout<<"null";
                    }
                    std::cout<<'}';
                }
                std::cout<<'}';
            }
            std::cout<<']';
#ifdef ODH_HAVE_SDL_AUDIO
            if(audio_service){std::cout<<",\"audio_probe\":";audio_service->emit_json(std::cout,ticks);}
#endif
        }
        if(main_probe) {
            const auto scene=view&&view->failed()?"view_fault":view&&ticks>=4096?"budget_exhausted":main_vm->status()==odh::VmStatus::fault || status==odh::VmStatus::fault?"fault":
                total_steps()>=10000 || main_vm->status()==odh::VmStatus::budget_exhausted || status==odh::VmStatus::budget_exhausted?"budget_exhausted":
                status==odh::VmStatus::returned_true || status==odh::VmStatus::returned_false?"returned":"incomplete";
            std::cout<<",\"scene_status\":"<<quote(scene)<<",\"requested_hold_ticks\":"<<hold_ticks.value_or(0)
                <<",\"actual_hold_ticks\":"<<hold_ticks_run<<",\"comfort_initial\":0,\"comfort_final\":"<<world.find(1)->motives[6]
                <<",\"avatar_definition_profile\":{\"source\":\"constructed_definition\",\"object_type\":2}"
                <<",\"object_thread\":{\"routine\":"<<definition.functions->main<<",\"status\":"<<quote(odh::vm_status_name(main_vm->status()))
                <<",\"steps\":"<<main_vm->steps()<<",\"diagnostic\":"<<quote(main_vm->diagnostic())
                <<",\"rng_backend\":\"none\",\"rng_cycles_semantics\":\"requested advances only\",\"rng_cycles\":"<<rng_cycles<<",\"clock\":"<<main_sleep.clock()<<",\"trace\":[";
            for(std::size_t n=0;n<main_trace.size();++n) {
                if(n)std::cout<<',';
                const auto& e=main_trace[n];
                std::cout<<"{\"step\":"<<e.step<<",\"depth\":"<<e.depth<<",\"routine\":"<<e.key.id<<",\"instruction\":";
                instruction(e.instruction,e.pc);std::cout<<'}';
            }
            std::cout<<"]}";
        }
        if(view)std::cout<<",\"view\":"<<view->report();
        std::cout<<"}\n";
        if(view&&view->failed()){std::cerr<<"odh-bhav view: "<<view->diagnostic()<<'\n';return 2;}
#ifdef ODH_HAVE_SDL_AUDIO
        if(audio_service && audio_service->unhealthy())return 3;
#endif
        if(view&&view->quit())return 0; // Explicit user close; JSON retains incomplete VM state.
        if(view&&ticks>=4096)return 3;
        if(main_probe && (main_failed() || total_steps()>=10000))return 3;
        return status == odh::VmStatus::returned_true || status == odh::VmStatus::returned_false ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << "odh-bhav: " << e.what() << '\n';
        return 2;
    }
}
