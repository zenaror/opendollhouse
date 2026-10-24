#include "odh/motive_change.hpp"

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
        result=odh::ts1_motive_change(world,{},frame(),instruction(6,2));
        require(result.disposition==odh::PrimitiveDisposition::unsupported&&world.find(1)->motives[6]==93,
                "missing input variables fail without changing state");

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
        std::cout<<"TS1 Set Motive Change: "<<checks<<" checks passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
