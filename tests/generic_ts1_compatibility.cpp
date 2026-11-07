// SPDX-License-Identifier: MPL-2.0
#include "odh/generic_ts1_compatibility.hpp"
#include "odh/random.hpp"
#include "odh/runtime_globals.hpp"
#include "odh/world.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
// Independent finite expectation. Do not derive this matrix from the helper.
const std::set<unsigned> expected{0,3,7,8,9,12,13,16,22,23,25,34,35,36,37,38,39,40,41};
void stateful_radio_checks(){
    const auto handler=odh::generic_ts1_compatibility_primitive();
    for(const auto value:{INT16_MIN,-1,0,1,INT16_MAX})for(unsigned padding=0;padding<3;++padding)
        for(const bool check:{false,true})for(const bool reject:{false,true}){
            odh::RuntimeGlobals globals(std::vector<std::int16_t>(32),odh::Ts1GlobalActivation::saved_new,7);
            require(globals.write(31,123),"seed actual radio global");
            const auto before=globals.snapshot();
            unsigned writes=0,expression_writes=0;
            odh::VmMemory memory;
            memory.read=[&](const auto&,auto scope,auto index){return scope==6?globals.read(index):std::nullopt;};
            memory.expression_write=[&](const auto&,auto,auto,auto)->std::optional<bool>{++expression_writes;return reject?std::optional<bool>{false}:std::nullopt;};
            memory.write=[&](const auto&,auto scope,auto index,auto output){++writes;return scope==6&&globals.write(index,output);};
            odh::Instruction instruction{1,254,255,{14,0,0,0,0,0,0,0}};
            for(unsigned i=1;i<8;++i)instruction.operands[i]=static_cast<std::uint8_t>(padding==0?0:padding==1?255:i*31);
            odh::Ts1Temps temps{};temps[0]=static_cast<std::int16_t>(value);
            const auto staged=handler({},instruction,temps,{});
            require(staged.disposition==odh::PrimitiveDisposition::goto_true&&staged.variable_write&&
                staged.variable_write->scope==6&&staged.variable_write->index==31&&staged.variable_write->value==value&&
                !staged.write&&!staged.call&&!staged.temp_write&&!staged.second_temp_write&&!staged.stack_object_write&&!staged.call_commit&&
                writes==0&&expression_writes==0&&globals.snapshot().words==before.words,
                "radio stages exact signed Global31 output without publishing or unrelated effects");
            odh::Vm vm({{odh::routine_key(4096),{0x8002,{instruction},{},odh::Bhav::Metadata{0,0,0,0}}}},memory,{},handler);
            vm.temps()=temps;
            auto status=odh::VmStatus::ready;
            if(check){
                const auto parent=vm.snapshot();
                const auto result=vm.evaluate_check_at_safe_point(odh::routine_key(4096),{}, {},1,handler);
                require(result.started&&vm.status()==odh::VmStatus::ready&&vm.temps()==temps&&vm.run_generation()==parent.run_generation,
                    "radio check preserves parent continuation");
                status=result.status;
            }else{
                require(vm.start(odh::routine_key(4096))==odh::VmStatus::running,"start radio VM");
                vm.temps()=temps;
                status=vm.run(1);
            }
            auto expected=before; if(!reject)expected.words[31]=static_cast<std::int16_t>(value);
            require(status==(reject?odh::VmStatus::fault:odh::VmStatus::returned_true)&&
                expression_writes==1&&writes==(reject?0U:1U)&&globals.snapshot().words==expected.words,
                "normal/check radio uses effective setter exactly once; rejection cannot fallback or claim success");
            odh::RuntimeGlobals reopened(std::vector<std::int16_t>(32),odh::Ts1GlobalActivation::saved_new,1);
            require(reopened.restore(globals.snapshot())&&reopened.read(31)==expected.words[31]&&
                reopened.snapshot().words==expected.words,
                "radio output survives real global bank snapshot and fresh restore without changing other words");
        }
}

}
int main(){try{
    odh::World world;require(world.create(1,odh::EntityKind::object,{2,0,1})&&
        world.create(2,odh::EntityKind::object)&&world.define_type(1,0x1234)&&world.define_type(2,0x5678)&&
        world.set_position(1,{24,40,1,6})&&world.set_position(2,{40,40,1,2})&&
        world.declare_object_word(1,8,17),"create meaningful owned World state");
    const auto original_world=odh::encode_world_snapshot(world.snapshot());
    odh::RandomState random(0x123456789abcdef0ULL);const auto original_seed=random.seed();
    const auto handler=odh::generic_ts1_compatibility_primitive();unsigned cases=0;
    for(unsigned mode=0;mode<256;++mode){
        const bool admitted=expected.count(mode)!=0;
        if(mode==14){
            require(odh::generic_ts1_compatibility_mode(14)&&odh::generic_ts1_compatibility_effect(14)==odh::GenericTs1CompatibilityEffect::global_state,"mode14 is supported stateful, never no-effect");
            continue;
        }
        require(odh::generic_ts1_compatibility_mode(static_cast<std::uint8_t>(mode))==admitted,
            "finite mode classification differs at "+std::to_string(mode));
        for(unsigned pattern=0;pattern<3;++pattern)for(const bool check:{false,true}){
            odh::Instruction instruction{1,254,255,{}};instruction.operands[0]=static_cast<std::uint8_t>(mode);
            for(unsigned n=1;n<8;++n)instruction.operands[n]=static_cast<std::uint8_t>(pattern==0?0:pattern==1?255:17*n+mode);
            unsigned reads=0,writes=0,dispatches=0;auto memory=world.vm_memory();
            const auto read=memory.read;const auto write=memory.write;
            memory.read=[&](const auto& f,auto scope,auto index){++reads;return read(f,scope,index);};
            memory.write=[&](const auto& f,auto scope,auto index,auto value){++writes;return write(f,scope,index,value);};
            const odh::VmPrimitive dispatch_handler=[&](const auto& frame,const auto& i,const auto& temps,const auto& dispatch){++dispatches;
                    require(dispatch.check_mode==check,"real dispatch check-mode differs from matrix case");
                    const auto before_frame=frame;const auto before_temps=temps;
                    const auto answer=handler(frame,i,temps,dispatch);
                    require(answer.disposition==(admitted?odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::unsupported),
                        "primitive answer differs at "+std::to_string(mode));
                    require(!answer.write&&!answer.call&&!answer.temp_write&&!answer.second_temp_write&&!answer.stack_object_write&&!answer.call_commit&&!answer.variable_write&&
                        odh::vm_frame_equal(frame,before_frame)&&temps==before_temps,"compatibility operation published frame/register/service effects");
                    return answer;
                };
            odh::Vm vm({{odh::routine_key(4096),{0x8002,{instruction},{},odh::Bhav::Metadata{0,4,4,0}}}},memory,{},dispatch_handler);
            if(!check)require(vm.start(odh::routine_key(4096),{1,2,2,0},{9,8,7,6})==odh::VmStatus::running,"start real compatibility VM");
            for(std::size_t n=0;n<vm.temps().size();++n)vm.temps()[n]=static_cast<std::int16_t>(700+n*13);
            vm.temp_xl()={-123456789,987654321};
            const auto temps=vm.temps();const auto xl=vm.temp_xl();
            const auto parent_status=vm.status();const auto parent_generation=vm.run_generation();
            auto status=odh::VmStatus::ready;
            if(check){
                const auto result=vm.evaluate_check_at_safe_point(odh::routine_key(4096),{1,2,2,0},{9,8,7,6},1,dispatch_handler);
                require(result.started,"real safe-point check did not start");status=result.status;
                require(vm.status()==parent_status&&vm.run_generation()==parent_generation&&vm.stack().empty(),"check changed its real parent Thread continuation");
            }else status=vm.run(1);
            require(status==(admitted?odh::VmStatus::returned_true:odh::VmStatus::fault),
                "real VM branch differs at "+std::to_string(mode)+" diagnostic="+vm.diagnostic());
            require(dispatches==1&&vm.temps()==temps&&vm.temp_xl()==xl&&reads==0&&writes==0&&
                odh::encode_world_snapshot(world.snapshot())==original_world&&random.seed()==original_seed,
                "World/register/RNG preservation differs at "+std::to_string(mode));
            ++cases;
        }
    }
    for(const auto opcode:{0U,2U,17U,27U,255U}){
        odh::Instruction instruction{static_cast<std::uint16_t>(opcode),254,255,{12,255,255,255,255,255,255,255}};
        require(handler({},instruction,{},{}).disposition==odh::PrimitiveDisposition::unsupported,"compatibility handler captured another opcode");
    }
    require(cases==1530,"complete remaining byte/ignored-operands/check-mode matrix");
    stateful_radio_checks();
    std::cout<<"generic_ts1_compatibility: PASS (1530 stateless/unsupported cases plus stateful radio matrix)\n";return 0;
}catch(const std::exception& e){std::cerr<<"generic_ts1_compatibility: "<<e.what()<<'\n';return 1;}}
