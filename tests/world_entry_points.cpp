#include "odh/world.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
odh::VmFrame frame(std::int16_t stack=2,odh::CodeOwner owner=77) {
    return {odh::routine_key(4096),0,{1,3,stack,owner},{},{}};
}
bool same(const std::vector<odh::EntryPoint>& a,const std::vector<odh::EntryPoint>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)
        if(a[i].condition!=b[i].condition || a[i].action!=b[i].action)return false;
    return true;
}
odh::Instruction expr(unsigned ls,std::int16_t l,unsigned rs,std::int16_t r,unsigned yes=254) {
    const auto lhs=static_cast<std::uint16_t>(l),rhs=static_cast<std::uint16_t>(r);
    return {2,static_cast<std::uint8_t>(yes),255,{static_cast<std::uint8_t>(lhs),
        static_cast<std::uint8_t>(lhs>>8),static_cast<std::uint8_t>(rhs),
        static_cast<std::uint8_t>(rhs>>8),0,5,static_cast<std::uint8_t>(ls),static_cast<std::uint8_t>(rs)}};
}
odh::Bhav routine(std::initializer_list<odh::Instruction> code) {
    return {0x8002,code,{},odh::Bhav::Metadata{0,4,0,0}};
}
}
int main() {
    try {
        odh::World w;
        for(const auto id:{1,2,3,4,5,6,77}) {
            require(w.create(static_cast<std::int16_t>(id),id==1||id==4?odh::EntityKind::avatar:odh::EntityKind::object,
                    {1,0,1}),"entry fixture entity");
            if(id!=5)require(w.define_type(static_cast<std::int16_t>(id),0x1000U+id),"explicit entity definition");
        }
        require(!w.define_entry_points(5,{{1,2}}) && !w.define_entry_points(5,{}) &&
                !w.find(5)->entry_points,"untyped entities reject even empty bindings");
        for(const auto id:{0,-1,99})require(!w.define_entry_points(static_cast<std::int16_t>(id),{}),"missing entity rejects binding");
        std::vector<odh::EntryPoint> incoming{{91,0},{92,65535},{93,32768},{94,32767}};
        const auto expected=incoming;
        const auto* identity=w.find(2);
        const auto before=*identity;
        const auto words=w.word_count();
        require(w.define_entry_points(2,incoming),"typed object binds condition and action pairs");
        require(w.find(2)==identity && identity->generation==before.generation &&
                identity->definition->guid==before.definition->guid && identity->attributes==before.attributes &&
                identity->slots==before.slots && identity->person_data==before.person_data &&
                identity->object_words==before.object_words && identity->definition_words==before.definition_words &&
                identity->motives==before.motives && w.word_count()==words+8,
                "binding preserves entity identity and unrelated state and charges pairs");
        require(same(*identity->entry_points,expected),"stored conditions retained");
        incoming[0]={0,12};incoming.clear();
        require(same(*identity->entry_points,expected),"binding owns independent input copy");
        require(w.define_entry_points(2,expected) && w.define_entry_points(2,*identity->entry_points) &&
                w.word_count()==words+8,"identical and self alias binding idempotent");
        auto changed=expected;changed[0].condition=0;
        require(!w.define_entry_points(2,changed),"condition changes reject immutable rebinding");
        changed=expected;changed[0].action=12;
        require(!w.define_entry_points(2,changed) && !w.define_entry_points(2,{}) &&
                same(*identity->entry_points,expected) && w.word_count()==words+8,"action and length changes leave bound table unchanged");
        require(w.read(frame(),35,0)==0 && w.read(frame(),35,1)==-1 &&
                w.read(frame(),35,2)==-32768 && w.read(frame(),35,3)==32767,
                "present zero and high unsigned function IDs preserve signed low bits");
        require(w.define_entry_points(1,{{0,111}}) && w.define_entry_points(3,{{0,333}}) &&
                w.define_entry_points(77,{{0,777}}) && w.define_entry_points(4,{{65535,444}}),
                "caller callee owner and avatar tables differ");
        for(const auto owner:{0U,1U,3U,77U,99U})require(w.read(frame(2,owner),35,0)==0,"code owner never selects entry point table");
        require(w.read(frame(4),35,0)==444 && w.read(frame(3),35,0)==333 && w.read(frame(1),35,0)==111,
                "stack changes select exact object or avatar");
        require(!w.read(frame(6),35,0) && !w.read(frame(5),35,0),"typed unbound and untyped entities have unsupported scope35");
        require(w.define_entry_points(6,{}) && w.find(6)->entry_points && w.find(6)->entry_points->empty() &&
                !w.read(frame(6),35,0) && w.define_entry_points(6,{}) && !w.define_entry_points(6,{{0,1}}),
                "empty table is bound immutable and distinct from absent");
        for(const auto id:{0,-1,99})require(!w.read(frame(static_cast<std::int16_t>(id)),35,0),"missing stack has no caller callee owner fallback");
        for(const auto index:{-32768,-1,4,255,256,32767})require(!w.read(frame(),35,static_cast<std::int16_t>(index)),"entry read bounds unsupported");
        const auto bound_words=w.word_count();
        require(w.declare_object_word(2,0,51) && w.declare_definition_word(2,0,52) && w.write(frame(),1,0,53),"other banks use same numeric index");
        for(const auto index:{-1,0,1,4,255})require(!w.write(frame(),35,static_cast<std::int16_t>(index),123),"all scope35 writes unsupported");
        require(w.read(frame(),35,0)==0 && w.read(frame(),4,0)==51 && w.read(frame(),21,0)==52 &&
                w.read(frame(),1,0)==53 && same(*identity->entry_points,expected) && w.word_count()==bound_words+2,
                "scope35 read write isolation leaves other banks and table intact");

        odh::World capacity({4,1024,512});
        require(capacity.create(1,odh::EntityKind::object) && capacity.define_type(1,1) &&
                capacity.create(2,odh::EntityKind::avatar) && capacity.define_type(2,2),"capacity fixtures");
        std::vector<odh::EntryPoint> entries(256,{65535,17});entries.back().action=65535;
        require(capacity.define_entry_points(1,entries) && capacity.word_count()==512 &&
                capacity.read(frame(1),35,255)==-1 && !capacity.read(frame(1),35,256),"256 entry bank boundary accepted");
        entries.push_back({0,99});
        require(!capacity.define_entry_points(2,entries) && !capacity.find(2)->entry_points &&
                capacity.word_count()==512,"257 entries rejected atomically");
        entries.pop_back();
        require(capacity.define_entry_points(2,entries) && capacity.word_count()==1024,"exact aggregate boundary accepts two banks");
        require(capacity.define_entry_points(1,entries) && !capacity.declare_object_word(2,0,1),"full aggregate still allows identical binding");
        require(capacity.remove(1) && capacity.word_count()==512 && capacity.create(1,odh::EntityKind::object) &&
                !capacity.find(1)->entry_points && capacity.define_type(1,3) && capacity.define_entry_points(1,entries) &&
                capacity.word_count()==1024,"removal reclaims pairs and reused ID has fresh binding");
        require(capacity.remove(1) && capacity.remove(2) && capacity.word_count()==0,"remove releases every stored pair");
        odh::World bank_limit({2,100,3});
        require(bank_limit.create(1,odh::EntityKind::object) && bank_limit.define_type(1,1),"odd bank budget fixture");
        require(!bank_limit.define_entry_points(1,{{0,1},{0,2}}) && !bank_limit.find(1)->entry_points &&
                bank_limit.word_count()==0 && bank_limit.define_entry_points(1,{{0,1}}) &&
                bank_limit.word_count()==2,"odd bank budget counts both condition and action without partial publish");
        odh::World shared({3,5,512});
        require(shared.create(1,odh::EntityKind::object,{1,0,0}) && shared.define_type(1,1) &&
                shared.create(2,odh::EntityKind::object) && shared.define_type(2,2),"shared word budget fixture");
        require(!shared.define_entry_points(1,{{0,1},{0,2},{0,3}}) && !shared.find(1)->entry_points &&
                shared.word_count()==1 && shared.define_entry_points(1,{{0,1},{0,2}}) && shared.word_count()==5,
                "existing banks share exact entry point budget and rejection is atomic");
        require(!shared.define_entry_points(2,{{0,1}}) && !shared.find(2)->entry_points &&
                shared.define_entry_points(2,{}) && shared.word_count()==5,"full budget rejects pairs but permits explicit empty table");
        require(shared.remove(1) && shared.word_count()==0 && shared.create(1,odh::EntityKind::object) &&
                shared.define_type(1,3) && shared.define_entry_points(1,{{0,1},{0,2}}),"freed shared words reusable");
        odh::World huge({1,std::numeric_limits<std::size_t>::max(),std::numeric_limits<std::size_t>::max()});
        require(huge.create(1,odh::EntityKind::object) && huge.define_type(1,1) &&
                huge.define_entry_points(1,entries) && huge.word_count()==512,"maximum limits cannot overflow validation");
        odh::World zero({1,0,0});
        require(zero.create(1,odh::EntityKind::object) && zero.define_type(1,1) &&
                !zero.define_entry_points(1,{{0,1}}) && zero.define_entry_points(1,{}) &&
                zero.word_count()==0,"zero budgets retain explicit empty table semantics");

        const auto key=odh::routine_key(4096);
        odh::RoutineSet code{{key,routine({expr(8,0,35,1,1),expr(10,0,7,4,2),expr(8,1,35,0)})}};
        odh::Vm vm({},w.vm_memory(),{},{},{{77,code}});
        vm.start(key,{1,3,2,77});
        require(vm.run(3)==odh::VmStatus::returned_true && vm.temps()[0]==-1 && vm.temps()[1]==444,
                "real VM expression reads scope35 under separate code owner and changed stack object");
        odh::Vm denied({},w.vm_memory(),{},{},{{77,{{key,routine({expr(35,0,7,9)})}}}});
        denied.start(key,{1,3,2,77});
        require(denied.run(1)==odh::VmStatus::fault && w.read(frame(),35,0)==0,
                "real VM assignment to scope35 faults without mutating table");
        odh::Vm absent({},w.vm_memory(),{},{},{{77,{{key,routine({expr(8,0,35,0)})}}}});
        absent.start(key,{1,3,99,77});
        require(absent.run(1)==odh::VmStatus::fault,"real VM missing stack read remains unsupported");
        std::cout<<checks<<" world entry point checks passed (synthetic fixtures only)\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"World entry point test failed: "<<e.what()<<'\n';return 1;}
}
