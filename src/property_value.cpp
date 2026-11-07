#include "odh/property_value.hpp"
#include <set>

namespace odh {
PropertyGroupValue inspect_property_group_value(const World& world,const SavedMultitileGroup& group,
    bool complete_group,std::size_t member_limit) {
    using R=PropertyValueReason;
    const auto unresolved=[](R reason){return PropertyGroupValue{PropertyValueStatus::unresolved,reason,{},{},false};};
    if(!complete_group)return unresolved(R::incomplete_group);
    if(group.members.size()>4096 || group.members.size()>member_limit)return unresolved(R::member_budget);
    if(group.members.empty())return unresolved(R::empty_group);
    if(group.leader<=0)return unresolved(R::invalid_id);
    if(group.master_guid && !*group.master_guid)
        return unresolved(R::identity_mismatch);
    std::set<std::int16_t> members;
    bool leader_found=false,leader_multitile=false,repair=false;
    std::optional<std::int16_t> current_value;
    for(const auto id:group.members) {
        if(id<=0)return unresolved(R::invalid_id);
        if(!members.insert(id).second)return unresolved(R::duplicate_member);
        const auto* entity=world.find(id);
        if(!entity)return unresolved(R::missing_entity);
        if(!entity->definition || !entity->definition->guid)return unresolved(R::missing_identity);
        if(entity->definition->master_guid&&!*entity->definition->master_guid)return unresolved(R::identity_mismatch);
        if(group.master_guid&&entity->definition->master_guid!=group.master_guid)return unresolved(R::identity_mismatch);
        if(id==group.leader) {
            leader_found=true;leader_multitile=entity->definition->master_guid.has_value();
            VmFrame address;address.context={id,id,id,0};
            if(!world.bind_context_live(address.context))return unresolved(R::missing_entity);
            current_value=world.read(address,3,41);
            if(!current_value)return unresolved(R::missing_current_value);
        }
    }
    if(!leader_found)return unresolved(R::leader_absent);
    // Source00432249 only follows the repair chain when ROOT's own definition
    // is multitile. A non-MT root reads its own RepairState15/Value41 only;
    // independently authenticated foreign members still belong to the complete
    // partition but their repair words are not inputs to this source branch.
    for(const auto id:group.members){
        if(!leader_multitile&&id!=group.leader)continue;
        VmFrame address;address.context={id,id,id,0};
        if(!world.bind_context_live(address.context))return unresolved(R::missing_entity);
        const auto repair_word=world.read(address,3,15);
        if(!repair_word)return unresolved(R::missing_repair_state);
        repair=repair||*repair_word!=0;
    }
    // C++ signed integer division truncates toward zero, including odd negatives.
    const auto value=static_cast<std::int16_t>(repair?static_cast<int>(*current_value)/2:*current_value);
    return {PropertyValueStatus::resolved,R::none,current_value,value,repair};
}
const char* property_value_status_name(PropertyValueStatus value) noexcept {
    switch(value){case PropertyValueStatus::resolved:return "resolved";case PropertyValueStatus::unresolved:return "unresolved";}
    return "invalid";
}
const char* property_value_reason_name(PropertyValueReason value) noexcept {
    switch(value){
    case PropertyValueReason::none:return "none";
    case PropertyValueReason::incomplete_group:return "incomplete_group";
    case PropertyValueReason::member_budget:return "member_budget";
    case PropertyValueReason::empty_group:return "empty_group";
    case PropertyValueReason::invalid_id:return "invalid_id";
    case PropertyValueReason::duplicate_member:return "duplicate_member";
    case PropertyValueReason::leader_absent:return "leader_absent";
    case PropertyValueReason::missing_entity:return "missing_entity";
    case PropertyValueReason::missing_identity:return "missing_identity";
    case PropertyValueReason::identity_mismatch:return "identity_mismatch";
    case PropertyValueReason::missing_current_value:return "missing_current_value";
    case PropertyValueReason::missing_repair_state:return "missing_repair_state";
    }
    return "invalid";
}
} // namespace odh
