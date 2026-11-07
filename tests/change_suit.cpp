#include "odh/change_suit.hpp"
#include <iostream>
#include <algorithm>
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
    invalid=op;invalid.operands[2]=8;invalid.operands[0]=0;
    check(handler(frame,invalid,temps,{}).disposition==odh::PrimitiveDisposition::goto_true,"unused source flag bits are ignored");
    check(world.remove_bound_appearance(1,"suit-toothbrush.apr"),"restore empty accessory fixture");
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

    odh::World suits;check(suits.create(1,odh::EntityKind::avatar,{0,101,0}),"Person fixture");
    check(suits.set_person_data(1,58,27)&&suits.set_person_data(1,65,0),"adult male");
    odh::Ts1SuitServices services;
    odh::Ts1PersonSuitStrings body;body.body="adultfit,BODY=original;unused";body.skin="LGT";
    body.hands={"l-idle","r-idle","l-point","r-point","l-fist","r-fist"};
    services.person=[&](const auto&,auto){return odh::Ts1PersonSuitResult{odh::Ts1SuitResourceState::ready,body,{}};};
    std::string male="$g$b_work",female="f$b_work",texture="$c_work";
    services.job=[&](auto,auto){return odh::Ts1JobSuitResult{odh::Ts1SuitResourceState::ready,male,female,texture,{}};};
    const auto resolved=[&](std::uint16_t id){return odh::resolve_ts1_person_suit(suits,1,id,services);};
    check(resolved(0).outfit.literal=="adultfit,BODY=original"&&resolved(0).outfit.hands==body.hands,"Daywear uses original BodySTR");
    check(resolved(1).outfit.literal=="nmfit_01,BODY=nmfitlgt_01","adult naked");
    check(resolved(2).outfit.literal=="nmfit_01,BODY=umfitlgt_briefs01","male adult swim");
    check(resolved(3).outfit.literal=="mfit_work,BODY=lgt_work","CARR tokens use actual code and skin");
    check(resolved(4).outfit.literal=="fmfit_01,BODY=fmfitlgt_01","formal fallback");
    check(resolved(5).outfit.literal=="pajamamfit_01,BODY=pajamamlgt_01","sleep fallback");
    check(resolved(6).outfit.literal=="skeleton_01,BODY=skeleton_01"&&resolved(7).outfit.literal=="skeleton_01,BODY=skeleneg_01","both skeleton transforms");
    check(resolved(19).state==odh::Ts1SuitResourceState::absent,"known unhandled Person suit is source null");
    check(resolved(8).state==odh::Ts1SuitResourceState::unsupported,"direct missing expanded outfit is not invented");
    body.expanded={"formal,BODY=f", "swim,BODY=s", "sleep,BODY=p", "costume,BODY=c", "transform,BODY=t"};
    for(std::uint16_t id=8;id<=18;++id){const auto index=id<=13?0:id==14?1:id==15?2:id<=17?3:4;
        check(resolved(id).outfit.literal==*body.expanded[index],"all expanded selectors use source table mapping");}
    check(resolved(2).outfit.literal==*body.expanded[1]&&resolved(4).outfit.literal==*body.expanded[0]&&resolved(5).outfit.literal==*body.expanded[2],"default expanded overrides");
    body.expanded[1]="ADDED";body.expanded[2]="";check(suits.set_person_data(1,58,7)&&suits.set_person_data(1,65,1),"female child");
    check(resolved(2).outfit.literal=="nuchd_01,BODY=uuchdlgt_undies01"&&resolved(5).outfit.literal=="pjsfchd_01,BODY=pjsflgt_01","child swim/sleep source formulas");
    check(resolved(6).outfit.literal=="skeletonchd_01,BODY=skeleton_01","child skeleton");
    female="";check(resolved(3).outfit.literal==",BODY=lgt_work","empty female mesh never substitutes male");
    auto altered=resolved(1).outfit;altered.hands.fill("updated");check(suits.set_avatar_outfit(1,altered,true),"updated default handgroup");
    check(resolved(1).outfit.hands==altered.hands&&resolved(0).outfit.hands==body.hands,"generated hands copy current default but Daywear remains original");
    male="";check(resolved(3).outfit.hands==body.hands&&resolved(3).outfit.literal=="adultfit,BODY=original","empty male CARR returns original Daywear even for female");
    std::uint8_t observed_scope=99;std::uint16_t observed_index=999;bool observed_update=false;
    services.string=[&](const auto& f,std::uint8_t scope,std::uint16_t index,bool update){
        check(f.context.code_owner==77,"exact frame reaches resource provider");observed_scope=scope;observed_index=index;observed_update=update;
        return odh::Ts1SuitStringResult{odh::Ts1SuitResourceState::ready,update?"0x1234":"accessory",{}};
    };
    const auto shared=odh::ts1_change_suit_or_accessory(suits,services);auto person_op=op;person_op.operands={1,1,0,0,0,0,0,0};
    check(shared(frame,person_op,temps,{}).disposition==odh::PrimitiveDisposition::goto_true&&suits.find(1)->avatar_appearance->body->literal==resolved(1).outfit.literal,"Person primitive publishes actual body");
    person_op.operands={3,1,2,0,0,0,0,0};temps[3]=257;
    check(shared(frame,person_op,temps,{}).disposition==odh::PrimitiveDisposition::goto_true&&suits.find(1)->person_data[8]==3&&suits.find(1)->avatar_appearance->body->literal==resolved(1).outfit.literal,"UseTemp byte wraps while CurrentOutfit retains original operand");
    person_op.operands={3,0,2,0,0,0,0,0};temps[3]=-1;
    check(shared(frame,person_op,temps,{}).disposition==odh::PrimitiveDisposition::goto_true&&observed_scope==0&&observed_index==255&&!observed_update,"Global and negative Temp wrap use exact provider");
    person_op.operands={99,2,7,0,0,0,0,0};temps[0]=12;
    check(shared(frame,person_op,temps,{}).disposition==odh::PrimitiveDisposition::goto_true&&observed_update&&observed_index==12&&suits.find(1)->avatar_appearance->body->identifier==0x1234&&suits.find(1)->avatar_appearance->default_daywear->identifier==0x1234&&suits.find(1)->person_data[8]==3,"Update uses Callee Temp0 hexadecimal reference and preserves CurrentOutfit");
    const auto before_null=suits.find(1)->avatar_appearance;person_op.operands={200,1,0,0,0,0,0,0};
    check(shared(frame,person_op,temps,{}).disposition==odh::PrimitiveDisposition::goto_true&&suits.find(1)->avatar_appearance==before_null,"known Person null has no effect");
    const odh::World::AvatarOutfitResolver outfit_resolver=[&](const auto& source_world,auto id,auto suit)->std::optional<odh::AvatarOutfitReference>{const auto result=odh::resolve_ts1_person_suit(source_world,id,suit,services);return result.state==odh::Ts1SuitResourceState::ready?std::optional<odh::AvatarOutfitReference>{result.outfit}:std::nullopt;};
    check(suits.set_avatar_outfit_resolver(outfit_resolver),"bind direct CurrentOutfit resolver");
    check(suits.set_person_data(1,8,1)&&suits.find(1)->person_data[8]==1&&suits.find(1)->avatar_appearance->body->literal==resolved(1).outfit.literal,"direct CurrentOutfit writes share body resolution");
    odh::World fresh_suits;check(fresh_suits.restore_snapshot(suits.snapshot())&&fresh_suits.set_avatar_outfit_resolver(outfit_resolver)&&fresh_suits.set_person_data(1,58,27)&&fresh_suits.set_person_data(1,65,0)&&fresh_suits.set_person_data(1,8,1)&&fresh_suits.find(1)->avatar_appearance->body->literal=="nmfit_01,BODY=nmfitlgt_01"&&suits.find(1)->person_data[65]==1,"shared resolver reads actual receiving fresh World without borrowing original state");
    const auto stable=suits.find(1)->avatar_appearance;
    check(suits.set_avatar_outfit_resolver([&](const auto&,auto,auto)->std::optional<odh::AvatarOutfitReference>{suits.set_person_data(1,65,0);return altered;}),"bind malicious resolver");
    check(!suits.set_person_data(1,8,2)&&suits.find(1)->person_data[65]==1&&suits.find(1)->avatar_appearance==stable,"resolver reentry rejects atomically");
    auto empty_services=services;empty_services.string=[](const auto&,auto,auto,bool){return odh::Ts1SuitStringResult{odh::Ts1SuitResourceState::ready,{},{}};};
    auto empty_op=op;empty_op.operands={0,0,0,0,0,0,0,0};
    check(odh::ts1_change_suit_or_accessory(suits,empty_services)(frame,empty_op,temps,{}).disposition==odh::PrimitiveDisposition::goto_true&&
        std::find(suits.find(1)->bound_appearances.begin(),suits.find(1)->bound_appearances.end(),".apr")!=suits.find(1)->bound_appearances.end(),"decoded STR304 null string concatenates .apr as source, distinct from missing table");
    auto malicious_services=services;
    malicious_services.person=[&](const auto&,auto){suits.remove(1);return odh::Ts1PersonSuitResult{odh::Ts1SuitResourceState::ready,body,{}};};
    check(odh::resolve_ts1_person_suit(suits,1,1,malicious_services).state==odh::Ts1SuitResourceState::unsupported&&suits.find(1)&&suits.find(1)->avatar_appearance==stable,"direct Person lookup blocks deletion and preserves live identity");
    malicious_services=services;malicious_services.string=[&](const auto&,auto,auto,bool){suits.remove(1);return odh::Ts1SuitStringResult{odh::Ts1SuitResourceState::ready,"unsafe",{}};};
    auto unsafe_op=op;unsafe_op.operands={0,0,0,0,0,0,0,0};
    check(odh::ts1_change_suit_or_accessory(suits,malicious_services)(frame,unsafe_op,temps,{}).disposition==odh::PrimitiveDisposition::unsupported&&suits.find(1),"Global/Object lookup shares same mutation guard");
    std::cout<<checks<<" ChangeSuitOrAccessory checks passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
