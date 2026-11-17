// SPDX-License-Identifier: MPL-2.0
// Ordinary dispatch and day-carry consumers; no game assets or fake clock hook.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/engine/VMThread.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/primitives/VMTS1Budget.h"
#include "simantics/model/VMStackObjectVariable.h"
#include "files/formats/iff/IffFile.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Primitives;
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct Payer:VMGameObject {
 VM* owner; int hour=-1,day=-1,globalHour=-1,globalSecond=-1;int64_t tick=-1;
 Payer(FSO::Content::GameObject* object,VM* vm):VMGameObject(object,nullptr),owner(vm){}
 void Tick() override {globalHour=owner->GetGlobalValue(0);globalSecond=owner->GetGlobalValue(6);hour=owner->Context->Clock->Hours;day=owner->Context->Clock->DayOfMonth;tick=owner->Context->Clock->Ticks;if(owner->TS1)VMTS1Budget::Commit(owner,3,5);}
};
int main(){try{
 VM::UseWorld(false);VMContext ctx(nullptr);VM vm(&ctx,nullptr);vm.TS1=true;vm.GlobalState.resize(64,0);
 ctx.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&ctx));ctx.Architecture()->Rooms[0].Map.assign(64,0);ctx.RoomInfo.resize(1);
 FSO::Files::Formats::IFF::IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"day-carry",true);
 FSO::Files::Formats::IFF::Chunks::OBJD def;def.GUID=0x12345678;def.Price=100;def.InitialDepreciation=20;def.DailyDepreciation=5;FSO::Content::GameObject object;object.OBJ=&def;object.GUID=def.GUID;object.Resource=&resource;
 Payer payer(&object,&vm);payer.ObjectID=1;payer.Thread=std::make_unique<FSO::SimAntics::Engine::VMThread>(&ctx,&payer,16);vm.Entities={&payer};
 VMTS1Budget::DaysType days{};days[0][3]=20;for(int i=1;i<6;++i)days[i][3]=i*35;VMTS1Budget::LoadState(&vm,590,days);
 auto& c=*ctx.Clock;c.TicksPerMinute=30;c.MinuteFractions=29;c.Minutes=59;c.Hours=23;c.DayOfMonth=18;c.Month=1;c.Year=2000;c.Ticks=123;vm.GlobalState[28]=7;
 payer.SetValue(FSO::SimAntics::Model::VMStackObjectVariable::CurrentValue,100);
 vm.InternalTick();need(payer.GetValue(FSO::SimAntics::Model::VMStackObjectVariable::CurrentValue)==80,"source initial depreciation did not run for retained offworld object");need(payer.hour==23&&payer.globalHour==23&&payer.globalSecond==58&&payer.day==18&&payer.tick==123,"TS1 entity observed next day before dispatch completed");
 need(c.Hours==0&&c.DayOfMonth==19&&c.Ticks==124&&vm.GlobalState[28]==8,"ordinary midnight did not publish one clock/day carry");
 need(vm.TS1Budget.Purse==585&&vm.TS1Budget.Days[0][3]==0&&vm.TS1Budget.Days[1][3]==25,"last old-day transaction was lost or posted into new day");
 for(int i=2;i<6;++i)need(vm.TS1Budget.Days[i][3]==35,"history used mathematical rotation rather than original ascending copies");
 c.DayOfMonth=22;c.Hours=9;c.Minutes=0;c.MinuteFractions=0;vm.GlobalState[6]=58;auto history=vm.TS1Budget.Days;vm.InternalTick();need(payer.globalHour==9&&payer.globalSecond==0&&vm.GlobalState[28]==8&&vm.TS1Budget.Days[1]==history[1],"date assignment fabricated midnight accounting");
 c.DayOfMonth=30;c.Month=12;c.Year=2000;c.Hours=23;c.Minutes=59;c.MinuteFractions=29;
 using V=FSO::SimAntics::Model::VMStackObjectVariable;
 payer.SetValue(V::BirthYear,2000);payer.SetValue(V::BirthMonth,12);payer.SetValue(V::BirthDay,1);
 vm.InternalTick();need(c.DayOfMonth==1&&c.Month==1&&c.Year==2001,"calendar carry did not follow original 30-day months");
 need(payer.GetValue(V::AgeInMonths)==0,"daily callback observed normalized next month instead of pre-carry date");
 vm.TS1=false;c.Ticks=500;vm.InternalTick();need(payer.tick==501,"TSO clock/dispatch order changed");vm.Entities.clear();std::puts("PASS ordinary TS1 daily accounting boundary");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
