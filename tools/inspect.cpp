#include "odh/formats.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using Counts = std::map<std::string, std::uint64_t>;

namespace {
std::string normalized(std::string name) {
    for (auto& c : name) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return name;
}
std::string extension(const std::string& name) {
    return normalized(fs::path(name).extension().string());
}
bool iff_candidate(const std::string& name) {
    const auto ext = extension(name);
    return ext == ".iff" || ext == ".stx" || ext == ".spf" || ext == ".wll" || ext == ".flr";
}
std::string hex(std::uint16_t n) {
    std::ostringstream out;
    out << "0x" << std::hex << n;
    return out.str();
}
std::string quoted(const std::string& input) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : input) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32 || c >= 127)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c);
        else out << static_cast<char>(c);
    }
    out << '"';
    return out.str();
}
void emit(const Counts& counts) {
    bool first = true;
    std::cout << '{';
    for (const auto& item : counts) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << quoted(item.first) << ':' << item.second;
    }
    std::cout << '}';
}
struct Census {
    Counts totals, chunks, versions, primitives, calls, slot_types;
    void merge(const Census& other) {
        for (const auto pair : {std::pair<Counts*, const Counts*>(&totals, &other.totals),
                               {&chunks, &other.chunks}, {&versions, &other.versions},
                               {&primitives, &other.primitives}, {&calls, &other.calls},
                               {&slot_types, &other.slot_types}})
            for (const auto& item : *pair.second) (*pair.first)[item.first] += item.second;
    }
};
Census inspect_iff(const odh::View& view) {
    Census result;
    const auto version = odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
        ++result.chunks[chunk.type];
        if (chunk.type == "BHAV") {
            const auto routine = odh::read_bhav(chunk.payload);
            ++result.versions["BHAV " + hex(routine.version)];
            ++result.totals["bhav_routines"];
            result.totals["bhav_instructions"] += routine.instructions.size();
            for (const auto& instruction : routine.instructions) {
                if (instruction.opcode < 256) ++result.primitives[hex(instruction.opcode)];
                else ++result.calls[instruction.opcode >= 0x2000 ? "semi_global" :
                                    instruction.opcode >= 0x1000 ? "private" : "global"];
            }
        } else if (chunk.type == "SPR2") {
            ++result.versions["SPR2 " + std::to_string(chunk.payload.le32(0))];
            odh::spr2_frames(chunk.payload, [&](const odh::SpriteFrame& frame) {
                ++result.totals["spr2_frames"];
                const auto pixels = static_cast<std::uint64_t>(frame.width) * frame.height;
                result.totals["spr2_rectangular_pixels"] += pixels;
                if (frame.flags & 2) {
                    ++result.totals["spr2_frames_with_z_flag"];
                    result.totals["spr2_rectangular_z_pixels"] += pixels;
                }
                if (frame.flags & 4) ++result.totals["spr2_frames_with_alpha_flag"];
            });
        } else if (chunk.type == "DGRP") {
            ++result.versions["DGRP " + std::to_string(chunk.payload.le16(0))];
        } else if (chunk.type == "OBJD") {
            ++result.versions["OBJD " + std::to_string(chunk.payload.le32(0))];
        } else if (chunk.type == "SPR#") {
            const bool big = chunk.payload.le16(0) == 0;
            ++result.versions[std::string("SPR# ") + (big ? "BE " : "LE ") +
                std::to_string(big ? chunk.payload.be32(0) : chunk.payload.le32(0))];
        } else if (chunk.type == "SLOT") {
            const auto table = odh::read_slot(chunk.payload);
            ++result.versions["SLOT " + std::to_string(table.version)];
            ++result.totals["slot_tables"];
            result.totals["slot_records"] += table.records.size();
            result.totals["slot_remaining_bytes"] += table.remaining_bytes;
            for (const auto& record : table.records)
                ++result.slot_types[std::to_string(record.type)];
        } else if (chunk.type == "TTAB") {
            const auto table = odh::read_ttab(chunk.payload);
            const std::string state = table.status == odh::TtabStatus::decoded ? "decoded" :
                table.status == odh::TtabStatus::empty ? "empty" :
                table.status == odh::TtabStatus::legacy_unsupported ? "legacy_unsupported" : "absent_payload";
            ++result.totals["ttab_" + state];
            if (table.version) ++result.versions["TTAB " + std::to_string(*table.version)];
            if (table.compressed) ++result.totals["ttab_compressed_decoded"];
            result.totals["ttab_interactions_decoded"] += table.interactions.size();
            result.totals["ttab_remaining_bits"] += table.remaining_bits;
            for (const auto& interaction : table.interactions)
                result.totals["ttab_motive_entries_decoded"] += interaction.motives.size();
        }
    });
    ++result.versions["IFF " + version];
    ++result.totals["iff_containers"];
    return result;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::string(argv[1]) == "--help") {
        std::cerr << "Usage: odh-inspect GAME_DIRECTORY\n"
                     "Read-only FAR/IFF/BHAV and SPR2-header census. JSON on stdout.\n"
                     "Does not extract assets, execute scripts or establish compatibility.\n";
        return argc == 2 ? 0 : 2;
    }
    try {
        const fs::path root = argv[1];
        if (!fs::is_directory(root)) throw odh::FormatError("game directory not found");
        std::vector<fs::path> files;
        for (const auto& entry : fs::recursive_directory_iterator(root))
            if (entry.is_regular_file() && (extension(entry.path().string()) == ".far" ||
                                           iff_candidate(entry.path().string())))
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        if (files.empty()) throw odh::FormatError("no supported input files found");
        Census census;
        census.totals["selected_input_files"] = files.size();
        Counts extensions, names;
        std::uint64_t errors = 0;
        auto inspect = [&](const odh::View& view, const std::string& origin) {
            try { census.merge(inspect_iff(view)); }
            catch (const std::exception& error) {
                ++errors;
                std::cerr << origin << ": " << error.what() << '\n';
            }
        };
        for (const auto& path : files) {
            const auto origin = fs::relative(path, root).generic_string();
            try {
                std::ifstream input(path, std::ios::binary);
                if (!input) throw odh::FormatError("cannot open file");
                const odh::View view(input, fs::file_size(path));
                if (extension(path.string()) == ".far") {
                    const auto entries = odh::far1_index(view);
                    ++census.totals["far_files"];
                    census.totals["far_entries"] += entries.size();
                    for (const auto& entry : entries) {
                        ++extensions[extension(entry.name)];
                        ++names[normalized(entry.name)];
                        if (iff_candidate(entry.name))
                            inspect(view.subview(entry.offset, entry.length), origin + "::" + entry.name);
                    }
                } else {
                    inspect(view, origin);
                }
            } catch (const std::exception& error) {
                ++errors;
                std::cerr << origin << ": " << error.what() << '\n';
            }
        }
        census.totals["far_unique_normalized_names"] = names.size();
        census.totals["far_repeated_normalized_names"] =
            static_cast<std::uint64_t>(std::count_if(names.begin(), names.end(),
                [](const auto& item) { return item.second > 1; }));
        census.totals["distinct_primitive_opcodes"] = census.primitives.size();
        census.totals["errors"] = errors;
        std::cout << "{\"schema_version\":1,\"evidence\":\"static_structure_only\",\"totals\":";
        emit(census.totals);
        for (const auto pair : {std::pair<const char*, const Counts*>("far_extensions", &extensions),
                               {"chunks", &census.chunks}, {"versions", &census.versions},
                               {"primitive_occurrences", &census.primitives}, {"direct_call_occurrences", &census.calls},
                               {"slot_type_occurrences", &census.slot_types}}) {
            std::cout << ',' << quoted(pair.first) << ':';
            emit(*pair.second);
        }
        std::cout << "}\n";
        return errors == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "odh-inspect: " << error.what() << '\n';
        return 2;
    }
}
