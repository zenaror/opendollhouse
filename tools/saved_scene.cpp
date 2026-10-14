#include "saved_resources.hpp"
#include "saved_appearance.hpp"
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
    if(argc<5)throw odh::FormatError("usage: HOUSE_IFF ANIMATION_FAR TEXTURES_FAR OUTPUT_PPM --skeleton ENTRY NAME --object-definitions FAR [--object-definition-file IFF] [--neighborhood IFF] [--depth-output FILE] [--width N --height N --zoom N --rotation N]");
    std::vector<SavedDefinitionSource> sources;std::optional<std::string> neighborhood;
    std::string skeleton_entry,skeleton_name,depth;odh::WorldProjection camera;camera.width=640;camera.height=400;
    std::set<std::string> seen;
    for(int n=5;n<argc;++n){const std::string option=argv[n];
        auto need=[&](int count){if(argc-n-1<count)throw odh::FormatError("incomplete saved scene option");};
        if(option=="--object-definitions"||option=="--object-definition-file"){
            need(1);if(sources.size()>=16)throw odh::FormatError("definition source count exceeds limit");
            sources.push_back({argv[++n],option=="--object-definition-file"});continue;}
        if(!seen.insert(option).second)throw odh::FormatError("duplicate saved scene option");
        if(option=="--skeleton"){need(2);skeleton_entry=argv[++n];skeleton_name=argv[++n];}
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
    std::vector<std::string> inputs{argv[1],argv[2],argv[3]};for(const auto& s:sources)inputs.push_back(s.path);
    if(neighborhood)inputs.push_back(*neighborhood);
    for(const auto& p:inputs){different(argv[4],p);if(!depth.empty())different(depth,p);}
    if(!depth.empty())different(argv[4],depth);
    auto index=saved_definition_index(sources);auto records=saved_world_records(argv[1],*index);
    std::optional<odh::NeighborContext> neighbors;if(neighborhood)neighbors.emplace(saved_neighbors(*neighborhood));
    auto world=odh::make_saved_world(records,*index,neighbors?&*neighbors:nullptr);
    struct Actor {const odh::SavedWorldEntity* saved;odh::GraphicsEntity graphics;SavedAppearance appearance;};
    std::vector<Actor> actors;std::vector<std::int16_t> outside;
    SavedAppearanceResources resources(argv[2],argv[3],skeleton_entry,skeleton_name);
    for(const auto& r:world->records){if(!r.tail.person)continue;
        auto id=static_cast<std::int16_t>(r.saved.object_id);
        if(!world->world->find(id)->position){outside.push_back(id);continue;}
        if(actors.size()>=16)throw odh::FormatError("saved scene avatar budget exceeded");
        actors.push_back({&r,odh::graphics_entity(*world->world,id),resources.resolve(*r.tail.person)});
    }
    if(actors.empty())throw odh::FormatError("saved scene has no positioned avatars");
    for(const auto& a:actors)for(std::size_t i=0;i<3;++i)camera.center_tile[i]+=a.graphics.tile_position[i]/static_cast<float>(actors.size());
    odh::RasterOptions options;options.lighting=true;
    odh::RasterFrame image=odh::rasterize(camera.width,camera.height,{}, {},options);
    std::ostringstream report;report<<std::setprecision(17)<<"{\"evidence\":\"ODH_saved_World_avatar_CPU_snapshot\",\"house\":"<<json_bytes(argv[1])
       <<",\"world_entities_restored\":true,\"saved_threads_restored\":false,\"architecture_loaded\":false,\"objects_rendered\":false,\"original_visual_equivalence\":false"
       <<",\"entities\":"<<world->records.size()<<",\"avatars_rendered\":"<<actors.size()<<",\"out_of_world_avatars\":";
    odh_tools::saved_bank(report,outside);report<<",\"center_tile\":";odh_tools::saved_bank(report,camera.center_tile);
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
    std::uint64_t covered=0;for(float z:image.depth)if(std::isfinite(z))++covered;
    report<<"],\"vertices\":"<<vertices<<",\"triangles\":"<<triangles<<",\"texture_bytes\":"<<textures<<",\"cfp_samples\":"<<samples
          <<",\"covered_pixels\":"<<covered<<",\"tested_samples\":"<<image.tested_samples<<",\"shaded_samples\":"<<image.shaded_samples
          <<",\"width\":"<<image.width<<",\"height\":"<<image.height<<",\"output\":"<<json_bytes(argv[4])<<",\"depth_output\":"<<json_bytes(depth)<<"}\n";
    write_frame(image,argv[4],depth);std::cout<<report.str();return 0;
}catch(const std::exception& e){std::cerr<<"saved scene error: "<<e.what()<<'\n';return 2;}}
