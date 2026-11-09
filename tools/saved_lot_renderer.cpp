#include "saved_lot_renderer.hpp"
#include "saved_wall_appearance.hpp"
#include "session_generic_resource_path.hpp"
#include "odh/compositor.hpp"
#include "odh/ground_owner.hpp"
#include "odh/cutaway_visibility.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace {
const odh::ArchitectureResourceCatalog& material_catalog(const std::optional<SavedLotMaterialOptions>& options) {
    static const odh::ArchitectureResourceCatalog empty;
    return options?options->catalog:empty;
}
std::string material_alias_error(const odh::ArchitectureMap& floors,const odh::ArchitectureMap& walls,
    const odh::ResolvedArchitectureAliases& aliases,const odh::ArchitectureResourceCatalog& catalog,
    bool include_walls) {
    const auto& d=aliases.diagnostics;
    std::string result="saved lot external material alias unresolved: floor_map_misses="+
        std::to_string(d.floor_map_misses)+" wall_map_misses="+
        std::to_string(include_walls?d.wall_map_misses:0)+" catalog_entries="+
        std::to_string(catalog.resources.size());
    std::size_t shown=0;
    const auto append=[&](const char* kind,const odh::ArchitectureMap& map,const auto& missing){
        for(const auto& entry:map.entries){
            if(!missing[entry.id]||shown>=8)continue;
            ++shown;
            result+=" [";result+=kind;result+=" id="+std::to_string(entry.id)+" name=";
            // Diagnostics are bounded and single-line even for malformed names.
            const auto count=std::min<std::size_t>(entry.name.size(),96);
            for(std::size_t i=0;i<count;++i){const auto c=static_cast<unsigned char>(entry.name[i]);
                result+=c>=32&&c<127&&c!=']'?static_cast<char>(c):'?';}
            if(entry.name.size()>count)result+="...";
            result+="]";
        }
    };
    append("floor",floors,d.unresolved_floor_aliases);
    if(include_walls)append("wall",walls,d.unresolved_wall_aliases);
    return result;
}
std::map<std::int16_t,std::array<float,2>> terrain_group_centers(const odh::SavedWorld& saved) {
    if(saved.records.size()>4096||saved.multitile_groups.size()>4096)
        throw odh::FormatError("live saved terrain group budget exceeded");
    std::map<std::int16_t,const odh::SavedWorldEntity*> records;
    for(const auto& record:saved.records){
        const auto id=record.saved.object_id;
        if(!id||id>32767||!records.emplace(static_cast<std::int16_t>(id),&record).second)
            throw odh::FormatError("live saved terrain group record identity invalid");
    }
    std::map<std::int16_t,std::array<float,2>> result;std::size_t members=0;
    for(const auto& group:saved.multitile_groups){
        if(group.members.empty()||group.members.size()>4096-members||
           std::find(group.members.begin(),group.members.end(),group.leader)==group.members.end())
            throw odh::FormatError("live saved terrain group membership invalid or over budget");
        members+=group.members.size();std::int64_t x=0,y=0;
        for(const auto id:group.members){
            const auto found=records.find(id);
            if(found==records.end()||found->second->tail.person||found->second->serialized_object_type==2)
                throw odh::FormatError("saved terrain group member has no imported object row");
            // Same bounded saved-coordinate profile as saved_scene, including
            // OOW members. Widen before addition, then reject short overflow.
            x+=found->second->saved.prefix.x;y+=found->second->saved.prefix.y;
            if(x < -32768||x > 32767||y < -32768||y > 32767)
                throw odh::FormatError("live saved terrain group short-coordinate overflow unsupported");
        }
        const auto count=static_cast<std::int64_t>(group.members.size());
        const std::array<float,2> center{static_cast<float>(x/count)/16,static_cast<float>(y/count)/16};
        for(const auto id:group.members)if(!result.emplace(id,center).second)
            throw odh::FormatError("duplicate live saved terrain group member");
    }
    return result;
}
}

const odh::SlotTable* SavedGraphicsResources::slots(const odh::Entity& container) {
    if(!container.definition)throw odh::FormatError("live graphics container has no definition");
    const auto guid=container.definition->guid;
    const auto found=slots_.find(guid);if(found!=slots_.end())return &found->second;
    if(slots_.size()>=limits_.slot_tables||records_>limits_.slot_records||records_>4096)
        throw odh::FormatError("live graphics SLOT cache budget exceeded");
    const auto* candidate=definitions_.unique(guid);
    if(!candidate)throw odh::FormatError("live graphics container definition unavailable");
    std::optional<odh::SlotTable> table;
    const auto load=[&](const odh::View& view){
        std::optional<odh::ObjdBinding> binding;odh::Limits bounds;bounds.iff_chunks=4096;
        odh::iff_chunks(view,[&](const auto& chunk){if(chunk.type=="OBJD"&&chunk.id==candidate->chunk_id){
            if(binding)throw odh::FormatError("duplicate live graphics container OBJD");
            binding=odh::read_objd_binding(chunk.payload);
        }},bounds);
        if(!binding||binding->guid!=guid)throw odh::FormatError("live graphics container OBJD identity mismatch");
        bounds.slot_records=static_cast<std::uint32_t>(std::min(std::size_t{4096}-records_,limits_.slot_records-records_));
        odh::iff_chunks(view,[&](const auto& chunk){if(chunk.type=="SLOT"&&chunk.id==binding->slot_table){
            if(table)throw odh::FormatError("duplicate live graphics container SLOT");
            table=odh::read_slot(chunk.payload,bounds);
        }},bounds);
    };
    if(candidate->asset.empty())with_iff_file(candidate->source,load);
    else with_far_entry(candidate->source,candidate->asset,load);
    if(!table)throw odh::FormatError("live graphics container SLOT unavailable");
    const auto count=table->records.size();
    const auto inserted=slots_.emplace(guid,std::move(*table));records_+=count;
    return &inserted.first->second;
}

odh::GraphicsEntity SavedGraphicsResources::entity(const odh::World& world,std::int16_t id) {
    return odh::graphics_entity(world,id,[&](const auto& container){return slots(container);});
}

