#include "odh/object_load.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/world_snapshot_codec.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int checks=0;
void require(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
constexpr std::int16_t entity_id=17;
constexpr odh::CodeOwner owner=0x80000005U;
odh::RoutineKey key(std::uint16_t id){return odh::routine_key(id);}
odh::Bhav routine(std::initializer_list<odh::Instruction> instructions){
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,4,4,0}};
}
odh::EntryPointTable table(std::uint16_t load=4097){
    odh::EntryPointTable result{};result.entries.resize(9);
    result.entries[0].action=4100;result.entries[1].action=4096;
    result.entries[2]={4100,load};result.entries[3].action=4100;result.entries[8].action=4100;
    return result;
}
odh::Instruction write_xl(){return {2,2,255,{1,0,77,0,0,5,42,7}};}
}

int main(){try {
    using namespace odh;
    World world;
    std::array<std::int16_t,68> raw{};raw[3]=-1;raw[12]=300;raw[26]=301;raw[54]=302;
    require(world.create_saved_entity(entity_id,EntityKind::object,0x1234,{7,8},{},{},raw,{},0),"private retained object fixture");
    auto shared=std::make_shared<const VmProgram>(RoutineSet{});
    int external_services=0;
    const VmPrimitive external=[&](const auto&,const auto&,const auto&,const auto&){
        ++external_services;return PrimitiveOutcome{PrimitiveDisposition::wait,{}};};
    const auto make_check=[&](RoutineSet code){return std::make_unique<Vm>(shared,ShareVmProgramTag{},world.vm_memory(),
        VmLimits{},external,RoutineOwners{{owner,std::move(code)}});};
    auto main=make_check({{key(4096),routine({{99,254,255,{}}})}});
    require(main->start(key(4096),{entity_id,entity_id,23,owner},{9,8,7,6})==VmStatus::running &&
        main->run(1)==VmStatus::waiting && main->pending(),"restored Main waiting on its own pending service");
    main->temps()[2]=71;main->temp_xl()[1]=202;
    const auto main_before=encode_vm_snapshot(main->snapshot());
    const auto world_before=world.snapshot();
    external_services=0;
    const auto assert_main=[&]{require(encode_vm_snapshot(main->snapshot())==main_before,
        "Load never changes restored Main stack, PC, status, pending, counters or registers");};
    for(const bool returns_false:{false,true}) {
        auto check=make_check({{key(4097),routine({{99,1,255,{}},write_xl(),
            {100,static_cast<std::uint8_t>(returns_false?255:254),255,{}}})},
            {key(4100),routine({{101,254,255,{}}})}});
        unsigned calls=0;
        const auto result=run_saved_entity_load(*check,*main,entity_id,owner,table(),100,
            [&](const VmFrame& frame,const Instruction& instruction,const Ts1Temps& temps,const VmDispatch& dispatch){
                require(dispatch.check_mode,"Load executes using synchronous check dispatch");
                require(frame.context.caller==entity_id && frame.context.callee==entity_id &&
                    frame.context.stack_object==0 && frame.context.code_owner==owner &&
                    frame.arguments==std::vector<std::int16_t>({0,0,0,0}),"Load has exact self/self/null-stack/resource-owner/zero-argument context");
                ++calls;
                if(instruction.opcode==99) {
                    require(temps[2]==71 && check->temp_xl()[1]==202,"Load starts with cloned restored TEMP and XL banks");
                    require(world.write(frame,0,0,991),"explicit Load write changes private retained attribute");
                    PrimitiveOutcome outcome{PrimitiveDisposition::goto_true,{}};outcome.temp_write=VmTempWrite{2,999};return outcome;
                }
                require(instruction.opcode==100 && temps[2]==999 && check->temp_xl()[1]==77,
                    "Load retains its own register changes and never dispatches Init/Main/Reset/dynamic or condition trees");
                return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
            });
        require(result.completed && result.steps==3 && calls==2 && result.stages.size()==1 &&
            result.stages[0].phase=="load" && result.stages[0].routine==4097 &&
            result.stages[0].status==(returns_false?VmStatus::returned_false:VmStatus::returned_true),
            "both true and false Load returns complete with exact stage and step count");
        require(world.find(entity_id)->attributes==std::vector<std::int16_t>({991,8}) &&
            world.find(entity_id)->object_words==world_before.entities.front().object_words &&
            world.find(entity_id)->saved_object_banks->object_data==raw,
            "Load preserves durable raw words and source banks except its explicit attribute write");
        assert_main();
        const auto before_retry=encode_world_snapshot(world.snapshot());
        const auto retry=run_saved_entity_load(*check,*main,entity_id,owner,table());
        require(!retry.completed && !retry.diagnostic.empty() && encode_world_snapshot(world.snapshot())==before_retry,
            "used disposable check is rejected before repeating any Load effect");
    }
    for(const unsigned action:{0U,255U}) {
        auto check=make_check({});const auto skipped=run_saved_entity_load(*check,*main,entity_id,owner,table(static_cast<std::uint16_t>(action)));
        require(skipped.completed && skipped.steps==0 && check->status()==VmStatus::ready && check->snapshot().run_generation==0,
            "absent or <=255 Load entry skips without starting a check");
    }
    auto check=make_check({});EntryPointTable missing{};
    require(run_saved_entity_load(*check,*main,entity_id,owner,missing).completed,"short table without EP2 is a supported no-op");
    const auto no_effects=[&](auto call,const char* message){
        const auto before=encode_world_snapshot(world.snapshot());const auto result=call();
        require(!result.completed && !result.diagnostic.empty() && encode_world_snapshot(world.snapshot())==before,message);assert_main();};
    no_effects([&]{return run_saved_entity_load(*check,*main,entity_id,owner,table());},"missing Load routine rejects before effects");
    no_effects([&]{return run_saved_entity_load(*main,*main,entity_id,owner,table());},"Main cannot also be its disposable Load VM");
    no_effects([&]{return run_saved_entity_load(*check,*main,0,owner,missing);},"invalid entity rejects even when Load absent");
    no_effects([&]{return run_saved_entity_load(*check,*main,entity_id,0,missing);},"invalid resource owner rejects even when Load absent");
    auto fresh=make_check({{key(4097),routine({{99,254,255,{}}})}});
    no_effects([&]{return run_saved_entity_load(*fresh,*main,entity_id,owner,table(),0);},"zero Load budget rejects before effects");
    require(fresh->status()==VmStatus::ready && fresh->snapshot().run_generation==0,"budget preflight leaves check unused");
    require(fresh->start(key(4097),{entity_id,entity_id,0,owner})==VmStatus::running,"nonfresh check fixture");
    no_effects([&]{return run_saved_entity_load(*fresh,*main,entity_id,owner,table());},"already-running check is rejected before effects");
    for(const unsigned failure:{0U,1U,2U}) {
        auto failing=make_check({{key(4097),routine({{99,static_cast<std::uint8_t>(failure==2?0:254),255,{}}})}});
        unsigned calls=0;
        const auto result=run_saved_entity_load(*failing,*main,entity_id,owner,table(),2,
            [&](const auto&,const auto&,const auto&,const auto& dispatch){
                require(dispatch.check_mode,"failed Load remains synchronous-only");++calls;
                return PrimitiveOutcome{failure==0?PrimitiveDisposition::wait:failure==1?
                    PrimitiveDisposition::unsupported:PrimitiveDisposition::goto_true,"fixture failure"};});
        require(!result.completed && !result.diagnostic.empty() && !failing->pending() && calls>0 &&
            failing->status()==(failure==2?VmStatus::budget_exhausted:VmStatus::fault),
            "Load wait, fault and budget exhaustion fail closed without publishing asynchronous pending work");
        assert_main();
    }
    {
        auto nested_check=make_check({{key(4097),routine({{100,254,255,{}}})}});
        Vm* running=nullptr;bool refused=false;unsigned nested_effects=0;
        const auto before=encode_world_snapshot(world.snapshot());
        const VmPrimitive in_tick=[&](const auto&,const auto&,const auto&,const auto&){
            const auto result=run_saved_entity_load(*nested_check,*running,entity_id,owner,table(),10,
                [&](const auto&,const auto&,const auto&,const auto&){
                    ++nested_effects;return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};});
            refused=!result.completed && !result.diagnostic.empty();
            require(nested_check->status()==VmStatus::ready && nested_check->run_generation()==0 &&
                nested_check->stack().empty() && !nested_check->pending(),
                "Load attempted inside Main dispatch leaves disposable check entirely fresh");
            return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
        };
        Vm nested_main(shared,ShareVmProgramTag{},world.vm_memory(),VmLimits{},in_tick,
            RoutineOwners{{owner,{{key(4096),routine({{99,254,255,{}}})}}}});
        running=&nested_main;
        require(nested_main.start(key(4096),{entity_id,entity_id,0,owner})==VmStatus::running &&
            nested_main.run(1)==VmStatus::returned_true && refused && nested_effects==0 &&
            encode_world_snapshot(world.snapshot())==before,
            "outside-tick Load helper refuses nested Main dispatch before effects while Main finishes normally");
        assert_main();
    }
    require(external_services==0,"Load never calls external host-service handler from restored Main or disposable check");
    std::cout<<"PASS saved entity Load contract ("<<checks<<" checks)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
