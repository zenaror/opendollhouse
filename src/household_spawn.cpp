#include "odh/household_spawn.hpp"

#include "odh/formats.hpp"

#include <algorithm>
#include <set>

namespace odh {
namespace {
bool inside(std::int32_t x, std::int32_t y,
            std::uint16_t width, std::uint16_t height) {
    return x >= 0 && y >= 0 && x <= 32767 && y <= 32767 &&
           x / 16 < width && y / 16 < height;
}
void append(std::vector<EntityPosition>& result, std::int32_t x, std::int32_t y,
            std::int16_t level, std::uint8_t direction) {
    result.push_back({x, y, level, direction});
}
}

std::vector<EntityPosition> household_spawn_candidates(
    EntityPosition mailbox, std::uint16_t lot_width, std::uint16_t lot_height) {
    if (mailbox.x < 0 || mailbox.y < 0 || mailbox.level < 1 || mailbox.level > 2 ||
        mailbox.direction > 7 || !lot_width || !lot_height ||
        !inside(mailbox.x, mailbox.y, lot_width, lot_height))
        throw FormatError("household spawn mailbox or lot bounds are invalid");

    // Mirrors VMTS1LotState.VerifyFamily -> VMFindLocationFor.FindLocationFor:
    // four facings on the mailbox, then nine expanding square rings. Keep the
    // repeated trials from the source loops; SetPosition decides their result.
    std::vector<EntityPosition> result;
    result.reserve(1444);
    for (std::uint8_t direction = 0; direction < 8; direction += 2)
        append(result, mailbox.x, mailbox.y, mailbox.level, direction);

    const auto tile_x = mailbox.x / 16;
    const auto tile_y = mailbox.y / 16;
    for (std::int32_t ring = 1; ring < 10; ++ring) {
        for (std::int32_t x = -ring; x <= ring; ++x) {
            for (std::uint8_t direction = 0; direction < 8; ++direction) {
                const auto y = tile_y + ((direction % 2) * 2 - 1) * ring;
                append(result, (tile_x + x) * 16 + 8, y * 16 + 8,
                       mailbox.level, static_cast<std::uint8_t>((direction / 2) * 2));
            }
        }
        for (std::int32_t y = 1 - ring; y < ring; ++y) {
            for (std::uint8_t direction = 0; direction < 8; ++direction) {
                const auto x = tile_x + ((direction % 2) * 2 - 1) * ring;
                append(result, x * 16 + 8, (tile_y + y) * 16 + 8,
                       mailbox.level, static_cast<std::uint8_t>((direction / 2) * 2));
            }
        }
    }
    return result;
}

std::optional<EntityPosition> first_household_spawn_candidate(
    EntityPosition mailbox, std::uint16_t lot_width, std::uint16_t lot_height,
    const std::function<bool(const EntityPosition&)>& accepts) {
    if (!accepts) throw FormatError("household spawn placement validator is required");
    for (const auto& candidate : household_spawn_candidates(mailbox, lot_width, lot_height))
        if (accepts(candidate)) return candidate;
    return std::nullopt;
}

} // namespace odh

#include "odh/household_spawn.hpp"
#include "odh/formats.hpp"
#include <set>

