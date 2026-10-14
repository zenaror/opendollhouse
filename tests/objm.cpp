#include "odh/objm.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void put(std::string& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(offset+i) = static_cast<char>((value >> (8*i)) & 255U);
}
// Independent fixture writer: choose width explicitly, rather than reproducing
// the reader's width selection or casting a decoded value back to the wire.
struct Bits {
    std::string bytes;
    unsigned position = 0;
    void bit(unsigned value) {
        if (position % 8 == 0) bytes.push_back('\0');
        if (value) bytes.back() = static_cast<char>(static_cast<unsigned char>(bytes.back()) |
                                                   (1U << (7 - position % 8)));
        ++position;
    }
    void field(std::uint16_t bits, unsigned width) {
        if (!width) { bit(0); return; }
        unsigned code = 0;
        if (width == 8) code = 1;
        else if (width == 13) code = 2;
        else if (width == 16) code = 3;
        else require(width == 5, "fixture width");
        bit(1); bit(code >> 1); bit(code & 1U);
        for (unsigned i = width; i > 0; --i) bit((bits >> (i - 1)) & 1U);
    }
};
struct Record { std::uint16_t id, type; unsigned id_width, type_width; };
struct Fixture { std::string bytes; std::vector<std::size_t> skips; };
Fixture fixture(const std::vector<Record>& records,
                const std::vector<std::string>& blocks, bool terminate = true) {
    Fixture result{std::string(13, '\0'), {}};
    put(result.bytes, 0, 0xa1b2c3d4U); put(result.bytes, 4, 0x3e);
    put(result.bytes, 8, 0x4f626a4dU); result.bytes[12] = 1;
    Bits bits;
    for (const auto& record : records) {
        bits.field(record.id, record.id_width); bits.field(record.type, record.type_width);
    }
    if (terminate) bits.field(0, 0);
    // Deliberately nonzero padding: Interrupt ignores all bits after ID zero.
    while (bits.position % 8) bits.bit(1);
    result.bytes += bits.bytes;
    for (const auto& block : blocks) {
        const auto skip_word = result.bytes.size(); result.skips.push_back(skip_word);
        result.bytes.resize(skip_word + 4, '\0'); result.bytes += block;
        put(result.bytes, skip_word, static_cast<std::uint32_t>(result.bytes.size() - 12));
    }
    return result;
}
odh::Objm parse(const std::string& bytes, const odh::ObjmLimits& limits = {}) {
    std::istringstream stream(bytes, std::ios::binary);
    return odh::read_objm(odh::View(stream, bytes.size()), limits);
}
void rejects(const std::string& bytes, const char* diagnostic = nullptr,
             const odh::ObjmLimits& limits = {}) {
    bool failed = false;
    try { parse(bytes, limits); }
    catch (const odh::FormatError& error) {
        failed = !diagnostic || std::string(error.what()).find(diagnostic) != std::string::npos;
    }
    require(failed, "invalid OBJM must fail with expected diagnostic");
}
void native(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("native fixture cannot be opened");
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg(); stream.seekg(0);
    require(size >= 0, "native fixture length");
    unsigned chunks = 0;
    odh::iff_chunks(odh::View(stream, static_cast<std::uint64_t>(size)), [&](const odh::Chunk& chunk) {
        if (chunk.type != "ObjM") return;
        ++chunks;
        const auto container = odh::read_objm(chunk.payload);
        require(container.map_records.size() == container.id_to_objt.size() &&
                container.blocks.size() == container.id_to_objt.size(), "native container counts");
        std::uint64_t bytes = 0, previous_end = 0;
        for (const auto& block : container.blocks) {
            require(12U + block.offset + block.bytes.size() <= chunk.payload.size(), "native block extent");
            if (previous_end) require(block.offset == previous_end + 4, "native contiguous skip words");
            previous_end = block.offset + block.bytes.size(); bytes += block.bytes.size();
        }
        if (previous_end) require(12 + previous_end + container.remaining_bytes == chunk.payload.size(), "native tail extent");
        std::cout << path << ": OBJM version=" << container.version << " ids=" << container.map_records.size()
                  << " blocks=" << container.blocks.size() << " owned_bytes=" << bytes
                  << " tail=" << container.remaining_bytes << '\n';
    });
    require(chunks == 1, "exactly one native OBJM");
}
}

