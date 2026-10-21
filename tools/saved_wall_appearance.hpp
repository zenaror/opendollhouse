#pragma once
#include "saved_resources.hpp"
#include "odh/wall_appearance.hpp"
#include "odh/entry_points.hpp"
#include <map>
#include <set>
struct SavedWallAppearanceBinding {
    std::uint32_t guid;
    std::string source,asset;
    std::uint16_t chunk_id;
    odh::ObjdWallAppearance binding;
};
struct SavedWallAppearance {
    odh::WallAppearance appearance;
    std::vector<SavedWallAppearanceBinding> bindings;
    std::vector<odh::WallAppearanceInput> inputs;
};
struct SavedPortalFunction {
    odh::EntryPointSource source;
    std::uint16_t action;
    bool action_bhav_in_asset;
};

struct SavedEntryPointResource {
    std::string source,asset;
    std::uint16_t chunk_id;
    odh::ObjdFunctions functions;
    odh::EntryPointTable table;
    std::set<std::uint16_t> bhavs;
};

// Resolve the exact OBJD/OBJf function table and private BHAV inventory from
// one saved entity's selected definition asset. This is static resource
// inspection only; it never executes any entry-point routine.
inline SavedEntryPointResource saved_entry_point_resource(const odh::DefinitionCandidate& candidate) {
    std::optional<odh::ObjdFunctions> functions;
    std::optional<odh::EntryPointTable> table;
    std::set<std::uint16_t> bhavs;
    auto load=[&](const auto& view) {
        std::optional<odh::View> objd,objf;
        std::size_t objd_count=0,objf_count=0;
        odh::Limits limits;limits.iff_chunks=4096;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk) {
            if(chunk.type=="OBJD"&&chunk.id==candidate.chunk_id) { ++objd_count; objd=chunk.payload; }
            else if(chunk.type=="OBJf"&&chunk.id==candidate.chunk_id) { ++objf_count; objf=chunk.payload; }
            else if(chunk.type=="BHAV") bhavs.insert(chunk.id);
        },limits);
        if(objd_count!=1||!objd) throw odh::FormatError("entry-point probe OBJD identity absent or ambiguous");
        functions=odh::read_objd_functions(*objd);
        if(functions->uses_function_table==0) {
            if(objf_count!=0) throw odh::FormatError("entry-point probe has unexpected OBJf for generated function table");
            table=odh::read_objd_entry_points(*objd);
        } else {
            if(objf_count!=1||!objf) throw odh::FormatError("entry-point probe selected OBJf absent or ambiguous");
            table=odh::read_objf(*objf);
        }
    };
    if(candidate.asset.empty()) with_iff_file(candidate.source,load);
    else with_far_entry(candidate.source,candidate.asset,load);
    if(!functions||!table) throw odh::FormatError("entry-point probe function table unavailable");
    return {candidate.source,candidate.asset,candidate.chunk_id,*functions,*table,std::move(bhavs)};
}

// Resolve the authored Portal entry (index 15) from the exact OBJD/OBJf asset
// selected for a saved entity. This reads a function reference only: it does
// not execute the BHAV or establish live portal/route state.
inline SavedPortalFunction saved_portal_function(const odh::DefinitionCandidate& candidate) {
    const auto resource=saved_entry_point_resource(candidate);
    if(resource.table.entries.size()<=15) throw odh::FormatError("portal probe entry 15 unavailable");
    const auto action=resource.table.entries[15].action;
    return {resource.table.source,action,action!=0&&resource.bhavs.find(action)!=resource.bhavs.end()};
}

inline SavedWallAppearance saved_wall_appearance(const odh::SavedWorld& saved,const odh::DefinitionIndex& index,const odh::SavedArchitecture& architecture,bool allow_removed=false){
    if(!saved.world||saved.records.size()>4096)throw odh::FormatError("wall appearance saved World/record budget invalid");
    SavedWallAppearance out;std::map<std::uint32_t,std::size_t> bindings;std::uint64_t names=0;
    for(const auto& raw:saved.records){const auto id=static_cast<std::int16_t>(raw.saved.object_id);const auto* e=saved.world->find(id);
        // Removed entities no longer contribute door/window appearance.
        if(!e&&allow_removed)continue;
        if(!e||!e->definition)throw odh::FormatError("wall appearance saved entity unavailable");
        const odh::VmFrame frame{odh::routine_key(4096),0,{id,id,id,0},{},{}};
        const auto flag=saved.world->read(frame,3,40),placement=saved.world->read(frame,3,13);
        if(!flag||!placement)throw odh::FormatError("wall appearance saved bank unavailable");
        odh::WallAppearanceInput input{id,e->generation,e->definition->guid,e->kind,e->position,e->attachment.has_value(),static_cast<std::uint16_t>(*flag),static_cast<std::uint16_t>(*placement),{}};
        if(e->kind==odh::EntityKind::object&&e->position&&!e->attachment&&(input.flags2&0xc000U)){
            auto found=bindings.find(input.guid);
            if(found==bindings.end()){
                if(bindings.size()>=256)throw odh::FormatError("wall appearance binding cache budget exceeded");
                const auto* candidate=index.unique(input.guid);if(!candidate)throw odh::FormatError("wall appearance concrete definition unavailable");
                if(candidate->source.size()+candidate->asset.size()>256*1024-names)throw odh::FormatError("wall appearance provenance budget exceeded");
                std::optional<odh::ObjdWallAppearance> binding;
                auto load=[&](const auto& v){odh::Limits limits;limits.iff_chunks=4096;odh::iff_chunks(v,[&](const auto& c){if(c.type=="OBJD"&&c.id==candidate->chunk_id){if(binding)throw odh::FormatError("duplicate wall appearance OBJD");binding=odh::read_objd_wall_appearance(c.payload);}},limits);};
                if(candidate->asset.empty())with_iff_file(candidate->source,load);else with_far_entry(candidate->source,candidate->asset,load);
                if(!binding||binding->guid!=input.guid)throw odh::FormatError("wall appearance OBJD identity mismatch");
                names+=candidate->source.size()+candidate->asset.size();const auto at=out.bindings.size();out.bindings.push_back({input.guid,candidate->source,candidate->asset,candidate->chunk_id,*binding});found=bindings.emplace(input.guid,at).first;
            }
            input.binding=out.bindings[found->second].binding;
        }
        out.inputs.push_back(input);
    }
    out.appearance=odh::derive_wall_appearance_restore_order(architecture,out.inputs);return out;
}
