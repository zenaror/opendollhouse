// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/native_personal_state.hpp"
#include "odh/npc_avatar.hpp"
namespace odh {
// Runtime-only metadata evidence. Resource identity is authenticated by the
// host BEFORE lookup; this table never loads a file or upgrades an avatar.
class AvatarConstructorMetadata {
public:
    explicit AvatarConstructorMetadata(std::size_t limit=8192):limit_(limit){
        if(!limit_)throw FormatError("avatar constructor metadata limit is zero");
    }
    static bool same_identity(const NativePersonalIdentity& a,const NativePersonalIdentity& b){
        return a.neighbor_id==b.neighbor_id&&a.guid==b.guid&&a.definition_chunk_id==b.definition_chunk_id&&
            a.character_asset==b.character_asset&&a.character_fingerprint==b.character_fingerprint;
    }
    // Existing native archives already declare this shape. NINV2 alone or an
    // enlarged bank alone is insufficient; imported saved bodies stay legacy.
    static bool original_shape(const Entity& e) noexcept {
        const auto type=e.definition_words.find(9),profile=e.definition_words.find(10);
        return e.kind==EntityKind::avatar&&e.definition&&!e.ui_ghost&&!e.saved_object_banks&&
            e.person_data.size()==256&&e.motives.size()==16&&e.native_slot0.has_value()&&
            type!=e.definition_words.end()&&type->second==2&&profile!=e.definition_words.end()&&profile->second==0;
    }
    static bool restored_original(const Entity& e,const NativePersonalIdentity& identity,bool native_protocol) noexcept {
        return native_protocol&&original_shape(e)&&e.id>0&&e.generation&&identity.guid&&
            e.definition->guid==identity.guid&&e.person_data[31]==identity.neighbor_id&&
            !identity.character_asset.empty();
    }
    bool admit_restored(const Entity& e,const NativePersonalIdentity& identity,bool native_protocol){
        if(!restored_original(e,identity,native_protocol))return false;
        remember({e.id,e.generation},identity);return true;
    }
    // Call only after successful actual materialize_npc_avatar and before
    // original Init/Main. NID is still zero at this exact constructor boundary;
    // its expected identity comes from the authenticated request, not a guess.
    void admit_constructed(const World& world,WorldEntityReference ref,const NativePersonalIdentity& identity,
        const NpcAvatarRequest& request,const std::function<bool()>& source_current){
        const auto* e=world.resolve_reference(ref);
        if(!source_current||!source_current()||!e||e->dead||e->position||e->attachment||!original_shape(*e)||
           !request.original_semantics||!request.original_residential.has_value()||!request.original_definition_version||
           request.guid()!=identity.guid||e->definition->guid!=identity.guid||
           request.requested_neighbor.neighbor_id!=identity.neighbor_id||e->person_data[31]!=0||identity.character_asset.empty())
            throw FormatError("original constructor metadata lacks its exact fresh body/request/resource authority");
        remember(ref,identity);
    }
    bool contains(WorldEntityReference ref,const NativePersonalIdentity& identity) const {
        const auto found=original_.find(ref);if(found==original_.end())return false;
        if(!same_identity(found->second,identity))throw FormatError("original constructor metadata resource identity changed");
        return true;
    }
    // Original ClassInit/Person.Init uses the authenticated NBRS name, not
    // the PIN random-name list. These authorities must never be merged.
    void validate_selected_name(WorldEntityReference ref,const NativePersonalIdentity& identity,
        const std::optional<std::string>& selected,const NeighborRecord* neighbor,
        const std::vector<std::string>& resource_names) const {
        if(contains(ref,identity)){
            if(!neighbor||neighbor->neighbor_id!=identity.neighbor_id||neighbor->guid!=identity.guid||
               !selected||*selected!=neighbor->name)
                throw FormatError("original NPC saved name differs from its authenticated neighbor source");
            return;
        }
        if(selected){
            bool present=false;for(const auto& name:resource_names)if(name==*selected){present=true;break;}
            if(!present)throw FormatError("NPC saved body name is not in its exact resource");
        }else if(!resource_names.empty())throw FormatError("NPC saved body name choice is absent");
    }
    // Keep dead-but-referenced bodies. Drop evidence only when the World owner
    // really released that exact generation; an ID reused later inherits none.
    void collect(const World& world){
        for(auto i=original_.begin();i!=original_.end();)
            if(!world.resolve_reference(i->first))i=original_.erase(i);else ++i;
    }
    std::size_t size() const noexcept {return original_.size();}
private:
    void remember(WorldEntityReference ref,const NativePersonalIdentity& identity){
        const auto found=original_.find(ref);
        if(found!=original_.end()){
            if(!same_identity(found->second,identity))throw FormatError("constructor metadata aliases a different resource identity");
            return;
        }
        if(original_.size()>=limit_)throw FormatError("avatar constructor metadata budget exceeded");
        original_.emplace(ref,identity);
    }
    std::size_t limit_;
    std::map<WorldEntityReference,NativePersonalIdentity> original_;
};
} // namespace odh