int main(int argc, char** argv) {
    try {
        const std::vector<Record> records{{65535,65520,5,5}, {100,65408,8,8},
            {60000,61440,16,13}, {4000,65535,13,16}, {1,0,5,0}};
        const std::vector<std::string> blocks{"\x80\x00\xff", "hello", "", "\x7f", "last"};
        // std::string from a C string stops at NUL; create explicit binary data.
        auto binary_blocks = blocks; binary_blocks[0] = std::string("\x80\x00\xff", 3);
        const auto good = fixture(records, binary_blocks);
        const auto container = parse(good.bytes);
        require(container.reserved == 0xa1b2c3d4U && container.version == 62 &&
                container.compression == 1 && container.remaining_bytes == 0, "header fields");
        require(container.map_records.size() == records.size() && container.id_to_objt.size() == records.size() &&
                container.blocks.size() == records.size(), "canonical counts");
        for (std::size_t i = 0; i < records.size(); ++i) {
            require(container.map_records[i].object_id == records[i].id &&
                    container.map_records[i].type_id == records[i].type &&
                    container.id_to_objt.at(records[i].id) == records[i].type,
                    "widths, signed payload casts, zero type and table order");
            const auto& block = container.blocks[i];
            require(block.offset == good.skips[i] + 4 - 12 && block.bytes.size() == binary_blocks[i].size() &&
                    std::string(block.bytes.begin(), block.bytes.end()) == binary_blocks[i], "owned raw block bytes and relative base");
        }
        for (std::size_t n = 0; n < good.bytes.size(); ++n) rejects(good.bytes.substr(0,n));
        auto bytes = good.bytes + std::string("\0\0\xa3",3);
        require(parse(bytes).remaining_bytes == 3, "unknown tail extent");
        for (const auto version : {0U,0x33U,0x45U,0x49U,0xffffffffU}) {
            bytes = good.bytes; put(bytes,4,version);
            require(parse(bytes).version == version, "container preserves version without instance claims");
        }
        bytes = good.bytes; bytes[8] = 'x'; rejects(bytes,"signature");
        for (unsigned code : {0U,2U,255U}) {
            bytes = good.bytes; bytes[12] = static_cast<char>(code); rejects(bytes,"compression");
        }
        const auto empty = fixture({},{});
        require(empty.bytes.size() == 14 && parse(empty.bytes).blocks.empty(), "zero ID terminator and empty table");
        rejects(empty.bytes.substr(0,13),"truncated");
        for (const unsigned width : {5U,8U,13U,16U}) {
            const auto sign = 1U << (width-1);
            const auto minimum = static_cast<std::uint16_t>(65536U-sign);
            const auto maximum = static_cast<std::uint16_t>(sign-1);
            const auto decoded = parse(fixture({{minimum,maximum,width,width}},{""}).bytes);
            require(decoded.map_records.front().object_id == minimum &&
                    decoded.map_records.front().type_id == maximum,
                    "signed minimum and maximum for every compressed word width");
        }
        for (unsigned count = 1; count <= 16; ++count) {
            std::vector<Record> sequence;
            std::vector<std::string> data;
            for (unsigned i = 1; i <= count; ++i) { sequence.push_back({static_cast<std::uint16_t>(i),0,5,0}); data.push_back("x"); }
            const auto aligned = fixture(sequence,data);
            const auto decoded = parse(aligned.bytes);
            require(decoded.blocks.front().offset == aligned.skips.front() + 4 - 12 &&
                    decoded.blocks.back().bytes == std::vector<std::uint8_t>{'x'},
                    "Interrupt alignment including exact byte boundary and odd block offsets");
        }
        // Hand-written independent bit pattern: id=1 (10000001), type=0 (0),
        // terminator=0 (0), ignored padding=111111. No fixture field writer.
        bytes = empty.bytes.substr(0,13); bytes += std::string("\x81\x3f",2);
        bytes.resize(19,'\0'); put(bytes,15,7);
        require(parse(bytes).map_records.front().object_id == 1 && parse(bytes).blocks.front().bytes.empty(),
                "literal MSB field pattern and empty block");
        auto duplicate = fixture({{1,1,5,5},{1,2,5,5}},{"a","b"});
        rejects(duplicate.bytes,"duplicate");
        for (const auto bad : {0U,1U,0x80000000U,0xffffffffU,0x7fffffffU}) {
            bytes = good.bytes; put(bytes,good.skips.front(),bad); rejects(bytes,"offset");
        }
        bytes = good.bytes; put(bytes,good.skips.front(),static_cast<std::uint32_t>(good.skips.front()+3-12));
        rejects(bytes,"backwards");
        bytes = good.bytes; put(bytes,good.skips.back(),static_cast<std::uint32_t>(bytes.size()-11));
        rejects(bytes,"outside");
        odh::ObjmLimits limits;
        limits.object_count = 4; rejects(good.bytes,"count",limits);
        limits.object_count = 0; rejects(good.bytes,"count",limits);
        require(parse(empty.bytes,limits).blocks.empty(), "zero count budget accepts zero objects");
        limits = {}; limits.payload_bytes = good.bytes.size()-1; rejects(good.bytes,"payload",limits);
        limits = {}; limits.owned_block_bytes = 12; rejects(good.bytes,"owned",limits);
        limits.owned_block_bytes = 13; require(parse(good.bytes,limits).blocks.size() == 5,"exact owned byte budget");
        limits.owned_block_bytes = 0;
        require(parse(fixture({{1,0,5,0}},{""}).bytes,limits).blocks.front().bytes.empty(),"zero owned budget and empty block");
        std::vector<Record> many;
        std::vector<std::string> no_data;
        for (unsigned i = 1; i <= 4096; ++i) {
            many.push_back({static_cast<std::uint16_t>(i),0,16,0}); no_data.emplace_back();
        }
        require(parse(fixture(many,no_data).bytes).blocks.size() == 4096,"default object count limit exact boundary");
        many.push_back({4097,0,16,0}); no_data.emplace_back();
        rejects(fixture(many,no_data).bytes,"count");
        // Missing table terminator cannot read synthetic EOF zeroes as fields.
        bytes = good.bytes.substr(0,13); bytes.push_back(static_cast<char>(0xff)); rejects(bytes,"truncated");
        // A 64-bit advertised View size is rejected before accessing/allocating.
        std::istringstream tiny("x"); bool failed = false;
        try { odh::read_objm(odh::View(tiny,UINT64_MAX)); } catch (const odh::FormatError&) { failed = true; }
        require(failed,"huge advertised payload fails before allocation");
        for (int i = 1; i < argc; ++i) native(argv[i]);
        std::cout << checks << " OBJM checks passed (container only; instance state not decoded)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OBJM test failed: " << error.what() << '\n'; return 1;
    }
}
