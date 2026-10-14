#include "odh/saved_objects.hpp"
#include <algorithm>
#include <set>

namespace odh {
std::vector<SavedObjectPrefix> read_saved_object_prefixes(const Objm& objects,const Objt& types,
    const SavedPrefixLimits& limits){
    if(objects.blocks.size()!=objects.id_to_objt.size()||objects.blocks.size()>limits.objects)
        throw FormatError("saved object prefix count mismatch or budget exceeded");
    constexpr std::uint64_t fixed_words=8+68+5;
    if(objects.blocks.size()>limits.words/fixed_words)
        throw FormatError("saved object fixed word budget exceeded");
    std::vector<SavedObjectPrefix> result;
    result.reserve(objects.blocks.size());
    std::set<std::uint16_t> ids;
    auto available=limits.words;
    for(const auto& block:objects.blocks){
        if(available<fixed_words)throw FormatError("saved object word budget exceeded");
        ObjmPrefixLimits prefix_limits;
        prefix_limits.attributes=static_cast<std::uint32_t>(std::min<std::uint64_t>(
            limits.attributes_per_object,available-fixed_words));
        auto prefix=read_objm_prefix(block,prefix_limits);
        available-=fixed_words+prefix.attributes.size();
        const auto id=static_cast<std::uint16_t>(prefix.object_data[11]);
        const auto mapped=objects.id_to_objt.find(id);
        if(id==0||mapped==objects.id_to_objt.end()||!ids.insert(id).second)
            throw FormatError("saved object prefix ID missing from map or duplicated");
        const auto type=mapped->second;
        const auto* entry=types.entry_for_type(type);
        if(!entry||entry->guid==0)
            throw FormatError("saved object type unavailable or sentinel GUID");
        result.push_back({id,type,entry->guid,std::move(prefix)});
    }
    return result;
}
}
