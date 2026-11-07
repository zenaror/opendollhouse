#include "odh/change_suit.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>

namespace odh {
namespace {
std::string lower(std::string text){for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return text;}
std::string first(std::string text){return text.substr(0,text.find(';'));}
bool expanded(const std::optional<std::string>& text){return text&&!text->empty()&&*text!="ADDED";}
std::string tokens(std::string value,const std::string& code,const std::string& skin){
    value=lower(std::move(value));
    for(const auto& replacement:std::array<std::pair<std::string,std::string>,3>{{{"$g",code.substr(0,1)},{"$b",code.substr(1)},{"$c",skin}}}){
        std::size_t at=0;while((at=value.find(replacement.first,at))!=std::string::npos){
            value.replace(at,replacement.first.size(),replacement.second);at+=replacement.second.size();}
    }return value;
}
Ts1PersonSuitResolution unsupported(std::string why){return {Ts1SuitResourceState::unsupported,{},std::move(why)};}
PrimitiveOutcome failure(std::string why){return {PrimitiveDisposition::unsupported,std::move(why)};}
bool flat(const std::string& name){
    if(name.size()>252)return false;
    for(const unsigned char c:name)
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))&&c!='-'&&c!='_'&&c!='.')return false;
    return true;
}
std::optional<std::uint64_t> outfit_id(std::string text){
    const auto begin=text.find_first_not_of(" \t\r\n"),end=text.find_last_not_of(" \t\r\n");
    if(begin==std::string::npos)return {};
    text=text.substr(begin,end-begin+1);
    if(text.front()=='+')text.erase(0,1);
    if(text.size()>2&&text[0]=='0'&&(text[1]=='x'||text[1]=='X'))text.erase(0,2);
    if(text.empty())return {};
    std::uint64_t result=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result,16);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())return {};
    return result;
}
}
Ts1PersonSuitResolution resolve_ts1_person_suit(const World& world,std::int16_t actor,std::uint16_t id,
        const Ts1SuitServices& services){
    const auto* avatar=world.find(actor);
    if(!avatar||avatar->kind!=EntityKind::avatar||avatar->person_data.size()<=65||!services.person)
        return unsupported("TS1 Person suit needs exact avatar BodySTR and PersonData age/gender");
    try{
        Ts1PersonSuitResult source;
        if(!world.evaluate_appearance_lookup([&]{source=services.person(world,actor);}))
            return unsupported("TS1 BodySTR provider failed or attempted World mutation");
        if(source.state!=Ts1SuitResourceState::ready)return unsupported("TS1 Person BodySTR unavailable: "+source.diagnostic);
        const auto& strings=source.strings;const bool male=avatar->person_data[65]==0;
        const auto age=avatar->person_data[58];const bool child=age<18&&age!=0;
        const auto skin=lower(strings.skin),body=lower(strings.body);
        std::string code=body.find("fat")!=std::string::npos?"fat":body.find("skn")!=std::string::npos?"skn":"fit";
        code=child?"uchd":(male?"m":"f")+code;
        AvatarOutfitReference outfit;outfit.hands=strings.hands;
        bool copy_default_hands=true;
        const auto original=[&]{outfit.literal=first(strings.body);copy_default_hands=false;};
        const auto extra=[&](std::size_t at){if(!strings.expanded[at])throw FormatError("TS1 expanded outfit BodySTR entry absent");return first(*strings.expanded[at]);};
        switch(id){
        case 0:original();break;
        case 1:outfit.literal="n"+code+"_01,BODY=n"+code+skin+"_01";break;
        case 2:
            if(expanded(strings.expanded[1]))outfit.literal=extra(1);
            else outfit.literal="n"+code+"_01,BODY=u"+code+skin+"_"+((male&&!child)?"briefs":"undies")+"01";
            break;
        case 3:{
            if(!services.job)return unsupported("TS1 Work.iff CARR uniform provider unavailable");
            Ts1JobSuitResult job;
            if(!world.evaluate_appearance_lookup([&]{job=services.job(avatar->person_data[56],avatar->person_data[57]);}))
                return unsupported("TS1 CARR provider failed or attempted World mutation");
            if(job.state!=Ts1SuitResourceState::ready)return unsupported("TS1 career level unavailable: "+job.diagnostic);
            if(job.male_mesh.empty()){original();break;}
            const auto mesh=male?job.male_mesh:job.female_mesh;
            outfit.literal=tokens(mesh,code,skin)+",BODY="+tokens(job.texture,code,skin);break;
        }
        case 4:
            outfit.literal=expanded(strings.expanded[0])?extra(0):"f"+code+"_01,BODY=f"+code+skin+"_01";break;
        case 5:
            if(expanded(strings.expanded[2]))outfit.literal=extra(2);
            else outfit.literal=(child?"pjs":"pajama")+(child?(male?std::string("mchd"):std::string("fchd")):code)+
                "_01,BODY="+(child?"pjs":"pajama")+(male?"m":"f")+skin+"_01";
            break;
        case 6:case 7:outfit.literal=(child?std::string("skeletonchd_01"):std::string("skeleton_01"))+
            (id==6?",BODY=skeleton_01":",BODY=skeleneg_01");break;
        case 8:case 9:case 10:case 11:case 12:case 13:outfit.literal=extra(0);break;
        case 14:outfit.literal=extra(1);break;
        case 15:outfit.literal=extra(2);break;
        case 16:case 17:outfit.literal=extra(3);break;
        case 18:outfit.literal=extra(4);break;
        default:return {Ts1SuitResourceState::absent,{},"source TS1 Person suit has no outfit for this index"};
        }
        // TS1 provider copies the literal hands from DefaultDaywear, or its
        // original BodySTR when that reference is a numeric outfit ID.
        if(copy_default_hands&&avatar->avatar_appearance&&avatar->avatar_appearance->default_daywear&&
           avatar->avatar_appearance->default_daywear->kind==AvatarOutfitKind::literal)
            outfit.hands=avatar->avatar_appearance->default_daywear->hands;
        return {Ts1SuitResourceState::ready,std::move(outfit),{}};
    }catch(const std::exception& error){return unsupported(error.what());}
}
VmPrimitive ts1_change_suit_or_accessory(World& world,Ts1SuitServices services){
    return [&world,services=std::move(services)](const VmFrame& frame,const Instruction& instruction,
        const Ts1Temps& temps,const VmDispatch&)->PrimitiveOutcome{
        if(instruction.opcode!=6)return failure("ChangeSuitOrAccessory handler received another opcode");
        const auto data=instruction.operands[0],scope=instruction.operands[1];
        const auto flags=static_cast<std::uint16_t>(instruction.operands[2]|(instruction.operands[3]<<8));
        const auto* actor=world.frame_entity(frame,VmEntityRole::caller,true);
        if(!actor||actor->kind!=EntityKind::avatar)return failure("ChangeSuitOrAccessory Caller is not an avatar");
        try{
            if(scope==2&&(flags&4)){
                if(!services.string)return failure("Update has no exact Callee STR304 provider");
                if(temps[0]<0)return failure("Update STR304 Temp0 index is negative");
                Ts1SuitStringResult source;
                if(!world.evaluate_appearance_lookup([&]{source=services.string(frame,scope,static_cast<std::uint16_t>(temps[0]),true);}))
                    return failure("Update STR304 provider failed or attempted World mutation");
                if(source.state!=Ts1SuitResourceState::ready)return failure("Update exact Callee STR304 unavailable: "+source.diagnostic);
                const auto id=outfit_id(source.value);if(!id)return failure("Update requires source hexadecimal outfit-ID, not a TS1 literal body");
                AvatarOutfitReference outfit;outfit.kind=AvatarOutfitKind::identifier;outfit.identifier=*id;
                if(!world.set_avatar_outfit(actor->id,outfit,true))return failure("Update avatar outfit publication failed");
                return {PrimitiveDisposition::goto_true,{}};
            }
            std::uint16_t index=data;
            if(flags&2){if(data>=temps.size())return failure("ChangeSuitOrAccessory Temp selector is outside the 20-register frame");
                index=static_cast<std::uint8_t>(temps[data]);}
            if(scope==1){
                const auto result=resolve_ts1_person_suit(world,actor->id,index,services);
                if(result.state==Ts1SuitResourceState::absent)return {PrimitiveDisposition::goto_true,result.diagnostic};
                if(result.state!=Ts1SuitResourceState::ready)return failure(result.diagnostic);
                if(!world.set_avatar_outfit(actor->id,result.outfit,false,static_cast<std::int16_t>(data)))
                    return failure("TS1 Person body and original CurrentOutfit publication failed");
                return {PrimitiveDisposition::goto_true,{}};
            }
            if(scope!=0&&scope!=2)return {PrimitiveDisposition::goto_true,"source scope has no suit"};
            if(!services.string)return failure("ChangeSuitOrAccessory exact STR304 provider unavailable");
            Ts1SuitStringResult result;
            if(!world.evaluate_appearance_lookup([&]{result=services.string(frame,scope,index,false);}))
                return failure("STR304 provider failed or attempted World mutation");
            if(result.state==Ts1SuitResourceState::absent)return {PrimitiveDisposition::goto_true,"source STR304 table absent"};
            if(result.state!=Ts1SuitResourceState::ready)return failure(result.diagnostic);
            if(!flat(result.value))return failure("ChangeSuitOrAccessory STR304 name is not a bounded flat identifier");
            const auto name=result.value+".apr";
            const bool changed=(flags&1)?world.remove_bound_appearance(actor->id,name):world.add_bound_appearance(actor->id,name);
            return changed?PrimitiveOutcome{PrimitiveDisposition::goto_true,{}}:failure("ChangeSuitOrAccessory accessory publication failed");
        }catch(const std::exception& error){return failure(std::string("ChangeSuitOrAccessory: ")+error.what());}
    };
}
VmPrimitive ts1_change_suit_or_accessory(World& world,Ts1ObjectSuitLookup lookup){
    Ts1SuitServices services;services.string=[lookup=std::move(lookup)](const auto& frame,std::uint8_t scope,std::uint16_t index,bool update){
        if(scope!=2||update||!lookup)return Ts1SuitStringResult{Ts1SuitResourceState::unsupported,{},"legacy exact Object provider lacks this source context"};
        const auto value=lookup(frame.context.code_owner,index);
        return value?Ts1SuitStringResult{Ts1SuitResourceState::ready,*value,{}}:
            Ts1SuitStringResult{Ts1SuitResourceState::unsupported,{},"exact code-owner STR304 index absent"};
    };return ts1_change_suit_or_accessory(world,std::move(services));
}
}