SavedLotRenderer::SavedLotRenderer(SavedArchitectureResource architecture,
    const odh::SavedWorld& saved,const odh::DefinitionIndex& definitions,
    const std::string& floors,const std::optional<std::string>& walls,SavedLotView view,
    const std::optional<SavedLotMaterialOptions>& materials)
    :architecture_(std::move(architecture)),saved_(saved),definitions_(definitions),graphics_(definitions),
     view_(view),floors_(floors,material_catalog(materials)),objects_(definitions) {
    if(!saved_.world||view_.level<1||view_.level>2||
       (view_.terrain_theme&&unsigned(*view_.terrain_theme)>=8)||
       (view_.walls_down&&!walls))
        throw odh::FormatError("live saved lot World/view outside profile");
    const auto* floor_map=selected_architecture_map(architecture_,"FLRm");
    const auto* wall_map=selected_architecture_map(architecture_,"WALm");
    if(materials){
        const odh::ArchitectureMap empty;
        const auto& catalog=materials->catalog;
        aliases_=odh::resolve_architecture_aliases(floor_map?*floor_map:empty,wall_map?*wall_map:empty,
            catalog.floors(),catalog.walls(),materials->alias_limits);
        // A missing name must not silently retain a byte and render a global
        // material of the same number. Diagnostics remain available on success.
        if(!aliases_->floor_map_complete()||(walls&&!aliases_->wall_map_complete()))
            throw odh::FormatError(material_alias_error(floor_map?*floor_map:empty,
                wall_map?*wall_map:empty,*aliases_,catalog,walls.has_value()));
        prepared_=odh::prepare_saved_architecture(architecture_.architecture,aliases_->aliases,
            materials->preparation,materials->preparation_limits);
        if(materials->restored_architecture){
            auto restored=odh::resolve_architecture_snapshot(*materials->restored_architecture,catalog);
            if(restored.width!=prepared_->width||restored.height!=prepared_->height)
                throw odh::FormatError("saved edited architecture differs from selected lot extent");
            prepared_=std::move(restored);
        }
        // Remap misses without declarations can represent global road/base
        // patterns; they are preserved and exposed in preparation diagnostics.
        prepared_render_=SavedArchitectureResource{
            prepared_render_architecture(architecture_.architecture,*prepared_),architecture_.maps};
        // Room membership uses floor identity and wall segments/styles, not
        // wall sprite material IDs. Keep the room map available when a wall
        // art alias is unresolved; the renderer still reports that material
        // miss separately and does not treat it as resolved artwork.
        if(prepared_->floor_identity_unambiguous())
            runtime_rooms_=odh::generate_prepared_room_map(*prepared_);
    }else if((floor_map&&!floor_map->entries.empty())||(walls&&wall_map&&!wall_map->entries.empty()))
        throw odh::FormatError("live saved lot custom materials require a resource resolver");
    group_centers_=terrain_group_centers(saved_);
    for(const auto& record:saved_.records){
        const auto id=static_cast<std::int16_t>(record.saved.object_id);
        if(record.tail.person||record.serialized_object_type==2)continue;
        if(record.imported_generation){
            object_generations_.emplace(id,record.imported_generation);
        }else{
            // Direct preview callers may construct records without an ObjM
            // import. Bind only an exact available object in that initial scene.
            const auto* entity=saved_.world->find(id);
            if(entity&&entity->kind==odh::EntityKind::object&&entity->definition&&
               entity->definition->guid==record.saved.guid)object_generations_.emplace(id,entity->generation);
        }
    }
    if(walls)walls_=std::make_unique<SavedWallRenderer>(*walls,definitions_,material_catalog(materials));
}

namespace {
// LotTilePos addition/subtraction wraps a signed 16-bit coordinate.
std::int32_t lot_short(std::int64_t coordinate) {
    const auto raw=static_cast<std::uint16_t>(coordinate);
    return raw<32768?raw:static_cast<std::int32_t>(raw)-65536;
}
std::array<float,2> rotate_group_offset(float x,float y,std::uint8_t direction){
    switch(direction){
        case 2:return {-y,x};
        case 4:return {-x,-y};
        case 6:return {y,-x};
        default:return {x,y}; // Pinned ChangePosition defaults non-cardinal headings to north.
    }
}
}
void SavedLotRenderer::restore_runtime_group_placements(const std::vector<odh::ObjectGroup>& groups){
    std::map<std::int16_t,RestoredGroupPlacement> initial;
    std::size_t members=0;
    for(const auto& group:groups){
        if(!group.multitile)continue;
        if(group.members.empty()||group.members.size()>4096-members)
            throw odh::FormatError("restored artwork group membership exceeds budget");
        members+=group.members.size();
        RestoredGroupPlacement placement;placement.group=group;
        std::int64_t x=0,y=0;
        for(const auto& member:group.members){
            const auto* entity=saved_.world->find(member.id);
            if(!entity||entity->generation!=member.generation)
                throw odh::FormatError("restored artwork group member is absent");
            placement.members.emplace_back(member.generation,entity->position);
            x+=entity->position?entity->position->x:-32768;
            y+=entity->position?entity->position->y:-32768;
        }
        const auto count=static_cast<std::int32_t>(group.members.size());
        placement.mean={lot_short(x)/count,lot_short(y)/count};
        if(!initial.emplace(group.base_id,std::move(placement)).second)
            throw odh::FormatError("duplicate restored artwork group base");
    }
    restored_group_placements_.swap(initial);
}

