#include "odh/object_type.hpp"
#include <iostream>
#include <sstream>
namespace {
int checks=0;
void require(bool b,const char* text){++checks;if(!b)throw std::runtime_error(text);}
odh::Instruction instruction(std::uint32_t guid,std::uint8_t scope=10,std::int16_t index=0){
    const auto bits=static_cast<std::uint16_t>(index);
    return {32,254,255,{static_cast<std::uint8_t>(guid),static_cast<std::uint8_t>(guid>>8),static_cast<std::uint8_t>(guid>>16),static_cast<std::uint8_t>(guid>>24),
        static_cast<std::uint8_t>(bits),static_cast<std::uint8_t>(bits>>8),scope,99}}; // Tail ignored by reference.
}
}
int main(){try{
    odh::World world;world.create(1,odh::EntityKind::avatar,{1,88,0});world.create(2,odh::EntityKind::object,{1,0,1});
    require(!world.define_type(3,1)&&world.define_type(2,0x26BFBB29)&&world.define_type(2,0x26BFBB29),"identity requires entity and binding is idempotent");
    require(!world.define_type(2,0x59C389BC)&&!world.define_type(2,0x26BFBB29,5),"different identity/master cannot overwrite live binding");
    auto callback=odh::object_type_primitive(world);const auto key=odh::routine_key(4096);
    auto run=[&](odh::Instruction i,std::vector<std::int16_t> args=std::vector<std::int16_t>{2}){
        odh::Vm vm({{key,{0x8002,{i},{},odh::Bhav::Metadata{0,4,2,0}}}},world.vm_memory(),{},callback);
        vm.start(key,{1,2,2},args);vm.temps()[0]=2;return vm.run(1);
    };
    require(run(instruction(0x26BFBB29))==odh::VmStatus::returned_true,"direct GUID match");
    require(run(instruction(0x59C389BC))==odh::VmStatus::returned_false,"office-chair GUID differs from cheap chair");
    for(const auto scope:{7,8,9,10})require(run(instruction(0x26BFBB29,static_cast<std::uint8_t>(scope),scope==7?2:0))==odh::VmStatus::returned_true,"supported source selects bound object");
    for(const auto scope:{8,9,25})require(run(instruction(0x26BFBB29,static_cast<std::uint8_t>(scope),-1))==odh::VmStatus::fault,"negative frame/temp index faults");
    require(run(instruction(0x26BFBB29,7,3))==odh::VmStatus::fault,"missing entity is error rather than false");
    require(run(instruction(0x26BFBB29,7,1))==odh::VmStatus::fault,"missing definition is error rather than false");
    require(run(instruction(0x26BFBB29,99))==odh::VmStatus::fault,"unsupported source is explicit error");
    require(world.create(3,odh::EntityKind::object)&&world.define_type(3,0xABCDEF01,0x11223344),"explicit master identity");
    require(run(instruction(0x11223344,7,3))==odh::VmStatus::returned_true&&run(instruction(0xABCDEF01,7,3))==odh::VmStatus::returned_true,"part/master both match");
    {
        odh::World retained;
        require(retained.create(1,odh::EntityKind::avatar)&&retained.create(2,odh::EntityKind::object)&&
            retained.define_type(2,0xABCDEF01,0x11223344),"retained identity setup");
        odh::VmFrame frame{};frame.context={1,2,2};
        require(retained.bind_context_live(frame.context),"bind original object pointer");
        const auto old=odh::vm_context_reference(frame.context,odh::VmEntityRole::stack_object);
        require(retained.remove_retaining_body(old)&&!retained.find(2),"retire while preserving original pointer body");
        const auto type=odh::object_type_primitive(retained);
        const auto inspect=[&](std::uint32_t guid,std::uint8_t scope=10){
            return type(frame,instruction(guid,scope,scope==7?2:0),{},{}).disposition;
        };
        require(inspect(0xABCDEF01)==odh::PrimitiveDisposition::goto_true&&
            inspect(0x11223344)==odh::PrimitiveDisposition::goto_true,
            "retained stack pointer preserves own and master type");
        require(inspect(0x26BFBB29)==odh::PrimitiveDisposition::goto_false,
            "known retained body still performs an actual type comparison");
        require(inspect(0xABCDEF01,7)==odh::PrimitiveDisposition::source_error,
            "numeric lookup does not discover a retired body");
        require(retained.create(2,odh::EntityKind::object)&&retained.define_type(2,0x26BFBB29),"reuse numeric ID with another identity");
        require(inspect(0xABCDEF01)==odh::PrimitiveDisposition::goto_true&&
            inspect(0x26BFBB29)==odh::PrimitiveDisposition::goto_false&&
            inspect(0x26BFBB29,7)==odh::PrimitiveDisposition::goto_true,
            "stack pointer keeps original generation while numeric lookup sees replacement");
        frame.context.stack_object_generation=UINT64_MAX;
        require(inspect(0x26BFBB29)==odh::PrimitiveDisposition::unsupported,
            "unresolved positive stack reference is an ownership failure, never an authored negative check");
        frame.context.entity_references_bound=false;
        require(inspect(0x26BFBB29)==odh::PrimitiveDisposition::unsupported,
            "invalid native context cannot become an authored ERROR");
        frame.context.entity_references_bound=true;
        frame.context.stack_object_generation=0;
        require(inspect(0x26BFBB29)==odh::PrimitiveDisposition::source_error,
            "known null stack pointer never rebinds to a reused raw ID");
    }
    world.remove(2);world.create(2,odh::EntityKind::object);
    require(run(instruction(0x26BFBB29))==odh::VmStatus::fault,"reused ID does not inherit old GUID");
    std::string bytes(42,'\0');bytes[0]=static_cast<char>(138);bytes[20]=0x34;bytes[21]=0x12;bytes[22]=static_cast<char>(255);bytes[23]=static_cast<char>(255);
    std::istringstream input(bytes);const auto binding=odh::read_objd_binding(odh::View(input,bytes.size()));
    require(binding.master_id==0x1234&&binding.sub_index==-1,"OBJD prefix master ID and signed subindex offsets");
    std::cout<<checks<<" object type checks passed (synthetic identities only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Object type test failed: "<<e.what()<<'\n';return 1;}}
