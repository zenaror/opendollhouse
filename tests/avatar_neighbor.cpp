#include "odh/avatar_neighbor.hpp"
#include <iostream>

namespace {
unsigned checks=0;
void check_impl(bool value,int line){++checks;if(!value)throw std::runtime_error("avatar neighbor assertion at line "+std::to_string(line));}
#define check(v) check_impl((v),__LINE__)
template<class F> void rejects(F f){try{f();}catch(const odh::FormatError&){check(true);return;}check(false);}
odh::NeighborRecord record(int version=4){
    odh::NeighborRecord r{};r.version=version;r.person_mode=1;r.neighbor_id=48;r.guid=0x12345678;
    r.person_data=std::vector<std::int16_t>(version==4?80:256,23);
    (*r.person_data)[61]=7;(*r.person_data)[65]=0;(*r.person_data)[32]=9;(*r.person_data)[68]=1;return r;
}
void avatar(odh::World& w,std::size_t size=101){
    check(w.create(1,odh::EntityKind::avatar,{3,size,3,16}));check(w.define_type(1,0x12345678));
    if(size>=69){check(w.set_person_data(1,65,1));check(w.set_person_data(1,32,0));check(w.set_person_data(1,35,77));}
    check(w.set_motive(1,0,-33));check(w.declare_object_word(1,8,264));
}
}
int main(){try{
    odh::World w;avatar(w);const auto before=*w.find(1);auto r=record();
    auto d=odh::inherit_neighbor(w,1,r);
    check(d.neighbor_id==48&&d.guid==r.guid&&!d.current_family&&d.neighbor_family==7);
    check(d.bank_words_before==101&&d.bank_words_after==88&&d.saved_data_present);
    check(d.retained_gender==1&&d.retained_person_type==0&&d.retained_visitor_schedule==77);
    check(d.selected_person_type==1&&d.ghost==1);
    auto* e=w.find(1);check(e->person_data.size()==88&&e->person_data[65]==1&&e->person_data[31]==48);
    check(e->person_data[32]==1&&e->person_data[35]==77&&e->person_data[34]==0);
    for(std::size_t i=80;i<88;++i)check(e->person_data[i]==0);
    check(e->person_data[0]==23&&e->motives==before.motives&&e->attributes==before.attributes);
    check(e->object_words==before.object_words&&e->slots==before.slots&&e->generation==before.generation);
    check(e->definition->guid==before.definition->guid&&w.word_count()==3+88+3+16+1);
    (*r.person_data)[0]=99;check(e->person_data[0]==23);
    // Runtime owns its copy after the source record is destroyed.
    {auto temporary=record();temporary.neighbor_id=-4;odh::inherit_neighbor(w,1,temporary);}
    check(w.find(1)->person_data[31]==-4);
    // Old nonzero types, including unknown signed values, remain unchanged.
    for(const auto type:{std::int16_t(4),std::int16_t(-9)}){
        check(w.set_person_data(1,32,type));d=odh::inherit_neighbor(w,1,record(),7);
        check(d.selected_person_type==type&&w.find(1)->person_data[32]==type);
    }
    check(w.set_person_data(1,32,0));d=odh::inherit_neighbor(w,1,record(),7);check(d.selected_person_type==0);
    check(w.set_person_data(1,32,0));d=odh::inherit_neighbor(w,1,record(),8);check(d.selected_person_type==1);
    r=record();(*r.person_data)[61]=-1;check(w.set_person_data(1,32,0));
    d=odh::inherit_neighbor(w,1,r,65535);check(d.selected_person_type==1&&d.neighbor_family==-1);
    check(w.set_person_data(1,32,0));(*r.person_data)[61]=0;
    d=odh::inherit_neighbor(w,1,r);check(d.selected_person_type==1);
    check(w.set_person_data(1,32,0));d=odh::inherit_neighbor(w,1,r,0);check(d.selected_person_type==0);
    r=record(10);(*r.person_data)[87]=87;(*r.person_data)[88]=88;r.neighbor_id=0;
    d=odh::inherit_neighbor(w,1,r);check(d.bank_words_after==88&&w.find(1)->person_data[87]==87&&d.neighbor_id==0);
    odh::World modern;avatar(modern);(*r.person_data)[68]=-3;
    d=odh::inherit_neighbor(modern,1,r);check(d.bank_words_before==101&&d.bank_words_after==88&&d.ghost==-3);
    check(modern.find(1)->person_data[68]==-3&&modern.find(1)->person_data[87]==87);
    // Absent saved data keeps the complete old bank including its tail.
    odh::World absent;avatar(absent);check(absent.set_person_data(1,100,456));
    r=record();r.person_mode=0;r.person_data.reset();d=odh::inherit_neighbor(absent,1,r);
    check(!d.saved_data_present&&d.bank_words_after==101&&absent.find(1)->person_data[100]==456);
    check(absent.set_person_data(1,68,2));d=odh::inherit_neighbor(absent,1,r);check(d.ghost==2);
    r.person_mode=-1;d=odh::inherit_neighbor(absent,1,r);check(!d.saved_data_present);
    auto unchanged=absent.find(1)->person_data;auto words=absent.word_count();
    for(int malformed=0;malformed<5;++malformed){r=record();
        if(malformed==0)r.version=5;
        if(malformed==1)r.person_data.reset();
        if(malformed==2)r.person_mode=0;
        if(malformed==3)r.person_data->resize(88);
        if(malformed==4)r.version=10;
        rejects([&]{odh::inherit_neighbor(absent,1,r);});
        check(absent.find(1)->person_data==unchanged&&absent.word_count()==words);
    }
    r=record();r.guid=2;rejects([&]{odh::inherit_neighbor(absent,1,r);});
    check(absent.find(1)->person_data==unchanged);
    rejects([&]{odh::inherit_neighbor(absent,9,record());});
    odh::World small;avatar(small,68);unchanged=small.find(1)->person_data;
    rejects([&]{odh::inherit_neighbor(small,1,record());});check(small.find(1)->person_data==unchanged);
    odh::World object;check(object.create(1,odh::EntityKind::object));check(object.define_type(1,0x12345678));
    rejects([&]{odh::inherit_neighbor(object,1,record());});
    odh::World no_type;check(no_type.create(1,odh::EntityKind::avatar,{0,101,0,0}));
    rejects([&]{odh::inherit_neighbor(no_type,1,record());});
    odh::World budget({1,91,88});check(budget.create(1,odh::EntityKind::avatar,{0,69,0,16}));
    check(budget.define_type(1,0x12345678));unchanged=budget.find(1)->person_data;words=budget.word_count();
    rejects([&]{odh::inherit_neighbor(budget,1,record());});
    check(budget.find(1)->person_data==unchanged&&budget.word_count()==words);
    odh::World bank_budget({1,1000,80});check(bank_budget.create(1,odh::EntityKind::avatar,{0,69,0,0}));
    check(bank_budget.define_type(1,0x12345678));unchanged=bank_budget.find(1)->person_data;
    rejects([&]{odh::inherit_neighbor(bank_budget,1,record());});check(bank_budget.find(1)->person_data==unchanged);
    std::cout<<"avatar neighbor checks: "<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
