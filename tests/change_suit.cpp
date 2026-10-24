#include "odh/change_suit.hpp"
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void unsupported(const odh::PrimitiveOutcome& out,const char* message){
    check(out.disposition==odh::PrimitiveDisposition::unsupported,message);
}
}
int main(){try{
    odh::World world;
    check(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object),
          "fixture entities");
    const std::map<std::pair<odh::CodeOwner,std::uint16_t>,std::string> names{
        {{77,0},"suit-toothbrush"},{{77,1},"suit-toothpaste"}};
    const auto lookup=[&](odh::CodeOwner owner,std::uint16_t index)->std::optional<std::string>{
        const auto found=names.find({owner,index});
        return found==names.end()?std::optional<std::string>{}:found->second;
    };
    const auto handler=odh::ts1_change_suit_or_accessory(world,lookup);
    odh::VmFrame frame{odh::routine_key(4102),23,{1,2,2,77},{},{}};
    odh::Instruction op{};op.opcode=6;op.operands[0]=0;op.operands[1]=2;
    std::array<std::int16_t,20> temps{};
    auto out=handler(frame,op,temps,{});
    check(out.disposition==odh::PrimitiveDisposition::goto_true&&
          world.find(1)->bound_appearances==std::vector<std::string>{"suit-toothbrush.apr"},
          "Object scope resolves STR#304 and binds the .apr name to Caller");
    out=handler(frame,op,temps,{});
    check(out.disposition==odh::PrimitiveDisposition::goto_true&&
          world.find(1)->bound_appearances.size()==1,
          "adding the same accessory is idempotent like BoundAppearances");
    op.operands[0]=1;
    out=handler(frame,op,temps,{});
    check(out.disposition==odh::PrimitiveDisposition::goto_true&&
          world.find(1)->bound_appearances==std::vector<std::string>({"suit-toothbrush.apr","suit-toothpaste.apr"}),
          "second accessory adds without replacing the first and keeps canonical order");
    op.operands[2]=1;op.operands[3]=0;
    out=handler(frame,op,temps,{});
    check(out.disposition==odh::PrimitiveDisposition::goto_true&&
          world.find(1)->bound_appearances==std::vector<std::string>{"suit-toothbrush.apr"},
          "Remove flag removes only the selected accessory");
    op.operands[0]=3;op.operands[2]=3;temps[3]=0;
    out=handler(frame,op,temps,{});
    check(out.disposition==odh::PrimitiveDisposition::goto_true&&
          world.find(1)->bound_appearances.empty(),
          "UseTemp selects the STR#304 index and Remove removes that selected accessory");

    const auto before=world.find(1)->bound_appearances;
    auto invalid=op;invalid.operands[1]=1;
    unsupported(handler(frame,invalid,temps,{}),"non-Object scope fails closed");
    invalid=op;invalid.operands[2]=4;
    unsupported(handler(frame,invalid,temps,{}),"Update default outfit is not guessed");
    invalid=op;invalid.operands[2]=8;
    unsupported(handler(frame,invalid,temps,{}),"unknown flags fail closed");
    invalid=op;invalid.operands[0]=9;invalid.operands[2]=0;
    unsupported(handler(frame,invalid,temps,{}),"missing STR#304 entry fails closed");
    invalid=op;invalid.operands[0]=3;temps[3]=-1;
    unsupported(handler(frame,invalid,temps,{}),"negative Temp index fails closed");
    invalid=op;invalid.operands[0]=20;
    unsupported(handler(frame,invalid,temps,{}),"Temp selector outside the frame fails closed");
    auto non_avatar=frame;non_avatar.context.caller=2;invalid=op;invalid.operands[0]=0;
    unsupported(handler(non_avatar,invalid,temps,{}),"object Caller cannot receive avatar appearance state");
    check(world.find(1)->bound_appearances==before,
          "unsupported operands leave avatar appearance state unchanged");

    odh::WorldLimits limits;limits.bound_appearance_names=1;
    odh::World limited(limits);check(limited.create(1,odh::EntityKind::avatar),"limited avatar");
    const auto limited_handler=odh::ts1_change_suit_or_accessory(limited,lookup);
    auto limit_frame=frame;limit_frame.context.code_owner=77;
    auto add_first=op;add_first.operands[0]=0;add_first.operands[2]=0;
    check(limited_handler(limit_frame,add_first,temps,{}).disposition==odh::PrimitiveDisposition::goto_true,
          "first accessory fits configured budget");
    auto add_second=add_first;add_second.operands[0]=1;
    unsupported(limited_handler(limit_frame,add_second,temps,{}),"appearance count budget is enforced");
    check(limited.find(1)->bound_appearances==std::vector<std::string>{"suit-toothbrush.apr"},
          "budget rejection is atomic");
    std::cout<<checks<<" ChangeSuitOrAccessory checks passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
