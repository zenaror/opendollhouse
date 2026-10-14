#include "odh/objm_prefix.hpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
// Fixture widths are explicit. No production encoder/selector logic is reused.
struct Bits {
    std::vector<std::uint8_t> bytes;
    std::uint64_t position = 0;
    void bit(unsigned value) {
        if (position % 8 == 0) bytes.push_back(0);
        if (value & 1U) bytes.back() |= static_cast<std::uint8_t>(1U << (7 - position % 8));
        ++position;
    }
    void field(std::int32_t value, unsigned width, bool big) {
        if (!width) {
            if (value != 0) throw std::logic_error("fixture compact zero");
            bit(0); return;
        }
        const std::array<unsigned, 4> widths = big ? std::array<unsigned, 4>{6,11,21,32}
                                                 : std::array<unsigned, 4>{5,8,13,16};
        unsigned selector = 0;
        while (selector < widths.size() && widths[selector] != width) ++selector;
        if (selector >= widths.size()) throw std::logic_error("fixture explicit width");
        bit(1); bit(selector >> 1); bit(selector);
        const auto raw = static_cast<std::uint32_t>(value);
        for (unsigned i = width; i > 0; --i) bit(raw >> (i - 1));
    }
    void word(std::int16_t value, unsigned width = 16) { field(value, width, false); }
    void integer(std::int32_t value, unsigned width = 32) { field(value, width, true); }
};
Bits zeros(std::int16_t count = 0, unsigned count_width = 0) {
    Bits bits;
    for (unsigned i = 0; i < 7; ++i) bits.integer(0, 0);
    bits.word(0,0); bits.word(count,count_width);
    return bits;
}
void zero_banks(Bits& bits, unsigned attrs = 0) {
    for (unsigned i = 0; i < attrs + 8 + 68 + 5; ++i) bits.word(0,0);
}
odh::ObjmBlock block(const Bits& bits, std::uint32_t offset = 0) { return {offset,bits.bytes}; }
void rejects(const odh::ObjmBlock& input, const char* diagnostic,
             const odh::ObjmPrefixLimits& limits = {}) {
    bool failed = false;
    try { odh::read_objm_prefix(input,limits); }
    catch (const odh::FormatError& error) {
        failed = std::string(error.what()).find(diagnostic) != std::string::npos;
    }
    require(failed,"invalid prefix fails with expected diagnostic");
}
void native(const char* path) {
    std::ifstream stream(path,std::ios::binary);
    if (!stream) throw std::runtime_error("native fixture cannot be opened");
    stream.seekg(0,std::ios::end);
    const auto size = stream.tellg(); stream.seekg(0);
    require(size >= 0,"native fixture size");
    unsigned chunks = 0;
    odh::iff_chunks(odh::View(stream,static_cast<std::uint64_t>(size)),[&](const odh::Chunk& chunk) {
        if (chunk.type != "ObjM" || chunk.id != 1) return;
        ++chunks;
        const auto container = odh::read_objm(chunk.payload);
        std::set<std::uint16_t> ids;
        for (const auto& input : container.blocks) {
            const auto p = odh::read_objm_prefix(input);
            const auto id = static_cast<std::uint16_t>(p.object_data[11]);
            require(container.id_to_objt.count(id) == 1,"native ID resolves separately from block order");
            require(ids.insert(id).second,"native prefix IDs unique");
            require(p.block_offset == input.offset && p.odd_offset == bool(input.offset & 1U),"native offset parity");
            require(p.consumed_bits + p.remaining_bits == input.bytes.size()*8,"native exact cursor extent");
            // Machine-readable rows for comparison with ignored independent
            // reports/objm-prefix-audit.py. Native data never becomes a fixture.
            std::cout << "native_prefix " << p.block_offset << ' ' << input.bytes.size() << ' '
                      << p.consumed_bits << ' ' << p.remaining_bits << ' '
                      << p.footprint.min_y << ' ' << p.footprint.min_x << ' '
                      << p.footprint.max_y << ' ' << p.footprint.max_x << ' '
                      << p.x << ' ' << p.y << ' ' << p.level << ' ' << p.unknown_data << ' '
                      << p.attributes.size() << ' ' << p.object_data[11] << ' '
                      << container.id_to_objt.at(id) << ' ' << p.object_data[1] << ' '
                      << p.object_data[2] << ' ' << p.object_data[3] << '\n';
            std::cout << "native_banks " << p.block_offset;
            for (auto word : p.attributes) std::cout << ' ' << word;
            for (auto word : p.temps) std::cout << ' ' << word;
            for (auto word : p.object_data) std::cout << ' ' << word;
            for (auto word : p.extras) std::cout << ' ' << word;
            std::cout << '\n';
        }
        require(ids.size() == container.id_to_objt.size(),"native ID set matches map");
    });
    require(chunks == 1,"exactly one native ObjM1");
}
}

