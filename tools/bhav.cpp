#include "odh/vm.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
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
          bool globals) {
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
        if (c.type != "BHAV") return;
        const auto key = odh::routine_key(c.id);
        if (globals && key.scope != odh::RoutineScope::global)
            throw odh::FormatError("non-global routine in explicit global resource");
        if (!globals && key.scope == odh::RoutineScope::global)
            throw odh::FormatError("global routine in private resource: explicit provenance required");
        if (!routines.emplace(key, odh::read_bhav(c.payload)).second)
            throw odh::FormatError("ambiguous routine ID");
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
                     "Default: structural dump. --run: bounded VM probe without world services.\n";
        return 2;
    }
    try {
        bool run = false;
        std::optional<std::pair<std::string, std::string>> global;
        for (int n = 4; n < argc; ++n) {
            const std::string option = argv[n];
            if (option == "--run" && !run) run = true;
            else if (option == "--global" && !global && n + 2 < argc) {
                global = std::make_pair(argv[n + 1], argv[n + 2]); n += 2;
            } else throw odh::FormatError("invalid or repeated option");
        }
        const auto key = odh::routine_key(identifier(argv[3]));
        odh::RoutineSet routines;
        load(argv[1], argv[2], routines, false);
        if (global) load(global->first, global->second, routines, true);
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
        odh::Vm vm(std::move(routines));
        vm.start(key); // Zero IDs are unbound: world scopes will diagnose missing support.
        std::cout << "{\"evidence\":\"ODH_VM_probe_without_world\",\"trace\":[";
        bool first = true;
        const auto status = vm.run(10000, [&](const odh::VmTrace& event) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"step\":" << event.step << ",\"depth\":" << event.depth
                      << ",\"routine\":" << event.key.id << ",\"instruction\":";
            instruction(event.instruction, event.pc);
            std::cout << '}';
        });
        std::cout << "],\"status\":" << quote(odh::vm_status_name(status))
                  << ",\"diagnostic\":" << quote(vm.diagnostic()) << ",\"steps\":" << vm.steps()
                  << ",\"stack_depth\":" << vm.stack().size() << "}\n";
        return status == odh::VmStatus::returned_true || status == odh::VmStatus::returned_false ? 0 : 3;
    } catch (const std::exception& e) {
        std::cerr << "odh-bhav: " << e.what() << '\n';
        return 2;
    }
}
