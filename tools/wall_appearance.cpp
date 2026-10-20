#include "saved_wall_appearance.hpp"
#include "saved_architecture_resources.hpp"
#include "saved_tail_json.hpp"
#include <iostream>
int main(int argc,char** argv){try{
 if(argc<2)throw odh::FormatError("usage: HOUSE_IFF --object-definitions FAR [--object-definition-file IFF] [--neighborhood IFF]");
 const std::string house=argv[1];std::vector<SavedDefinitionSource> sources;std::optional<std::string> neighborhood;
 for(int n=2;n<argc;++n){const std::string option=argv[n];
  if((option=="--object-definitions"||option=="--object-definition-file")&&n+1<argc){if(sources.size()>=16)throw odh::FormatError("definition source count exceeds limit");sources.push_back({argv[++n],option=="--object-definition-file"});}
  else if(option=="--neighborhood"&&!neighborhood&&n+1<argc)neighborhood=argv[++n];else throw odh::FormatError("invalid wall appearance option");}
 if(sources.empty())throw odh::FormatError("wall appearance requires explicit definitions");
 auto index=saved_definition_index(sources);auto records=saved_world_records(house,*index);std::optional<odh::NeighborContext> neighbors;if(neighborhood)neighbors.emplace(saved_neighbors(*neighborhood));
 auto world=odh::make_saved_world(records,*index,neighbors?&*neighbors:nullptr);auto architecture=saved_architecture_resource(house);
 const auto result=saved_wall_appearance(*world,*index,architecture.architecture);
 std::cout<<"{\"evidence\":\"ODH_saved_architectural_object_wall_overrides\",\"house\":"<<json_bytes(house)
 <<",\"wall_appearance_prepared\":true,\"masks_resolved\":false,\"walls_rendered\":false,\"rooms_loaded\":false,\"saved_threads_restored\":false,\"original_runtime_equivalence\":false"
 <<",\"input_order\":\"ascending_object_id_simitone_restore_profile\",\"entities\":"<<world->records.size()<<",\"overwritten_faces\":"<<result.appearance.overwritten_faces<<",\"bindings\":[";
 for(std::size_t n=0;n<result.bindings.size();++n){if(n)std::cout<<',';const auto& b=result.bindings[n];std::cout<<"{\"guid\":"<<b.guid<<",\"source\":"<<json_bytes(b.source)<<",\"asset\":"<<json_bytes(b.asset)<<",\"objd_id\":"<<b.chunk_id<<",\"version\":"<<b.binding.version<<",\"wall_style\":"<<b.binding.wall_style<<",\"sprite_base\":"<<(b.binding.sprite_base?std::to_string(*b.binding.sprite_base):"null")<<",\"header_extent\":"<<b.binding.header_extent<<",\"remaining_bytes\":"<<b.binding.remaining_bytes<<'}';}
 std::cout<<"],\"applications\":[";for(std::size_t n=0;n<result.appearance.applications.size();++n){if(n)std::cout<<',';const auto& a=result.appearance.applications[n];std::cout<<"{\"id\":"<<a.id<<",\"applied_sides\":"<<unsigned(a.applied_sides)<<",\"ignored_sides\":"<<unsigned(a.ignored_sides)<<'}';}
 std::cout<<"],\"faces\":[";for(std::size_t n=0;n<result.appearance.faces.size();++n){if(n)std::cout<<',';const auto& f=result.appearance.faces[n];std::cout<<"{\"x\":"<<f.x<<",\"y\":"<<f.y<<",\"level\":"<<unsigned(f.level)<<",\"segment\":"<<unsigned(f.segment)<<",\"segment_present\":"<<(f.segment_present?"true":"false")<<",\"owner\":"<<f.owner<<",\"generation\":"<<f.generation<<",\"door\":"<<(f.door?"true":"false")<<",\"door_owner\":"<<f.door_owner<<",\"door_generation\":"<<f.door_generation<<",\"raw_style\":"<<f.style.raw_style<<",\"resource_guid\":"<<f.style.resource_guid<<",\"sprites\":";odh_tools::saved_bank(std::cout,f.style.sprites);std::cout<<'}';}
 std::cout<<"]}\n";return 0;
}catch(const std::exception& e){std::cerr<<"wall appearance error: "<<e.what()<<'\n';return 2;}}
