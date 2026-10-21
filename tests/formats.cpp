#include "odh/formats.hpp"

#include <iostream>
#include <limits>
#include <sstream>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const odh::FormatError&) { rejected = true; }
    require(rejected, message);
}
void put(std::string& bytes, std::size_t offset, std::uint32_t value, unsigned width, bool big = false) {
    for (unsigned i = 0; i < width; ++i)
        bytes.at(offset + i) = static_cast<char>((value >> (8 * (big ? width - i - 1 : i))) & 255);
}
template<class F> void use(const std::string& bytes, F operation) {
    std::istringstream input(bytes, std::ios::binary);
    operation(odh::View(input, bytes.size()));
}
std::string bhav() {
    std::string b(36, '\0');
    put(b, 0, 0x8002, 2); put(b, 2, 2, 2);
    put(b, 12, 0x100F, 2); put(b, 14, 1, 1); put(b, 15, 0xFE, 1);
    put(b, 24, 2, 2); put(b, 26, 0xFF, 1); put(b, 27, 0xFD, 1);
    put(b, 28, 0xAB, 1);
    return b;
}
std::string iff(const std::string& payload,const std::string& label={}) {
    std::string b(140, '\0');
    const std::string prefix = "IFF FILE 2.5:TYPE FOLLOWED BY SIZE";
    const std::string credit = " JAMIE DOORNBOS & MAXIS 1";
    b.replace(0, prefix.size(), prefix);
    b.replace(prefix.size() + 1, credit.size(), credit);
    b.replace(64, 4, "BHAV");
    put(b, 68, static_cast<std::uint32_t>(76 + payload.size()), 4, true);
    put(b, 72, 0x1234, 2, true); put(b, 74, 0xBEEF, 2, true);
    b.replace(76,label.size(),label);
    return b + payload;
}
std::string far(const std::string& asset) {
    const auto manifest = static_cast<std::uint32_t>(16 + asset.size());
    std::string b(16, '\0'); b.replace(0, 8, "FAR!byAZ");
    put(b, 8, 1, 4); put(b, 12, manifest, 4);
    b += asset; b.resize(manifest + 20, '\0');
    put(b, manifest, 1, 4);
    put(b, manifest + 4, static_cast<std::uint32_t>(asset.size()), 4);
    put(b, manifest + 8, static_cast<std::uint32_t>(asset.size()), 4);
    put(b, manifest + 12, 16, 4); put(b, manifest + 16, 8, 4);
    b += "test.iff";
    return b;
}
std::string far_names(const std::vector<std::string>& names) {
    std::string bytes(20, '\0');
    bytes.replace(0, 8, "FAR!byAZ");
    put(bytes, 8, 1, 4); put(bytes, 12, 16, 4);
    put(bytes, 16, static_cast<std::uint32_t>(names.size()), 4);
    for (const auto& name : names) {
        const auto offset = bytes.size();
        bytes.resize(offset + 16, '\0');
        put(bytes, offset + 8, 16, 4);
        put(bytes, offset + 12, static_cast<std::uint32_t>(name.size()), 4);
        bytes += name;
    }
    return bytes;
}
} // namespace

