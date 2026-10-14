#include "odh/objm_common.hpp"

#include <fstream>
#include <iostream>
#include <limits>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
// Independent fixture writer: each selector width is supplied explicitly.
struct Bits {
    std::vector<std::uint8_t> bytes;
    std::uint64_t position = 0;
    void bit(unsigned value) {
        if (position % 8 == 0) bytes.push_back(0);
        if (value & 1U) bytes.back() |= static_cast<std::uint8_t>(1U << (7-position%8));
        ++position;
    }
    void field(std::int32_t value, unsigned width, const std::array<unsigned,4>& widths) {
        if (width == 0) {
            if (value != 0) throw std::logic_error("fixture compact zero");
            bit(0); return;
        }
        unsigned selector = 0;
        while (selector < 4 && widths[selector] != width) ++selector;
        if (selector == 4) throw std::logic_error("fixture width");
        bit(1); bit(selector >> 1); bit(selector);
        for (unsigned i = width; i > 0; --i) bit(static_cast<std::uint32_t>(value) >> (i-1));
    }
    void integer(std::int32_t value, unsigned width = 32) { field(value,width,{6,11,21,32}); }
    void word(std::int16_t value, unsigned width = 16) { field(value,width,{5,8,13,16}); }
    void byte(std::int32_t value, unsigned width = 8) { field(value,width,{2,4,6,8}); }
};
Bits prefix() {
    Bits bits;
    // Seven int32, two int16 headers, 81 bank entries, all compact zero.
    for (unsigned i = 0; i < 90; ++i) bits.bit(0);
    return bits;
}
void empty_tail(Bits& bits) {
    bits.integer(-1,6); bits.integer(0,0); bits.word(0,0); bits.word(0,0);
}
Bits empty(std::int32_t stack = 0, std::int32_t flags = 0, unsigned flag_width = 0) {
    auto bits = prefix();
    bits.integer(stack,stack == 0 ? 0 : 32); bits.integer(flags,flag_width);
    empty_tail(bits); return bits;
}
odh::ObjmBlock block(const Bits& bits, std::uint32_t offset = 19) { return {offset,bits.bytes}; }
odh::ObjmCommonState read(const Bits& bits, const odh::ObjmCommonLimits& limits = {}) {
    const auto input = block(bits);
    return odh::read_objm_common(input,odh::read_objm_prefix(input),limits);
}
void rejects(const Bits& bits, const char* diagnostic, const odh::ObjmCommonLimits& limits = {}) {
    bool failed = false;
    std::string actual = "accepted";
    try { (void)read(bits,limits); }
    catch (const odh::FormatError& error) {
        actual = error.what();
        failed = actual.find(diagnostic) != std::string::npos;
    }
    ++checks;
    if (!failed) throw std::runtime_error(std::string("expected ") + diagnostic + "; got " + actual);
}
Bits rich() {
    auto b = prefix();
    b.integer(1,6); b.integer(512,11);
    b.word(-32768); b.word(8330,16); b.word(9,5);
    b.byte(2,4); b.byte(3,4);
    b.word(-16,5); b.word(127,8); b.word(4095,13);
    b.word(-128,8); b.word(-4096,13);
    b.integer(std::numeric_limits<std::int32_t>::min()); b.word(-1,5);
    b.integer(-11,6); b.integer(2,6);
    b.integer(-8,6); b.integer(-2147483647); b.integer(3,6);
    b.integer(-32,6); b.integer(1023,11); b.integer(1048575,21);
    b.integer(4,6); b.integer(17,6); b.integer(0,0);
    b.word(3,5); b.word(7,5); b.word(-1,5);
    b.word(0,0); b.word(-32768); b.word(-9,5); b.word(32767);
    b.word(3,5); b.word(-16,5); b.word(255,13); b.word(-32768);
    return b;
}
void native(const char* path) {
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw std::runtime_error("native fixture cannot be opened");
    stream.seekg(0,std::ios::end); const auto size = stream.tellg(); stream.seekg(0);
    require(size >= 0,"native fixture size");
    unsigned chunks = 0;
    odh::iff_chunks(odh::View(stream,static_cast<std::uint64_t>(size)),[&](const odh::Chunk& chunk) {
        if (chunk.type != "ObjM" || chunk.id != 1) return;
        ++chunks;
        const auto container = odh::read_objm(chunk.payload);
        for (const auto& input : container.blocks) {
            const auto p = odh::read_objm_prefix(input);
            const auto c = odh::read_objm_common(input,p);
            require(c.start_bits == p.consumed_bits,"native resumes prefix bit cursor");
            require(c.consumed_bits + c.remaining_bits == input.bytes.size()*8,"native exact end extent");
            require(c.remaining_bits > 0,"native resource-dependent tail remains");
            require(c.slot_count == static_cast<std::int16_t>(c.slots.size() + (c.linked_slot ? 1 : 0)),"native linked slot count");
            // Full integer rows permit a separate Python decoder comparison.
            std::cout << "native_common " << c.block_offset << ' ' << c.start_bits << ' '
                      << c.consumed_bits << ' ' << c.remaining_bits << ' ' << c.decoded_words << ' '
                      << c.stack_count << ' ' << c.stack_flags;
            for (const auto& f : c.stack) {
                std::cout << ' ' << f.stack_object_id << ' ' << f.tree_id << ' ' << f.node_id << ' '
                          << unsigned(f.local_count) << ' ' << unsigned(f.parameter_count);
                for (auto v : f.parameters) std::cout << ' ' << v;
                for (auto v : f.locals) std::cout << ' ' << v;
                std::cout << ' ' << f.primitive_state << ' ' << f.code_owner_obj_type;
            }
            std::cout << ' ' << c.relationship_flag << ' ' << c.relationship_count;
            for (const auto& r : c.relationships) {
                std::cout << ' ' << r.is_present << ' ' << r.target_id << ' ' << r.value_count;
                for (auto v : r.values) std::cout << ' ' << v;
            }
            std::cout << ' ' << c.slot_count;
            if (c.linked_slot) std::cout << ' ' << c.linked_slot->unknown << ' ' << c.linked_slot->object_id;
            for (const auto& s : c.slots) std::cout << ' ' << s.unknown << ' ' << s.object_id;
            std::cout << ' ' << c.dynamic_sprite_flag_count;
            for (auto v : c.dynamic_sprite_flags) std::cout << ' ' << v;
            std::cout << '\n';
        }
    });
    require(chunks == 1,"exactly one native ObjM1");
}
}

