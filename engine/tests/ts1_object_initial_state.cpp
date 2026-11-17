// SPDX-License-Identifier: MPL-2.0
// Synthetic definitions exercise the real fresh-creation consumer and Init
// expressions. No installed game data, world ticks or product test hooks.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/primitives/VMExpression.h"
#include "simantics/utils/VMTS1ObjectAge.h"
#include "content/ContentManager.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/BHAV.h"
#include <array>
#include <cstdio>
#include <stdexcept>

using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::LotView::Model::LotTilePos;
using FSO::LotView::Model::Direction;
using Scope=Engine::Scopes::VMVariableScope;
using Value=VMStackObjectVariable;

static int checks=0;
static void check(bool ok,const char* message) {
    if(!ok) throw std::runtime_error(message);
    ++checks;
}

// Isolate the same provider cache used by normal CreateObjectInstance without
// exposing it in the production API or consulting an ambient installation.
template<class T,typename T::Type Member> struct Access {
    friend typename T::Type field(T) { return Member; }
};
struct CacheField {
    using Type=std::unordered_map<uint64_t,std::unique_ptr<FSO::Content::GameObject>> FSO::Content::WorldObjectProvider::*;
    friend Type field(CacheField);
};
template struct Access<CacheField,&FSO::Content::WorldObjectProvider::Cache>;

struct ProviderScope {
    FSO::Content::Content* content=FSO::Content::Content::Get();
    std::unique_ptr<FSO::Content::WorldObjectProvider> previous=std::move(content->WorldObjects);
    ProviderScope() { content->WorldObjects=std::make_unique<FSO::Content::WorldObjectProvider>(content); }
    ~ProviderScope() { content->WorldObjects=std::move(previous); }
    void add(OBJD* definition,FSO::Content::GameObjectResource* resource) {
        auto object=std::make_unique<FSO::Content::GameObject>();
        object->GUID=definition->GUID;object->OBJ=definition;object->Resource=resource;
        (content->WorldObjects.get()->*field(CacheField{}))[definition->GUID]=std::move(object);
    }
};

static BHAVInstruction assign(Scope destination,int index,Scope source,int sourceIndex,int next) {
    BHAVInstruction instruction;instruction.Opcode=2;
    instruction.TruePointer=uint8_t(next);instruction.FalsePointer=255;
    Primitives::VMExpressionOperand operand;
    operand.LhsOwner=destination;operand.LhsData=int16_t(index);
    operand.RhsOwner=source;operand.RhsData=int16_t(sourceIndex);
    operand.Operator=Primitives::VMExpressionOperator::Assign;
    operand.Write(instruction.Operand.data());return instruction;
}

struct Fixture {
    IffFile iff;
    FSO::Content::GameObjectResource resource{&iff,nullptr,nullptr,"object-initial-state",true};
    ProviderScope provider;
    VMContext context{nullptr};
    VM vm{&context,nullptr};
    explicit Fixture(bool ts1=true,int16_t authoredBirthYear=0) {
        vm.TS1=ts1;vm.GlobalState.assign(64,0);
        context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&context));
        context.Architecture()->Rooms[0].Map.assign(64,0);context.RoomInfo.resize(1);
        auto& clock=*context.Clock;
        clock.Year=1997;clock.Month=6;clock.DayOfMonth=18;clock.Hours=9;clock.Minutes=45;
        auto init=new BHAV();init->ChunkProcessed=true;init->ChunkID=4096;init->Args=4;
        const std::array<Value,6> observed={Value::BirthYear,Value::BirthMonth,Value::BirthDay,
            Value::BirthHour,Value::BirthMinutes,Value::CurrentValue};
        for(size_t i=0;i<observed.size();++i)
            init->Instructions.push_back(assign(Scope::MyObjectAttributes,int(i),Scope::MyObject,int(observed[i]),int(i+1)));
        if(authoredBirthYear)
            init->Instructions.push_back(assign(Scope::MyObject,int(Value::BirthYear),Scope::Literal,authoredBirthYear,
                                                int(init->Instructions.size()+1)));
        init->Instructions.push_back(assign(Scope::MyObject,int(Value::CurrentValue),Scope::Literal,777,254));
        iff.AddChunk(init);
    }
    OBJD* definition(uint32_t guid,uint16_t id,uint16_t master=0,int16_t sub=0,uint16_t price=100) {
        auto result=new OBJD();result->ChunkProcessed=true;result->ChunkID=id;
        result->GUID=guid;result->ObjectType=OBJDType::Normal;result->StackSize=16;
        result->NumAttributes=6;result->BHAV_Init=4096;result->Price=price;
        result->MasterID=master;result->SubIndex=sub;
        iff.AddChunk(result);provider.add(result,&resource);return result;
    }
    Entities::VMMultitileGroup* create(uint32_t guid) {
        return context.CreateObjectInstance(guid,LotTilePos::OUT_OF_WORLD,Direction::NORTH,vm.TS1);
    }
};

static void birthVisibleToInit(VMEntity& object) {
    const std::array<int16_t,5> expected={1997,6,18,9,45};
    for(size_t i=0;i<expected.size();++i)
        check(object.GetAttribute(int(i))==expected[i],"Init did not observe the fresh calendar stamp");
    check(object.Position()==LotTilePos::OUT_OF_WORLD,"fresh offworld creation moved the object");
    check(object.GetValue(Value::SimAge)==0&&object.GetValue(Value::AgeInMonths)==0,
          "fresh age was not recalculated from its stamped date");
}

