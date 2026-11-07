#include "odh/special_effect.hpp"
#include <exception>
#include <memory>
#include <utility>

namespace odh {
namespace {
std::int8_t signed_byte(std::uint8_t value) {
    return static_cast<std::int8_t>(value < 128 ? static_cast<int>(value) : static_cast<int>(value)-256);
}
}
SpecialEffectOperands parse_special_effect_operands(const std::array<std::uint8_t,8>& bytes) {
    return {static_cast<std::uint16_t>(bytes[0] | (static_cast<std::uint16_t>(bytes[1]) << 8)),
            signed_byte(bytes[2]), signed_byte(bytes[3]), signed_byte(bytes[4]),
            signed_byte(bytes[5]), {bytes[6],bytes[7]}};
}
VmPrimitive special_effect_primitive(const World& world, SpecialEffectServices services) {
    return [&world, services=std::move(services)](const VmFrame& frame,
        const Instruction& instruction, const Ts1Temps&, const VmDispatch& dispatch) -> PrimitiveOutcome {
        if (instruction.opcode != 35) return {};
        const auto authenticated = [&] {
            return dispatch.origin && dispatch.origin->matches_active_query(frame,instruction,dispatch);
        };
        if (!authenticated()) return {PrimitiveDisposition::unsupported,"SpecialEffect requires its actual VM dispatch"};
        const auto operand=parse_special_effect_operands(instruction.operands);
        if (!services.use_world || (static_cast<std::uint8_t>(operand.flags)&8U)==0)
            return {PrimitiveDisposition::goto_true,{}};
        const auto* target=world.frame_entity(frame,VmEntityRole::stack_object);
        if (!target) return {PrimitiveDisposition::unsupported,"SpecialEffect visual focus requires an exact StackObject"};
        if (!services.center_to) return {PrimitiveDisposition::unsupported,"SpecialEffect visual focus backend is unavailable"};
        const WorldEntityReference reference{target->id,target->generation};
        auto active=std::make_shared<bool>(true);
        struct Expire { std::shared_ptr<bool> active; ~Expire(){*active=false;} } expire{active};
        // Copies own their proof data; a retained callback cannot outlive the
        // scope and subsequently dereference the captured VM or World.
        auto proof=[active,&world,frame,instruction,dispatch,reference] {
            return *active && dispatch.origin &&
                dispatch.origin->matches_active_query(frame,instruction,dispatch) &&
                world.frame_entity(frame,VmEntityRole::stack_object) &&
                world.resolve_reference(reference)==world.frame_entity(frame,VmEntityRole::stack_object);
        };
        SpecialEffectFocusResult result;
        try { result=services.center_to({reference,operand,proof}); }
        catch (const std::exception& e) {
            return {PrimitiveDisposition::unsupported,"SpecialEffect focus backend failed: "+std::string(e.what()).substr(0,256)};
        } catch (...) { return {PrimitiveDisposition::unsupported,"SpecialEffect focus backend failed"}; }
        if (!proof()) return {PrimitiveDisposition::unsupported,"SpecialEffect focus source changed during callback"};
        if (!result.completed) return {PrimitiveDisposition::unsupported,"SpecialEffect focus incomplete: "+result.diagnostic.substr(0,256)};
        return {PrimitiveDisposition::goto_true,{}};
    };
}
}
