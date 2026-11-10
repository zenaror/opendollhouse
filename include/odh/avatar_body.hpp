#pragma once
#include "odh/animation.hpp"
#include "odh/random.hpp"
#include "odh/world.hpp"
#include <string_view>

namespace odh {
inline constexpr std::size_t avatar_body_text_bytes=65536;
inline constexpr std::size_t avatar_body_names=256;
enum class AvatarPhenotype { ts1_light };
enum class AvatarBodyType { adult,child,cat,dog };
// Source BodySTR type comparisons are case-sensitive. Cat and kat select the
// same avatar type, but the literal string still selects its skeleton name.
inline std::optional<AvatarBodyType> avatar_body_type(std::string_view type) noexcept {
    if(type=="adult")return AvatarBodyType::adult;
    if(type=="child")return AvatarBodyType::child;
    if(type=="cat"||type=="kat")return AvatarBodyType::cat;
    if(type=="dog")return AvatarBodyType::dog;
    return {};
}
inline bool human_avatar_body_type(AvatarBodyType type) noexcept {
    return type==AvatarBodyType::adult||type==AvatarBodyType::child;
}
struct AvatarBodyStrings {
    std::optional<std::int16_t> age, gender;
    std::string avatar_type, body, head;
    // STR# indices 17..22: left/right idle, pointing, fist.
    std::array<std::string,6> hands;
    std::string raw_names;
    std::vector<std::string> names;
};
// Apply only live outfit state; original BodySTR remains immutable for Daywear.
AvatarBodyStrings effective_avatar_body(const AvatarBodyStrings&,const Entity&);
std::vector<std::string> effective_avatar_accessories(const Entity&);
// Bounded US group only (language 0/1); present body STR# requires indices
// 0..22, at most 50 strings and 64 KiB US value bytes, names at most 256.
// Body keeps the first semicolon segment; head/hands stay unsplit.
// No appearance choice RNG, mesh, texture, skeleton or outfit resolution.
AvatarBodyStrings parse_avatar_body_strings(const StringTable&);
struct AvatarBodyDescriptor {
    std::optional<AvatarBodyStrings> body;
    std::optional<std::string> selected_name;
    std::optional<AvatarPhenotype> phenotype;
    std::array<std::string,50> walk, swim;
    bool child;
    std::uint16_t global_walk_id, global_swim_id;
    std::optional<AvatarBodyType> body_type;
};
// Post-Init TS1 profile with DisableAvatarCollision=false. Caller explicitly
// supplies global STR#150/158 for age>=18, STR#151/160 otherwise. Object STR#150
// nonempty entries override walking only. Table identity is not inferred.
// Absent body preserves age/gender and produces no name/outfit/phenotype update.
// Rejects invalid tables/avatar banks before any World or RNG change.
AvatarBodyDescriptor derive_avatar_body_descriptor(const World&,std::int16_t actor,
    const RandomState&,const std::optional<AvatarBodyStrings>& body,
    const StringTable& global_walk,const StringTable& global_swim,
    const std::optional<StringTable>& object_walk={},bool use_live_age=false);
AvatarBodyDescriptor apply_avatar_body(World&,std::int16_t actor,RandomState&,
    const std::optional<AvatarBodyStrings>& body,const StringTable& global_walk,
    const StringTable& global_swim,const std::optional<StringTable>& object_walk={});
}
