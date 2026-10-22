#pragma once

#include "odh/saved_thread_plan.hpp"
#include "resource.hpp"
#include "saved_resources.hpp"

#include <map>

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

namespace saved_thread_audit_detail {
struct BhavCatalog {
    std::map<std::uint16_t, std::size_t> instruction_counts;
    std::optional<odh::Glob> semiglobal_link;
};

inline void add_chunks(const odh::View& view, BhavCatalog& result) {
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
    }, limits);
}

inline BhavCatalog from_definition(const odh::DefinitionCandidate& candidate) {
    BhavCatalog result;
    if (candidate.asset.empty())
        with_iff_file(candidate.source, [&](const auto& view) { add_chunks(view, result); });
    else
        with_far_entry(candidate.source, candidate.asset,
            [&](const auto& view) { add_chunks(view, result); });
    return result;
}

inline BhavCatalog from_far_resource(const SavedThreadCodeResource& resource) {
    BhavCatalog result;
    with_far_entry(resource.archive, resource.entry,
        [&](const auto& view) { add_chunks(view, result); });
    return result;
}

inline std::string fold_ascii(std::string value) {
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}
} // namespace saved_thread_audit_detail

inline std::vector<SavedThreadCodeCheck> audit_saved_thread_plan(
    const odh::SimitoneSavedThreadPlan& plan, const odh::DefinitionIndex& definitions,
    const std::optional<SavedThreadCodeResource>& semiglobal,
    const std::optional<SavedThreadCodeResource>& global) {
    using namespace saved_thread_audit_detail;
    std::map<std::uint32_t, BhavCatalog> owners;
    std::optional<BhavCatalog> semiglobal_catalog;
    std::optional<BhavCatalog> global_catalog;
    std::vector<SavedThreadCodeCheck> result;
    result.reserve(plan.frames.size());

    for (const auto& frame : plan.frames) {
        const auto candidate = definitions.unique(frame.code_owner_guid);
        const BhavCatalog* selected = nullptr;
        std::string scope;
        std::string archive, entry;
        if (frame.routine_id < 4096) {
            if (!global) throw odh::FormatError("saved thread needs an explicit Global.iff source");
            if (!global_catalog) global_catalog = from_far_resource(*global);
            selected = &*global_catalog;
            scope = "global"; archive = global->archive; entry = global->entry;
        } else if (frame.routine_id < 8192) {
            if (!candidate) throw odh::FormatError("saved thread private code owner is missing or ambiguous");
            auto found = owners.find(frame.code_owner_guid);
            if (found == owners.end()) {
                if (owners.size() >= 64)
                    throw odh::FormatError("saved-thread code-owner audit exceeds 64 resources");
                found = owners.emplace(frame.code_owner_guid, from_definition(*candidate)).first;
            }
            selected = &found->second;
            scope = "private"; archive = candidate->source; entry = candidate->asset;
        } else {
            if (!candidate) throw odh::FormatError("saved thread semiglobal owner is missing or ambiguous");
            auto found = owners.find(frame.code_owner_guid);
            if (found == owners.end()) {
                if (owners.size() >= 64)
                    throw odh::FormatError("saved-thread code-owner audit exceeds 64 resources");
                found = owners.emplace(frame.code_owner_guid, from_definition(*candidate)).first;
            }
            if (!found->second.semiglobal_link || found->second.semiglobal_link->name.empty())
                throw odh::FormatError("saved thread owner has no usable GLOB link");
            if (!semiglobal) throw odh::FormatError("saved thread needs an explicit semiglobal source");
            const auto expected = fold_ascii(found->second.semiglobal_link->name + ".iff");
            if (fold_ascii(semiglobal->entry) != expected)
                throw odh::FormatError("explicit semiglobal does not match saved thread owner's GLOB");
            if (!semiglobal_catalog) semiglobal_catalog = from_far_resource(*semiglobal);
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
    return result;
}
