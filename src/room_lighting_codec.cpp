// SPDX-License-Identifier: MPL-2.0
#include "odh/room_lighting_codec.hpp"
#include "odh/formats.hpp"
#include <cstring>
#include <limits>
namespace odh { namespace {
static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559,
    "lighting snapshots require IEEE binary32 floats");
struct Writer {
    std::vector<std::uint8_t> data;std::size_t limit;
    void u8(std::uint8_t v){if(data.size()>=limit)throw FormatError("lighting snapshot byte limit exceeded");data.push_back(v);}
    void u16(std::uint16_t v){u8(std::uint8_t(v));u8(std::uint8_t(v>>8));}
    void u32(std::uint32_t v){for(unsigned s=0;s<32;s+=8)u8(std::uint8_t(v>>s));}
    void u64(std::uint64_t v){for(unsigned s=0;s<64;s+=8)u8(std::uint8_t(v>>s));}
    void f32(float v){std::uint32_t bits;std::memcpy(&bits,&v,4);u32(bits);}
};
struct Reader {
    const std::vector<std::uint8_t>& data;std::size_t at=0;
    std::size_t remaining()const{return data.size()-at;}
    std::uint8_t u8(){if(!remaining())throw FormatError("lighting snapshot truncated");return data[at++];}
    bool flag(){const auto v=u8();if(v>1)throw FormatError("lighting snapshot boolean invalid");return v!=0;}
    std::uint16_t u16(){const auto lo=u8();return std::uint16_t(lo|(std::uint16_t(u8())<<8));}
    std::uint32_t u32(){std::uint32_t v=0;for(unsigned s=0;s<32;s+=8)v|=std::uint32_t(u8())<<s;return v;}
    std::uint64_t u64(){std::uint64_t v=0;for(unsigned s=0;s<64;s+=8)v|=std::uint64_t(u8())<<s;return v;}
    std::int16_t i16(){const auto v=u16();return static_cast<std::int16_t>(v<=32767?std::int32_t(v):std::int32_t(v)-65536);}
    std::int32_t i32(){const auto v=u32();return static_cast<std::int32_t>(v<=2147483647U?std::int64_t(v):std::int64_t(v)-4294967296LL);}
    float f32(){const auto bits=u32();float v;std::memcpy(&v,&bits,4);return v;}
    std::uint32_t count(std::size_t& left,std::size_t min_bytes){const auto n=u32();
        if(n>left||n>remaining()/min_bytes)throw FormatError("lighting snapshot collection exceeds payload or limit");left-=n;return n;}
};
void identity(Writer&w,ObjectPositionIdentity v){w.u16(static_cast<std::uint16_t>(v.id));w.u64(v.generation);}
ObjectPositionIdentity identity(Reader&r){const auto id=r.i16();return {id,r.u64()};}
void bounds(Writer&w,RoomLightingBounds v){w.u32(v.x);w.u32(v.y);w.u32(v.width);w.u32(v.height);}
RoomLightingBounds bounds(Reader&r){RoomLightingBounds v;v.x=r.i32();v.y=r.i32();v.width=r.i32();v.height=r.i32();return v;}
}
std::vector<std::uint8_t> encode_room_lighting_snapshot(const RoomLightingSnapshot& state,const RoomLightingCodecLimits& limits){
    validate_room_lighting_snapshot(state,limits.values);Writer w{{},limits.bytes};w.u32(state.version);w.u32(static_cast<std::uint32_t>(state.by_base_room.size()));
    for(const auto& item:state.by_base_room){const auto& room=item.second;w.u16(item.first);w.u16(room.ambient_light);w.u16(room.outside_light);
        w.u16(static_cast<std::uint16_t>(room.room_score));bounds(w,room.bounds);w.u32(static_cast<std::uint32_t>(room.lights.size()));
        for(const auto& light:room.lights){w.f32(light.x);w.f32(light.y);w.f32(light.size);w.f32(light.intensity);w.f32(light.weight);w.f32(light.height);
            w.u16(light.room);w.u8(light.level);w.u8(light.outdoors_color?1:0);for(auto c:light.color)w.u8(c);
            w.u8(light.window_room?1:0);if(light.window_room)w.u16(*light.window_room);
            w.u8(light.component?1:0);if(light.component)identity(w,*light.component);bounds(w,light.bounds);}
        w.u32(static_cast<std::uint32_t>(room.object_footprints.size()));for(const auto& b:room.object_footprints){w.u32(b.x1);w.u32(b.y1);w.u32(b.x2);w.u32(b.y2);}
        w.u32(static_cast<std::uint32_t>(room.components.size()));for(const auto id:room.components)identity(w,id);
    }
    w.u32(static_cast<std::uint32_t>(state.visual_rooms.size()));
    for(const auto& visual:state.visual_rooms){identity(w,visual.owner);w.u16(visual.room);}
    return std::move(w.data);
}
RoomLightingSnapshot decode_room_lighting_snapshot(const std::vector<std::uint8_t>& bytes,const RoomLightingCodecLimits& limits){
    if(bytes.size()>limits.bytes)throw FormatError("lighting snapshot byte limit exceeded");Reader r{bytes};RoomLightingSnapshot state;state.version=r.u32();
    if(state.version!=1)throw FormatError("lighting snapshot version unsupported");
    auto rooms=limits.values.rooms,lights=limits.values.lights,footprints=limits.values.footprints,components=limits.values.components;
    const auto count=r.count(rooms,36);std::optional<std::uint16_t> previous;
    for(std::uint32_t n=0;n<count;++n){RoomLighting room;room.base_room=r.u16();
        if(previous&&room.base_room<=*previous)throw FormatError("lighting snapshot base rooms not ordered and unique");previous=room.base_room;
        room.ambient_light=r.u16();room.outside_light=r.u16();room.room_score=r.i16();room.bounds=bounds(r);
        const auto lc=r.count(lights,50);room.lights.reserve(lc);
        for(std::uint32_t i=0;i<lc;++i){RoomLightSource light;light.x=r.f32();light.y=r.f32();light.size=r.f32();light.intensity=r.f32();light.weight=r.f32();light.height=r.f32();
            light.room=r.u16();light.level=r.u8();light.outdoors_color=r.flag();for(auto& c:light.color)c=r.u8();
            if(r.flag())light.window_room=r.u16();if(r.flag())light.component=identity(r);light.bounds=bounds(r);room.lights.push_back(std::move(light));}
        const auto fc=r.count(footprints,16);room.object_footprints.reserve(fc);
        for(std::uint32_t i=0;i<fc;++i){RoomObstacleRect b;b.x1=r.i32();b.y1=r.i32();b.x2=r.i32();b.y2=r.i32();room.object_footprints.push_back(b);}
        const auto cc=r.count(components,10);room.components.reserve(cc);for(std::uint32_t i=0;i<cc;++i)room.components.push_back(identity(r));
        state.by_base_room.emplace(room.base_room,std::move(room));
    }
    auto visuals=limits.values.entities;const auto vc=r.count(visuals,12);state.visual_rooms.reserve(vc);
    for(std::uint32_t i=0;i<vc;++i){RoomLightingVisualRoom visual;visual.owner=identity(r);visual.room=r.u16();state.visual_rooms.push_back(visual);}
    if(r.remaining())throw FormatError("lighting snapshot trailing bytes");validate_room_lighting_snapshot(state,limits.values);return state;
}
}
