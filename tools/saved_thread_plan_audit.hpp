#pragma once

#include "odh/saved_thread_plan.hpp"
#include "resource.hpp"
#include "saved_resources.hpp"

#include <map>
#include <set>
#include <tuple>

struct SavedThreadCodeResource {
    std::string archive;
    std::string entry;
};

struct SavedThreadCodeCheck {
    std::string scope;
    std::string archive;
    std::string entry;
    std::size_t instruction_count;
};

inline const char* saved_thread_return_name(odh::SimitoneSavedReturnState value) {
    switch (value) {
    case odh::SimitoneSavedReturnState::unresolved: return "unresolved";
    case odh::SimitoneSavedReturnState::normal: return "normal";
    case odh::SimitoneSavedReturnState::retry: return "retry";
    case odh::SimitoneSavedReturnState::interaction: return "interaction";
    }
    return "invalid";
}

namespace saved_thread_audit_detail {
struct BhavCatalog {
    std::map<std::uint16_t, std::size_t> instruction_counts;
    std::map<std::uint16_t, std::map<std::uint16_t, std::uint16_t>> requested_opcodes;
    std::optional<odh::Glob> semiglobal_link;
};

using OpcodeTargets = std::map<std::uint16_t, std::set<std::uint16_t>>;

inline void add_chunks(const odh::View& view, BhavCatalog& result,
                       const OpcodeTargets& targets = {}) {
    odh::Limits limits;
    limits.iff_chunks = 4096;
    odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
        if (chunk.type == "GLOB") {
            if (result.semiglobal_link)
                throw odh::FormatError("ambiguous saved-thread owner GLOB link");
            result.semiglobal_link = odh::read_glob(chunk.payload);
        }
        if (chunk.type != "BHAV") return;
        if (result.instruction_counts.size() >= 4096)
            throw odh::FormatError("saved-thread BHAV catalog exceeds routine limit");
        const auto bhav = odh::read_bhav(chunk.payload);
        if (bhav.instructions.empty() || bhav.instructions.size() > 65536 ||
            !result.instruction_counts.emplace(chunk.id, bhav.instructions.size()).second)
            throw odh::FormatError("invalid or ambiguous saved-thread BHAV");
        const auto wanted = targets.find(chunk.id);
        if (wanted != targets.end()) {
            auto& opcodes = result.requested_opcodes[chunk.id];
            for (const auto pc : wanted->second)
                if (pc < bhav.instructions.size()) opcodes.emplace(pc, bhav.instructions[pc].opcode);
        }
    }, limits);
}

inline BhavCatalog from_definition(const odh::DefinitionCandidate& candidate,
                                  const OpcodeTargets& targets) {
    BhavCatalog result;
    if (candidate.asset.empty())
        with_iff_file(candidate.source, [&](const auto& view) { add_chunks(view, result, targets); });
    else
        with_far_entry(candidate.source, candidate.asset,
            [&](const auto& view) { add_chunks(view, result, targets); });
    return result;
}

inline BhavCatalog from_far_resource(const SavedThreadCodeResource& resource,
                                     const OpcodeTargets& targets) {
    BhavCatalog result;
    with_far_entry(resource.archive, resource.entry,
        [&](const auto& view) { add_chunks(view, result, targets); });
    return result;
}

inline std::string fold_ascii(std::string value) {
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}
} // namespace saved_thread_audit_detail