namespace odh {
namespace {
std::int16_t family_word(std::uint16_t id) {
    const auto value = static_cast<std::int32_t>(id);
    return static_cast<std::int16_t>(value < 32768 ? value : value - 65536);
}
std::int16_t next_free_id(const World& world) {
    for (std::int32_t candidate = 1; candidate <= 32767; ++candidate) {
        const auto id = static_cast<std::int16_t>(candidate);
        if (world.entity_id_available(id)) return id;
    }
    throw FormatError("household runtime ID space exhausted");
}
}
std::optional<std::int16_t> avatar_constructor_initial_value(const std::vector<std::int16_t>& words) {
    // VMOBJDVariable indices include the two version words. Source TS1 provider
    // uses NumGraphics4, Master10/Sub11, Disabled16, Price18, FunctionFlags40,
    // BuildModeType69. Avatar type2 is a singleton, never an abstract master.
    if(words.size()<70||(words[0]!=136&&words[0]!=138)||words[1]!=0||words[9]!=2||words[10]!=0)return {};
    // The provider has thirty categories. An invalid build-only category can
    // throw during catalog insertion; do not claim a price for malformed data.
    if(words[40]==0&&static_cast<std::uint16_t>(words[69])>22)return {};
    const bool catalog=(words[40]!=0||words[69]!=0)&&words[16]==0&&static_cast<std::uint16_t>(words[4])>0;
    // VMContext initializes group.InitialPrice to zero only when the complete
    // TS1 catalog has no entry; its GetValue41 is the signed low16 price.
    return catalog?words[18]:std::int16_t{0};
}
void initialize_avatar_constructor_words(World& world, std::int16_t id,
    std::optional<std::int16_t> initial_value) {
    // The managed allocation has eighty words, but index79 is specifically
    // FSOEngineQuery, outside the native TS1 bank/profile. Keep0..78 complete.
    for (std::int16_t index = 0; index < 79; ++index) {
        // Direction's NORTH default and Room's zero backing storage exist
        // before placement; GetValue(Room) still derives -5 while out of world.
        if (index == 2 || index == 3 || index == 11 ||
            index == 26 || index == 41 ||
            index == 62 || index == 67) continue;
        const auto value = index == 8 ? std::int16_t{256} : std::int16_t{0};
        if (!world.declare_object_word(id, index, value))
            throw FormatError("avatar constructor object words exceed World budget");
    }
    if(initial_value&&!world.declare_object_word(id,41,*initial_value))
        throw FormatError("avatar constructor group initial value exceeds World budget");
}
namespace {
HouseholdSpawnResult materialize_household_members_impl(
    const WorldSnapshot& source, const std::vector<HouseholdSpawnRequest>& household,
    std::uint16_t family_chunk_id,
    const HouseholdAvatarPreparer& prepare_avatar,
    const HouseholdArrivalResolver* resolve_arrival, bool defer_inheritance,
    WorldLimits limits) {
    if (household.empty()) throw FormatError("selected household has no members");
    if (!prepare_avatar)
        throw FormatError("household avatar preparation callback is required");

    // Snapshot bytes omit reservation policy. Every candidate explicitly
    // receives the caller's launch limits, including retired-ID reservations.
    auto candidate = std::make_unique<World>(limits, source.attribute_policy);
    if (!candidate->restore_snapshot(source))
        throw FormatError("source WorldSnapshot cannot be restored into candidate World");

    std::map<std::uint32_t, std::int16_t> existing;
    auto cursor = std::int16_t{0};
    while (const auto id = candidate->next_entity(cursor, EntityKind::avatar)) {
        cursor = *id;
        const auto* avatar = candidate->find(*id);
        if (!avatar || !avatar->definition || avatar->definition->guid == 0)
            throw FormatError("candidate World contains avatar with invalid GUID");
        if (!existing.emplace(avatar->definition->guid, *id).second)
            throw FormatError("candidate World contains duplicate avatar GUIDs");
    }

    std::set<std::uint32_t> seen;
    for (const auto& member : household) {
        if (member.guid == 0 || !seen.insert(member.guid).second)
            throw FormatError("selected household contains invalid or duplicate GUID");
        if (!existing.count(member.guid) &&
            (!member.neighbor || member.neighbor->guid != member.guid))
            throw FormatError("missing household avatar has no matching valid neighbor record");
    }

    HouseholdSpawnResult result;
    result.candidate = std::move(candidate);
    result.new_members.reserve(household.size());
    for (const auto& member : household) {
        const auto found = existing.find(member.guid);
        if (found != existing.end()) {
            result.runtime_ids.emplace(member.guid, found->second);
            continue;
        }
        const auto id = next_free_id(*result.candidate);
        if (!result.candidate->create(id, EntityKind::avatar, {0, 101, 3, 16}) ||
            !result.candidate->define_type(id, member.guid))
            throw FormatError("cannot create household avatar within World budget");
        initialize_avatar_constructor_words(*result.candidate, id);
        if (!prepare_avatar(*result.candidate, id, member.guid))
            throw FormatError("household avatar preparation failed");
        if (defer_inheritance) {
            const auto* staged = result.candidate->find(id);
            if (!staged || staged->position)
                throw FormatError("staged household avatar must remain unpositioned");
        } else {
            inherit_staged_household_member(*result.candidate, id, *member.neighbor,
                                            family_chunk_id);
        }
        if (resolve_arrival) {
            const auto arrival = (*resolve_arrival)(*result.candidate, id, member.guid);
            if (!arrival)
                throw FormatError("household avatar arrival position was not accepted");
            if (!result.candidate->set_position(id, *arrival))
                throw FormatError("accepted household arrival position cannot be applied");
        }
        result.runtime_ids.emplace(member.guid, id);
        result.new_members.push_back({member.guid, id});
    }
    return result;
}
}

void inherit_staged_household_member(
    World& candidate, std::int16_t runtime_id, const NeighborRecord& neighbor,
    std::uint16_t family_chunk_id) {
    inherit_neighbor(candidate, runtime_id, neighbor, family_chunk_id);
    if (!candidate.set_person_data(runtime_id, 61, family_word(family_chunk_id)))
        throw FormatError("household FAMI chunk id cannot be stored in PersonData[61]");
}

HouseholdSpawnResult stage_household_members(
    const WorldSnapshot& source, const std::vector<HouseholdSpawnRequest>& household,
    std::uint16_t family_chunk_id,
    const HouseholdAvatarPreparer& prepare_avatar, WorldLimits limits) {
    return materialize_household_members_impl(source, household, family_chunk_id,
                                              prepare_avatar, nullptr, true, limits);
}

HouseholdSpawnResult materialize_household_members(
    const WorldSnapshot& source, const std::vector<HouseholdSpawnRequest>& household,
    std::uint16_t family_chunk_id,
    const HouseholdAvatarPreparer& prepare_avatar,
    const HouseholdArrivalResolver& resolve_arrival, WorldLimits limits) {
    if (!resolve_arrival)
        throw FormatError("household arrival resolver is required");
    return materialize_household_members_impl(source, household, family_chunk_id,
                                              prepare_avatar, &resolve_arrival, false, limits);
}

HouseholdVerificationResult verify_household_members(
    const WorldSnapshot& source,
    const std::vector<HouseholdSpawnRequest>& household,
    std::uint16_t family_chunk_id,
    const HouseholdAvatarPreparer& prepare_avatar,
    std::optional<EntityPosition> mailbox,
    std::uint16_t lot_width, std::uint16_t lot_height,
    const HouseholdPositionAttempt& attempt_position,
    const HouseholdMemberActivation& activate_member,
    WorldLimits limits) {
    if (!activate_member)
        throw FormatError("household member activation callback is required");
    if (!lot_width || !lot_height)
        throw FormatError("household verification lot bounds are invalid");

    HouseholdVerificationResult result;
    if (household.empty()) throw FormatError("selected household has no members");

    // Validate the whole request before invoking host callbacks. Then process
    // each missing member as VerifyFamily does: create/start one avatar,
    // inherit its records, search for arrival, and activate it before moving
    // to the next member. This lets later startup see earlier residents.
    World validation_world(limits, source.attribute_policy);
    if (!validation_world.restore_snapshot(source))
        throw FormatError("source WorldSnapshot cannot be restored into candidate World");
    std::set<std::uint32_t> existing;
    auto cursor = std::int16_t{0};
    while (const auto id = validation_world.next_entity(cursor, EntityKind::avatar)) {
        cursor = *id;
        const auto* avatar = validation_world.find(*id);
        if (!avatar || !avatar->definition || avatar->definition->guid == 0 ||
            !existing.insert(avatar->definition->guid).second)
            throw FormatError("candidate World contains avatar with invalid or duplicate GUID");
    }
    std::set<std::uint32_t> seen;
    for (const auto& member : household) {
        if (member.guid == 0 || !seen.insert(member.guid).second)
            throw FormatError("selected household contains invalid or duplicate GUID");
        if (!existing.count(member.guid) &&
            (!member.neighbor || member.neighbor->guid != member.guid))
            throw FormatError("missing household avatar has no matching valid neighbor record");
    }

    result.new_members.reserve(household.size());
    const auto candidates = mailbox ? household_spawn_candidates(
        *mailbox, lot_width, lot_height) : std::vector<EntityPosition>{};

    auto current = source;
    for (const auto& request : household) {
        auto staged = stage_household_members(current, {request}, family_chunk_id,
                                              prepare_avatar, limits);
        current = staged.candidate->snapshot();
        result.candidate = std::move(staged.candidate);
        if (staged.new_members.empty()) {
            const auto found = staged.runtime_ids.find(request.guid);
            if (found == staged.runtime_ids.end())
                throw FormatError("existing household member lost its runtime ID");
            result.runtime_ids.emplace(request.guid, found->second);
            continue;
        }
        const auto created = staged.new_members.front();
        result.runtime_ids.emplace(created.guid, created.runtime_id);

        // Simitone creates and initializes the avatar out of world, inherits
        // NBRS, then assigns the FAMI chunk before attempting mailbox arrival.
        inherit_staged_household_member(*result.candidate, created.runtime_id,
                                        *request.neighbor, family_chunk_id);

        bool positioned = false;
        std::size_t attempts = 0;
        if (mailbox) {
            if (!attempt_position)
                throw FormatError("mailbox arrival requires a stateful position-attempt callback");
            for (const auto& candidate : candidates) {
                ++attempts;
                const auto attempt = attempt_position(*result.candidate,
                                                      created.runtime_id, candidate);
                const auto* avatar = result.candidate->find(created.runtime_id);
                if (!avatar || avatar->kind != EntityKind::avatar ||
                    !avatar->definition || avatar->definition->guid != created.guid)
                    throw FormatError("position attempt changed the staged household identity");
                if (attempt == HouseholdPositionAttemptStatus::unsupported)
                    throw FormatError("household arrival semantics are unsupported by this host");
                positioned = attempt == HouseholdPositionAttemptStatus::placed;
                if (positioned) {
                    if (!avatar->position || !(*avatar->position == candidate))
                        throw FormatError("successful position attempt did not place the staged avatar");
                    break;
                }
                if (avatar->position)
                    throw FormatError("failed position attempt left the staged avatar positioned");
            }
        }

        if (!activate_member(*result.candidate, created.runtime_id,
                             created.guid, positioned))
            throw FormatError("household member activation failed");
        result.new_members.push_back({created.guid, created.runtime_id,
                                      positioned, attempts});
        current = result.candidate->snapshot();
    }
    return result;
}
HouseholdActivationResult verify_household_members_in_place(
    World& world,const std::vector<HouseholdSpawnRequest>& household,
    std::uint16_t family_chunk_id,const HouseholdAvatarPreparer& prepare,
    std::optional<EntityPosition> mailbox,std::uint16_t width,std::uint16_t height,
    const HouseholdPositionAttempt& place,const HouseholdMemberActivation& activate) {
    if(!prepare||!activate||!width||!height||household.empty()||(mailbox&&!place))
        throw FormatError("in-place household bootstrap requires complete constructor/arrival/activation services");
    // Authenticate the complete request before its first constructor effect.
    // Existing GUIDs are identities, not absence inferred from a chosen ID.
    std::map<std::uint32_t,std::int16_t> existing;
    for(auto id=world.next_entity(0,EntityKind::avatar);id;id=world.next_entity(*id,EntityKind::avatar)){
        const auto* avatar=world.find(*id);
        if(!avatar||avatar->dead||!avatar->definition||!avatar->definition->guid||
           !existing.emplace(avatar->definition->guid,*id).second)
            throw FormatError("in-place household has invalid or duplicate existing avatar identity");
    }
    std::set<std::uint32_t> seen;
    for(const auto& request:household)
        if(!request.guid||!seen.insert(request.guid).second||
           (!existing.count(request.guid)&&(!request.neighbor||request.neighbor->guid!=request.guid)))
            throw FormatError("in-place household lacks unique GUID and exact missing-member NBRS");
    const auto candidates=mailbox?household_spawn_candidates(*mailbox,width,height):std::vector<EntityPosition>{};
    HouseholdActivationResult result;result.new_members.reserve(household.size());
    for(const auto& request:household){
        const auto present=existing.find(request.guid);
        if(present!=existing.end()){result.runtime_ids.emplace(request.guid,present->second);continue;}
        const auto id=next_free_id(world);
        if(!world.create(id,EntityKind::avatar,{0,101,3,16})||!world.define_type(id,request.guid))
            throw FormatError("in-place household constructor exceeded World budget");
        const WorldEntityReference identity{id,world.find(id)->generation};
        initialize_avatar_constructor_words(world,id);
        // This is an actual NEW cXPerson allocation, not restored-resource
        // metadata. Share the same source base-constructor proof as NPC
        // materialization: empty record0, raw Slot=-1/Parent=0, OOW level1.
        if(!world.declare_object_word(id,14,-1)||!world.declare_object_word(id,26,0)||
           !world.publish_native_slot0(identity,{})||!world.set_native_out_of_world_level(identity,1))
            throw FormatError("in-place household native base constructor publication failed");
        if(world.lockout_count_clock()&&!world.admit_lockout_count_state(id,identity.generation,true,0))
            throw FormatError("in-place household fresh Thread lockout state unavailable");
        if(!prepare(world,id,request.guid))throw FormatError("in-place household Init unsupported; discard bootstrap");
        const auto exact=[&]()->const Entity*{
            const auto* avatar=world.resolve_reference(identity);
            if(!world.is_live_reference(identity)||!avatar||avatar->dead||avatar->kind!=EntityKind::avatar||
               !avatar->definition||avatar->definition->guid!=request.guid)
                throw FormatError("in-place household constructor identity changed; discard bootstrap");
            return avatar;
        };
        if(exact()->position)throw FormatError("in-place household Init must leave avatar out of world");
        inherit_staged_household_member(world,id,*request.neighbor,family_chunk_id);
        bool positioned=false;std::size_t attempts=0;
        for(const auto& candidate:candidates){
            ++attempts;const auto outcome=place(world,id,candidate);const auto* avatar=exact();
            if(outcome==HouseholdPositionAttemptStatus::unsupported)
                throw FormatError("in-place household placement unsupported; discard bootstrap");
            positioned=outcome==HouseholdPositionAttemptStatus::placed;
            if(positioned){
                if(!(avatar->position==std::optional<EntityPosition>{candidate}))
                    throw FormatError("in-place household placement did not publish actual selected position");
                break;
            }
            if(avatar->position)throw FormatError("in-place household blocked placement left avatar positioned");
        }
        if(!activate(world,id,request.guid,positioned))
            throw FormatError("in-place household admission failed; discard bootstrap");
        (void)exact();
        result.runtime_ids.emplace(request.guid,id);
        result.new_members.push_back({request.guid,id,positioned,attempts});
    }
    return result;
}
} // namespace odh
