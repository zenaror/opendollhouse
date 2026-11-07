// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/lot_context.hpp"
#include "odh/ts1_simulation.hpp"
#include "odh/world.hpp"
namespace odh {
enum class Ts1GlobalActivation { saved_new, legacy_old };
struct RuntimeGlobalsSnapshot {
    std::vector<std::optional<std::int16_t>> words;
    // False means SIMI selected-object IDs have not been mapped to this World.
    bool selected_sim_is_runtime_id=false;
};
struct RuntimeGlobalAvatar {std::int16_t id=0;std::uint64_t generation=0;};
// TS1 SIMI projects its 32/64 source words into exactly38 runtime words.
// Clock reads overlay writable shadows; source words38..63 are not runtime globals.
// The bank is not a World entity and does not advance time or choose an avatar.
class RuntimeGlobals {
public:
    RuntimeGlobals(const std::vector<std::int16_t>& simi,Ts1GlobalActivation,
                   std::int16_t selected_lot);
    // Explicit migration of an older native sparse bank. Absent values remain
    // unknown; the caller must prove any reconstructed runtime initialization.
    static RuntimeGlobals from_legacy(const LotContext::Snapshot&);
    std::optional<std::int16_t> read(std::int16_t,const Ts1Clock* clock=nullptr) const;
    bool write(std::int16_t,std::int16_t);
    bool resolve_imported_selection(const World&,std::int16_t source_id,RuntimeGlobalAvatar);
    bool select_avatar(const World&,RuntimeGlobalAvatar);
    void clear_selection();
    RuntimeGlobalsSnapshot snapshot() const {return state_;}
    bool restore(const RuntimeGlobalsSnapshot&);
    std::size_t extent() const noexcept {return state_.words.size();}
private:
    explicit RuntimeGlobals(RuntimeGlobalsSnapshot state):state_(std::move(state)){}
    RuntimeGlobalsSnapshot state_;
};
}
