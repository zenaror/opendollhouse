#include "odh/motive_change.hpp"
#include "original_motive_import.hpp"
#include "odh/world_snapshot_codec.hpp"
#include "odh/avatar_tick.hpp"

#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>

namespace {
int checks=0;
void require(bool condition,const char* message) {
    ++checks;
    if(!condition)throw std::runtime_error(message);
}

void add_actor(odh::World& world) {
    if(!world.create(1,odh::EntityKind::avatar,{0,0,0,16}))
        throw std::runtime_error("avatar fixture setup failed");
}

odh::Instruction instruction(std::uint8_t motive,std::uint8_t flags,
                             std::int16_t delta_index=0,std::int16_t max_index=1) {
    const auto put=[](std::array<std::uint8_t,8>& out,std::size_t offset,std::int16_t value) {
        const auto bits=static_cast<std::uint16_t>(value);
        out[offset]=static_cast<std::uint8_t>(bits&0xffU);
        out[offset+1]=static_cast<std::uint8_t>(bits>>8);
    };
    odh::Instruction result{29,0,0,{14,14,motive,flags,0,0,0,0}};
    put(result.operands,4,delta_index);
    put(result.operands,6,max_index);
    return result;
}

odh::VmMemory memory_with(std::int16_t delta,std::int16_t maximum) {
    odh::VmMemory memory;
    memory.read=[=](const odh::VmFrame&,std::uint8_t scope,std::int16_t index)
        ->std::optional<std::int16_t> {
        if(scope!=14)return {};
        if(index==0)return delta;
        if(index==1)return maximum;
        return {};
    };
    return memory;
}

odh::VmFrame frame() {
    return {odh::routine_key(4096),0,{1,1,1,0},{},{}};
}
}

