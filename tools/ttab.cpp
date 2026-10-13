#include "odh/formats.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: odh-ttab ARCHIVE.far ENTRY.iff CHUNK_ID\n"
                     "Read-only TS1 interaction table; IDs decimal or 0x-prefixed.\n";
        return 2;
    }
    try {
        const std::string id_text = argv[3];
        std::size_t used = 0;
        const bool hexadecimal = id_text.size() > 2 && id_text[0] == '0' &&
                                 (id_text[1] == 'x' || id_text[1] == 'X');
        const auto id = std::stoull(id_text, &used, hexadecimal ? 16 : 10);
        if (used != id_text.size() || id > 65535) throw odh::FormatError("invalid chunk ID");
        std::ifstream stream(argv[1], std::ios::binary);
        if (!stream) throw odh::FormatError("cannot open archive");
        const odh::View archive(stream, std::filesystem::file_size(argv[1]));
        std::optional<odh::FarEntry> selected;
        for (const auto& entry : odh::far1_index(archive)) {
            if (entry.name != argv[2]) continue;
            if (selected) throw odh::FormatError("ambiguous archive entry");
            selected = entry;
        }
        if (!selected) throw odh::FormatError("archive entry not found (exact name required)");
        std::optional<odh::Ttab> table;
        odh::iff_chunks(archive.subview(selected->offset, selected->length), [&](const odh::Chunk& c) {
            if (c.type != "TTAB" || c.id != id) return;
            if (table) throw odh::FormatError("ambiguous TTAB chunk ID");
            table = odh::read_ttab(c.payload);
        });
        if (!table) throw odh::FormatError("TTAB chunk not found");
        const char* state = table->status == odh::TtabStatus::decoded ? "decoded" :
            table->status == odh::TtabStatus::empty ? "empty" :
            table->status == odh::TtabStatus::legacy_unsupported ? "legacy_unsupported" : "absent_payload";
        std::cout << "{\"evidence\":\"decoded_fields_only\",\"status\":\"" << state
                  << "\",\"version\":";
        if (table->version) std::cout << *table->version; else std::cout << "null";
        std::cout << ",\"declared_count\":" << table->declared_count
                  << ",\"compressed\":" << (table->compressed ? "true" : "false")
                  << ",\"remaining_bits\":" << table->remaining_bits << ",\"interactions\":[";
        bool first = true;
        for (const auto& row : table->interactions) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"action\":" << row.action << ",\"test\":" << row.test
                      << ",\"flags\":" << row.flags << ",\"label_index\":" << row.label_index
                      << ",\"attenuation_code\":" << row.attenuation_code
                      << ",\"attenuation_bits\":" << row.attenuation_bits
                      << ",\"autonomy_threshold\":" << row.autonomy_threshold
                      << ",\"joining_index\":" << row.joining_index << ",\"motives\":[";
            bool first_motive = true;
            for (const auto& motive : row.motives) {
                if (!first_motive) std::cout << ',';
                first_motive = false;
                std::cout << "{\"minimum\":" << motive.minimum << ",\"delta\":" << motive.delta
                          << ",\"personality_modifier\":" << motive.personality_modifier << '}';
            }
            std::cout << "]}";
        }
        std::cout << "]}\n";
        return table->status == odh::TtabStatus::legacy_unsupported ||
               table->status == odh::TtabStatus::absent_payload ? 3 : 0;
    } catch (const std::exception& error) {
        std::cerr << "odh-ttab: " << error.what() << '\n';
        return 2;
    }
}
