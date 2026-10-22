#pragma once

#include "odh/saved_thread_plan.hpp"
#include "resource.hpp"
#include "saved_resources.hpp"

#include <map>
#include <set>

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
