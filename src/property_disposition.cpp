#include "odh/property_disposition.hpp"

namespace odh {
PropertyDispositionInspection inspect_property_disposition(const DefinitionIndex& index,
    std::uint32_t guid,std::uint16_t instance_type,std::optional<std::uint16_t> flags) {
    using D=PropertyDisposition;using R=PropertyDispositionReason;
    // Requiring identity/type agreement even for retained entities deliberately
    // rejects incomplete native inputs; the comparison predicate alone is not
    // sufficient to validate their reconstruction.
    const auto* selected=index.unique(guid);
    if(!selected)return {D::unresolved,index.candidates(guid)?R::ambiguous_definition:R::missing_definition,{},{}};
    if(selected->definition.is_multitile!=(selected->definition.master_id!=0))
        return {D::unresolved,R::invalid_definition,{},{}};
    if(selected->definition.object_type!=instance_type)return {D::unresolved,R::type_mismatch,{},{}};
    if(!flags)return {D::unresolved,R::missing_flags,{},{}};
    const auto* effective=selected;
    if(selected->definition.is_multitile) {
        effective=index.unique_master(*selected);
        if(!effective)return {D::unresolved,R::missing_master,{},{}};
        if(!effective->definition.is_multitile)
            return {D::unresolved,R::invalid_definition,{},{}};
        if(effective->definition.object_type!=instance_type)
            return {D::unresolved,R::type_mismatch,{},{}};
    }
    const auto& definition=effective->definition;
    if((*flags&0x10U)!=0)return {D::retain,R::retained_flag,definition.guid,definition.build_mode_type};
    // Type2 is broader than the departing household. This diagnostic result is
    // never permission to remove visitors/NPCs or mutate neighborhood records.
    if(instance_type==2)return {D::remove,R::actor,definition.guid,definition.build_mode_type};
    if(!definition.build_mode_type)return {D::unresolved,R::missing_build_mode,definition.guid,{}};
    const auto build=*definition.build_mode_type;
    return {build?D::retain:D::remove,build?R::build_mode:R::buy_mode,definition.guid,build};
}
const char* property_disposition_name(PropertyDisposition value) noexcept {
    switch(value){case PropertyDisposition::retain:return "retain";case PropertyDisposition::remove:return "remove";
        case PropertyDisposition::unresolved:return "unresolved";}
    return "invalid";
}
const char* property_disposition_reason_name(PropertyDispositionReason value) noexcept {
    switch(value){
    case PropertyDispositionReason::retained_flag:return "retained_flag";
    case PropertyDispositionReason::actor:return "actor";
    case PropertyDispositionReason::build_mode:return "build_mode";
    case PropertyDispositionReason::buy_mode:return "buy_mode";
    case PropertyDispositionReason::missing_flags:return "missing_flags";
    case PropertyDispositionReason::missing_definition:return "missing_definition";
    case PropertyDispositionReason::ambiguous_definition:return "ambiguous_definition";
    case PropertyDispositionReason::missing_master:return "missing_master";
    case PropertyDispositionReason::missing_build_mode:return "missing_build_mode";
    case PropertyDispositionReason::type_mismatch:return "type_mismatch";
    case PropertyDispositionReason::invalid_definition:return "invalid_definition";
    }
    return "invalid";
}
} // namespace odh
