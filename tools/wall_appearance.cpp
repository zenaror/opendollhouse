#include "saved_wall_appearance.hpp"
#include "saved_architecture_resources.hpp"
#include "saved_tail_json.hpp"
#include "odh/wall_route_state.hpp"
#include "odh/room_map.hpp"
#include "odh/room_obstacles.hpp"
#include "odh/saved_collision_candidates.hpp"
#include "odh/saved_avatar_place_discovery.hpp"
#include "odh/saved_room_route_grid.hpp"
#include "odh/saved_slot_goal_selection.hpp"
#include "odh/saved_room_boundary_audit.hpp"
#include "odh/architecture_resource_catalog.hpp"
#include "odh/route.hpp"
#include "odh/routing.hpp"
#include "odh/formats.hpp"
#include "odh/saved_floor_placement.hpp"
#include "odh/prepared_architecture.hpp"
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <tuple>
#include <vector>

namespace {
struct SlotResource { std::string archive,entry; std::uint16_t chunk_id,type,index; };
std::uint16_t parse_u16(const char* value,const char* label) {
 int parsed=0;const auto* end=value+std::char_traits<char>::length(value);
 const auto result=std::from_chars(value,end,parsed);
 if(result.ec!=std::errc{}||result.ptr!=end||parsed<0||parsed>std::numeric_limits<std::uint16_t>::max())
  throw odh::FormatError(std::string(label)+" must be an unsigned 16-bit integer");
 return static_cast<std::uint16_t>(parsed);
}
odh::SlotTable read_slot_resource(const SlotResource& resource) {
 std::ifstream stream(resource.archive,std::ios::binary);
 if(!stream)throw odh::FormatError("cannot open SLOT archive");
 const odh::View archive(stream,std::filesystem::file_size(resource.archive));
 std::optional<odh::FarEntry> selected;
 for(const auto& entry:odh::far1_index(archive))if(entry.name==resource.entry){
  if(selected)throw odh::FormatError("ambiguous SLOT archive entry");
  selected=entry;
 }
 if(!selected)throw odh::FormatError("exact SLOT archive entry not found");
 std::optional<odh::SlotTable> table;
 odh::iff_chunks(archive.subview(selected->offset,selected->length),[&](const odh::Chunk& chunk){
  if(chunk.type!="SLOT"||chunk.id!=resource.chunk_id)return;
  if(table)throw odh::FormatError("ambiguous SLOT chunk ID");
  table=odh::read_slot(chunk.payload);
 });
 if(!table)throw odh::FormatError("SLOT chunk not found");
 if(!odh::slot_of_type(*table,resource.type,resource.index))throw odh::FormatError("selected SLOT type/index not found");
 return *table;
}
const char* route_status_name(odh::RouteStatus status) {
 switch(status){
 case odh::RouteStatus::found:return "found";
 case odh::RouteStatus::no_path:return "no_path";
 case odh::RouteStatus::invalid_input:return "invalid_input";
 case odh::RouteStatus::budget_exhausted:return "budget_exhausted";
 }
 return "invalid_status";
}
const char* floor_check_name(odh::SavedFloorPlacementState state) {
 switch(state){case odh::SavedFloorPlacementState::allowed:return "allowed";
 case odh::SavedFloorPlacementState::rejected:return "rejected";
 case odh::SavedFloorPlacementState::unsupported:return "unsupported";}
 return "invalid_state";
}
const char* wall_raycast_name(odh::WallRaycastState state) {
 switch(state){case odh::WallRaycastState::clear:return "clear";
 case odh::WallRaycastState::blocked:return "blocked";
 case odh::WallRaycastState::unsupported:return "unsupported";}
 return "invalid_state";
}

void emit_route_grid_diagnostic(
 std::ostream& out, std::int16_t actor_id, std::int16_t target_id,
 const odh::SavedArchitecture& architecture, const odh::WallAppearance& appearance,
 const odh::GeometricRoomMap& rooms,
 const std::vector<odh::RoomObstacleCandidateSet>& room_obstacles,
 const std::vector<odh::SavedCollisionCandidate>& entities,
 const std::vector<odh::SavedMultitileGroup>& groups,
 const odh::SlotTable& slots,std::size_t slot_type3_index,
 const odh::SavedWorldEntity* actor_record,const odh::PreparedArchitecture* prepared) {
 const auto find_entity=[&](std::int16_t id)->const odh::SavedCollisionCandidate*{
  for(const auto& entity:entities)if(entity.object_id==id)return &entity;
  return nullptr;
 };
 const auto* actor=find_entity(actor_id);const auto* target=find_entity(target_id);
 out<<"{\"evidence\":\"candidate_grid_search_only\",\"route_clearance\":false"
  <<",\"actor_id\":"<<actor_id<<",\"target_id\":"<<target_id;
 if(!actor||!actor->avatar||!actor->position){out<<",\"status\":\"unsupported\",\"reason\":\"actor_position_unavailable\"}";return;}
 if(!target||target->avatar||target->state!=odh::SavedCollisionCandidateState::candidate||!target->position||!target->box){
  out<<",\"status\":\"unsupported\",\"reason\":\"target_collision_candidate_unavailable\"}";return;
 }
 std::vector<odh::ReferenceSlotCircleCandidate> slot_candidates;
 try {slot_candidates=odh::enumerate_reference_slot_circle_candidates(slots,slot_type3_index,
       {target->position->x,target->position->y},target->position->direction);}
 catch(const std::exception&){out<<",\"status\":\"unsupported\",\"reason\":\"slot_circle_profile_unimplemented\"}";return;}
 const auto* selected_slot=odh::slot_of_type(slots,3,slot_type3_index);
 if(!selected_slot){out<<",\"status\":\"unsupported\",\"reason\":\"slot_selection_missing\"}";return;}
 const auto actor_room=odh::geometric_room_at(rooms,actor->position->x,actor->position->y,actor->position->level);
 const auto target_room=odh::geometric_room_at(rooms,target->position->x,target->position->y,target->position->level);
 if(!actor_room||!target_room){out<<",\"status\":\"unsupported\",\"reason\":\"room_lookup_unavailable\"}";return;}
 out<<",\"actor_room\":"<<*actor_room<<",\"target_room\":"<<*target_room;
 if(*actor_room!=*target_room){out<<",\"status\":\"cross_room_unmodeled\"}";return;}
 if(*actor_room==0){out<<",\"status\":\"unsupported\",\"reason\":\"outside_room_unmodeled\"}";return;}
 const auto level=static_cast<std::uint8_t>(actor->position->level-1);
 const auto obstacle_it=std::find_if(room_obstacles.begin(),room_obstacles.end(),[&](const auto& candidate){
  return candidate.room==*actor_room&&candidate.level==level;
 });
 if(obstacle_it==room_obstacles.end()){out<<",\"status\":\"unsupported\",\"reason\":\"room_obstacles_unavailable\"}";return;}
 const auto edges=odh::resolve_initial_wall_edges(architecture,appearance,static_cast<std::int16_t>(level+1));
 odh::WallObstacleCandidateSet wall_obstacles;wall_obstacles.level=level;
 if(edges.status==odh::WallEdgeBuildStatus::ready)
  wall_obstacles=odh::generate_wall_obstacle_candidates(architecture,level,edges.edges);
 const auto grid=odh::build_saved_room_route_grid(rooms,*actor_room,level,*obstacle_it,wall_obstacles,
  entities,actor_id,true);
 out<<",\"grid_state\":"<<json_bytes(grid.state==odh::SavedRoomRouteGridState::ready?"ready":"unsupported")
  <<",\"grid_reason\":"<<json_bytes(odh::saved_room_route_grid_reason_name(grid.reason));
 if(grid.grid){
  const auto& bounds=rooms.rooms[*actor_room].bounds;
  out<<",\"grid_width\":"<<std::uint64_t(bounds.width)*16<<",\"grid_height\":"<<std::uint64_t(bounds.height)*16
   <<",\"grid_cells\":"<<grid.grid->cells()
   <<",\"walkable_cells\":"<<(grid.grid->cells()-grid.blocked_by_room-grid.blocked_by_room_obstacles-grid.blocked_by_wall_obstacles-grid.blocked_by_entities)
   <<",\"blocked_by_room\":"<<grid.blocked_by_room<<",\"blocked_by_room_obstacles\":"<<grid.blocked_by_room_obstacles
   <<",\"blocked_by_wall_obstacles\":"<<grid.blocked_by_wall_obstacles<<",\"blocked_by_entities\":"<<grid.blocked_by_entities
   <<",\"rasterized_cells\":"<<grid.rasterized_cells
   <<",\"start_walkable\":"<<(grid.grid->walkable({actor->position->x,actor->position->y})?"true":"false");
 }else{out<<",\"status\":\"unsupported\"}";return;}
 if(grid.state!=odh::SavedRoomRouteGridState::ready){out<<",\"status\":\"unsupported\"}";return;}
 const auto edge_state=odh::resolve_initial_wall_edges(architecture,appearance,actor->position->level);
 std::vector<odh::RoutePoint> goals;
 std::size_t selection_clear=0,selection_object_overlap=0,selection_avatar_penalty=0,selection_unsupported=0,selection_eligible=0;
 out<<",\"approach_model\":\"global_slot_type3_candidate_circle\",\"slot_type3_index\":"<<slot_type3_index
  <<",\"slot_offset_bits\":["<<selected_slot->offset_bits[0]<<','<<selected_slot->offset_bits[1]<<','
  <<selected_slot->offset_bits[2]<<"],\"wall_raycast_profile\":\"initial_saved_edge_snapshot\""
  <<",\"static_goal_gate_model\":\"bounds+room+FloorChangeValid+RaycastWall+saved_grid\""
  <<",\"runtime_occupancy_validated\":false,\"goal_selection_profile\":\"conservative_solid_saved_footprints\""
  <<",\"route_search_goal_model\":\"static_gates_and_snapshot_goal_selection\""
  <<",\"goal_selection_unmodeled\":[\"dynamic_obstacles\",\"AllowPersonIntersection\",\"DisallowPersonIntersection\",\"bilateral_EntryPoint5\",\"doors\",\"multitile_policy\",\"sitting_positive_EntryPoint26\"]"
  <<",\"slot_candidates\":[";
 for(std::size_t i=0;i<slot_candidates.size();++i){
  if(i)out<<',';
  const auto& candidate=slot_candidates[i];
  const bool in_bounds=grid.grid->contains(candidate.position);
  const auto candidate_room=odh::geometric_room_at(rooms,candidate.position.x,candidate.position.y,target->position->level);
  const bool same_room=candidate_room&&*candidate_room==*actor_room;
  const bool walkable=in_bounds&&grid.grid->walkable(candidate.position);
  const auto floor=actor_record&&prepared
   ?odh::check_saved_floor_placement(*actor_record,*prepared,candidate.position.x,candidate.position.y,actor->position->level)
   :odh::SavedFloorPlacementCheck{};
  const auto wall=edge_state.status==odh::WallEdgeBuildStatus::ready&&candidate.position.x>=0&&candidate.position.y>=0
   ?odh::saved_wall_raycast(architecture,actor->position->level,
      candidate.position.x/16,candidate.position.y/16,target->position->x/16,target->position->y/16,edge_state.edges)
   :odh::WallRaycastResult{};
  const bool position_valid_static=candidate.entry_flags&&in_bounds&&same_room&&walkable&&
       floor.state==odh::SavedFloorPlacementState::allowed&&wall.state==odh::WallRaycastState::clear;
  const auto selection=odh::assess_saved_slot_goal_selection(slots,slot_type3_index,rooms,actor_id,
      {candidate.position.x,candidate.position.y,actor->position->level,0},entities,groups,true);
  const auto avatar_place=odh::discover_saved_avatar_place_blockers(
      rooms,
      {candidate.position.x,candidate.position.y,actor->position->level,actor->position->direction},
      actor_id,entities,groups,true,true);
  switch(selection.state){
   case odh::SavedSlotGoalSelectionState::snapshot_clear:++selection_clear;break;
   case odh::SavedSlotGoalSelectionState::object_overlap_rejected:++selection_object_overlap;break;
   case odh::SavedSlotGoalSelectionState::avatar_overlap_score_penalty:++selection_avatar_penalty;break;
   case odh::SavedSlotGoalSelectionState::unsupported:++selection_unsupported;break;
  }
  const auto snapshot_eligible=selection.snapshot_eligible();
  if(snapshot_eligible&&*snapshot_eligible)++selection_eligible;
  const bool route_search_goal=position_valid_static&&snapshot_eligible&&*snapshot_eligible;
  out<<"{\"x\":"<<candidate.position.x<<",\"y\":"<<candidate.position.y
   <<",\"entry_flags\":"<<unsigned(candidate.entry_flags)<<",\"mask_accepted\":"<<(candidate.entry_flags?"true":"false")
   <<",\"in_bounds\":"<<(in_bounds?"true":"false")<<",\"room\":";
  if(candidate_room)out<<*candidate_room;else out<<"null";
  out<<",\"same_room\":"<<(same_room?"true":"false")<<",\"grid_walkable\":"<<(walkable?"true":"false")
   <<",\"floor_change_valid\":"<<json_bytes(floor_check_name(floor.state))<<",\"floor\":";
  if(floor.floor)out<<*floor.floor;else out<<"null";
  out<<",\"wall_raycast\":"<<json_bytes(wall_raycast_name(wall.state))
   <<",\"wall_hit_tile\":";
  if(wall.tile_x>=0&&wall.tile_y>=0)out<<'['<<wall.tile_x<<','<<wall.tile_y<<']';else out<<"null";
  out<<",\"static_goal_gates_passed\":"<<(position_valid_static?"true":"false")
   <<",\"goal_selection\":{\"state\":"<<json_bytes(odh::saved_slot_goal_selection_state_name(selection.state))
   <<",\"reason\":"<<json_bytes(odh::saved_slot_goal_selection_reason_name(selection.reason))
   <<",\"snapshot_eligible\":";
  if(snapshot_eligible)out<<(*snapshot_eligible?"true":"false");else out<<"null";
  out<<",\"object_overlaps\":"<<selection.object_overlaps<<",\"avatar_overlaps\":"<<selection.avatar_overlaps
   <<",\"related_entity_id\":";
  if(selection.blocker)out<<*selection.blocker;else out<<"null";
  out<<"},\"saved_entity_overlap_audit\":{\"evidence\":\"saved_entity_footprint_snapshot_only\",\"status\":"
   <<json_bytes(odh::saved_avatar_place_discovery_status_name(avatar_place.status))
   <<",\"reason\":"<<json_bytes(odh::saved_avatar_place_discovery_reason_name(avatar_place.reason))
   <<",\"runtime_obstacle_inventory_complete\":false,\"candidate_count\":"<<avatar_place.entities_examined
   <<",\"possible_entity_overlaps\":[";
  for(std::size_t blocker_index=0;blocker_index<avatar_place.entity_snapshot_blockers.size();++blocker_index){
   if(blocker_index)out<<',';
   const auto& blocker=avatar_place.entity_snapshot_blockers[blocker_index];
   out<<"{\"id\":"<<blocker.blocker_id<<",\"kind\":"
    <<json_bytes(blocker.overlap.blocker_kind==odh::AvatarPlaceBlockerKind::avatar?"avatar":"object")
    <<",\"same_multitile_group\":"<<(blocker.overlap.same_multitile_group?"true":"false")<<'}';
  }
  const auto audit_status=avatar_place.entity_snapshot_rule_audit.status;
  const char* audit_name=audit_status==odh::SavedAvatarPlaceAuditStatus::allowed?"allowed":
   audit_status==odh::SavedAvatarPlaceAuditStatus::blocked?"blocked":"unsupported";
  out<<"],\"rule_audit_status\":"<<json_bytes(audit_name)
   <<",\"rule_audit_pairs\":"<<avatar_place.entity_snapshot_rule_audit.pair_classifications
   <<",\"rule_audit_final_blocker\":"<<avatar_place.entity_snapshot_rule_audit.final_blocker_id
   <<",\"runtime_clearance\":false},\"route_search_goal\":"<<(route_search_goal?"true":"false")<<'}';
  if(route_search_goal)goals.push_back(candidate.position);
 }
 out<<"],\"goal_selection_counts\":{\"snapshot_clear\":"<<selection_clear
  <<",\"object_overlap_rejected\":"<<selection_object_overlap<<",\"avatar_overlap_score_penalty\":"<<selection_avatar_penalty
  <<",\"unsupported\":"<<selection_unsupported<<",\"snapshot_eligible\":"<<selection_eligible
  <<"},\"approach_candidates\":"<<goals.size();
 if(goals.empty()){out<<",\"status\":\"no_approach_cell\"}";return;}
 const auto path=odh::find_route(*grid.grid,{actor->position->x,actor->position->y},goals);
 out<<",\"status\":"<<json_bytes(route_status_name(path.status))<<",\"route_cost\":"<<path.cost
  <<",\"expanded_nodes\":"<<path.expanded<<",\"path_points\":"<<path.points.size();
 if(!path.points.empty())out<<",\"route_start\":["<<path.points.front().x<<','<<path.points.front().y<<']'
  <<",\"route_approach\":["<<path.points.back().x<<','<<path.points.back().y<<']';
 out<<'}';
}
}

