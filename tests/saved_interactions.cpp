#include "saved_interactions.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void put(std::string& b,std::uint32_t v,unsigned n,bool big=false){for(unsigned i=0;i<n;++i)b+=static_cast<char>((v>>((big?n-1-i:i)*8))&255);}
std::string chunk(const std::string& type,unsigned id,const std::string& payload){
    std::string b(76,'\0');b.replace(0,4,type);for(unsigned i=0;i<4;++i)b[4+i]=static_cast<char>((76+payload.size())>>((3-i)*8));
    b[8]=static_cast<char>(id>>8);b[9]=static_cast<char>(id);return b+payload;
}
std::string iff(const std::string& payload){std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');return b+payload;}
std::string ttab(){std::string b;put(b,1,2);put(b,8,2);put(b,0x1001,2);put(b,0x1002,2);put(b,0,4);put(b,0,4);put(b,0,4);put(b,4,4);put(b,0x3f800000,4);put(b,50,4);put(b,0xffffffff,4);return b;}
std::string labels(){std::string b;put(b,0,2);put(b,1,2);b+=char(3);b+="Sit";return b;}
std::string localized_labels(){std::string b;put(b,65533,2);put(b,2,2);
    b.push_back(1);b+=std::string("Caf\xe9\0\0",6);b.push_back(14);b+=std::string("Sentar\0\0",8);return b;}
std::string test_bhav(unsigned opcode=2){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(4);b.append(6,'\0');
    put(b,opcode,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));
    if(opcode==2){b.push_back(0);b.push_back(0);b.push_back(11);b.push_back(0);b.push_back(0);b.push_back(2);b.push_back(16);b.push_back(3);}
    else b.append(8,'\0');return b;}
std::string call_bhav(unsigned callee){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(4);b.append(6,'\0');
    put(b,callee,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));b.append(8,'\0');return b;}
std::string write_bhav(){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(4);b.append(6,'\0');
    put(b,2,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));
    b.push_back(0);b.push_back(0);b.push_back(1);b.push_back(0);b.push_back(0);b.push_back(5);b.push_back(3);b.push_back(7);return b;}
