#pragma once
#include "odh/inventory.hpp"
#include "odh/world.hpp"

namespace odh {
struct InventoryFind {
    VmContext context;
    std::int16_t target,neighbor;
    std::uint32_t guid;
    std::uint8_t type,count_temp,index_temp;
    bool write_index,success;
    std::optional<std::size_t> found_index;
    std::optional<std::int16_t> count;
};
// TS1 opcode51 mode3 only. NGBH must have been loaded successfully; an empty
// serialized map differs from an unavailable resource. Source and World must
// outlive the callback and remain unchanged during dispatch. Query allocates
// no inventory storage. Observer must not throw/reenter/mutate the source.
// Outputs are staged in reference order (count then optional index), including
// false results from a found zero/negative signed count.
VmPrimitive inventory_find_primitive(const Ngbh&,const World&,
    std::function<void(const InventoryFind&)> observed={});
}
