#include "odh/object_definition.hpp"

#include <iostream>
#include <sstream>

namespace {
void put16(std::string& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<char>(value & 255);
    bytes[offset + 1] = static_cast<char>(value >> 8);
}
void put32(std::string& bytes, std::size_t offset, std::uint32_t value) {
    put16(bytes, offset, static_cast<std::uint16_t>(value));
    put16(bytes, offset + 2, static_cast<std::uint16_t>(value >> 16));
}
odh::ObjectDefinition read(const std::string& bytes) {
    std::istringstream stream(bytes);
    return odh::read_object_definition(odh::View(stream, bytes.size()));
}
template<class F> bool rejects(F fn) {
    try { fn(); } catch (const odh::FormatError&) { return true; }
    return false;
}
}

int main() {
    try {
        unsigned checks = 0;
        const auto check = [&](bool value) {
            ++checks;
            if (!value) throw std::runtime_error("OBJD metadata assertion " + std::to_string(checks));
        };
        // Unread fields intentionally contain sentinels, avoiding zero-offset
        // fixtures that could accidentally validate a misaligned reader.
        std::string bytes(32, '\x5a');
        put32(bytes, 0, 138);
        put16(bytes, 14, 128);
        put16(bytes, 18, 0xfedc);
        put16(bytes, 20, 0x9876);
        put16(bytes, 22, 0xffff);
        put32(bytes, 28, 0xfedcba98);
        auto metadata = read(bytes);
        check(metadata.version == 138 && metadata.object_type == 0xfedc);
        check(metadata.guid == 0xfedcba98 && metadata.master_id == 0x9876);
        check(metadata.tree_table_id == 128);
        check(metadata.sub_index == -1 && metadata.is_multitile);
        check(metadata.header_extent == 32 && metadata.remaining_bytes == 0);
        check(!metadata.level_offset&&!metadata.tile_width&&!metadata.footprint_mask&&
              !metadata.slot_table_id&&!metadata.num_attributes);
        // read() destroys its stream: metadata remains independently owned.
        check(metadata.guid == 0xfedcba98);

        for (std::uint32_t version : {136u, 138u}) {
            auto profile = bytes;
            put32(profile, 0, version);
            const auto parsed = read(profile);
            check(parsed.version == version && parsed.guid == 0xfedcba98 &&
                  parsed.object_type == 0xfedc && parsed.master_id == 0x9876 &&
                  parsed.sub_index == -1 && parsed.is_multitile && parsed.header_extent == 32 &&
                  parsed.tree_table_id == 128);
            for (std::size_t size = 0; size < profile.size(); ++size)
                check(rejects([&] { read(profile.substr(0, size)); }));
        }
        {
            auto runtime = std::string(120, '\0'); put32(runtime,0,138);
            put16(runtime,40,0x3456); put16(runtime,116,17);
            auto parsed = read(runtime);
            check(parsed.slot_table_id==0x3456&&parsed.num_attributes==17&&
                  parsed.header_extent==118&&parsed.remaining_bytes==2);
            runtime.resize(117);
            parsed = read(runtime);
            check(parsed.slot_table_id==0x3456&&!parsed.num_attributes&&
                  parsed.header_extent==114&&parsed.remaining_bytes==3);
            runtime.resize(41);
            parsed = read(runtime);
            check(!parsed.slot_table_id&&!parsed.num_attributes&&parsed.header_extent==32);
        }
        {
            auto placement = std::string(116, '\0');put32(placement,0,138);
            put16(placement,112,0x8001);
            const auto parsed=read(placement);
            check(parsed.level_offset==0x8001&&parsed.header_extent==114&&parsed.remaining_bytes==2);
            placement.resize(113);
            const auto partial=read(placement);
            check(!partial.level_offset&&partial.header_extent==42);
        }
        {
            auto collision=std::string(160,'\0');put32(collision,0,138);
            put16(collision,14,128);put16(collision,112,0x1234);put16(collision,134,0x2345);put16(collision,154,0xA19C);
            const auto parsed=read(collision);
            check(parsed.tile_width==0x2345&&parsed.footprint_mask==0xA19C&&
                  parsed.level_offset==0x1234&&parsed.slot_table_id==0&&
                  parsed.num_attributes==0&&parsed.header_extent==156&&parsed.remaining_bytes==4);
            collision.resize(155);
            const auto partial=read(collision);
            check(partial.level_offset==0x1234&&!partial.tile_width&&!partial.footprint_mask&&partial.header_extent==118);
        }
        for (std::uint32_t version : {0u, 137u, 139u, 140u, 141u, 142u, 0xffffffffu}) {
            auto unsupported = bytes;
            put32(unsupported, 0, version);
            check(rejects([&] { read(unsupported); }));
        }
        for (std::uint16_t sub : {0u, 1u, 32767u, 32768u, 65535u}) {
            put16(bytes, 22, sub);
            const auto expected = sub < 32768 ? static_cast<std::int32_t>(sub)
                                             : static_cast<std::int32_t>(sub) - 65536;
            check(read(bytes).sub_index == expected);
        }
        put16(bytes, 20, 0);
        put16(bytes, 22, 0xffff);
        put32(bytes, 28, 0);
        const std::string tail = "tail";
        metadata = read(bytes + tail);
        check(!metadata.is_multitile && metadata.sub_index == -1 && metadata.guid == 0);
        check(metadata.header_extent == 32 && metadata.remaining_bytes == tail.size());

        std::istringstream parent("prefix" + bytes + "suffix");
        metadata = odh::read_object_definition(odh::View(parent, 44).subview(6, 32));
        check(metadata.object_type == 0xfedc && metadata.remaining_bytes == 0);
        // A claimed extent alone cannot hide a truncated backing stream.
        std::istringstream short_stream(bytes.substr(0, 30));
        check(rejects([&] { odh::read_object_definition(odh::View(short_stream, 32)); }));
        std::cout << checks << " OBJD metadata checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