void SavedLotRenderer::set_runtime_objects(const std::vector<SavedLotRuntimeObject>& objects,
        const std::vector<odh::ObjectGroup>& groups,const std::filesystem::path& game_root) {
    if(objects.size()>4096||groups.size()>4096)
        throw odh::FormatError("live runtime object binding budget exceeded");
    std::map<std::int16_t,SavedLotRuntimeObject> replacement;
    std::size_t names=0;
    for(const auto& object:objects){
        if(object.id<=0||!object.generation||!object.guid||object.source.empty()||
           !object.definition_chunk_id||replacement.count(object.id))
            throw odh::FormatError("live runtime object binding identity invalid");
        if(object.source.size()>256*1024-names)
            throw odh::FormatError("live runtime object binding provenance budget exceeded");
        names+=object.source.size();
        if(object.asset.size()>256*1024-names)
            throw odh::FormatError("live runtime object binding provenance budget exceeded");
        names+=object.asset.size();
        const auto* entity=saved_.world->find(object.id);
        const auto* candidate=definitions_.unique(object.guid);
        if(!entity||entity->kind!=odh::EntityKind::object||entity->ui_ghost||
           entity->generation!=object.generation||!entity->definition||
           entity->definition->guid!=object.guid||!candidate||
           candidate->source!=object.source||candidate->asset!=object.asset||
           candidate->chunk_id!=object.definition_chunk_id)
            throw odh::FormatError("live runtime object binding World/resource mismatch for object "+std::to_string(object.id));
        replacement.emplace(object.id,object);
    }
    // The runtime authenticates group membership on construction and restore.
    // Recheck its concrete pieces against this render frame's identities.
    odh::validate_object_groups({groups},saved_.world->snapshot());
    std::set<std::int16_t> grouped,unchanged_groups;
    std::map<std::int16_t,std::uint64_t> member_generations;
    std::map<std::int16_t,std::array<float,2>> centers;
    for(const auto& group:groups){
        if(group.members.empty()||group.members.size()>4096-grouped.size()||
           (group.kind==odh::ObjectGroupKind::constructed&&!group.multitile&&group.members.size()!=1))
            throw odh::FormatError("live runtime artwork group extent invalid");
        const bool live=group.kind==odh::ObjectGroupKind::live_association;
        const auto* requested=live?nullptr:definitions_.unique(group.definition_guid);
        if(!live&&!requested)throw odh::FormatError("live runtime artwork group definition unavailable");
        std::int64_t sum_x=0,sum_y=0;
        bool has_base=false;
        const auto restored=restored_group_placements_.find(group.base_id);
        bool unchanged=restored!=restored_group_placements_.end()&&
            restored->second.members.size()==group.members.size()&&
            restored->second.group.kind==group.kind&&restored->second.group.multitile==group.multitile;
        std::size_t index=0;
        for(const auto& member:group.members){
            has_base|=member.id==group.base_id;
            const auto binding=replacement.find(member.id);
            if(!grouped.insert(member.id).second)
                throw odh::FormatError("live runtime artwork group member identity differs");
            const auto* entity=saved_.world->find(member.id);
            const auto* part=definitions_.unique(entity->definition->guid);
            if(!part)throw odh::FormatError("live runtime artwork member definition unavailable");
            member_generations.emplace(member.id,member.generation);
            if(part->definition.is_multitile&&part->definition.sub_index==-1)
                throw odh::FormatError("runtime artwork must use the concrete OBJD, not its group master");
            if(live){
                const auto& origin=*member.origin; // Validated with World above.
                bool source_matches=origin.source==part->source;
                if(!source_matches&&!game_root.empty()){
                    odh_host::validate_generic_session_source_key(origin.source);
                    source_matches=odh_host::generic_session_source_key(game_root,part->source)==origin.source;
                }
                if(!source_matches||origin.asset!=part->asset||origin.definition_chunk_id!=part->chunk_id)
                    throw odh::FormatError("live runtime artwork individual resource differs");
                const auto* master=definitions_.unique_master(*part);
                if(origin.master_guid&&(!master||master->definition.guid!=*origin.master_guid))
                    throw odh::FormatError("live runtime artwork individual master differs");
                if(origin.source_kind==odh::ObjectGroupMemberSource::native_created){
                    if(binding==replacement.end()||binding->second.generation!=member.generation)
                        throw odh::FormatError("live runtime artwork created member lacks binding");
                }else{
                    if(binding!=replacement.end())
                        throw odh::FormatError("live runtime artwork original member has a creation binding");
                    const odh::SavedWorldEntity* original=nullptr;
                    for(const auto& row:saved_.records)if(row.saved.object_id==member.id){
                        if(original)throw odh::FormatError("live runtime artwork original row ambiguous");
                        original=&row;
                    }
                    if(!original||original->tail.person||original->serialized_object_type==2||
                       !original->imported_generation||original->imported_generation!=member.generation||
                       original->saved.guid!=origin.guid||original->definition.guid!=origin.guid||
                       original->definition.master_id!=part->definition.master_id||
                       original->definition.sub_index!=part->definition.sub_index||
                       original->definition.is_multitile!=part->definition.is_multitile||
                       origin.master_guid!=(master?std::optional<std::uint32_t>{master->definition.guid}:std::nullopt))
                        throw odh::FormatError("live runtime artwork original identity differs from import");
                }
            }else{
                if(binding==replacement.end()||binding->second.generation!=member.generation)
                    throw odh::FormatError("live runtime artwork construction member lacks binding");
                if(group.multitile){
                    if(!requested->definition.master_id||requested->definition.sub_index!=-1||
                       requested->definition.master_id!=part->definition.master_id||
                       requested->source!=part->source||requested->asset!=part->asset||
                       entity->definition->master_guid!=group.definition_guid)
                        throw odh::FormatError("live runtime artwork part differs from its construction master");
                }else if(part!=requested||entity->definition->master_guid)
                    throw odh::FormatError("live runtime artwork singleton differs from its construction resource");
            }
            if(unchanged){
                const auto& previous=restored->second.members[index];
                const auto& original=restored->second.group.members[index];
                unchanged=original.id==member.id&&original.offset_x==member.offset_x&&
                    original.offset_y==member.offset_y&&original.offset_level==member.offset_level&&
                    previous.first==entity->generation&&previous.second==entity->position;
            }
            ++index;sum_x+=member.offset_x;sum_y+=member.offset_y;
        }
        if(!has_base)throw odh::FormatError("live runtime artwork group base absent");
        if(unchanged)unchanged_groups.insert(group.base_id);
        if(group.multitile){
            const auto count=static_cast<float>(group.members.size());
            for(const auto& member:group.members){
                const auto& pos=saved_.world->find(member.id)->position;
                if(!pos)continue;
                if(unchanged){
                    // VM.Load rebuilds MTOffset with LotTilePos short arithmetic.
                    const auto& mean=restored->second.mean;
                    centers.emplace(member.id,std::array<float,2>{
                        (pos->x-lot_short(pos->x-mean[0]))/16.0f,
                        (pos->y-lot_short(pos->y-mean[1]))/16.0f});
                }else{
                    // ChangePosition uses the rotated offset of every piece,
                    // including parts whose positions are outside the world.
                    const auto correction=rotate_group_offset(sum_x/count-member.offset_x,
                        sum_y/count-member.offset_y,pos->direction);
                    centers.emplace(member.id,std::array<float,2>{
                        (pos->x+correction[0])/16,(pos->y+correction[1])/16});
                }
            }
        }
    }
    for(const auto& [id,object]:replacement){
        if(definitions_.unique(object.guid)->definition.is_multitile&&!grouped.count(id))
            throw odh::FormatError("live runtime artwork subpart lacks its construction group");
    }
    std::size_t total=object_generations_.size();
    for(const auto& [id,binding]:replacement){
        (void)binding;if(!object_generations_.count(id))++total;
    }
    if(total>4096)throw odh::FormatError("live runtime and saved object binding budget exceeded");
    runtime_objects_.swap(replacement);
    runtime_group_centers_.swap(centers);
    runtime_group_members_.swap(member_generations);
    // Once a load-time placement changes, returning to the same tile later
    // is still a live placement. Drop the old rounded offsets permanently.
    for(auto iter=restored_group_placements_.begin();iter!=restored_group_placements_.end();){
        if(unchanged_groups.count(iter->first))++iter;else iter=restored_group_placements_.erase(iter);
    }
}

