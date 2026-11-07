#include "odh/headline.hpp"
#include <limits>

namespace odh {
namespace {
std::uint16_t word(const std::array<std::uint8_t,8>& bytes,std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at]|(static_cast<unsigned>(bytes[at+1])<<8));
}
std::int32_t signed_word(std::uint16_t v) {return v<32768 ? v : static_cast<std::int32_t>(v)-65536;}
std::int8_t signed_byte(std::uint32_t v) {
    const auto low=v&255U;
    return static_cast<std::int8_t>(low<128 ? static_cast<std::int32_t>(low) : static_cast<std::int32_t>(low)-256);
}
bool valid(const BalloonHeadlineState& s) {
    auto initial=signed_word(word(s.operand,4));
    if((s.operand[7]&8U)&&initial!=-1)initial*=15;
    return s.target>0 && s.target_generation && s.icon_target>=0 &&
        ((s.icon_target==0)==(s.icon_generation==0)) && s.duration!=0 &&
        signed_byte(s.operand[2])!=-1 && word(s.operand,4)!=0 &&
        (s.operand[3]==7 || s.icon_target==0) &&
        (initial<0 ? s.duration==initial : s.duration>0&&s.duration<=initial);
}
}
bool valid_balloon_headline_snapshot(const BalloonHeadlineSnapshot& state) {
    if(state.size()>4096)return false;
    std::int16_t previous=0;
    for(const auto& s:state) {
        if(!valid(s)||s.target<=previous)return false;
        previous=s.target;
    }
    return true;
}
VmPrimitive BalloonHeadlines::primitive() {
    return [this](const auto& frame,const auto& instruction,const auto& temps,const auto&){
        return request(frame,instruction,temps);
    };
}
PrimitiveOutcome BalloonHeadlines::request(const VmFrame& frame,const Instruction& instruction,
    const Ts1Temps& temps) {
    if(instruction.opcode!=41)return {};
    const auto reject=[&](const char* why){diagnostic_=why;return PrimitiveOutcome{PrimitiveDisposition::unsupported,why};};
    const auto& op=instruction.operands;
    const auto* target=world_.frame_entity(frame,(word(op,0)&1U)?VmEntityRole::stack_object:VmEntityRole::caller,true);
    if(!target)return reject("headline target is not registered in World");
    const auto index=signed_byte(op[2]);
    const auto duration=signed_word(word(op,4));
    if(index==-1 || duration==0) {
        states_.erase(target->id);diagnostic_.clear();
        return {PrimitiveDisposition::goto_true,{}};
    }
    const Entity* icon=nullptr;
    auto resolved_index=static_cast<std::int32_t>(index);
    if(op[3]==7) {
        if(index<2)icon=world_.frame_entity(frame,VmEntityRole::stack_object);
        else {
            const auto local=static_cast<std::size_t>(word(op,0)>>1);
            if(local>=frame.locals.size())return reject("headline algorithmic local outside frame");
            icon=world_.find(frame.locals[local]);
        }
        // GetObjectById is allowed to return null. Missing StackObject is also
        // a valid null icon when Caller is the target.
    } else if(op[7]&16U) {
        if(temps.empty())return reject("headline indexed operand requires TEMP0");
        resolved_index+=temps[0];
    }
    if(states_.find(target->id)==states_.end()&&states_.size()>=4096)
        return reject("headline entity budget exceeded");
    BalloonHeadlineState state;
    state.operand=op;state.target=target->id;state.target_generation=target->generation;
    if(icon){state.icon_target=icon->id;state.icon_generation=icon->generation;}
    state.index=signed_byte(static_cast<std::uint32_t>(resolved_index));
    state.duration=(op[7]&8U)&&duration!=-1 ? duration*15 : duration;
    states_.insert_or_assign(target->id,state);
    diagnostic_.clear();return {PrimitiveDisposition::goto_true,{}};
}
bool BalloonHeadlines::tick(std::int16_t id) {
    const auto found=states_.find(id);
    if(found==states_.end()){diagnostic_.clear();return true;}
    const auto* target=world_.find(id);
    if(!target||target->generation!=found->second.target_generation) {
        diagnostic_="headline target generation changed";return false;
    }
    auto& state=found->second;
    // The reference uses unchecked signed32 Anim++, including indefinitely
    // displayed headlines. Express the wrap without C++ signed overflow.
    state.anim=state.anim==std::numeric_limits<std::int32_t>::max()
        ? std::numeric_limits<std::int32_t>::min() : state.anim+1;
    if(state.duration>0 && --state.duration==0)states_.erase(found);
    diagnostic_.clear();return true;
}
bool BalloonHeadlines::remove(std::int16_t id) {
    diagnostic_.clear();return states_.erase(id)!=0;
}
BalloonHeadlineSnapshot BalloonHeadlines::snapshot() const {
    BalloonHeadlineSnapshot result;result.reserve(states_.size());
    for(const auto& [id,state]:states_){(void)id;result.push_back(state);}
    return result;
}
bool BalloonHeadlines::restore_snapshot(const BalloonHeadlineSnapshot& snapshot) {
    if(!valid_balloon_headline_snapshot(snapshot)){diagnostic_="invalid headline snapshot";return false;}
    std::map<std::int16_t,BalloonHeadlineState> candidate;
    for(const auto& state:snapshot) {
        const auto* target=world_.find(state.target);
        // Icon identity is weak presentation history. Preserve its old pair
        // even after its body was collected; never bind it to a replacement.
        if(!target||target->generation!=state.target_generation) {
            diagnostic_="headline snapshot World identity mismatch";return false;
        }
        candidate.emplace(state.target,state);
    }
    states_.swap(candidate);diagnostic_.clear();return true;
}
} // namespace odh
