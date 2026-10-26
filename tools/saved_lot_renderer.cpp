#include "saved_lot_renderer.hpp"
#include "saved_wall_appearance.hpp"
#include "odh/compositor.hpp"
#include "odh/ground_owner.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const odh::ArchitectureResourceCatalog& material_catalog(const std::optional<SavedLotMaterialOptions>& options) {
    static const odh::ArchitectureResourceCatalog empty;
    return options?options->catalog:empty;
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
            const auto found=records.find(id);const auto* entity=saved.world->find(id);
            if(found==records.end()||!entity||entity->kind!=odh::EntityKind::object)
                throw odh::FormatError("live saved terrain group member unavailable");
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
    if(slots_.size()>=limits_.slot_tables||slots_.size()>=16||records_>limits_.slot_records||records_>4096)
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
            throw odh::FormatError("saved lot external material alias unresolved");
        prepared_=odh::prepare_saved_architecture(architecture_.architecture,aliases_->aliases,
            materials->preparation,materials->preparation_limits);
        // Remap misses without declarations can represent global road/base
        // patterns; they are preserved and exposed in preparation diagnostics.
        prepared_render_=prepared_render_architecture(architecture_.architecture,*prepared_);
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
        const auto* entity=saved_.world->find(id);
        if(entity&&entity->kind==odh::EntityKind::object)object_generations_.emplace(id,entity->generation);
    }
    if(walls)walls_=std::make_unique<SavedWallRenderer>(*walls,definitions_,material_catalog(materials));
}

void SavedLotRenderer::set_runtime_objects(const std::vector<SavedLotRuntimeObject>& objects) {
    if(objects.size()>4096)
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
           candidate->chunk_id!=object.definition_chunk_id||candidate->definition.is_multitile)
            throw odh::FormatError("live runtime object binding World/resource mismatch");
        replacement.emplace(object.id,object);
    }
    std::size_t total=object_generations_.size();
    for(const auto& [id,binding]:replacement){
        (void)binding;if(!object_generations_.count(id))++total;
    }
    if(total>4096)throw odh::FormatError("live runtime and saved object binding budget exceeded");
    runtime_objects_.swap(replacement);
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

odh::GraphicsEntity SavedLotRenderer::avatar(std::int16_t id) {
    auto graphics=entity_graphics(id);
    const auto* entity=saved_.world->find(graphics.container?graphics.container:id);
    if(graphics.kind!=odh::EntityKind::avatar||!entity||
       !entity->position||entity->position->level>view_.level)
        throw odh::FormatError("live saved lot avatar outside selected view");
    return graphics;
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
        const auto center=group_centers_.find(entity->id);if(center!=group_centers_.end()&&!runtime_objects_.count(entity->id))xy=center->second;
    }
    graphics.tile_position[2]+=odh::terrain_interp_altitude(render_architecture(),xy[0],xy[1]);
    return graphics;
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
        result.terrain_triangles=surface.triangles.size();
        result.terrain_reserved_bytes=surface.triangles.capacity()*sizeof(odh::RasterTriangle);
        odh::RasterOptions options;options.lighting=true;options.directional_up_light=true;options.vertex_colors=true;options.record_owner_ids=record_picks;
        const odh::BitmapImage white{1,1,32,true,{{255,255,255,255}},0};
        image=odh::rasterize(camera.width,camera.height,surface.triangles,{white},options);
    }
    auto floor=floors_.render(render_architecture(),camera,true,0,view_.level,record_picks);
    result.floor_draws=floor.draws.size();result.floor_triangles=floor.triangles;
    result.floor_texture_bytes=floor.texture_bytes;
    image=odh::merge_raster(image,floor.image);
    std::vector<float> avatar_pick_depth;
    if(record_picks){
        const auto pixels=std::uint64_t{camera.width}*camera.height;
        if(!camera.width||!camera.height||pixels>1048576)
            throw odh::FormatError("live saved lot avatar pick viewport outside profile");
        result.avatar_pick_ids.assign(static_cast<std::size_t>(pixels),0);
        avatar_pick_depth.assign(static_cast<std::size_t>(pixels),std::numeric_limits<float>::infinity());
    }
    for(const auto& avatar_layer:avatar_layers){
        const auto& layer=avatar_layer.image;
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
        // Derive from current World banks and positions on every capture.
        const auto appearance=saved_wall_appearance(saved_,definitions_,render_architecture(),true);
        auto wall=walls_->render(render_architecture(),appearance.appearance,camera,image,
                                true,0,odh::WallView{view_.level,view_.walls_down});
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
    std::vector<SavedObjectVisual> visuals;
    const auto append=[&](std::int16_t id,std::uint64_t generation,std::uint32_t guid,
                          const std::vector<std::int16_t>& flags){
        const auto* entity=saved_.world->find(id);
        if(!entity){++result.object_removed;return;}
        if(entity->generation!=generation||entity->kind!=odh::EntityKind::object||
           entity->ui_ghost||!entity->definition||entity->definition->guid!=guid)
            throw odh::FormatError("live saved object identity changed; refresh renderer");
        if(!entity->position){++result.object_outside;return;}
        const odh::VmFrame frame{odh::routine_key(4096),0,{id,id,id,0},{},{}};
        const auto hidden=saved_.world->read(frame,3,34),graphic=saved_.world->read(frame,3,0);
        if(!hidden||!graphic)throw odh::FormatError("live saved object visibility/graphic unavailable");
        if(*hidden){++result.object_hidden;return;}
        const auto graphics=entity_graphics(id);
        const auto* anchor=saved_.world->find(graphics.container?graphics.container:id);
        if(!anchor||!anchor->position)throw odh::FormatError("live saved object graphics anchor unavailable");
        if(anchor->position->level>view_.level){++result.object_above_level;return;}
        if(view_.walls_down&&anchor->position->level==view_.level){
            const auto flags=saved_.world->read(frame,3,8);
            if(!flags)throw odh::FormatError("live saved object cutaway flags unavailable");
            if(static_cast<std::uint16_t>(*flags)&1024U)
                throw odh::FormatError("HideForCutaway needs runtime wall occupancy reconstruction");
        }
        visuals.push_back({id,guid,*graphic,graphics.direction,graphics.tile_position,flags});
        if(record_picks)result.object_pick_identities.emplace(id,SavedLotObjectIdentity{guid,generation});
    };
    for(const auto& raw:saved_.records){
        const auto id=static_cast<std::int16_t>(raw.saved.object_id);
        const auto identity=object_generations_.find(id);
        if(identity!=object_generations_.end()&&!runtime_objects_.count(id))
            append(id,identity->second,raw.saved.guid,raw.common.dynamic_sprite_flags);
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