void write(const std::filesystem::path& path,const std::string& bytes){std::ofstream f(path,std::ios::binary);f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));if(!f)throw std::runtime_error("fixture write failed");}
}
int main(){try{
    const auto root=std::filesystem::temp_directory_path()/"odh-saved-interactions-test";
    std::filesystem::create_directories(root);const auto path=root/"object.iff";
    auto data=iff(chunk("TTAB",128,ttab())+chunk("TTAs",128,labels()));write(path,data);
    odh::ObjectDefinition object{};object.guid=123;object.tree_table_id=128;
    const auto result=saved_object_interactions({path.string(),"",7,object});
    check(result.state==SavedObjectInteractions::State::found&&result.table&&result.table->interactions.size()==1,"owner TTAB discovered");
    check(result.source==path.string()&&result.table_id==128&&result.asset.empty(),"exact owner provenance retained");
    check(result.labels.state==odh::StringTableState::decoded&&result.row_labels.size()==1&&result.row_labels[0]=="Sit","TTAs label mapped by explicit language/index");
    data=iff(chunk("TTAB",131,ttab()));write(path,data);
    odh::ObjectDefinition part{};part.guid=3586796916U;part.master_id=11;part.sub_index=0;part.tree_table_id=0xffff;
    odh::ObjectDefinition master{};master.guid=2091978777U;master.master_id=11;master.sub_index=-1;master.tree_table_id=131;
    const odh::DefinitionCandidate part_candidate{path.string(),"",16848,part};
    const odh::DefinitionCandidate master_candidate{path.string(),"",16847,master};
    const odh::DefinitionIndex group_index({part_candidate,master_candidate});
    const auto inherited=saved_object_interactions(part_candidate,group_index);
    check(inherited.state==SavedObjectInteractions::State::found&&inherited.table_id==131&&
          inherited.table&&inherited.table->interactions[0].action==0x1001,
          "multitile part inherits its exact same-resource master TTAB");
    check(inherited.source==part_candidate.source&&inherited.asset==part_candidate.asset,
          "inherited TTAB retains selected-part provenance");
    const odh::DefinitionCandidate other_resource_master{path.string(),"Sofas.iff",16847,master};
    const odh::DefinitionIndex split_index({part_candidate,other_resource_master});
    const auto no_cross_resource=saved_object_interactions(part_candidate,split_index);
    check(no_cross_resource.state==SavedObjectInteractions::State::missing_ttab&&
          no_cross_resource.table_id==0xffff,
          "multitile part never inherits a master TTAB from another resource");
    data=iff(chunk("GLOB",128,std::string(1,'\x0b')+"SofaGlobals"));write(path,data);
    const auto linked_catalog=load_saved_tree_name_catalog(part_candidate);
    check(linked_catalog.semiglobal_name==std::optional<std::string>("SofaGlobals"),
          "GLOB semiglobal link is decoded from the payload, not the IFF chunk label");
    data=iff(chunk("TTAB",128,ttab())+chunk("TTAs",128,localized_labels()));write(path,data);
    const auto portuguese=saved_object_interactions({path.string(),"",7,object},14);
    const auto missing_language=saved_object_interactions({path.string(),"",7,object},5);
    check(portuguese.language==14&&portuguese.row_labels[0]=="Sentar","interaction label follows selected Portuguese group");
    check(missing_language.language==5&&missing_language.row_labels[0]==std::string("Caf\xe9")&&
          missing_language.row_label_languages[0]==1,
          "missing localized interaction label retains English fallback code page and source language");
    data=iff(chunk("TTAB",128,ttab()));write(path,data);
    const auto no_labels=saved_object_interactions({path.string(),"",7,object});
    check(no_labels.state==SavedObjectInteractions::State::found&&!no_labels.row_labels[0],"missing TTAs leaves decoded interaction unlabeled");
    const std::vector<SavedTestFunctionResult> eligible_row{{SavedTestFunctionResult::State::eligible,0x1002,3,{}}};
    odh::World actor_world;check(actor_world.create(1,odh::EntityKind::avatar,{0,66,0,0}),"actor permission profile created");
    check(actor_world.set_person_data(1,32,0)&&actor_world.set_person_data(1,34,0)&&
          actor_world.set_person_data(1,58,27)&&actor_world.set_person_data(1,65,0),"resident adult permission profile initialized");
    const auto* actor=actor_world.find(1);
    check(first_eligible_saved_interaction(no_labels,eligible_row,actor)==0,
        "zero-flag private action accepts resident with matching eligible TestFunction");
    auto semiglobal_action=no_labels;
    semiglobal_action.table->interactions[0].action=8213;
    odh::RoutineSet linked_actions{{odh::routine_key(8213),odh::Bhav{0x8002,{}, {},odh::Bhav::Metadata{0,4,4,0}}}};
    check(!first_eligible_saved_interaction(semiglobal_action,eligible_row,actor),
        "semiglobal ActionFunction is rejected when its exact linked code bank is absent");
    check(first_eligible_saved_interaction(semiglobal_action,eligible_row,actor,&linked_actions)==0,
        "semiglobal ActionFunction is accepted only when present in the explicit linked bank");
    auto multiple=no_labels;
    multiple.table->interactions.push_back(multiple.table->interactions[0]);
    multiple.table->interactions.push_back(multiple.table->interactions[0]);
    multiple.table->interactions[1].action=0x1003;multiple.table->interactions[1].test=0x1004;
    multiple.table->interactions[2].action=0x1005;multiple.table->interactions[2].test=0x1006;
    const std::vector<SavedTestFunctionResult> mixed_rows{
        {SavedTestFunctionResult::State::eligible,0x1002,3,{}},
        {SavedTestFunctionResult::State::ineligible,0x1004,1,{}},
        {SavedTestFunctionResult::State::eligible,0x1006,2,{}}};
    const auto eligible_rows=eligible_saved_interaction_rows(multiple,mixed_rows,actor);
    check(eligible_rows==std::vector<std::size_t>{0,2},
        "interaction chooser enumerates eligible rows in TTAB order and filters failed tests");
    check(cycle_eligible_saved_interaction_row(eligible_rows,0,1)==2&&
          cycle_eligible_saved_interaction_row(eligible_rows,2,1)==0&&
          cycle_eligible_saved_interaction_row(eligible_rows,0,-1)==2,
        "interaction chooser cycles forward and backward across eligible rows");
    check(cycle_eligible_saved_interaction_row(eligible_rows,1,1)==0&&
          cycle_eligible_saved_interaction_row(eligible_rows,2,0)==2&&
          !cycle_eligible_saved_interaction_row({},std::nullopt,1),
        "interaction chooser recovers a stale selection at the first live row and handles empty rows");
    auto visitor_allowed=no_labels;visitor_allowed.table->interactions[0].flags=1;
    check(first_eligible_saved_interaction(visitor_allowed,eligible_row,actor)==0,
        "AllowVisitors flag accepts resident adult");
    check(actor_world.set_person_data(1,32,1)&&actor_world.set_person_data(1,34,1),"active visitor profile initialized");
    check(!first_eligible_saved_interaction(no_labels,eligible_row,actor),
        "visitor with greet status below two is rejected when AllowVisitors is clear");
    check(first_eligible_saved_interaction(visitor_allowed,eligible_row,actor)==0,
        "visitor with greet status below two is accepted when AllowVisitors is set");
    check(actor_world.set_person_data(1,34,2),"greet threshold profile initialized");
    check(first_eligible_saved_interaction(no_labels,eligible_row,actor)==0,
        "PersonType one with greet status two or greater is not treated as visitor");
    auto unknown_flags=no_labels;unknown_flags.table->interactions[0].flags=2;
    check(!first_eligible_saved_interaction(unknown_flags,eligible_row,actor),
        "unknown TTAB permission bit remains unsupported");
    unknown_flags.table->interactions[0].flags=3;
    check(!first_eligible_saved_interaction(unknown_flags,eligible_row,actor),
        "known AllowVisitors plus unknown TTAB bit remains unsupported");
    check(actor_world.set_person_data(1,34,0)&&actor_world.set_person_data(1,65,8),"pet profile initialized");
    check(!first_eligible_saved_interaction(visitor_allowed,eligible_row,actor),
        "pet interaction permissions remain outside the supported profile");
    auto mismatched=eligible_row;mismatched[0].routine=0x1003;
    check(actor_world.set_person_data(1,65,0),"resident gender profile restored");
    check(!first_eligible_saved_interaction(no_labels,mismatched,actor),"manual action selector requires TestFunction identity match");
    data=iff(chunk("TTAB",128,ttab())+chunk("TTAB",128,ttab()));write(path,data);
    bool duplicate=false;try{(void)saved_object_interactions({path.string(),"",7,object});}catch(const odh::FormatError&){duplicate=true;}
    check(duplicate,"duplicate owner TTAB is rejected");
    object.tree_table_id=0;check(saved_object_interactions({path.string(),"",7,object}).state==SavedObjectInteractions::State::no_tree_table_id,"zero table id distinguished");
    object.tree_table_id=128;write(path,iff(""));check(saved_object_interactions({path.string(),"",7,object}).state==SavedObjectInteractions::State::missing_ttab,"absent TTAB distinguished");
    data=iff(chunk("BHAV",4098,test_bhav())+chunk("BHAV",4097,test_bhav())+chunk("BHAV",256,test_bhav()));write(path,data);object.guid=123;
    const auto private_code=load_saved_private_routines({path.string(),"",7,object});
    check(private_code.size()==2&&private_code.count(odh::routine_key(4097))&&private_code.count(odh::routine_key(4098)),
        "action owner loads exact private BHAVs and excludes shared global code");
    bool owner_budget=false;try{(void)load_saved_private_routines({path.string(),"",7,object},1);}
    catch(const odh::FormatError&){owner_budget=true;}
    check(owner_budget,"private owner resource budget rejects overflow");
    data=iff(chunk("BHAV",4098,test_bhav())+chunk("BHAV",4098,test_bhav()));write(path,data);
    bool duplicate_private=false;try{(void)load_saved_private_routines({path.string(),"",7,object});}
    catch(const odh::FormatError&){duplicate_private=true;}
    check(duplicate_private,"ambiguous private action owner rejected");
    data=iff(chunk("BHAV",4098,call_bhav(4097))+chunk("BHAV",4097,call_bhav(4099))+
             chunk("BHAV",4099,test_bhav())+chunk("BHAV",4100,test_bhav()));write(path,data);
    const auto closure=load_saved_action_routines({path.string(),"",7,object},4098);
    check(closure.size()==3&&closure.count(odh::routine_key(4098))&&closure.count(odh::routine_key(4097))&&
          closure.count(odh::routine_key(4099))&&!closure.count(odh::routine_key(4100)),
          "action owner loads only reachable private call closure");
    bool missing_dependency=false;data=iff(chunk("BHAV",4098,call_bhav(4097)));write(path,data);
    try{(void)load_saved_action_routines({path.string(),"",7,object},4098);}
    catch(const odh::FormatError&){missing_dependency=true;}
    check(missing_dependency,"missing private action dependency is rejected without fallback");
    bool closure_limit=false;data=iff(chunk("BHAV",4098,call_bhav(4097))+chunk("BHAV",4097,test_bhav()));write(path,data);
    try{(void)load_saved_action_routines({path.string(),"",7,object},4098,1);}
    catch(const odh::FormatError&){closure_limit=true;}
    check(closure_limit,"private action closure routine budget is enforced");
    data=iff(chunk("BHAV",4098,test_bhav()));write(path,data);
    odh::World world;check(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object,{0,0,1}),"probe world entities created");
    check(world.define_type(2,123),"target identity bound");
    auto blocked=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(blocked.size()==1&&blocked[0].state==SavedTestFunctionResult::State::ineligible&&blocked[0].steps==1,"isolated read-only TestFunction false result");
    check(world.attach(2,0,1),"occupant inserted into object slot");
    auto eligible=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(eligible[0].state==SavedTestFunctionResult::State::eligible,"read-only probe sees live slot occupant");
    check(world.detach(2,0),"live occupant removed before action-boundary eligibility recheck");
    auto stale_eligibility=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(stale_eligibility[0].state==SavedTestFunctionResult::State::ineligible,
        "fresh TestFunction result supersedes an earlier eligible click-time result");
    data=iff(chunk("BHAV",4098,test_bhav(49)));write(path,data);
    auto unsupported=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(unsupported[0].state==SavedTestFunctionResult::State::unsupported&&!unsupported[0].diagnostic.empty(),"unsupported primitive never becomes eligible");
    data=iff(chunk("BHAV",4098,write_bhav()));write(path,data);
    auto write_attempt=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(write_attempt[0].state==SavedTestFunctionResult::State::unsupported&&write_attempt[0].diagnostic.find("write")!=std::string::npos,"TestFunction write rejected by read-only memory view");
    data=iff(chunk("BHAV",4098,test_bhav()));write(path,data);
    auto exhausted=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},0);
    check(exhausted[0].state==SavedTestFunctionResult::State::unsupported&&exhausted[0].diagnostic.find("budget")!=std::string::npos,"zero instruction budget is unsupported");
    std::filesystem::remove_all(root);std::cout<<checks<<" saved interaction discovery checks passed (synthetic IFF only)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
