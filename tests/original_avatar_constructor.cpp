// SPDX-License-Identifier: MPL-2.0
#include "odh/original_avatar_constructor.hpp"
#include "odh/native_personal_state.hpp"
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
constexpr odh::CodeOwner owner=77;
constexpr std::uint32_t guid=0xfc72c0c0;
odh::NeighborRecord seed(unsigned schema=5,unsigned record=4){
 odh::NeighborRecord n{};n.version=record;n.person_mode=schema;n.guid=guid;n.neighbor_id=34;n.name="Source name";
 if(schema){n.person_data.emplace(record==4?80:256,0);auto& b=*n.person_data;b[58]=30;b[61]=5;b[65]=1;
  for(unsigned i=46;i<=55;++i)b[i]=500;b[31]=999;b[32]=99;b[34]=99;b[35]=99;b[77]=999;}
 return n;
}
odh::Instruction assign(std::uint8_t scope,unsigned index,std::int16_t value,std::uint8_t next){
 const auto bits=static_cast<std::uint16_t>(value);return {2,next,253,{static_cast<std::uint8_t>(index),static_cast<std::uint8_t>(index>>8),static_cast<std::uint8_t>(bits),static_cast<std::uint8_t>(bits>>8),0,5,scope,7}};
}
odh::Bhav routine(std::initializer_list<odh::Instruction> code){return {0x8002,code,{},odh::Bhav::Metadata{0,4,0,0}};}
odh::EntryPointTable entries(){odh::EntryPointTable e{};e.entries.resize(9);e.entries[0].action=4096;e.entries[1].action=4097;e.entries[8].action=4098;return e;}
odh::ObjectDefinition definition(){odh::ObjectDefinition d{};d.version=138;d.guid=guid;d.object_type=2;d.num_attributes=2;d.level_offset=0;return d;}
odh::StringTable strings(const char* s){return {odh::StringTableState::decoded,-3,{{1,s,{}}},0};}
void schemas(){
 for(const auto schema:{0U,1U,5U,9U})for(const auto record:{4U,10U}){
  const auto n=seed(schema,record);const auto p=odh::original_neighbor_seed_projection(n);
  require(p.has_value()==(schema!=0),"schema0 is absence, not zero-valued projection");
  std::vector<std::int16_t> b(256,777);odh::apply_original_neighbor_personal(n,b);
  require(b[31]==34&&b[61]==(schema?5:0),"separate exact NID/family binding");
  require(b[32]==777&&b[34]==777&&b[35]==777&&b[77]==777&&b[8]==777,"selective loader cannot copy workspace, role, greet, outfit");
  if(schema){require(p->schema_version==schema&&p->present[80]&&p->values[80]==0,"present serialized short-bank tail is known zero");
   require(bool(p->present[65])==(schema>=3)&&bool(p->present[54])==(schema>=7),"serialized schema gates modern fields independently of record format");
   require(b[58]==30,"selected age loaded");}
 }
 auto n=seed();n.person_data->resize(81);bool failed=false;try{(void)odh::original_neighbor_seed_projection(n);}catch(const odh::FormatError&){failed=true;}require(failed,"invalid source bank shape rejected");
 n=seed();odh::NativePersonalProjection overlay;overlay.schema_version=9;overlay.present[58]=1;overlay.values[58]=9;n.native_personal=overlay;
 std::vector<std::int16_t> b(256);odh::apply_original_neighbor_personal(n,b);require(b[58]==9,"authorized mutable projection follows immutable seed");
}
void constructor(bool sentinel,bool residential){
 odh::World world{{},odh::AttributePolicy::ts1_bounded};bool current=true;
 odh::NeighborContext neighbors({0,62,{{1,seed()}},0});
 auto selected=odh::resolve_original_load_avatar_request(34,neighbors,world,residential,[&]{return current;});
 require(selected.status==odh::NpcAvatarRequestStatus::ready&&selected.request.has_value(),"source frontend request needs no fake caller");
 auto request=*selected.request;request.original_definition_version=138;
 const auto table=entries();require(odh::materialize_npc_avatar(world,2,request,definition(),table,12),"fresh original body materialized");
 require(world.find(2)->person_data.size()==256&&world.find(2)->motives[5]==70&&world.find(2)->motives[7]==-40&&world.find(2)->motives[9]==0,"original pre-Init needs baseline and person width");
 const odh::WorldEntityReference ref{2,world.find(2)->generation};
 require(world.set_person_data(2,32,0)&&world.set_person_data(2,34,3)&&world.set_person_data(2,35,4),"initial status words fixture");
 unsigned main_calls=0,init_calls=0;std::vector<unsigned> order;
 odh::RoutineSet routines{
  {odh::routine_key(4096),routine({assign(18,58,9,1),assign(18,77,222,2),assign(14,5,44,3),assign(18,25,sentinel?1966:0,254)})},
  {odh::routine_key(4097),routine({assign(18,77,333,1),{15,0,253,{}}})},
  {odh::routine_key(4098),routine({assign(18,77,999,254)})}
 };
 odh::Vm vm(odh::RoutineSet{},world.vm_memory(),odh::VmLimits{},
  [&](const auto&,const auto& instruction,const auto&,const auto& dispatch){
   require(instruction.opcode==15&&!dispatch.check_mode,"only Main reaches real ordinary yield");++main_calls;
   return odh::PrimitiveOutcome{odh::PrimitiveDisposition::continue_next_tick,{}};
  },odh::RoutineOwners{{owner,std::move(routines)}});
 request.original_main_prepared=[&](auto exact,auto& actual,std::string&){require(exact==ref&&&actual==&vm&&actual.steps()==0&&actual.run_generation()==1,"one real Main prepared before first Init");order.push_back(1);return true;};
 request.original_guarded_init=[&](auto exact,auto& actual,auto code,const auto& operation){require(exact==ref&&&actual==&vm&&code==owner,"Init uses actual owner");order.push_back(++init_calls==1?2:5);return operation();};
 request.original_initial_tick=[&](auto exact,auto& actual,std::uint64_t budget,std::string&){
  require(exact==ref&&world.find(2)->person_data[58]==9&&world.find(2)->motives[5]==44,"first Init effects precede real Main");order.push_back(3);return actual.run(budget)==odh::VmStatus::next_tick;};
 request.original_lookup_flags=[&](auto exact,std::string&){require(main_calls==1&&world.find(2)->person_data[77]==333,"lookup publication follows existing Main execution");order.push_back(4);return world.publish_lookup_selector(exact,2,true)&&world.publish_lookup_selector(exact,8,false);};
 request.original_curves_ready=[&](auto exact,std::string&){require(exact==ref&&init_calls==2&&world.find(2)->person_data[58]==(sentinel?9:30),"interaction curves bind final selective age after both Init calls");order.push_back(6);return true;};
 odh::NpcAvatarBodyInputs input;input.body=odh::AvatarBodyStrings{30,0,"adult","body","head",{},"Name",{"Name"}};
 input.global_walk=strings("walk");input.global_swim=strings("swim");input.birth_time=odh::NpcAvatarBirthTime{2001,9,7,12,34};odh::RandomState random{12345};
 const auto before_random=random.seed();
 const auto result=odh::initialize_npc_avatar(world,vm,2,owner,request,table,input,random,{},{});
 require(result.completed&&!result.self_deleted&&result.main_started,"source-ordered initialization completes");
 require(order==std::vector<unsigned>({1,2,3,4,5,6})&&main_calls==1&&init_calls==2,"Main is prepared/executed once, both real Init calls surround source selectors");
 const auto* live=world.find(2);
 require(live->person_data[58]==(sentinel?9:30)&&live->person_data[25]==0,"reload sentinel1966 distinct from normal selective reload");
 require(live->person_data[77]==222&&live->person_data[31]==34&&live->person_data[61]==5,"second Init workspace survives selective reload and no EP8 runs");
 require(live->person_data[32]==(residential?0:1)&&live->person_data[34]==3&&live->person_data[35]==4,"status retained with original nonresidential classification");
 require(live->motives[5]==44&&live->motives[7]==-40&&live->motives[9]==0,"Init needs retained, absent effects never reset to100");
 require(world.lookup_selectors(ref,1)==std::optional<std::uint16_t>{1},"final scheduling publication follows completed initializer");
 require(random.seed()==before_random&&result.body->selected_name=="Source name","scaled source personality and source name do not consume a PIN random name draw");
 require(!odh::initialize_npc_avatar(world,vm,2,owner,request,table,input,random,{},{}).completed&&main_calls==1,"already initialized body cannot be reinitialized through fresh constructor");
 current=false;require(odh::resolve_original_load_avatar_request(34,neighbors,world,residential,[&]{return current;}).status!=odh::NpcAvatarRequestStatus::ready,"expired frontend scope cannot allocate");
}
}
int main(){try{schemas();for(bool sentinel:{false,true})for(bool residential:{false,true})constructor(sentinel,residential);
 std::cout<<"original avatar constructor: "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
