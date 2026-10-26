#include "odh/household_transfer.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace odh {
namespace {
bool zero(const std::array<std::uint8_t, 32>& hash) {
    return std::all_of(hash.begin(), hash.end(), [](auto byte) { return byte == 0; });
}
bool same_lease(const SessionFamilyLease& a, const SessionFamilyLease& b) {
    return a.neighborhood_identity == b.neighborhood_identity && a.family_identity == b.family_identity &&
        a.family_chunk_id == b.family_chunk_id && a.occupancy_epoch == b.occupancy_epoch;
}
bool same_identity(const HouseholdTransferIdentity& a, const HouseholdTransferIdentity& b) {
    return a.guid == b.guid && a.neighbor_id == b.neighbor_id && a.object_definition_id == b.object_definition_id &&
        a.character_asset == b.character_asset && a.character_fingerprint == b.character_fingerprint;
}
std::map<std::uint32_t, HouseholdTransferIdentity> identities(const std::vector<HouseholdTransferIdentity>& roster) {
    if (roster.empty() || roster.size() > 8) throw FormatError("household transfer requires a complete 1..8-member roster");
    std::map<std::uint32_t, HouseholdTransferIdentity> result;
    std::set<std::int16_t> neighbors;
    for (const auto& member : roster) {
        const auto split = member.character_asset.find_last_of('/');
        const auto prefix = split == std::string::npos ? std::string{} : member.character_asset.substr(0, split + 1);
        const auto name = split == std::string::npos ? std::string{} : member.character_asset.substr(split + 1);
        if (!member.guid || member.neighbor_id <= 0 || zero(member.character_fingerprint) ||
            (prefix != "PlayerData/Characters/" && prefix != "UserData/Characters/") ||
            name.empty() || name == "." || name == ".." || (name.find('\\') != std::string::npos || name.find(':') != std::string::npos || name.find('\0') != std::string::npos) ||
            !neighbors.insert(member.neighbor_id).second || !result.emplace(member.guid, member).second)
            throw FormatError("invalid or duplicate household transfer resource identity");
    }
    return result;
}
HouseholdTransferIdentity identity(const SessionHouseholdMember& member) {
    return {member.guid, member.neighbor_id, member.object_definition_id, member.character_asset, member.character_fingerprint};
}
void unsupported_source(const HouseholdTransferMember& member) {
    for (unsigned index : {68U, 84U, 85U, 86U})
        if (member.person_data[index] != 0)
            throw FormatError("household transfer unsupported PersonData[" + std::to_string(index) + "] transformation");
    if (!member.bound_appearances.empty()) throw FormatError("household transfer cannot carry bound appearance props");
    for (unsigned index : {5U, 6U, 7U, 8U, 9U, 14U, 15U})
        if (member.motives[index] < -100 || member.motives[index] > 100)
            throw FormatError("household transfer need motive is outside supported range");
}
void validate_relationships(const RelationshipSnapshot& snapshot) {
    RelationshipState state;
    if (!state.restore_snapshot(snapshot)) throw FormatError("household transfer relationship snapshot is invalid or exceeds budgets");
}
void recount(RelationshipSnapshot& snapshot) {
    snapshot.pair_count = snapshot.value_count = 0;
    for (const auto& source : snapshot.sources) {
        if (source.pairs.size() > snapshot.limits.pairs - snapshot.pair_count)
            throw FormatError("household transfer relationship pair budget exceeded");
        snapshot.pair_count += source.pairs.size();
        for (const auto& pair : source.pairs) {
            if (pair.values.size() > snapshot.limits.values - snapshot.value_count)
                throw FormatError("household transfer relationship value budget exceeded");
            snapshot.value_count += pair.values.size();
        }
    }
}
std::int16_t signed_family(std::uint16_t family) {
    return static_cast<std::int16_t>(family < 32768 ? static_cast<std::int32_t>(family) : static_cast<std::int32_t>(family) - 65536);
}
}

