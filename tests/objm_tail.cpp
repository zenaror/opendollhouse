#include "odh/objm_tail.hpp"
#include "odh/definition_index.hpp"
#include "odh/saved_objects.hpp"

#include <fstream>
#include <iostream>
#include <limits>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
struct Bits {
    std::vector<std::uint8_t> bytes;
    std::uint64_t position = 0;
    std::uint32_t offset = 0;
    void bit(unsigned value) {
        if (position % 8 == 0) bytes.push_back(0);
        if (value & 1U) bytes.back() |= static_cast<std::uint8_t>(1U << (7 - position % 8));
        ++position;
    }
    void field(std::int64_t value, const std::array<unsigned,4>& widths, unsigned chosen = 0) {
        if (value == 0 && chosen == 0) { bit(0); return; }
        unsigned selector = 0;
        while (selector < 4) {
            const auto range = std::int64_t{1} << (widths[selector] - 1);
            if ((chosen ? widths[selector] == chosen : value >= -range && value < range)) break;
            ++selector;
        }
        if (selector == 4) throw std::logic_error("fixture width");
        bit(1); bit(selector >> 1); bit(selector);
        for (unsigned i = widths[selector]; i > 0; --i)
            bit(static_cast<std::uint32_t>(value) >> (i - 1));
    }
    void integer(std::int32_t value, unsigned width = 0) { field(value,{6,11,21,32},width); }
    void word(std::int16_t value) { field(value,{5,8,13,16}); }
    void byte(std::uint8_t value) { field(value > 127 ? int(value)-256 : value,{2,4,6,8}); }
    void floating(std::uint32_t value) {
        integer(value & 0x80000000U ? static_cast<std::int32_t>(std::int64_t{value}-0x100000000LL)
                                  : static_cast<std::int32_t>(value));
    }
    // Independent fixture alignment: strings start at next physical byte and
    // NUL/padding bring their absolute end to even. Pad is deliberately nonzero.
    void string(const std::string& text) {
        while (position % 8) bit(1);
        for (unsigned char value : text) for (unsigned i = 8; i > 0; --i) bit(value >> (i - 1));
        for (unsigned i = 0; i < 8; ++i) bit(0);
        if ((offset + position / 8) & 1U)
            for (unsigned i = 8; i > 0; --i) bit(0xa3U >> (i - 1));
    }
};
odh::ObjectDefinition definition(bool multi = false) {
    return {138,0x12345678U,4,static_cast<std::uint16_t>(multi ? 12 : 0),-1,multi,32,0};
}
odh::ObjtEntry entry(std::uint16_t type = 4) {
    return {0x12345678U,odh::ObjtMetadata{0,0,0,0,1,type},"",{}, {}};
}
odh::ObjmCommonState common(const odh::ObjmBlock& b, std::uint64_t start) {
    odh::ObjmCommonState c{};
    c.block_offset = b.offset; c.odd_offset = bool(b.offset & 1U);
    c.consumed_bits = start; c.remaining_bits = b.bytes.size() * 8 - start;
    return c;
}
odh::ObjmTailState read(const Bits& bits, std::uint64_t start = 0, std::uint16_t type = 4,
                        bool multi = false, std::uint32_t version = 0x45,
                        const odh::ObjmTailLimits& limits = {}) {
    const odh::ObjmBlock b{bits.offset,bits.bytes};
    return odh::read_objm_tail(b,common(b,start),version,definition(multi),entry(type),limits);
}
template<class Action> void fails(Action action, const char* diagnostic) {
    bool rejected = false;
    std::string actual = "accepted";
    try { action(); }
    catch (const odh::FormatError& error) {
        actual = error.what(); rejected = actual.find(diagnostic) != std::string::npos;
    }
    ++checks;
    if (!rejected) throw std::runtime_error(std::string("expected ") + diagnostic + "; got " + actual);
}
constexpr std::array<std::uint32_t,9> patterns{
    0,0x80000000U,0x7f800000U,0xff800000U,0x7fc12345U,0xffa23456U,1,0xffffffffU,0x3f800000U};
