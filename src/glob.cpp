#include "odh/formats.hpp"

namespace odh {

Glob read_glob(const View& view, GlobNameEncoding encoding) {
    switch (encoding) {
    case GlobNameEncoding::pascal_u8: {
        const auto length = view.u8(0);
        const std::uint64_t extent = 1 + static_cast<std::uint64_t>(length);
        if (extent > view.size()) throw FormatError("truncated GLOB name");
        Glob result{std::string(length, '\0'), view.size() - extent};
        view.read(1, result.name.data(), result.name.size());
        return result;
    }
    case GlobNameEncoding::c_string: {
        Glob result{{}, 0};
        for (std::uint64_t offset = 0; offset <= 256 && offset < view.size(); ++offset) {
            const auto byte = view.u8(offset);
            if (byte == 0) {
                result.remaining_bytes = view.size() - offset - 1;
                return result;
            }
            if (offset == 256) throw FormatError("GLOB C string exceeds 256 name bytes");
            result.name.push_back(static_cast<char>(byte));
        }
        throw FormatError("unterminated GLOB C string");
    }
    }
    throw FormatError("unsupported GLOB name encoding");
}

} // namespace odh
