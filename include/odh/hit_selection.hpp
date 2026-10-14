#pragma once

#include "odh/hit_resources.hpp"

namespace odh {
// Only the Jump operand interpretation changes. Literal32 is supported by
// static evidence from the separately supplied Sims.exe, not proven for .bak.
// Other selection semantics remain the bounded FreeSO reference profile.
enum class HitJumpOperand { FreeSoHeuristic, Literal32 };
struct HitSelectionLimits {
    std::uint32_t instructions = 4096;
    std::uint32_t trace_entries = 4096;
    std::uint32_t group_kills = 64;
    std::uint32_t choice_ids = 262144; // Maximum entries snapshotted for one chooser invocation.
};

// The callback returns an INDEX, not a patch ID. It is required for every
// selection, including a one-element list; no random generator is implicit.
// It must not mutate the supplied HIT/HOT resources through other aliases.
using HitChooser = std::function<std::size_t(std::uint32_t,
                                            const std::vector<std::uint32_t>&)>;
// Explicit signed (source, field) lookup, for integrating a World/source
// provider. nullopt means unsupported/missing, distinct from a valid zero.
// The reader must not mutate HIT/HOT inputs through aliases. Calls are bounded
// by the selection instruction budget; no result or reader is retained.
using HitSourceFieldReader = std::function<std::optional<std::int32_t>(std::int32_t,
                                                                    std::int32_t)>;
struct HitChoice {
    std::uint32_t hitlist_id = 0, patch_id = 0;
    std::size_t index = 0;
};
struct HitSelectionTrace {
    std::uint32_t pc = 0, next_pc = 0;
    std::uint8_t opcode = 0;
    bool zero_flag = false, sign_flag = false;
    std::optional<std::uint32_t> active_hitlist, selected_patch;
    std::optional<std::uint8_t> write_location;
    std::optional<std::int32_t> write_value;
    std::optional<bool> patch_replaced;
    std::optional<HitChoice> choice;
    std::optional<std::uint32_t> branch_id, branch_pc;
    std::optional<bool> branch_taken;
    std::optional<std::int32_t> source_object_value, field_index, field_value;
    std::optional<std::uint8_t> sequence_group_kill;
};
struct HitNotePlan {
    std::uint32_t patch_id = 0, pc = 0, steps = 0;
    std::uint8_t note_destination = 0;
    bool zero_flag = false, sign_flag = false;
    std::optional<std::uint32_t> active_hitlist;
    std::optional<HitChoice> initial_choice;
    std::array<std::int32_t, 16> registers{};
    std::array<std::int32_t, 54> locals{};
    std::vector<HitSelectionTrace> trace;
    // Raw constant operands, in order. These are requested effects only: no
    // existing sound, actor, sequence group or backend is changed by this API.
    std::vector<std::uint8_t> sequence_group_kills;
};

// Offline, bounded primary-FreeSO TS1 selection profile, not an audio backend
// or proof of original-game semantics. start_pc is an already resolved code
// address. Registers/local variables begin zero, except register 1 = 12.
// GetSrcDataField reads object_vars[field] directly: field 20 is index 20,
// FreeSO neatness/10, not index 14 (age). The caller supplies all 29 values.
// If source_reader is supplied, it replaces that lookup and receives BOTH
// source/field register values without the default object_vars index bound.
// Missing lookups fail explicitly; no array or zero fallback is applied.
// Only registers [0,16) and locals [16,70) can be bytecode variables here;
// unmapped and global locations are rejected rather than invented as zero.
// Every taken branch ID resolves through HOT TrackData; any corresponding HIT
// ENTP row must agree. Untaken conditional operands are consumed but not
// resolved, matching FreeSO. Addresses must be in the code
// region [16,ENTP), or [16,bytes.size()) when there is no ENTP table.
// With default FreeSoHeuristic, Jump's first byte > 15 selects a 32-bit literal.
// Otherwise it is a register ID: FreeSO remaps that register's value BEFORE
// inspecting the TARGET byte;
// a zero byte skips three target bytes, a nonzero byte leaves PC at the target.
// Literal32 instead always consumes a four-byte ID and remaps it directly;
// it never reads a register or probes/skips the destination's first bytes.
// Writes to 0x12 and decimal 50 replace the selected patch only when it exists
// in HOT, preserving the previous patch on a failed lookup, as in FreeSO.
// optional initial_track applies SetTrack's initial hitlist/patch selection;
// an explicit zero patch with a nonzero list invokes chooser before bytecode.
// The plan stops before NoteOn's audio effect and destination write. WaitSamp,
// End, other unsupported opcodes, missing resources, or budget exhaustion
// before that first NoteOn raise FormatError. There is no hidden RNG or BHAV
// state consumption. HSM/HOT parser precedence remains a separate policy.
HitNotePlan select_hit_note(const HitFile&, const Hot&, std::uint32_t start_pc,
                           const std::array<std::int32_t, 29>& object_vars,
                           const HitChooser&, const HitSelectionLimits& = {},
                           const HotTrack* initial_track = nullptr,
                           HitJumpOperand jump_operand = HitJumpOperand::FreeSoHeuristic,
                           const HitSourceFieldReader& source_reader = {});
}
