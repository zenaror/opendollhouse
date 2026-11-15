// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
namespace odh_host {
// Selection/accept/cancel state extracted from LinuxCasDraftMembers. The
// current CASScreen remains owner of its family vector and its writer.
class CasDraftState {
public:
    std::optional<std::size_t> editing()const noexcept{return editing_;}
    template<class Member>const Member& select(const std::vector<Member>& members,std::size_t index){
        const auto& member=members.at(index);editing_=index;return member;
    }
    void begin_new()noexcept{editing_.reset();}
    template<class Member>std::size_t accept(std::vector<Member>& members,Member member){
        const auto index=editing_.value_or(members.size());
        if(editing_)members.at(*editing_)=std::move(member);
        else{if(members.size()>=8)throw std::invalid_argument("CAS household limit reached");members.push_back(std::move(member));}
        editing_.reset();return index;
    }
    template<class Member>bool erase_selected(std::vector<Member>& members){
        if(!editing_)return false;members.erase(members.begin()+*editing_);editing_.reset();return true;
    }
private:
    std::optional<std::size_t> editing_;
};
}
