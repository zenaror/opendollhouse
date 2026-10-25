#pragma once
#include "odh/neighborhood.hpp"
#include "odh/vm.hpp"
namespace odh {
enum class NeighborDataSource {serialized,legacy_zero_extension,missing_neighbor_zero,absent_person_data_zero};
struct NeighborDataValue {std::int16_t neighbor,index,value;NeighborDataSource source;};
struct NeighborNext {std::int16_t current,result;bool success;};
// Immutable TS1 neighborhood data, independent of instantiated World entities.
class NeighborContext {
public:
    explicit NeighborContext(Nbrs);
    const Nbrs& source() const{return source_;}
    std::size_t active_count() const{return active_.size();}
    const NeighborRecord* by_id(std::int16_t id) const;
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
    std::vector<Ordered> ordered_;
    std::map<std::int16_t,std::size_t> active_;
    std::map<std::uint32_t,std::int16_t> guids_;
};
}