int main(int argc,char** argv){try{
 if(argc<2)throw odh::FormatError("usage: HOUSE_IFF --object-definitions FAR [--object-definition-file IFF] [--neighborhood IFF] [--architecture-resource IFF] [--route-edges] [--portal-probe] [--route-grid-diagnostic ACTOR_ID TARGET_ID --route-slot-resource ARCHIVE ENTRY CHUNK_ID 3 TYPE3_INDEX]");
 const std::string house=argv[1];std::vector<SavedDefinitionSource> sources;std::vector<std::string> architecture_resources;std::optional<std::string> neighborhood;bool route_edges=false,portal_probe=false;
 std::optional<std::pair<std::int16_t,std::int16_t>> route_grid_diagnostic;
 std::optional<SlotResource> slot_resource;
 const auto parse_id=[](const char* value){
  int parsed=0;const auto* end=value+std::char_traits<char>::length(value);
  const auto result=std::from_chars(value,end,parsed);
  if(result.ec!=std::errc{}||result.ptr!=end||parsed<std::numeric_limits<std::int16_t>::min()||parsed>std::numeric_limits<std::int16_t>::max())
   throw odh::FormatError("route-grid actor/target ID must be a signed 16-bit integer");
  return static_cast<std::int16_t>(parsed);
 };
 for(int n=2;n<argc;++n){const std::string option=argv[n];
  if((option=="--object-definitions"||option=="--object-definition-file")&&n+1<argc){if(sources.size()>=16)throw odh::FormatError("definition source count exceeds limit");sources.push_back({argv[++n],option=="--object-definition-file"});}
  else if(option=="--neighborhood"&&!neighborhood&&n+1<argc)neighborhood=argv[++n];
  else if(option=="--architecture-resource"&&n+1<argc){if(architecture_resources.size()>=256)throw odh::FormatError("architecture resource source count exceeds limit");architecture_resources.push_back(argv[++n]);}
  else if(option=="--route-edges"&&!route_edges)route_edges=true;
  else if(option=="--portal-probe"&&!portal_probe)portal_probe=true;
  else if(option=="--route-grid-diagnostic"&&!route_grid_diagnostic&&n+2<argc){
   const auto actor_id=parse_id(argv[++n]);const auto target_id=parse_id(argv[++n]);
   route_grid_diagnostic=std::make_pair(actor_id,target_id);portal_probe=true;
  }
  else if(option=="--route-slot-resource"&&!slot_resource&&n+5<argc){
   SlotResource resource;resource.archive=argv[++n];resource.entry=argv[++n];
   resource.chunk_id=parse_u16(argv[++n],"SLOT chunk ID");resource.type=parse_u16(argv[++n],"SLOT type");
   resource.index=parse_u16(argv[++n],"SLOT type index");
   if(resource.type!=3)throw odh::FormatError("route-grid diagnostic currently supports SLOT type 3 only");
   slot_resource=std::move(resource);
  }
  else throw odh::FormatError("invalid wall appearance option");}
 if(route_grid_diagnostic&&!slot_resource)throw odh::FormatError("route-grid diagnostic requires explicit --route-slot-resource ARCHIVE ENTRY CHUNK_ID TYPE INDEX");
 if(slot_resource&&!route_grid_diagnostic)throw odh::FormatError("--route-slot-resource requires --route-grid-diagnostic");
 std::optional<odh::SlotTable> route_slots;if(slot_resource)route_slots=read_slot_resource(*slot_resource);
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
   const auto wall_obstacles=odh::generate_wall_obstacle_candidates(
    architecture.architecture,static_cast<std::uint8_t>(level-1),edges.edges);
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
    <<",\"present_wall_segments\":"<<present<<",\"solid_edges\":"<<solid<<",\"non_solid_edges\":"<<non_solid<<",\"door_faces\":"<<doors
    <<",\"wall_obstacles_complete\":"<<(wall_obstacles.complete?"true":"false")
    <<",\"wall_obstacle_rectangles\":"<<wall_obstacles.rectangles.size()<<'}';
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
   // Preserve unresolved declarations in the prepared result. The room map
   // determines whether a missing FLRm value actually affects used identity;
   // raw lookup and map-entry misses remain visible in its diagnostics.
   prepared=odh::prepare_saved_architecture(architecture.architecture,aliases->aliases);
   rooms=odh::generate_prepared_room_map(*prepared);
  }else rooms=odh::generate_saved_room_map(architecture.architecture);
  std::map<std::int16_t,const odh::SavedWorldEntity*> records_by_id;
  for(const auto& record:world->records)records_by_id.emplace(static_cast<std::int16_t>(record.saved.object_id),&record);
  std::map<std::int16_t,const odh::SavedMultitileGroup*> groups_by_member;
  for(const auto& group:world->multitile_groups)for(const auto id:group.members)groups_by_member.emplace(id,&group);
  std::map<std::uint32_t,SavedPortalFunction> portal_cache;
  const auto portal_for=[&](const odh::SavedWorldEntity& record)->SavedPortalFunction {
   const auto found=portal_cache.find(record.definition.guid);
   if(found!=portal_cache.end())return found->second;
   const auto* candidate=index->unique(record.definition.guid);
   if(!candidate)throw odh::FormatError("portal probe definition missing or ambiguous");
   const auto function=saved_portal_function(*candidate);
   portal_cache.emplace(record.definition.guid,function);return function;
  };
  std::map<std::int16_t,const odh::WallAppearanceInput*> inputs_by_id;
  std::set<std::int16_t> door_group_leaders;
  for(const auto& input:result.inputs){
   inputs_by_id.emplace(input.id,&input);
   if(input.kind!=odh::EntityKind::object||input.contained||!(input.flags2&0x8000U))continue;
   const auto group_it=groups_by_member.find(input.id);
   door_group_leaders.insert(group_it==groups_by_member.end()?input.id:group_it->second->leader);
  }
  std::vector<odh::RoomPortalMemberSnapshot> portal_members;
  for(const auto leader:door_group_leaders){
   const auto group_it=groups_by_member.find(leader);
   const auto group=group_it==groups_by_member.end()?nullptr:group_it->second;
   const std::vector<std::int16_t> members=group?group->members:std::vector<std::int16_t>{leader};
   for(const auto member:members){
    const auto member_record=records_by_id.find(member);
    const auto input=inputs_by_id.find(member);
    const auto* entity=world->world->find(member);
    if(member_record==records_by_id.end()||input==inputs_by_id.end()||!entity)
     throw odh::FormatError("portal probe group member missing from saved World snapshot");
    const auto function=portal_for(*member_record->second);
    portal_members.push_back({member,leader,(input->second->flags2&0x8000U)!=0,
      function.action!=0,input->second->contained,entity->position});
   }
  }
  const auto portal_inventory=odh::build_room_portal_inventory(rooms,portal_members);
  const auto& portal_openings=portal_inventory.openings;
  const bool portal_inventory_complete=portal_inventory.complete;
  std::vector<odh::RoomObstacleCandidateSet> room_obstacles;
  for(std::size_t room=1;room<rooms.rooms.size();++room){
   const auto level=rooms.rooms[room].level;
   room_obstacles.push_back(odh::generate_room_obstacle_candidates(rooms,static_cast<std::uint16_t>(room),level,portal_openings,portal_inventory_complete));
  }
  std::map<std::int16_t,std::vector<const odh::WallAppearanceFace*>> door_faces;
  for(const auto& face:result.appearance.faces)if(face.door)door_faces[face.door_owner].push_back(&face);
  const char* room_map_profile=prepared?(prepared->floor_identity_unambiguous()?"prepared_floor_identity_unambiguous":"prepared_floor_identity_ambiguous"):"saved_raw_no_floor_alias_remap";
  std::cout<<",\"portal_probe\":{\"evidence\":\"authored_entry_point15_inventory_only\",\"room_map_profile\":"<<json_bytes(room_map_profile)
   <<",\"architecture_prepared\":"<<(prepared?"true":"false")<<",\"floor_alias_misses\":"<<(prepared?prepared->diagnostics.floor_alias_misses:0)
   <<",\"floor_map_entries\":"<<(aliases?aliases->diagnostics.floor_map_entries:0)
   <<",\"floor_map_misses\":"<<(aliases?aliases->diagnostics.floor_map_misses:0)
   <<",\"unresolved_floor_alias_references\":"<<(prepared?prepared->diagnostics.unresolved_floor_alias_references:0)
   <<",\"floor_identity_unambiguous\":"<<(prepared?(prepared->floor_identity_unambiguous()?"true":"false"):"false")
   <<",\"wall_alias_misses\":"<<(prepared?prepared->diagnostics.wall_alias_misses:0)<<",\"room_count\":"<<rooms.rooms.size()
   <<",\"room_floor_lookups_complete\":"<<(rooms.floor_resources_resolved?"true":"false")
   <<",\"portal_opening_tile_room_pairs\":"<<portal_openings.size()<<",\"portal_inventory_complete\":"<<(portal_inventory_complete?"true":"false")
   <<",\"overlapping_portal_door_tiles\":"<<portal_inventory.overlapping_door_tiles
   <<",\"room_obstacle_candidates\":[";
  for(std::size_t i=0;i<room_obstacles.size();++i){if(i)std::cout<<',';const auto& candidate=room_obstacles[i];
   std::cout<<"{\"room\":"<<candidate.room<<",\"level\":"<<unsigned(candidate.level)<<",\"generation_complete\":"<<(candidate.complete?"true":"false")<<",\"rectangles\":"<<candidate.rectangles.size()<<'}';}
  std::size_t collision_candidate_count=0,collision_absent_count=0,collision_unsupported_count=0;
  std::size_t boundary_no_intersection=0,boundary_intersections=0,outer_bounds_intersections=0,boundary_unsupported=0,boundary_not_applicable=0;
  std::vector<odh::SavedCollisionCandidate> collision_candidates;collision_candidates.reserve(world->records.size());
  std::cout<<"],\"collision_candidates\":{\"evidence\":\"saved_snapshot_geometry_only\",\"route_clearance\":false,\"entities\":[";
  bool first_collision=true;
  for(const auto& record:world->records){
   const auto id=static_cast<std::int16_t>(record.saved.object_id);
   const auto* entity=world->world->find(id);
   const auto candidate=odh::saved_collision_candidate(record,entity);
   collision_candidates.push_back(candidate);
   const auto boundary=odh::audit_saved_room_boundary(candidate,rooms,room_obstacles);
   if(boundary.intersecting_outer_bounds>0)++outer_bounds_intersections;
   if(candidate.state==odh::SavedCollisionCandidateState::candidate)++collision_candidate_count;
   else if(candidate.state==odh::SavedCollisionCandidateState::absent)++collision_absent_count;
   else ++collision_unsupported_count;
   switch(boundary.state){
   case odh::SavedRoomBoundaryAuditState::no_intersection:++boundary_no_intersection;break;
   case odh::SavedRoomBoundaryAuditState::intersects_boundary:++boundary_intersections;break;
   case odh::SavedRoomBoundaryAuditState::intersects_outer_room_bounds:break;
   case odh::SavedRoomBoundaryAuditState::unsupported:++boundary_unsupported;break;
   case odh::SavedRoomBoundaryAuditState::not_applicable:++boundary_not_applicable;break;
   }
   if(!first_collision)std::cout<<',';
   first_collision=false;
   std::cout<<"{\"id\":"<<candidate.object_id<<",\"kind\":"<<json_bytes(candidate.avatar?"avatar":"object")
    <<",\"state\":"<<json_bytes(odh::saved_collision_candidate_state_name(candidate.state))
    <<",\"reason\":"<<json_bytes(odh::saved_collision_candidate_reason_name(candidate.reason))<<",\"box\":";
   if(candidate.box)std::cout<<'['<<candidate.box->x1<<','<<candidate.box->y1<<','<<candidate.box->x2<<','<<candidate.box->y2<<']';
   else std::cout<<"null";
   std::cout<<",\"room_boundary_audit\":{\"state\":"<<json_bytes(odh::saved_room_boundary_audit_state_name(boundary.state))
    <<",\"reason\":"<<json_bytes(odh::saved_room_boundary_audit_reason_name(boundary.reason))<<",\"room\":";
   if(boundary.room)std::cout<<*boundary.room;else std::cout<<"null";
   std::cout<<",\"intersecting_rectangles\":"<<boundary.intersecting_rectangles
    <<",\"intersecting_outer_bounds\":"<<boundary.intersecting_outer_bounds<<"}}";
  }
  std::cout<<"],\"counts\":{\"candidate\":"<<collision_candidate_count<<",\"absent\":"<<collision_absent_count
   <<",\"unsupported\":"<<collision_unsupported_count<<"},\"room_boundary_audit\":{\"evidence\":\"room_obstacles_and_outer_bounds_only\",\"route_clearance\":false,\"counts\":{\"no_intersection\":"
   <<boundary_no_intersection<<",\"intersects_boundary\":"<<boundary_intersections
   <<",\"intersects_outer_room_bounds_entities\":"<<outer_bounds_intersections<<",\"unsupported\":"<<boundary_unsupported
   <<",\"not_applicable\":"<<boundary_not_applicable<<"}}}";
  if(route_grid_diagnostic){std::cout<<",\"route_grid_diagnostic\":";
   const auto route_actor_record=records_by_id.find(route_grid_diagnostic->first);
   emit_route_grid_diagnostic(std::cout,route_grid_diagnostic->first,route_grid_diagnostic->second,
    architecture.architecture,result.appearance,rooms,room_obstacles,collision_candidates,world->multitile_groups,*route_slots,slot_resource->index,
    route_actor_record==records_by_id.end()?nullptr:route_actor_record->second,prepared?&*prepared:nullptr);
  }
  std::cout<<",\"portal_bhav_executed\":false,\"runtime_portal_state\":false,\"routing_clearance\":false,\"doors\":[";
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
