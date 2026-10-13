#include "odh/routing.hpp"
#include "odh/snap.hpp"

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
          bool globals,std::optional<odh::SlotTable>* slots=nullptr,DefinitionSelection* definition=nullptr) {
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
                     "Default: structural dump. --run: bounded VM probe without world services.\n";
        return 2;
    }
    try {
        bool run = false;
        bool world_probe = false, autonomous = false;
        bool route_probe=false;
        bool snap_probe=false;
        std::optional<std::uint16_t> object_def;
        std::optional<std::uint16_t> occupant, posture;
        std::optional<std::pair<std::string, std::string>> global;
        for (int n = 4; n < argc; ++n) {
            const std::string option = argv[n];
            if (option == "--run" && !run) run = true;
            else if (option == "--world-probe" && !world_probe) world_probe = true;
            else if (option == "--route-probe" && !route_probe) route_probe=true;
            else if (option == "--snap-probe" && !snap_probe) snap_probe=true;
            else if (option == "--object-def" && !object_def && n+1<argc) object_def=identifier(argv[++n]);
            else if (option == "--autonomous" && !autonomous) autonomous = true;
            else if (option == "--occupant" && !occupant && n + 1 < argc) occupant = identifier(argv[++n]);
            else if (option == "--posture" && !posture && n + 1 < argc) posture = identifier(argv[++n]);
            else if (option == "--global" && !global && n + 2 < argc) {
                global = std::make_pair(argv[n + 1], argv[n + 2]); n += 2;
            } else throw odh::FormatError("invalid or repeated option");
        }
        if ((world_probe && !run) || (!world_probe && (occupant || posture || autonomous)) ||
            (route_probe && (!world_probe || !global || occupant.value_or(0)!=0)) ||
            (snap_probe && (!route_probe || !object_def)) || (!snap_probe && object_def) ||
            (occupant && *occupant != 0 && *occupant != 1 && *occupant != 3) ||
            (posture && *posture > 32767)) throw odh::FormatError("invalid constructed-world options");
        const auto key = odh::routine_key(identifier(argv[3]));
        odh::RoutineSet routines;
        std::optional<odh::SlotTable> routing_slots;
        DefinitionSelection definition{object_def.value_or(0),{}, {}};
        load(argv[1], argv[2], routines, false,nullptr,snap_probe?&definition:nullptr);
        if (global) load(global->first, global->second, routines, true,route_probe?&routing_slots:nullptr);
        if(route_probe && !routing_slots) throw odh::FormatError("route probe requires global SLOT100");
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
            if (!world.create(1,odh::EntityKind::avatar,{0,88,0}) ||
                !world.create(2,odh::EntityKind::object,{0,0,storage_slots}) ||
                !world.create(3,odh::EntityKind::avatar,{0,88,0}) ||
                !world.set_person_data(1,0,static_cast<std::int16_t>(posture.value_or(0))) ||
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
        const auto route_handler=routing.primitive();
        const auto snap_handler=snap_probe?snap->primitive():odh::VmPrimitive{};
        odh::VmPrimitive handler;
        if(route_probe) handler=[&](const auto& f,const auto& i,const auto& t,const auto& d) {
            return i.opcode==46 && snap_handler?snap_handler(f,i,t,d):route_handler(f,i,t,d);
        };
        odh::Vm vm(std::move(routines),world_probe ? world.vm_memory() : odh::VmMemory{}, {},
                   handler);
        routing.bind(vm);
        if(snap_probe) snap->bind(vm);
        vm.start(key,world_probe ? odh::VmContext{1,2,2} : odh::VmContext{},
                 {static_cast<std::int16_t>(autonomous ? 1 : 0)});
        std::cout << "{\"evidence\":" << quote(snap_probe?"ODH_VM_constructed_route_snap_world":
                                                       route_probe?"ODH_VM_constructed_route_world":
                                                       world_probe ? "ODH_VM_constructed_world" : "ODH_VM_probe_without_world");
        if (world_probe) std::cout << ",\"initial_world\":{\"caller\":1,\"callee\":2,\"stack_object\":2,\"occupant\":"
            << occupant.value_or(0) << ",\"posture\":" << posture.value_or(0)
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
        auto status=vm.run(10000,trace);
        std::uint64_t ticks=0;
        std::vector<odh::RouteMovement> movements;
        while(route_probe && status==odh::VmStatus::waiting && ticks<4096) {
            routing.tick(++ticks,[&](const auto& movement){movements.push_back(movement);});
            status=vm.run(vm.steps()<10000?10000-vm.steps():0,trace);
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
                <<",\"slot_table\":"<<definition.binding->slot_table<<",\"animation_table\":"<<definition.binding->animation_table
                <<"},\"container\":"<<(actor.attachment?actor.attachment->container:0)
                <<",\"container_slot\":"<<(actor.attachment?static_cast<std::int64_t>(actor.attachment->slot):-1)
                <<",\"posture\":"<<actor.person_data[0];
        }
        std::cout<<"}\n";
        return status == odh::VmStatus::returned_true || status == odh::VmStatus::returned_false ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << "odh-bhav: " << e.what() << '\n';
        return 2;
    }
}
