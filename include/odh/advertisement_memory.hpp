// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <cstddef>
#include <map>
#include <optional>
#include <functional>
namespace odh {
struct VmFrame;struct VmMemory;
using AdvertisementChanges=std::optional<std::map<std::uint32_t,std::int16_t>>;
using AdvertisementFallback=std::function<std::optional<std::int16_t>(const VmFrame&,std::uint8_t,std::uint16_t)>;
constexpr std::size_t advertisement_key_limit=16384;
std::optional<std::uint8_t> advertisement_scope_type(std::uint8_t) noexcept;
bool advertisement_changes_valid(const AdvertisementChanges&,std::size_t limit=advertisement_key_limit) noexcept;
// Missing callback proves no ActiveAction; callback nullopt means unavailable authority.
std::optional<std::int16_t> read_advertisement(const AdvertisementChanges&,const VmFrame&,
    std::uint8_t type,std::uint16_t index,const AdvertisementFallback& = {});
bool write_advertisement(AdvertisementChanges&,std::uint8_t type,std::uint16_t index,std::int16_t value,
    std::size_t limit=advertisement_key_limit);
// State and captured services must outlive the returned memory adapter.
VmMemory with_advertisement_memory(VmMemory,AdvertisementChanges&,AdvertisementFallback = {},
    std::size_t limit=advertisement_key_limit);
}