int main(int argc, char** argv) {
    try {
        auto bits = zeros(); zero_banks(bits);
        const auto empty = odh::read_objm_prefix(block(bits));
        require(empty.attributes.empty() && empty.level == 0 && empty.unknown_data == 0,"raw zero prefix");
        require(empty.consumed_bits == 90 && empty.remaining_bits == 6,"compact zeros exact bit cursor");
        require(empty.block_offset == 0 && !empty.odd_offset,"even offset metadata");
        // Hand-written independent 90-zero-bit prefix, trailing bits 111111.
        odh::ObjmBlock literal{19,std::vector<std::uint8_t>(12,0)};
        literal.bytes.back() = 0x3f;
        const auto literal_result = odh::read_objm_prefix(literal);
        require(literal_result.consumed_bits == 90 && literal_result.remaining_bits == 6 &&
                literal_result.block_offset == 19 && literal_result.odd_offset,"literal MSB zeros and ignored tail");
        for (const unsigned width : {6U,11U,21U,32U}) {
            const auto sign = std::int64_t{1} << (width - 1);
            const auto minimum = static_cast<std::int32_t>(-sign);
            const auto maximum = static_cast<std::int32_t>(sign - 1);
            for (const auto value : {minimum,maximum,-1,0,1}) {
                Bits encoded;
                for (unsigned i = 0; i < 7; ++i) encoded.integer(value,width);
                encoded.word(0,0); encoded.word(0,0); zero_banks(encoded);
                const auto p = odh::read_objm_prefix(block(encoded));
                require(p.footprint.min_y == value && p.footprint.min_x == value &&
                        p.footprint.max_y == value && p.footprint.max_x == value &&
                        p.x == value && p.y == value && p.level == value,"every int32 selector and signed boundary; raw Level");
                require(p.consumed_bits == encoded.position &&
                        p.remaining_bits == encoded.bytes.size()*8 - encoded.position,"int32 exact bit cursor");
                for (std::size_t size = 0; size < encoded.bytes.size(); ++size) {
                    auto truncated = block(encoded); truncated.bytes.resize(size); rejects(truncated,"truncated");
                }
            }
        }
        for (const unsigned width : {5U,8U,13U,16U}) {
            const auto sign = 1 << (width - 1);
            const auto minimum = static_cast<std::int16_t>(-sign);
            const auto maximum = static_cast<std::int16_t>(sign - 1);
            for (const auto value : {minimum,maximum,std::int16_t{-1},std::int16_t{0},std::int16_t{1}}) {
                Bits encoded;
                for (unsigned i = 0; i < 7; ++i) encoded.integer(0,0);
                encoded.word(value,width); encoded.word(3,5);
                for (unsigned i = 0; i < 3 + 8 + 68 + 5; ++i) encoded.word(value,width);
                const auto p = odh::read_objm_prefix(block(encoded));
                require(p.unknown_data == value && p.attributes == std::vector<std::int16_t>(3,value),"int16 attrs and unknown signed boundaries");
                for (auto word : p.temps) require(word == value,"temps8 direct int16 fields");
                for (auto word : p.object_data) require(word == value,"ObjectData68 direct int16 fields");
                for (auto word : p.extras) require(word == value,"extras5 direct int16 fields");
                require(p.consumed_bits == encoded.position,"int16 exact bit cursor");
            }
        }
        Bits varied;
        for (auto value : {352,272,368,288,280,360,1}) varied.integer(value,11);
        varied.word(-9,5); varied.word(2,5); varied.word(123,8); varied.word(-123,8);
        for (std::int16_t i = 0; i < 8 + 68 + 5; ++i) varied.word(static_cast<std::int16_t>(i-20),8);
        auto owned_input = block(varied,0xffffffffU);
        const auto owned = odh::read_objm_prefix(owned_input);
        owned_input.bytes.assign(2,255);
        require(owned.footprint.min_y == 352 && owned.footprint.min_x == 272 &&
                owned.footprint.max_y == 368 && owned.footprint.max_x == 288 &&
                owned.x == 280 && owned.y == 360 && owned.level == 1,"field order and no delta decoding");
        require(owned.attributes == std::vector<std::int16_t>{123,-123} && owned.temps[7] == -13 &&
                owned.object_data[0] == -12 && owned.object_data[67] == 55 && owned.extras[4] == 60,
                "owned data survives changed block and fixed bank order");
        require(owned.block_offset == 0xffffffffU && owned.odd_offset,"raw offset retained without overflow");
        for (std::uint32_t offset = 0; offset < 16; ++offset) {
            const auto p = odh::read_objm_prefix(block(varied,offset));
            require(p.block_offset == offset && p.odd_offset == bool(offset&1U) &&
                    p.consumed_bits == varied.position,"offset parity does not change field bit origin");
        }
        // Full prefix ending exactly at a byte boundary: four encoded5 words
        // add28 bits and one encoded8 adds10 to the minimal90-bit prefix.
        bits = zeros();
        for (unsigned i = 0; i < 4; ++i) bits.word(1,5);
        bits.word(1,8);
        for (unsigned i = 5; i < 81; ++i) bits.word(0,0);
        require(bits.position == 128,"fixture byte boundary");
        auto aligned = block(bits); aligned.bytes.push_back(255);
        const auto p = odh::read_objm_prefix(aligned);
        require(p.consumed_bits == 128 && p.remaining_bits == 8,"exact byte boundary does not prefetch tail");
        for (auto count : {std::int16_t{-1},std::numeric_limits<std::int16_t>::min()})
            rejects(block(zeros(count,16)),"negative");
        rejects(block(zeros(4097,16)),"count exceeds");
        bits = zeros(4096,16); zero_banks(bits,4096);
        require(odh::read_objm_prefix(block(bits)).attributes.size() == 4096,"default attribute limit exact boundary");
        odh::ObjmPrefixLimits limits;
        limits.attributes = 4095; rejects(block(bits),"count exceeds",limits);
        limits.attributes = 0;
        auto small = zeros(); zero_banks(small);
        require(odh::read_objm_prefix(block(small),limits).attributes.empty(),"zero attribute budget accepts empty attrs");
        rejects(block(zeros(1,5)),"count exceeds",limits);
        limits.attributes = 32767;
        bits = zeros(32767,16); zero_banks(bits,32767);
        require(odh::read_objm_prefix(block(bits),limits).attributes.size() == 32767,"configurable full signed count bound");
        rejects(block(zeros(32767,16)),"truncated",limits);
        limits = {}; limits.block_bytes = small.bytes.size()-1; rejects(block(small),"byte limit",limits);
        limits.block_bytes = small.bytes.size();
        require(odh::read_objm_prefix(block(small),limits).consumed_bits == 90,"exact block byte budget");
        limits.block_bytes = 0; rejects(block(small),"byte limit",limits);
        rejects({0,{}} ,"truncated");
        rejects({0,{0xff}},"truncated"); // width32 selector with no payload
        for (int i = 1; i < argc; ++i) native(argv[i]);
        std::cout << checks << " OBJM prefix checks passed (raw prefix only; no entity restore)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OBJM prefix test failed: " << error.what() << '\n'; return 1;
    }
}
