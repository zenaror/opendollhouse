#include "saved_wall_appearance.hpp"
#include "saved_architecture_resources.hpp"
#include "saved_tail_json.hpp"
#include "odh/wall_route_state.hpp"
#include "odh/room_map.hpp"
#include "odh/architecture_resource_catalog.hpp"
#include <iostream>
int main(int argc,char** argv){try{
 if(argc<2)throw odh::FormatError("usage: HOUSE_IFF --object-definitions FAR [--object-definition-file IFF] [--neighborhood IFF] [--architecture-resource IFF] [--route-edges] [--portal-probe]");
 const std::string house=argv[1];std::vector<SavedDefinitionSource> sources;std::vector<std::string> architecture_resources;std::optional<std::string> neighborhood;bool route_edges=false,portal_probe=false;
 for(int n=2;n<argc;++n){const std::string option=argv[n];
  if((option=="--object-definitions"||option=="--object-definition-file")&&n+1<argc){if(sources.size()>=16)throw odh::FormatError("definition source count exceeds limit");sources.push_back({argv[++n],option=="--object-definition-file"});}
  else if(option=="--neighborhood"&&!neighborhood&&n+1<argc)neighborhood=argv[++n];
  else if(option=="--architecture-resource"&&n+1<argc){if(architecture_resources.size()>=256)throw odh::FormatError("architecture resource source count exceeds limit");architecture_resources.push_back(argv[++n]);}
  else if(option=="--route-edges"&&!route_edges)route_edges=true;
  else if(option=="--portal-probe"&&!portal_probe)portal_probe=true;
  else throw odh::FormatError("invalid wall appearance option");}
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
 std::cout<<']';
 if(route_edges){
  std::cout<<",\"initial_wall_edges\":{\"initial_snapshot_only\":true,\"dynamic_portals\":false,\"object_collision\":false,\"runtime_equivalence\":false,\"levels\":[";
  for(std::int16_t level=1;level<=2;++level){
   const auto edges=odh::resolve_initial_wall_edges(architecture.architecture,result.appearance,level);
   if(edges.status!=odh::WallEdgeBuildStatus::ready)throw odh::FormatError("initial wall edge resolution unsupported: "+edges.diagnostic);
   std::uint64_t present=0,solid=0,non_solid=0,doors=0;
   const auto& walls=architecture.architecture.levels[level-1].walls;
   for(std::size_t i=0;i<walls.size();++i){
    if(walls[i].segments&1U)++present;
    if(walls[i].segments&2U)++present;
    if(edges.edges[i].top_left==odh::WallEdgeKnowledge::solid)++solid;else ++non_solid;
    if(edges.edges[i].top_right==odh::WallEdgeKnowledge::solid)++solid;else ++non_solid;
   }
   for(const auto& face:result.appearance.faces)if(face.level==level&&face.door)++doors;
   if(level>1)std::cout<<',';
   std::cout<<"{\"level\":"<<level<<",\"width\":"<<architecture.architecture.width<<",\"height\":"<<architecture.architecture.height
    <<",\"present_wall_segments\":"<<present<<",\"solid_edges\":"<<solid<<",\"non_solid_edges\":"<<non_solid<<",\"door_faces\":"<<doors<<'}';
  }
  std::cout<<"]}";
 }
 if(portal_probe){
  std::optional<odh::PreparedArchitecture> prepared;
  std::optional<odh::ResolvedArchitectureAliases> aliases;
  odh::GeometricRoomMap rooms;
  if(!architecture_resources.empty()){
   std::vector<odh::ArchitectureResourceSource> catalog_sources;
   for(const auto& path:architecture_resources)catalog_sources.push_back({path,odh::ArchitectureResourceSourceKind::loose_file});
   const auto catalog=odh::discover_architecture_resources(catalog_sources);
   const odh::ArchitectureMap empty;
   const auto* floor_map=selected_architecture_map(architecture,"FLRm");
   const auto* wall_map=selected_architecture_map(architecture,"WALm");
   aliases=odh::resolve_architecture_aliases(floor_map?*floor_map:empty,wall_map?*wall_map:empty,catalog.floors(),catalog.walls());
   if(!aliases->floor_map_complete()||!aliases->wall_map_complete())throw odh::FormatError("portal probe architecture aliases unresolved");
   prepared=odh::prepare_saved_architecture(architecture.architecture,aliases->aliases);
   rooms=odh::generate_prepared_room_map(*prepared);
  }else rooms=odh::generate_saved_room_map(architecture.architecture);
  std::map<std::int16_t,const odh::SavedWorldEntity*> records_by_id;
  for(const auto& record:world->records)records_by_id.emplace(static_cast<std::int16_t>(record.saved.object_id),&record);
  std::map<std::int16_t,const odh::SavedMultitileGroup*> groups_by_member;
  for(const auto& group:world->multitile_groups)for(const auto id:group.members)groups_by_member.emplace(id,&group);
  std::map<std::int16_t,std::vector<const odh::WallAppearanceFace*>> door_faces;
  for(const auto& face:result.appearance.faces)if(face.door)door_faces[face.door_owner].push_back(&face);
  std::cout<<",\"portal_probe\":{\"evidence\":\"authored_entry_point15_inventory_only\",\"room_map_profile\":"<<json_bytes(prepared?"prepared_alias_resolved":"saved_raw_no_floor_alias_remap")
   <<",\"architecture_prepared\":"<<(prepared?"true":"false")<<",\"floor_alias_misses\":"<<(prepared?prepared->diagnostics.floor_alias_misses:0)
   <<",\"wall_alias_misses\":"<<(prepared?prepared->diagnostics.wall_alias_misses:0)<<",\"room_count\":"<<rooms.rooms.size()
   <<",\"portal_bhav_executed\":false,\"runtime_portal_state\":false,\"routing_clearance\":false,\"doors\":[";
  bool first_door=true;
  for(const auto& [door_id,faces]:door_faces){
   const auto found=records_by_id.find(door_id);
   if(found==records_by_id.end())throw odh::FormatError("portal probe door owner missing from saved World");
   const auto& record=*found->second;
   const auto* candidate=index->unique(record.definition.guid);
   if(!candidate)throw odh::FormatError("portal probe door definition missing or ambiguous");
   const auto function=saved_portal_function(*candidate);
   const auto* entity=world->world->find(door_id);
   const auto group_found=groups_by_member.find(door_id);
   std::vector<std::int16_t> members=group_found==groups_by_member.end()?std::vector<std::int16_t>{door_id}:group_found->second->members;
   if(!first_door)std::cout<<',';
   first_door=false;
   std::cout<<"{\"id\":"<<door_id<<",\"guid\":"<<record.definition.guid<<",\"portal_action\":"<<function.action
    <<",\"portal_property_authored\":"<<(function.action?"true":"false")<<",\"portal_entry_source\":"<<json_bytes(function.source==odh::EntryPointSource::objf?"OBJf":"OBJD")
    <<",\"action_bhav_in_definition_asset\":"<<(function.action_bhav_in_asset?"true":"false")<<",\"position\":";
   if(entity&&entity->position){const auto& p=*entity->position;std::cout<<"{\"x\":"<<p.x<<",\"y\":"<<p.y<<",\"level\":"<<p.level<<",\"room\":";
    const auto room=odh::geometric_room_at(rooms,p.x,p.y,p.level);if(room)std::cout<<*room;else std::cout<<"null";std::cout<<'}';
   }else std::cout<<"null";
   std::cout<<",\"group_members\":[";
   for(std::size_t i=0;i<members.size();++i){if(i)std::cout<<',';const auto member=members[i];const auto member_record=records_by_id.find(member);
    if(member_record==records_by_id.end())throw odh::FormatError("portal probe multitile member missing from saved World");
    const auto* member_candidate=index->unique(member_record->second->definition.guid);
    if(!member_candidate)throw odh::FormatError("portal probe multitile member definition missing or ambiguous");
    const auto member_function=saved_portal_function(*member_candidate);const auto* member_entity=world->world->find(member);
    std::cout<<"{\"id\":"<<member<<",\"portal_action\":"<<member_function.action<<",\"portal_property_authored\":"<<(member_function.action?"true":"false")<<",\"position\":";
    if(member_entity&&member_entity->position){const auto& p=*member_entity->position;std::cout<<"{\"x\":"<<p.x<<",\"y\":"<<p.y<<",\"level\":"<<p.level<<",\"room\":";
     const auto room=odh::geometric_room_at(rooms,p.x,p.y,p.level);if(room)std::cout<<*room;else std::cout<<"null";std::cout<<'}';
    }else std::cout<<"null";std::cout<<'}';
   }
   std::cout<<"],\"faces\":[";
   for(std::size_t i=0;i<faces.size();++i){if(i)std::cout<<',';const auto& face=*faces[i];std::cout<<"{\"x\":"<<face.x<<",\"y\":"<<face.y<<",\"level\":"<<unsigned(face.level)<<",\"segment\":"<<unsigned(face.segment)<<'}';}
   std::cout<<"]}";
  }
  std::cout<<"]}";
 }
 std::cout<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<"wall appearance error: "<<e.what()<<'\n';return 2;}}
