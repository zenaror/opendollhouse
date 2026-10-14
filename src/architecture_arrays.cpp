#include "odh/architecture_arrays.hpp"
#include "odh/formats.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace odh {
namespace {
struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t p = 0;
    void need(std::size_t n) const {
        if (n > bytes.size() - p) throw FormatError("architecture payload truncated");
    }
    std::uint8_t byte() { need(1); return bytes[p++]; }
    std::uint16_t word() { auto a = byte(); auto b = byte(); return static_cast<std::uint16_t>(a | (std::uint16_t(b) << 8)); }
    std::int32_t integer() {
        std::uint32_t n = word(); n |= std::uint32_t(word()) << 16;
        return n <= 0x7fffffffU ? static_cast<std::int32_t>(n)
            : static_cast<std::int32_t>(std::int64_t(n) - 0x100000000LL);
    }
};
std::size_t extent(std::int32_t w, std::int32_t h, std::int32_t stride) {
    if (w <= 0 || h <= 0 || stride <= 0) throw FormatError("invalid architecture array extent");
    const std::uint64_t cells = std::uint64_t(w) * std::uint64_t(h);
    if (cells > std::numeric_limits<std::size_t>::max() / std::uint64_t(stride))
        throw FormatError("architecture extent overflow");
    return static_cast<std::size_t>(cells * std::uint64_t(stride));
}
}
ArchitectureArray read_architecture_array(const std::vector<std::uint8_t>& bytes,
                                         const ArchitectureArrayLimits& limits) {
    if (bytes.size() > limits.encoded_bytes) throw FormatError("architecture encoded byte limit");
    Reader r{bytes}; ArchitectureArray a;
    a.zero = r.integer(); a.width = r.integer(); a.height = r.integer();
    a.stride = r.integer(); a.unknown = r.integer();
    const auto size = extent(a.width,a.height,a.stride);
    if (static_cast<std::uint32_t>(a.width) > limits.dimension ||
        static_cast<std::uint32_t>(a.height) > limits.dimension ||
        static_cast<std::uint32_t>(a.stride) > limits.stride || size > limits.decoded_bytes)
        throw FormatError("architecture decoded extent limit");
    // First validate the complete stream before allocating decoded storage.
    std::size_t produced = 0;
    while (r.p < bytes.size() && produced < size) {
        auto op = r.word();
        if (op == 0) { ++a.zero_opcodes; continue; }
        const bool fill = (op & 0x8000U) != 0;
        const auto n = std::size_t(op & 0x7fffU);
        if (n == 0 || n > size - produced) throw FormatError("architecture RLE output overrun or empty fill");
        if (fill) { r.need(2); r.p += 2; ++a.fill_runs; }
        else { r.need(n + (n & 1U)); r.p += n + (n & 1U); ++a.raw_runs; }
        produced += n;
    }
    if (produced != size) throw FormatError("architecture RLE output incomplete");
    a.consumed_bytes = r.p;
    a.trailing_bytes.assign(bytes.begin()+static_cast<std::ptrdiff_t>(r.p),bytes.end());
    a.encoded = bytes; a.data.resize(size); r.p = 20; produced = 0;
    while (r.p < bytes.size() && produced < size) {
        auto op = r.word(); if (op == 0) continue;
        const auto n = std::size_t(op & 0x7fffU);
        if (op & 0x8000U) {
            const auto value = r.byte(); r.byte();
            std::fill_n(a.data.begin() + static_cast<std::ptrdiff_t>(produced),n,value);
        } else {
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(r.p),n,
                        a.data.begin() + static_cast<std::ptrdiff_t>(produced));
            r.p += n + (n & 1U);
        }
        produced += n;
    }
    return a;
}
std::vector<std::uint8_t> transpose_architecture_array(const ArchitectureArray& a) {
    const auto size = extent(a.width,a.height,a.stride);
    if (size != a.data.size()) throw FormatError("architecture transpose extent mismatch");
    std::vector<std::uint8_t> result(size);
    const auto w = static_cast<std::size_t>(a.width), h = static_cast<std::size_t>(a.height);
    const auto stride = static_cast<std::size_t>(a.stride);
    for (std::size_t y = 0; y < h; ++y) for (std::size_t x = 0; x < w; ++x)
        std::copy_n(a.data.begin() + static_cast<std::ptrdiff_t>((y*w+x)*stride),stride,
                    result.begin() + static_cast<std::ptrdiff_t>((x*h+y)*stride));
    return result;
}
ArchitectureMap read_architecture_map(const std::vector<std::uint8_t>& bytes,
                                     std::uint16_t chunk_id, const ArchitectureMapLimits& limits) {
    if (bytes.size() > limits.encoded_bytes) throw FormatError("architecture map byte limit");
    Reader r{bytes}; ArchitectureMap result;
    result.zero = r.integer(); result.version = r.integer(); result.signature = r.integer();
    result.chunk_id = chunk_id; const auto count = r.integer();
    const std::size_t metadata = 5 + std::size_t(chunk_id) * 2;
    if (count < 0 || static_cast<std::uint64_t>(count) > limits.entries || metadata > limits.entry_metadata_bytes)
        throw FormatError("architecture map record limit");
    // Validate all extents/strings and exact EOF before any entry allocation.
    const auto start = r.p; std::size_t strings = 0;
    for (std::int32_t i = 0; i < count; ++i) {
        std::size_t length = 0;
        while (r.byte() != 0) {
            if (++length > limits.string_length || ++strings > limits.string_bytes)
                throw FormatError("architecture map string limit");
        }
        if ((length & 1U) == 0) r.byte();
        r.need(5 + metadata); r.p += 5 + metadata;
    }
    if (r.p != bytes.size()) throw FormatError("architecture map trailing bytes");
    r.p = start; result.entries.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        ArchitectureMapEntry e; auto b = r.byte();
        while (b) { e.name.push_back(static_cast<char>(b)); b = r.byte(); }
        if ((e.name.size() & 1U) == 0) e.name_padding.push_back(r.byte());
        e.unknown = r.integer(); e.id = r.byte();
        e.unknown2.assign(bytes.begin()+static_cast<std::ptrdiff_t>(r.p),
                          bytes.begin()+static_cast<std::ptrdiff_t>(r.p+metadata));
        r.p += metadata; result.entries.push_back(std::move(e));
    }
    result.encoded = bytes; return result;
}
}