void SavedLotRenderer::invalidate_runtime_wall_appearance() noexcept {
    runtime_wall_appearance_.reset();runtime_wall_owners_.clear();
}
bool SavedLotRenderer::runtime_wall_appearance_current() const {
    if(!runtime_wall_appearance_)return false;
    for(const auto& item:runtime_wall_owners_){
        const auto* e=saved_.world->find(item.first);const auto& expected=item.second;
        if(!e||e->generation!=expected.generation||!e->definition||e->definition->guid!=expected.guid||!(e->position==expected.position))
            return false;
        std::optional<std::tuple<std::int16_t,std::size_t,bool>> attachment;
        if(e->attachment)attachment=std::make_tuple(e->attachment->container,e->attachment->slot,e->attachment->positioned);
        if(attachment!=expected.attachment)return false;
    }
    // An added architectural object also needs its actual source event, rather
    // than retaining a known-old empty appearance. Normal Sim motion is ignored.
    for(std::int16_t cursor=0;;){const auto next=saved_.world->next_entity(cursor);if(!next)break;cursor=*next;
        const auto* e=saved_.world->find(cursor);if(!e||e->kind!=odh::EntityKind::object||e->dead||!e->position||e->attachment)continue;
        const auto flags=e->object_words.find(40);
        if(flags!=e->object_words.end()&&(std::uint16_t(flags->second)&0xc000U)&&!runtime_wall_owners_.count(cursor))return false;
    }
    return true;
}
const std::optional<odh::WallAppearance>& SavedLotRenderer::runtime_wall_appearance() {
    if(runtime_wall_appearance_&&!runtime_wall_appearance_current())invalidate_runtime_wall_appearance();
    return runtime_wall_appearance_;
}
void SavedLotRenderer::set_runtime_wall_appearance(std::optional<odh::WallAppearance> appearance) {
    if(!appearance){invalidate_runtime_wall_appearance();return;}
    if(appearance->faces.size()>16384||appearance->applications.size()>4096)
        throw odh::FormatError("runtime wall appearance exceeds bounded event profile");
    std::map<std::int16_t,WallAppearanceOwnerState> owners;
    const auto capture=[&](std::int16_t id,std::optional<std::uint64_t> generation){
        const auto* e=saved_.world->find(id);
        if(id<=0||!e||!e->definition||e->kind!=odh::EntityKind::object||(generation&&*generation!=e->generation))
            throw odh::FormatError("runtime wall appearance owner identity unavailable");
        WallAppearanceOwnerState state;state.generation=e->generation;state.guid=e->definition->guid;state.position=e->position;
        if(e->attachment)state.attachment=std::make_tuple(e->attachment->container,e->attachment->slot,e->attachment->positioned);
        owners[id]=state;
    };
    std::optional<std::tuple<std::uint8_t,std::uint32_t,std::uint32_t,std::uint8_t>> previous;
    const auto& geometry=render_architecture();
    for(const auto& face:appearance->faces){
        const auto key=std::make_tuple(face.level,face.y,face.x,face.segment);
        if(face.x>=geometry.width||face.y>=geometry.height||face.level<1||face.level>2||
           (face.segment!=odh::wall_top_left&&face.segment!=odh::wall_top_right)||(previous&&key<=*previous))
            throw odh::FormatError("runtime wall appearance face extent or ordering invalid");
        if(!odh::is_canonical_cleared_wall_face(face)){
            capture(face.owner,face.generation);
            if(face.style.resource_guid&&face.style.resource_guid!=owners.at(face.owner).guid)
                throw odh::FormatError("runtime wall appearance resource differs from source owner");
        }
        if(face.door_owner)capture(face.door_owner,face.door_generation);
        else if(face.door||face.door_generation)throw odh::FormatError("runtime door appearance lacks event owner");
        previous=key;
    }
    // Applications are bounded event history, not live ownership. A legitimate
    // delete leaves a canonical clear; its former numeric ID must not make the
    // next renderer require a retired object or authenticate a reused number.
    std::size_t count=0;
    for(std::int16_t cursor=0;;){const auto next=saved_.world->next_entity(cursor);if(!next)break;cursor=*next;
        if(++count>4096)throw odh::FormatError("runtime wall appearance World exceeds entity budget");
        const auto* e=saved_.world->find(cursor);if(!e||e->kind!=odh::EntityKind::object||e->dead||!e->position||e->attachment)continue;
        const auto flags=e->object_words.find(40);
        if(flags!=e->object_words.end()&&(std::uint16_t(flags->second)&0xc000U))capture(cursor,{});
    }
    runtime_wall_appearance_=std::move(appearance);runtime_wall_owners_=std::move(owners);
}
void SavedLotRenderer::set_avatar_slot_anchors(const std::vector<SavedLotAvatarSlotAnchor>& anchors) {
    if(anchors.size()>64*3)throw odh::FormatError("avatar visual slot anchor budget exceeded");
    std::map<std::pair<std::int16_t,std::size_t>,SavedLotAvatarSlotAnchor> replacement;
    for(const auto& anchor:anchors){
        const auto* actor=saved_.world->find(anchor.actor);
        if(!actor||actor->kind!=odh::EntityKind::avatar||actor->generation!=anchor.generation||
           !actor->position||!(*actor->position==anchor.logical_position)||anchor.slot>=3||
           anchor.slot>=actor->slots.size()||
           !std::all_of(anchor.tile_position.begin(),anchor.tile_position.end(),[](float n){return std::isfinite(n);})||
           !replacement.emplace(std::make_pair(anchor.actor,anchor.slot),anchor).second)
            throw odh::FormatError("avatar visual slot anchor identity or pose is invalid");
    }
    avatar_slot_anchors_.swap(replacement);
}

std::uint8_t SavedLotRenderer::avatar_visual_level(const odh::GraphicsEntity& graphics) const{
    const auto* actor=saved_.world->find(graphics.id);
    if(!actor||actor->kind!=odh::EntityKind::avatar||actor->generation!=graphics.generation||
       graphics.kind!=odh::EntityKind::avatar)throw odh::FormatError("live saved lot avatar identity changed");
    const auto* anchor=actor;
    if(graphics.container){
        const auto parent=saved_.world->attachment_parent({actor->id,actor->generation});
        anchor=parent&&parent->id==graphics.container?saved_.world->resolve_reference(*parent):nullptr;
    }else if(actor->attachment)throw odh::FormatError("live saved lot avatar container changed");
    if(!anchor||!anchor->position||anchor->position->level<1||anchor->position->level>2)
        throw odh::FormatError("live saved lot avatar visual floor unavailable");
    return static_cast<std::uint8_t>(anchor->position->level);
}

