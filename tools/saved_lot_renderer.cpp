#include "saved_lot_renderer.hpp"
#include "saved_wall_appearance.hpp"
#include "odh/compositor.hpp"
#include <algorithm>

namespace {
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
    const std::string& floors,const std::optional<std::string>& walls,SavedLotView view)
    :architecture_(std::move(architecture)),saved_(saved),definitions_(definitions),graphics_(definitions),
     view_(view),floors_(floors) {
    if(!saved_.world||view_.level<1||view_.level>2||
       (view_.terrain_theme&&unsigned(*view_.terrain_theme)>=8)||
       (view_.walls_down&&!walls))
        throw odh::FormatError("live saved lot World/view outside profile");
    for(const auto& map:architecture_.maps)
        if(!map.second.entries.empty()&&(map.first=="FLRm"||(walls&&map.first=="WALm")))
            throw odh::FormatError("live saved lot custom materials require a resource resolver");
    group_centers_=terrain_group_centers(saved_);
    if(walls)walls_=std::make_unique<SavedWallRenderer>(*walls,definitions_);
}

odh::GraphicsEntity SavedLotRenderer::avatar(std::int16_t id) {
    auto graphics=graphics_.entity(*saved_.world,id);
    const auto* entity=saved_.world->find(graphics.container?graphics.container:id);
    if(graphics.kind!=odh::EntityKind::avatar||!entity||
       !entity->position||entity->position->level>view_.level)
        throw odh::FormatError("live saved lot avatar outside selected view");
    std::array<float,2> xy{static_cast<float>(entity->position->x)/16,static_cast<float>(entity->position->y)/16};
    if(entity->kind==odh::EntityKind::object){
        const auto center=group_centers_.find(entity->id);if(center!=group_centers_.end())xy=center->second;
    }
    graphics.tile_position[2]+=odh::terrain_interp_altitude(architecture_.architecture,xy[0],xy[1]);
    return graphics;
}

SavedLotFrame SavedLotRenderer::render(const odh::WorldProjection& camera,
                                      const odh::RasterFrame& avatar_frame) {
    auto image=odh::rasterize(camera.width,camera.height,{},{});
    SavedLotFrame result;
    if(view_.terrain_theme){
        auto surface=odh::terrain_surface(architecture_.architecture,camera,*view_.terrain_theme);
        result.terrain_triangles=surface.triangles.size();
        result.terrain_reserved_bytes=surface.triangles.capacity()*sizeof(odh::RasterTriangle);
        odh::RasterOptions options;options.lighting=true;options.directional_up_light=true;options.vertex_colors=true;
        const odh::BitmapImage white{1,1,32,true,{{255,255,255,255}},0};
        image=odh::rasterize(camera.width,camera.height,surface.triangles,{white},options);
    }
    auto floor=floors_.render(architecture_.architecture,camera,true,0,view_.level);
    result.floor_draws=floor.draws.size();result.floor_triangles=floor.triangles;
    result.floor_texture_bytes=floor.texture_bytes;
    image=odh::merge_raster(image,floor.image);
    image=odh::merge_raster(image,avatar_frame);
    if(walls_){
        // Derive from current World banks and positions on every capture.
        const auto appearance=saved_wall_appearance(saved_,definitions_,architecture_.architecture);
        auto wall=walls_->render(architecture_.architecture,appearance.appearance,camera,image,
                                true,0,odh::WallView{view_.level,view_.walls_down});
        result.wall_layers=wall.draws.size();result.wall_junctions=wall.junctions.size();
        result.wall_cached_frame_bytes=wall.cached_frame_bytes;
        result.wall_tested_samples=wall.composite.tested_samples;
        result.wall_shaded_samples=wall.composite.shaded_samples;
        image=std::move(wall.composite.image);
    }
    result.image=std::move(image);return result;
}
