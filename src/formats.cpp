#include "odh/formats.hpp"

#include <algorithm>
#include <limits>

namespace odh {

View::View(std::istream& input, std::uint64_t size) : View(input, 0, size) {}
View::View(std::istream& input, std::uint64_t base, std::uint64_t size)
    : input_(&input), base_(base), size_(size) {
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max());
    if (base > maximum || size > maximum - base)
        throw FormatError("view exceeds seekable stream range");
}

void View::check(std::uint64_t offset, std::uint64_t length) const {
    if (offset > size_ || length > size_ - offset)
        throw FormatError("read outside resource at offset " + std::to_string(offset));
}

View View::subview(std::uint64_t offset, std::uint64_t length) const {
    check(offset, length);
    return View(*input_, base_ + offset, length);
}

void View::read(std::uint64_t offset, void* destination, std::size_t length) const {
    check(offset, length);
    if (length > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        throw FormatError("read exceeds streamsize range");
    input_->clear();
    input_->seekg(static_cast<std::streamoff>(base_ + offset));
    if (!*input_) throw FormatError("seek failed");
    if (length == 0) return;
    input_->read(static_cast<char*>(destination), static_cast<std::streamsize>(length));
    if (input_->gcount() != static_cast<std::streamsize>(length))
        throw FormatError("input truncated during read");
}

std::uint8_t View::u8(std::uint64_t offset) const {
    std::uint8_t value{};
    read(offset, &value, 1);
    return value;
}
std::uint16_t View::le16(std::uint64_t offset) const {
    std::array<std::uint8_t, 2> b{};
    read(offset, b.data(), b.size());
    return static_cast<std::uint16_t>(b[0] | (static_cast<std::uint16_t>(b[1]) << 8));
}
std::uint16_t View::be16(std::uint64_t offset) const {
    std::array<std::uint8_t, 2> b{};
    read(offset, b.data(), b.size());
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[0]) << 8) | b[1]);
}
std::uint32_t View::le32(std::uint64_t offset) const {
    std::array<std::uint8_t, 4> b{};
    read(offset, b.data(), b.size());
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(b[i]) << (8 * i);
    return value;
}
std::uint32_t View::be32(std::uint64_t offset) const {
    std::array<std::uint8_t, 4> b{};
    read(offset, b.data(), b.size());
    std::uint32_t value = 0;
    for (const auto byte : b) value = (value << 8) | byte;
    return value;
}

std::vector<FarEntry> far1_index(const View& file, const Limits& limits) {
    std::array<char, 8> magic{};
    file.read(0, magic.data(), magic.size());
    if (std::string(magic.data(), magic.size()) != "FAR!byAZ" || file.le32(8) != 1)
        throw FormatError("unsupported FAR signature/version");
    const std::uint64_t manifest = file.le32(12);
    if (manifest < 16) throw FormatError("FAR manifest overlaps header");
    if (manifest > file.size()) throw FormatError("FAR manifest outside file");
    const auto table = file.subview(manifest, file.size() - manifest);
    const auto count = table.le32(0);
    if (count > limits.far_entries || count > (table.size() - 4) / 17)
        throw FormatError("FAR entry count exceeds index bounds or configured limit");
    std::vector<FarEntry> entries;
    entries.reserve(count);
    std::uint64_t position = 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto length = table.le32(position);
        const auto second_length = table.le32(position + 4);
        const auto offset = table.le32(position + 8);
        const auto name_length = table.le32(position + 12);
        position += 16;
        if (length != second_length)
            throw FormatError("unsupported FAR entry with differing data lengths");
        if (offset < 16 || offset > manifest || length > manifest - offset)
            throw FormatError("FAR entry outside pre-manifest data area");
        if (name_length == 0 || name_length > 4096 || name_length > table.size() - position)
            throw FormatError("FAR name outside index bounds or supported length");
        std::string name(name_length, '\0');
        table.read(position, name.data(), name.size());
        if (name.find('\0') != std::string::npos) throw FormatError("FAR name contains NUL");
        position += name_length;
        entries.push_back({std::move(name), offset, length});
    }
    if (position != table.size())
        throw FormatError("unsupported FAR index layout or trailing bytes");
    return entries;
}

std::string iff_chunks(const View& file, const std::function<void(const Chunk&)>& visit,
                       const Limits& limits) {
    std::array<char, 60> header{};
    file.read(0, header.data(), header.size());
    std::string signature;
    for (const char c : header) if (c != '\0') signature += c;
    const std::string suffix = ":TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    std::string version;
    if (signature == "IFF FILE 2.5" + suffix) version = "2.5";
    else if (signature == "IFF FILE 2.0" + suffix) version = "2.0";
    else throw FormatError("unsupported IFF signature/version");
    file.be32(60); // Resource-map pointer, not a chunk boundary authority.
    std::uint64_t position = 64;
    std::uint32_t count = 0;
    while (position < file.size()) {
        if (++count > limits.iff_chunks) throw FormatError("IFF chunk count exceeds configured limit");
        std::array<char, 76> bytes{};
        file.read(position, bytes.data(), bytes.size());
        const std::uint64_t length = file.be32(position + 4);
        if (length < 76) throw FormatError("IFF chunk length is smaller than header");
        const auto payload = file.subview(position + 76, length - 76);
        visit({std::string(bytes.data(), 4), file.be16(position + 8),
               file.be16(position + 10), position, payload});
        position += length;
    }
    return version;
}

Bhav read_bhav(const View& payload) {
    const auto version = payload.le16(0);
    if (version < 0x8000 || version > 0x8002)
        throw FormatError("unsupported BHAV version (supports 8000/8001/8002)");
    const auto count = payload.le16(2);
    if (payload.size() != 12 + static_cast<std::uint64_t>(count) * 12)
        throw FormatError("BHAV payload length does not match instruction count");
    Bhav routine{version, {}};
    routine.instructions.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint64_t offset = 12 + i * 12;
        Instruction instruction{payload.le16(offset), payload.u8(offset + 2),
                                payload.u8(offset + 3), {}};
        payload.read(offset + 4, instruction.operands.data(), instruction.operands.size());
        for (const auto target : {instruction.on_true, instruction.on_false}) {
            if (target < 0xFD && target >= count)
                throw FormatError("BHAV branch outside instruction list at node " + std::to_string(i));
        }
        routine.instructions.push_back(instruction);
    }
    return routine;
}

void spr2_frames(const View& payload, const std::function<void(const SpriteFrame&)>& visit,
                 const Limits& limits) {
    if (payload.le32(0) != 1000) throw FormatError("unsupported SPR2 version (supports 1000)");
    const auto count = payload.le32(4);
    payload.le32(8); // Default palette; pixel/palette semantics are not interpreted here.
    if (count > limits.sprite_frames || count > (payload.size() - 12) / 4)
        throw FormatError("SPR2 frame count exceeds table bounds or configured limit");
    const std::uint64_t table_end = 12 + static_cast<std::uint64_t>(count) * 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint64_t offset = payload.le32(12 + static_cast<std::uint64_t>(i) * 4);
        if (offset < table_end) throw FormatError("SPR2 frame overlaps offset table");
        const auto header = payload.subview(offset, 16);
        visit({header.le16(0), header.le16(2), header.le32(4)});
    }
}

} // namespace odh