odh::GraphicsEntity SavedLotRenderer::avatar(std::int16_t id) {
    auto graphics=entity_graphics(id);
    if(avatar_visual_level(graphics)>view_.level)throw odh::FormatError("live saved lot avatar outside selected view");
    return graphics;
}

std::array<float,3> SavedLotRenderer::preview_tile_position(const odh::EntityPosition& position) const {
    auto result=odh::free_graphics_position(odh::EntityKind::object,position);
    result[2]+=odh::terrain_interp_altitude(render_architecture(),
        static_cast<float>(position.x)/16,static_cast<float>(position.y)/16);
    return result;
}
odh::GraphicsEntity SavedLotRenderer::entity_graphics(std::int16_t id) {
    const auto* child=saved_.world->find(id);
    const auto* parent=child&&child->attachment?saved_.world->find(child->attachment->container):nullptr;
    if(parent&&parent->kind==odh::EntityKind::avatar){
        const auto slot=child->attachment->slot;
        const auto anchor=avatar_slot_anchors_.find({parent->id,slot});
        if(child->kind!=odh::EntityKind::object||!child->position||child->position->direction>7||
           !parent->position||slot>=parent->slots.size()||parent->slots[slot]!=id||
           anchor==avatar_slot_anchors_.end()||anchor->second.generation!=parent->generation||
           !(anchor->second.logical_position==*parent->position))
            throw odh::FormatError("held object has no matching animated avatar slot anchor");
        return {id,child->generation,child->kind,anchor->second.tile_position,
            child->position->direction,parent->id};
    }
    auto graphics=graphics_.entity(*saved_.world,id);
    const auto* entity=saved_.world->find(graphics.container?graphics.container:id);
    if(!entity||!entity->position)
        throw odh::FormatError("live saved lot graphics anchor unavailable");
    std::array<float,2> xy{static_cast<float>(entity->position->x)/16,static_cast<float>(entity->position->y)/16};
    if(entity->kind==odh::EntityKind::object){
        const auto runtime=runtime_objects_.find(entity->id);
        const auto associated=runtime_group_members_.find(entity->id);
        if((associated!=runtime_group_members_.end()&&associated->second==entity->generation)||
           (runtime!=runtime_objects_.end()&&runtime->second.generation==entity->generation&&entity->definition&&
            runtime->second.guid==entity->definition->guid)){
            const auto center=runtime_group_centers_.find(entity->id);
            if(center!=runtime_group_centers_.end())xy=center->second;
        }else{
            const auto imported=object_generations_.find(entity->id);
            if(imported!=object_generations_.end()&&imported->second==entity->generation){
                const auto center=group_centers_.find(entity->id);
                if(center!=group_centers_.end())xy=center->second;
            }
        }
    }
    graphics.tile_position[2]+=odh::terrain_interp_altitude(render_architecture(),xy[0],xy[1]);
    return graphics;
}

std::optional<bool> SavedLotRenderer::cutaway_hidden_for(const odh::Entity& entity,const odh::GraphicsEntity* graphics,bool down) const {
    const auto previous=cutaway_hidden_.find({entity.id,entity.generation});
    const bool was_hidden=previous!=cutaway_hidden_.end()&&previous->second;
    const odh::VmFrame frame{odh::routine_key(4096),0,{entity.id,entity.id,entity.id,0},{},{}};
    const auto flags=saved_.world->read(frame,3,8);
    // A fresh component is false. In full walls-up, either unknown flag value
    // leaves that false or computes false; it cannot introduce hidden state.
    if(!flags){if(!down&&!was_hidden)return false;return {};}
    odh::FullCutawayVisibilityInput input;input.hide_for_cutaway=(static_cast<std::uint16_t>(*flags)&1024U)!=0;
    input.previous_hidden=was_hidden;input.walls_down=down;
    input.object_level=entity.position?entity.position->level:0;input.view_level=view_.level;
    if(!input.hide_for_cutaway||input.object_level<=0)return was_hidden;
    if(input.object_level!=input.view_level)return false;
    if(!down&&!was_hidden)return false; // In-bounds false or OOB unchanged false.
    if(!graphics)return {};
    input.x=graphics->tile_position[0];input.y=graphics->tile_position[1];
    const auto& architecture=render_architecture();input.width=architecture.width;input.height=architecture.height;
    const auto tile=odh::source_cutaway_tile(input.x,input.y);
    if(tile&&(*tile)[0]>=0&&(*tile)[1]>=0&&static_cast<std::uint32_t>((*tile)[0])<input.width&&
       static_cast<std::uint32_t>((*tile)[1])<input.height&&input.object_level>=1&&input.object_level<=2){
        const auto at=static_cast<std::size_t>((*tile)[1])*input.width+static_cast<std::size_t>((*tile)[0]);
        const auto& walls=architecture.levels[static_cast<std::size_t>(input.object_level-1)].walls;
        if(at>=walls.size())return {};
        input.wall_segments=walls[at].segments;
    }
    // Runtime SetWallUse/AdjacentWall publication is not represented yet. Keep
    // it UNKNOWN, proving only outcomes independent of those values.
    return odh::full_cutaway_hidden(input);
}
namespace {
std::string artwork_identity(std::int16_t id,std::uint64_t generation,std::uint32_t guid,const odh::Entity& current){
    return " id="+std::to_string(id)+" original_generation="+std::to_string(generation)+
        " original_guid="+std::to_string(guid)+" current_generation="+std::to_string(current.generation)+
        " current_guid="+std::to_string(current.definition?current.definition->guid:0)+
        " current_kind="+(current.kind==odh::EntityKind::avatar?"avatar":"object")+
        " ghost="+(current.ui_ghost?"1":"0");
}
// Called only when no current runtime-object binding shadows this ObjM row.
// Avatars have their own presentation and UI ghosts do not own lot artwork.
bool retired_import(const odh::World& world,std::int16_t id,std::uint64_t generation,std::uint32_t guid){
    const auto* entity=world.find(id);
    if(!entity)return true;
    if(entity->generation==generation)return false;
    if(entity->kind!=odh::EntityKind::avatar&&!entity->ui_ghost)
        throw odh::FormatError("replacement object lacks current artwork binding"+artwork_identity(id,generation,guid,*entity));
    return true;
}
}
bool SavedLotRenderer::can_set_walls_down(std::string* diagnostic) {
    if(diagnostic)diagnostic->clear();
    if(!walls_){if(diagnostic)*diagnostic="wall resources are unavailable";return false;}
    try{
        const auto check=[&](std::int16_t id,std::uint64_t generation,std::uint32_t guid){
            const auto* entity=saved_.world->find(id);if(!entity)return true;
            if(entity->generation!=generation||entity->kind!=odh::EntityKind::object||entity->ui_ghost||
               !entity->definition||entity->definition->guid!=guid)
                throw odh::FormatError("cutaway preflight object identity changed"+artwork_identity(id,generation,guid,*entity));
            if(!entity->position)return true;
            auto hidden=cutaway_hidden_for(*entity,nullptr,true);
            if(!hidden){const auto graphics=entity_graphics(id);hidden=cutaway_hidden_for(*entity,&graphics,true);}
            if(!hidden){
                if(diagnostic)*diagnostic="walls-down visibility needs actual wall-use state for object "+std::to_string(id);
                return false;
            }
            return true;
        };
        for(const auto& raw:saved_.records){const auto id=static_cast<std::int16_t>(raw.saved.object_id);
            const auto found=object_generations_.find(id);
            if(found!=object_generations_.end()&&!runtime_objects_.count(id)&&
               !retired_import(*saved_.world,id,found->second,raw.saved.guid)&&!check(id,found->second,raw.saved.guid))return false;}
        for(const auto& item:runtime_objects_)if(!check(item.first,item.second.generation,item.second.guid))return false;
        return true;
    }catch(const std::exception& error){if(diagnostic)*diagnostic=error.what();return false;}
}
bool SavedLotRenderer::set_walls_down(bool down) {
    if(down&&!can_set_walls_down())return false;
    view_.walls_down=down;return true;
}
void SavedLotRenderer::set_preview_hidden_objects(const std::vector<std::pair<std::int16_t,std::uint64_t>>& values){
    if(values.size()>4096)throw odh::FormatError("held object presentation exceeds identity budget");
    std::set<std::pair<std::int16_t,std::uint64_t>> next;
    for(const auto& identity:values)if(identity.first<=0||!identity.second||!next.insert(identity).second)
        throw odh::FormatError("held object presentation contains invalid or duplicate identity");
    preview_hidden_objects_.swap(next);
}
void SavedLotRenderer::set_room_lighting(std::optional<odh_host::RoomLightingView> lighting,
    std::function<std::optional<std::uint16_t>(std::int16_t,std::uint64_t)> visual_room){
    if(lighting){
        if(!runtime_rooms_||lighting->room_colors.size()!=runtime_rooms_->rooms.size()||!visual_room)
            throw odh::FormatError("basic lighting requires matching runtime rooms and actual visual-room authority");
        for(std::size_t room=0;room<lighting->room_colors.size();++room)
            (void)odh_host::room_lighting_view_color(*lighting,static_cast<std::uint16_t>(room));
    }
    lighting_=std::move(lighting);visual_room_=std::move(visual_room);
}