void interaction(Bits& b, int seed, int initial = 0) {
    b.integer(initial); b.integer(seed); b.word(-32768); b.word(32767); b.word(-1);
    b.integer(-1);
    for (int i = 0; i < 4; ++i) b.word(static_cast<std::int16_t>(seed+i));
    b.integer(-50); b.word(8192); b.floating(0x7fc12345U); b.integer(0x109);
}
Bits person_fixture(std::uint32_t offset, unsigned start, std::uint32_t version,
                    int bad_section = -1, int bad_count = -1, int initial = 0,
                    unsigned accessory_count = 2) {
    Bits b; b.offset = offset;
    for (unsigned i = 0; i < start; ++i) b.bit(1);
    b.integer(17); b.integer(-9);
    const std::array<std::string,10> strings{
        std::string("\xff\x80",2),"abc","","x","hand","ht","r","rt","head","headtex"};
    for (const auto& text : strings) b.string(text);
    b.integer(bad_section == 0 ? bad_count : static_cast<int>(accessory_count));
    if (bad_section == 0) return b;
    for (unsigned i = 0; i < accessory_count; ++i) { b.string("acc"); b.string(""); }
    b.string("anim;1000"); b.string(""); b.string("base");
    b.integer(99);
    for (auto value : patterns) b.floating(value);
    for (unsigned i = 0; i < 16; ++i) b.floating(patterns[i%patterns.size()]);
    for (unsigned i = 0; i < 16; ++i) b.floating(patterns[(i+2)%patterns.size()]);
    for (unsigned i = 0; i < (version > 0x45 ? 256U : 80U); ++i)
        b.word(static_cast<std::int16_t>(int(i)-128));
    b.integer(-211);
    interaction(b,13,initial); interaction(b,29);
    b.integer(bad_section == 1 ? bad_count : 2); if (bad_section == 1) return b;
    interaction(b,41); interaction(b,57);
    b.integer(bad_section == 2 ? bad_count : 2); if (bad_section == 2) return b;
    for (int i = 0; i < 2; ++i) { b.word(static_cast<std::int16_t>(-5+i)); b.integer(23+i); b.byte(i ? 255 : 1); }
    b.integer(bad_section == 3 ? bad_count : 2); if (bad_section == 3) return b;
    for (int i = 0; i < 2; ++i) { b.integer(-2+i); b.floating(0x80000000U); b.floating(0xffa23456U); }
    b.integer(-2147483647); return b;
}
void synthetic() {
    Bits b; b.integer(0);
    auto t = read(b);
    require(t.consumed_bits == 1 && t.remaining_bits == 7 && t.remaining_data == std::vector<std::uint8_t>{0},"minimal zero field and exact packed suffix");
    require(!t.person && !t.multitile && !t.portal_float_bits && t.decoded_words == 1,"ordinary object has no inferred resource branches");

    auto placeholder_definition = definition();
    placeholder_definition.guid = 0x3C08965FU;
    auto placeholder_entry = entry(4);
    placeholder_entry.guid = 0x3C08965FU;
    placeholder_entry.name = "*Placeholder";
    const odh::ObjmBlock empty_block{0,{}};
    auto empty_common = common(empty_block,0);
    const auto absent_placeholder = odh::read_objm_tail(
        empty_block,empty_common,0x45,placeholder_definition,placeholder_entry);
    require(!absent_placeholder.unknown_int_present && absent_placeholder.unknown_int == 0 &&
            absent_placeholder.placeholder_unknown_int_omission_verified &&
            absent_placeholder.remaining_bits == 0,
            "exact Placeholder may omit final field only at bounded block end");
    Bits placeholder_value; placeholder_value.integer(-73);
    const odh::ObjmBlock placeholder_value_block{0,placeholder_value.bytes};
    const auto present_placeholder = odh::read_objm_tail(
        placeholder_value_block,common(placeholder_value_block,0),0x45,
        placeholder_definition,placeholder_entry);
    require(present_placeholder.unknown_int_present && present_placeholder.unknown_int == -73,
            "exact Placeholder retains final field when serialized");
    for (std::uint64_t remaining = 1; remaining <= 31; ++remaining) {
        const auto byte_count = static_cast<std::size_t>((remaining + 7) / 8);
        std::vector<std::uint8_t> partial_bytes(byte_count,0);
        const auto start_bit = byte_count * 8 - remaining;
        // Select the 32-bit representation so every 1..31-bit suffix is
        // genuinely incomplete, rather than a valid compact zero field.
        for (std::uint64_t i = 0; i < std::min<std::uint64_t>(3,remaining); ++i)
            partial_bytes[static_cast<std::size_t>((start_bit+i)/8)] |=
                static_cast<std::uint8_t>(1U << (7-(start_bit+i)%8));
        const odh::ObjmBlock partial_block{0,std::move(partial_bytes)};
        auto partial_common = common(partial_block,start_bit);
        fails([&]{ (void)odh::read_objm_tail(partial_block,partial_common,0x45,
            placeholder_definition,placeholder_entry); },"truncated");
    }
    auto other_entry = placeholder_entry;
    other_entry.name = "Other";
    fails([&]{ (void)odh::read_objm_tail(empty_block,empty_common,0x45,
        placeholder_definition,other_entry); },"truncated");
    other_entry = placeholder_entry;
    other_entry.guid = 0x12345678U;
    placeholder_definition.guid = 0x12345678U;
    fails([&]{ (void)odh::read_objm_tail(empty_block,empty_common,0x45,
        placeholder_definition,other_entry); },"truncated");
    placeholder_definition.guid = 0x3C08965FU;
    Bits last_zero;
    for (unsigned i = 0; i < 8; ++i) last_zero.bit(0);
    require(read(last_zero,7).consumed_bits == 8 && read(last_zero,7).remaining_data.empty(),
            "compact zero at last bit exists without requiring reference prefetch");
    auto last_nonzero = last_zero; last_nonzero.bytes[0] = 1;
    fails([&]{ (void)read(last_nonzero,7); },"truncated");
    fails([&]{ (void)read(last_zero,8); },"truncated");
    fails([&]{ (void)read(last_zero,6,2); },"truncated");
    Bits missing_pad; missing_pad.offset = 0;
    missing_pad.integer(0); missing_pad.integer(0);
    while (missing_pad.position % 8) missing_pad.bit(0);
    // First string starts at odd byte1; one content byte plus NUL needs pad.
    for (unsigned i = 8; i > 0; --i) missing_pad.bit(0x61U >> (i-1));
    for (unsigned i = 0; i < 8; ++i) missing_pad.bit(0);
    fails([&]{ (void)read(missing_pad,0,2); },"string padding");
    b.bit(1); b.bit(0); b.bit(1);
    require(read(b).remaining_data == std::vector<std::uint8_t>{0xa0},"opaque suffix preserves nonzero bits without rounding");
    for (unsigned start = 0; start < 8; ++start) {
        for (unsigned offset = 0; offset < 2; ++offset) {
            Bits x; x.offset = offset;
            for (unsigned i = 0; i < start; ++i) x.bit(1);
            for (auto value : {-32,1023,-1048576,2147483647,-2147483647,0}) x.integer(value);
            x.word(-32768); x.integer(-73);
            auto r = read(x,start,4,true);
            require(r.multitile && r.multitile->offset_x == -32 && r.multitile->offset_y == 1023 &&
                    r.multitile->offset_level == -1048576 && r.multitile->group_x == 2147483647 &&
                    r.multitile->group_y == -2147483647 && r.multitile->group_level == 0 &&
                    r.multitile->parent_id == -32768 && r.unknown_int == -73,"all multitile raw fields and parent, independent of subindex");
            require(r.start_bits == start && r.consumed_bits == x.position && r.odd_offset == bool(offset),"multitile misaligned exact continuation");
            for (auto version : {0x45U,0x46U}) {
                auto p = person_fixture(offset,start,version);
                auto result = read(p,start,2,false,version);
                const auto& person = *result.person;
                const odh::ObjmBlock person_block{p.offset,p.bytes};
                const auto no_definition = odh::read_objm_person_tail_without_definition(
                    person_block,common(person_block,start),version,entry(2),false);
                require(no_definition.person && no_definition.person->interaction_queue.size()==2 &&
                        no_definition.person->interaction_queue[1].uid==57 && !no_definition.multitile,
                        "person tail can be read from OBJT without fabricating an OBJD");
                require(result.consumed_bits == p.position && result.remaining_bits < 8,"complete person exact cursor for parity and version");
                require(person.person_data.size() == (version > 0x45 ? 256U : 80U) &&
                        person.person_data.front() == -128 && person.person_data.back() == (version > 0x45 ? 127 : -49),"version boundary PersonData 80 versus256");
                require(person.body.bytes == std::string("\xff\x80",2) && person.body_texture.bytes == "abc" &&
                        person.unknown1.bytes.empty() && person.base_animation.bytes == "base", "raw high-bit strings, odd/even/empty lengths and reload");
                require(person.first_float_bits == patterns && person.motive_data_old_bits[15] == patterns[6] &&
                        person.motive_data_bits[15] == patterns[8],"binary32 negativezero infinities NaN payloads and subnormals preserved");
                require(person.animation_event_count == 17 && person.engaged == -9 && person.routing_state == 99 &&
                        person.routing_frame_count == -211 && result.unknown_int == -2147483647,"person unknown scalar values retained");
                require(person.accessories.size() == 2 && person.accessories[1].name.bytes == "acc" &&
                        person.accessories[1].binding.bytes.empty(),"accessory strings and count");
                require(person.active_interaction.uid == 13 && person.last_interaction.uid == 29 &&
                        person.interaction_queue[1].uid == 57 && person.interaction_queue[0].args[3] == 44 &&
                        person.active_interaction.attenuation_bits == 0x7fc12345U && person.last_interaction.flags == 0x109,
                        "two interactions, complete queue, raw flags attenuation and argument ordering");
                require(person.object_uses.size() == 2 && person.object_uses[1].unknown2 == 255 &&
                        person.object_uses[0].target_id == -5 && person.object_uses[1].stack_length == 24,
                        "object uses and signed byte cast");
                require(person.motive_deltas[1].motive == -1 && person.motive_deltas[0].tick_delta_bits == 0x80000000U &&
                        person.motive_deltas[1].stop_at_bits == 0xffa23456U,"motive delta raw bits and counts");
                for (const auto* text : {&person.body,&person.body_texture,&person.unknown1,&person.unknown2,
                        &person.left_hand,&person.left_hand_texture,&person.right_hand,&person.right_hand_texture,
                        &person.head,&person.head_texture,&person.animation,&person.carry_animation,&person.base_animation})
                    require(!text->padding || *text->padding == 0xa3,"string nonzero padding preserved");
            }
        }
    }
    for (auto width : {6U,11U,21U,32U}) {
        const auto magnitude = std::int64_t{1} << (width-1);
        for (auto value : {-magnitude,magnitude-1,std::int64_t{-1},std::int64_t{0},std::int64_t{1}}) {
            Bits x; x.integer(static_cast<std::int32_t>(value),width);
            require(read(x).unknown_int == value,"all integer selectors and signed bounds");
        }
    }
    Bits portal; portal.integer(static_cast<int>(patterns.size()));
    for (auto value : patterns) portal.floating(value);
    portal.integer(-16);
    const auto pt = read(portal,0,8);
    require(pt.portal_count == 9 && *pt.portal_float_bits == std::vector<std::uint32_t>(patterns.begin(),patterns.end()) &&
            pt.unknown_int == -16 && !pt.person,"portal selected by OBJT even when OBJD type differs");
    const odh::ObjmBlock portal_block{portal.offset,portal.bytes};
    fails([&]{ (void)odh::read_objm_person_tail_without_definition(
        portal_block,common(portal_block,0),0x45,entry(8),false); },"requires Person OBJT");
    Bits portalzero; portalzero.integer(0); portalzero.integer(0);
    require(read(portalzero,0,8).portal_float_bits->empty(),"present portal branch with zero count");
    for (int value : {-1,2147483647}) {
        Bits x; x.integer(value);
        fails([&]{ (void)read(x,0,8); },value < 0 ? "negative" : "count exceeds limit");
    }
    auto rich = person_fixture(1,3,0x46);
    const auto owned = read(rich,3,2,false,0x46);
    odh::ObjmTailLimits limits;
    limits.decoded_words = owned.decoded_words;
    limits.string_bytes = owned.string_bytes;
    require(read(rich,3,2,false,0x46,limits).decoded_words == owned.decoded_words,"exact scalar and cumulative string budgets accepted");
    --limits.decoded_words;
    fails([&]{ (void)read(rich,3,2,false,0x46,limits); },"word budget");
    limits = {}; limits.string_bytes = owned.string_bytes - 1;
    fails([&]{ (void)read(rich,3,2,false,0x46,limits); },"string byte budget");
    limits = {}; limits.string_length = 3;
    fails([&]{ (void)read(rich,3,2,false,0x46,limits); },"string length");
    limits = {}; limits.block_bytes = rich.bytes.size()-1;
    fails([&]{ (void)read(rich,3,2,false,0x46,limits); },"byte limit");
    limits = {}; limits.portal_values = 8;
    fails([&]{ (void)read(portal,0,8,false,0x45,limits); },"portal value count");
    for (int section = 0; section < 4; ++section) {
        for (int bad : {-1,2147483647}) {
            auto x = person_fixture(0,0,0x45,section,bad);
            // Supply spare bytes so fixed preflight reaches the intended count.
            x.bytes.resize(x.bytes.size()+1000,0);
            fails([&]{ (void)read(x,0,2); },bad < 0 ? "negative" : "count exceeds limit");
        }
        limits = {};
        if (section == 0) limits.accessories = 1;
        else if (section == 1) limits.interactions = 1;
        else if (section == 2) limits.object_uses = 1;
        else limits.motive_deltas = 1;
        fails([&]{ (void)read(rich,3,2,false,0x46,limits); },"count exceeds limit");
    }
    auto initial = person_fixture(0,0,0x45,-1,-1,1);
    fails([&]{ (void)read(initial,0,2); },"nonzero interaction");
    auto no_accessories = person_fixture(0,0,0x45,-1,-1,0,0);
    require(read(no_accessories,0,2).person->accessories.empty(),"zero accessories preserves animation stream interruption");
    for (std::size_t length = 0; length < rich.bytes.size(); ++length) {
        auto short_copy = rich; short_copy.bytes.resize(length);
        if (length == 0) continue; // Start cursor3 would be outside the empty block.
        fails([&]{ (void)read(short_copy,3,2,false,0x46); },"truncated");
    }
    odh::ObjmBlock source{rich.offset,rich.bytes}; auto c = common(source,3);
    auto d = definition(); auto e = entry(2);
    auto mismatched = c; ++mismatched.block_offset;
    fails([&]{ (void)odh::read_objm_tail(source,mismatched,70,d,e); },"cursor");
    mismatched = c; mismatched.odd_offset = !mismatched.odd_offset;
    fails([&]{ (void)odh::read_objm_tail(source,mismatched,70,d,e); },"cursor");
    mismatched = c; ++mismatched.remaining_bits;
    fails([&]{ (void)odh::read_objm_tail(source,mismatched,70,d,e); },"cursor");
    mismatched = c; mismatched.consumed_bits = source.bytes.size()*8+1;
    fails([&]{ (void)odh::read_objm_tail(source,mismatched,70,d,e); },"cursor");
    mismatched = c; mismatched.start_bits = c.consumed_bits+1;
    fails([&]{ (void)odh::read_objm_tail(source,mismatched,70,d,e); },"cursor");
    e.guid = 0;
    fails([&]{ (void)odh::read_objm_tail(source,c,70,d,e); },"resource metadata");
    e = entry(2); ++e.guid;
    fails([&]{ (void)odh::read_objm_tail(source,c,70,d,e); },"resource metadata");
    e = entry(2); e.metadata.reset();
    fails([&]{ (void)odh::read_objm_tail(source,c,70,d,e); },"resource metadata");
    e = entry(2); d.is_multitile = true;
    fails([&]{ (void)odh::read_objm_tail(source,c,70,d,e); },"OBJD metadata");
    d = definition(); d.version = 137;
    fails([&]{ (void)odh::read_objm_tail(source,c,70,d,e); },"OBJD metadata");
    d.version = 136;
    require(odh::read_objm_tail(source,c,70,d,e).consumed_bits == owned.consumed_bits,
            "OBJD136 known metadata profile also accepted");
    source.bytes.assign(1,255);
    require(owned.person->body.bytes == std::string("\xff\x80",2) &&
            owned.person->person_data.back() == 127 && owned.person->motive_data_bits[0] == 0x7f800000U,
            "all saved arrays and strings own data after source destruction");
}

