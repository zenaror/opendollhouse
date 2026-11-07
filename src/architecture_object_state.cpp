// SPDX-License-Identifier: MPL-2.0
#include "odh/architecture_object_state.hpp"
#include "odh/room_lighting_codec.hpp"
#include "odh/formats.hpp"
#include <tuple>
#include <algorithm>

namespace odh { namespace {
struct Writer {
    std::vector<std::uint8_t> data;std::size_t limit;
    void u8(std::uint8_t value){if(data.size()>=limit)throw FormatError("architecture object state byte budget exceeded");data.push_back(value);}
    void u16(std::uint16_t value){u8(std::uint8_t(value));u8(std::uint8_t(value>>8));}
    void u32(std::uint32_t value){for(unsigned shift=0;shift<32;shift+=8)u8(std::uint8_t(value>>shift));}
    void u64(std::uint64_t value){for(unsigned shift=0;shift<64;shift+=8)u8(std::uint8_t(value>>shift));}
};
struct Reader {
    const std::vector<std::uint8_t>& data;std::size_t at=0;
    std::uint8_t u8(){if(at==data.size())throw FormatError("architecture object state truncated");return data[at++];}
    std::uint16_t u16(){const auto lo=u8();return std::uint16_t(std::uint16_t(lo)|(std::uint16_t(u8())<<8));}
    std::int16_t i16(){const auto bits=u16();return static_cast<std::int16_t>(bits<=32767?std::int32_t(bits):std::int32_t(bits)-65536);}
    std::uint32_t u32(){std::uint32_t value=0;for(unsigned shift=0;shift<32;shift+=8)value|=std::uint32_t(u8())<<shift;return value;}
    std::uint64_t u64(){std::uint64_t value=0;for(unsigned shift=0;shift<64;shift+=8)value|=std::uint64_t(u8())<<shift;return value;}
    std::size_t remaining()const{return data.size()-at;}
};
constexpr std::size_t face_bytes=45,application_bytes=4;
struct RegistryBudget {
    std::size_t remaining;
    void take(std::size_t size,std::size_t maximum){
        if(size>maximum||size>remaining)throw FormatError("architecture object registry collection budget exceeded");
        remaining-=size;
    }
};
std::size_t registry_bytes(const ObjectPositionRegistrySnapshot& state,const ArchitectureObjectStateLimits& limits){
    RegistryBudget budget{std::min<std::size_t>(limits.registry_entries,65536)};
    budget.take(state.rooms.size(),32768);budget.take(state.tiles.size(),8193);
    std::size_t size=45; // Dimensions, complete, seven counts and footprint instance counter.
    const auto add=[&](std::size_t count,std::size_t bytes,std::size_t maximum){budget.take(count,maximum);size+=count*bytes;};
    for(const auto& room:state.rooms){
        size+=33+(room.room_score?2:0);
        add(room.entities.size(),10,4096);add(room.static_obstacles.size(),34,65536);add(room.dynamic_obstacles.size(),34,65536);
        add(room.portals.size(),12,65536);add(room.window_portals.size(),12,65536);
        for(const auto& rectangles:room.boundaries)add(rectangles.size(),16,65536);
    }
    for(const auto& tile:state.tiles){size+=8;add(tile.entities.size(),10,4096);}
    add(state.deferred_lighting.size(),2,32768);add(state.score_dirty.size(),2,32768);
    add(state.motion.size(),11,4096);
    budget.take(state.footprints.size(),4096);
    for(const auto& binding:state.footprints)size+=36+(binding.room?2:0);
    add(state.retired_owners.size(),10,65536);
    validate_object_position_registry_snapshot(state);return size;
}
void write_identity(Writer& w,ObjectPositionIdentity id){w.u16(static_cast<std::uint16_t>(id.id));w.u64(id.generation);}
ObjectPositionIdentity read_identity(Reader& r){const auto id=r.i16();return {id,r.u64()};}
template<class T>void write_rect(Writer& w,const T& box){
    w.u32(static_cast<std::uint32_t>(box.x1));w.u32(static_cast<std::uint32_t>(box.y1));
    w.u32(static_cast<std::uint32_t>(box.x2));w.u32(static_cast<std::uint32_t>(box.y2));
}
std::int32_t read_i32(Reader& r){const auto value=r.u32();return static_cast<std::int32_t>(value<=2147483647U?std::int64_t(value):std::int64_t(value)-4294967296LL);}
template<class T>T read_rect(Reader& r){T box{};box.x1=read_i32(r);box.y1=read_i32(r);box.x2=read_i32(r);box.y2=read_i32(r);return box;}
void write_registry(Writer& w,const ObjectPositionRegistrySnapshot& state){
    w.u32(state.width);w.u32(state.height);w.u8(state.complete?1:0);w.u32(static_cast<std::uint32_t>(state.rooms.size()));
    for(const auto& room:state.rooms){
        w.u16(room.id);w.u32(static_cast<std::uint32_t>(room.entities.size()));for(const auto id:room.entities)write_identity(w,id);
        for(const auto* obstacles:{&room.static_obstacles,&room.dynamic_obstacles}){
            w.u32(static_cast<std::uint32_t>(obstacles->size()));for(const auto& obstacle:*obstacles){write_identity(w,obstacle.owner);write_rect(w,obstacle.box);w.u64(obstacle.instance);}}
        for(const auto* portals:{&room.portals,&room.window_portals}){
            w.u32(static_cast<std::uint32_t>(portals->size()));for(const auto& portal:*portals){write_identity(w,portal.owner);w.u16(portal.target_room);}}
        for(std::size_t level=0;level<2;++level){w.u8(room.boundaries_known[level]?1:0);w.u32(static_cast<std::uint32_t>(room.boundaries[level].size()));
            for(const auto& rectangle:room.boundaries[level])write_rect(w,rectangle);}
        w.u8(room.room_score?1:0);if(room.room_score)w.u16(static_cast<std::uint16_t>(*room.room_score));
    }
    w.u32(static_cast<std::uint32_t>(state.tiles.size()));for(const auto& tile:state.tiles){
        w.u32(static_cast<std::uint32_t>(tile.offset));w.u32(static_cast<std::uint32_t>(tile.entities.size()));for(const auto id:tile.entities)write_identity(w,id);}
    for(const auto* dirty:{&state.deferred_lighting,&state.score_dirty}){w.u32(static_cast<std::uint32_t>(dirty->size()));for(const auto room:*dirty)w.u16(room);}
    w.u32(static_cast<std::uint32_t>(state.motion.size()));for(const auto& motion:state.motion){write_identity(w,motion.owner);w.u8(motion.moved_self?1:0);}
    w.u64(state.next_footprint_instance);w.u32(static_cast<std::uint32_t>(state.footprints.size()));
    for(const auto& binding:state.footprints){
        write_identity(w,binding.owner);w.u8(binding.present?1:0);w.u8(binding.room?1:0);if(binding.room)w.u16(*binding.room);
        write_rect(w,binding.box);w.u64(binding.instance);
    }
    w.u32(static_cast<std::uint32_t>(state.retired_owners.size()));for(const auto owner:state.retired_owners)write_identity(w,owner);
}
std::uint32_t read_count(Reader& r,RegistryBudget& budget,std::size_t maximum,std::size_t minimum_bytes){
    const auto count=r.u32();budget.take(count,maximum);
    if(count>r.remaining()/minimum_bytes)throw FormatError("architecture object registry collection exceeds payload");return count;
}
bool read_bool(Reader& r){const auto value=r.u8();if(value>1)throw FormatError("architecture object registry boolean invalid");return value!=0;}
ObjectPositionRegistrySnapshot read_registry(Reader& r,const ArchitectureObjectStateLimits& limits){
    ObjectPositionRegistrySnapshot state;state.width=r.u32();state.height=r.u32();state.complete=read_bool(r);
    if(!state.width||state.width!=state.height||state.width>64||state.width>limits.dimension)
        throw FormatError("architecture object registry extent invalid");
    RegistryBudget budget{std::min<std::size_t>(limits.registry_entries,65536)};
    const auto rooms=read_count(r,budget,32768,33);state.rooms.reserve(rooms);
    for(std::uint32_t i=0;i<rooms;++i){
        ObjectPositionRegistryRoom room;room.id=r.u16();
        const auto entities=read_count(r,budget,4096,10);room.entities.reserve(entities);for(std::uint32_t j=0;j<entities;++j)room.entities.push_back(read_identity(r));
        for(auto* obstacles:{&room.static_obstacles,&room.dynamic_obstacles}){
            const auto count=read_count(r,budget,65536,34);obstacles->reserve(count);for(std::uint32_t j=0;j<count;++j){const auto owner=read_identity(r);const auto box=read_rect<CollisionBox>(r);obstacles->push_back({owner,box,r.u64()});}}
        for(auto* portals:{&room.portals,&room.window_portals}){
            const auto count=read_count(r,budget,65536,12);portals->reserve(count);for(std::uint32_t j=0;j<count;++j){const auto owner=read_identity(r);portals->push_back({owner,r.u16()});}}
        for(std::size_t level=0;level<2;++level){room.boundaries_known[level]=read_bool(r);const auto count=read_count(r,budget,65536,16);
            room.boundaries[level].reserve(count);for(std::uint32_t j=0;j<count;++j)room.boundaries[level].push_back(read_rect<RoomObstacleRect>(r));}
        if(read_bool(r))room.room_score=r.i16();state.rooms.push_back(std::move(room));
    }
    const auto tiles=read_count(r,budget,8193,8);state.tiles.reserve(tiles);
    for(std::uint32_t i=0;i<tiles;++i){ObjectPositionRegistryTile tile;tile.offset=read_i32(r);
        const auto count=read_count(r,budget,4096,10);tile.entities.reserve(count);for(std::uint32_t j=0;j<count;++j)tile.entities.push_back(read_identity(r));state.tiles.push_back(std::move(tile));}
    for(auto* dirty:{&state.deferred_lighting,&state.score_dirty}){const auto count=read_count(r,budget,32768,2);dirty->reserve(count);for(std::uint32_t i=0;i<count;++i)dirty->push_back(r.u16());}
    const auto motion=read_count(r,budget,4096,11);state.motion.reserve(motion);
    for(std::uint32_t i=0;i<motion;++i){const auto owner=read_identity(r);state.motion.push_back({owner,read_bool(r)});}
    state.next_footprint_instance=r.u64();const auto bindings=read_count(r,budget,4096,36);state.footprints.reserve(bindings);
    for(std::uint32_t i=0;i<bindings;++i){
        ObjectPositionRegistryFootprintBinding binding;binding.owner=read_identity(r);binding.present=read_bool(r);
        if(read_bool(r))binding.room=r.u16();binding.box=read_rect<CollisionBox>(r);binding.instance=r.u64();state.footprints.push_back(binding);
    }
    const auto retired=read_count(r,budget,65536,10);state.retired_owners.reserve(retired);
    for(std::uint32_t i=0;i<retired;++i)state.retired_owners.push_back(read_identity(r));
    validate_object_position_registry_snapshot(state);return state;
}

void validate_style(const WallAppearanceStyle& style){
    if(style.raw_style>=256)throw FormatError("architecture object style contains transient provider ID");
    const bool dynamic=style.raw_style>21;
    if(dynamic!=bool(style.resource_guid))throw FormatError("architecture object style resource identity missing or extraneous");
    for(unsigned i=0;i<6;++i){
        const auto expected=dynamic?std::uint16_t(std::uint32_t(style.sprites[0])+i):std::uint16_t{0};
        if(style.sprites[i]!=expected)throw FormatError("architecture object style sprite identity is not source-contiguous");
    }
}
}
void validate_architecture_object_state(const ArchitectureObjectState& state,const ArchitectureObjectStateLimits& limits){
    validate_architecture_object_support(state.object_support,limits.dimension);
    const auto cells=std::size_t(state.object_support.width)*state.object_support.height;
    for(const auto& grid:state.occupied_walls){
        if(grid.size()!=cells)throw FormatError("architecture object occupied-wall extent mismatch");
        for(const auto mask:grid)if(mask&0xf0U)throw FormatError("architecture object occupied-wall mask invalid");
    }
    const auto& appearance=state.wall_appearance;
    if(appearance.faces.size()>limits.faces||appearance.faces.size()>16384||
       appearance.applications.size()>limits.applications||appearance.applications.size()>4096)
        throw FormatError("architecture object wall event budget exceeded");
    // Fixed width wire records permit overflow-safe allocation/budget checks.
    const auto registry_size=state.position_registry?registry_bytes(*state.position_registry,limits):0;
    if(state.position_registry&&(state.position_registry->width!=state.object_support.width||state.position_registry->height!=state.object_support.height))
        throw FormatError("architecture object registry/support extent mismatch");
    std::size_t lighting_size=0;
    if(state.lighting){
        if(!state.position_registry)throw FormatError("architecture lighting has no position-registry authority");
        RoomLightingCodecLimits lighting_limits;lighting_limits.bytes=limits.bytes;
        lighting_size=5+encode_room_lighting_snapshot(*state.lighting,lighting_limits).size();
        if(state.lighting_committed)lighting_size+=4+encode_room_lighting_snapshot(*state.lighting_committed,lighting_limits).size();
        for(const auto& entry:state.lighting->by_base_room)
            if(entry.first>=state.position_registry->rooms.size())throw FormatError("architecture lighting room exceeds registry");
        if(state.lighting_committed)for(const auto& entry:state.lighting_committed->by_base_room)
            if(entry.first>=state.position_registry->rooms.size())throw FormatError("committed lighting room exceeds registry");
    }
    else if(state.lighting_committed)throw FormatError("committed lighting has no simulation lighting authority");
    const auto required=std::size_t{23}+cells*4+appearance.faces.size()*face_bytes+appearance.applications.size()*application_bytes+registry_size+lighting_size;
    if(required>limits.bytes)throw FormatError("architecture object state byte budget exceeded");
    using Key=std::tuple<std::uint8_t,std::uint32_t,std::uint32_t,std::uint8_t>;
    std::optional<Key> previous;
    for(const auto& face:appearance.faces){
        const Key key{face.level,face.y,face.x,face.segment};
        if(face.level<1||face.level>2||face.x>=state.object_support.width||face.y>=state.object_support.height||
           (face.segment!=wall_top_left&&face.segment!=wall_top_right)||(previous&&key<=*previous))
            throw FormatError("architecture object wall face extent/order invalid");
        if(face.owner<=0||!face.generation||face.door_owner<0||
           (bool(face.door_owner)!=bool(face.door_generation))||(face.door&&!face.door_owner))
            throw FormatError("architecture object wall face owner invalid");
        validate_style(face.style);previous=key;
    }
    for(const auto& application:appearance.applications){
        if(application.id<=0||(application.applied_sides&~9U)||(application.ignored_sides&~6U))
            throw FormatError("architecture object wall application invalid");
    }
}
std::vector<std::uint8_t> encode_architecture_object_state(const ArchitectureObjectState& state,const ArchitectureObjectStateLimits& limits){
    validate_architecture_object_state(state,limits);Writer w{{},limits.bytes};
    w.u16(state.lighting?2:1);w.u32(state.object_support.width);w.u32(state.object_support.height);
    for(const auto& grid:state.object_support.levels)for(const auto value:grid)w.u8(value);
    for(const auto& grid:state.occupied_walls)for(const auto value:grid)w.u8(value);
    const auto& appearance=state.wall_appearance;w.u32(static_cast<std::uint32_t>(appearance.faces.size()));
    for(const auto& face:appearance.faces){
        w.u16(static_cast<std::uint16_t>(face.x));w.u16(static_cast<std::uint16_t>(face.y));w.u8(face.level);w.u8(face.segment);
        w.u16(face.style.raw_style);w.u32(face.style.resource_guid);for(const auto sprite:face.style.sprites)w.u16(sprite);
        w.u16(static_cast<std::uint16_t>(face.owner));w.u16(static_cast<std::uint16_t>(face.door_owner));
        w.u64(face.generation);w.u64(face.door_generation);w.u8((face.door?1U:0U)|(face.segment_present?2U:0U));
    }
    w.u32(static_cast<std::uint32_t>(appearance.applications.size()));
    for(const auto& application:appearance.applications){w.u16(static_cast<std::uint16_t>(application.id));w.u8(application.applied_sides);w.u8(application.ignored_sides);}
    w.u32(appearance.overwritten_faces);w.u8(state.position_registry?1:0);
    if(state.position_registry)write_registry(w,*state.position_registry);
    if(state.lighting){RoomLightingCodecLimits lighting_limits;lighting_limits.bytes=limits.bytes;
        const auto bytes=encode_room_lighting_snapshot(*state.lighting,lighting_limits);
        w.u32(static_cast<std::uint32_t>(bytes.size()));for(const auto byte:bytes)w.u8(byte);
        w.u8(state.lighting_committed?1:0);
        if(state.lighting_committed){const auto visual=encode_room_lighting_snapshot(*state.lighting_committed,lighting_limits);
            w.u32(static_cast<std::uint32_t>(visual.size()));for(const auto byte:visual)w.u8(byte);}}
    return std::move(w.data);
}
ArchitectureObjectState decode_architecture_object_state(const std::vector<std::uint8_t>& bytes,const ArchitectureObjectStateLimits& limits){
    if(bytes.size()>limits.bytes)throw FormatError("architecture object state byte budget exceeded");
    Reader r{bytes};const auto version=r.u16();if(version!=1&&version!=2)throw FormatError("architecture object state version unsupported");
    ArchitectureObjectState state;state.object_support.width=r.u32();state.object_support.height=r.u32();
    const auto width=state.object_support.width,height=state.object_support.height;
    if(!width||width!=height||width>64||width>limits.dimension)throw FormatError("architecture object state extent invalid");
    const auto cells=std::size_t(width)*height;
    if(r.remaining()<cells*4+13)throw FormatError("architecture object state grid payload truncated");
    for(auto& grid:state.object_support.levels){grid.resize(cells);for(auto& value:grid)value=r.u8();}
    for(auto& grid:state.occupied_walls){grid.resize(cells);for(auto& value:grid)value=r.u8();}
    const auto faces=r.u32();
    if(faces>limits.faces||faces>16384||r.remaining()<9||std::size_t(faces)>(r.remaining()-9)/face_bytes)
        throw FormatError("architecture object state face allocation exceeds payload/budget");
    state.wall_appearance.faces.reserve(faces);
    for(std::uint32_t i=0;i<faces;++i){
        WallAppearanceFace face{};face.x=r.u16();face.y=r.u16();face.level=r.u8();face.segment=r.u8();
        face.style.raw_style=r.u16();face.style.resource_guid=r.u32();for(auto& sprite:face.style.sprites)sprite=r.u16();
        face.owner=r.i16();face.door_owner=r.i16();face.generation=r.u64();face.door_generation=r.u64();
        const auto flags=r.u8();if(flags>3)throw FormatError("architecture object wall face flags invalid");face.door=(flags&1)!=0;face.segment_present=(flags&2)!=0;
        state.wall_appearance.faces.push_back(face);
    }
    const auto applications=r.u32();
    if(applications>limits.applications||applications>4096||r.remaining()<5||std::size_t(applications)>(r.remaining()-5)/application_bytes)
        throw FormatError("architecture object state application allocation exceeds payload/budget");
    state.wall_appearance.applications.reserve(applications);
    for(std::uint32_t i=0;i<applications;++i){WallAppearanceApplication application{};application.id=r.i16();application.applied_sides=r.u8();application.ignored_sides=r.u8();state.wall_appearance.applications.push_back(application);}
    state.wall_appearance.overwritten_faces=r.u32();if(read_bool(r))state.position_registry=read_registry(r,limits);
    if(version==2){const auto length=r.u32();if(length>r.remaining())throw FormatError("architecture lighting payload truncated");
        std::vector<std::uint8_t> lighting(bytes.begin()+r.at,bytes.begin()+r.at+length);r.at+=length;
        RoomLightingCodecLimits lighting_limits;lighting_limits.bytes=limits.bytes;
        state.lighting=decode_room_lighting_snapshot(lighting,lighting_limits);
        if(read_bool(r)){const auto visual_length=r.u32();if(visual_length>r.remaining())throw FormatError("committed lighting payload truncated");
            std::vector<std::uint8_t> visual(bytes.begin()+r.at,bytes.begin()+r.at+visual_length);r.at+=visual_length;
            state.lighting_committed=decode_room_lighting_snapshot(visual,lighting_limits);}}
    if(r.remaining())throw FormatError("architecture object state trailing bytes");
    validate_architecture_object_state(state,limits);return state;
}
}
