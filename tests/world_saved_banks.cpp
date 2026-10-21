#include "odh/world.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace { int allocation_countdown=-1; }
void* operator new(std::size_t size) {
    if (allocation_countdown==0) { allocation_countdown=-1; throw std::bad_alloc(); }
    if (allocation_countdown>0) --allocation_countdown;
    if (auto* p=std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }

namespace {
int checks=0;
void require(bool value,const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::VmFrame frame(std::int16_t id=1) {
    return {odh::routine_key(4096),0,{id,id,id},{},{}};
}
}
int main() {
    try {
        std::vector<std::int16_t> attributes{7,-8,9},person{11,-12},motives{-32768,32767};
        std::array<std::int16_t,68> object_data{};
        std::array<std::int16_t,5> extras{-5,6,-7,8,-9};
        for (std::size_t i=0;i<object_data.size();++i) object_data[i]=static_cast<std::int16_t>(100+i);
        object_data[1]=7;
        object_data[2]=0;object_data[3]=-1;
        const auto original=object_data;
        const auto original_extras=extras;
        const auto import=[&](odh::World& w,std::int16_t id=1,odh::EntityKind kind=odh::EntityKind::avatar,
                              std::size_t slots=1) {
            return w.create_saved_entity(id,kind,0x1234,attributes,person,motives,object_data,extras,slots,0x5678);
        };
        odh::World w({3,145,68});
        require(import(w),"exact combined budget accepts raw, runtime and variable banks");
        const auto* e=w.find(1);
        require(w.word_count()==145 && e->generation==1 && e->definition->guid==0x1234 &&
                e->definition->master_guid==0x5678,"accounting, generation and definition published together");
        require(e->saved_object_banks->object_data==original && e->saved_object_banks->extras==original_extras &&
                e->object_words.size()==64,"raw object snapshot and all ordinary runtime words imported");
        require(e->attributes==attributes && e->person_data==person && e->motives==motives &&
                e->slots==std::vector<std::int16_t>{0} && !e->position && !e->attachment,
                "variable banks are exact and slots await attachment");
        require(w.read(frame(),14,0)==-32768 && w.read(frame(),14,1)==32767,
                "imported motives are not clamped");
        attributes[0]=99;person[0]=99;motives[0]=99;object_data[0]=99;extras[0]=99;
        require(e->attributes[0]==7 && e->person_data[0]==11 && e->motives[0]==-32768 &&
                e->saved_object_banks->object_data==original && e->saved_object_banks->extras==original_extras,
                "input mutation does not change owned banks");
        require(w.write(frame(),3,0,-30) && w.write(frame(),0,0,-31) && w.set_person_data(1,0,-32) &&
                w.set_motive(1,0,120) && w.read(frame(),14,0)==100 &&
                e->saved_object_banks->object_data==original && e->object_words.at(0)==-30,
                "runtime writes preserve original snapshot and retain normal setter semantics");
        require(w.read(frame(),3,2)==0 && w.read(frame(),3,3)==-1 && w.read(frame(),3,11)==1 &&
                w.read(frame(),3,67)==1 && w.read(frame(),3,1)==7,
                "derived fields preserve uncontained baseline and ordinary direction");
        require(w.set_position(1,{16,16,1,7}) &&
                    std::abs(e->radian_direction-7.0f*0.7853981633974483f)<0.00001f &&
                    w.set_radian_direction(1,-0.3f) && e->position->direction==0 &&
                    e->saved_object_banks->object_data==original,
                "restored avatar derives radians from its saved notch and runtime turns preserve raw save data");
        for (const auto index : {2,3,11,67})
            require(!e->object_words.count(static_cast<std::int16_t>(index)),"derived words excluded from runtime map");
        require(!import(w) && w.word_count()==145 && e->generation==1,"duplicate import leaves existing entity unchanged");
        require(w.remove(1) && w.word_count()==0 && import(w) && w.find(1)->generation==2 &&
                w.remove(1) && w.word_count()==0,"removal recovers both snapshot and runtime budgets");

        odh::World short_budget({3,144,68}), short_bank({3,1000,67}), no_entities({0,1000,68});
        require(!import(short_budget) && !import(short_bank) && !import(no_entities) &&
                !short_budget.find(1) && short_budget.word_count()==0,"combined, fixed bank and entity limits reject atomically");
        odh::World invalid;
        require(!import(invalid,0) && !import(invalid,-1) &&
                !import(invalid,1,static_cast<odh::EntityKind>(99)) &&
                !import(invalid,1,odh::EntityKind::object),"invalid IDs, kind and object avatar banks rejected");
        require(!import(invalid,1,odh::EntityKind::avatar,32768),"slot signed index range checked before allocation");
        object_data[1]=-1;require(!import(invalid),"negative direction rejected");
        object_data[1]=8;require(!import(invalid),"direction beyond eight notches rejected");
        object_data[1]=7;
        motives.resize(17);
        require(!import(invalid),"avatar motive bank limited to sixteen");
        motives.resize(2);
        require(!invalid.create_saved_entity(1,odh::EntityKind::object,0,{}, {}, {},object_data,extras,0) &&
                !invalid.create_saved_entity(1,odh::EntityKind::object,1,{}, {}, {},object_data,extras,0,0),
                "zero GUID or supplied zero master identity rejected");
        std::vector<std::int16_t> oversized(32769);
        odh::World large({4,std::numeric_limits<std::size_t>::max(),std::numeric_limits<std::size_t>::max()});
        require(!large.create_saved_entity(1,odh::EntityKind::avatar,1,oversized,{}, {},object_data,extras,0) &&
                !large.create_saved_entity(1,odh::EntityKind::avatar,1,{},oversized,{},object_data,extras,0),
                "attribute and person signed index ranges rejected");
        require(invalid.word_count()==0 && !invalid.find(1),"validation failures never publish state");
        allocation_countdown=0;
        const auto rejected=import(invalid,0);
        const auto untouched=allocation_countdown;
        allocation_countdown=-1;
        require(!rejected && untouched==0,"validation precedes all allocation");
        require(invalid.create_saved_entity(1,odh::EntityKind::object,1,{}, {}, {},object_data,extras,0) &&
                invalid.word_count()==137 && invalid.find(1)->person_data.empty() &&
                invalid.find(1)->motives.empty(),"objects import exact fixed banks without avatar state");

        odh::World failures;
        require(failures.create(2,odh::EntityKind::object),"allocation injection retains an existing entity");
        const auto* retained=failures.find(2);
        bool completed=false;
        for (int allocation=0;allocation<100;++allocation) {
            allocation_countdown=allocation;
            bool threw=false;
            try { completed=import(failures); } catch(const std::bad_alloc&) { threw=true; }
            allocation_countdown=-1;
            if (completed) break;
            require(threw && !failures.find(1) && failures.word_count()==0 &&
                    failures.find(2)==retained && retained->generation==1,
                    "every bank, map and publication allocation failure leaves World unchanged");
        }
        require(completed && failures.find(1)->generation==2 && failures.word_count()==145,
                "failed allocations never consume entity generations");
        require(failures.remove(1) && failures.word_count()==0,"successful import releases full count");
        odh::World containment;
        require(containment.create(2,odh::EntityKind::object,{0,0,1}) &&
                containment.read(frame(2),3,3)==-1,"legacy uncontained slot contract preserved");
        object_data[2]=0;object_data[3]=-1;
        require(import(containment) && containment.read(frame(),3,3)==-1 &&
                containment.attach(2,0,1) && containment.read(frame(),3,3)==0 &&
                containment.detach(2,0) && containment.read(frame(),3,3)==-1,
                "imported uncontained slot follows attach and detach");
        require(containment.remove(1),"remove detached imported entity");
        object_data[2]=0;object_data[3]=0;
        require(import(containment) && containment.read(frame(),3,3)==0 &&
                containment.attach(2,0,1) && containment.detach(2,0) &&
                containment.read(frame(),3,3)==-1 && containment.remove(1),
                "saved zero uncontained baseline preserved until explicit detachment");
        object_data[2]=0;object_data[3]=19;
        require(import(containment) && containment.read(frame(),3,3)==19 &&
                containment.attach(2,0,1) && containment.detach(2,0) &&
                containment.read(frame(),3,3)==-1 &&
                containment.find(1)->saved_object_banks->object_data[3]==19,
                "saved uncontained baseline survives import but detachment clears it");
        require(containment.remove(1),"remove entity with cleared baseline");
        object_data[2]=2;object_data[3]=19;
        require(import(containment) && containment.read(frame(),3,3)==-1 &&
                containment.attach(2,0,1) && containment.read(frame(),3,3)==0 &&
                containment.detach(2,0) && containment.read(frame(),3,3)==-1,
                "saved contained slot never leaks into unattached state");
        std::cout<<checks<<" saved World bank checks passed (synthetic fixtures only)\n";
        return 0;
    } catch(const std::exception& e) {
        allocation_countdown=-1;
        std::cerr<<"Saved World bank test failed: "<<e.what()<<'\n';return 1;
    }
}
