#include "odh/avatar_body.hpp"
#include <iostream>

namespace {
unsigned checks=0;
void check_impl(bool value,int line){++checks;if(!value)throw std::runtime_error("avatar body assertion "+std::to_string(checks)+" at line "+std::to_string(line));}
#define check(value) check_impl((value),__LINE__)
template<class F>void rejects(F f){try{f();}catch(const odh::FormatError&){check(true);return;}check(false);}
odh::StringTable table(std::vector<std::string> values,std::uint8_t language=1){
    odh::StringTable result{odh::StringTableState::decoded,-3,{},0};
    for(auto& value:values)result.entries.push_back({language,std::move(value),""});
    return result;
}
std::vector<std::string> fields(){
    std::vector<std::string> result(27);
    result[0]="adult";result[1]="body_first;body_unused";result[2]="head_unsplit;head_tail";
    result[9]="random_head_ignored";result[10]="random_body_ignored";result[12]="MaLe";result[13]="27";
    result[14]="lgt;med;drk";
    for(std::size_t i=17;i<=22;++i)result[i]="hand"+std::to_string(i);
    return result;
}
void avatar(odh::World& world,std::int16_t id=1){check(world.create(id,odh::EntityKind::avatar,{0,101,3,16}));}
}
void species_descriptors(){
    for(const auto& profile:std::vector<std::pair<std::string,odh::AvatarBodyType>>{
        {"adult",odh::AvatarBodyType::adult},{"child",odh::AvatarBodyType::child},
        {"dog",odh::AvatarBodyType::dog},{"cat",odh::AvatarBodyType::cat},{"kat",odh::AvatarBodyType::cat}}){
        odh::World world;avatar(world);odh::RandomState random{91};auto values=fields();
        values[0]=profile.first;values[12]=profile.first=="dog"?"dogmale":profile.second==odh::AvatarBodyType::cat?"catfemale":"female";
        values[13]="27";for(unsigned i=17;i<=22;++i)values[i].clear();
        const auto body=odh::parse_avatar_body_strings(table(values));const auto seed=random.seed();
        const auto descriptor=odh::derive_avatar_body_descriptor(world,1,random,body,
            table({"global-walk","global-turn"}),table({"global-swim"}),table({"","authored-turn","authored-run"}));
        check(descriptor.body_type==profile.second&&descriptor.body->avatar_type==profile.first&&
            !descriptor.child&&descriptor.global_walk_id==150&&descriptor.global_swim_id==158&&
            descriptor.walk[0]=="global-walk"&&descriptor.walk[1]=="authored-turn"&&descriptor.walk[2]=="authored-run"&&
            descriptor.swim[0]=="global-swim"&&random.seed()==seed);
        check(world.set_person_data(1,58,7));
        const auto live=odh::derive_avatar_body_descriptor(world,1,random,body,
            table({"child-live-walk"}),table({"child-live-swim"}),{},true);
        check(live.child&&live.global_walk_id==151&&live.global_swim_id==160&&live.walk[0]=="child-live-walk"&&
            live.swim[0]=="child-live-swim"&&live.body->age==27&&world.find(1)->person_data[58]==7&&random.seed()==seed);
        const auto applied=odh::apply_avatar_body(world,1,random,body,table({"global-walk"}),table({"global-swim"}));
        check(applied.body_type==profile.second&&world.find(1)->person_data[58]==27&&
            world.find(1)->person_data[65]==(profile.first=="dog"?8:profile.second==odh::AvatarBodyType::cat?17:1));
    }
    for(const auto& type:std::vector<std::string>{"","DOG","unknown",std::string("dog\0bad",7)}){
        check(!odh::avatar_body_type(type));odh::World world;avatar(world);odh::RandomState random{91};
        auto values=fields();values[0]=type;const auto body=odh::parse_avatar_body_strings(table(values));
        const auto person=world.find(1)->person_data,motives=world.find(1)->motives;const auto seed=random.seed();
        rejects([&]{odh::apply_avatar_body(world,1,random,body,table({"walk"}),table({"swim"}));});
        check(world.find(1)->person_data==person&&world.find(1)->motives==motives&&random.seed()==seed);
    }
}
int main(){try{species_descriptors();
    const auto walk=table({"adult-walk","adult-run","adult-back"}),swim=table({"adult-swim"});
    const auto childwalk=table({"child-walk"}),childswim=table({"child-swim"});
    auto input=fields();
    auto body=odh::parse_avatar_body_strings(table(input,0));
    check(body.avatar_type=="adult"&&body.age==27&&body.gender==0);
    check(body.body=="body_first"&&body.head=="head_unsplit;head_tail"&&body.hands[0]=="hand17"&&body.hands[5]=="hand22");
    check(body.names.empty()&&body.raw_names.empty());
    odh::World world;avatar(world);check(world.set_person_data(1,65,99));
    check(world.declare_object_word(1,8,234));
    for(std::int16_t i=0;i<16;++i)check(world.set_motive(1,i,-30));
    odh::RandomState random(123),expected(123);
    const auto original_person=world.find(1)->person_data,original_motives=world.find(1)->motives;
    const auto original_seed=random.seed();
    const auto descriptor=odh::derive_avatar_body_descriptor(world,1,random,body,walk,swim,
        table({"","private-run","private-back","private-extra"}));
    check(random.seed()==original_seed&&world.find(1)->person_data==original_person&&world.find(1)->motives==original_motives);
    auto result=odh::apply_avatar_body(world,1,random,body,walk,swim,table({"","private-run","private-back","private-extra"}));
    check(descriptor.body&&result.body&&descriptor.body->age==result.body->age&&descriptor.body->body==result.body->body&&descriptor.body->head==result.body->head);
    check(descriptor.selected_name==result.selected_name&&descriptor.phenotype==result.phenotype);
    check(descriptor.child==result.child&&descriptor.global_walk_id==result.global_walk_id&&descriptor.global_swim_id==result.global_swim_id);
    check(descriptor.walk==result.walk&&descriptor.swim==result.swim);
    odh::World compact_saved;check(compact_saved.create(42,odh::EntityKind::avatar,{0,80,3,16}));
    check(odh::derive_avatar_body_descriptor(compact_saved,42,random,body,walk,swim).body->age==body.age);
    check(random.seed()==123&&!result.selected_name&&result.phenotype==odh::AvatarPhenotype::ts1_light);
    check(result.body&&result.body->body=="body_first"&&!result.child&&result.global_walk_id==150&&result.global_swim_id==158);
    check(result.walk[0]=="adult-walk"&&result.walk[1]=="private-run"&&result.walk[3]=="private-extra"&&result.walk[49].empty());
    check(result.swim[0]=="adult-swim"&&result.swim[1].empty());
    auto* entity=world.find(1);check(entity&&entity->person_data[58]==27&&entity->person_data[65]==0&&entity->person_data[7]==1000);
    check(entity->object_words.at(8)==234);
    for(std::size_t i=0;i<16;++i)check(entity->motives[i]==(i==11?0:100));
    // Only a nonempty name list consumes RNG, including one possible name.
    input[11]=";OnlyName;;";body=odh::parse_avatar_body_strings(table(input));expected.next(1);
    result=odh::apply_avatar_body(world,1,random,body,walk,swim);
    check(result.selected_name=="OnlyName"&&random.seed()==expected.seed()&&random.seed()!=123);
    input[11]="Alice;;Bob;Carol;";body=odh::parse_avatar_body_strings(table(input));const auto choice=expected.next(3);
    result=odh::apply_avatar_body(world,1,random,body,walk,swim);
    check(body.names.size()==3&&result.selected_name==body.names[static_cast<std::size_t>(choice)]&&random.seed()==expected.seed());
    const std::vector<std::pair<std::string,std::int16_t>> genders={{"male",0},{"FEMALE",1},{"dogmale",8},{"DogFemale",9},{"CATMALE",16},{"catfemale",17}};
    input[11]="";
    for(const auto& item:genders){input[12]=item.first;body=odh::parse_avatar_body_strings(table(input));
        odh::apply_avatar_body(world,1,random,body,walk,swim);check(world.find(1)->person_data[65]==item.second);}
    input[12]="unknown";input[13]=" +18 ";body=odh::parse_avatar_body_strings(table(input));
    check(!body.gender&&body.age==18);result=odh::apply_avatar_body(world,1,random,body,walk,swim);check(!result.child&&world.find(1)->person_data[65]==17);
    for(const auto& value:{"17","0","-1","-32768"}){input[13]=value;body=odh::parse_avatar_body_strings(table(input));
        result=odh::apply_avatar_body(world,1,random,body,childwalk,childswim);
        check(result.child&&result.global_walk_id==151&&result.global_swim_id==160&&result.walk[0]=="child-walk"&&result.swim[0]=="child-swim");
        check(world.find(1)->person_data[58]==body.age);}
    input[13]="32767";input[1]="";input[2]="";body=odh::parse_avatar_body_strings(table(input));
    result=odh::apply_avatar_body(world,1,random,body,walk,swim);check(!result.child&&result.body->body.empty()&&result.body->head.empty());
    const auto before=random.seed();
    result=odh::apply_avatar_body(world,1,random,{},walk,swim);
    check(!result.body&&!result.selected_name&&!result.phenotype&&!result.child&&random.seed()==before&&world.find(1)->person_data[58]==32767);
    for(const auto& bad:{"32768","-32769","","1x","1.0","+-1","--1"}){input[13]=bad;rejects([&]{odh::parse_avatar_body_strings(table(input));});}
    input=fields();input[11]=";;;";rejects([&]{odh::parse_avatar_body_strings(table(input));});
    input=fields();input.resize(22);rejects([&]{odh::parse_avatar_body_strings(table(input));});
    input=fields();input.resize(50);check(odh::parse_avatar_body_strings(table(input)).age==27);
    input.resize(51);rejects([&]{odh::parse_avatar_body_strings(table(input));});
    rejects([&]{odh::parse_avatar_body_strings(table(fields(),2));});
    auto malformed=table(fields());malformed.state=odh::StringTableState::header_only;rejects([&]{odh::parse_avatar_body_strings(malformed);});
    // Rejected apply calls preserve all affected state and the RNG snapshot.
    body=odh::parse_avatar_body_strings(table(fields()));body.raw_names="Chosen";body.names={"Chosen"};
    const auto saved_person=world.find(1)->person_data,saved_motives=world.find(1)->motives;
    auto preserved=[&]{check(random.seed()==before&&world.find(1)->person_data==saved_person&&world.find(1)->motives==saved_motives);};
    rejects([&]{odh::apply_avatar_body(world,1,random,body,table({},2),swim);});preserved();
    rejects([&]{odh::apply_avatar_body(world,1,random,body,walk,table(std::vector<std::string>(51)));});preserved();
    rejects([&]{odh::apply_avatar_body(world,1,random,body,walk,swim,table({},2));});preserved();
    rejects([&]{odh::apply_avatar_body(world,2,random,body,walk,swim);});preserved();
    check(world.create(2,odh::EntityKind::object));
    rejects([&]{odh::apply_avatar_body(world,2,random,body,walk,swim);});preserved();
    check(world.create(3,odh::EntityKind::avatar,{0,100,0,16}));
    rejects([&]{odh::apply_avatar_body(world,3,random,body,walk,swim);});preserved();
    check(world.create(4,odh::EntityKind::avatar,{0,101,0,15}));
    rejects([&]{odh::apply_avatar_body(world,4,random,body,walk,swim);});preserved();
    body.age.reset();rejects([&]{odh::apply_avatar_body(world,1,random,body,walk,swim);});preserved();
    body.age=27;body.names.clear();rejects([&]{odh::apply_avatar_body(world,1,random,body,walk,swim);});preserved();
    body.names={"Chosen"};body.head=std::string(odh::avatar_body_text_bytes,'a');
    rejects([&]{odh::apply_avatar_body(world,1,random,body,walk,swim);});preserved();
    rejects([&]{odh::apply_avatar_body(world,1,random,{},table({std::string(odh::avatar_body_text_bytes+1,'a')}),swim);});preserved();
    result=odh::apply_avatar_body(world,1,random,{},table({std::string(odh::avatar_body_text_bytes,'a')}),swim);
    check(result.walk[0].size()==odh::avatar_body_text_bytes&&random.seed()==before);
    input=fields();std::size_t bytes=0;for(const auto& value:input)bytes+=value.size();
    input[3]=std::string(odh::avatar_body_text_bytes-bytes,'a');check(odh::parse_avatar_body_strings(table(input)).age==27);
    input[3]+='a';rejects([&]{odh::parse_avatar_body_strings(table(input));});
    input=fields();for(std::size_t i=0;i<odh::avatar_body_names;++i)input[11]+="name;";
    body=odh::parse_avatar_body_strings(table(input));check(body.names.size()==odh::avatar_body_names);
    input[11]+="extra";rejects([&]{odh::parse_avatar_body_strings(table(input));});
    body.raw_names=input[11];body.names.push_back("extra");
    rejects([&]{odh::apply_avatar_body(world,1,random,body,walk,swim);});preserved();
    result=odh::apply_avatar_body(world,1,random,{},table(std::vector<std::string>(50,"walk")),table(std::vector<std::string>(50,"swim")));
    check(result.walk[49]=="walk"&&result.swim[49]=="swim"&&random.seed()==before);
    auto foreign=table(fields());foreign.entries.resize(65536,odh::StringEntry{2,"",""});
    check(odh::parse_avatar_body_strings(foreign).age==27);
    foreign.entries.push_back({2,"",""});rejects([&]{odh::parse_avatar_body_strings(foreign);});
    body=odh::parse_avatar_body_strings(table(fields()));
    odh::AvatarOutfitReference live;live.literal="sleep,BODY=sleeptexture";live.hands.fill("newhand");
    check(world.set_avatar_outfit(1,live,true));
    const auto effective=odh::effective_avatar_body(body,*world.find(1));
    check(effective.body==live.literal&&effective.hands==live.hands&&effective.head==body.head&&body.body=="body_first");
    check(world.add_bound_appearance(1,"Raw","Raw.apr"));
    check(odh::effective_avatar_accessories(*world.find(1))==std::vector<std::string>{"Raw.apr"});
    live.kind=odh::AvatarOutfitKind::identifier;live.identifier=123;live.literal.clear();live.hands.fill("");
    check(world.set_avatar_outfit(1,live));
    rejects([&]{odh::effective_avatar_body(body,*world.find(1));});
    std::cout<<checks<<" avatar body checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
