#include "odh/routing.hpp"
#include "odh/snap.hpp"
#include "odh/playback.hpp"
#include "odh/object_type.hpp"
#include "odh/sound.hpp"
#include "resource.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
struct DefinitionSelection {
    std::uint16_t id;
    std::optional<odh::ObjdBinding> binding;
    std::optional<odh::SlotTable> slots;
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
void load(const std::string& path, const std::string& name, odh::RoutineSet& routines,
          bool globals,std::optional<odh::SlotTable>* slots=nullptr,DefinitionSelection* definition=nullptr,
          std::map<std::uint16_t,odh::SoundEvent>* sounds=nullptr) {
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
        if(definition && c.type=="OBJD" && c.id==definition->id) {
            if(definition->binding) throw odh::FormatError("ambiguous selected OBJD");
            definition->binding=odh::read_objd_binding(c.payload);
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
        if (c.type != "BHAV") return;
        const auto key = odh::routine_key(c.id);
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
                     "  [--world-probe [--occupant 0|1|3] [--posture 0..32767] [--autonomous]]\n"
                     "  [--route-probe] requires --world-probe and --global; constructed clearance grid.\n"
                     "  [--snap-probe --object-def ID] also requires --route-probe; explicit OBJD binding.\n"
                     "  [--animation-probe ARCHIVE.far ENTRY.bcf [--age 0..32767]] requires --snap-probe; silent animation.\n"
                     "  [--reset-probe ENTRY.bcf [--post-ticks 0..4096]] requires --animation-probe; explicit global posture loops.\n"
                     "  [--sound-probe] requires --animation-probe; silent FWAV requests, no audio backend.\n"
                     "Default: structural dump. --run: bounded VM probe without world services.\n";
        return 2;
    }
    try {
        bool run = false;
        bool world_probe = false, autonomous = false;
        bool route_probe=false;
        bool snap_probe=false, sound_probe=false;
        std::optional<std::uint16_t> object_def;
        std::optional<std::uint16_t> occupant, posture, age, post_ticks;
        std::optional<std::string> reset_entry;
        std::optional<std::pair<std::string,std::string>> animation;
        std::optional<std::pair<std::string, std::string>> global;
        for (int n = 4; n < argc; ++n) {
            const std::string option = argv[n];
            if (option == "--run" && !run) run = true;
            else if (option == "--world-probe" && !world_probe) world_probe = true;
            else if (option == "--route-probe" && !route_probe) route_probe=true;
            else if (option == "--sound-probe" && !sound_probe) sound_probe=true;
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
            else if (option == "--global" && !global && n + 2 < argc) {
                global = std::make_pair(argv[n + 1], argv[n + 2]); n += 2;
            } else throw odh::FormatError("invalid or repeated option");
        }
        if ((world_probe && !run) || (!world_probe && (occupant || posture || autonomous)) ||
            (route_probe && (!world_probe || !global || occupant.value_or(0)!=0)) ||
            (snap_probe && (!route_probe || !object_def)) || (!snap_probe && object_def) ||
            (occupant && *occupant != 0 && *occupant != 1 && *occupant != 3) ||
            (posture && *posture > 32767) || (animation && !snap_probe) ||
            (age && (!animation || *age>32767)) || (sound_probe && !animation) || (reset_entry && !animation) ||
            (post_ticks && (!reset_entry || *post_ticks>4096))) throw odh::FormatError("invalid constructed-world options");
        const auto key = odh::routine_key(identifier(argv[3]));
        odh::RoutineSet routines;
        std::optional<odh::SlotTable> routing_slots;
        DefinitionSelection definition{object_def.value_or(0),{}, {}};
        std::map<std::uint16_t,odh::SoundEvent> private_sounds,global_sounds;
        load(argv[1], argv[2], routines, false,nullptr,snap_probe?&definition:nullptr,sound_probe?&private_sounds:nullptr);
        if (global) load(global->first, global->second, routines, true,route_probe?&routing_slots:nullptr,nullptr,sound_probe?&global_sounds:nullptr);
        if(route_probe && !routing_slots) throw odh::FormatError("route probe requires global SLOT100");
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
            load_clips(animation->second);
            if(reset_entry && *reset_entry!=animation->second)load_clips(*reset_entry);
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
            std::cout << "]}\n";
            return 0;
        }
        odh::World world;
        std::size_t storage_slots=1;
        if(snap_probe) {storage_slots=0;for(const auto& r:definition.slots->records) if(r.type==0) ++storage_slots;}
        if (world_probe) {
            if (!world.create(1,odh::EntityKind::avatar,{0,88,reset_entry?1u:0u}) ||
                !world.create(2,odh::EntityKind::object,{0,0,storage_slots}) ||
                !world.create(3,odh::EntityKind::avatar,{0,88,0}) ||
                !world.set_person_data(1,0,static_cast<std::int16_t>(posture.value_or(0))) ||
                (animation && (!world.set_person_data(1,58,static_cast<std::int16_t>(age.value_or(21))) ||
                               !world.declare_object_word(1,17,0))) ||
                (occupant.value_or(0) && !world.attach(2,0,static_cast<std::int16_t>(*occupant))))
                throw odh::FormatError("cannot construct probe world");
        }
        odh::RouteGrid grid(128,128);
        if(route_probe) {
            if(!world.set_position(1,{16,64,1,0}) || !world.set_position(2,{64,64,1,0}))
                throw odh::FormatError("cannot position route probe entities");
            for(int y=55;y<=73;++y) for(int x=55;x<=73;++x) grid.block({x,y});
            for(int y=0;y<128;++y) if(y<50 || y>55) grid.wall({39,y},{40,y});
        }
        const odh::SlotTable empty_slots{0,10,{},0};
        odh::Routing routing(world,grid,routing_slots?*routing_slots:empty_slots);
        std::optional<odh::Snap> snap;
        if(snap_probe) snap.emplace(world,2,*definition.binding,*definition.slots,definition.binding->slot_table);
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
        struct ObservedSound {odh::SoundRequest request;std::string status;};
        std::vector<ObservedSound> sound_requests;
        bool sound_events_truncated=false,sound_event_limit_reached=false;
        const auto sound_handler=sound_probe?odh::sound_primitive(world,
            [&](const odh::VmFrame&,std::uint16_t id)->std::optional<odh::SoundEvent> {
                auto selected=private_sounds.find(id);
                if(selected!=private_sounds.end())return selected->second;
                selected=global_sounds.find(id);
                if(selected!=global_sounds.end())return selected->second;
                return {};
            },{},[&](const odh::SoundRequest& request,const std::string& status) {
                if(sound_requests.size()<4096)sound_requests.push_back({request,status});
                else sound_events_truncated=true;
                if(sound_requests.size()==4096)sound_event_limit_reached=true;
            }):odh::VmPrimitive{};
        const auto type_handler=snap_probe?odh::object_type_primitive(world):odh::VmPrimitive{};
        const auto animation_handler=animation?playback->primitive():odh::VmPrimitive{};
        const auto route_handler=routing.primitive();
        const auto snap_handler=snap_probe?snap->primitive():odh::VmPrimitive{};
        odh::VmPrimitive handler;
        if(route_probe) handler=[&](const auto& f,const auto& i,const auto& t,const auto& d) {
            if(i.opcode==32 && type_handler)return type_handler(f,i,t,d);
            if(i.opcode==23 && sound_handler)return sound_handler(f,i,t,d);
            if(i.opcode==44 && animation_handler)return animation_handler(f,i,t,d);
            return i.opcode==46 && snap_handler?snap_handler(f,i,t,d):route_handler(f,i,t,d);
        };
        odh::Vm vm(std::move(routines),world_probe ? world.vm_memory() : odh::VmMemory{}, {},
                   handler);
        routing.bind(vm);
        if(snap_probe) snap->bind(vm);
        if(animation && !playback->bind(vm))throw odh::FormatError("cannot bind animation probe");
        vm.start(key,world_probe ? odh::VmContext{1,2,2} : odh::VmContext{},
                 {static_cast<std::int16_t>(autonomous ? 1 : 0)});
        std::cout << "{\"evidence\":" << quote(sound_probe?"ODH_VM_constructed_route_snap_silent_animation_sound_requests_world":animation?"ODH_VM_constructed_route_snap_silent_animation_world":snap_probe?"ODH_VM_constructed_route_snap_world":
                                                       route_probe?"ODH_VM_constructed_route_world":
                                                       world_probe ? "ODH_VM_constructed_world" : "ODH_VM_probe_without_world");
        if (world_probe) std::cout << ",\"initial_world\":{\"caller\":1,\"callee\":2,\"stack_object\":2,\"occupant\":"
            << occupant.value_or(0) << ",\"posture\":" << posture.value_or(0)
            << ",\"age\":" << (animation?age.value_or(21):0)
            << ",\"autonomous\":" << (autonomous ? "true" : "false") << '}';
        std::cout << ",\"trace\":[";
        bool first = true;
        const auto trace=[&](const odh::VmTrace& event) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"step\":" << event.step << ",\"depth\":" << event.depth
                      << ",\"routine\":" << event.key.id << ",\"instruction\":";
            instruction(event.instruction, event.pc);
            std::cout << '}';
        };
        const auto run_probe=[&]() {
            if(!sound_probe)return vm.run(vm.steps()<10000?10000-vm.steps():0,trace);
            auto state=vm.status();
            while((state==odh::VmStatus::running || state==odh::VmStatus::budget_exhausted) &&
                  vm.steps()<10000 && !sound_event_limit_reached)state=vm.run(1,trace);
            return state;
        };
        auto status=run_probe();
        std::uint64_t ticks=0;
        std::vector<odh::RouteMovement> movements;
        std::vector<odh::PlaybackEvent> animation_events;
        bool animation_events_truncated=false, animation_event_limit_reached=false;
        while(route_probe && status==odh::VmStatus::waiting && ticks<4096 && !animation_event_limit_reached && !sound_event_limit_reached) {
            routing.tick(++ticks,[&](const auto& movement){movements.push_back(movement);});
            if(animation)playback->tick(ticks,[&](const auto& event){
                if(animation_events.size()<4096)animation_events.push_back(event);
                else animation_events_truncated=true;
            });
            if(animation_events.size()==4096)animation_event_limit_reached=true;
            status=run_probe();
        }
        const auto execution_ticks=ticks;
        std::uint16_t post_ticks_run=0;
        const bool terminal=status==odh::VmStatus::returned_true || status==odh::VmStatus::returned_false || status==odh::VmStatus::fault;
        while(reset_entry && terminal && post_ticks_run<post_ticks.value_or(0) && !animation_event_limit_reached && !sound_event_limit_reached) {
            ++post_ticks_run;
            routing.tick(++ticks,[&](const auto& movement){movements.push_back(movement);});
            playback->tick(ticks,[&](const auto& event){
                if(animation_events.size()<4096)animation_events.push_back(event);
                else animation_events_truncated=true;
            });
            if(animation_events.size()==4096)animation_event_limit_reached=true;
        }
        std::cout << "],\"status\":" << quote(odh::vm_status_name(status))
                  << ",\"diagnostic\":" << quote(vm.diagnostic()) << ",\"steps\":" << vm.steps()
                  << ",\"stack_depth\":" << vm.stack().size();
        if(route_probe) {
            std::cout<<",\"route_ticks\":"<<ticks<<",\"route_diagnostic\":"<<quote(routing.diagnostic())
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
            std::cout<<",\"object_binding\":{\"objd_id\":"<<definition.id<<",\"guid\":"<<definition.binding->guid
                <<",\"master_id\":"<<definition.binding->master_id<<",\"sub_index\":"<<definition.binding->sub_index
                <<",\"slot_table\":"<<definition.binding->slot_table<<",\"animation_table\":"<<definition.binding->animation_table
                <<"},\"container\":"<<(actor.attachment?actor.attachment->container:0)
                <<",\"container_slot\":"<<(actor.attachment?static_cast<std::int64_t>(actor.attachment->slot):-1)
                <<",\"posture\":"<<actor.person_data[0];
        }
        if(animation) {
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
            std::cout<<",\"sound_backend\":\"none\",\"sound_code_owner\":{\"archive\":"<<quote(argv[1])<<",\"entry\":"<<quote(argv[2])<<'}'
                <<",\"sound_event_limit_reached\":"<<(sound_event_limit_reached?"true":"false")
                <<",\"sound_events_truncated\":"<<(sound_events_truncated?"true":"false")<<",\"sound_requests\":[";
            for(std::size_t n=0;n<sound_requests.size();++n) {
                if(n)std::cout<<',';
                const auto& e=sound_requests[n];const auto& r=e.request;
                std::cout<<"{\"event_id\":"<<r.event_id<<",\"name\":"<<quote(r.name)<<",\"source\":"<<r.source
                    <<",\"pan\":"<<(r.pan?"true":"false")<<",\"zoom\":"<<(r.zoom?"true":"false")
                    <<",\"loop\":"<<(r.loop?"true":"false")<<",\"sample_rate\":"<<r.sample_rate
                    <<",\"flags\":"<<static_cast<unsigned>(r.flags)<<",\"volume\":"<<static_cast<unsigned>(r.volume)
                    <<",\"status\":"<<quote(e.status)<<'}';
            }
            std::cout<<']';
        }
        std::cout<<"}\n";
        return status == odh::VmStatus::returned_true || status == odh::VmStatus::returned_false ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << "odh-bhav: " << e.what() << '\n';
        return 2;
    }
}
