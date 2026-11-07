// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/vm.hpp"
#include <map>

namespace odh {
class World;
// Native adaptation of FreeSO7e76 TS1NeighbourProvider.Get/SetTATT,
// VMMemory scopes36/37 and tso.files/Formats/IFF/Chunks/TTAT.cs.
struct TypeAttributesSnapshot {
    std::uint32_t version=1;
    std::map<std::uint32_t,std::vector<std::int16_t>> by_guid;
};
// Admission budgets, not source-format maxima or a console memory claim.
struct TypeAttributesLimits {
    std::size_t types=4096,words=65536,words_per_type=4096;
    std::uint64_t payload_bytes=16u*1024u*1024u;
};
TypeAttributesSnapshot read_tatt(const View&,TypeAttributesLimits={});
class TypeAttributes {
public:
    explicit TypeAttributes(TypeAttributesSnapshot={},TypeAttributesLimits={});
    TypeAttributesSnapshot snapshot() const {return state_;}
    void restore(const TypeAttributesSnapshot&);
    std::optional<std::int16_t> read(std::uint32_t guid,std::int16_t index) const;
    // resource_exists must query the TypeAttrGUID itself, not the instance GUID.
    // Source absent-resource or index>=row-length writes succeed without effect.
    bool write(std::uint32_t guid,std::int16_t index,std::int16_t value,bool resource_exists);
    bool changed() const noexcept {return changed_;}
private:
    TypeAttributesSnapshot state_;
    TypeAttributesLimits limits_;
    std::size_t words_=0;
    bool changed_=false;
};
using TypeAttributeGuidResolver=std::function<std::optional<std::uint32_t>(std::uint32_t)>;
using TypeAttributeResourceExists=std::function<bool(std::uint32_t)>;
// Only36/37; caller composes with other memory scopes. The resolver maps the
// exact master-or-object resource GUID to its source-derived TypeAttrGUID.
VmMemory type_attribute_memory(const World&,TypeAttributes&,
    TypeAttributeGuidResolver,TypeAttributeResourceExists);
std::vector<std::uint8_t> encode_type_attributes_snapshot(const TypeAttributesSnapshot&,TypeAttributesLimits={});
TypeAttributesSnapshot decode_type_attributes_snapshot(const std::vector<std::uint8_t>&,TypeAttributesLimits={});
}