SavedLotFrame SavedLotRenderer::render(const odh::WorldProjection& camera,
                                      const odh::RasterFrame& avatar_frame,bool record_picks,
                                      bool include_objects) {
    return render(camera,std::vector<SavedLotAvatarLayer>{{avatar_frame,0}},record_picks,include_objects);
}

SavedLotFrame SavedLotRenderer::render(const odh::WorldProjection& camera,
                                      const std::vector<SavedLotAvatarLayer>& avatar_layers,
                                      bool record_picks,bool include_objects) {
    if(avatar_layers.size()>64)
        throw odh::FormatError("live saved lot avatar layer count exceeds budget");
    auto image=odh::rasterize(camera.width,camera.height,{},{});
    SavedLotFrame result;
    if(view_.terrain_theme){
        auto surface=odh::terrain_surface(render_architecture(),camera,*view_.terrain_theme,0,record_picks);
        if(lighting_){
            if(surface.room_samples.size()!=surface.triangles.size())throw odh::FormatError("terrain lighting samples unavailable");
            for(std::size_t i=0;i<surface.triangles.size();++i){const auto& p=surface.room_samples[i];
                const auto room=odh::geometric_room_at_runtime(*runtime_rooms_,p[0],p[1],static_cast<std::int16_t>(p[2]));
                if(!room)throw odh::FormatError("terrain lighting room unavailable");
                const auto tint=odh_host::room_lighting_gamma_multiplier(odh_host::room_lighting_view_color(*lighting_,*room));
                for(auto& vertex:surface.triangles[i].vertices)for(unsigned c=0;c<3;++c)vertex.color[c]*=tint[c];
            }
        }
        result.terrain_triangles=surface.triangles.size();
        result.terrain_reserved_bytes=surface.triangles.capacity()*sizeof(odh::RasterTriangle);
        odh::RasterOptions options;options.lighting=true;options.directional_up_light=true;options.vertex_colors=true;options.record_owner_ids=record_picks;
        const odh::BitmapImage white{1,1,32,true,{{255,255,255,255}},0};
        image=odh::rasterize(camera.width,camera.height,surface.triangles,{white},options);
    }
    std::function<std::array<float,3>(std::uint32_t,std::uint32_t,std::uint8_t)> floor_light;
    if(lighting_)floor_light=[&](auto x,auto y,auto level){return odh_host::room_lighting_gamma_multiplier(
        odh_host::room_lighting_view_color(*lighting_,odh_host::room_lighting_tile_room(*runtime_rooms_,x,y,level)));};
    auto floor=floors_.render(render_architecture(),camera,true,0,view_.level,record_picks,floor_light,&image);
    result.floor_draws=floor.draws.size();result.floor_triangles=floor.triangles;
    result.floor_texture_bytes=floor.texture_bytes;
    image=std::move(floor.image);
    std::vector<float> avatar_pick_depth;
    if(record_picks){
        const auto pixels=std::uint64_t{camera.width}*camera.height;
        if(!camera.width||!camera.height||pixels>1048576)
            throw odh::FormatError("live saved lot avatar pick viewport outside profile");
        result.avatar_pick_ids.assign(static_cast<std::size_t>(pixels),0);
        avatar_pick_depth.assign(static_cast<std::size_t>(pixels),std::numeric_limits<float>::infinity());
    }
    for(const auto& avatar_layer:avatar_layers){
        auto lit_image=std::optional<odh::RasterFrame>{};
        if(lighting_){
            const auto* actor=saved_.world->find(avatar_layer.entity_id);
            if(!actor||actor->generation!=avatar_layer.entity_generation||actor->kind!=odh::EntityKind::avatar)
                throw odh::FormatError("basic avatar lighting requires actual live identity");
            const auto room=visual_room_(actor->id,actor->generation);
            if(!room)throw odh::FormatError("basic avatar visual room unresolved");
            const auto tint=odh_host::room_lighting_gamma_multiplier(odh_host::room_lighting_view_color(*lighting_,*room));
            lit_image=avatar_layer.image;
            if(lit_image->pixels.size()!=lit_image->depth.size())throw odh::FormatError("avatar lighting arrays invalid");
            for(std::size_t i=0;i<lit_image->pixels.size();++i)if(std::isfinite(lit_image->depth[i]))
                for(unsigned c=0;c<3;++c)lit_image->pixels[i][c]=static_cast<std::uint8_t>(std::clamp(
                    std::floor(double(lit_image->pixels[i][c])*tint[c]+0.5),0.0,255.0));
        }
        const auto& layer=lit_image?*lit_image:avatar_layer.image;
        if(layer.width!=camera.width||layer.height!=camera.height||
           layer.pixels.size()!=image.pixels.size()||layer.depth.size()!=image.depth.size())
            throw odh::FormatError("live saved lot avatar layer dimensions invalid");
        if(!layer.owner_ids.empty()&&layer.owner_ids.size()!=layer.pixels.size())
            throw odh::FormatError("live saved lot avatar owner buffer invalid");
        if(record_picks){
            for(std::size_t i=0;i<layer.depth.size();++i){
                if(layer.depth[i]<image.depth[i]){
                    result.avatar_pick_ids[i]=!layer.owner_ids.empty()&&layer.owner_ids[i]
                        ?avatar_layer.pick_id:0;
                    avatar_pick_depth[i]=layer.depth[i];
                }
            }
        }
        // The legacy single-avatar API retains its historical owner merge.
        // Multi-resident layers use a dedicated avatar buffer and must not
        // leak resident tokens into the ground buffer.
        const bool legacy_owner_merge=avatar_layers.size()==1&&avatar_layer.pick_id==0;
        if(legacy_owner_merge)image=odh::merge_raster(image,layer);
        else {
            auto visual_layer=layer;visual_layer.owner_ids.clear();
            image=odh::merge_raster(image,visual_layer);
        }
    }
    if(walls_){
        // A completed source load may have changed flags in EP6 after its
        // style event. Use that event-owned appearance until its lifecycle is
        // invalidated; otherwise retain the established live derivation path.
        const auto& event_appearance=runtime_wall_appearance();
        std::optional<SavedWallAppearance> derived;
        if(!event_appearance)derived=saved_wall_appearance(saved_,definitions_,render_architecture(),true);
        const auto& appearance=event_appearance?*event_appearance:derived->appearance;
        std::function<std::array<float,3>(std::uint32_t,std::uint32_t,std::uint8_t,std::uint8_t)> wall_light;
        if(lighting_)wall_light=[&](auto x,auto y,auto level,auto orientation){return odh_host::room_lighting_view_color(*lighting_,
            odh_host::room_lighting_wall_room(*runtime_rooms_,x,y,level,orientation,camera.rotation));};
        auto wall=walls_->render(render_architecture(),appearance,camera,image,
                                true,0,odh::WallView{view_.level,view_.walls_down},wall_light);
        result.wall_layers=wall.draws.size();result.wall_junctions=wall.junctions.size();
        result.wall_cached_frame_bytes=wall.cached_frame_bytes;
        result.wall_tested_samples=wall.composite.tested_samples;
        result.wall_shaded_samples=wall.composite.shaded_samples;
        image=std::move(wall.composite.image);
    }
    if(record_picks){
        for(std::size_t i=0;i<result.avatar_pick_ids.size();++i)
            if(image.depth[i]<avatar_pick_depth[i])result.avatar_pick_ids[i]=0;
    }
    if(!include_objects){
        result.ground_pick_ids=std::move(image.owner_ids);
        result.image=std::move(image);
        return result;
    }
    for(auto it=cutaway_hidden_.begin();it!=cutaway_hidden_.end();){
        const auto* live=saved_.world->find(it->first.first);
        if(!live||live->generation!=it->first.second||live->kind!=odh::EntityKind::object)it=cutaway_hidden_.erase(it);
        else ++it;
    }
    const auto remember_cutaway=[&](std::int16_t id,std::uint64_t generation,bool hidden){
        const auto key=std::make_pair(id,generation);
        if(!cutaway_hidden_.count(key)&&cutaway_hidden_.size()>=4096)
            throw odh::FormatError("live cutaway component state exceeds object budget");
        cutaway_hidden_[key]=hidden;
    };
    std::vector<SavedObjectVisual> visuals;
    const auto append=[&](std::int16_t id,std::uint64_t generation,std::uint32_t guid,
                          const std::vector<std::int16_t>& flags){
        const auto* entity=saved_.world->find(id);
        if(!entity){++result.object_removed;return;}
        if(entity->generation!=generation||entity->kind!=odh::EntityKind::object||
           entity->ui_ghost||!entity->definition||entity->definition->guid!=guid)
            throw odh::FormatError("live object artwork identity changed"+artwork_identity(id,generation,guid,*entity));
        if(preview_hidden_objects_.count({id,generation})){
            ++result.object_hidden;return;
        }
        if(!entity->position){++result.object_outside;return;}
        const odh::VmFrame frame{odh::routine_key(4096),0,{id,id,id,0},{},{}};
        const auto hidden=saved_.world->read(frame,3,34),graphic=saved_.world->read(frame,3,0);
        if(!hidden||!graphic)throw odh::FormatError("live saved object visibility/graphic unavailable");
        auto cutaway=cutaway_hidden_for(*entity,nullptr,view_.walls_down);
        if(cutaway&&(*hidden||*cutaway)){
            remember_cutaway(id,generation,*cutaway);++result.object_hidden;return;
        }
        const auto graphics=entity_graphics(id);
        const auto* anchor=saved_.world->find(graphics.container?graphics.container:id);
        if(!anchor||!anchor->position)throw odh::FormatError("live saved object graphics anchor unavailable");
        if(!cutaway)cutaway=cutaway_hidden_for(*entity,&graphics,view_.walls_down);
        if(!cutaway)throw odh::FormatError("walls-down visibility requires unavailable runtime wall-use state");
        remember_cutaway(id,generation,*cutaway);
        if(*hidden||*cutaway){++result.object_hidden;return;}
        if(anchor->position->level>view_.level){++result.object_above_level;return;}
        visuals.push_back({id,guid,*graphic,graphics.direction,graphics.tile_position,flags});
        if(lighting_){const auto room=visual_room_(id,generation);
            if(!room)throw odh::FormatError("basic object visual room unresolved");
            if(*room==65533)visuals.back().grayscale=true;
            else if(*room==65534)visuals.back().invert=true;
            else if(*room!=0)visuals.back().linear_light=odh_host::room_lighting_view_color(*lighting_,*room);
        }
        if(record_picks)result.object_pick_identities.emplace(id,SavedLotObjectIdentity{guid,generation});
    };
    for(const auto& raw:saved_.records){
        const auto id=static_cast<std::int16_t>(raw.saved.object_id);
        const auto identity=object_generations_.find(id);
        if(identity!=object_generations_.end()&&!runtime_objects_.count(id)){
            if(retired_import(*saved_.world,id,identity->second,raw.saved.guid)){++result.object_removed;continue;}
            append(id,identity->second,raw.saved.guid,raw.common.dynamic_sprite_flags);
        }
    }
    for(const auto& [id,binding]:runtime_objects_){
        std::vector<std::int16_t> flags;
        if(const auto* entity=saved_.world->find(id)){
            auto bits=entity->dynamic_sprite_flags;
            for(unsigned bit=0;bits;++bit,bits>>=1)flags.push_back((bits&1U)?1:0);
        }
        append(id,binding.generation,binding.guid,flags);
    }
    // Walls have binary alpha and establish the opaque base before objects;
    // reversing these passes loses the wall color behind translucent objects.
    auto objects=objects_.render(visuals,camera,image,record_picks);
    if(record_picks){result.pick_width=camera.width;result.pick_height=camera.height;}
    result.object_pick_ids=std::move(objects.composite.owner_ids);
    result.objects=objects.records.size();result.object_layers=objects.layers;
    result.object_offscreen_layers=objects.offscreen_layers;
    result.object_cached_frame_bytes=objects.cached_frame_bytes;
    result.object_tested_samples=objects.composite.tested_samples;
    result.object_shaded_samples=objects.composite.shaded_samples;
    image=std::move(objects.composite.image);
    if(record_picks){
        for(std::size_t i=0;i<result.avatar_pick_ids.size();++i)
            if(image.depth[i]<avatar_pick_depth[i])result.avatar_pick_ids[i]=0;
    }
    result.ground_pick_ids=std::move(image.owner_ids);
    result.image=std::move(image);return result;
}

