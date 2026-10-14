#include "saved_resources.hpp"
#include "saved_appearance.hpp"
#include "saved_object_renderer.hpp"
#include "saved_floor_renderer.hpp"
#include "saved_wall_renderer.hpp"
#include "saved_wall_appearance.hpp"
#include "saved_architecture_resources.hpp"
#include "odh/terrain.hpp"
#include "odh/terrain_surface.hpp"
#include "saved_tail_json.hpp"
#include "json.hpp"
#include "odh/graphics_state.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>

namespace {
void different(const std::string& output,const std::string& input) {
    if(std::filesystem::weakly_canonical(output)==std::filesystem::weakly_canonical(input) ||
       (std::filesystem::exists(output)&&std::filesystem::exists(input)&&std::filesystem::equivalent(output,input)))
        throw odh::FormatError("saved scene output aliases an input or another output");
}
void write_frame(const odh::RasterFrame& frame,const std::string& color,const std::string& depth) {
    std::ofstream ppm(color,std::ios::binary|std::ios::trunc);
    if(!ppm)throw odh::FormatError("cannot open saved scene PPM output");
    ppm<<"P6\n"<<frame.width<<' '<<frame.height<<"\n255\n";
    for(const auto& pixel:frame.pixels)for(unsigned c=0;c<3;++c)ppm.put(static_cast<char>(pixel[c]));
    ppm.close();if(!ppm)throw odh::FormatError("cannot write saved scene PPM output");
    if(depth.empty())return;
    std::ofstream out(depth,std::ios::binary|std::ios::trunc);
    if(!out)throw odh::FormatError("cannot open saved scene depth output; PPM may remain");
    for(float z:frame.depth){std::uint32_t bits;static_assert(sizeof(bits)==sizeof(z));std::memcpy(&bits,&z,4);
        for(unsigned c=0;c<4;++c)out.put(static_cast<char>((bits>>(8*c))&255));}
    out.close();if(!out)throw odh::FormatError("cannot write saved scene depth output; PPM may remain");
}
void animation_json(std::ostream& out,const std::optional<SavedAnimationAppearance>& animation){
    if(!animation){out<<"null";return;}const auto& a=*animation;
    out<<"{\"raw\":"<<json_bytes(a.raw)<<",\"entry\":"<<json_bytes(a.entry)<<",\"clip\":"<<json_bytes(a.header.name)
       <<",\"cfp_entry\":"<<json_bytes(a.cfp_entry)
       <<",\"xskill\":"<<json_bytes(a.header.xskill)<<",\"frame\":"<<a.frame<<",\"normalized_frame\":"<<a.normalized_frame
       <<",\"weight_milli\":"<<a.weight_milli<<",\"speed_milli\":"<<a.speed_milli<<",\"priority\":"<<a.priority
       <<",\"loop\":"<<(a.loop?"true":"false")<<",\"backwards\":"<<(a.backwards?"true":"false")<<'}';
}
}
int main(int argc,char** argv){try{
    if(argc<5)throw odh::FormatError("usage: HOUSE_IFF ANIMATION_FAR TEXTURES_FAR OUTPUT_PPM --skeleton ENTRY NAME --object-definitions FAR [--object-definition-file IFF] [--neighborhood IFF] [--objects] [--floors FLOORS_IFF] [--walls WALLS_IFF] [--terrain-theme 0..7] [--depth-output FILE] [--width N --height N --zoom N --rotation N]");
    std::vector<SavedDefinitionSource> sources;std::optional<std::string> neighborhood;
    std::string skeleton_entry,skeleton_name,depth;std::optional<std::string> floors_path,walls_path;std::optional<odh::TerrainTheme> terrain_theme;bool draw_objects=false;odh::WorldProjection camera;camera.width=640;camera.height=400;
    std::set<std::string> seen;
    for(int n=5;n<argc;++n){const std::string option=argv[n];
        auto need=[&](int count){if(argc-n-1<count)throw odh::FormatError("incomplete saved scene option");};
        if(option=="--object-definitions"||option=="--object-definition-file"){
            need(1);if(sources.size()>=16)throw odh::FormatError("definition source count exceeds limit");
            sources.push_back({argv[++n],option=="--object-definition-file"});continue;}
        if(!seen.insert(option).second)throw odh::FormatError("duplicate saved scene option");
        if(option=="--objects")draw_objects=true;
        else if(option=="--floors"){need(1);floors_path=argv[++n];if(floors_path->empty())throw odh::FormatError("empty floor input");}
        else if(option=="--walls"){need(1);walls_path=argv[++n];if(walls_path->empty())throw odh::FormatError("empty wall input");}
        else if(option=="--terrain-theme"){need(1);auto value=selector(argv[++n]);if(value>=8)throw odh::FormatError("terrain theme exceeds range");terrain_theme=static_cast<odh::TerrainTheme>(value);}
        else if(option=="--skeleton"){need(2);skeleton_entry=argv[++n];skeleton_name=argv[++n];}
        else if(option=="--neighborhood"){need(1);neighborhood=argv[++n];}
        else if(option=="--depth-output"){need(1);depth=argv[++n];if(depth.empty())throw odh::FormatError("empty depth output");}
        else if(option=="--width"||option=="--height"||option=="--zoom"||option=="--rotation"){
            need(1);auto value=selector(argv[++n]);
            if(option=="--width")camera.width=value;else if(option=="--height")camera.height=value;
            else {if(value>3)throw odh::FormatError("saved scene zoom/rotation exceeds range");
                if(option=="--zoom")camera.zoom=static_cast<std::uint8_t>(value);else camera.rotation=static_cast<std::uint8_t>(value);}}
        else throw odh::FormatError("invalid saved scene option");
    }
    if(sources.empty()||skeleton_entry.empty()||skeleton_name.empty()||std::string(argv[4]).empty())
        throw odh::FormatError("saved scene requires definitions, skeleton and output");
    if(walls_path&&!floors_path)throw odh::FormatError("walls require --floors for shared terrain profile");
    if(terrain_theme&&!floors_path)throw odh::FormatError("terrain theme requires --floors");
    std::vector<std::string> inputs{argv[1],argv[2],argv[3]};for(const auto& s:sources)inputs.push_back(s.path);
    if(neighborhood)inputs.push_back(*neighborhood);
    if(floors_path)inputs.push_back(*floors_path);
    if(walls_path)inputs.push_back(*walls_path);
    for(const auto& p:inputs){different(argv[4],p);if(!depth.empty())different(depth,p);}
    if(!depth.empty())different(argv[4],depth);
    auto index=saved_definition_index(sources);auto records=saved_world_records(argv[1],*index);
    std::optional<odh::NeighborContext> neighbors;if(neighborhood)neighbors.emplace(saved_neighbors(*neighborhood));
    auto world=odh::make_saved_world(records,*index,neighbors?&*neighbors:nullptr);
    std::optional<SavedArchitectureResource> architecture;
    std::map<std::int16_t,std::array<float,2>> group_centers;
    if(floors_path){
        architecture=saved_architecture_resource(argv[1]);
        for(const auto& map:architecture->maps)if(map.first=="FLRm"&&!map.second.entries.empty())
            throw odh::FormatError("custom FLRm materials require a resource resolver");
        if(walls_path)for(const auto& map:architecture->maps)if(map.first=="WALm"&&!map.second.entries.empty())
            throw odh::FormatError("custom WALm materials require a resource resolver");
        // Bounded no-overflow subset of the reference's short-coordinate group
        // average. Saved raw coordinates are retained, including absent members.
        std::map<std::int16_t,const odh::SavedWorldEntity*> by_id;
        for(const auto& r:world->records)by_id.emplace(static_cast<std::int16_t>(r.saved.object_id),&r);
        for(const auto& group:world->multitile_groups){
            if(group.members.empty())throw odh::FormatError("empty saved terrain group");
            std::int32_t x=0,y=0;
            for(auto id:group.members){const auto& p=by_id.at(id)->saved.prefix;x+=p.x;y+=p.y;
                if(x < -32768 || x > 32767 || y < -32768 || y > 32767)
                    throw odh::FormatError("terrain group short-coordinate overflow unsupported");}
            const auto count=static_cast<std::int32_t>(group.members.size());
            const std::array<float,2> center{static_cast<float>(x/count)/16,static_cast<float>(y/count)/16};
            for(auto id:group.members)if(!group_centers.emplace(id,center).second)
                throw odh::FormatError("duplicate terrain group member");
        }
    }
    auto elevation=[&](std::int16_t id){
        if(!architecture)return 0.0f;
        const auto* e=world->world->find(id);
        if(!e||!e->position)throw odh::FormatError("terrain entity position unavailable");
        if(e->attachment){e=world->world->find(e->attachment->container);
            if(!e||!e->position||e->attachment)throw odh::FormatError("terrain containment unsupported");}
        auto xy=std::array<float,2>{static_cast<float>(e->position->x)/16,static_cast<float>(e->position->y)/16};
        if(e->kind==odh::EntityKind::object){const auto found=group_centers.find(e->id);if(found!=group_centers.end())xy=found->second;}
        return odh::terrain_interp_altitude(architecture->architecture,xy[0],xy[1]);
    };
    struct Actor {const odh::SavedWorldEntity* saved;odh::GraphicsEntity graphics;SavedAppearance appearance;};
    std::vector<Actor> actors;std::vector<std::int16_t> outside;
    SavedAppearanceResources resources(argv[2],argv[3],skeleton_entry,skeleton_name);
    for(const auto& r:world->records){if(!r.tail.person)continue;
        auto id=static_cast<std::int16_t>(r.saved.object_id);
        if(!world->world->find(id)->position){outside.push_back(id);continue;}
        if(actors.size()>=16)throw odh::FormatError("saved scene avatar budget exceeded");
        actors.push_back({&r,odh::graphics_entity(*world->world,id),resources.resolve(*r.tail.person)});
        actors.back().graphics.tile_position[2]+=elevation(id);
    }
    if(actors.empty()&&!draw_objects&&!floors_path)throw odh::FormatError("saved scene has no positioned avatars");
    for(const auto& a:actors)for(std::size_t i=0;i<3;++i)camera.center_tile[i]+=a.graphics.tile_position[i]/static_cast<float>(actors.size());
    if(actors.empty()){
        std::size_t count=0;
        for(const auto& r:world->records){const auto* e=world->world->find(static_cast<std::int16_t>(r.saved.object_id));
            if(!e->position||e->kind!=odh::EntityKind::object||r.saved.prefix.object_data[34]!=0)continue;
            ++count;camera.center_tile[0]+=static_cast<float>(e->position->x)/16-.5f;camera.center_tile[1]+=static_cast<float>(e->position->y)/16-.5f;
            camera.center_tile[2]+=static_cast<float>(e->position->level-1)*2.95f+elevation(e->id);
        }
        if(!count)throw odh::FormatError("saved scene has no positioned visible entities");
        for(auto& c:camera.center_tile)c/=static_cast<float>(count);
    }
    odh::RasterOptions options;options.lighting=true;
    odh::RasterFrame image=odh::rasterize(camera.width,camera.height,{}, {},options);
    std::optional<odh::TerrainSurface> terrain_geometry;
    if(terrain_theme){
        terrain_geometry=odh::terrain_surface(architecture->architecture,camera,*terrain_theme);
        odh::RasterOptions ground_options;ground_options.lighting=true;ground_options.directional_up_light=true;ground_options.vertex_colors=true;
        const odh::BitmapImage white{1,1,32,true,{{255,255,255,255}},0};
        image=odh::rasterize(camera.width,camera.height,terrain_geometry->triangles,{white},ground_options);
    }
    std::optional<SavedFloorResult> floor_result;
    if(floors_path){SavedFloorRenderer renderer(*floors_path);floor_result=renderer.render(architecture->architecture,camera);
        if(terrain_geometry){image=odh::merge_raster(image,floor_result->image);floor_result->image=odh::RasterFrame{};}
        else image=std::move(floor_result->image);
    }
    std::optional<SavedWallRenderResult> wall_result;
    if(walls_path){
        auto appearance=saved_wall_appearance(*world,*index,architecture->architecture);
        SavedWallRenderer renderer(*walls_path,*index);
        wall_result=renderer.render(architecture->architecture,appearance.appearance,camera,image);
        image=std::move(wall_result->composite.image);
    }
    std::ostringstream report;report<<std::setprecision(17)<<"{\"evidence\":"<<json_bytes(draw_objects?"ODH_saved_World_avatar_object_CPU_snapshot":"ODH_saved_World_avatar_CPU_snapshot")<<",\"house\":"<<json_bytes(argv[1])
       <<",\"world_entities_restored\":true,\"saved_threads_restored\":false,\"architecture_loaded\":false,\"objects_rendered\":"<<(draw_objects?"true":"false")<<",\"original_visual_equivalence\":false"
       <<",\"entities\":"<<world->records.size()<<",\"avatars_rendered\":"<<actors.size()<<",\"out_of_world_avatars\":";
    odh_tools::saved_bank(report,outside);report<<",\"center_tile\":";odh_tools::saved_bank(report,camera.center_tile);
    report<<",\"architecture_decoded\":"<<(architecture?"true":"false")
          <<",\"floors_rendered\":"<<(floor_result?"true":"false")
          <<",\"walls_rendered\":"<<(wall_result?"true":"false")<<",\"grass_surface_rendered\":"<<(terrain_geometry?"true":"false")
          <<",\"grass_blades_rendered\":false,\"terrain_theme\":"<<(terrain_theme?std::to_string(unsigned(*terrain_theme)):"null")
          <<",\"terrain_whole_tiles\":"<<(terrain_geometry?terrain_geometry->whole_tiles:0)
          <<",\"terrain_half_tiles\":"<<(terrain_geometry?terrain_geometry->half_tiles:0)
          <<",\"terrain_triangles\":"<<(terrain_geometry?terrain_geometry->triangles.size():0)
          <<",\"terrain_geometry_bytes\":"<<(terrain_geometry?terrain_geometry->triangles.size()*sizeof(odh::RasterTriangle):0)
          <<",\"terrain_reserved_triangle_bytes\":"<<(terrain_geometry?terrain_geometry->triangles.capacity()*sizeof(odh::RasterTriangle):0)
          <<",\"terrain_lighting\":"<<(terrain_theme?json_bytes("CPU_directional_up_no_room_light_gamma_blades"):"null")
          <<",\"terrain_profile\":"
          <<(architecture?json_bytes("FreeSO_byte1_times10_bilinear_BaseAlt0_bounded_group_average"):"null")
          <<",\"floor_materials\":[";
    if(floor_result)for(std::size_t n=0;n<floor_result->materials.size();++n){if(n)report<<',';const auto& m=floor_result->materials[n];
        report<<"{\"pattern\":"<<m.pattern<<",\"sprite_id\":"<<m.sprite_id<<",\"palette_id\":"<<m.palette_id<<",\"width\":"<<m.width<<",\"height\":"<<m.height<<'}';}
    report<<"],\"floor_draws\":[";
    if(floor_result)for(std::size_t n=0;n<floor_result->draws.size();++n){if(n)report<<',';const auto& d=floor_result->draws[n];
        report<<"{\"x\":"<<d.x<<",\"y\":"<<d.y<<",\"level\":"<<unsigned(d.level)<<",\"pattern\":"<<d.pattern
              <<",\"half\":"<<unsigned(d.half)<<",\"altitude\":";odh_tools::saved_bank(report,d.altitude);report<<'}';}
    report<<"],\"floor_triangles\":"<<(floor_result?floor_result->triangles:0)
          <<",\"floor_texture_bytes\":"<<(floor_result?floor_result->texture_bytes:0)
          <<",\"floor_geometry_texture_pixel_visits\":"<<(floor_result?floor_result->geometry_texture_pixel_visits:0);
    if(wall_result){report<<",\"wall_profile\":"<<(wall_result?json_bytes("FreeSO_2D_standing_up_with_junctions_unlit_ODH_projection"):"null")
          <<",\"wall_layers\":"<<(wall_result?wall_result->draws.size():0)
          <<",\"wall_cached_assets\":"<<(wall_result?wall_result->cached_assets:0)
          <<",\"wall_cached_frames\":"<<(wall_result?wall_result->cached_frames:0)
          <<",\"wall_cached_pixels\":"<<(wall_result?wall_result->cached_pixels:0)
          <<",\"wall_cached_frame_bytes\":"<<(wall_result?wall_result->cached_frame_bytes:0)
          <<",\"wall_mask_fallbacks\":"<<(wall_result?wall_result->mask_fallbacks:0)
          <<",\"wall_tested_samples\":"<<(wall_result?wall_result->composite.tested_samples:0)
          <<",\"wall_shaded_samples\":"<<(wall_result?wall_result->composite.shaded_samples:0)
          <<",\"wall_transparent_samples\":"<<(wall_result?wall_result->composite.transparent_samples:0)
          <<",\"wall_occluded_samples\":"<<(wall_result?wall_result->composite.occluded_samples:0)
          <<",\"wall_draws\":[";
    if(wall_result)for(std::size_t n=0;n<wall_result->draws.size();++n){if(n)report<<',';const auto& d=wall_result->draws[n];const auto& f=d.face;
        report<<"{\"tile_x\":"<<f.x<<",\"tile_y\":"<<f.y<<",\"level\":"<<unsigned(f.level)
              <<",\"owner_x\":"<<f.owner_x<<",\"owner_y\":"<<f.owner_y<<",\"owner_segment\":"<<unsigned(f.owner_segment)
              <<",\"orientation\":"<<unsigned(f.orientation)<<",\"pattern\":"<<f.pattern<<",\"raw_style\":"<<f.style.raw_style
              <<",\"resource_guid\":"<<f.style.resource_guid<<",\"object_owner\":"<<f.object_owner<<",\"door\":"<<(f.door?"true":"false")
              <<",\"pattern_source\":"<<json_bytes(d.pattern_source)<<",\"mask_source\":"<<json_bytes(d.mask_source)<<",\"mask_asset\":"<<json_bytes(d.mask_asset)
              <<",\"pattern_sprite\":"<<d.pattern_sprite<<",\"pattern_palette\":"<<d.pattern_palette<<",\"mask_sprite\":"<<d.mask_sprite<<",\"mask_palette\":"<<d.mask_palette
              <<",\"pattern_palette_missing\":"<<(d.pattern_palette_missing?"true":"false")<<",\"mask_palette_missing\":"<<(d.mask_palette_missing?"true":"false")
              <<",\"mask_fallback\":"<<(d.mask_fallback?"true":"false")<<",\"x\":"<<d.x<<",\"y\":"<<d.y<<",\"width\":"<<d.width<<",\"height\":"<<d.height
              <<",\"back_depth\":"<<d.back_depth<<",\"front_depth_delta\":"<<d.front_depth_delta<<'}';}
    report<<"],\"wall_junctions_unmapped\":"<<wall_result->unmapped_junctions<<",\"wall_junction_draws\":[";
    for(std::size_t n=0;n<wall_result->junctions.size();++n){if(n)report<<',';const auto& d=wall_result->junctions[n];const auto& j=d.junction;
        report<<"{\"tile_x\":"<<j.x<<",\"tile_y\":"<<j.y<<",\"level\":"<<unsigned(j.level)<<",\"raw_flags\":"<<unsigned(j.raw_flags)
              <<",\"rotated_flags\":"<<unsigned(j.rotated_flags)<<",\"frame\":"<<unsigned(j.frame)<<",\"source\":"<<json_bytes(d.source)
              <<",\"sprite_id\":"<<d.sprite_id<<",\"palette_id\":"<<d.palette_id<<",\"palette_missing\":"<<(d.palette_missing?"true":"false")
              <<",\"x\":"<<d.x<<",\"y\":"<<d.y<<",\"width\":"<<d.width<<",\"height\":"<<d.height
              <<",\"back_depth\":"<<d.back_depth<<",\"front_depth_delta\":"<<d.front_depth_delta<<'}';}
    report<<']';}
    report<<",\"zoom\":"<<unsigned(camera.zoom)<<",\"rotation\":"<<unsigned(camera.rotation)<<",\"actors\":[";
    std::uint64_t vertices=0,triangles=0,textures=0,samples=0;
    for(std::size_t n=0;n<actors.size();++n){auto& a=actors[n];SceneRenderer renderer(a.appearance.scene);
        vertices+=renderer.vertices();triangles+=renderer.triangles();textures+=renderer.texture_bytes();
        if(vertices>65536||triangles>odh::RasterLimits{}.triangles||textures>4ULL*odh::RasterLimits{}.texture_pixels)
            throw odh::FormatError("saved scene cumulative avatar resources exceed budget");
        SceneState state;state.avatar_tile=a.graphics.tile_position;state.avatar_direction=a.graphics.direction;
        if(a.appearance.current){state.animation=&a.appearance.current->header;state.frame=a.appearance.current->frame;}
        auto layer=renderer.render(state,camera,options);image=odh::merge_raster(image,layer.image);
        samples+=renderer.cache_samples();if(samples>odh::AnimationLimits{}.samples)throw odh::FormatError("saved scene cumulative CFP budget exceeded");
        if(n)report<<',';
        report<<"{\"id\":"<<a.graphics.id<<",\"generation\":"<<a.graphics.generation<<",\"guid\":"<<a.saved->saved.guid
           <<",\"tile\":";odh_tools::saved_bank(report,a.graphics.tile_position);
        report<<",\"direction\":"<<unsigned(a.graphics.direction)<<",\"current\":";animation_json(report,a.appearance.current);
        report<<",\"base\":";animation_json(report,a.appearance.base);report<<",\"base_executed\":false,\"parts\":[";
        for(std::size_t p=0;p<a.appearance.scene.parts.size();++p){if(p)report<<',';const auto& part=a.appearance.scene.parts[p];
            report<<"{\"appearance_entry\":"<<json_bytes(part.appearance_entry)<<",\"appearance\":"<<json_bytes(part.appearance_name)
               <<",\"bmf\":"<<json_bytes(part.bmf_entry)<<",\"texture\":"<<json_bytes(part.texture_entry)<<'}';}
        report<<"],\"vertices\":"<<renderer.vertices()<<",\"triangles\":"<<renderer.triangles()<<",\"texture_bytes\":"<<renderer.texture_bytes()<<'}';
    }
    report<<"],\"objects\":[";
    std::size_t outside_objects=0,hidden_objects=0;
    if(draw_objects){
        std::map<std::uint32_t,odh::SlotTable> tables;
        auto slots=[&](const odh::Entity& container)->const odh::SlotTable* {
            if(!container.definition)throw odh::FormatError("saved container has no definition");
            const auto guid=container.definition->guid;auto found=tables.find(guid);if(found!=tables.end())return &found->second;
            const auto* candidate=index->unique(guid);if(!candidate)throw odh::FormatError("saved container definition unavailable");
            std::optional<odh::SlotTable> table;
            auto load=[&](const odh::View& view){std::optional<odh::ObjdBinding> binding;
                odh::iff_chunks(view,[&](const auto& c){if(c.type=="OBJD"&&c.id==candidate->chunk_id){
                    if(binding)throw odh::FormatError("duplicate container OBJD");
                    binding=odh::read_objd_binding(c.payload);}});
                if(!binding||binding->guid!=guid)throw odh::FormatError("container OBJD identity mismatch");
                odh::iff_chunks(view,[&](const auto& c){if(c.type=="SLOT"&&c.id==binding->slot_table){
                    if(table)throw odh::FormatError("duplicate container SLOT");
                    table=odh::read_slot(c.payload);}});
            };
            if(candidate->asset.empty())with_iff_file(candidate->source,load);else with_far_entry(candidate->source,candidate->asset,load);
            if(!table)throw odh::FormatError("container SLOT unavailable");
            return &tables.emplace(guid,std::move(*table)).first->second;
        };
        std::vector<SavedObjectVisual> objects;
        for(const auto& raw:world->records){const auto id=static_cast<std::int16_t>(raw.saved.object_id);const auto* entity=world->world->find(id);
            if(entity->kind!=odh::EntityKind::object)continue;
            if(!entity->position){++outside_objects;continue;}
            const odh::VmFrame f{odh::routine_key(4096),0,{id,id,id,0},{},{}};
            const auto hidden=world->world->read(f,3,34),graphic=world->world->read(f,3,0);
            if(!hidden||!graphic)throw odh::FormatError("saved object visibility/graphic unavailable");
            if(*hidden!=0){++hidden_objects;continue;}
            auto g=odh::graphics_entity(*world->world,id,slots);
            g.tile_position[2]+=elevation(id);
            objects.push_back({id,raw.saved.guid,*graphic,g.direction,g.tile_position,raw.common.dynamic_sprite_flags});
        }
        SavedObjectRenderer object_renderer(*index);auto result=object_renderer.render(objects,camera,image);
        for(std::size_t n=0;n<result.records.size();++n){if(n)report<<',';const auto& r=result.records[n];
            report<<"{\"id\":"<<r.id<<",\"guid\":"<<r.guid<<",\"source\":"<<json_bytes(r.source)<<",\"asset\":"<<json_bytes(r.asset)
                  <<",\"objd_id\":"<<r.objd_id<<",\"dgrp_id\":"<<r.dgrp_id<<",\"tile\":";odh_tools::saved_bank(report,objects[n].tile_position);
            report<<",\"layers\":"<<r.layers<<",\"omitted_dynamic_layers\":"<<r.omitted_dynamic_layers<<",\"omitted_empty_layers\":"<<r.omitted_empty_layers<<",\"no_graphics\":"<<(r.no_graphics?"true":"false")<<'}';}
        image=std::move(result.composite.image);
        report<<"],\"object_layers\":"<<result.layers<<",\"cached_object_assets\":"<<result.cached_assets
              <<",\"cached_object_frames\":"<<result.cached_frames<<",\"cached_object_pixels\":"<<result.cached_pixels<<",\"object_sprite_bytes\":"<<result.cached_frame_bytes
              <<",\"object_tested_samples\":"<<result.composite.tested_samples<<",\"object_shaded_samples\":"<<result.composite.shaded_samples;
    }else report<<']';
    std::uint64_t covered=0;for(float z:image.depth)if(std::isfinite(z))++covered;
    report<<",\"out_of_world_objects\":"<<outside_objects<<",\"hidden_objects\":"<<hidden_objects
          <<",\"vertices\":"<<vertices<<",\"triangles\":"<<triangles<<",\"texture_bytes\":"<<textures<<",\"cfp_samples\":"<<samples
          <<",\"covered_pixels\":"<<covered<<",\"tested_samples\":"<<image.tested_samples<<",\"shaded_samples\":"<<image.shaded_samples
          <<",\"width\":"<<image.width<<",\"height\":"<<image.height<<",\"output\":"<<json_bytes(argv[4])<<",\"depth_output\":"<<json_bytes(depth)<<"}\n";
    write_frame(image,argv[4],depth);std::cout<<report.str();return 0;
}catch(const std::exception& e){std::cerr<<"saved scene error: "<<e.what()<<'\n';return 2;}}
