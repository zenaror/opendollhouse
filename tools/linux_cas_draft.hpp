#pragma once

#include "linux_simitone_cas_member.hpp"
#include "cas_draft_state.hpp"

#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace odh_host {

// Simitone TS1CASScreen's WIPFamily/EditIndex: edits replace the selected
// member; only new members consume a household place. No player files exist
// at this stage. The screen validates form/catalog choices before accepting.
class LinuxCasDraftMembers {
public:
    const std::vector<SimitoneCasPersonInput>& members() const noexcept { return members_; }
    std::optional<std::size_t> editing() const noexcept { return state_.editing(); }
    const SimitoneCasPersonInput& select(std::size_t index) { return state_.select(members_,index); }
    void begin_new() noexcept { state_.begin_new(); }
    void accept(SimitoneCasPersonInput member) { state_.accept(members_,std::move(member)); }
    bool erase_selected() { return state_.erase_selected(members_); }

private:
    std::vector<SimitoneCasPersonInput> members_;
    CasDraftState state_;
};

} // namespace odh_host
