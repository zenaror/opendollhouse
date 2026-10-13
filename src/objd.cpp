#include "odh/formats.hpp"
namespace odh {
ObjdBinding read_objd_binding(const View& payload) {
    const auto prefix=payload.subview(0,42);
    const auto version=prefix.le32(0);
    if(version!=136 && version!=138 && version!=139 && version!=140 && version!=141 && version!=142)
        throw FormatError("unsupported OBJD binding prefix version");
    return {version,prefix.le32(28),prefix.le16(26),prefix.le16(40)};
}
}
