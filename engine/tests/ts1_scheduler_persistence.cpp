// SPDX-License-Identifier: MPL-2.0
// Synthetic wire/provenance and atomic-publication contracts. Actual lot
// acceptance remains a separate App save/reopen witness on coherent libraries.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include "simantics/utils/VMTS1SchedulerState.h"
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
static int checks = 0;
static void need(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); ++checks; }
template<class F> static void rejects(F action, const char* why) {
    bool caught = false; try { action(); } catch (const std::runtime_error&) { caught = true; }
    need(caught, why);
}
static OBJTEntry type(uint32_t guid) {
    st::Writer bytes(st::Endian::Little); bytes.WriteUInt32(guid);
    for (int i=0;i<4;++i) bytes.WriteUInt16(0);
    bytes.WriteUInt16(1); bytes.WriteUInt16(uint16_t(OBJDType::Normal));
    bytes.WriteByte('x'); bytes.WriteByte(0);
    auto raw=bytes.TakeArray(); st::Reader input(raw); return OBJTEntry(input,2);
}
static void wire() {
    auto definition=type(0x12345678);
    OBJMInstance instance; instance.Level=1; instance.OBJT=&definition;
    instance.ObjectData.resize(OBJMInstance::ObjectDataCount); instance.ObjectData[11]=1;
    auto decode=[&](const std::vector<uint8_t>& data) {
        st::Reader bytes(data); IffFieldEncode fields(&bytes); OBJMInstance result;
        result.Read(0x49,fields,[&](int16_t){OBJMResource r;r.OBJT=&definition;return r;});
        return result;
    };
    for (uint32_t word : {0u,0x80000u,0x100a3u,0x9fffeu,0xfedcffffu}) {
        instance.UnknownInt=static_cast<int32_t>(word);
        IffFieldEncodeWriter encoded; WriteOBJMInstance(instance,0x49,encoded,false);
        const auto restored=decode(encoded.Data);
        need(restored.HasSchedulerStateTail && uint32_t(restored.UnknownInt)==word,
             "ordinary tail lost a zero, disabled, countdown or high-bit word");
    }
    instance.UnknownInt=0x12345678;
    IffFieldEncodeWriter encoded; WriteOBJMInstance(instance,0x49,encoded,false);
    need(encoded.Data.size()>4,"wire fixture too short"); encoded.Data.resize(encoded.Data.size()-4);
    rejects([&]{decode(encoded.Data);},"ordinary truncated tail became an invented zero");
    std::vector<uint8_t> shortField{0x80}; st::Reader bytes(shortField); IffFieldEncode fields(&bytes);
    rejects([&]{fields.ReadInt32Strict();},"truncated strict field accepted");
    instance.UnknownInt=0;
    need(TS1SavedSchedulerState(instance,0x49,8)==0x1ffffu,"old native stale tail disabled an actor");
    need(TS1SavedSchedulerState(instance,0x49,9)==0,"v9 bound zero word spuriously enabled an actor");
    instance.UnknownInt=static_cast<int32_t>(0xabcd1234u);
    need(TS1SavedSchedulerState(instance,0x38)==0x11234u,"old source normalization lost the signed low word");
    instance.HasSchedulerStateTail=false;
    need(TS1SavedSchedulerState(instance,0x49)==0x1ffffu,"observed absent-tail fallback disabled a placeholder");
    need(TS1SavedSchedulerState(instance,0x35)==0x1ffffu,"unproven old layout consumed manager bits");
}
static void provenance() {
    VM::UseWorld(false); VMContext context(nullptr); VM vm(&context,nullptr);
    vm.GlobalState.resize(64,0);
    IffFile resources; FSO::Content::GameObjectResource resource(&resources,nullptr,nullptr,"scheduler",true);
    OBJD definition; definition.GUID=0x12345678;
    FSO::Content::GameObject object; object.OBJ=&definition; object.Resource=&resource;
    VMGameObject entity(&object,nullptr); Entities::VMMultitileGroup group;
    entity.MultitileGroup=&group; group.AddObject(&entity); vm.AddEntity(&entity);
    entity.Thread=std::make_unique<VMThread>(&context,&entity,16);
    IffRawFile raw; raw.Set("ObjM",1,{1,2,3});
    VMTS1InteractionCheckpoint::Capture(vm,raw,false);
    auto native=raw.Find("ODHI",1)->Data;
    need(native[0]==8,"unbound execution overlay claimed scheduler provenance");
    rejects([&]{VMTS1InteractionCheckpoint::Capture(vm,raw,false,true);},
            "bound capture accepted missing classic type graph");
    need(raw.Find("ODHI",1)->Data==native,"rejected binding changed the old supplement");
    IffFile house;
    auto objm=new OBJM(); objm->ChunkID=1; objm->ChunkProcessed=true; objm->Decoded=true; objm->Version=0x49;
    objm->SourceSignature=FSO::Files::Hash::CalculateHash(raw.Find("ObjM",1)->Data);
    auto entry=type(definition.GUID); auto instance=std::make_shared<OBJMInstance>();
    instance->OBJT=&entry; instance->UnknownInt=0x80000;
    objm->ObjectData[entity.ObjectID].Instance=instance; house.AddChunk(objm);
    auto chunk=new ODHI(); chunk->ChunkID=1; chunk->ChunkProcessed=true; chunk->Data=native; house.AddChunk(chunk);
    entity.TS1SchedulerState=0;
    VMTS1InteractionCheckpoint::Restore(vm,house);
    need(entity.TS1SchedulerState==0x1ffffu,"v8 restore consumed stale source admission");
    chunk->Data[0]=9;
    VMTS1InteractionCheckpoint::Restore(vm,house);
    need(entity.TS1SchedulerState==0x80000u,"v9 failed to bind disabled manager word");
    auto thread=entity.Thread.get(); entity.TS1SchedulerState=0xabc10005u;
    entry.GUID^=1;
    rejects([&]{VMTS1InteractionCheckpoint::Restore(vm,house);},"corrupt word identity accepted");
    need(entity.Thread.get()==thread && entity.TS1SchedulerState==0xabc10005u,
         "corrupt binding partially published a thread or manager word");
    entry.GUID=definition.GUID; chunk->Data[0]=10;
    rejects([&]{VMTS1InteractionCheckpoint::Restore(vm,house);},"future native version accepted");
    need(entity.Thread.get()==thread && entity.TS1SchedulerState==0xabc10005u,
         "bad version partially published state");
}
int main() { try { wire(); provenance(); std::printf("PASS %d scheduler persistence checks\n",checks); return 0; }
    catch (const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; } }
