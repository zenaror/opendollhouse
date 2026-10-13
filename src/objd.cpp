#include "odh/formats.hpp"
namespace odh {
ObjdGraphics read_objd_graphics(const View& payload) {
    const auto prefix=payload.subview(0,10);
    const auto version=prefix.le32(0);
    if(version!=136 && version!=138 && version!=139 && version!=140 && version!=141 && version!=142)
        throw FormatError("unsupported OBJD graphics prefix version");
    return {version,prefix.le16(6),prefix.le16(8)};
}
ObjdBinding read_objd_binding(const View& payload) {
    const auto prefix=payload.subview(0,42);
    const auto version=prefix.le32(0);
    if(version!=136 && version!=138 && version!=139 && version!=140 && version!=141 && version!=142)
        throw FormatError("unsupported OBJD binding prefix version");
    const auto sub=prefix.le16(22);
    return {version,prefix.le32(28),prefix.le16(26),prefix.le16(40),prefix.le16(20),
            static_cast<std::int16_t>(sub<32768?static_cast<std::int32_t>(sub):static_cast<std::int32_t>(sub)-65536)};
}
ObjdFunctions read_objd_functions(const View& payload) {
    const auto binding=read_objd_binding(payload);
    const auto prefix=payload.subview(0,68);
    return {binding.version,prefix.le16(10),prefix.le16(64),prefix.le16(66),prefix.le16(44)};
}
}
