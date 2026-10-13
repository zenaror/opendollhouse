#include "odh/formats.hpp"
namespace odh {
ObjdBinding read_objd_binding(const View& payload) {
    const auto prefix=payload.subview(0,42);
    const auto version=prefix.le32(0);
    if(version!=136 && version!=138 && version!=139 && version!=140 && version!=141 && version!=142)
        throw FormatError("unsupported OBJD binding prefix version");
    const auto sub=prefix.le16(22);
    return {version,prefix.le32(28),prefix.le16(26),prefix.le16(40),prefix.le16(20),
            static_cast<std::int16_t>(sub<32768?static_cast<std::int32_t>(sub):static_cast<std::int32_t>(sub)-65536)};
}
}
