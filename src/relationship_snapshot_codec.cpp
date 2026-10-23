#include "odh/relationship_snapshot_codec.hpp"

#include <limits>
#include <utility>

namespace odh {
namespace {
constexpr std::uint16_t codec_version = 1;
constexpr std::size_t header_size = 46;
constexpr std::size_t minimum_source_size = 15;
constexpr std::size_t minimum_pair_size = 8;

class Writer {
public:
    explicit Writer(std::size_t limit) : limit_(limit) {}
    void u8(std::uint8_t value) { push(value); }
    void u16(std::uint16_t value) {
        push(static_cast<std::uint8_t>(value));
        push(static_cast<std::uint8_t>(value >> 8));
    }
    void i16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }
    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            push(static_cast<std::uint8_t>(value >> shift));
    }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8)
            push(static_cast<std::uint8_t>(value >> shift));
    }
    void count(std::size_t value) {
        if (value > std::numeric_limits<std::uint32_t>::max())
            throw FormatError("relationship snapshot count exceeds format");
        u32(static_cast<std::uint32_t>(value));
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }
private:
    void push(std::uint8_t value) {
        if (bytes_.size() >= limit_) throw FormatError("relationship snapshot exceeds byte limit");
        bytes_.push_back(value);
    }
    std::size_t limit_;
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    std::uint8_t u8() { require(1); return bytes_[offset_++]; }
    std::uint16_t u16() {
        const auto low = static_cast<std::uint16_t>(u8());
        const auto high = static_cast<std::uint16_t>(u8());
        return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8));
    }
    std::int16_t i16() {
        const auto bits = u16();
        const auto value = bits <= static_cast<std::uint16_t>(INT16_MAX)
            ? static_cast<std::int32_t>(bits)
            : static_cast<std::int32_t>(bits) - 65536;
        return static_cast<std::int16_t>(value);
    }
    std::uint32_t u32() {
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
            value |= static_cast<std::uint32_t>(u8()) << shift;
        return value;
    }
    std::int32_t i32() {
        const auto bits = u32();
        const auto value = bits <= static_cast<std::uint32_t>(INT32_MAX)
            ? static_cast<std::int64_t>(bits)
            : static_cast<std::int64_t>(bits) - INT64_C(4294967296);
        return static_cast<std::int32_t>(value);
    }
    std::uint64_t u64() {
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
            value |= static_cast<std::uint64_t>(u8()) << shift;
        return value;
    }
    std::size_t size(std::size_t maximum) {
        const auto value = u64();
        if (value > maximum || value > std::numeric_limits<std::size_t>::max())
            throw FormatError("relationship snapshot size exceeds limit");
        return static_cast<std::size_t>(value);
    }
    std::uint32_t count(std::size_t maximum, std::size_t minimum_bytes) {
        const auto value = u32();
        if (value > maximum || (minimum_bytes && value > remaining() / minimum_bytes))
            throw FormatError("relationship snapshot count exceeds limit or remaining data");
        return value;
    }
    std::size_t remaining() const { return bytes_.size() - offset_; }
    bool done() const { return offset_ == bytes_.size(); }
private:
    void require(std::size_t count) {
        if (count > remaining()) throw FormatError("relationship snapshot is truncated");
    }
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
};

bool source_less(const RelationshipSourceSnapshot& a, const RelationshipSourceSnapshot& b) {
    if (a.matrix != b.matrix) return a.matrix < b.matrix;
    if (a.id != b.id) return a.id < b.id;
    return a.generation < b.generation;
}

void validate_snapshot(const RelationshipSnapshot& snapshot, RelationshipLimits caps) {
    for (std::size_t i = 1; i < snapshot.sources.size(); ++i)
        if (!source_less(snapshot.sources[i - 1], snapshot.sources[i]))
            throw FormatError("relationship snapshot sources are not in canonical order");
    for (const auto& source : snapshot.sources) {
        for (std::size_t i = 1; i < source.pairs.size(); ++i)
            if (source.pairs[i - 1].target >= source.pairs[i].target)
                throw FormatError("relationship snapshot pairs are not in canonical order");
    }
    RelationshipState candidate(caps);
    if (!candidate.restore_snapshot(snapshot))
        throw FormatError("invalid relationship snapshot state");
}

