#include "odh/hit_selection.hpp"
#include <limits>

namespace odh {
namespace {
// C# unchecked int32 bit conversion/subtraction without C++ signed overflow
// or implementation-defined conversion of an out-of-range unsigned integer.
std::int32_t signed_bits(std::uint32_t bits) {
    if (bits <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return static_cast<std::int32_t>(bits);
    return -1 - static_cast<std::int32_t>(~bits);
}
class Selection {
public:
    Selection(const HitFile& file, const Hot& resources, std::uint32_t start,
              const std::array<std::int32_t, 29>& vars, const HitChooser& choice,
              const HitSelectionLimits& bounds, HitJumpOperand operand)
        : hit(file), hot(resources), object_vars(vars), chooser(choice), limits(bounds), jump_operand(operand), pc(start) {
        if (operand != HitJumpOperand::FreeSoHeuristic && operand != HitJumpOperand::Literal32)
            fail("unsupported Jump operand profile");
        if (hit.major_version != 1 || (hit.minor_version != 0 && hit.minor_version != 8))
            fail("unsupported HIT version");
        if (hit.bytes.size() > std::numeric_limits<std::uint32_t>::max()) fail("HIT too large");
        code_end = hit.entry_table_offset.value_or(hit.bytes.size());
        if (code_end > hit.bytes.size() || code_end <= 16) fail("invalid HIT code region");
        address(pc);
        plan.registers[1] = 12;
    }
    HitNotePlan run(const HotTrack* track) {
        if (track) initialize(*track);
        for (;;) {
            instruction_pc.reset();
            if (plan.steps >= limits.instructions) fail("instruction budget exhausted before NoteOn");
            if (plan.trace.size() >= limits.trace_entries) fail("trace budget exhausted before NoteOn");
            current = {};
            current.pc = pc;
            instruction_pc = pc;
            current.opcode = byte();
            ++plan.steps;
            const auto op = current.opcode;
            switch (op) {
            case 2: {
                const auto dest = byte();
                variable(dest);
                if (!patch || hot.patches.find(*patch) == hot.patches.end()) fail("NoteOn has no valid selected patch");
                plan.patch_id = *patch;
                plan.pc = current.pc;
                plan.note_destination = dest;
                append();
                plan.zero_flag = zero;
                plan.sign_flag = sign;
                plan.active_hitlist = list;
                return plan;
            }
            case 4: {
                const auto dest = byte();
                const auto bits = byte();
                const auto value = bits < 128 ? static_cast<std::int32_t>(bits) : static_cast<std::int32_t>(bits) - 256;
                write(dest, value);
                flags(value);
                break;
            }
            case 5: {
                const auto dest = byte();
                const auto value = signed_bits(word());
                write(dest, value);
                flags(value);
                break;
            }
            case 6: {
                const auto dest = byte();
                const auto value = read(byte());
                write(dest, value);
                flags(value);
                break;
            }
            case 13: {
                if (jump_operand == HitJumpOperand::Literal32) {
                    const auto id = word();
                    pc = remap(id);
                    current.branch_id = id;
                    current.branch_pc = pc;
                    current.branch_taken = true;
                    break;
                }
                const auto first = byte();
                std::uint32_t id;
                if (first > 15) {
                    --pc;
                    id = word();
                    pc = remap(id);
                } else {
                    id = static_cast<std::uint32_t>(read(first));
                    pc = remap(id);
                    // This probe reads at the remapped destination, not the
                    // original operand. Preserve the pinned source's odd rule.
                    if (byte() == 0) {
                        if (pc > std::numeric_limits<std::uint32_t>::max() - 2) fail("Jump address overflow");
                        pc += 2;
                        address(pc);
                    } else --pc;
                }
                current.branch_id = id;
                current.branch_pc = pc;
                current.branch_taken = true;
                break;
            }
            case 14: flags(read(byte())); break;
            case 20: {
                const auto a = read(byte());
                const auto b = read(byte());
                flags(signed_bits(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)));
                break;
            }
            case 39: {
                const auto dest = byte();
                variable(dest); // Reject before calling the chooser.
                const auto choice = choose();
                current.choice = choice;
                write(dest, signed_bits(choice.patch_id));
                break; // SmartChoose preserves zero/sign flags.
            }
            case 62: case 63: case 64: case 65: case 66: case 67: {
                const auto id = word();
                const bool take = op == 62 ? zero : op == 63 ? !zero : op == 64 ? !sign && !zero :
                                  op == 65 ? sign : op == 66 ? !sign : sign || zero;
                current.branch_id = id;
                current.branch_taken = take;
                if (take) {
                    pc = remap(id);
                    current.branch_pc = pc;
                }
                break;
            }
            case 68: load_list(static_cast<std::uint32_t>(read(byte()))); break;
            case 69: {
                const auto group = byte();
                if (plan.sequence_group_kills.size() >= limits.group_kills) fail("sequence-group request budget exhausted");
                plan.sequence_group_kills.push_back(group);
                current.sequence_group_kill = group;
                break;
            }
            case 72: {
                const auto dest = byte();
                const auto source = read(byte());
                const auto field = read(byte());
                variable(dest);
                if (field < 0 || static_cast<std::uint32_t>(field) >= object_vars.size()) fail("GetSrcDataField index outside object variables");
                const auto value = object_vars[static_cast<std::size_t>(field)];
                current.source_object_value = source; // Source read is required, even though FreeSO ignores its value.
                current.field_index = field;
                current.field_value = value;
                write(dest, value);
                flags(value);
                break;
            }
            case 11: fail("WaitSamp reached before first NoteOn");
            case 12: fail("End reached before first NoteOn");
            default: fail("unsupported HIT opcode " + std::to_string(op));
            }
            append();
        }
    }
private:
    const HitFile& hit;
    const Hot& hot;
    const std::array<std::int32_t, 29>& object_vars;
    const HitChooser& chooser;
    const HitSelectionLimits& limits;
    HitJumpOperand jump_operand;
    std::uint32_t pc;
    std::optional<std::uint32_t> instruction_pc;
    std::uint64_t code_end = 0;
    bool zero = false, sign = false;
    std::optional<std::uint32_t> list, patch;
    HitNotePlan plan;
    HitSelectionTrace current;
    [[noreturn]] void fail(const std::string& reason) const {
        throw FormatError("HIT selection at PC " + std::to_string(instruction_pc.value_or(pc)) +
                          " (cursor " + std::to_string(pc) + "): " + reason);
    }
    void address(std::uint32_t value) const {
        if (value < 16 || value >= code_end) fail("address outside HIT code region: " + std::to_string(value));
    }
    std::uint8_t byte() {
        address(pc);
        return hit.bytes[pc++];
    }
    std::uint32_t word() {
        std::uint32_t result = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) result |= static_cast<std::uint32_t>(byte()) << shift;
        return result;
    }
    void variable(std::uint8_t location) const {
        if (location >= 70) fail("unsupported variable location " + std::to_string(location));
    }
    std::int32_t read(std::uint8_t location) const {
        variable(location);
        return location < 16 ? plan.registers[location] : plan.locals[location - 16];
    }
    void write(std::uint8_t location, std::int32_t value) {
        variable(location);
        if (location == 0x12 || location == 50) {
            const auto id = static_cast<std::uint32_t>(value);
            const bool exists = hot.patches.find(id) != hot.patches.end();
            current.patch_replaced = exists;
            if (exists) patch = id;
        }
        if (location < 16) plan.registers[location] = value;
        else plan.locals[location - 16] = value;
        current.write_location = location;
        current.write_value = value;
    }
    void flags(std::int32_t value) { zero = value == 0; sign = value < 0; }
    std::uint32_t remap(std::uint32_t id) const {
        const auto entry = hot.track_data.find(id);
        if (entry == hot.track_data.end()) fail("missing HOT TrackData branch ID " + std::to_string(id));
        const auto hit_entry = hit.entry_points.find(id);
        if (hit_entry != hit.entry_points.end() && hit_entry->second != entry->second) fail("HOT TrackData and HIT ENTP disagree for branch ID " + std::to_string(id));
        address(entry->second);
        return entry->second;
    }
    void load_list(std::uint32_t id) {
        const auto entry = hot.hitlists.find(id);
        if (entry == hot.hitlists.end()) fail("missing HOT hitlist " + std::to_string(id));
        if (!entry->second.unsupported_reason.empty()) fail("unsupported HOT hitlist: " + entry->second.unsupported_reason);
        if (entry->second.ids.empty()) fail("empty HOT hitlist " + std::to_string(id));
        list = id;
    }
    HitChoice choose() const {
        if (!list) fail("SmartChoose has no active hitlist");
        if (!chooser) fail("hitlist selection requires an explicit chooser");
        const auto found = hot.hitlists.find(*list);
        if (found == hot.hitlists.end() || found->second.ids.empty() || !found->second.unsupported_reason.empty()) fail("active HOT hitlist unavailable");
        if (found->second.ids.size() > limits.choice_ids) fail("hitlist choice entry budget exhausted");
        // Snapshot entries across callback invocation; callback may not mutate
        // resource aliases; no borrowed entry is accessed after the callback.
        const auto ids = found->second.ids;
        const auto index = chooser(*list, ids);
        if (index >= ids.size()) fail("chooser returned an out-of-range index");
        return {*list, ids[index], index};
    }
    void initialize(const HotTrack& track) {
        if (!track.unsupported_reason.empty()) fail("unsupported initial HOT track: " + track.unsupported_reason);
        if (!track.hitlist_id || !track.patch_id) fail("initial HOT track has unresolved hitlist/patch");
        if (*track.hitlist_id != 0) load_list(*track.hitlist_id);
        auto id = *track.patch_id;
        if (id == 0 && list) {
            plan.initial_choice = choose();
            id = plan.initial_choice->patch_id;
        }
        if (hot.patches.find(id) != hot.patches.end()) patch = id;
    }
    void append() {
        current.next_pc = pc;
        current.zero_flag = zero;
        current.sign_flag = sign;
        current.active_hitlist = list;
        current.selected_patch = patch;
        plan.trace.push_back(current);
    }
};
}
HitNotePlan select_hit_note(const HitFile& hit, const Hot& hot, std::uint32_t start,
                           const std::array<std::int32_t, 29>& vars,
                           const HitChooser& chooser, const HitSelectionLimits& limits,
                           const HotTrack* track, HitJumpOperand jump_operand) {
    return Selection(hit, hot, start, vars, chooser, limits, jump_operand).run(track);
}
}