PreparedHouseholdTransfer prepare_household_transfer(const std::vector<std::uint8_t>& encoded,
        const HouseholdTransferTicket& ticket, const std::vector<HouseholdTransferIdentity>& expected_roster) {
    if (encoded.size() > SessionArchiveLimits{}.max_file_bytes)
        throw FormatError("household transfer archive exceeds bounded codec size");
    const auto expected = identities(expected_roster);
    if (ticket.source_house == 0 || ticket.source_house > 98 || ticket.checkpoint_budget < 0 ||
        ticket.family_number < 0 || !ticket.source_lease.occupancy_epoch || zero(ticket.source_lease.neighborhood_identity) ||
        zero(ticket.source_lease.family_identity) || zero(ticket.archive_sha256) ||
        sha256(encoded.data(), encoded.size()) != ticket.archive_sha256)
        throw FormatError("household transfer ticket or archive digest is invalid");
    const auto bundle = decode_session_bundle(encoded);
    if (!bundle.family_lease || !same_lease(*bundle.family_lease, ticket.source_lease) ||
        bundle.host.house_number != ticket.source_house || bundle.host.family_chunk_id != ticket.source_lease.family_chunk_id ||
        bundle.host.family_number != ticket.family_number || bundle.host.family_budget != ticket.checkpoint_budget ||
        bundle.household_roster.size() != expected.size() || !bundle.simulation)
        throw FormatError("household transfer archive differs from ticket, roster or required SIMS profile");
    PreparedHouseholdTransfer result;
    result.ticket_ = ticket;
    std::set<std::int32_t> family_neighbors;
    std::set<std::uint32_t> seen;
    for (const auto& saved : bundle.household_roster) {
        const auto member = expected.find(saved.guid);
        if (member == expected.end() || !seen.insert(saved.guid).second || !same_identity(identity(saved), member->second))
            throw FormatError("household transfer archive resource roster differs from expected family");
        const auto actor = std::find_if(bundle.world.entities.begin(), bundle.world.entities.end(),
            [&](const auto& entity) { return entity.id == saved.id && entity.generation == saved.generation; });
        if (actor == bundle.world.entities.end() || actor->kind != EntityKind::avatar || !actor->definition ||
            actor->definition->guid != saved.guid ||
            (actor->person_data.size() != 80 && actor->person_data.size() != 88 && actor->person_data.size() != 256) ||
            actor->motives.size() != 16)
            throw FormatError("household transfer requires known raw 80/88/256-word person and 16-word motive banks");
        if(actor->person_data.size()>88&&std::any_of(actor->person_data.begin()+88,actor->person_data.end(),
                [](auto word){return word!=0;}))
            throw FormatError("household transfer cannot project unknown nonzero person extension words");
        const auto decay = std::find_if(bundle.simulation->avatars.begin(), bundle.simulation->avatars.end(),
            [&](const auto& value) { return value.id == saved.id; });
        if (decay == bundle.simulation->avatars.end()) throw FormatError("household transfer resident has no SIMS fractional decay");
        HouseholdTransferMember profile;
        profile.identity = member->second;
        // The existing TS1 NBRS inheritance profile projects 80 or 256 words
        // onto 88, padding old records. The immutable archive retains all bytes.
        std::copy_n(actor->person_data.begin(), std::min<std::size_t>(88,actor->person_data.size()), profile.person_data.begin());
        std::copy_n(actor->motives.begin(), 16, profile.motives.begin());
        profile.attributes = actor->attributes;
        profile.bound_appearances = actor->bound_appearances;
        profile.decay = decay->decay;
        unsupported_source(profile);
        result.members_.push_back(std::move(profile));
        family_neighbors.insert(saved.neighbor_id);
    }
    // Keep every outbound family source (even empty) and inbound family pair.
    result.relationships_.limits = bundle.relationships.limits;
    for (const auto& source : bundle.relationships.sources) {
        if (source.matrix != RelationshipMatrix::neighbor) continue;
        auto projected = source;
        if (!family_neighbors.count(source.id)) {
            projected.pairs.erase(std::remove_if(projected.pairs.begin(), projected.pairs.end(),
                [&](const auto& pair) { return !family_neighbors.count(pair.target); }), projected.pairs.end());
            if (projected.pairs.empty()) continue;
        }
        result.relationships_.sources.push_back(std::move(projected));
    }
    recount(result.relationships_);
    validate_relationships(result.relationships_);
    return result;
}

