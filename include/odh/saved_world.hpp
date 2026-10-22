#pragma once

#include "odh/definition_index.hpp"
#include "odh/objm_tail.hpp"
#include "odh/relationship.hpp"
#include "odh/saved_objects.hpp"
#include <memory>

namespace odh {
struct SavedWorldEntity {
    SavedObjectPrefix saved;
    ObjmCommonState common;
    ObjmTailState tail;
    ObjectDefinition definition;
    std::uint16_t serialized_object_type; // OBJT metadata; selects person2/portal8.
};
struct SavedMultitileGroup {
    std::int16_t leader;
    std::optional<std::uint32_t> master_guid;
    std::vector<std::int16_t> members;
};
struct SavedWorldLimits {
    WorldLimits world;
    RelationshipLimits relationships;
    // Logical scalar/array fields including cursor metadata; not a measured RSS.
    std::uint64_t retained_words = 65536;
    std::uint64_t retained_string_bytes = 256U * 1024U;
    std::uint64_t retained_suffix_bytes = 8U * 1024U * 1024U;
};
// Owns the complete decoded source records (including temps, extras, raw float
// banks, linked slots and opaque suffixes). Group membership does not execute
// placement or construct a renderer. Saved stacks/queues remain dormant.
struct SavedWorld {
    std::unique_ptr<World> world;
    std::unique_ptr<RelationshipState> relationships;
    std::vector<SavedWorldEntity> records;
    std::vector<SavedMultitileGroup> multitile_groups;
};
// A full snapshot is validated before any candidate World is created. Invalid
// metadata/references/budgets throw FormatError; allocation exceptions propagate.
// No partially built World is returned. neighbors, if supplied, are copied by
// RelationshipState; no caller-owned resource or record pointers are retained.
// Paired (-16,-16) remains out-of-world (runtime position absent); other saved
// coordinates are already sixteenth-tile units. LinkedSlot is not runtime slot0.
// Kind follows the OBJT-selected person branch, as in the pinned activator;
// both OBJT type and OBJD object_type are retained even if they differ.
std::unique_ptr<SavedWorld> make_saved_world(std::vector<SavedWorldEntity>,
    const DefinitionIndex&, const NeighborContext* neighbors = nullptr,
    const SavedWorldLimits& = {});
// Integer-only IEEE binary32 to signed16 adaptation: finite values rounded to
// nearest with ties to even, then range checked. Independent of host fenv.
std::int16_t saved_motive_word(std::uint32_t bits);
} // namespace odh
