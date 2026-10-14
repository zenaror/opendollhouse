#include "odh/saved_architecture.hpp"
#include "odh/formats.hpp"
#include <stdexcept>

namespace odh {
namespace {
void add(std::size_t& used,std::size_t n,std::size_t limit) {
    if(n>limit-used) throw FormatError("saved architecture retained byte limit");
    used+=n;
}
const ArchitectureArray* find(const std::vector<SavedArchitectureArray>& a,std::uint16_t id) {
    for(const auto& r:a) if(r.id==id) return &r.array;
    return nullptr;
}
int required_stride(std::uint16_t id) {
    switch(id) {
    case 0:return 4;
    case 1:case 101:case 6:case 7:case 8:case 108:case 9:case 10:return 1;
    case 3:case 103:case 11:case 111:return 2;
    case 2:case 102:return 8;
    case 12:case 112:return 14;
    default:return 0;
    }
}
std::size_t source(const ArchitectureArray& a,std::size_t i) {
    const auto w=static_cast<std::size_t>(a.width), h=static_cast<std::size_t>(a.height);
    return ((i%w)*h+i/w)*static_cast<std::size_t>(a.stride);
}
std::uint16_t word(const std::vector<std::uint8_t>& b,std::size_t p) {
    return static_cast<std::uint16_t>(b[p]|(std::uint16_t(b[p+1])<<8));
}
std::vector<std::uint8_t> bytes(const ArchitectureArray& a,std::size_t n) {
    std::vector<std::uint8_t> out(n);
    for(std::size_t i=0;i<n;++i) out[i]=a.data[source(a,i)];
    return out;
}
std::vector<std::uint16_t> words(const ArchitectureArray& a,std::size_t n) {
    std::vector<std::uint16_t> out(n);
    for(std::size_t i=0;i<n;++i) {
        const auto p=source(a,i);
        out[i]=a.stride==1 ? a.data[p] : word(a.data,p);
    }
    return out;
}
}
SavedArchitecture interpret_saved_architecture(const std::vector<SavedArchitectureArray>& arrays,
                                               const SavedArchitectureLimits& limits) {
    if(arrays.empty()||arrays.size()>limits.arrays) throw FormatError("saved architecture array count");
    const auto w=arrays.front().array.width,h=arrays.front().array.height;
    if(w<=0||h<=0||static_cast<std::uint32_t>(w)>limits.dimension||
       static_cast<std::uint32_t>(h)>limits.dimension) throw FormatError("saved architecture dimensions");
    const auto cells64=std::uint64_t(w)*std::uint64_t(h);
    // All output sizing arithmetic stays bounded by the retained-byte limit.
    if(cells64>limits.retained_bytes/sizeof(SavedWallTile)) throw FormatError("saved architecture cell budget");
    const auto n=static_cast<std::size_t>(cells64);
    std::size_t used=0;
    for(std::size_t i=0;i<arrays.size();++i) {
        const auto& r=arrays[i];const auto& a=r.array;
        for(std::size_t j=0;j<i;++j) if(arrays[j].id==r.id) throw FormatError("duplicate saved architecture array ID");
        if(a.width!=w||a.height!=h||a.stride<=0||a.stride>16||
           cells64>std::uint64_t(a.data.size())/std::uint64_t(a.stride)||
           std::uint64_t(a.data.size())!=cells64*std::uint64_t(a.stride))
            throw FormatError("saved architecture array extent");
        const auto expected=required_stride(r.id);
        if(expected&&a.stride!=expected) throw FormatError("saved architecture typed stride");
        add(used,sizeof(SavedArchitectureArray),limits.retained_bytes);
        add(used,a.data.size(),limits.retained_bytes);
        add(used,a.encoded.size(),limits.retained_bytes);
        add(used,a.trailing_bytes.size(),limits.retained_bytes);
    }
    const bool advanced=find(arrays,11)!=nullptr;
    const std::array<std::uint16_t,2> floor_ids=advanced?std::array<std::uint16_t,2>{11,111}:std::array<std::uint16_t,2>{1,101};
    const std::array<std::uint16_t,2> wall_ids=advanced?std::array<std::uint16_t,2>{12,112}:std::array<std::uint16_t,2>{2,102};
    for(std::size_t l=0;l<2;++l) {
        if(!find(arrays,floor_ids[l])||!find(arrays,wall_ids[l])) throw FormatError("saved architecture required grid missing");
        add(used,n*sizeof(std::uint16_t),limits.retained_bytes);
        add(used,n*sizeof(SavedWallTile),limits.retained_bytes);
    }
    // Auxiliary grids are optional, but present grids are exact typed extents.
    for(auto id:{0,3,103,6,7,8,108,9,10}) if(find(arrays,static_cast<std::uint16_t>(id)))
        add(used,n*static_cast<std::size_t>(required_stride(static_cast<std::uint16_t>(id))),limits.retained_bytes);
    SavedArchitecture out;out.width=static_cast<std::uint32_t>(w);out.height=static_cast<std::uint32_t>(h);out.advanced=advanced;
    for(std::size_t l=0;l<2;++l) {
        auto& level=out.levels[l];
        level.floors=words(*find(arrays,floor_ids[l]),n);
        const auto& a=*find(arrays,wall_ids[l]);level.walls.resize(n);
        for(std::size_t i=0;i<n;++i) {
            const auto p=source(a,i);auto& t=level.walls[i];t.segments=a.data[p];t.unknown=a.data[p+1];
            std::array<std::uint16_t,6> fields{};
            for(std::size_t f=0;f<6;++f) fields[f]=advanced?word(a.data,p+2+f*2):a.data[p+2+f];
            t.top_left_style=fields[0];t.top_right_style=fields[1];
            t.top_left_pattern=fields[2];t.top_right_pattern=fields[3];
            t.bottom_left_pattern=fields[4];t.bottom_right_pattern=fields[5];
        }
        if(const auto* ids=find(arrays,l==0?3:103))level.object_ids=words(*ids,n);
        if(const auto* flags=find(arrays,l==0?8:108))level.flags=bytes(*flags,n);
    }
    if(const auto* a=find(arrays,0)) {
        out.altitude=std::vector<std::array<std::uint8_t,4>>(n);
        for(std::size_t i=0;i<n;++i)for(std::size_t j=0;j<4;++j)(*out.altitude)[i][j]=a->data[source(*a,i)+j];
    }
    if(const auto* a=find(arrays,6))out.grass=bytes(*a,n);
    if(const auto* a=find(arrays,7))out.target_grass=bytes(*a,n);
    if(const auto* a=find(arrays,9))out.pools=bytes(*a,n);
    if(const auto* a=find(arrays,10))out.water=bytes(*a,n);
    out.records=arrays;
    return out;
}
}
