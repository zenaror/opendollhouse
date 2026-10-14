#pragma once
#include "odh/objm_prefix.hpp"
#include "odh/object_types.hpp"

namespace odh {
struct SavedObjectPrefix {
    std::uint16_t object_id,type_id;
    std::uint32_t guid;
    ObjmPrefix prefix;
};
struct SavedPrefixLimits {
    std::uint32_t objects=4096;
    std::uint64_t words=65536; // attrs +8 temps +68 ObjectData +5 extras.
    std::uint32_t attributes_per_object=4096;
};
// Decode and validate a complete owned prefix snapshot before returning it.
// Block order is retained. ObjectData11 resolves unsigned ID -> map type ->
// one-based serialized objt entry. No lookup by the entry's raw type_id field.
// Every ID must appear once and resolve a nonzero GUID; this does not resolve
// that GUID to OBJD or decode the remaining variable instance state.
std::vector<SavedObjectPrefix> read_saved_object_prefixes(const Objm&,const Objt&,
    const SavedPrefixLimits& = {});
}
