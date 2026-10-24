#include "odh/change_suit.hpp"
#include <cstdint>

namespace odh {
VmPrimitive ts1_change_suit_or_accessory(World& world,Ts1ObjectSuitLookup lookup) {
    return [&world,lookup=std::move(lookup)](const VmFrame& frame,const Instruction& instruction,
        const std::array<std::int16_t,20>& temps,const VmDispatch&) {
        const auto unsupported=[](const char* why){
            return PrimitiveOutcome{PrimitiveDisposition::unsupported,why};
        };
        if(instruction.opcode!=6)return unsupported("ChangeSuitOrAccessory handler received another opcode");
        const auto data=instruction.operands[0];
        const auto scope=instruction.operands[1];
        const auto flags=static_cast<std::uint16_t>(instruction.operands[2])|
            (static_cast<std::uint16_t>(instruction.operands[3])<<8);
        constexpr std::uint16_t remove=1,use_temp=2,update=4;
        if(scope!=2)return unsupported("ChangeSuitOrAccessory supports only exact Object scope");
        if(flags&static_cast<std::uint16_t>(~(remove|use_temp|update)))
            return unsupported("ChangeSuitOrAccessory has unknown flags");
        if(flags&update)return unsupported("ChangeSuitOrAccessory Update changes default outfit and is not implemented");
        auto index=static_cast<std::uint16_t>(data);
        if(flags&use_temp){
            if(data>=temps.size())return unsupported("ChangeSuitOrAccessory Temp selector is outside the 20-register frame");
            const auto selected=temps[data];
            if(selected<0||selected>255)return unsupported("ChangeSuitOrAccessory Temp index is outside byte range");
            index=static_cast<std::uint16_t>(selected);
        }
        if(!lookup)return unsupported("ChangeSuitOrAccessory has no exact code-owner STR#304 provider");
        std::optional<std::string> name;
        try{name=lookup(frame.context.code_owner,index);}
        catch(const std::exception& error){
            return PrimitiveOutcome{PrimitiveDisposition::unsupported,
                std::string("ChangeSuitOrAccessory STR#304 lookup failed: ")+error.what()};
        }
        if(!name)return unsupported("ChangeSuitOrAccessory exact code-owner STR#304 index is absent");
        if(name->empty()||name->size()>252)return unsupported("ChangeSuitOrAccessory STR#304 name is not bounded");
        for(const unsigned char c:*name){
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))
                return unsupported("ChangeSuitOrAccessory STR#304 name is not a flat asset identifier");
        }
        *name += ".apr";
        const bool changed=(flags&remove)?world.remove_bound_appearance(frame.context.caller,*name):
                                          world.add_bound_appearance(frame.context.caller,*name);
        if(!changed)return unsupported("ChangeSuitOrAccessory avatar or appearance state is outside the supported profile");
        return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
    };
}
}
