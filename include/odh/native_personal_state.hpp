// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/neighborhood.hpp"
#include "odh/native_personal_catalog.hpp"
#include "odh/world.hpp"
#include "odh/wardrobe.hpp"
#include <array>
#include <map>
#include <set>

namespace odh {
// Existing direct character keys retain their exact bytes. Other admitted
// resources use Resource/v1/<hex portable source>/<hex exact archive entry>.
// This locator is not authority: the host must resolve its exact GUID/OBJD in
// the selected catalog and authenticate the immutable resource fingerprint.
struct NativePersonalResourceLocator {
    std::string source,asset;
    bool character_catalog=false;
};
std::string native_personal_resource_key(const std::string& source,const std::string& asset);
NativePersonalResourceLocator native_personal_resource_locator(const std::string& key);
// This personal-record profile does not change the legacy NINV travel protocol.
// Original executable 6e598c22, writer00425ab3/table0042385a/loader004258ed.
// NBRS container/record versions are deliberately not personal schema versions.
struct NativePersonalIdentity {
    std::int16_t neighbor_id=0;
    std::uint32_t guid=0;
    std::uint16_t definition_chunk_id=0;
    std::string character_asset;
    std::array<std::uint8_t,32> character_fingerprint{};
};
bool same_native_personal_identity(const NativePersonalIdentity&,const NativePersonalIdentity&);
enum class NativePersonalWriteSource : std::uint8_t { generic25=1, retirement=2, family17=3, frontend_save=4 };
struct NativePersonalRecord {
    NativePersonalIdentity identity;
    std::uint64_t revision=0;
    NativePersonalWriteSource writer=NativePersonalWriteSource::generic25;
    NativePersonalProjection projection;
    // Authenticated immutable NBRS bank, including unknown words and absence.
    // It is not an inferred v9 field table, nor a bank to copy into live actors.
    std::optional<std::vector<std::int16_t>> seed_words;
};
struct NativeWardrobeRecord {
    NativePersonalIdentity identity;
    std::uint64_t revision=1;
    OriginalWardrobeProjection wardrobe;
};
struct NativeWardrobeSource {
    NativePersonalIdentity identity;
    OriginalWardrobeSource source;
};
struct NativePersonalSnapshot {
    std::uint32_t version=1;
    std::array<std::uint8_t,32> neighborhood_identity{};
    std::vector<NativePersonalRecord> records; // Strict ascending positive NID.
    std::optional<NativePersonalCatalog> catalog; // Required in v2; optional in explicit v3.
    std::vector<NativeWardrobeRecord> wardrobes; // Independent ascending positive NID, explicit v3 only.
};
void validate_native_personal_snapshot(const NativePersonalSnapshot&);
void validate_native_personal_successor(const NativePersonalSnapshot&,const NativePersonalSnapshot&);
std::vector<std::uint8_t> encode_native_personal_snapshot(const NativePersonalSnapshot&);
NativePersonalSnapshot decode_native_personal_snapshot(const std::vector<std::uint8_t>&);
bool native_personal_v9_field(std::size_t index) noexcept;
// Original serialized schema is NBRS person_mode (record+18), proven by423e3b.
// Record4 reads80 words into a zeroed256-word original allocation; selected
// fields80..82 have known zero backing, unlike an absent schema0 record.
std::optional<NativePersonalProjection> original_neighbor_seed_projection(const NeighborRecord&);
void apply_original_neighbor_personal(const NeighborRecord&,std::vector<std::int16_t>&);
// Preserves absent/unproven fields. Family61 is read separately by constructor.
void apply_native_personal_projection(const NativePersonalProjection&,std::vector<std::int16_t>&);

struct NativePersonalSource {NativePersonalIdentity identity;WorldEntityReference body;};
class NativePersonalState;
class PreparedNativePersonalCommit {
public:
    PreparedNativePersonalCommit()=default;
    const NativePersonalSnapshot& preview_snapshot() const {return candidate_;}
private:
    const NativePersonalState* owner_=nullptr;
    std::uint64_t epoch_=0;
    std::vector<NativePersonalSource> sources_;
    std::vector<std::vector<std::int16_t>> expected_;
    NativePersonalSnapshot candidate_;
    std::map<std::int16_t,NeighborRecord> effective_;
    friend class NativePersonalState;
};
class PreparedNativeWardrobeCommit {
public:
    PreparedNativeWardrobeCommit()=default;
    const NativePersonalSnapshot& preview_snapshot() const {return candidate_;}
private:
    const NativePersonalState* owner_=nullptr;
    std::uint64_t epoch_=0;
    NativePersonalSnapshot candidate_;
    friend class NativePersonalState;
};
// One mutable owner per neighborhood runtime. NeighborContext holds a non-owning
// read view; the host owns lifetime, safe points and durable save transactions.
class NativePersonalState {
public:
    NativePersonalState(const Nbrs&,const std::array<std::uint8_t,32>& neighborhood_identity,
        std::set<std::int16_t> retired_neighbors={},const NativePersonalCatalog* catalog=nullptr);
    NativePersonalState(const NativePersonalState&)=delete;
    NativePersonalState& operator=(const NativePersonalState&)=delete;
    const NeighborRecord* by_id(std::int16_t) const;
    const NativePersonalSnapshot& snapshot() const {return state_;}
    bool changed() const {return !state_.records.empty()||state_.catalog.has_value()||!state_.wardrobes.empty();}
    const NativeWardrobeRecord* wardrobe_by_id(std::int16_t) const;
    PreparedNativeWardrobeCommit prepare_wardrobes(const std::vector<NativeWardrobeSource>&,
        const std::vector<WardrobeCatalogAsset>&) const;
    bool source_current(const PreparedNativeWardrobeCommit&) const;
    // Safe-point memory commit only; durable publication remains existing whole-overlay checkpoint CAS.
    bool commit(PreparedNativeWardrobeCommit&&);
    // Restore is transactional and authenticates all immutable seed words.
    void restore(const NativePersonalSnapshot&);
    PreparedNativePersonalCommit prepare(const World&,const std::vector<NativePersonalSource>&,
        NativePersonalWriteSource) const;
    bool source_current(const PreparedNativePersonalCommit&,const World&) const;
    // Immediate actual Generic25/family17 boundary; no callbacks run inside.
    bool commit(PreparedNativePersonalCommit&&,const World&);
    // Revalidates immediately before the host's actual retirement operation.
    // Publishes only on success with every exact source now unindexed. A failed
    // operation leaves this owner unchanged; host owns World lifecycle failure.
    bool commit_retirement(PreparedNativePersonalCommit&&,World&,const std::function<bool()>&);
private:
    NativePersonalSnapshot state_;
    std::map<std::int16_t,NeighborRecord> seeds_,effective_;
    std::uint64_t epoch_=0;
    std::set<std::int16_t> retired_;
    std::map<std::int16_t,NeighborRecord> materialize(const NativePersonalSnapshot&) const;
    bool publish(PreparedNativePersonalCommit&&);
};
using NativePersonalSourceResolver=std::function<NativePersonalSource(std::int16_t)>;
VmPrimitive native_personal_write_primitive(World&,NativePersonalState*,NativePersonalSourceResolver);
} // namespace odh
