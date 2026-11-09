// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/template_person.hpp"
#include "odh/wardrobe.hpp"
#include <functional>
#include <map>
#include <optional>
#include <utility>

namespace odh {
// A valid request has no admitted outfit. The host may take its authored false
// branch; malformed input, invalid RNG and resource corruption remain errors.
class Ts1CharacterUnavailable : public FormatError {
public:
    using FormatError::FormatError;
};
struct Ts1MakeCharacterInput {
    std::uint8_t avatar_type=0,language=1;
    std::int16_t color=0,age=27,gender=0,pet_outfit_index=0;
    // Exact selected/authenticated catalog name, already in game encoding.
    // This planner does not invent the source random-name/fallback policy.
    std::string source_name;
    // Authenticated receiver resource/live banks, NOT a source identified by a
    // coincidentally equal StackObjectID/World ID. Neither is an implicit seed.
    std::vector<TemplateBodyString> source_body_strings;
    std::vector<std::int16_t> source_person_data; // Legacy host input only, <=256; never read or copied into the new NBRS.
};
struct Ts1MakeCharacterPlan {
    std::uint32_t template_guid=0;
    // GUID, object label and exact template OBJD/BodySTR descriptor are filled
    // by the existing private store/host, not allocated by this planner.
    TemplatePersonRequest character{};
    // Original Make19 registers NBRS person_mode0: the serialized bank is
    // ABSENT. Constructor memory backing256 zeros is not a serialized seed.
    // Kept optional for host API compatibility; this factory leaves it absent.
    std::optional<std::vector<std::int16_t>> person_data;
    bool reset_pet_outfit_index=false;
};
// Bounded appearance-selection planner; never creates a neighbor, World body,
// GUID, file or source/check authority. The admitted assets vector must outlive
// this instance and remain immutable (including its storage and element names).
// Owns only bounded collection indices. Missing-texture filtering mutates the
// selected collection before RNG, matching the pinned shared collection owner.
// Optional personal data is not synthesized from the caller or C# defaults.
// The host binds the proven original schema/registration policy separately.
// Negative-age clone reads the authenticated receiver BodySTR, copies source
// class/sex/skin and the first matching appearance+CLAUSE pairs without RNG.
// Unknown pairs map to source setter -1/empty, never a random fallback.
// Outfit defaults/hands use pinned Simitone as the intermediate base; original
// afbd/complete commit equivalence is not asserted. Host preserves template
// biography (the original copies CTSS name1 only) and authenticates publication.
class Ts1CharacterCollections {
public:
    Ts1CharacterCollections(const std::vector<WardrobeCatalogAsset>&,
        const std::vector<std::uint32_t>& appearance_order,
        const std::vector<std::uint32_t>& texture_order);
    Ts1MakeCharacterPlan prepare(const Ts1MakeCharacterInput&,
        const std::function<std::uint64_t(std::uint64_t)>& next_random);
private:
    const std::vector<WardrobeCatalogAsset>* assets_;
    std::vector<std::pair<std::uint32_t,std::uint32_t>> texture_lookup_; // asset index, original provider rank
    std::map<std::string,std::vector<std::uint32_t>> collections_;
    // Complete admitted collection indices, never pruned by normal skin queries.
    // Clone matches an entire original pair, not a normal FirstPrefix choice.
    std::map<std::string,std::vector<std::uint32_t>> clone_collections_;
    struct Choice {std::uint32_t appearance,texture;};
    std::vector<Choice> filter(const std::string&,const std::string& skin);
    std::optional<std::uint32_t> texture(const std::string&,bool exact=false) const;
    bool appearance(const std::string&) const;
};
} // namespace odh