int main(int argc, char** argv) {
    try {
        const auto minimal = read(empty());
        require(minimal.start_bits == 90 && minimal.consumed_bits == 104 && minimal.remaining_bits == 0,"misaligned prefix resumes exactly through byte boundary");
        require(minimal.decoded_words == 6 && minimal.stack.empty() && minimal.relationships.empty() &&
                !minimal.linked_slot && minimal.slots.empty() && minimal.dynamic_sprite_flags.empty(),"zero counts own no invented linked slot");
        require(minimal.relationship_flag == -1 && minimal.stack_flags == 0 && minimal.odd_offset,"raw flags and parity preserved");
        auto aligned = prefix(); aligned.bytes.clear(); aligned.position = 0;
        for (unsigned i = 0; i < 9; ++i) aligned.bit(0);
        for (unsigned i = 0; i < 4; ++i) aligned.word(1,5);
        aligned.word(1,8);
        for (unsigned i = 5; i < 81; ++i) aligned.word(0,0);
        require(aligned.position == 128,"fixture aligned prefix cursor");
        aligned.integer(0,0); aligned.integer(0,0); empty_tail(aligned);
        require(read(aligned).start_bits == 128 && read(aligned).consumed_bits == 142,"aligned prefix resume without consuming next byte early");
        // Literal 90 zero prefix bits, 0/0/-1/0/0/0 common fields.
        odh::ObjmBlock literal{0,std::vector<std::uint8_t>(13,0)};
        literal.bytes[11] = 0x09; literal.bytes[12] = 0xf8;
        const auto lp = odh::read_objm_prefix(literal);
        const auto lc = odh::read_objm_common(literal,lp);
        require(lc.relationship_flag == -1 && lc.consumed_bits == 104 && !lc.odd_offset,"hand-written common bit layout");
        auto b = rich(); auto input = block(b); const auto p = odh::read_objm_prefix(input);
        const auto c = odh::read_objm_common(input,p);
        require(c.consumed_bits == b.position && c.remaining_bits == input.bytes.size()*8-b.position,"rich cursor stops before tail");
        require(c.stack_count == 1 && c.stack_flags == 512 && c.stack.size() == 1,"stack headers");
        const auto& f = c.stack[0];
        require(f.stack_object_id == -32768 && f.tree_id == 8330 && f.node_id == 9 &&
                f.local_count == 2 && f.parameter_count == 3 &&
                f.parameters == std::vector<std::int16_t>{-16,127,4095} &&
                f.locals == std::vector<std::int16_t>{-128,-4096} &&
                f.primitive_state == std::numeric_limits<std::int32_t>::min() && f.code_owner_obj_type == -1,"frame signed values and parameters before locals");
        require(c.relationship_flag == -11 && c.relationship_count == 2 && c.relationships[0].is_present == -8 &&
                c.relationships[0].target_id == -2147483647 &&
                c.relationships[0].values == std::vector<std::int32_t>{-32,1023,1048575} &&
                c.relationships[1].is_present == 4 && c.relationships[1].values.empty(),"raw presence and signed relationship fields");
        require(c.linked_slot && c.linked_slot->unknown == 7 && c.linked_slot->object_id == -1 &&
                c.slots.size() == 2 && c.slots[0].object_id == -32768 && c.slots[1].unknown == -9 &&
                c.slots[1].object_id == 32767 && c.dynamic_sprite_flags == std::vector<std::int16_t>{-16,255,-32768},"linked first slot and raw sprite words");
        require(c.decoded_words == 36,"decoded budget counts all scalar and array fields");
        input.bytes.assign(1,255);
        require(c.stack[0].parameters[2] == 4095 && c.relationships[0].values[2] == 1048575 && c.slots[1].object_id == 32767,"results survive source mutation");
        // Unknown resource-dependent bits must remain unread.
        b = rich(); b.bit(1); b.bit(0); const auto tail = read(b);
        require(tail.consumed_bits == c.consumed_bits && tail.remaining_bits == b.bytes.size()*8-c.consumed_bits,"tail never prefetched or aligned");
        for (auto width : {6U,11U,21U,32U}) {
            const auto sign = std::int64_t{1} << (width-1);
            for (auto v : {-sign,sign-1,std::int64_t{-1},std::int64_t{0},std::int64_t{1}}) {
                b = empty(0,static_cast<std::int32_t>(v),width);
                require(read(b).stack_flags == v,"all int32 selectors and signed boundaries");
            }
        }
        for (auto width : {2U,4U,6U,8U}) {
            const auto sign = 1 << (width-1);
            for (auto v : {-sign,sign-1,-1,0,1}) {
                b = prefix(); b.integer(1,6); b.integer(0,0);
                b.word(0,0); b.word(0,0); b.word(0,0); b.byte(v,width); b.byte(v,width);
                const auto n = static_cast<std::uint8_t>(v);
                for (unsigned i = 0; i < 2U*n; ++i) b.word(0,0);
                b.integer(0,0); b.word(0,0); empty_tail(b);
                const auto r = read(b);
                require(r.stack[0].locals.size() == n && r.stack[0].parameters.size() == n,"encoded byte sign extension then uint8 cast, including 255");
                require(r.consumed_bits == b.position,"byte counts resume exact cursor");
            }
        }
        for (auto width : {5U,8U,13U,16U}) {
            const auto sign = 1 << (width-1);
            for (auto v : {-sign,sign-1,-1,0,1}) {
                b = prefix(); b.integer(0,0); b.integer(0,0);
                b.integer(-1,6); b.integer(0,0); b.word(1,5);
                b.word(static_cast<std::int16_t>(v),width); b.word(static_cast<std::int16_t>(v),width);
                b.word(1,5); b.word(static_cast<std::int16_t>(v),width);
                const auto r = read(b);
                require(r.linked_slot->unknown == v && r.linked_slot->object_id == v && r.dynamic_sprite_flags[0] == v,"int16 selectors and raw signed slot/sprite values");
            }
        }
        rejects(empty(-1),"negative"); rejects(empty(std::numeric_limits<std::int32_t>::min()),"negative");
        rejects(empty(1025),"stack frame count exceeds");
        b = prefix(); b.integer(0,0); b.integer(0,0); b.integer(0,0); rejects(b,"nonnegative relationship flag");
        b = prefix(); b.integer(0,0); b.integer(0,0); b.integer(-1,6); b.integer(-1,6); rejects(b,"negative");
        b = prefix(); b.integer(0,0); b.integer(0,0); b.integer(-1,6); b.integer(4097,21); rejects(b,"relationship count exceeds");
        b = prefix(); b.integer(0,0); b.integer(0,0); b.integer(-1,6); b.integer(1,6);
        b.integer(0,0); b.integer(0,0); b.integer(0,0); b.word(0,0); b.word(0,0); rejects(b,"absent relationship");
        for (auto v : {-1,4097}) {
            b = prefix(); b.integer(0,0); b.integer(0,0); b.integer(-1,6); b.integer(1,6);
            b.integer(1,6); b.integer(0,0); b.integer(v,21); b.word(0,0); b.word(0,0);
            rejects(b,v < 0 ? "negative" : "relationship value count exceeds");
            b = prefix(); b.integer(0,0); b.integer(0,0); b.integer(-1,6); b.integer(0,0);
            b.word(static_cast<std::int16_t>(v)); b.word(0,0);
            rejects(b,v < 0 ? "negative" : "slot count exceeds");
            b = prefix(); b.integer(0,0); b.integer(0,0); b.integer(-1,6); b.integer(0,0); b.word(0,0);
            b.word(static_cast<std::int16_t>(v)); rejects(b,v < 0 ? "negative" : "dynamic sprite flag count exceeds");
        }
        odh::ObjmCommonLimits limits;
        limits.decoded_words = c.decoded_words;
        require(read(rich(),limits).decoded_words == c.decoded_words,"exact cumulative decoded budget");
        --limits.decoded_words; rejects(rich(),"budget",limits);
        limits = {}; limits.decoded_words = 5; rejects(empty(),"budget",limits);
        limits.decoded_words = 0; rejects(empty(),"budget",limits);
        limits = {}; limits.stack_frames = 0; rejects(rich(),"stack frame count exceeds",limits);
        limits = {}; limits.relationships = 1; rejects(rich(),"relationship count exceeds",limits);
        limits = {}; limits.relationship_values = 2; rejects(rich(),"relationship value count exceeds",limits);
        limits = {}; limits.slots = 2; rejects(rich(),"slot count exceeds",limits);
        limits = {}; limits.dynamic_sprite_flags = 2; rejects(rich(),"dynamic sprite flag count exceeds",limits);
        limits = {}; limits.block_bytes = rich().bytes.size()-1; rejects(rich(),"byte limit",limits);
        limits.block_bytes = rich().bytes.size(); require(read(rich(),limits).stack.size() == 1,"exact block byte budget");
        limits = {}; limits.stack_frames = std::numeric_limits<std::uint32_t>::max();
        limits.decoded_words = std::numeric_limits<std::uint64_t>::max();
        rejects(empty(std::numeric_limits<std::int32_t>::max()),"truncated",limits);
        // Default count boundaries are accepted, with all zero payload words
        // still explicitly present. Each distinct variable allocation is gated.
        b = prefix(); b.integer(1024,21); b.integer(0,0);
        for (unsigned i = 0; i < 1024; ++i) {
            b.word(0,0); b.word(0,0); b.word(0,0); b.byte(0,0); b.byte(0,0);
            b.integer(0,0); b.word(0,0);
        }
        empty_tail(b);
        require(read(b).stack.size() == 1024,"exact default stack count boundary");
        b = prefix(); b.integer(0,0); b.integer(0,0); b.integer(-1,6); b.integer(4096,21);
        for (unsigned i = 0; i < 4096; ++i) {
            b.integer(1,6); b.integer(0,0); b.integer(0,0);
        }
        b.word(0,0); b.word(0,0);
        require(read(b).relationships.size() == 4096,"exact default relationship count boundary");
        b = prefix(); b.integer(0,0); b.integer(0,0); b.integer(-1,6); b.integer(1,6);
        b.integer(1,6); b.integer(0,0); b.integer(4096,21);
        for (unsigned i = 0; i < 4096; ++i) b.integer(0,0);
        b.word(0,0); b.word(0,0);
        require(read(b).relationships[0].values.size() == 4096,"exact default per-relationship value boundary");
        limits = {}; limits.decoded_words = 4096;
        rejects(b,"budget",limits); // Counts/other fields share the array budget.
        b = prefix(); b.integer(0,0); b.integer(0,0); b.integer(-1,6); b.integer(0,0); b.word(4096);
        for (unsigned i = 0; i < 8192; ++i) b.word(0,0);
        b.word(0,0);
        require(read(b).slots.size() == 4095 && read(b).linked_slot.has_value(),"exact default slot count includes linked slot");
        b = prefix(); b.integer(0,0); b.integer(0,0); b.integer(-1,6); b.integer(0,0); b.word(0,0); b.word(4096);
        for (unsigned i = 0; i < 4096; ++i) b.word(0,0);
        require(read(b).dynamic_sprite_flags.size() == 4096,"exact default sprite flag count boundary");
        limits = {}; limits.stack_frames = 0; limits.relationships = 0;
        limits.relationship_values = 0; limits.slots = 0; limits.dynamic_sprite_flags = 0; limits.decoded_words = 6;
        require(read(empty(),limits).decoded_words == 6,"zero variable budgets permit minimal section");
        // Prefix separately succeeds; every subsequent byte truncation fails.
        b = rich();
        for (std::size_t size = 12; size < b.bytes.size(); ++size) {
            auto short_bits = b; short_bits.bytes.resize(size); rejects(short_bits,"truncated");
        }
        input = block(empty()); auto bad = odh::read_objm_prefix(input);
        for (unsigned i = 0; i < 4; ++i) {
            auto wrong = bad;
            if (i == 0) ++wrong.block_offset;
            if (i == 1) wrong.odd_offset = !wrong.odd_offset;
            if (i == 2) wrong.consumed_bits = input.bytes.size()*8+1;
            if (i == 3) ++wrong.remaining_bits;
            bool failed = false;
            try { (void)odh::read_objm_common(input,wrong); }
            catch (const odh::FormatError&) { failed = true; }
            require(failed,"mismatched prefix metadata rejected");
        }
        for (int i = 1; i < argc; ++i) native(argv[i]);
        std::cout << checks << " OBJM common checks passed (resource-dependent tail remains raw)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OBJM common test failed: " << error.what() << '\n'; return 1;
    }
}
