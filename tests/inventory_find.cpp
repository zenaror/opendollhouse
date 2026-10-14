#include "odh/inventory_find.hpp"
#include <iostream>

namespace {
unsigned checks=0;
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
odh::Instruction query(std::uint32_t guid=0x4e562938,std::uint8_t type=6,
    std::uint8_t flags=17,std::uint8_t flags2=13){
    return {51,254,255,{3,type,flags,flags2,static_cast<std::uint8_t>(guid),
        static_cast<std::uint8_t>(guid>>8),static_cast<std::uint8_t>(guid>>16),static_cast<std::uint8_t>(guid>>24)}};
}
void run(){
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,32,0})&&world.set_person_data(1,31,48),"caller avatar");
    require(world.create(2,odh::EntityKind::object)&&world.define_type(2,0x4e562938),"stack object known guid");
    require(world.create(3,odh::EntityKind::avatar,{0,32,0})&&world.set_person_data(3,31,-2),"temp4 avatar signed neighbor");
    odh::Ngbh source{};
    source.inventory_by_id[48]={{9,0x4e562938,44},{6,0x4e562938,3},{6,0x4e562938,99}};
    source.inventory_by_id[-2]={{6,0x4e562938,65535}};
    const auto original=source.inventory_by_id[48][1].count;
    std::array<std::int16_t,20> temps{};temps.fill(77);temps[4]=3;
    odh::VmFrame frame{odh::routine_key(4096),0,{1,2,2,0},{},{}};
    unsigned observed=0;
    auto primitive=odh::inventory_find_primitive(source,world,[&](const odh::InventoryFind& record){
        require(record.context.caller==frame.context.caller&&record.context.stack_object==frame.context.stack_object,
            "observer retains context");++observed;
    });
    auto dispatch=[&](odh::Instruction i){return primitive(frame,i,temps,{nullptr,1});};
    auto r=dispatch(query());
    require(r.disposition==odh::PrimitiveDisposition::goto_true&&r.temp_write&&r.temp_write->index==0&&r.temp_write->value==3,
        "first GUID and type match writes count");
    require(r.second_temp_write&&r.second_temp_write->index==1&&r.second_temp_write->value==1,"found index staged after count");
    require(temps[0]==77&&source.inventory_by_id[48][1].count==original,"query leaves source and temps unchanged before publication");
    r=dispatch(query(0));require(r.temp_write&&r.temp_write->value==3,"zero operand guid uses StackObject definition");
    r=dispatch(query(0x4e562938,0));require(r.temp_write&&r.temp_write->value==44&&r.second_temp_write->value==0,"type zero wildcard");
    r=dispatch(query(0x4e562938,9));require(r.temp_write&&r.temp_write->value==44,"exact alternate type");
    r=dispatch(query(0x4e562938,6,1));require(r.temp_write&&!r.second_temp_write,"count output without index flag");
    r=dispatch(query(0x4e562938,6,29,14));
    require(r.temp_write->index==3&&r.second_temp_write->index==2,"count and index selectors independent");
    r=dispatch(query(0x4e562938,6,17,45));require(r.disposition==odh::PrimitiveDisposition::goto_false&&r.temp_write->value==-1,
        "temp4 target count interpreted signed16 false with outputs");
    for(std::uint16_t count:{0,32767,32768,65535}){
        source.inventory_by_id[48][1].count=count;r=dispatch(query());
        require(r.temp_write&&r.second_temp_write&&(r.disposition==odh::PrimitiveDisposition::goto_true)==(count>0&&count<32768),
            "signed count boundary preserves outputs on false");
    }
    source.inventory_by_id[48][1].count=3;
    for(auto i:{query(88),query(0x4e562938,7)}){
        r=dispatch(i);require(r.disposition==odh::PrimitiveDisposition::goto_false&&!r.temp_write&&!r.second_temp_write,
            "no match false no outputs");
    }
    frame.context.caller=3;source.inventory_by_id.erase(-2);
    r=dispatch(query());require(r.disposition==odh::PrimitiveDisposition::goto_false&&!r.temp_write&&!r.second_temp_write,
        "known resource missing inventory false no writes");
    frame.context.caller=2;r=dispatch(query());require(r.disposition==odh::PrimitiveDisposition::unsupported,"object caller refused");
    frame.context.caller=1;temps[4]=0;r=dispatch(query(0x4e562938,6,17,45));
    require(r.disposition==odh::PrimitiveDisposition::unsupported,"missing temp4 entity refused");
    frame.context.stack_object=0;r=dispatch(query(0));require(r.disposition==odh::PrimitiveDisposition::unsupported,"missing GUID fallback refused");
    r=dispatch(query());require(r.disposition==odh::PrimitiveDisposition::goto_true,"literal GUID ignores missing StackObject");
    frame.context.stack_object=2;
    for(unsigned mode:{0U,1U,2U,4U,5U,6U,8U,255U}){
        auto i=query();i.operands[0]=static_cast<std::uint8_t>(mode);const auto before=observed;r=dispatch(i);
        require(r.disposition==odh::PrimitiveDisposition::unsupported&&observed==before&&!r.temp_write&&!r.second_temp_write,
            "unimplemented modes not fabricated success");
    }
    auto wrong=query();wrong.opcode=49;r=dispatch(wrong);require(r.disposition==odh::PrimitiveDisposition::unsupported,"notify49 distinct");
    // Real VM applies both synchronous outputs, including false found counts.
    for(auto flags2:{std::uint8_t{13},std::uint8_t{12}}){
        odh::Bhav code{0x8002,{query(0x4e562938,6,17,flags2)},{},odh::Bhav::Metadata{0,0,0,0}};
        odh::Vm vm({{odh::routine_key(4096),code}},{},{},primitive);
        vm.start(odh::routine_key(4096),frame.context);vm.temps().fill(77);vm.run(1);
        require(vm.status()==odh::VmStatus::returned_true,"real VM successful query branch");
        require(vm.temps()[0]==(flags2==12?1:3)&&vm.temps()[1]==(flags2==12?77:1),"real VM output ordering and alias second wins");
    }
    source.inventory_by_id[48][1].count=32768;
    odh::Bhav code{0x8002,{query()},{},odh::Bhav::Metadata{0,0,0,0}};
    odh::Vm vm({{odh::routine_key(4096),code}},{},{},primitive);
    vm.start(odh::routine_key(4096),frame.context);vm.run(1);
    require(vm.status()==odh::VmStatus::returned_false&&vm.temps()[0]==-32768&&vm.temps()[1]==1,"real VM found negative count false outputs");
    // Index outputs use the same signed low16 cast as the reference.
    source.inventory_by_id[48].assign(32769,{1,88,1});
    source.inventory_by_id[48][32768]={6,0x4e562938,2};
    r=dispatch(query());require(r.second_temp_write&&r.second_temp_write->value==-32768,
        "large bounded inventory index uses signed low16");
}
}
int main(){try{run();std::cout<<checks<<" inventory FindToken checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