inline std::vector<SavedThreadCodeCheck> audit_saved_thread_plan(
    odh::SimitoneSavedThreadPlan& plan, const odh::DefinitionIndex& definitions,
    const std::optional<SavedThreadCodeResource>& semiglobal,
    const std::optional<SavedThreadCodeResource>& global) {
    using namespace saved_thread_audit_detail;
    std::map<std::uint32_t, BhavCatalog> owners;
    std::map<std::uint32_t, OpcodeTargets> owner_targets;
    OpcodeTargets global_targets;
    OpcodeTargets semiglobal_targets;
    std::optional<BhavCatalog> semiglobal_catalog;
    std::optional<BhavCatalog> global_catalog;
    std::vector<SavedThreadCodeCheck> result;
    result.reserve(plan.frames.size());

    for (const auto& use : plan.object_uses) {
        if (use.stack_length <= 1 ||
            static_cast<std::size_t>(use.stack_length) > plan.frames.size()) continue;
        const auto& caller = plan.frames[static_cast<std::size_t>(use.stack_length - 2)];
        auto& targets = caller.routine_id < 4096 ? global_targets :
            caller.routine_id < 8192 ? owner_targets[caller.code_owner_guid] : semiglobal_targets;
        targets[caller.routine_id].insert(caller.instruction_pointer);
    }

    for (const auto& frame : plan.frames) {
        const auto* candidate = frame.routine_id < 4096 ? nullptr : definitions.unique(frame.code_owner_guid);
        const BhavCatalog* selected = nullptr;
        std::string scope;
        std::string archive, entry;
        if (frame.routine_id < 4096) {
            if (!global) throw odh::FormatError("saved thread needs an explicit Global.iff source");
            if (!global_catalog) global_catalog = from_far_resource(*global, global_targets);
            selected = &*global_catalog;
            scope = "global"; archive = global->archive; entry = global->entry;
        } else if (frame.routine_id < 8192) {
            if (!candidate) throw odh::FormatError("saved thread private code owner is missing or ambiguous");
            auto found = owners.find(frame.code_owner_guid);
            if (found == owners.end()) {
                if (owners.size() >= 64)
                    throw odh::FormatError("saved-thread code-owner audit exceeds 64 resources");
                found = owners.emplace(frame.code_owner_guid,
                    from_definition(*candidate, owner_targets[frame.code_owner_guid])).first;
            }
            selected = &found->second;
            scope = "private"; archive = candidate->source; entry = candidate->asset;
        } else {
            if (!candidate) throw odh::FormatError("saved thread semiglobal owner is missing or ambiguous");
            auto found = owners.find(frame.code_owner_guid);
            if (found == owners.end()) {
                if (owners.size() >= 64)
                    throw odh::FormatError("saved-thread code-owner audit exceeds 64 resources");
                found = owners.emplace(frame.code_owner_guid,
                    from_definition(*candidate, owner_targets[frame.code_owner_guid])).first;
            }
            if (!found->second.semiglobal_link || found->second.semiglobal_link->name.empty())
                throw odh::FormatError("saved thread owner has no usable GLOB link");
            if (!semiglobal) throw odh::FormatError("saved thread needs an explicit semiglobal source");
            const auto expected = fold_ascii(found->second.semiglobal_link->name + ".iff");
            if (fold_ascii(semiglobal->entry) != expected)
                throw odh::FormatError("explicit semiglobal does not match saved thread owner's GLOB");
            if (!semiglobal_catalog) semiglobal_catalog = from_far_resource(*semiglobal, semiglobal_targets);
            selected = &*semiglobal_catalog;
            scope = "semiglobal"; archive = semiglobal->archive; entry = semiglobal->entry;
        }

        const auto routine = selected->instruction_counts.find(frame.routine_id);
        if (routine == selected->instruction_counts.end())
            throw odh::FormatError("saved thread BHAV is absent from its explicit code resource");
        if (frame.instruction_pointer >= routine->second)
            throw odh::FormatError("saved thread instruction address is outside its BHAV");
        result.push_back({scope, std::move(archive), std::move(entry), routine->second});
    }

    for (auto& frame : plan.frames) {
        frame.return_state = odh::SimitoneSavedReturnState::normal;
        frame.action_tree = false;
    }
    odh::apply_simitone_saved_return_mapping(plan, [&](const odh::SimitoneSavedFramePlan& frame)
        -> std::optional<std::uint16_t> {
        const std::map<std::uint16_t, std::map<std::uint16_t, std::uint16_t>>* routines = nullptr;
        if (frame.routine_id < 4096) {
            if (global_catalog) routines = &global_catalog->requested_opcodes;
        } else {
            const auto found = owners.find(frame.code_owner_guid);
            if (frame.routine_id < 8192) {
                if (found != owners.end()) routines = &found->second.requested_opcodes;
            } else if (semiglobal_catalog) routines = &semiglobal_catalog->requested_opcodes;
        }
        if (!routines) return {};
        const auto routine = routines->find(frame.routine_id);
        if (routine == routines->end()) return {};
        const auto opcode = routine->second.find(frame.instruction_pointer);
        if (opcode == routine->second.end()) return {};
        return opcode->second;
    });
    return result;
}

struct SavedThreadRoutineBanks {
    odh::RoutineSet global;
    odh::RoutineOwners owners;
};

