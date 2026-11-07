#pragma once
#include "odh/neighborhood.hpp"
#include "odh/vm.hpp"
namespace odh {
class World;
class NativePersonalState;
enum class NeighborDataSource {serialized,legacy_zero_extension,missing_neighbor_zero,absent_person_data_zero};
struct NeighborDataValue {std::int16_t neighbor,index,value;NeighborDataSource source;};
struct NeighborNext {std::int16_t current,result;bool success;};
// Immutable TS1 neighborhood data, independent of instantiated World entities.
class NeighborContext {
public:
    explicit NeighborContext(Nbrs);
    const Nbrs& source() const{return source_;}
    // Owner must outlive this view; binding never changes the immutable seed.
    void bind_personal_state(const NativePersonalState* owner) {personal_=owner;}
    std::size_t active_count() const{return active_.size();}
    const NeighborRecord* by_id(std::int16_t id) const;
    // TS1 scope24 field0 only: first live non-ghost avatar with this neighbor's
    // exact GUID, in ascending ObjectID order (FreeSO7e76 VMMemory304-318).
    // Missing record/instance returns0. An earlier avatar lacking its resource
    // identity makes the first-match result unprovable and returns nullopt.
    std::optional<std::int16_t> instance_id(const World&,std::int16_t neighbor) const;
    // Default reference mapping: last active serialized record for this GUID.
    std::optional<std::int16_t> id_for_guid(std::uint32_t guid) const;
    // Raw provider result; -1 means end. Negative results also branch false in
    // SetToNext, even if they came from a serialized signed neighbor ID.
    std::int16_t next(std::int16_t current) const;
    // TS1 SetToNext/NeighborOfType (mode 7): next signed neighbor ID carrying
    // the requested object GUID, or -1 when no matching record remains.
    std::int16_t next_of_guid(std::int16_t current,std::uint32_t guid) const;
    std::optional<NeighborDataValue> person_data(std::int16_t neighbor,std::int16_t index) const;
    VmPrimitive primitive(VmMemory memory={},std::function<void(const NeighborNext&)> observed={}) const;
private:
    struct Ordered {std::int16_t id;std::size_t serialized_index;};
    Nbrs source_;
    const NativePersonalState* personal_=nullptr;
    std::vector<Ordered> ordered_;
    std::map<std::int16_t,std::size_t> active_;
    std::map<std::uint32_t,std::int16_t> guids_;
};
}
