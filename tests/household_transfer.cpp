#include "odh/household_transfer.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& operation, const char* message) {
    try { operation(); } catch (const odh::FormatError&) { return; }
    throw std::runtime_error(message);
}
std::vector<odh::HouseholdTransferIdentity> identities() {
    std::vector<odh::HouseholdTransferIdentity> result;
    for (unsigned i = 0; i < 2; ++i) {
        odh::HouseholdTransferIdentity member;
        member.guid = 0x12345678U + i; member.neighbor_id = static_cast<std::int16_t>(11 + i);
        member.object_definition_id = 128; member.character_asset = "PlayerData/Characters/User" + std::to_string(i) + ".iff";
        member.character_fingerprint.fill(static_cast<std::uint8_t>(0x31 + i)); result.push_back(member);
    }
    return result;
}
odh::SessionFamilyLease lease() {
    odh::SessionFamilyLease result;
    result.neighborhood_identity.fill(1); result.family_identity.fill(2);
    result.family_chunk_id = 21; result.occupancy_epoch = 1; return result;
}
void add_actor(odh::World& world, std::int16_t id, unsigned index, bool source) {
    std::vector<std::int16_t> person(88), motives(16);
    person[16] = 333; person[65] = static_cast<std::int16_t>(index);
    person[61] = 21; person[31] = static_cast<std::int16_t>(11 + index);
    if (source) {
        person[0] = 0; // This fixture has no completed Sit continuation.
        person[10] = 7; person[51] = 412; person[75] = 120; person[80] = 91; person[83] = 63;
        person[46] = static_cast<std::int16_t>(100 + index);
        for (unsigned need : {5U,6U,7U,8U,9U,14U,15U}) motives[need] = static_cast<std::int16_t>(20 + index);
        motives[3] = 55; motives[13] = 66; motives[11] = 1;
    } else { motives[3] = -5; motives[13] = 9; }
    require(world.create_saved_entity(id, odh::EntityKind::avatar, identities()[index].guid,
        {3, 4}, person, motives, {}, {}, 0), "create bounded transfer actor");
    require(world.set_position(id, {static_cast<std::int16_t>(id * 16), 24, 1, 0}), "position transfer actor");
}
odh::RelationshipSnapshot relations(bool source) {
    odh::RelationshipSnapshot result;
    using Matrix = odh::RelationshipMatrix;
    if (source) result.sources = {
        {Matrix::neighbor, 11, 0, {{12,{50}}, {90,{8}}, {65548,{-3}}}},
        {Matrix::neighbor, 12, 0, {{11,{70}}}},
        {Matrix::neighbor, 90, 0, {{11,{23}}, {91,{99}}, {65548,{777}}}},
        {Matrix::neighbor, 91, 0, {{12,{33}}}},
        {Matrix::object, 1, 1, {{2,{55}}}}};
    else result.sources = {
        {Matrix::neighbor, 11, 0, {{90,{-5}}}},
        {Matrix::neighbor, 90, 0, {{11,{-9}}, {91,{111}}}},
        {Matrix::neighbor, 92, 0, {{12,{-1}}}},
        {Matrix::object, 101, 1, {{102,{88,99}}}}};
    for (const auto& row : result.sources) {
        result.pair_count += row.pairs.size();
        for (const auto& pair : row.pairs) result.value_count += pair.values.size();
    }
    return result;
}
odh::SessionBundle fixture() {
    odh::World world;
    add_actor(world, 1, 0, true); add_actor(world, 2, 1, true);
    odh::Vm first(odh::RoutineSet{}), second(odh::RoutineSet{});
    odh::EntityThreadControllers first_controllers(world,1), second_controllers(world,2);
    require(first_controllers.bind(first) && second_controllers.bind(second), "bind source controllers");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1,first,[](std::uint64_t){return true;},[]{return true;},[]{return true;}) &&
        scheduler.add(2,second,[](std::uint64_t){return true;},[]{return true;},[]{return true;}), "register source VMs");
    odh::SessionBundle bundle;
    bundle.generic_execution = true; bundle.family_lease = lease();
    bundle.resources.language_code = 1; bundle.resources.resource_fingerprint.fill(0x5a);
    bundle.host.neighborhood_mode = 1;
    bundle.host.house_number = 7; bundle.host.family_chunk_id = 21; bundle.host.family_number = 900;
    bundle.host.resident_guid = identities()[0].guid; bundle.host.resident_neighbor_id = 11;
    bundle.host.runtime_avatar_id = 1; bundle.host.family_budget = 1000;
    bundle.world = world.snapshot(); bundle.relationships = relations(true);
    for (unsigned i = 0; i < 2; ++i) {
        const auto id = static_cast<std::int16_t>(i + 1); const auto member = identities()[i];
        bundle.household_roster.push_back({id, world.find(id)->generation, member.guid, member.neighbor_id,
            member.object_definition_id, member.character_asset, member.character_fingerprint});
        odh::SessionRuntimeEntity runtime;
        runtime.id = id; runtime.vm = i ? second.snapshot() : first.snapshot();
        runtime.controllers = i ? second_controllers.snapshot() : first_controllers.snapshot();
        bundle.runtime_entities.push_back(std::move(runtime));
    }
    bundle.scheduler = scheduler.snapshot();
    odh::SessionSimulationState simulation;
    simulation.avatars = {{1,{3,{1,2,3,4,5,6,7}}}, {2,{4,{11,12,13,14,15,16,17}}}};
    bundle.simulation = simulation;
    return bundle;
}
odh::HouseholdTransferTicket ticket_for(const odh::SessionBundle& bundle, const std::vector<std::uint8_t>& bytes) {
    return {*bundle.family_lease, 7, 900, 1000, odh::sha256(bytes.data(), bytes.size())};
}
const odh::Entity& actor(const odh::WorldSnapshot& world, std::int16_t id) {
    const auto found = std::find_if(world.entities.begin(), world.entities.end(), [&](const auto& entity){return entity.id == id;});
    require(found != world.entities.end(), "find transfer actor"); return *found;
}
}
int main() {
    try {
        const auto source = fixture(); const auto encoded = odh::encode_session_bundle(source);
        const auto ticket = ticket_for(source, encoded);
        const auto prepared = odh::prepare_household_transfer(encoded, ticket, identities());
        {
            auto legacy=source;
            for(auto& entity:legacy.world.entities)if(entity.kind==odh::EntityKind::avatar){
                legacy.world.word_count-=entity.person_data.size()-80;
                entity.person_data.resize(80);
            }
            const auto bytes=odh::encode_session_bundle(legacy);
            const auto projected=odh::prepare_household_transfer(bytes,ticket_for(legacy,bytes),identities());
            require(projected.members().front().person_data[79]==legacy.world.entities.front().person_data[79]&&
                projected.members().front().person_data[80]==0,"legacy 80-word projection pads the absent TS1 extension");
            auto extended=source;
            for(auto& entity:extended.world.entities)if(entity.kind==odh::EntityKind::avatar){
                extended.world.word_count+=256-entity.person_data.size();entity.person_data.resize(256);
            }
            const auto expanded=odh::encode_session_bundle(extended);
            require(odh::prepare_household_transfer(expanded,ticket_for(extended,expanded),identities()).members().front().person_data[80]==91,
                "256-word projection retains all 88 semantic words");
            extended.world.entities.front().person_data[255]=1;
            const auto unknown=odh::encode_session_bundle(extended);
            rejects([&]{(void)odh::prepare_household_transfer(unknown,ticket_for(extended,unknown),identities());},
                "nonzero unknown person extension cannot be silently truncated");
        }
        require(prepared.members().size() == 2 && prepared.members()[1].person_data[51] == 412,
            "prepared raw profile preserves persistent source progress");
        odh::World destination;
        add_actor(destination, 101, 0, false); add_actor(destination, 102, 1, false);
        const auto world_before = destination.snapshot(); const auto relationship_before = relations(false);
        const auto world_bytes = odh::encode_world_snapshot(world_before);
        const auto relation_bytes = odh::encode_relationship_snapshot(relationship_before);
        auto destination_lease = lease(); destination_lease.occupancy_epoch = 2;
        odh::Ts1ClockSnapshot destination_clock; destination_clock.minute = 42;
        const auto applied = odh::apply_household_transfer(prepared, world_before, relationship_before,
            destination_lease, identities(), destination_clock);
        require(applied.members()[0].id == 101 && applied.members()[1].id == 102 &&
            applied.ticket().archive_sha256 == ticket.archive_sha256 && applied.destination_lease().occupancy_epoch == 2,
            "receipt binds original archive and every rebased destination runtime identity");
        for (std::int16_t id : {101,102}) {
            const auto& transferred = actor(applied.world(), id); const auto& fresh = actor(world_before,id);
            require(transferred.person_data[10] == 7 && transferred.person_data[51] == 412 &&
                transferred.person_data[75] == 120 && transferred.person_data[80] == 91 && transferred.person_data[83] == 63,
                "raw zodiac, fame and magic progression survives projection");
            require(transferred.position == fresh.position && transferred.generation == fresh.generation &&
                transferred.person_data[0] == 0 && transferred.motives[11] == 0 &&
                transferred.motives[3] == -5 && transferred.motives[13] == 9,
                "destination position, posture, sleep, mood and room score stay destination-owned");
            require(transferred.person_data[31] == id - 90 && transferred.person_data[61] == 21 &&
                transferred.motives[7] == 20 + id - 101, "stable neighbor ID and seven needs map to destination actors");
        }
        require(applied.decays()[0].id == 101 && applied.decays()[0].decay.fractions[6] == 7 &&
            applied.decays()[1].decay.fractions[0] == 11 && applied.decays()[1].decay.last_two_minute_bucket == 21,
            "fractional motive decay is preserved per GUID and rebased to destination clock");
        odh::RelationshipState merged;
        require(merged.restore_snapshot(applied.relationships()), "merged relationships validate");
        require(merged.value(11,12,0) == 50 && merged.value(90,11,0) == 23 && merged.value(91,12,0) == 33 &&
            merged.value(90,91,0) == 111 && !merged.value(92,12,0) && !merged.value(90,65548,0) &&
            merged.value(11,65548,0) == -3, "complete inbound/outbound merge preserves other pairs without narrowing target IDs");
        const auto object = std::find_if(applied.relationships().sources.begin(), applied.relationships().sources.end(),
            [](const auto& row){return row.matrix == odh::RelationshipMatrix::object;});
        require(object != applied.relationships().sources.end() && object->id == 101 &&
            object->pairs[0].target == 102 && object->pairs[0].values == std::vector<std::int16_t>({88,99}),
            "destination OBJECT relationships survive and source OBJECT IDs do not migrate");
        require(odh::encode_world_snapshot(destination.snapshot()) == world_bytes &&
            odh::encode_relationship_snapshot(relationship_before) == relation_bytes, "successful stage does not mutate destination inputs");
        auto bad_world = world_before;
        bad_world.entities.back().person_data[16] = 334;
        rejects([&]{(void)odh::apply_household_transfer(prepared,bad_world,relationship_before,destination_lease,identities(),destination_clock);},
            "second resident unknown progression mismatch rejects entire transfer");
        require(odh::encode_world_snapshot(destination.snapshot()) == world_bytes, "no partial first-resident writes escape a rejected transfer");
        bad_world = world_before; bad_world.entities[0].attributes[0] = 99;
        require(odh::apply_household_transfer(prepared,bad_world,relationship_before,destination_lease,identities(),destination_clock)
            .world().entities[0].attributes[0]==99,"fresh destination Init/EP8 attributes remain destination-owned");
        bad_world = world_before; bad_world.entities[0].person_data[0] = 1;
        rejects([&]{(void)odh::apply_household_transfer(prepared,bad_world,relationship_before,destination_lease,identities(),destination_clock);}, "existing destination seat rejects");
        auto wrong_roster = identities(); wrong_roster[1].character_fingerprint[0] ^= 1;
        rejects([&]{(void)odh::prepare_household_transfer(encoded,ticket,wrong_roster);}, "changed character bytes reject source authentication");
        rejects([&]{(void)odh::apply_household_transfer(prepared,world_before,relationship_before,destination_lease,wrong_roster,destination_clock);}, "changed destination resource cannot reuse receipt");
        auto wrong_ticket = ticket; ++wrong_ticket.checkpoint_budget;
        rejects([&]{(void)odh::prepare_household_transfer(encoded,wrong_ticket,identities());}, "checkpoint budget authentication rejects mismatch");
        auto wrong_lease = destination_lease; wrong_lease.occupancy_epoch = 1;
        rejects([&]{(void)odh::apply_household_transfer(prepared,world_before,relationship_before,wrong_lease,identities(),destination_clock);}, "old occupancy is not a new destination");
        for (unsigned transform : {68U,84U,85U,86U}) {
            auto unsupported = source; unsupported.world.entities[0].person_data[transform] = 1;
            const auto data = odh::encode_session_bundle(unsupported);
            rejects([&]{(void)odh::prepare_household_transfer(data,ticket_for(unsupported,data),identities());}, "unsupported transformation rejects before apply");
        }
        auto unsupported = source; unsupported.world.entities[0].bound_appearances = {"held-prop.apr"};
        auto data = odh::encode_session_bundle(unsupported);
        rejects([&]{(void)odh::prepare_household_transfer(data,ticket_for(unsupported,data),identities());}, "bound props cannot migrate through appearance paths");
        unsupported = source; unsupported.simulation.reset(); data = odh::encode_session_bundle(unsupported);
        rejects([&]{(void)odh::prepare_household_transfer(data,ticket_for(unsupported,data),identities());}, "SIMS required for fractional decay guarantee");
        auto small_budget = relationship_before; small_budget.limits.pairs = small_budget.pair_count;
        rejects([&]{(void)odh::apply_household_transfer(prepared,world_before,small_budget,destination_lease,identities(),destination_clock);}, "merged relationship budget overflow rejects atomically");
        std::cout << "Household portable transfer checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