void write_source(Writer& writer, const RelationshipSourceSnapshot& source) {
    writer.u8(static_cast<std::uint8_t>(source.matrix));
    writer.i16(source.id);
    writer.u64(source.generation);
    writer.count(source.pairs.size());
    for (const auto& pair : source.pairs) {
        writer.i32(pair.target);
        writer.count(pair.values.size());
        for (const auto value : pair.values) writer.i16(value);
    }
}
}

std::vector<std::uint8_t> encode_relationship_snapshot(
        const RelationshipSnapshot& snapshot, RelationshipSnapshotCodecLimits limits) {
    if (limits.max_bytes < header_size)
        throw FormatError("relationship snapshot byte limit is too small");
    validate_snapshot(snapshot, limits.relationships);
    Writer writer(limits.max_bytes);
    writer.u16(codec_version);
    writer.u64(static_cast<std::uint64_t>(snapshot.limits.sources));
    writer.u64(static_cast<std::uint64_t>(snapshot.limits.pairs));
    writer.u64(static_cast<std::uint64_t>(snapshot.limits.values));
    writer.u64(static_cast<std::uint64_t>(snapshot.pair_count));
    writer.u64(static_cast<std::uint64_t>(snapshot.value_count));
    writer.count(snapshot.sources.size());
    for (const auto& source : snapshot.sources) write_source(writer, source);
    return writer.take();
}

RelationshipSnapshot decode_relationship_snapshot(
        const std::vector<std::uint8_t>& bytes, RelationshipSnapshotCodecLimits limits) {
    if (limits.max_bytes < header_size || bytes.size() > limits.max_bytes)
        throw FormatError("relationship snapshot exceeds byte limit");
    if (bytes.size() < header_size) throw FormatError("relationship snapshot header is truncated");
    Reader reader(bytes);
    if (reader.u16() != codec_version)
        throw FormatError("unsupported relationship snapshot version");
    RelationshipSnapshot snapshot;
    snapshot.limits.sources = reader.size(limits.relationships.sources);
    snapshot.limits.pairs = reader.size(limits.relationships.pairs);
    snapshot.limits.values = reader.size(limits.relationships.values);
    snapshot.pair_count = reader.size(snapshot.limits.pairs);
    snapshot.value_count = reader.size(snapshot.limits.values);
    const auto source_count = reader.count(snapshot.limits.sources, minimum_source_size);
    snapshot.sources.reserve(source_count);

    std::size_t pairs_seen = 0;
    std::size_t values_seen = 0;
    for (std::uint32_t i = 0; i < source_count; ++i) {
        RelationshipSourceSnapshot source{};
        const auto matrix = reader.u8();
        if (matrix > static_cast<std::uint8_t>(RelationshipMatrix::object))
            throw FormatError("invalid relationship matrix kind");
        source.matrix = static_cast<RelationshipMatrix>(matrix);
        source.id = reader.i16();
        source.generation = reader.u64();
        const auto pair_count = reader.count(snapshot.limits.pairs - pairs_seen, minimum_pair_size);
        if (pair_count > snapshot.limits.pairs - pairs_seen)
            throw FormatError("relationship pair count exceeds limit");
        pairs_seen += pair_count;
        source.pairs.reserve(pair_count);
        for (std::uint32_t j = 0; j < pair_count; ++j) {
            RelationshipPairSnapshot pair{};
            pair.target = reader.i32();
            const auto value_count = reader.count(snapshot.limits.values - values_seen, 2);
            values_seen += value_count;
            pair.values.reserve(value_count);
            for (std::uint32_t k = 0; k < value_count; ++k)
                pair.values.push_back(reader.i16());
            source.pairs.push_back(std::move(pair));
        }
        snapshot.sources.push_back(std::move(source));
    }
    if (!reader.done()) throw FormatError("relationship snapshot has unclaimed bytes");
    validate_snapshot(snapshot, limits.relationships);
    return snapshot;
}

} // namespace odh