int main() {
    try {
        std::string constants(8, '\0');
        put(constants, 0, 3, 1); put(constants, 1, 128, 1);
        put(constants, 2, 1, 2); put(constants, 4, 0x8000, 2); put(constants, 6, 0xffff, 2);
        use(constants, [](const odh::View& v) {
            const auto b = odh::read_bcon(v);
            require(b.flags == 128 && b.constants == std::vector<std::uint16_t>{1, 0x8000, 0xffff}
                    && b.remaining_bytes == 0, "BCON raw words and flags");
        });
        use(constants + "tail", [](const odh::View& v) {
            require(odh::read_bcon(v).remaining_bytes == 4, "BCON trailing extent");
        });
        use(std::string(2, '\0'), [](const odh::View& v) {
            require(odh::read_bcon(v).constants.empty(), "empty BCON");
        });
        for (std::size_t n = 0; n < constants.size(); ++n)
            use(constants.substr(0, n), [](const odh::View& v) {
                rejects([&] { odh::read_bcon(v); }, "truncated BCON");
            });
        std::string maximum(512, '\0'); put(maximum, 0, 255, 1);
        use(maximum, [](const odh::View& v) {
            require(odh::read_bcon(v).constants.size() == 255, "BCON byte count bound");
        });
        use("abcdef", [](const odh::View& v) {
            require(v.subview(2, 2).u8(1) == 'd', "nested view offset");
            rejects([&] { v.subview(2, 2).u8(2); }, "crossing nested boundary");
            rejects([&] { v.subview(std::numeric_limits<std::uint64_t>::max(), 2); }, "overflow offset");
            rejects([&] { v.subview(1, std::numeric_limits<std::uint64_t>::max()); }, "overflow size");
        });
        {
            std::istringstream truncated("abc", std::ios::binary);
            const odh::View declared(truncated, 6);
            rejects([&] { declared.u8(5); }, "physical stream shorter than view");
        }
        auto routine = bhav();
        use(routine, [](const odh::View& v) {
            const auto b = odh::read_bhav(v);
            require(b.instructions.size() == 2 && b.instructions[0].opcode == 0x100F, "BHAV endianness");
            require(b.instructions[1].operands[0] == 0xAB, "preserve operands");
        });
        for (const auto version : {0x8000, 0x8001}) {
            auto b = routine; put(b, 0, static_cast<std::uint32_t>(version), 2);
            use(b, [](const odh::View& v) { require(odh::read_bhav(v).instructions.size() == 2, "older BHAV"); });
        }
        auto bad = routine; put(bad, 14, 2, 1);
        use(bad, [](const odh::View& v) { rejects([&] { odh::read_bhav(v); }, "invalid BHAV branch"); });
        bad = routine; put(bad, 0, 0x8003, 2);
        use(bad, [](const odh::View& v) { rejects([&] { odh::read_bhav(v); }, "unsupported BHAV"); });

        auto container = iff(routine,"sample tree");
        use(container, [](const odh::View& v) {
            int visited = 0;
            require(odh::iff_chunks(v, [&](const odh::Chunk& c) {
                ++visited;
                require(c.id == 0x1234 && c.flags == 0xBEEF && c.label=="sample tree", "IFF big endian fields and bounded chunk label");
                require(odh::read_bhav(c.payload).instructions.size() == 2, "nested BHAV");
            }) == "2.5" && visited == 1, "IFF enumeration");
        });
        auto archive = far(container);
        const auto names = far_names({"same", "same"});
        use(names, [](const odh::View& v) {
            odh::Limits limits;
            limits.far_name_length = 4; limits.far_name_bytes = 8;
            const auto entries = odh::far1_index(v, limits);
            require(entries.size() == 2 && entries[0].name == "same" && entries[1].name == "same",
                    "FAR exact name budgets and duplicate preservation");
            limits.far_name_bytes = 7;
            rejects([&] { odh::far1_index(v, limits); }, "FAR duplicate names consume cumulative budget");
            limits.far_name_bytes = 8; limits.far_name_length = 3;
            rejects([&] { odh::far1_index(v, limits); }, "FAR configured individual name budget");
            limits.far_name_length = 4; limits.far_name_bytes = 0;
            rejects([&] { odh::far1_index(v, limits); }, "FAR zero cumulative name budget");
            limits.far_name_bytes = 8; limits.far_name_length = 0;
            rejects([&] { odh::far1_index(v, limits); }, "FAR zero individual name budget");
        });
        use(far_names({std::string(4097, 'x')}), [](const odh::View& v) {
            rejects([&] { odh::far1_index(v); }, "FAR default name length budget");
            odh::Limits limits; limits.far_name_length = 4097; limits.far_name_bytes = 4097;
            require(odh::far1_index(v, limits)[0].name.size() == 4097, "FAR raised explicit name budget");
        });
        use(far_names({std::string(4096, 'x')}), [](const odh::View& v) {
            require(odh::far1_index(v)[0].name.size() == 4096, "FAR exact default individual bound");
        });
        std::vector<std::string> maximum_names(256, std::string(4096, 'x'));
        use(far_names(maximum_names), [](const odh::View& v) {
            require(odh::far1_index(v).size() == 256, "FAR exact default cumulative bound");
        });
        maximum_names.push_back("x");
        use(far_names(maximum_names), [](const odh::View& v) {
            rejects([&] { odh::far1_index(v); }, "FAR default cumulative bound plus one");
        });
        use(far_names({}), [](const odh::View& v) {
            odh::Limits limits; limits.far_name_length = 0; limits.far_name_bytes = 0;
            require(odh::far1_index(v, limits).empty(), "FAR empty index permits zero name budget");
        });
        for (std::size_t size = 20; size < names.size(); ++size)
            use(names.substr(0, size), [](const odh::View& v) {
                rejects([&] { odh::far1_index(v); }, "FAR truncated multiple name records");
            });
        use(archive, [](const odh::View& v) {
            const auto entries = odh::far1_index(v);
            require(entries.size() == 1 && entries[0].name == "test.iff", "FAR index");
            require(v.subview(entries[0].offset, entries[0].length).u8(0) == 'I', "FAR entry view");
        });
        use(archive, [](const odh::View& v) {
            odh::Limits limits;
            limits.far_entries = 0;
            rejects([&] { odh::far1_index(v, limits); }, "FAR configured entry budget");
        });
        use(container, [](const odh::View& v) {
            odh::Limits limits;
            limits.iff_chunks = 0;
            rejects([&] { odh::iff_chunks(v, [](const auto&) {}, limits); }, "IFF configured chunk budget");
        });
        bad = archive; put(bad, 12, 0xFFFFFFFF, 4);
        use(bad, [](const odh::View& v) { rejects([&] { odh::far1_index(v); }, "FAR manifest outside file"); });
        bad = archive; put(bad, 16 + container.size(), 0xFFFFFFFF, 4);
        use(bad, [](const odh::View& v) { rejects([&] { odh::far1_index(v); }, "FAR enormous count"); });
        bad = archive; put(bad, 16 + container.size() + 12, 0xFFFFFFFF, 4);
        use(bad, [](const odh::View& v) { rejects([&] { odh::far1_index(v); }, "FAR data outside archive"); });
        bad = archive; put(bad, 16 + container.size() + 16, 0xFFFFFFFF, 4);
        use(bad, [](const odh::View& v) { rejects([&] { odh::far1_index(v); }, "FAR name allocation bound"); });
        bad = container; put(bad, 68, 75, 4, true);
        use(bad, [](const odh::View& v) { rejects([&] { odh::iff_chunks(v, [](const auto&) {}); }, "IFF undersized chunk"); });
        bad = container; put(bad, 68, 0xFFFFFFFF, 4, true);
        use(bad, [](const odh::View& v) { rejects([&] { odh::iff_chunks(v, [](const auto&) {}); }, "IFF chunk crosses resource"); });

        std::string sprite(32, '\0');
        put(sprite, 0, 1000, 4); put(sprite, 4, 1, 4); put(sprite, 12, 16, 4);
        put(sprite, 16, 136, 2); put(sprite, 18, 368, 2); put(sprite, 20, 7, 4);
        use(sprite, [](const odh::View& v) {
            int visited = 0;
            odh::spr2_frames(v, [&](const auto& f) {
                ++visited; require(f.width == 136 && f.height == 368 && f.flags == 7, "SPR2 header");
            });
            require(visited == 1, "SPR2 callback count");
        });
        bad = sprite; put(bad, 12, 12, 4);
        use(bad, [](const odh::View& v) { rejects([&] { odh::spr2_frames(v, [](const auto&) {}); }, "SPR2 offset overlaps table"); });
        bad = sprite; put(bad, 12, 17, 4);
        use(bad, [](const odh::View& v) { rejects([&] { odh::spr2_frames(v, [](const auto&) {}); }, "SPR2 header crosses payload"); });
        bad = sprite; put(bad, 4, 0xFFFFFFFF, 4);
        use(bad, [](const odh::View& v) { rejects([&] { odh::spr2_frames(v, [](const auto&) {}); }, "SPR2 enormous frame count"); });
        for (const auto version : {136U, 138U, 139U, 140U, 141U, 142U}) {
            std::string graphics(10, '\0');
            put(graphics, 0, version, 4); put(graphics, 4, 0xbeef, 2);
            put(graphics, 6, 1128, 2); put(graphics, 8, 7, 2);
            use(graphics, [&](const odh::View& v) {
                const auto g = odh::read_objd_graphics(v);
                require(g.version == version && g.base_graphic == 1128 && g.num_graphics == 7,
                        "OBJD graphics prefix offsets and supported versions");
            });
            for (std::size_t n = 0; n < graphics.size(); ++n)
                use(graphics.substr(0, n), [](const odh::View& v) {
                    rejects([&] { odh::read_objd_graphics(v); }, "truncated OBJD graphics prefix");
                });
            put(graphics, 6, 0xffff, 2); put(graphics, 8, 0, 2);
            use(graphics + "tail", [](const odh::View& v) {
                const auto g = odh::read_objd_graphics(v);
                require(g.base_graphic == 65535 && g.num_graphics == 0, "raw graphics IDs and prefix-only read");
            });
        }
        for (const auto version : {0U, 135U, 137U, 143U, 0xffffffffU}) {
            std::string graphics(10, '\0'); put(graphics, 0, version, 4);
            use(graphics, [](const odh::View& v) {
                rejects([&] { odh::read_objd_graphics(v); }, "unsupported OBJD graphics version");
            });
        }
        std::string functions(68,'\0');put(functions,0,138,4);
        put(functions,10,4096,2);put(functions,64,4100,2);put(functions,66,4102,2);put(functions,44,0,2);
        use(functions,[](const odh::View& v){const auto f=odh::read_objd_functions(v);
            require(f.main==4096&&f.sit==4100&&f.stand==4102&&f.uses_function_table==0,"legacy OBJD functional bindings");});
        bad=functions;put(bad,44,1,2);
        use(bad,[](const odh::View& v){require(odh::read_objd_functions(v).uses_function_table==1,"OBJf selection preserved explicitly");});
        for(std::size_t n=0;n<functions.size();++n)
            use(functions.substr(0,n),[](const odh::View& v){rejects([&]{odh::read_objd_functions(v);},"truncated functional OBJD prefix");});
        // Every truncation point of independent valid fixtures must be rejected.
        for (std::size_t n = 0; n < routine.size(); ++n)
            use(routine.substr(0, n), [](const odh::View& v) { rejects([&] { odh::read_bhav(v); }, "truncated BHAV"); });
        for (std::size_t n = 0; n < archive.size(); ++n)
            use(archive.substr(0, n), [](const odh::View& v) { rejects([&] { odh::far1_index(v); }, "truncated FAR"); });
        for (std::size_t n = 0; n < sprite.size(); ++n)
            use(sprite.substr(0, n), [](const odh::View& v) { rejects([&] { odh::spr2_frames(v, [](const auto&) {}); }, "truncated SPR2"); });
        std::cout << checks << " checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failed: " << error.what() << '\n';
        return 1;
    }
}
