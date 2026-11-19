// SPDX-License-Identifier: MPL-2.0
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include "simantics/engine/VMThread.h"
#include "simantics/VMContext.h"
#include "simantics/VM.h"
#include "common/TS1/TS1PersonBirth.h"
#include "content/ContentManager.h"
#include <algorithm>
#include <stdexcept>

namespace FSO { namespace SimAntics {
using Engine::Utils::VMTS1Neighbourhood;

void VMAvatar::InitTS1FreshBirth(VMContext* context, bool runFirstTick) {
    if(!context || !context->VM->TS1 || GhostImage || !VMTS1Neighbourhood::HasBirthNeighbor(this))
        throw std::runtime_error("Original person birth requires a fresh registered neighbor");
    //Factory440e62 first invokes person virtual3c. Its tail4258ed/459a40
    //precedes the separate virtual40 reset below. Appearance/resource binding
    //has already run in Init; this migration uses the SAME independent stream.
    VMTS1Neighbourhood::ApplyBirthProfile(this);
    if(Common::TS1::LegacyInterestBank(PersonData)){
        auto migrated=PersonData;
        Common::TS1::MigratePersonInterests(migrated,Common::TS1::NaturalPersonCRTStream());
        for(int index:{46,47,48,49,50,51,52,53,54,55,13,14,16,20,26})
            SetPersonData((Model::VMPersonDataVariable)index,migrated[index]);
    }
    //459a54 preserves these runtime fields across the person-bank reset.
    const auto type=PersonData[32],greet=PersonData[34],schedule=PersonData[35];
    PersonData.fill(0);PersonData[32]=type;PersonData[34]=greet;PersonData[35]=schedule;
    Animations.clear();TS1AnimationController.Events.clear();
    TS1AnimationController.ScriptActive=0;TS1AnimationController.CarrySuppressed=0;
    const bool legacyInterests=Common::TS1::LegacyInterestBank(PersonData);
    VMTS1Neighbourhood::ApplyBirthProfile(this); //4258ed, before base reset
    PrepareTS1Fresh(context); //Main is installed before the first Init.
    if(Object->OBJ->MasterID==0){
        RunTS1InitEntry(context,true);
        if(!Dead && runFirstTick){
            struct CounterScope {
                bool& Flag;bool Previous;
                explicit CounterScope(bool& flag):Flag(flag),Previous(flag){Flag=true;}
                ~CounterScope(){Flag=Previous;}
            } counter(TS1FreshPersonTick);
            Thread->RunTS1FreshTick();TS1PublishSimIndependent();
        }
    }
    if(Dead)return;
    //459b2a: same interpreter/temporaries/Main, not another constructor.
    RunTS1InitEntry(context,false);
    if(Dead)return;
    if(PersonData[25]==1966)SetPersonData((Model::VMPersonDataVariable)25,0);
    else VMTS1Neighbourhood::ApplyBirthProfile(this);
    //4074bc's policy-object identity is not bound here. Keep the established
    //native classification rather than substitute a guessed zoning predicate.
    if(legacyInterests){
        if(Common::TS1::LegacyInterestBank(PersonData)){
            auto migrated=PersonData;
            Common::TS1::MigratePersonInterests(migrated,Common::TS1::NaturalPersonCRTStream());
            for(int index:{46,47,48,49,50,51,52,53,54,55,13,14,16,20,26})
                SetPersonData((Model::VMPersonDataVariable)index,migrated[index]);
        }
    }else VMTS1Neighbourhood::RefreshBirthInterests(this); //42597a, distinct narrow table
}

void VMAvatar::RunTS1VisitorInit(VMContext* context) {
    if(!context || !context->VM->TS1 || Dead || !Thread || !TreeByName || PersonData[32]==0)return;
    const auto found=TreeByName->find("init visitor");
    if(found==TreeByName->end() || !found->second.bhav)return;
    auto frame=std::make_shared<Engine::VMStackFrame>();
    frame->Thread=Thread.get();frame->Caller=this;frame->Callee=this;frame->StackObject=this;
    frame->StackObjectRawID=ObjectID;frame->CodeOwner=Object;
    frame->Routine=context->VM->Assemble(found->second.bhav);
    const auto count=size_t(frame->Routine->Arguments);
    if(count>Thread->TempRegisters.size())throw std::runtime_error("Original visitor argument bank overflow");
    frame->Args.assign(std::max<size_t>(4,count),0);
    std::copy_n(Thread->TempRegisters.begin(),count,frame->Args.begin());
    Thread->RunTS1SynchronousCall(std::move(frame));
}
}}
