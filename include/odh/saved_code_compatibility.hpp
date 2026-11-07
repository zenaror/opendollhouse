#pragma once

#include "odh/object_types.hpp"

namespace odh {

// Bound to ONE authenticated BehaviorFinder context. All IDs (including calls
// from global/semiglobal trees) are resolved in that same context. A null tree
// means absent, not an unsupported parser format. Exceptions propagate.
using SavedCodeResolver = std::function<const Bhav*(std::uint16_t)>;
struct SavedCodeSignatureLimits {
    std::uint32_t routine_ids = 65536;
    std::uint64_t instructions = 4U * 1024U * 1024U;
};
enum class SavedCodeSignatureState {
    computed, missing_resolver, unsupported_format, routine_limit, instruction_limit
};
struct SavedCodeSignature {
    SavedCodeSignatureState state = SavedCodeSignatureState::missing_resolver;
    // Valid only for computed; unsupported results never publish a partial sum.
    std::uint32_t value = 0;
    std::uint32_t visited_ids = 0; // Includes missing IDs, each at most once.
    std::uint64_t examined_instructions = 0;
    std::uint16_t problem_id = 0;
    std::uint16_t problem_format = 0;
};
SavedCodeSignature compute_saved_code_signature(std::uint16_t root,
    const SavedCodeResolver&, const SavedCodeSignatureLimits& = {});

struct SavedCodePair { std::uint32_t init = 0, main = 0; };
// Adapter resolves actual Init/Main entries via legacy OBJD or OBJf BEFORE
// supplying this record. supported=false includes absent/ambiguous definitions
// or unavailable entry metadata; it must not fabricate root IDs from GUIDs.
struct SavedCodeResources {
    bool supported = false;
    std::uint16_t init_root = 0, main_root = 0;
    SavedCodeResolver resolve;
};
enum class SavedCodeTypeState {
    matched, mismatched, sentinel, invalid_type, unsupported_objt_version,
    missing_metadata, unsupported_resources, invalid_entry_root, missing_required_root, unsupported_signature
};
struct SavedCodeTypeComparison {
    std::uint16_t serialized_type = 0;
    std::uint32_t guid = 0;
    SavedCodeTypeState state = SavedCodeTypeState::invalid_type;
    SavedCodePair saved{}, current{};
    SavedCodeSignature init{}, main{};
    bool special_guid = false;
    // Native special type resets even with global recovery disabled; final Init
    // belongs only to an enabled signature mismatch (0x431edd).
    bool recovery_required = false;
    bool final_init_required = false;
};
// OBJT versions are signed32 in the native reader; <1 is explicitly unsupported.
// No ObjectVersion input: it is not the signature-compatibility trigger.
SavedCodeTypeComparison compare_saved_code_type(const Objt&, std::uint16_t type,
    const SavedCodeResources&, bool recovery_enabled = true,
    const SavedCodeSignatureLimits& = {});

using SavedCodeResourceProvider =
    std::function<SavedCodeResources(std::uint16_t type, std::uint32_t guid)>;
struct SavedCodePreflightLimits {
    std::uint32_t types = 4096; // Includes every GUID-zero sentinel.
    SavedCodeSignatureLimits signature{};
    // Total examinations across both roots of all types; limits total work even
    // when many types resolve the same large tree graph in distinct contexts.
    std::uint64_t instructions = 16U * 1024U * 1024U;
    std::uint64_t routine_ids = 1024U * 1024U;
};
enum class SavedCodePreflightState { complete, unsupported, type_limit, work_limit };
struct SavedCodePreflight {
    SavedCodePreflightState state = SavedCodePreflightState::unsupported;
    // Exactly serialized order including GUID0. No rows returned on a table
    // limit failure; unsupported rows stay explicit and never become matches.
    std::vector<SavedCodeTypeComparison> types;
    std::uint64_t examined_instructions = 0, visited_ids = 0;
    std::uint32_t mismatched_types = 0, recovery_types = 0;
};
// Pure preflight only: no world access, script execution or recovery callbacks.
// Visits all entries unless a bounded-work limit is reached. Missing resources
// do not prevent later types from being diagnosed.
SavedCodePreflight preflight_saved_code_compatibility(const Objt&,
    const SavedCodeResourceProvider&, bool recovery_enabled = true,
    const SavedCodePreflightLimits& = {});

} // namespace odh
