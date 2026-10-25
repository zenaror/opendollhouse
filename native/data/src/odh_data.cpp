#include "odh_data.h"
#include "odh/formats.hpp"

#include <cstddef>
#include <cstdint>
#include <istream>
#include <limits>
#include <streambuf>

namespace {

// View requires a seekable stream. This streambuf only exposes the caller's
// immutable byte range and never copies or takes ownership of the packet.
class BorrowedBuffer final : public std::streambuf {
public:
    BorrowedBuffer(const std::uint8_t* data, std::uint32_t length)
        : size_(static_cast<std::streamoff>(length)) {
        begin_ = const_cast<char*>(reinterpret_cast<const char*>(data));
        setg(begin_, begin_, begin_ + length);
    }

protected:
    pos_type seekoff(off_type offset, std::ios_base::seekdir direction,
                     std::ios_base::openmode mode) override {
        if (!(mode & std::ios_base::in)) return pos_type(off_type(-1));
        std::streamoff base;
        if (direction == std::ios_base::beg) base = 0;
        else if (direction == std::ios_base::cur) base = gptr() - eback();
        else if (direction == std::ios_base::end) base = size_;
        else return pos_type(off_type(-1));
        if (offset < -base || offset > size_ - base)
            return pos_type(off_type(-1));
        const auto target = base + offset;
        setg(begin_, begin_ + target, begin_ + size_);
        return pos_type(target);
    }

    pos_type seekpos(pos_type position, std::ios_base::openmode mode) override {
        return seekoff(static_cast<off_type>(position), std::ios_base::beg, mode);
    }

private:
    char* begin_;
    std::streamoff size_;
};

} // namespace

extern "C" uint32_t odh_data_abi_version(void) {
    return 1;
}

extern "C" int32_t odh_bcon_decode_v1(
    const uint8_t* data, uint32_t len,
    uint16_t* words, uint32_t capacity,
    uint32_t* count, uint32_t* flags, uint32_t* remaining) {
    if (count) *count = 0;
    if (flags) *flags = 0;
    if (remaining) *remaining = 0;
    if (!count || !flags || !remaining || !data || (!words && capacity))
        return ODH_DATA_INVALID_ARGUMENT;
    if (len > static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) ||
        len > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
        return ODH_DATA_INVALID_ARGUMENT;

    try {
        BorrowedBuffer buffer(data, len);
        std::istream input(&buffer);
        const odh::View view(input, len);
        const auto decoded = odh::read_bcon(view);
        const auto required = static_cast<uint32_t>(decoded.constants.size());
        const auto tail = static_cast<uint32_t>(decoded.remaining_bytes);
        if (capacity < required) {
            *count = required;
            *flags = decoded.flags;
            *remaining = tail;
            return ODH_DATA_BUFFER_TOO_SMALL;
        }
        for (uint32_t i = 0; i < required; ++i) words[i] = decoded.constants[i];
        *count = required;
        *flags = decoded.flags;
        *remaining = tail;
        return ODH_DATA_OK;
    } catch (const odh::FormatError&) {
        return ODH_DATA_INVALID_FORMAT;
    } catch (...) {
        return ODH_DATA_INTERNAL_ERROR;
    }
}
