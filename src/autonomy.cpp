// SPDX-License-Identifier: MPL-2.0
// VMFindBestAction / TTAB, pinned FreeSO7e76c1960252ddd033d8937d5edb47d326f5b69c.
#include "odh/autonomy.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
namespace odh {
namespace {
constexpr std::array<std::uint8_t,9> motives{{5,6,7,8,9,3,13,14,15}};
constexpr std::array<std::uint8_t,23> vary{{19,2,2,3,3,4,4,5,5,6,6,7,7,19,10,11,12,19,19,15,19,17,18}};
constexpr std::array<float,5> attenuation{{0,0,0.1f,0.3f,0.6f}};
constexpr std::array<float,5> visitor_attenuation{{0,0,0.01f,0.02f,0.03f}};
float add(float a,float b){volatile float value=a+b;return value;}
float sub(float a,float b){volatile float value=a-b;return value;}
float mul(float a,float b){volatile float value=a*b;return value;}
float divide(float a,float b){volatile float value=a/b;return value;}
std::int16_t short_bits(std::int32_t value){const auto bits=static_cast<std::uint16_t>(value);std::int16_t out;std::memcpy(&out,&bits,sizeof(out));return out;}
struct Position {std::int16_t x,y;std::int8_t level;};
Position position(const Entity& entity){
    if(!entity.position)return {-32768,-32768,1};
    const auto level_bits=static_cast<std::uint8_t>(entity.position->level);std::int8_t level;
    std::memcpy(&level,&level_bits,sizeof(level));
    return {short_bits(entity.position->x),short_bits(entity.position->y),level};
}
bool oow(const Position& p){return p.x==-32768&&p.y==-32768&&p.level==1;}
float distance(const Position& a,const Position& b){
    const auto dx=static_cast<std::int32_t>(a.x)-b.x,dy=static_cast<std::int32_t>(a.y)-b.y;
    const auto dl=(static_cast<std::int32_t>(a.level)-b.level)*320;
    // The cast precedes /16f in the source; coordinate differences are Int32,
    // unlike LotTilePos.operator- used by FindBestObjectForFunction.
    return divide(static_cast<float>(std::sqrt(std::pow(static_cast<double>(dx),2)+
        std::pow(static_cast<double>(dy),2)+std::pow(static_cast<double>(dl),2))),16.0f);
}
PrimitiveOutcome unsupported(std::string why){return {PrimitiveDisposition::unsupported,"FindBestAction: "+std::move(why)};}
PrimitiveOutcome branch(bool yes){return {yes?PrimitiveDisposition::goto_true:PrimitiveDisposition::goto_false,{}};}
PrimitiveOutcome selected(std::int16_t id){auto out=branch(true);out.stack_object_write=VmStackObjectWrite{id};return out;}
bool active(const FindBestActionRequest& r){
    return r.world&&r.source_vm&&r.source_frame&&r.source_instruction&&
        (r.source_instruction->opcode==3||r.source_instruction->opcode==65)&&
        r.source_dispatch.origin==r.source_vm&&!r.source_dispatch.check_mode&&
        r.source_vm->matches_active_primitive(*r.source_frame,*r.source_instruction,r.source_dispatch);
}
const Entity* entity(const World& world,AutonomyIdentity identity){
    const auto* value=world.find(identity.id);
    return value&&identity.id>0&&identity.generation&&value->generation==identity.generation?value:nullptr;
}
float sum(const std::array<float,9>& values){double out=0;for(auto value:values)out+=value;return static_cast<float>(out);}
}
PrimitiveOutcome find_best_action(const FindBestActionRequest& r){
    if(!active(r))return unsupported("authenticated TS1 source primitive required");
    try{
        const auto* initial=r.world->find(r.source_frame->context.caller);
        if(!initial||initial->kind!=EntityKind::avatar||initial->person_data.size()<=33)
            return unsupported("Caller avatar or Priority bank unavailable");
        const AutonomyIdentity caller{initial->id,initial->generation};
        const auto intact=[&]{
            if(!active(r)||!r.identities_still_match||!r.identities_still_match())return false;
            const auto* live=entity(*r.world,caller);
            return active(r)&&live&&live->kind==EntityKind::avatar;
        };
        const auto complete_avatar=[&]()->const Entity*{const auto* live=entity(*r.world,caller);
            return live&&live->kind==EntityKind::avatar&&live->person_data.size()>65&&live->motives.size()==16?live:nullptr;};
        if(!r.first_higher_priority||!intact())return unsupported("queue/source identity provider unavailable");
        const auto existing=r.first_higher_priority(initial->person_data[33]);
        if(!intact())return unsupported("queue lookup changed source identity");
        if(existing.status!=AutonomyStatus::ready)return unsupported("queue lookup: "+existing.diagnostic);
        if(existing.value){
            if(!entity(*r.world,*existing.value))return unsupported("queued Callee identity stale");
            return selected(existing.value->id);
        }
        if(r.limits.entities>4096||r.limits.interactions>65536||r.limits.variants>65536)
            return unsupported("configured work limits exceed bounded profile");
        const auto* avatar=complete_avatar();
        if(!avatar)
            return unsupported("Caller motives/personality/type banks incomplete");
        if(!r.ordered_with_autonomy)return unsupported("complete WithAutonomy inventory unavailable");
        const auto inventory=r.ordered_with_autonomy();
        if(!intact())return unsupported("inventory lookup changed source identity");
        if(inventory.status!=AutonomyStatus::ready)return unsupported("WithAutonomy inventory: "+inventory.diagnostic);
        if(inventory.value.size()>r.limits.entities)return unsupported("WithAutonomy entity budget exceeded");
        avatar=complete_avatar();
        if(!avatar)return unsupported("inventory lookup invalidated Caller banks");
        const auto start=position(*avatar);
        const bool visitor=avatar->person_data[32]==1,child=avatar->person_data[58]<18;
        const bool stray=(static_cast<std::uint16_t>(avatar->person_data[65])&24U)!=0&&
            visitor&&avatar->person_data[34]==0;
        if(!r.interaction_curves)return unsupported("selected global curve provider unavailable");
        const auto curve_result=r.interaction_curves(child);
        if(!intact())return unsupported("curve lookup changed source identity");
        if(curve_result.status!=AutonomyStatus::ready||!curve_result.value)
            return unsupported("selected global interaction curves: "+curve_result.diagnostic);
        const auto& curves=*curve_result.value;
        std::array<float,9> happy_parts{};
        const auto weight=divide(1.0f,9.0f);
        avatar=complete_avatar();
        if(!avatar)return unsupported("curve lookup invalidated Caller banks");
        for(std::size_t i=0;i<9;++i)happy_parts[i]=mul(curves[i].point(static_cast<float>(avatar->motives[motives[i]])),weight);
        const auto base_happy=sum(happy_parts);
        const double minimum=avatar->person_data[0]>0?1e-6:1e-7;
        std::set<std::int16_t> members,processed;
        struct Ranked {AutonomyChoice choice;std::shared_ptr<const Ttab> table;bool auto_first;};
        std::vector<Ranked> candidates;
        std::size_t interactions=0,variants=0;
        for(const auto member:inventory.value){
            const auto* live=entity(*r.world,member);
            if(!live||!members.insert(member.id).second)return unsupported("WithAutonomy member identity duplicate or stale");
            if(oow(position(*live)))continue;
            if(!r.resolve_object)return unsupported("interaction group/resource provider unavailable");
            const auto object_result=r.resolve_object(member);
            if(!intact()||!entity(*r.world,member))return unsupported("object lookup changed source identity");
            if(object_result.status!=AutonomyStatus::ready)return unsupported("object lookup: "+object_result.diagnostic);
            const auto& object=object_result.value;
            live=entity(*r.world,object.leader);
            if(!live)return unsupported("interaction group leader identity stale");
            if(processed.count(live->id))continue;
            if(live->kind==EntityKind::object){
                if(!object.disabled)return unsupported("object Disabled state unknown");
                if(*object.disabled>0)continue;
            }
            processed.insert(live->id);
            if(stray){
                if(live->kind==EntityKind::object)continue;
                if(!object.outside)return unsupported("stray avatar room/outside state unknown");
                if(!*object.outside)continue;
            }
            const auto dist=distance(start,position(*live));
            const auto occupied=live->object_words.find(8);
            if(occupied==live->object_words.end())return unsupported("leader Occupied flag unknown");
            const bool in_use=(static_cast<std::uint16_t>(occupied->second)&32U)!=0;
            if(!object.table)continue;
            const auto& table=*object.table;
            if(table.status==TtabStatus::empty)continue;
            if(table.status!=TtabStatus::decoded)return unsupported("TTAB is not decoded");
            for(const auto& entry:table.interactions){
                if(interactions>=r.limits.interactions)return unsupported("TTAB interaction budget exceeded");
                ++interactions;
                const auto has_join=[&](std::uint32_t id){return std::any_of(table.interactions.begin(),table.interactions.end(),
                    [&](const auto& row){return row.joining_index>=0&&static_cast<std::uint32_t>(row.joining_index)==id;});};
                if(in_use&&!has_join(entry.label_index))continue;
                if(entry.motives.size()>65536)return unsupported("TTAB motive-count budget exceeded");
                std::vector<std::size_t> advertisements;
                for(std::size_t i=0;i<entry.motives.size();++i)if(entry.motives[i].delta!=0)advertisements.push_back(i);
                if(advertisements.empty())continue;
                if(!r.check_action)return unsupported("source AutoCheck provider unavailable");
                const auto checked=r.check_action(object.leader,entry);
                if(!intact()||!entity(*r.world,object.leader))return unsupported("AutoCheck changed source identity");
                if(checked.status!=AutonomyStatus::ready)return unsupported("AutoCheck: "+checked.diagnostic);
                if(!checked.value.allowed)continue;
                const auto& pie=checked.value.variants;
                const auto pie_size=pie.empty()?(object.strings_present?std::size_t{1}:std::size_t{0}):pie.size();
                if(!pie_size)continue;
                if(pie_size>r.limits.variants-variants)return unsupported("AutoCheck variant budget exceeded");
                variants+=pie_size;
                const AutonomyVariant fallback;
                const auto& first=pie.empty()?fallback:pie.front();
                if(first.motive_ad_changes&&first.motive_ad_changes->size()>65536)
                    return unsupported("AutoCheck advertisement dictionary budget exceeded");
                float score=base_happy;
                for(const auto motive_index:advertisements){
                    if(motive_index>=16)return unsupported("active TTAB advertisement exceeds source motive bank");
                    const auto& advertised=entry.motives[motive_index];
                    auto min=advertised.minimum,delta=advertised.delta;
                    auto personality=short_bits(advertised.personality_modifier);
                    if(first.motive_ad_changes){
                        const auto get=[&](std::uint32_t type){const auto at=first.motive_ad_changes->find((type<<16)|static_cast<std::uint32_t>(motive_index));
                            return at==first.motive_ad_changes->end()?std::int16_t{0}:at->second;};
                        min=get(0);delta=get(1);personality=get(2);
                    }
                    if(delta==0&&min>0){delta=min;min=0;}
                    delta=short_bits(static_cast<std::int32_t>(delta)+min);
                    avatar=complete_avatar();
                    if(!avatar)
                        return unsupported("AutoCheck invalidated Caller banks");
                    const auto current=avatar->motives[motive_index];
                    if(min!=0&&current>min)continue;
                    const auto at=std::find(motives.begin(),motives.end(),motive_index);
                    if(at==motives.end())continue;
                    const auto index=static_cast<std::size_t>(at-motives.begin());
                    score=sub(score,happy_parts[index]);
                    float modifier=1;
                    if(personality>0&&static_cast<std::size_t>(personality)<vary.size()){
                        modifier=divide(static_cast<float>(avatar->person_data[vary[static_cast<std::size_t>(personality)]]),1000.0f);
                        if(personality<13){if((personality&1)==0)modifier=sub(1.0f,modifier);}
                        else modifier=mul(modifier,2.0f);
                    }
                    const auto changed=add(static_cast<float>(current),divide(mul(static_cast<float>(delta),modifier),1000.0f));
                    score=add(score,mul(curves[index].point(changed),weight));
                }
                score=sub(score,base_happy);
                float atten;
                if(entry.attenuation_code==0||entry.attenuation_code>=attenuation.size())
                    std::memcpy(&atten,&entry.attenuation_bits,sizeof(atten));
                else atten=visitor?visitor_attenuation[entry.attenuation_code]:attenuation[entry.attenuation_code];
                score=divide(score,add(1.0f,mul(atten,dist)));
                if(static_cast<double>(score)>minimum){
                    Ranked ranked{{object.leader,static_cast<std::uint8_t>(entry.label_index),first.param0,score,first.name,first.origin},
                        object.table,(entry.flags&(1U<<8))!=0};
                    // The pinned code appends FIRST once per variant, rather
                    // than appending each variant. Keep that weighting quirk.
                    for(std::size_t i=0;i<pie_size;++i)candidates.push_back(ranked);
                }
            }
        }
        std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.choice.score>b.choice.score;});
        std::vector<Ranked> top;
        for(const auto& candidate:candidates){
            if(!entity(*r.world,candidate.choice.callee))return unsupported("ranked Callee identity stale");
            if(!r.use_count)return unsupported("active-block UseCount provider unavailable");
            const auto users=r.use_count(candidate.choice.callee);
            if(!intact()||!entity(*r.world,candidate.choice.callee))return unsupported("UseCount lookup changed source identity");
            if(users.status!=AutonomyStatus::ready||users.value<0)return unsupported("UseCount unavailable/invalid: "+users.diagnostic);
            const bool joining=std::any_of(candidate.table->interactions.begin(),candidate.table->interactions.end(),
                [&](const auto& entry){return entry.joining_index==candidate.choice.id;});
            if(users.value==0||joining){top.push_back(candidate);if(top.size()==4)break;}
        }
        if(top.empty())return branch(false);
        auto selection=top.front();
        if(!selection.auto_first){
            if(!r.next_random)return unsupported("shared RNG provider unavailable");
            double total=0;for(const auto& candidate:top)total+=candidate.choice.score;
            const auto total_score=static_cast<float>(total);
            const auto random=r.next_random(10000);
            if(!intact())return unsupported("RNG callback changed source identity");
            if(random.status!=AutonomyStatus::ready||random.value>=10000)return unsupported("shared RNG result invalid: "+random.diagnostic);
            float accumulated=0;
            for(const auto& candidate:top){
                accumulated=add(accumulated,mul(divide(candidate.choice.score,total_score),10000.0f));
                if(static_cast<float>(random.value)<=accumulated){selection=candidate;break;}
            }
        }
        if(!r.enqueue||!entity(*r.world,selection.choice.callee))return unsupported("selected action admission unavailable or stale");
        const auto queued=r.enqueue(selection.choice);
        if(!intact())return unsupported("enqueue changed source identity");
        if(queued.status!=AutonomyStatus::ready)return unsupported("enqueue: "+queued.diagnostic);
        return queued.value?selected(selection.choice.callee.id):branch(false);
    }catch(const std::exception& error){return unsupported(error.what());}
}
}
