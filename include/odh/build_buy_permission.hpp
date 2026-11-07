// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/vm.hpp"

namespace odh {
struct BuildBuyPermissionSnapshot { bool enabled=true; };
// FreeSO7e76 VMArchitecture.BuildBuyEnabled; Generic10/11 changes this single
// authority and emits TS1BuildBuyChange on every call, even if unchanged.
class BuildBuyPermission {
public:
    using Notify=std::function<void(bool)>;
    bool enabled() const noexcept {return enabled_;}
    BuildBuyPermissionSnapshot snapshot() const noexcept {return {enabled_};}
    void restore(BuildBuyPermissionSnapshot value) noexcept {enabled_=value.enabled;}
    bool apply_generic(std::uint8_t mode,const Notify& notify={});
private:
    bool enabled_=true;
};
VmPrimitive build_buy_permission_primitive(BuildBuyPermission&,const BuildBuyPermission::Notify& = {});
std::vector<std::uint8_t> encode_build_buy_permission(BuildBuyPermissionSnapshot);
BuildBuyPermissionSnapshot decode_build_buy_permission(const std::vector<std::uint8_t>&);
}