// Load only the routines named by this saved stack, from the exact resources
// that passed audit_saved_thread_plan. This is enough to validate a dormant
// import; it is deliberately not a runnable BHAV dependency closure.
inline SavedThreadRoutineBanks load_saved_thread_routine_banks(
    const odh::SimitoneSavedThreadPlan& plan,
    const std::vector<SavedThreadCodeCheck>& checks) {
    if (checks.size() != plan.frames.size())
        throw odh::FormatError("saved-thread code audit does not cover every frame");
    if (plan.frames.size() > 64)
        throw odh::FormatError("saved-thread VM import exceeds the VM stack limit");

    using ResourceKey = std::tuple<std::string, std::uint32_t, std::string, std::string>;
    std::map<ResourceKey, std::set<std::uint16_t>> requested;
    for (std::size_t i = 0; i < plan.frames.size(); ++i) {
        const auto& frame = plan.frames[i];
        const auto& check = checks[i];
        const auto expected_scope = frame.routine_id < 4096 ? "global" :
            frame.routine_id < 8192 ? "private" : "semiglobal";
        if (check.scope != expected_scope || check.archive.empty() ||
            (check.scope != "private" && check.entry.empty()))
            throw odh::FormatError("saved-thread audit resource does not match the frame scope");
        if (frame.code_owner_guid == 0)
            throw odh::FormatError("saved-thread frame has no resource code-owner GUID");
        const auto resource_owner = check.scope == "global" ? 0U : frame.code_owner_guid;
        requested[{check.scope, resource_owner, check.archive, check.entry}]
            .insert(frame.routine_id);
    }

    SavedThreadRoutineBanks banks;
    if (requested.size() > 64)
        throw odh::FormatError("saved-thread code bank count exceeds the import budget");
    std::size_t total_instructions = 0;
    constexpr std::size_t instruction_limit = 65536;
    const auto load_group = [&](const ResourceKey& key, const std::set<std::uint16_t>& ids,
                                odh::RoutineSet& destination) {
        const auto& scope = std::get<0>(key);
        const auto expected_scope = scope == "global" ? odh::RoutineScope::global :
            scope == "private" ? odh::RoutineScope::private_object : odh::RoutineScope::semiglobal;
        std::set<std::uint16_t> found;
        const auto scan = [&](const odh::View& view) {
            odh::Limits limits;
            limits.iff_chunks = 4096;
            odh::iff_chunks(view, [&](const odh::Chunk& chunk) {
                if (chunk.type != "BHAV" || !ids.count(chunk.id)) return;
                const auto routine = odh::routine_key(chunk.id);
                if (routine.scope != expected_scope || !found.insert(chunk.id).second)
                    throw odh::FormatError("saved-thread routine is duplicated or in the wrong scope");
                if (destination.size() >= 64)
                    throw odh::FormatError("saved-thread VM routine count exceeds the import budget");
                auto bhav = odh::read_bhav(chunk.payload);
                if (bhav.instructions.size() > instruction_limit - total_instructions)
                    throw odh::FormatError("saved-thread VM code exceeds the instruction budget");
                total_instructions += bhav.instructions.size();
                if (!destination.emplace(routine, std::move(bhav)).second)
                    throw odh::FormatError("saved-thread routine collides with an installed bank");
            }, limits);
        };
        const auto& archive = std::get<2>(key);
        const auto& entry = std::get<3>(key);
        if (scope == "private" && entry.empty()) with_iff_file(archive, scan);
        else with_far_entry(archive, entry, scan);
        if (found.size() != ids.size())
            throw odh::FormatError("audited saved-thread routine disappeared from its resource");
    };

    for (const auto& group : requested) {
        const auto& scope = std::get<0>(group.first);
        const auto owner = std::get<1>(group.first);
        auto& destination = scope == "global" ? banks.global : banks.owners[owner];
        load_group(group.first, group.second, destination);
    }
    // Global frames retain their OBJT-derived owner in VmContext. The VM
    // requires that owner to be registered even though globals use bank zero.
    for (const auto& frame : plan.frames)
        if (frame.routine_id < 4096) banks.owners.try_emplace(frame.code_owner_guid);
    if (banks.owners.size() >= 64)
        throw odh::FormatError("saved-thread code owners exceed the VM owner limit");
    return banks;
}