static void freshSingle() {
    Fixture fixture;fixture.definition(0xf0100001,100);
    auto group=fixture.create(0xf0100001);
    check(group&&group->Objects.size()==1,"real single creation did not produce one entity");
    auto object=group->BaseObject();birthVisibleToInit(*object);
    check(object->GetAttribute(5)==0,"single Price became visible before Init");
    check(object->GetValue(Value::CurrentValue)==100&&group->Price==100,
          "single creation did not overwrite Init's777 with definition Price");
}

static void freshMultitile() {
    Fixture fixture;
    fixture.definition(0xf0100010,100,7,-1,100);
    fixture.definition(0xf0100011,101,7,0,11);
    fixture.definition(0xf0100012,102,7,1,12);
    auto group=fixture.create(0xf0100010);
    check(group&&group->MultiTile&&group->Objects.size()==2,"real master creation did not produce both parts");
    auto root=group->BaseObject();
    for(auto part:group->Objects) {
        birthVisibleToInit(*part);
        check(part->GetAttribute(5)==(part==root?100:0),"multitile Init observed the wrong root/component Price");
        check(part->GetValue(Value::CurrentValue)==777,"multitile creation overwrote Init's value afterward");
    }
    check(group->Price==777,"group value cache does not reflect the root's final authored value");
}

static void ageAfterInit() {
    Fixture fixture(true,1996);fixture.definition(0xf0100018,100);
    auto group=fixture.create(0xf0100018);
    check(group&&group->Objects.size()==1,"authored-birthday fixture creation failed");
    auto object=group->BaseObject();
    check(object->GetAttribute(0)==1997&&object->GetValue(Value::BirthYear)==1996,
          "Init did not observe then change the fresh birthday");
    check(object->GetValue(Value::SimAge)==1&&object->GetValue(Value::AgeInMonths)==12,
          "real creation did not refresh age after the authored Init change");
}

static void ageOnlyAndLegacyCreation() {
    Fixture fixture;fixture.definition(0xf0100020,100);
    auto group=fixture.create(0xf0100020);auto object=group->BaseObject();
    // Expected constants include borrowing, a future birthday and WORD wrap;
    // they are independent vectors, not a reimplementation of the helper.
    struct AgeCase { int year,month,day,birthYear,birthMonth,birthDay,years,months; };
    const AgeCase cases[]={
        {2000,1,1,1999,12,1,0,1},
        {2000,1,1,2000,1,2,-1,-1},
        {1997,6,18,1998,7,19,-2,-14},
        {2000,6,18,1997,6,18,3,36},
        {32768,1,1,0,1,1,-32768,0},
        {65535,1,1,-1,1,1,0,0}
    };
    for(const auto& c:cases) {
        fixture.context.Clock->Year=c.year;fixture.context.Clock->Month=c.month;fixture.context.Clock->DayOfMonth=c.day;
        object->SetValue(Value::BirthYear,int16_t(c.birthYear));object->SetValue(Value::BirthMonth,int16_t(c.birthMonth));
        object->SetValue(Value::BirthDay,int16_t(c.birthDay));
        Utils::RefreshTS1ObjectAge(*object,*fixture.context.Clock);
        check(object->GetValue(Value::SimAge)==c.years&&object->GetValue(Value::AgeInMonths)==c.months,
              "signed calendar age differs from the source vector");
    }
    object->SetValue(Value::BirthYear,0);object->SetValue(Value::BirthMonth,0);object->SetValue(Value::BirthDay,0);
    object->SetValue(Value::BirthHour,3);object->SetValue(Value::BirthMinutes,4);object->SetValue(Value::CurrentValue,321);
    fixture.context.Clock->Year=1997;fixture.context.Clock->Month=6;fixture.context.Clock->DayOfMonth=18;
    Utils::RefreshTS1ObjectAge(*object,*fixture.context.Clock);
    check(object->GetValue(Value::BirthYear)==0&&object->GetValue(Value::BirthMonth)==0&&object->GetValue(Value::BirthDay)==0&&
          object->GetValue(Value::BirthHour)==3&&object->GetValue(Value::BirthMinutes)==4&&object->GetValue(Value::CurrentValue)==321,
          "age refresh stamped or repriced retained zero-DOB state");
    check(object->GetValue(Value::SimAge)==1997&&object->GetValue(Value::AgeInMonths)==23970,
          "zero-DOB age was silently normalized instead of calculated");
}

static void nonTS1Creation() {
    Fixture fixture(false);fixture.definition(0xf0100030,100);
    auto group=fixture.create(0xf0100030);
    check(group&&group->Objects.size()==1,"non-TS1 creation failed");
    auto object=group->BaseObject();
    for(int i=0;i<5;++i) check(object->GetAttribute(i)==0,"TS1 birth stamp leaked into non-TS1 Init");
    check(object->GetAttribute(5)==0&&object->GetValue(Value::CurrentValue)==777,
          "TS1 post-Init pricing leaked into non-TS1 creation");
}

int main() { try {
    VM::UseWorld(false);
    for(auto run:{freshSingle,freshMultitile,ageAfterInit,ageOnlyAndLegacyCreation,nonTS1Creation}) {
        run();
        // Resource addresses may be reused by the next fixture. Drop compiled
        // BHAVs only after its VM has gone, using the normal public cache API.
        check(VM::ReleaseAssembled(),"fixture VM outlived its routine-cache cleanup");
    }
    std::printf("PASS object initial state: %d checks\n",checks);return 0;
} catch(const std::exception& error) {
    std::fprintf(stderr,"FAIL object initial state: %s\n",error.what());return 1;
} }
