#pragma once

#include "odh/saved_world.hpp"
#include "saved_portal.hpp"
#include "saved_wall_appearance.hpp"
#include "saved_interactions.hpp"
#include <array>
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <limits>

struct SavedLoadPreparation {
    std::vector<std::int16_t> entity_order;
    std::map<std::int16_t, odh::CodeOwner> code_owners;
    std::map<std::int16_t, bool> ghost_images;
    std::map<std::int16_t, std::array<std::int16_t, 20>> temporary_registers;
    odh::RoutineSet globals;
    odh::RoutineOwners owners;
    odh::TuningResources tuning;
    std::map<odh::CodeOwner,odh::DefinitionCandidate> sound_resources;
};

// Prepare the exact resource inputs for a fresh saved-lot restore. EntryPoint
// tables and code owners come from each selected OBJD asset, never from World
// IDs or archive search order. The explicit global archive supplies Global and
// linked semiglobal assets. Fresh ObjM entities use the Simitone default
// GhostImage=false; the eight saved OBJM temps are padded to the VM's 20 slots.
inline SavedLoadPreparation prepare_saved_load_entry_points(
    odh::SavedWorld& saved, const odh::DefinitionIndex& definitions,
    const std::string& global_archive, const std::string& global_entry) {
    if (!saved.world || saved.records.size() > 4096)
        throw odh::FormatError("saved Load World or record budget invalid");
    if (global_archive.empty() || global_entry.empty())
        throw odh::FormatError("saved Load requires explicit global archive and entry");

    SavedLoadPreparation result;
    result.globals = load_saved_global_code_bank(global_archive, global_entry).routines;
    result.tuning.globals = load_saved_tuning_tables(global_archive, global_entry);
    std::map<std::uint32_t, SavedEntryPointResource> resources;
    std::map<std::pair<std::string, std::string>, odh::CodeOwner> owner_ids;
    std::map<std::string, SavedSemiglobalCodeBank> semiglobals;
    odh::CodeOwner next_owner = 1;
    constexpr std::size_t owner_limit = 63; // VM default limit is 64 including owner zero.

    std::vector<const odh::SavedWorldEntity*> ordered;
    ordered.reserve(saved.records.size());
    for (const auto& record : saved.records) ordered.push_back(&record);
    std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        return left->saved.object_id < right->saved.object_id;
    });

    for (const auto* record : ordered) {
        const auto object_id = record->saved.object_id;
        if (object_id == 0 || object_id > static_cast<std::uint16_t>(std::numeric_limits<std::int16_t>::max()))
            throw odh::FormatError("saved Load object ID is outside runtime identity range");
        const auto id = static_cast<std::int16_t>(object_id);
        auto* entity = saved.world->find(id);
        if (!entity || !entity->definition || entity->definition->guid != record->saved.guid)
            throw odh::FormatError("saved Load entity and definition identity mismatch");
        const auto* candidate = definitions.unique(record->saved.guid);
        if (!candidate)
            throw odh::FormatError("saved Load definition is missing or ambiguous");

        auto resource = resources.find(record->saved.guid);
        if (resource == resources.end()) {
            auto decoded = saved_entry_point_resource(*candidate);
            if (decoded.chunk_id != candidate->chunk_id || decoded.table.entries.size() > 256)
                throw odh::FormatError("saved Load EntryPoint identity or size invalid");
            resource = resources.emplace(record->saved.guid, std::move(decoded)).first;
        }
        if (!saved.world->define_entry_points(id, resource->second.table.entries))
            throw odh::FormatError("saved Load EntryPoint table exceeds World limits");

        std::array<std::int16_t, 20> temps{};
        std::copy(record->saved.prefix.temps.begin(), record->saved.prefix.temps.end(), temps.begin());
        result.temporary_registers.emplace(id, temps);
        result.ghost_images.emplace(id, false);
        result.entity_order.push_back(id);

        const auto action = resource->second.table.entries.size() > 2
            ? resource->second.table.entries[2].action : std::uint16_t{0};
        if (action <= 255) continue;

        const auto identity = std::make_pair(candidate->source, candidate->asset);
        auto owner = owner_ids.find(identity);
        if (owner == owner_ids.end()) {
            if (owner_ids.size() >= owner_limit)
                throw odh::FormatError("saved Load distinct resource owners exceed VM limit");
            owner = owner_ids.emplace(identity, next_owner++).first;
            result.owners.try_emplace(owner->second);
            result.sound_resources.emplace(owner->second,*candidate);
            odh::TuningOwner tuning_owner;
            tuning_owner.private_tables = load_saved_tuning_tables(*candidate);
            tuning_owner.semiglobal_state = odh::TuningSemiState::absent;
            result.tuning.owners.emplace(owner->second,std::move(tuning_owner));
        }
        result.code_owners.emplace(id, owner->second);

        auto& bank = result.owners.at(owner->second);
        const auto action_key = odh::routine_key(action);
        // Simitone's immediate EntryPoint dispatch returns false when its root
        // is absent. Do not confuse that case with a missing nested call: once
        // an exact private root exists, the complete private closure is still
        // required and an unresolved call remains a preparation error.
        if (action_key.scope == odh::RoutineScope::private_object &&
            resource->second.bhavs.count(action) != 0) {
            odh::RoutineSet closure;
            try {
                closure = load_saved_action_routines(*candidate, action);
            } catch (const std::exception& error) {
                throw odh::FormatError("Load action " + std::to_string(action) + " for object " +
                    std::to_string(id) + " in " + candidate->source + "!" + candidate->asset +
                    " has an unresolved private dependency: " + error.what());
            }
            for (auto& [key, routine] : closure)
                bank.try_emplace(key, std::move(routine));
        }

        if (resource->second.semiglobal_binding &&
            !resource->second.semiglobal_binding->name.empty()) {
            const auto& name = resource->second.semiglobal_binding->name;
            auto semiglobal = semiglobals.find(name);
            if (semiglobal == semiglobals.end()) {
                try {
                    semiglobal = semiglobals.emplace(name,
                        load_saved_semiglobal_code_bank(global_archive, name + ".iff")).first;
                    auto& tuning_owner=result.tuning.owners.at(owner->second);
                    tuning_owner.semiglobal_tables=load_saved_tuning_tables(global_archive,name+".iff");
                    tuning_owner.semiglobal_state=odh::TuningSemiState::loaded;
                } catch (const std::exception& error) {
                    throw odh::FormatError("Load action " + std::to_string(action) + " for object " +
                        std::to_string(id) + " links missing semiglobal " + name + ": " + error.what());
                }
            }
            for (const auto& [key, routine] : semiglobal->second.routines)
                bank.try_emplace(key, routine);
        }
    }
    return result;
}
