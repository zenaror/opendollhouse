#pragma once

#include "odh/formats.hpp"

namespace odh {

struct EntryPoint {
    std::uint16_t condition;
    std::uint16_t action;
};

enum class EntryPointSource { objd_legacy, objf };

struct EntryPointTable {
    std::uint32_t version;
    std::optional<std::uint32_t> reserved; // OBJf header only; absent for OBJD.
    std::vector<EntryPoint> entries;
    std::uint64_t remaining_bytes;
    EntryPointSource source;
};

struct EntryPointLimits {
    std::uint32_t entries = 256;
    std::uint64_t payload_bytes = 64 * 1024;
    std::uint64_t decoded_bytes = 1024; // Four decoded bytes per entry.
};

// OBJD138 prefix through Repair (162 bytes), requiring UsesFnTable == 0.
// Generates all 33 legacy entries according to the pinned FreeSO mapping.
// Every condition and actions 30..32 are defined generated defaults of zero.
// remaining_bytes exposes the uninterpreted tail; this is not full OBJD validation.
EntryPointTable read_objd_entry_points(const View&);

// Bounded structural OBJf reader: reserved, version, fJBO, count, LE16 pairs.
// Retains the raw version without imposing a semantic version whitelist.
// Does not generate, execute, or assign meaning to the returned entry ids.
EntryPointTable read_objf(const View&, const EntryPointLimits& = {});

} // namespace odh