std::string hex(const std::string& input) {
    std::string out;
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char byte : input) { out += digits[byte >> 4]; out += digits[byte & 15]; }
    return out.empty() ? "-" : out;
}
void print_interaction(const odh::ObjmSavedInteraction& i) {
    std::cout << ' ' << i.unknown_zero << ' ' << i.uid << ' ' << i.caller_id << ' ' << i.target_id
              << ' ' << i.icon << ' ' << i.tta_index;
    for (auto value : i.args) std::cout << ' ' << value;
    std::cout << ' ' << i.priority << ' ' << i.action_tree_id << ' ' << i.attenuation_bits << ' ' << i.flags;
}
void print_native(std::uint16_t id, const odh::ObjmTailState& t) {
    std::vector<const odh::ObjmSavedString*> strings;
    std::cout << "native_tail " << id << ' ' << t.block_offset << ' ' << t.start_bits << ' '
              << t.consumed_bits << ' ' << t.remaining_bits << ' ' << t.decoded_words << ' ' << t.string_bytes;
    std::cout << ' ' << (t.unknown_int_present ? 1 : 0);
    std::cout << ' ' << (t.placeholder_unknown_int_omission_verified ? 1 : 0);
    if (t.multitile) {
        const auto& m = *t.multitile;
        std::cout << ' ' << m.offset_x << ' ' << m.offset_y << ' ' << m.offset_level << ' '
                  << m.group_x << ' ' << m.group_y << ' ' << m.group_level << ' ' << m.parent_id;
    }
    if (t.portal_float_bits) {
        std::cout << ' ' << *t.portal_count;
        for (auto value : *t.portal_float_bits) std::cout << ' ' << value;
    }
    if (t.person) {
        const auto& p = *t.person;
        std::cout << ' ' << p.animation_event_count << ' ' << p.engaged;
        strings = {&p.body,&p.body_texture,&p.unknown1,&p.unknown2,&p.left_hand,&p.left_hand_texture,
                   &p.right_hand,&p.right_hand_texture,&p.head,&p.head_texture};
        std::cout << ' ' << p.accessory_count;
        for (const auto& a : p.accessories) { strings.push_back(&a.name); strings.push_back(&a.binding); }
        strings.push_back(&p.animation); strings.push_back(&p.carry_animation); strings.push_back(&p.base_animation);
        std::cout << ' ' << p.routing_state;
        for (auto value : p.first_float_bits) std::cout << ' ' << value;
        for (auto value : p.motive_data_old_bits) std::cout << ' ' << value;
        for (auto value : p.motive_data_bits) std::cout << ' ' << value;
        for (auto value : p.person_data) std::cout << ' ' << value;
        std::cout << ' ' << p.routing_frame_count;
        print_interaction(p.active_interaction); print_interaction(p.last_interaction);
        std::cout << ' ' << p.interaction_queue_count;
        for (const auto& i : p.interaction_queue) print_interaction(i);
        std::cout << ' ' << p.object_use_count;
        for (const auto& u : p.object_uses) std::cout << ' ' << u.target_id << ' ' << u.stack_length << ' ' << unsigned(u.unknown2);
        std::cout << ' ' << p.motive_delta_count;
        for (const auto& m : p.motive_deltas) std::cout << ' ' << m.motive << ' ' << m.tick_delta_bits << ' ' << m.stop_at_bits;
    }
    std::cout << ' ' << t.unknown_int << '\n';
    for (std::size_t i = 0; i < strings.size(); ++i)
        std::cout << "native_tail_string " << id << ' ' << i << ' ' << strings[i]->start_byte << ' '
                  << (strings[i]->padding ? int(*strings[i]->padding) : -1) << ' ' << hex(strings[i]->bytes) << '\n';
    std::cout << "native_tail_suffix " << id << ' ' << t.remaining_bits << ' ';
    if (t.remaining_data.empty()) std::cout << '-';
    else for (auto v : t.remaining_data) std::cout << "0123456789abcdef"[v>>4] << "0123456789abcdef"[v&15];
    std::cout << '\n';
}
template<class Action> void file(const char* path, Action action) {
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw std::runtime_error("native fixture cannot be opened");
    stream.seekg(0,std::ios::end); const auto length = stream.tellg(); stream.seekg(0);
    require(length >= 0,"native file extent");
    action(odh::View(stream,static_cast<std::uint64_t>(length)));
}
void native(int argc, char** argv) {
    std::vector<odh::DefinitionCandidate> candidates;
    auto gather = [&](const odh::View& v, const std::string& source, const std::string& asset) {
        odh::iff_chunks(v,[&](const odh::Chunk& chunk) {
            if (chunk.type == "OBJD") candidates.push_back({source,asset,chunk.id,odh::read_object_definition(chunk.payload)});
        });
    };
    file(argv[2],[&](const odh::View& far) {
        for (const auto& asset : odh::far1_index(far)) gather(far.subview(asset.offset,asset.length),argv[2],asset.name);
    });
    for (int i = 3; i < argc; ++i) file(argv[i],[&](const odh::View& v){gather(v,argv[i],"");});
    const odh::DefinitionIndex index(candidates);
    file(argv[1],[&](const odh::View& house) {
        std::optional<odh::Objm> objm; std::optional<odh::Objt> objt;
        odh::iff_chunks(house,[&](const odh::Chunk& chunk) {
            if (chunk.type == "ObjM" && chunk.id == 1) { require(!objm,"unique native ObjM"); objm = odh::read_objm(chunk.payload); }
            if (chunk.type == "objt" && chunk.id == 0) { require(!objt,"unique native objt"); objt = odh::read_objt(chunk.payload); }
        });
        require(objm && objt,"native ObjM and objt available");
        const auto saved = odh::read_saved_object_prefixes(*objm,*objt);
        for (std::size_t i = 0; i < saved.size(); ++i) {
            const auto& p = saved[i]; const auto& b = objm->blocks[i];
            const auto* resource = index.unique(p.guid);
            require(resource != nullptr,"native unique explicit definition required");
            const auto c = odh::read_objm_common(b,p.prefix);
            const auto t = odh::read_objm_tail(b,c,objm->version,resource->definition,*objt->entry_for_type(p.type_id));
            require(t.start_bits == c.consumed_bits && t.consumed_bits+t.remaining_bits == b.bytes.size()*8,"native tail exact common resume and extent");
            require(bool(t.multitile) == (resource->definition.master_id != 0),"native multitile branch uses MasterID");
            require(t.remaining_bits <= 7,"native only sub-byte suffix remains");
            print_native(p.object_id,t);
        }
    });
}
} // namespace

int main(int argc, char** argv) {
    try {
        synthetic();
        if (argc >= 3) native(argc,argv);
        else if (argc != 1) throw std::runtime_error("usage: objm-tail [HOUSE FAR [CHARACTER_IFF...]]");
        std::cout << "objm tail checks: " << checks << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