std::optional<SavedObjectSelection> SavedLotRenderer::pick(const SavedLotFrame& frame,
                                                         std::int32_t x,std::int32_t y) const {
    const auto count=std::uint64_t{frame.pick_width}*frame.pick_height;
    if(!frame.pick_width||!frame.pick_height||frame.pick_width>2048||frame.pick_height>2048||
       count>1048576||count!=frame.object_pick_ids.size()||frame.object_pick_identities.size()>4096)
        throw odh::FormatError("live saved object pick buffer invalid");
    if(x<0||y<0||static_cast<std::uint32_t>(x)>=frame.pick_width||static_cast<std::uint32_t>(y)>=frame.pick_height)
        return {};
    const auto token=frame.object_pick_ids[std::size_t(y)*frame.pick_width+static_cast<std::uint32_t>(x)];
    if(!token||token>32767)return {};
    const auto id=static_cast<std::int16_t>(token);
    const auto* entity=saved_.world->find(id);
    if(!entity||entity->kind!=odh::EntityKind::object||entity->ui_ghost||
       !entity->definition||!entity->position)return {};
    const auto presented=frame.object_pick_identities.find(id);
    if(presented==frame.object_pick_identities.end()||
       presented->second.generation!=entity->generation||
       presented->second.guid!=entity->definition->guid)return {};
    const auto runtime=runtime_objects_.find(id);
    if(runtime!=runtime_objects_.end()){
        if(entity->generation!=runtime->second.generation||entity->definition->guid!=runtime->second.guid)return {};
    }else{
        const auto identity=object_generations_.find(id);
        const auto record=std::find_if(saved_.records.begin(),saved_.records.end(),
            [&](const auto& raw){return raw.saved.object_id==token;});
        if(identity==object_generations_.end()||entity->generation!=identity->second||
           record==saved_.records.end()||record->saved.guid!=entity->definition->guid)return {};
    }
    const auto* definition=definitions_.unique(entity->definition->guid);
    if(!definition)return {};
    const odh::VmFrame context{odh::routine_key(4096),0,{id,id,id,0},{},{}};
    const auto hidden=saved_.world->read(context,3,34);
    if(!hidden||*hidden)return {};
    return SavedObjectSelection{id,entity->definition->guid,entity->generation,
                                definition->definition.tree_table_id};
}

