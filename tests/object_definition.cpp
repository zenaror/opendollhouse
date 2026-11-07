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
std::vector<std::int16_t> words(const std::string& bytes) {
    std::istringstream stream(bytes);
    return odh::read_object_definition_words(odh::View(stream,bytes.size()));
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
        {
            auto raw=bytes;
            raw.resize(224,'\x5a');
            for(std::size_t i=2;i<112;++i)put16(raw,i*2,static_cast<std::uint16_t>(0x8000+i));
            for(auto version:{136U,138U}) {
                put32(raw,0,version);
                const auto bank=words(raw);
                check(bank.size()==(version==136 ? 80U : 95U));
                check(bank[0]==static_cast<std::int16_t>(version)&&bank[1]==0);
                for(std::size_t i=2;i<bank.size();++i)
                    check(bank[i]==static_cast<std::int16_t>(-32768+static_cast<int>(i)));
                // Price18 is the raw word at36, not sale price34 or GUID14.
                check(bank[18]==-32750&&bank[34]==-32734&&bank[14]==-32754);
            }
            put32(raw,0,138);put16(raw,36,321);put16(raw,38,999);
            check(words(raw)[18]==321);
            raw.resize(37);check(words(raw).size()==18); // incomplete Price
            raw.resize(38);put16(raw,36,0xffff);
            check(words(raw).size()==19&&words(raw)[18]==-1);
            check(words(bytes).size()==16); // no fabricated optional words
            put32(raw,0,142);check(rejects([&]{words(raw);}));
        }
        check(metadata.version == 138 && metadata.object_type == 0xfedc);
        check(metadata.guid == 0xfedcba98 && metadata.master_id == 0x9876);
        check(metadata.tree_table_id == 128);
        check(metadata.sub_index == -1 && metadata.is_multitile);
        check(metadata.header_extent == 32 && metadata.remaining_bytes == 0);
        check(!metadata.level_offset&&!metadata.tile_width&&!metadata.footprint_mask&&
              !metadata.slot_table_id&&!metadata.num_attributes&&!metadata.build_mode_type);
        check(!metadata.global_flag);
        check(!metadata.type_attribute_guid);
        {
            auto type=bytes;type.resize(188,0);put32(type,0,138);
            put32(type,184,0x81234567);
            check(read(type).type_attribute_guid==0x81234567&&read(type).header_extent==188);
            put32(type,184,0);check(read(type).type_attribute_guid==0xfedcba98);
            type.resize(187);check(!read(type).type_attribute_guid);
            // The pinned v136 reader never reaches its v138 fallback branch.
            put32(type,0,136);type.resize(160);check(read(type).type_attribute_guid==0);
            type.resize(159);check(!read(type).type_attribute_guid);
        }
        for(const auto version:{136u,138u}){
            auto controller=bytes;controller.resize(85,0);put32(controller,0,version);
            check(!read(controller).global_flag); // Half a raw Global word is unknown.
            controller.resize(86,0);put16(controller,84,1);
            check(read(controller).global_flag==1&&read(controller).header_extent==86);
            put16(controller,84,0);check(read(controller).global_flag==0);
            put16(controller,84,0xffff);check(read(controller).global_flag==65535);
            check(words(controller)[42]==-1); // Preserve raw bits; do not coerce a boolean.
        }
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
            check(!partial.level_offset&&partial.global_flag==0&&partial.header_extent==86);
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
            check(partial.level_offset==0x1234&&!partial.tile_width&&!partial.footprint_mask&&
                  partial.build_mode_type==0&&partial.header_extent==140);
        }
        for (auto version : {136u,138u}) {
            std::string definition(142,'\x5a');put32(definition,0,version);
            put16(definition,138,0x8123);
            const auto complete=read(definition);
            check(complete.build_mode_type==0x8123&&complete.header_extent==140&&complete.remaining_bytes==2);
            definition.resize(139);
            check(!read(definition).build_mode_type);
            definition.resize(140);put16(definition,138,0);
            check(read(definition).build_mode_type==0);
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
