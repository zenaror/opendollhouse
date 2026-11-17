// SPDX-License-Identifier: MPL-2.0
// Real script/register/checkpoint consumers without commercial assets. The
// actual lot saver overlay and map reopen also require the private App witness.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMMemory.h"
#include "simantics/primitives/VMExpression.h"
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/IffFieldEncode.h"
#include "files/formats/iff/chunks/OBJMWriter.h"
#include "files/formats/iff/chunks/ODHI.h"
#include "files/Hash.h"
#include "content/ContentManager.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Engine::Utils;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using Scope=FSO::SimAntics::Engine::Scopes::VMVariableScope;
using V=FSO::SimAntics::Model::VMStackObjectVariable;
using Pos=FSO::LotView::Model::LotTilePos;
static int checks=0;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);++checks;}
static OBJTEntry type(uint32_t guid){st::Writer bytes(st::Endian::Little);bytes.WriteUInt32(guid);for(int i=0;i<4;++i)bytes.WriteUInt16(0);bytes.WriteUInt16(1);bytes.WriteUInt16(uint16_t(OBJDType::Normal));bytes.WriteByte('x');bytes.WriteByte(0);auto raw=bytes.TakeArray();st::Reader input(raw);return OBJTEntry(input,2);}
int main(){try{
    VM::UseWorld(false);VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;
    vm.GlobalState.resize(64,0);
    context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&context));
    context.Architecture()->Rooms[0].Map.assign(64,0);context.RoomInfo.resize(1);
    context.RoomInfo[0].Room.IsOutside=true;
    IffFile resources;FSO::Content::GameObjectResource resource(&resources,nullptr,nullptr,"offworld-room",true);
    OBJD definition;definition.GUID=0x12345678;definition.NumAttributes=1;
    FSO::Content::GameObject object;object.OBJ=&definition;object.Resource=&resource;
    VMGameObject entity(&object,nullptr);Entities::VMMultitileGroup group;
    entity.MultitileGroup=&group;group.AddObject(&entity);vm.AddEntity(&entity);
    entity.Thread=std::make_unique<VMThread>(&context,&entity,16);
    VMStackFrame frame;frame.Caller=frame.Callee=frame.StackObject=&entity;frame.Thread=entity.Thread.get();
    Primitives::VMExpression expression;Primitives::VMExpressionOperand operand;
    operand.LhsData=int16_t(V::Room);operand.RhsOwner=Scope::Literal;operand.RhsData=0;
    operand.Operator=Primitives::VMExpressionOperator::LessThan;
    auto negative=[&](Scope scope){operand.LhsOwner=scope;return expression.Execute(&frame,&operand)==VMPrimitiveExitCode::GOTO_TRUE;};
    entity.Position(Pos::OUT_OF_WORLD);context.RegisterObjectPos(&entity);
    need(context.GetObjectRoom(&entity)==0&&context.RoomInfo.size()==1,"internal out-of-world index changed");
    need(VMMemory::GetVariable(&frame,Scope::MyObject,int16_t(V::Room))==-5,"My Room script exposure");
    need(VMMemory::GetVariable(&frame,Scope::StackObject,int16_t(V::Room))==-5,"Stack Room script exposure");
    need(negative(Scope::MyObject)&&negative(Scope::StackObject),"authored Room<0 rejects no off-world entity");
    context.UnregisterObjectPos(&entity);entity.Position(Pos::FromBigTile(2,2,1));context.RegisterObjectPos(&entity);
    need(entity.GetValue(V::Room)==0&&!negative(Scope::StackObject),"valid outdoor room0 became negative");
    context.UnregisterObjectPos(&entity);entity.Position(Pos::OUT_OF_WORLD);entity.PositionChange(&context,true);
    need(entity.GetValue(V::Room)==-5&&context.GetObjectRoom(&entity)==0,"position-change out-of-world publication");
    vm.TS1=false;entity.SetRoom(0);
    need(entity.GetValue(V::Room)==0&&!negative(Scope::MyObject),"non-TS1 room policy changed");
    vm.TS1=true;entity.SetRoom(0);entity.SetAttribute(0,77);
    // Recreate an old native overlay whose bank overwrites the positioned room.
    // No main/placement/Init/Reset is executed by this capture or publication.
    entity.ObjectData[int(V::Room)]=0;
    IffRawFile captured;captured.Set("ObjM",1,{1,2,3});
    VMTS1InteractionCheckpoint::Capture(vm,captured,false);
    IffFile house;auto decoded=new OBJM();decoded->ChunkID=1;decoded->ChunkProcessed=true;
    decoded->Decoded=true;decoded->Version=0x49;decoded->SourceSignature=FSO::Files::Hash::CalculateHash(captured.Find("ObjM",1)->Data);
    auto entry=type(definition.GUID);auto instance=std::make_shared<OBJMInstance>();instance->OBJT=&entry;
    instance->UnknownInt=int32_t(entity.TS1SchedulerState);decoded->ObjectData[entity.ObjectID].Instance=instance;house.AddChunk(decoded);
    auto native=new ODHI();native->ChunkID=1;native->ChunkProcessed=true;native->Data=captured.Find("ODHI",1)->Data;house.AddChunk(native);
    const auto id=entity.ObjectID;const auto count=vm.Entities.size();
    for(uint8_t version:{uint8_t(8),uint8_t(9)}){
        native->Data[0]=version;entity.ObjectData[int(V::Room)]=0;
        VMTS1InteractionCheckpoint::Restore(vm,house);
        need(entity.GetValue(V::Room)==-5&&entity.ObjectData[int(V::Room)]==-5,"native old raw room0 replaced the script sentinel");
        need(entity.ObjectID==id&&vm.Entities.size()==count&&entity.GetAttribute(0)==77&&entity.Thread->Stack.empty(),"room migration changed identity/state or started Main");
    }
    instance->Level=1;instance->ObjectData.assign(entity.ObjectData.begin(),
        entity.ObjectData.begin()+OBJMInstance::ObjectDataCount);
    instance->ObjectData[int(V::ObjectId)]=id;
    IffFieldEncodeWriter encoded;WriteOBJMInstance(*instance,0x49,encoded,false);
    st::Reader bytes(encoded.Data);IffFieldEncode fields(&bytes);OBJMInstance roundtrip;
    roundtrip.Read(0x49,fields,[&](int16_t){OBJMResource r;r.OBJT=&entry;return r;});
    need(roundtrip.ObjectData[int(V::Room)]==-5,"ordinary classic wire lost negative room");
    std::printf("PASS %d off-world room checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