namespace {
std::uint32_t float_bits(float value){std::uint32_t bits;std::memcpy(&bits,&value,4);return bits;}
struct OriginalMotiveFixture {
    odh::World world;odh::Vm vm;odh::Ts1MotiveDecay decay;odh::Ts1Clock clock;
    odh::WorldEntityReference ref{};odh::ObjmPersonState source{};bool lease=true;
    OriginalMotiveFixture():vm({{odh::routine_key(4096),odh::Bhav{0x8002,{{99,254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}}}}){
        require(world.create(1,odh::EntityKind::avatar,{0,80,0,16}),"original motive avatar");
        ref={1,world.find(1)->generation};require(world.set_person_data(1,0,45),"preserve source PD0");
        for(std::int16_t i=0;i<16;++i)require(world.set_motive(1,i,40),"source motive seed");
        odh::VmContext context{1,1,1,0};require(world.bind_context_live(context),"source actual identity");
        require(vm.start(odh::routine_key(4096),context)==odh::VmStatus::running&&clock.initialize(7,1,2,6,1997),"source VM and SIMI clock");
    }
    void delta(int motive,float rate,float cap){source.motive_deltas.push_back({motive,float_bits(rate),float_bits(cap)});source.motive_delta_count=static_cast<std::int32_t>(source.motive_deltas.size());}
    std::optional<odh::VmOriginalImportPlan> thread(std::string& error){auto frame=vm.stack().front();
        frame.original_import=odh::VmOriginalFrameState{0,odh::VmOriginalReturn::normal};
        return vm.prepare_original_import({frame},vm.temps(),[this]{return lease;},error);}
    auto prepare(const odh::VmSnapshot& future,std::string& error){return odh_host::OriginalMotiveImportPlan::prepare(world,ref,vm,decay,clock,source,future,[this]{return lease;},error);}
};
void original_motive_import_matrix(){
    OriginalMotiveFixture f;f.delta(5,2.0f,90);f.delta(7,-1.0f,-20.9f);
    // Product is exact binary32 2.5/3.5; source uses ties-to-even, not std::round.
    f.delta(6,2.5f/1800.0f,100);f.delta(8,3.5f/1800.0f,100);f.delta(5,0.5f,77.9f);
    std::string error;auto execution=f.thread(error);require(bool(execution),error.c_str());
    const auto old=f.world.snapshot();auto plan=f.prepare(execution->snapshot(),error);require(bool(plan),error.c_str());
    require(f.world.find(1)->motive_changes.empty()&&f.vm.steps()==0,"motive prepare is read-only");
    require(f.vm.commit_original_import(*execution)&&plan->commit(f.vm.snapshot(),error),error.c_str());
    const auto* actor=f.world.find(1);const auto& changes=actor->motive_changes;
    require(changes.size()==16&&changes[0].maximum==0&&changes[0].per_hour_change==0&&
        changes[5].per_hour_change==900&&changes[5].maximum==77&&changes[7].per_hour_change==-1800&&changes[7].maximum==-20&&
        changes[6].per_hour_change==2&&changes[8].per_hour_change==4,"source defaults, float product ties-even, signed truncation, last-wins and no Energy rescale");
    for(const auto& change:changes)require(change.fractional==0&&!change.ticked,"new source carries and Ticked are zero");
    require(actor->motives==old.entities[0].motives&&actor->person_data==old.entities[0].person_data&&actor->person_data[0]==45&&
        f.decay.snapshot().last_two_minute_bucket==0&&f.clock.snapshot().minute==2&&f.clock.snapshot().ticks==0&&f.vm.steps()==0,
        "matching import preserves needs/PD0/rest/real SIMI clock and gives new source decay bucket0");
    require(!plan->commit(f.vm.snapshot(),error),"motive import is single-use");
    require(f.world.tick_motive_changes(f.ref),"actual continuous consumer after import");
    const auto bytes=odh::encode_world_snapshot(f.world.snapshot());odh::World cold;
    require(cold.restore_snapshot(odh::decode_world_snapshot(bytes))&&
        cold.find(1)->motive_changes[5].fractional==f.world.find(1)->motive_changes[5].fractional&&
        cold.find(1)->motive_changes[5].per_hour_change==900&&!cold.find(1)->motive_changes[5].ticked,
        "existing cold codec keeps native rate/cap/fraction and source resets Ticked only");
    require(f.world.tick_motive_changes(f.ref)&&cold.tick_motive_changes({1,cold.find(1)->generation})&&
        f.world.find(1)->motives==cold.find(1)->motives,"cold continues actual controller without reconstructed elapsed time");
    require(f.world.clear_motive_changes(1),"existing source Reset clear");
    for(const auto& change:f.world.find(1)->motive_changes)require(change.per_hour_change==0&&change.maximum==32767,
        "mismatch/reset Clear differs from new ConvertAvatar defaults and uses real service");
    for(unsigned scenario=0;scenario<7;++scenario){OriginalMotiveFixture bad;bad.delta(7,1,100);auto vmplan=bad.thread(error);require(bool(vmplan),error.c_str());
        if(scenario==0)bad.source.motive_deltas[0].motive=16;
        if(scenario==1)bad.source.motive_deltas[0].tick_delta_bits=0x7fc00000;
        if(scenario==2)bad.source.motive_deltas[0].stop_at_bits=0x7f800000;
        if(scenario==3)bad.source.motive_deltas[0].tick_delta_bits=float_bits(100.0f);
        if(scenario==4)bad.source.motive_delta_count=2;
        if(scenario==5)bad.source.motive_deltas[0].stop_at_bits=float_bits(32768.0f);
        if(scenario==6)bad.source.motive_deltas[0].motive=-1;
        require(!bad.prepare(vmplan->snapshot(),error)&&bad.world.find(1)->motive_changes.empty()&&!bad.vm.steps(),
            "invalid original delta profile refuses before effects");}
    for(unsigned scenario=0;scenario<5;++scenario){OriginalMotiveFixture stale;auto vmplan=stale.thread(error);require(bool(vmplan),error.c_str());auto prepared=stale.prepare(vmplan->snapshot(),error);require(bool(prepared),error.c_str());
        if(scenario==0)stale.lease=false;
        if(scenario==1)require(stale.world.set_person_data(1,0,46),"change PD source preimage");
        if(scenario==2)require(stale.world.set_motive(1,5,41),"change need source preimage");
        if(scenario==3)require(stale.clock.tick(true),"change actual clock preimage");
        if(scenario==4)require(stale.world.tick_motive_changes(stale.ref),"change actual controller preimage");
        require(!prepared->current(error)&&stale.vm.steps()==0,"stale authority/PD/needs/clock/controller rejected without dispatch");}
    OriginalMotiveFixture retained;const auto retired_ref=retained.ref;
    require(retained.world.set_person_data(1,27,123)&&retained.world.remove_retaining_body(retired_ref)&&
        retained.world.create(1,odh::EntityKind::avatar,{0,80,0,16})&&retained.world.set_person_data(1,27,1000),"old avatar retained while ID is reused");
    require(!odh::tick_ts1_avatar_counter(retained.world,retired_ref)&&retained.world.find(1)->person_data[27]==1000&&
        retained.world.resolve_reference(retired_ref)->person_data[27]==123,"retained old generation cannot write replacement's counter");
    auto retained_vm=retained.thread(error);require(bool(retained_vm),error.c_str());
    require(!retained.prepare(retained_vm->snapshot(),error),"retained ancestor is not an actual live motive import target");
    OriginalMotiveFixture publication;std::array<odh::MotiveChangeState,16> source_bank{};
    source_bank[0].fractional=1.0;const auto pristine=odh::encode_world_snapshot(publication.world.snapshot());
    require(!publication.world.publish_original_motive_changes(publication.ref,{},source_bank)&&
        odh::encode_world_snapshot(publication.world.snapshot())==pristine,"World batch rejects invented source fraction atomically");
    source_bank[0].fractional=0.0;
    require(!publication.world.publish_original_motive_changes({1,publication.ref.generation+1},{},source_bank)&&
        odh::encode_world_snapshot(publication.world.snapshot())==pristine,"World batch rejects stale generation atomically");
    require(publication.world.tick_motive_changes(publication.ref),"publish controller preimage drift");
    const auto drifted=odh::encode_world_snapshot(publication.world.snapshot());
    require(!publication.world.publish_original_motive_changes(publication.ref,{},source_bank)&&
        odh::encode_world_snapshot(publication.world.snapshot())==drifted,"World conditional publication rejects changed bank before mutation");
    OriginalMotiveFixture boundaries;boundaries.delta(0,-0.0f,-32768.9f);boundaries.delta(15,0,32767.9f);
    auto boundary_vm=boundaries.thread(error);require(bool(boundary_vm),error.c_str());auto boundary_plan=boundaries.prepare(boundary_vm->snapshot(),error);require(bool(boundary_plan),error.c_str());
    require(boundaries.vm.commit_original_import(*boundary_vm)&&boundary_plan->commit(boundaries.vm.snapshot(),error)&&
        boundaries.world.find(1)->motive_changes[0].maximum==-32768&&boundaries.world.find(1)->motive_changes[15].maximum==32767,
        "finite short boundary truncation and signed zero source rate are admitted");
    OriginalMotiveFixture order;order.delta(5,2,80);require(order.world.set_motive(1,5,79)&&order.world.set_motive(1,11,-1),"wake-boundary seed");
    auto vmplan=order.thread(error);require(bool(vmplan),error.c_str());auto prepared=order.prepare(vmplan->snapshot(),error);require(bool(prepared),error.c_str());
    require(order.vm.commit_original_import(*vmplan)&&prepared->commit(order.vm.snapshot(),error),error.c_str());
    odh::Ts1MotiveInputs in;in.active_personality=666;in.outgoing_personality=500;in.sleep_state=-1;
    for(std::size_t i=0;i<odh::ts1_need_count;++i){auto motive=static_cast<std::int16_t>(odh::ts1_motive_index(static_cast<odh::Ts1Need>(i)));in.needs[i]=order.world.find(1)->motives[std::size_t(motive)];in.active_changes[i]=order.world.has_motive_change(1,motive);}
    const auto result=order.decay.update(7,2,in);require(result.status==odh::Ts1MotiveStatus::updated,"decay at new source bucket");
    for(std::size_t i=0;i<odh::ts1_need_count;++i)if(result.needs_written[i])require(order.world.set_motive(1,static_cast<std::int16_t>(odh::ts1_motive_index(static_cast<odh::Ts1Need>(i))),result.needs[i]),"decay writes");
    if(result.sleep_state_written)require(order.world.set_motive(1,11,result.sleep_state),"source wake setter");
    require(order.world.tick_motive_changes(order.ref)&&odh::tick_ts1_avatar_counter(order.world,order.ref)&&
        order.world.find(1)->motives[5]==81&&order.world.find(1)->motives[11]==-1&&
        order.world.find(1)->motive_changes[5].fractional==2.0&&order.world.find(1)->person_data[27]==1,
        "source decay before continuous change preserves sleep and over-limit fractional carry; inverse order would wake at80");
}
}

int main() {
    try {
        odh::World world;
        add_actor(world);
        require(world.set_motive(1,6,90),"set comfort before one-shot gain");
        auto result=odh::ts1_motive_change(world,memory_with(20,100),frame(),instruction(6,2));
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&world.find(1)->motives[6]==100,
                "one-shot motive gain clamps at its supplied maximum");

        require(world.set_motive(1,8,-90),"set hygiene before one-shot loss");
        result=odh::ts1_motive_change(world,memory_with(-20,-100),frame(),instruction(8,2));
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&world.find(1)->motives[8]==-100,
                "one-shot motive loss clamps at its supplied minimum");

