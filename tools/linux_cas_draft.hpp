#pragma once

#include "linux_simitone_cas_member.hpp"

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
    std::optional<std::size_t> editing() const noexcept { return editing_; }
    const SimitoneCasPersonInput& select(std::size_t index) {
        const auto& member = members_.at(index);
        editing_ = index;
        return member;
    }
    void begin_new() noexcept { editing_.reset(); }
    void accept(SimitoneCasPersonInput member) {
        if (editing_) members_.at(*editing_) = std::move(member);
        else {
            if (members_.size() >= 8) throw std::invalid_argument("CAS household limit reached");
            members_.push_back(std::move(member));
        }
        editing_.reset();
    }
    bool erase_selected() {
        if (!editing_) return false;
        members_.erase(members_.begin() + *editing_);
        editing_.reset();
        return true;
    }

private:
    std::vector<SimitoneCasPersonInput> members_;
    std::optional<std::size_t> editing_;
};

} // namespace odh_host
