#include "odh/skeleton.hpp"
#include "read_cursor.hpp"
namespace odh {
namespace {
std::uint32_t bounded(detail::Cursor& c,std::uint32_t& budget,std::uint32_t minimum) {
    const auto count=c.i32();
    if(count<0 || static_cast<std::uint32_t>(count)>budget ||
       static_cast<std::uint32_t>(count)>c.remaining()/minimum)
        throw FormatError("skeleton count exceeds payload or configured budget");
    budget-=static_cast<std::uint32_t>(count);return static_cast<std::uint32_t>(count);
}
AnimationProperties pairs(detail::Cursor& c,std::uint32_t& budget) {
    const auto count=bounded(c,budget,2);AnimationProperties result;result.reserve(count);
    for(std::uint32_t i=0;i<count;++i){auto key=c.pascal();auto value=c.pascal();
        result.emplace_back(std::move(key),std::move(value));}
    return result;
}
}
SkeletonCollection read_bcf_skeletons(const View& view,SkeletonLimits limits) {
    detail::Cursor c(view,limits.text_bytes);SkeletonCollection result{{},0};
    const auto count=bounded(c,limits.skeletons,3);result.skeletons.reserve(count);
    for(std::uint32_t n=0;n<count;++n){Skeleton skeleton{};skeleton.name=c.pascal();
        const auto raw=c.u16();
        if(raw>=0x8000U)throw FormatError("negative skeleton bone count");
        const auto bones=static_cast<std::uint32_t>(raw);
        // Each decoded bone has a nonempty name, two Pascal prefixes, a group
        // count and 48 bytes of transform/flag/wiggle fields. Empty records are
        // additional records and do not advance this decoded bone count.
        if(bones>limits.bones || bones>c.remaining()/55)
            throw FormatError("skeleton bone count exceeds payload or configured budget");
        skeleton.bones.reserve(bones);
        while(skeleton.bones.size()<bones){SkeletonBone bone{};bone.name=c.pascal();bone.parent=c.pascal();
            if(bone.name.empty()){
                if(limits.skipped_records==0)throw FormatError("skeleton skipped record budget exceeded");
                --limits.skipped_records;continue;
            }
            if(limits.bones==0)throw FormatError("skeleton bone budget exceeded");
            --limits.bones;
            const auto groups=bounded(c,limits.properties,4);bone.properties.reserve(groups);
            for(std::uint32_t p=0;p<groups;++p)bone.properties.push_back(pairs(c,limits.properties));
            for(auto& bits:bone.translation_bits)bits=c.u32();
            for(auto& bits:bone.rotation_bits)bits=c.u32();
            bone.can_translate=c.i32();bone.can_rotate=c.i32();bone.can_blend=c.i32();
            bone.wiggle_value_bits=c.u32();bone.wiggle_power_bits=c.u32();
            skeleton.bones.push_back(std::move(bone));
        }
        result.skeletons.push_back(std::move(skeleton));
    }
    if(c.i32()!=0)throw FormatError("BCF appearance section outside skeleton-only profile");
    if(c.i32()!=0)throw FormatError("BCF animation section outside skeleton-only profile");
    result.remaining_bytes=c.remaining();return result;
}
}
