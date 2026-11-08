#pragma once
// Internal shared binary BCF section decoders. Each caller owns its Cursor text
// budget; section limits are cumulative within that section collection.
#include "odh/bcf.hpp"
#include "read_cursor.hpp"
namespace odh::detail {
namespace bcf_sections {
inline std::uint32_t bounded(detail::Cursor& c,std::uint32_t& budget,std::uint32_t minimum) {
    const auto count=c.i32();
    if(count<0 || static_cast<std::uint32_t>(count)>budget ||
       static_cast<std::uint32_t>(count)>c.remaining()/minimum)
        throw FormatError("skeleton count exceeds payload or configured budget");
    budget-=static_cast<std::uint32_t>(count);return static_cast<std::uint32_t>(count);
}
inline AnimationProperties pairs(detail::Cursor& c,std::uint32_t& budget) {
    const auto count=bounded(c,budget,2);AnimationProperties result;result.reserve(count);
    for(std::uint32_t i=0;i<count;++i){auto key=c.pascal();auto value=c.pascal();
        result.emplace_back(std::move(key),std::move(value));}
    return result;
}
template<class T>
std::uint32_t mesh_bounded(detail::Cursor& c,std::uint32_t count,
                      std::uint32_t& budget,std::uint64_t stride,
                      std::uint64_t fixed=0) {
    if(count>budget || c.remaining()<fixed ||
       count>(c.remaining()-fixed)/stride ||
       count>std::vector<T>{}.max_size())
        throw FormatError("mesh count exceeds payload or configured allocation budget");
    budget-=count;
    return count;
}
template<class T>
std::uint32_t mesh_signed_count(detail::Cursor& c,std::uint32_t& budget,
                           std::uint64_t stride,std::uint64_t fixed=0) {
    const auto count=c.i32();
    if(count<0)throw FormatError("negative mesh count");
    return mesh_bounded<T>(c,static_cast<std::uint32_t>(count),budget,stride,fixed);
}

} // namespace bcf_sections
inline std::vector<Skeleton> bcf_skeleton_section(Cursor& c,SkeletonLimits limits){
    using namespace bcf_sections;
    std::vector<Skeleton> result;
    const auto count=bounded(c,limits.skeletons,3);result.reserve(count);
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
        result.push_back(std::move(skeleton));
    }
    return result;
}
inline std::vector<Appearance> bcf_appearance_section(Cursor& c,MeshLimits limits){
    using namespace bcf_sections;
    std::vector<Appearance> result;
    const auto count=mesh_signed_count<Appearance>(c,limits.appearances,13,4);
    result.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        Appearance appearance{};
        appearance.name=c.pascal();appearance.type=c.i32();appearance.zero=c.i32();
        const auto raw_bindings=c.u32();
        const auto bindings=mesh_bounded<AppearanceBinding>(c,raw_bindings,
                                                       limits.appearance_bindings,10);
        appearance.bindings.reserve(bindings);
        for(std::uint32_t j=0;j<bindings;++j) {
            AppearanceBinding binding{};
            binding.bone=c.pascal();binding.mesh_name=c.pascal();
            binding.censor_flags=c.i32();binding.zero=c.i32();
            appearance.bindings.push_back(std::move(binding));
        }
        result.push_back(std::move(appearance));
    }
    return result;
}
namespace bcf_animation {
inline std::uint32_t bounded(detail::Cursor& c,std::uint32_t& budget,std::uint32_t minimum){const auto n=c.u32();
    if(n>budget || n>c.remaining()/minimum)throw FormatError("animation count exceeds payload or configured budget");
    budget-=n;return n;}
inline AnimationProperties pairs(detail::Cursor& c,std::uint32_t& budget) {
    const auto n=bounded(c,budget,2);AnimationProperties result;result.reserve(n);
    for(std::uint32_t i=0;i<n;++i){auto key=c.pascal();auto value=c.pascal();result.emplace_back(std::move(key),std::move(value));}return result;
}
}
inline std::vector<AnimationHeader> bcf_animation_section(detail::Cursor& c,AnimationLimits limits) {
    using namespace bcf_animation;
    const auto count=bounded(c,limits.animations,26);std::vector<AnimationHeader> result;result.reserve(count);
    for(std::uint32_t n=0;n<count;++n){AnimationHeader a{};a.name=c.pascal();a.xskill=c.pascal();
        a.duration_bits=c.u32();a.distance_bits=c.u32();a.moving=c.u32();a.translations=c.u32();a.rotations=c.u32();
        if(a.translations>limits.samples || a.rotations>limits.samples)throw FormatError("animation sample count exceeds configured limit");
        const auto motions=bounded(c,limits.motions,33);a.motions.reserve(motions);
        for(std::uint32_t m=0;m<motions;++m){BoneMotion b{};b.bone=c.pascal();b.frames=c.u32();b.duration_bits=c.u32();
            b.has_translation=c.u32();b.has_rotation=c.u32();b.first_translation=c.i32();b.first_rotation=c.i32();
            if(b.has_translation>1 || b.has_rotation>1 || b.frames>limits.samples)throw FormatError("unsupported animation motion flags or frame limit");
            for(const auto active:{true,false}){const auto present=active?b.has_translation:b.has_rotation;
                const auto first=active?b.first_translation:b.first_rotation;const auto total=active?a.translations:a.rotations;
                if(present && (first<0 || static_cast<std::uint32_t>(first)>total || b.frames>total-static_cast<std::uint32_t>(first)))
                    throw FormatError("animation motion sample range outside declared arrays");}
            const auto props=bounded(c,limits.properties,4);b.properties.reserve(props);
            for(std::uint32_t p=0;p<props;++p)b.properties.push_back(pairs(c,limits.properties));
            const auto groups=bounded(c,limits.properties,4);b.time_properties.reserve(groups);
            for(std::uint32_t g=0;g<groups;++g){const auto events=bounded(c,limits.properties,8);std::vector<AnimationTimeProperty> group;group.reserve(events);
                for(std::uint32_t e=0;e<events;++e){const auto id=c.i32();group.push_back({id,pairs(c,limits.properties)});}b.time_properties.push_back(std::move(group));}
            a.motions.push_back(std::move(b));}
        result.push_back(std::move(a));}
    return result;
}
inline BcfResources bcf_resources(Cursor& c,SkeletonLimits skeleton_limits,
        MeshLimits mesh_limits,AnimationLimits animation_limits) {
    BcfResources result;
    result.skeletons=bcf_skeleton_section(c,skeleton_limits);
    result.appearances=bcf_appearance_section(c,mesh_limits);
    result.animations=bcf_animation_section(c,animation_limits);
    result.remaining_bytes=c.remaining();return result;
}
} // namespace odh::detail