AppliedHouseholdTransfer apply_household_transfer(const PreparedHouseholdTransfer& prepared,
        const WorldSnapshot& destination_world, const RelationshipSnapshot& destination_relationships,
        const SessionFamilyLease& destination_lease, const std::vector<HouseholdTransferIdentity>& destination_roster,
        const Ts1ClockSnapshot& destination_clock) {
    const auto expected = identities(destination_roster);
    const auto& source_lease = prepared.ticket().source_lease;
    if (destination_lease.neighborhood_identity != source_lease.neighborhood_identity ||
        destination_lease.family_identity != source_lease.family_identity || destination_lease.family_chunk_id != source_lease.family_chunk_id ||
        destination_lease.occupancy_epoch <= source_lease.occupancy_epoch || expected.size() != prepared.members().size())
        throw FormatError("household transfer destination family or occupancy is invalid");
    Ts1Clock clock;
    if (!clock.restore(destination_clock)) throw FormatError("household transfer destination clock is invalid");
    WorldLimits limits; limits.words = 131072;
    World validation(limits, destination_world.attribute_policy);
    if (!validation.restore_snapshot(destination_world)) throw FormatError("household transfer destination World is invalid");
    validate_relationships(destination_relationships);
    AppliedHouseholdTransfer result;
    result.ticket_ = prepared.ticket(); result.destination_lease_ = destination_lease;
    result.world_ = destination_world;
    result.relationships_ = destination_relationships;
    std::set<std::int32_t> family_neighbors;
    for (const auto& profile : prepared.members()) {
        const auto found = expected.find(profile.identity.guid);
        if (found == expected.end() || !same_identity(profile.identity, found->second))
            throw FormatError("household transfer destination resource roster changed");
        Entity* actor = nullptr;
        for (auto& entity : result.world_.entities)
            if (entity.kind == EntityKind::avatar && entity.definition && entity.definition->guid == profile.identity.guid) {
                if (actor) throw FormatError("household transfer destination contains duplicate resident GUID");
                actor = &entity;
            }
        if (!actor || actor->person_data.size() != 88 || actor->motives.size() != 16 || actor->attachment ||
            actor->ui_ghost || actor->person_data[0] != 0 || actor->person_data[68] != 0 ||
            std::any_of(actor->slots.begin(), actor->slots.end(), [](auto value) { return value != 0; }))
            throw FormatError("household transfer destination resident has unsupported banks, posture, ghost or attachment");
        // VMContext creates the avatar, runs Init/EP8, then inherits only the
        // neighbor person bank. Keep those fresh runtime attributes; the full
        // previous bank remains authenticated in the source archive/profile.
        if (!actor->bound_appearances.empty()) throw FormatError("household transfer destination contains bound appearance props");
        for (unsigned index : {9U, 16U, 19U, 20U, 24U, 29U, 54U, 55U, 66U, 67U, 77U})
            if (actor->person_data[index] != profile.person_data[index])
                throw FormatError("household transfer unknown PersonData[" + std::to_string(index) + "] differs from destination");
        if (actor->person_data[65] != profile.person_data[65]) throw FormatError("household transfer gender differs from destination resource");
        for (unsigned index : {0U, 1U, 2U, 4U, 10U, 12U})
            if (actor->motives[index] != profile.motives[index])
                throw FormatError("household transfer unknown motive[" + std::to_string(index) + "] differs from destination");
        for (unsigned index : {68U, 84U, 85U, 86U})
            if (actor->person_data[index] != 0) throw FormatError("household transfer destination transformation is unsupported");
        for (unsigned index : {2U,3U,4U,5U,6U,7U,10U,11U,12U,15U,17U,18U,36U,
                46U,47U,48U,49U,50U,51U,52U,53U,56U,57U,58U,60U,63U,65U,68U,69U,70U,
                75U,76U,80U,81U,82U,83U,84U,85U,86U}) actor->person_data[index] = profile.person_data[index];
        actor->person_data[31] = profile.identity.neighbor_id;
        actor->person_data[61] = signed_family(destination_lease.family_chunk_id);
        for (unsigned index : {5U,6U,7U,8U,9U,14U,15U}) actor->motives[index] = profile.motives[index];
        // Mood/room score/sleep and motive change controllers stay destination-owned.
        auto decay = profile.decay;
        decay.last_two_minute_bucket = static_cast<std::uint8_t>(destination_clock.minute / 2);
        result.decays_.push_back({actor->id, decay});
        result.members_.push_back({actor->id, actor->generation, profile.identity.guid, profile.identity.neighbor_id,
            profile.identity.object_definition_id, profile.identity.character_asset, profile.identity.character_fingerprint});
        family_neighbors.insert(profile.identity.neighbor_id);
    }
    // Replace all family-owned rows and inbound pairs; preserve destination
    // nonfamily-to-nonfamily rows and every OBJECT matrix/generation verbatim.
    for (auto it = result.relationships_.sources.begin(); it != result.relationships_.sources.end();) {
        if (it->matrix != RelationshipMatrix::neighbor) { ++it; continue; }
        if (family_neighbors.count(it->id)) { it = result.relationships_.sources.erase(it); continue; }
        it->pairs.erase(std::remove_if(it->pairs.begin(), it->pairs.end(),
            [&](const auto& pair) { return family_neighbors.count(pair.target) != 0; }), it->pairs.end());
        ++it;
    }
    for (const auto& incoming : prepared.relationships().sources) {
        auto found = std::find_if(result.relationships_.sources.begin(), result.relationships_.sources.end(),
            [&](const auto& source) { return source.matrix == RelationshipMatrix::neighbor && source.id == incoming.id; });
        if (found == result.relationships_.sources.end()) result.relationships_.sources.push_back(incoming);
        else found->pairs.insert(found->pairs.end(), incoming.pairs.begin(), incoming.pairs.end());
    }
    recount(result.relationships_);
    validate_relationships(result.relationships_);
    RelationshipState normalized;
    if (!normalized.restore_snapshot(result.relationships_)) throw FormatError("household transfer merged relationships are invalid");
    result.relationships_ = normalized.snapshot();
    if (!validation.publish_snapshot(result.world_)) throw FormatError("household transfer result World cannot be published");
    result.world_ = validation.snapshot();
    std::sort(result.members_.begin(), result.members_.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(result.decays_.begin(), result.decays_.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}
} // namespace odh