std::optional<SavedGroundSelection> SavedLotRenderer::pick_ground(
    const SavedLotFrame& frame,std::int32_t x,std::int32_t y) const {
    const auto count=std::uint64_t{frame.pick_width}*frame.pick_height;
    if(!frame.pick_width||!frame.pick_height||frame.pick_width>2048||frame.pick_height>2048||
       count>1048576||frame.image.width!=frame.pick_width||frame.image.height!=frame.pick_height||
       count!=frame.ground_pick_ids.size())
        throw odh::FormatError("live saved ground pick buffer invalid");
    if(x<0||y<0||static_cast<std::uint32_t>(x)>=frame.pick_width||
       static_cast<std::uint32_t>(y)>=frame.pick_height)return {};
    const auto token=frame.ground_pick_ids[std::size_t(y)*frame.pick_width+static_cast<std::uint32_t>(x)];
    const auto cell=odh::decode_ground_owner(token);
    if(!cell||cell->x+1>=render_architecture().width||
       cell->y+1>=render_architecture().height||cell->level>view_.level||!runtime_rooms_)return {};
    const odh::EntityPosition center{static_cast<std::int16_t>(cell->x*16+8),
        static_cast<std::int16_t>(cell->y*16+8),cell->level,0};
    const auto room=runtime_room_at(center);
    if(!room)return {};
    return SavedGroundSelection{cell->x,cell->y,cell->level,*room};
}

std::optional<std::uint16_t> SavedLotRenderer::runtime_room_at(
    const odh::EntityPosition& position) const {
    return runtime_rooms_ ? odh::geometric_room_at_runtime(*runtime_rooms_,position.x,position.y,position.level)
                          : std::nullopt;
}
