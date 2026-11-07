#pragma once
#include <cstdint>
namespace odh {
struct WorldEntityReference {
    std::int16_t id=0;std::uint64_t generation=0;
    bool operator==(const WorldEntityReference& r)const noexcept{return id==r.id&&generation==r.generation;}
    bool operator!=(const WorldEntityReference& r)const noexcept{return !(*this==r);}
    bool operator<(const WorldEntityReference& r)const noexcept{return id<r.id||(id==r.id&&generation<r.generation);}
};
enum class VmEntityRole {caller,callee,stack_object};
}
