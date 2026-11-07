#include "odh/direction_to.hpp"
#include "odh/routing.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message) {
    ++checks;if(!value)throw std::runtime_error(message);
}
odh::VmStatus run(odh::Vm& vm) {
    vm.start(odh::routine_key(4096),{1,2,2});return vm.run(1);
}
odh::RoutineSet routine(std::array<std::uint8_t,8> operands) {
    return {{odh::routine_key(4096),odh::Bhav{0x8002,
        {{12,254,255,operands}},{},odh::Bhav::Metadata{0,4,4,0}}}};
}
}
int main() {
    try {
        odh::World world;
        require(world.create(1,odh::EntityKind::avatar,{4,88,1,16})&&
            world.create(2,odh::EntityKind::avatar,{4,88,1,16}),"create direction fixture entities");
        require(world.set_position(1,{32,32,1,0})&&world.set_position(2,{32,16,1,0}),
            "position direction fixture");
        auto vm_for=[&](std::array<std::uint8_t,8> operands){
            return std::make_unique<odh::Vm>(routine(operands),world.vm_memory(),
                odh::VmLimits{},odh::get_direction_to_primitive(world,world.vm_memory()));
        };
        auto vm=vm_for({0,0,8,0,0,0,0,0});
        require(run(*vm)==odh::VmStatus::returned_true&&vm->temps()[0]==0,
            "GetDirectionTo maps north to notch zero");
        for(const auto [position,expected]:{
            std::pair<odh::RoutePoint,std::int16_t>{{48,32},2},{{48,48},3},
            {{32,48},4},{{16,32},6},{{39,15},0},{{40,15},1}}) {
            require(world.set_position(2,{position.x,position.y,1,0}),"move direction target");
            vm=vm_for({0,0,8,0,0,0,0,0});
            require(run(*vm)==odh::VmStatus::returned_true&&vm->temps()[0]==expected,
                "GetDirectionTo rounds the source vector to the expected clockwise notch");
        }
        require(world.set_position(2,{32,32,1,0}),"restore target position");
        vm=vm_for({0,0,8,0,1,7,1,0});
        require(run(*vm)==odh::VmStatus::returned_true&&vm->temps()[0]==0,
            "GetDirectionTo supports an explicit literal source selector");
        require(world.set_position(1,{0,1393,1,0})&&world.set_position(2,{577,0,1,0}),"position fine angle boundary");
        vm=vm_for({0,0,8,0,0,0,0,0});
        require(run(*vm)==odh::VmStatus::returned_true&&vm->temps()[0]==1,
            "double source rounding distinguishes a vector the former Q16 approximation misclassified");
        require(world.set_position(1,{1,100,1,0})&&world.set_position(2,{0,0,1,0}),"position just west of north");
        vm=vm_for({0,0,8,0,0,0,0,0});
        require(run(*vm)==odh::VmStatus::returned_true&&vm->temps()[0]==8,
            "source Math.Round retains eight near north instead of reducing modulo eight");
        require(world.place_out_of_world(1)&&world.place_out_of_world(2),"put direction participants OOW");
        vm=vm_for({0,0,8,0,0,0,0,0});
        require(run(*vm)==odh::VmStatus::returned_true&&vm->temps()[0]==0,
            "two OOW source sentinels have zero-vector direction zero");
        require(world.set_position(2,{0,0,2,0}),"position target while source stays OOW");
        vm=vm_for({0,0,8,0,0,0,0,0});
        require(run(*vm)==odh::VmStatus::returned_true&&vm->temps()[0]==3,
            "OOW source sentinel participates in XY direction; floor does not alter direction");
        require(world.set_position(1,{32,32,1,0})&&world.set_position(2,{48,16,1,0}),"position northeast vector");
        {
            auto memory=world.vm_memory();unsigned writes=0;
            const auto writer=memory.write;memory.write=[&](const auto& f,auto scope,auto index,auto value){
                ++writes;return writer(f,scope,index,value);};
            memory.expression_write=[&](const auto&,auto,auto,auto)->std::optional<bool>{++writes;return true;};
            const auto before=world.find(1)->attributes;
            odh::VmFrame frame;frame.context={1,2,2};
            const auto staged=odh::get_direction_to_primitive(world,memory)(frame,{12,254,255,{0,0,0,0,0,0,0,0}},{},{});
            require(staged.disposition==odh::PrimitiveDisposition::goto_true&&staged.variable_write&&
                staged.variable_write->scope==0&&staged.variable_write->index==0&&staged.variable_write->value==1&&
                writes==0&&world.find(1)->attributes==before,
                "primitive only stages its World result; external setter runs exclusively during VM publication");
        }
        for(unsigned flags=0;flags<256;++flags){
            vm=vm_for({0,0,8,0,static_cast<std::uint8_t>(flags),7,2,0});
            require(run(*vm)==odh::VmStatus::returned_true&&vm->temps()[0]==((flags&1)?0:1),
                "only source flag bit zero selects explicit object; all other bits are ignored");
        }
        for(const auto scope:{0U,1U,3U,4U,14U,15U,18U,19U}){
            const auto index=static_cast<std::uint8_t>(scope==3||scope==4?7:2);
            if(scope==3||scope==4)require(world.declare_object_word(scope==3?1:2,index,0),"declare general result word");
            vm=vm_for({index,0,static_cast<std::uint8_t>(scope),0,0,255,255,255});
            require(run(*vm)==odh::VmStatus::returned_true,"source result reaches general World setter");
            odh::VmFrame frame;frame.context={1,2,2};
            require(world.read(frame,static_cast<std::uint8_t>(scope),index)==1,
                "World result setter publishes the source direction to the selected entity/bank");
        }
        // Observe frame outputs with a real second expression, not by mutating
        // a copied frame or inspecting a fabricated callback result.
        for(const auto scope:{9U,25U,40U,11U}){
            const auto output_scope=static_cast<std::uint8_t>(scope==40?25:scope==11?8:scope);
            auto program=routine({2,0,static_cast<std::uint8_t>(scope),0,0,0,0,0});
            auto& instructions=program.begin()->second.instructions;instructions.front().on_true=1;
            instructions.push_back({2,254,255,{7,0,2,0,0,5,8,output_scope}});
            odh::Vm frame_vm(program,world.vm_memory(),{},odh::get_direction_to_primitive(world,world.vm_memory()));
            frame_vm.start(odh::routine_key(4096),{1,2,2});frame_vm.temps()[2]=2;
            require(frame_vm.run(2)==odh::VmStatus::returned_true&&frame_vm.temps()[7]==1,
                "parameter/local/indirect frame or Temp destination commits through real VM output");
        }
        for(const auto scope:{7U,21U,26U,35U,38U,43U,44U,45U,48U,51U,52U,53U,59U}){
            auto memory=world.vm_memory();unsigned writes=0;
            memory.write=[&](const auto&,auto,auto,auto){++writes;return true;};
            memory.expression_write=[&](const auto&,auto,auto,auto)->std::optional<bool>{++writes;return true;};
            odh::Vm ignored(routine({255,255,static_cast<std::uint8_t>(scope),0,0,0,0,0}),memory,{},
                odh::get_direction_to_primitive(world,memory));
            require(run(ignored)==odh::VmStatus::returned_true&&writes==0,
                "source ignores known read-only SetVariable false or explicit no-op without calling host writer");
        }
        for(const auto scope:{2U,5U,16U,20U,23U,24U,32U,33U,34U,39U,42U,46U,47U,54U,55U,256U,65535U}){
            auto memory=world.vm_memory();unsigned writes=0;
            memory.write=[&](const auto&,auto,auto,auto){++writes;return true;};
            memory.expression_write=[&](const auto&,auto,auto,auto)->std::optional<bool>{++writes;return true;};
            odh::Vm unsupported(routine({0,0,static_cast<std::uint8_t>(scope),static_cast<std::uint8_t>(scope>>8),0,0,0,0}),memory,{},
                odh::get_direction_to_primitive(world,memory));
            require(run(unsupported)==odh::VmStatus::fault&&writes==0,
                "unsupported or high ushort result scope cannot truncate into a permissive host setter");
        }
        {
            auto memory=world.vm_memory();unsigned writes=0,expression_writes=0;
            memory.write=[&](const auto&,auto,auto,auto){++writes;return false;};
            memory.expression_write=[&](const auto&,auto,auto,auto)->std::optional<bool>{++expression_writes;return false;};
            const auto before=world.find(1)->attributes;
            odh::Vm check(routine({0,0,0,0,0,0,0,0}),memory,{},odh::get_direction_to_primitive(world,memory));
            const auto result=check.evaluate_check_at_safe_point(odh::routine_key(4096),{1,2,2},{},1,
                odh::get_direction_to_primitive(world,memory));
            require(result.started&&result.status==odh::VmStatus::fault&&expression_writes==1&&writes==0&&world.find(1)->attributes==before&&
                check.status()==odh::VmStatus::ready,
                "synchronous check result uses its rejecting effective memory without writing captured live World");
        }
        {
            auto program=routine({1,0,22,0,0,0,0,0});
            odh::Vm indirect(program,world.vm_memory(),{},odh::get_direction_to_primitive(world,world.vm_memory()));
            indirect.start(odh::routine_key(4096),{1,2,2},{0,3,0,0});
            require(indirect.run(1)==odh::VmStatus::returned_true&&world.find(2)->attributes[3]==1,
                "parameter-selected attribute destination resolves through the real frame");
            program=routine({1,0,31,0,0,0,0,0});
            odh::Vm person(program,world.vm_memory(),{},odh::get_direction_to_primitive(world,world.vm_memory()));
            person.start(odh::routine_key(4096),{1,2,2});person.temps()[1]=3;
            require(person.run(1)==odh::VmStatus::returned_true&&world.find(2)->person_data[3]==1,
                "Temp-selected person-data destination resolves through the real register bank");
        }
        {
            odh::VmFrame stale;stale.context={1,2,2};require(world.bind_context_live(stale.context),"bind exact old target identity");
            require(world.remove(2)&&world.create(2,odh::EntityKind::avatar,{4,88,1,16})&&world.set_position(2,{48,16,1,0}),
                "reuse target number with a new generation");
            const auto outcome=odh::get_direction_to_primitive(world,world.vm_memory())(
                stale,{12,254,255,{0,0,8,0,0,0,0,0}},{},{});
            require(outcome.disposition==odh::PrimitiveDisposition::unsupported&&!outcome.variable_write,
                "generation-bound StackObject cannot borrow a replacement target position");
        }
        const auto direction=odh::get_direction_to_primitive(world,world.vm_memory());
        odh::VmFrame source{odh::routine_key(4096),0,{1,2,2},{},{0,1}};
        odh::Ts1Temps selector{};selector[0]=1;
        const auto local_direction=direction(source,{12,254,255,{0,0,8,0,1,40,0,0}},selector,{});
        require(local_direction.disposition==odh::PrimitiveDisposition::goto_true&&local_direction.variable_write,
            "GetDirectionTo resolves LocalByTemp from the frame instead of the memory backend");
        std::cout<<checks<<" direction-to checks passed\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
