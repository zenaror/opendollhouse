#include "odh/wall_appearance.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
namespace odh {
ObjdWallAppearance read_objd_wall_appearance(const View& v){
    const auto version=v.le32(0);if(version!=136&&version!=138)throw FormatError("unsupported OBJD wall appearance profile");
    const std::uint64_t extent=version==138?164:94;const auto prefix=v.subview(0,extent);
    return {version,prefix.le32(28),prefix.le16(92),version==138?std::optional<std::uint16_t>(prefix.le16(162)):std::nullopt,extent,v.size()-extent};
}
WallAppearance derive_wall_appearance(const SavedArchitecture& a,const std::vector<WallAppearanceInput>& inputs,std::size_t limit){
    if(a.width==0||a.height==0||a.width>64||a.height>64||inputs.size()>limit||inputs.size()>4096)throw FormatError("wall appearance input/extent limit");
    const auto cells=std::size_t(a.width)*a.height;
    for(const auto& level:a.levels)if(level.walls.size()!=cells)throw FormatError("wall appearance wall grid extent mismatch");
    std::set<std::int16_t> ids;for(const auto& in:inputs){
        if(in.id<=0||!in.generation||!in.guid||!ids.insert(in.id).second||(in.kind!=EntityKind::object&&in.kind!=EntityKind::avatar))throw FormatError("invalid/duplicate wall appearance identity");}
    using Key=std::tuple<std::uint8_t,std::uint32_t,std::uint32_t,std::uint8_t>;
    std::map<Key,WallAppearanceFace> faces;WallAppearance result;
    auto tile=[](std::int32_t value){auto q=value/16;if(value%16<0)--q;return q;};
    for(const auto& in:inputs){
        if(in.kind!=EntityKind::object||in.contained||!in.position||!(in.flags2&0xc000U))continue;
        if(!in.binding||in.binding->guid!=in.guid||(in.binding->version!=136&&in.binding->version!=138))throw FormatError("architectural object binding missing/mismatched");
        const auto& pos=*in.position;const auto x=tile(pos.x),y=tile(pos.y);
        if(pos.direction>7||pos.level<1||pos.level>2||x<0||y<0||std::uint32_t(x)>=a.width||std::uint32_t(y)>=a.height)throw FormatError("architectural object position outside profile");
        const auto& b=*in.binding;WallAppearanceStyle style{b.wall_style,0,{}};
        if((b.version==138)!=b.sprite_base.has_value())throw FormatError("wall appearance sprite-base presence violates schema");
        if(b.wall_style>21&&b.wall_style<256){
            if(!b.sprite_base)throw FormatError("dynamic architectural style has no sprite-base field");
            style.resource_guid=in.guid;for(unsigned i=0;i<6;++i)style.sprites[i]=static_cast<std::uint16_t>(std::uint32_t(*b.sprite_base)+i);
        }else if(b.wall_style>=256)throw FormatError("preallocated dynamic wall style ID unsupported");
        WallAppearanceApplication application{in.id};const unsigned direction=pos.direction%2?0:pos.direction/2;
        for(unsigned relative=0;relative<4;++relative){if(!(in.placement&(1U<<relative)))continue;const unsigned side=(direction+relative)%4;
            if(side==1||side==2){application.ignored_sides|=static_cast<std::uint8_t>(1U<<side);continue;}
            const auto segment=static_cast<std::uint8_t>(side==0?wall_top_right:wall_top_left);const auto level=static_cast<std::uint8_t>(pos.level);
            Key key{level,static_cast<std::uint32_t>(y),static_cast<std::uint32_t>(x),segment};auto previous=faces.find(key);const bool exists=previous!=faces.end();
            const bool is_door=(in.flags2&0x8000U)!=0;
            const bool door=is_door?b.wall_style!=0:exists&&previous->second.door;
            const auto door_owner=is_door?in.id:exists?previous->second.door_owner:std::int16_t{0};
            const auto door_generation=is_door?in.generation:exists?previous->second.door_generation:std::uint64_t{0};
            const auto present=(a.levels[static_cast<std::size_t>(level-1)].walls[std::size_t(y)*a.width+static_cast<std::size_t>(x)].segments&segment)!=0;
            WallAppearanceFace face{static_cast<std::uint32_t>(x),static_cast<std::uint32_t>(y),level,segment,style,in.id,door_owner,in.generation,door_generation,door,present};
            if(exists){++result.overwritten_faces;previous->second=face;}else faces.emplace(key,face);
            application.applied_sides|=static_cast<std::uint8_t>(1U<<side);
        }
        result.applications.push_back(application);
    }
    result.faces.reserve(faces.size());for(const auto& pair:faces)result.faces.push_back(pair.second);return result;
}
WallAppearance derive_wall_appearance_restore_order(const SavedArchitecture& a,
    const std::vector<WallAppearanceInput>& inputs,std::size_t limit){
    if(inputs.size()>limit||inputs.size()>4096)throw FormatError("wall appearance input/extent limit");
    auto ordered=inputs;
    std::sort(ordered.begin(),ordered.end(),[](const auto& lhs,const auto& rhs){return lhs.id<rhs.id;});
    return derive_wall_appearance(a,ordered,limit);
}
}