        require(world.set_motive(1,5,0),"set energy before scaled gain");
        result=odh::ts1_motive_change(world,memory_with(1,100),frame(),instruction(5,2));
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&world.find(1)->motives[5]==22,
                "positive TS1 energy change uses the reference integer scale 22");

        result=odh::ts1_motive_change(world,memory_with(-7,-100),frame(),instruction(6,2));
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&world.find(1)->motives[6]==93,
                "one-shot changes below the maximum apply their signed delta");

        result=odh::ts1_motive_change(world,{},frame(),instruction(6,1));
        require(result.disposition==odh::PrimitiveDisposition::goto_true,
                "ClearAll succeeds without reading unused variable inputs");
        result=odh::ts1_motive_change(world,memory_with(1,100),frame(),instruction(6,4));
        require(result.disposition==odh::PrimitiveDisposition::unsupported&&world.find(1)->motives[6]==93,
                "unknown operand flags fail without changing the motive");
        result=odh::ts1_motive_change(world,memory_with(1,100),frame(),instruction(16,2));
        require(result.disposition==odh::PrimitiveDisposition::unsupported&&world.find(1)->motives[6]==93,
                "motive index outside the bank fails without changing state");
        result=odh::ts1_motive_change(world,{},frame(),instruction(6,2,20,21));
        require(result.disposition==odh::PrimitiveDisposition::unsupported&&world.find(1)->motives[6]==93,
                "missing input variables fail without changing state");

        // TVs.iff BHAV4157 pc5 reads its cap from the executing Thread's
        // Temp0, not from the entity memory backend. Exercise the primitive
        // callback so dropping its supplied registers cannot go unnoticed.
        odh::World television;
        add_actor(television);
        require(television.tick_motive_changes(),"open TV motive controller gate");
        odh::VmMemory tuning;
        tuning.read=[](const odh::VmFrame&,std::uint8_t scope,std::int16_t index)
            ->std::optional<std::int16_t>{if(scope==26&&index==132)return 1800;return {};};
        auto tv=odh::ts1_motive_change_primitive(television,tuning);
        odh::Ts1Temps registers{};registers[0]=75;
        const odh::Instruction tv_instruction{29,0,0,{26,8,15,0,132,0,0,0}};
        result=tv(frame(),tv_instruction,registers,{});
        auto tv_state=television.snapshot();
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&
                tv_state.entities[0].motive_changes[15].per_hour_change==1800&&
                tv_state.entities[0].motive_changes[15].maximum==75,
                "real TV operand reads tuning rate and executing Thread Temp0 cap");

        auto variables=frame();variables.arguments={3};variables.locals={4};
        registers[1]=0;
        const auto apply_variable=[&](std::uint8_t scope,std::int16_t index){
            auto op=instruction(6,2,index,100);op.operands[0]=scope;op.operands[1]=7;
            require(television.set_motive(1,6,0),"reset variable-sourced comfort");
            return tv(variables,op,registers,{});
        };
        for(const auto scope:{std::uint8_t{7},std::uint8_t{8},std::uint8_t{9},std::uint8_t{11},std::uint8_t{25},std::uint8_t{40}}){
            const auto index=static_cast<std::int16_t>(scope==7?5:(scope==11||scope==40)?1:0);
            result=apply_variable(scope,index);
            const auto expected=scope==7?5:(scope==8||scope==11)?75:scope==9?3:4;
            require(result.disposition==odh::PrimitiveDisposition::goto_true&&
                    television.find(1)->motives[6]==expected,
                    "motive operands share literal, Thread, argument and local selectors");
        }
        result=apply_variable(8,20);
        require(result.disposition==odh::PrimitiveDisposition::unsupported&&television.find(1)->motives[6]==0,
                "invalid Thread selector fails without changing motives");

        require(world.set_motive(1,6,100),"set comfort at ordinary cap");
        result=odh::ts1_motive_change(world,memory_with(5,90),frame(),instruction(6,2));
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&world.find(1)->motives[6]==100,
                "already-above-maximum motive is preserved without a clamp");
        result=odh::ts1_motive_change(world,memory_with(-5,-90),frame(),instruction(8,2));
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&world.find(1)->motives[8]==-100,
                "already-below-minimum motive is preserved without a clamp");

        require(world.set_motive(1,6,100),"reset comfort before signed overflow case");
        result=odh::ts1_motive_change(world,memory_with(32767,32767),frame(),instruction(6,2));
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&world.find(1)->motives[6]==-100,
                "short compound addition wraps before the reference limit check");

        odh::World continuous;
        add_actor(continuous);
        require(continuous.set_motive(1,6,40),"initialize comfort before continuous gain");
        result=odh::ts1_motive_change(continuous,memory_with(1800,50),frame(),instruction(6,0));
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&!continuous.has_motive_change(1,6),
                "first pre-tick SetMotiveChange is ignored by Simitone ordering");
        require(continuous.tick_motive_changes(),"avatar tick opens continuous controller gate");
        result=odh::ts1_motive_change(continuous,memory_with(1800,50),frame(),instruction(6,0));
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&continuous.has_motive_change(1,6),
                "continuous form installs its rate on the caller avatar");
        require(continuous.tick_motive_changes()&&continuous.find(1)->motives[6]==41,
                "continuous rate accumulates once per TS1 five-second tick");
        require(!continuous.has_motive_change(1,7),"unrelated motive remains inactive");
        result=odh::ts1_motive_change(continuous,{},frame(),instruction(6,255));
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&!continuous.has_motive_change(1,6),
                "ClearAll wins over unused operands and unknown flag bits");

        require(continuous.tick_motive_changes(),"re-arm after ClearAll");
        require(continuous.set_motive(1,6,99),"set value near continuous maximum");
        result=odh::ts1_motive_change(continuous,memory_with(3600,100),frame(),instruction(6,0));
        require(result.disposition==odh::PrimitiveDisposition::goto_true,"install faster continuous rate");
        require(continuous.tick_motive_changes()&&continuous.find(1)->motives[6]==100&&continuous.has_motive_change(1,6),
                "continuous rate clamps at maximum but remains active");

        result=odh::ts1_motive_change(continuous,memory_with(1,100),frame(),instruction(5,0));
        const auto energy_state=continuous.snapshot();
        require(result.disposition==odh::PrimitiveDisposition::goto_true&&
                energy_state.entities[0].motive_changes[5].per_hour_change==22,
                "continuous positive TS1 energy rate uses the reference scale 22");

        require(continuous.clear_motive_changes(1)&&!continuous.has_motive_change(1,5)&&
                !continuous.has_motive_change(1,6),
                "host interaction cleanup clears all active rates");
        original_motive_import_matrix();
        std::cout<<"TS1 Set Motive Change: "<<checks<<" checks passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
