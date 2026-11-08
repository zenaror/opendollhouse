// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/formats.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace odh {
inline constexpr std::size_t wardrobe_max_source_entries=256;
inline constexpr std::size_t wardrobe_max_string_bytes=4096;
inline constexpr std::size_t wardrobe_max_source_bytes=64*1024;
inline constexpr std::size_t wardrobe_max_name_bytes=256;
inline constexpr std::size_t wardrobe_max_locator_bytes=1024;
inline constexpr std::size_t wardrobe_max_catalog_entries=32768;
inline constexpr std::size_t wardrobe_max_catalog_bytes=16*1024*1024;
using WardrobeDigest=std::array<std::uint8_t,32>;
enum class WardrobeAssetKind : std::uint8_t {appearance=1,texture=2};
// Input is owned and ordered by the actual admitted native resource provider.
// Core never opens files, guesses roots or sorts a filesystem itself.
struct WardrobeCatalogAsset {
    WardrobeAssetKind kind=WardrobeAssetKind::appearance;
    // Name is CMX internal appearance member or renderer texture key/stem,
    // without .cmx/.apr/.bmp/.tga; locator pins the actual containing bytes.
    std::string name,source,asset;
    WardrobeDigest fingerprint{};
    std::uint64_t producer_ordinal=0;
};
struct OriginalWardrobeSource {
    std::uint16_t body_str_id=0;
    std::vector<std::string> strings; // Exact default-language source; bounded, owned.
};
struct WardrobeChoice {
    std::string literal;
    WardrobeCatalogAsset appearance,texture;
};
struct OriginalWardrobeProjection {
    std::uint32_t rule_profile=1; // Original6e598 upgrade, compiled rule provenance only.
    std::uint16_t body_str_id=0;
    // Actual BodySTR1/12/13/14 source metadata, not mutable PD substitutes.
    std::array<std::optional<std::string>,4> source_fields;
    std::array<std::optional<std::string>,5> seed,strings; // Slots30..34, absent != empty.
    std::array<std::optional<WardrobeChoice>,5> chosen;
};
// Pure atomic preview. Unsupported source/query/catalog authority throws;
// never replaces a missing winter outfit with daywear/None and never draws RNG.
OriginalWardrobeProjection prepare_original_wardrobe_upgrade(const OriginalWardrobeSource&,
    const std::vector<WardrobeCatalogAsset>& ordered_catalog);
void validate_original_wardrobe_projection(const OriginalWardrobeProjection&);
// Cold/source admission: compare exact pristine STR seed/metadata and recheck
// every selected asset locator/content. Existing accepted choices remain stable
// if an unrelated CC asset is added or provider enumeration order changes.
void validate_original_wardrobe_source(const OriginalWardrobeProjection&,const OriginalWardrobeSource&);
using WardrobeAssetResolver=std::function<std::optional<WardrobeCatalogAsset>(const WardrobeCatalogAsset&)>;
void validate_original_wardrobe_assets(const OriginalWardrobeProjection&,const WardrobeAssetResolver&);
} // namespace odh
