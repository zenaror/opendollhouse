// SPDX-License-Identifier: MPL-2.0
// Synthetic wire/provenance and atomic-publication contracts. Actual lot
// acceptance remains a separate App save/reopen witness on coherent libraries.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/VMArchitecture.h"
#include "simantics/primitives/VMGenericTS1Call.h"
#include "simantics/utils/VMTS1LotSaver.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include "simantics/utils/VMTS1SchedulerState.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/IffFieldEncode.h"
#include "files/formats/iff/chunks/OBJMWriter.h"
#include "files/formats/iff/chunks/ODHI.h"
#include "files/formats/iff/chunks/ODMP.h"
#include "files/formats/iff/chunks/OBJT.h"
#include "files/Hash.h"
#include "content/ContentManager.h"
#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <chrono>
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
    VMTS1InteractionCheckpoint::Capture(vm,raw,false,false,10);
    auto native=raw.Find("ODHI",1)->Data;
    need(native[0]==10,"unbound execution overlay claimed scheduler provenance");
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
    need(native[9]==0,"direct capture claimed scheduler provenance");
    entity.TS1SchedulerState=0;
    VMTS1InteractionCheckpoint::Restore(vm,house);
    need(entity.TS1SchedulerState==0x1ffffu,"v8 restore consumed stale source admission");
    // Reconstruct the legacy no-departure v8/v9 header: v10 adds one flag.
    chunk->Data.erase(chunk->Data.begin()+9); chunk->Data[0]=9;
    VMTS1InteractionCheckpoint::Restore(vm,house);
    need(entity.TS1SchedulerState==0x80000u,"v9 failed to bind disabled manager word");
    auto thread=entity.Thread.get(); entity.TS1SchedulerState=0xabc10005u;
    entry.GUID^=1;
    rejects([&]{VMTS1InteractionCheckpoint::Restore(vm,house);},"corrupt word identity accepted");
    need(entity.Thread.get()==thread && entity.TS1SchedulerState==0xabc10005u,
         "corrupt binding partially published a thread or manager word");
    entry.GUID=definition.GUID; chunk->Data[0]=13;
    rejects([&]{VMTS1InteractionCheckpoint::Restore(vm,house);},"future native version accepted");
    need(entity.Thread.get()==thread && entity.TS1SchedulerState==0xabc10005u,
         "bad version partially published state");
}

// An unchanged pose must not make the classic writer preserve a stale larger
// source rectangle. This drives primitive15, the real lot saver and native
// checkpoint restoration into a separately constructed VM; no game assets used.
static void samePoseFootprint() {
    using namespace FSO::LotView::Model;
    using namespace FSO::SimAntics::Model;
    using namespace FSO::SimAntics::Primitives;
    VM::UseWorld(false);
    VMContext ctx(nullptr); VM vm(&ctx,nullptr); vm.GlobalState.resize(64,0);
    ctx.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&ctx));
    ctx.Architecture()->Rooms[0].Map.assign(64,0); ctx.RoomInfo.resize(1);
    IffFile resources; FSO::Content::GameObjectResource resource(&resources,nullptr,nullptr,"same-pose",true);
    OBJD definition; definition.GUID=0x42bbaacc; definition.ObjectType=OBJDType::Person;
    FSO::Content::GameObject object; object.OBJ=&definition; object.GUID=definition.GUID; object.Resource=&resource;
    VMAvatar actor(&object); Entities::VMMultitileGroup group;actor.MultitileGroup=&group;group.AddObject(&actor);
    vm.AddEntity(&actor);actor.Thread=std::make_unique<VMThread>(&ctx,&actor,16);
    const LotTilePos pose(64,64,1);actor.Position(pose);actor.Direction(Direction::NORTH);
    actor.SetPersonData(VMPersonDataVariable::FootprintExtension,8);actor.RefreshTS1Footprint();
    need(actor.Footprint&&actor.Footprint->x1==61&&actor.Footprint->y1==53&&actor.Footprint->x2==67&&actor.Footprint->y2==67,"fixture starts with extended bounds");
    // The imported classic record has the same pose/direction and larger cache.
    st::Writer typeBytes(st::Endian::Little);typeBytes.WriteUInt32(0);typeBytes.WriteUInt32(2);typeBytes.WriteUInt32(0x6f626a74);
    typeBytes.WriteUInt32(definition.GUID);for(int i=0;i<4;++i)typeBytes.WriteUInt16(0);
    typeBytes.WriteUInt16(1);typeBytes.WriteUInt16(uint16_t(OBJDType::Person));typeBytes.WriteByte('x');typeBytes.WriteByte(0);
    auto typeData=typeBytes.ToArray();st::Reader typeInput(typeData);OBJT types;types.Read(nullptr,typeInput);
    OBJMRecordOut record;record.ObjectID=actor.ObjectID;record.TypeID=1;record.Instance.OBJT=&types.Entries[0];
    record.Instance.X=pose.x;record.Instance.Y=pose.y;record.Instance.Level=pose.Level;
    record.Instance.Footprint.MinX=61;record.Instance.Footprint.MinY=53;record.Instance.Footprint.MaxX=67;record.Instance.Footprint.MaxY=67;
    record.Instance.UnknownData=0x57;record.Instance.ObjectData.resize(OBJMInstance::ObjectDataCount);record.Instance.ObjectData[1]=0;record.Instance.ObjectData[11]=actor.ObjectID;
    record.Instance.HasPersonData=true;record.Instance.PersonData.PersonData.resize(256);record.Instance.PersonData.PersonData[73]=8;
    IffRawFile source;source.Set("objt",0,typeData);source.Set("ObjM",1,WriteOBJMChunk(0,0x49,{record},{}));
    const auto dir=std::filesystem::temp_directory_path()/("odh-same-pose-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    need(std::filesystem::create_directory(dir),"create isolated synthetic fixture");
    struct RemoveFixture {std::filesystem::path path;~RemoveFixture(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{dir};
    const auto sourcePath=dir/"source.iff";std::string error;need(source.Save(sourcePath.string(),&error),"write synthetic classic source");
    VMStackFrame call;call.Caller=&actor;call.Thread=actor.Thread.get();actor.Thread->TempRegisters[0]=0;
    VMGenericTS1Call::Execute(&call,VMGenericTS1CallMode::MyRoutingFootprintEqualsTemp0);
    need(actor.Position()==pose&&actor.Direction()==Direction::NORTH&&actor.GetPersonData(VMPersonDataVariable::FootprintExtension)==0&&
         actor.Footprint&&actor.Footprint->x1==61&&actor.Footprint->y1==61&&actor.Footprint->x2==67&&actor.Footprint->y2==67,
         "primitive15 zero shrinks cache without changing pose");
    VMTS1LotSaver saver(&vm);std::vector<uint8_t> bytes;
    if(!saver.BuildHouse(sourcePath.string(),bytes))throw std::runtime_error("synthetic lot save: "+saver.Error);
    IffRawFile saved;need(saved.Parse(bytes,&error),"decode saved IFF");
    auto rawObjm=saved.Find("ObjM",1);auto rawOdhi=saved.Find("ODHI",1);need(rawObjm&&rawOdhi&&rawOdhi->Data[0]==12,"bound save emitted ObjM and ODHI12");
    st::Reader savedTypesInput(saved.Find("objt",0)->Data);OBJT savedTypes;savedTypes.Read(nullptr,savedTypesInput);
    auto objm=new OBJM();objm->ChunkID=1;objm->ChunkProcessed=true;st::Reader objmInput(rawObjm->Data);objm->Read(nullptr,objmInput);
    need(objm->Prepare([&](uint16_t id){OBJMResource result;for(auto& t:savedTypes.Entries)if(t.TypeID==id){result.OBJT=&t;result.OBJD=&definition;}return result;}),"decode actual saved classic object");
    auto savedRecord=objm->ObjectData.find(actor.ObjectID);need(savedRecord!=objm->ObjectData.end()&&savedRecord->second.Instance,"saved avatar record exists");
    const auto& stored=*savedRecord->second.Instance;need(stored.X==pose.x&&stored.Y==pose.y&&stored.Level==pose.Level&&stored.UnknownData==0x57&&
        stored.Footprint.MinX==61&&stored.Footprint.MinY==61&&stored.Footprint.MaxX==67&&stored.Footprint.MaxY==67,
        "classic store publishes shrunk rectangle at unchanged pose and preserves unknown data");
    IffFile house;house.AddChunk(objm);auto odhi=new ODHI();odhi->ChunkID=1;odhi->ChunkProcessed=true;odhi->Data=rawOdhi->Data;house.AddChunk(odhi);
    if(auto marker=saved.Find("ODMP",1)){auto odmp=new ODMP();odmp->ChunkID=1;odmp->ChunkProcessed=true;odmp->Data=marker->Data;house.AddChunk(odmp);}
    VMContext reopenedCtx(nullptr);VM reopened(&reopenedCtx,nullptr);reopened.GlobalState.resize(64,0);
    reopenedCtx.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&reopenedCtx));reopenedCtx.Architecture()->Rooms[0].Map.assign(64,0);reopenedCtx.RoomInfo.resize(1);
    VMAvatar loaded(&object);Entities::VMMultitileGroup loadedGroup;loaded.MultitileGroup=&loadedGroup;loadedGroup.AddObject(&loaded);reopened.AddEntity(&loaded);
    loaded.Thread=std::make_unique<VMThread>(&reopenedCtx,&loaded,16);loaded.Position(pose);loaded.Direction(Direction::NORTH);loaded.RestoreTS1Footprint(61,53,67,67);
    VMTS1InteractionCheckpoint::Restore(reopened,house);
    need(loaded.Position()==pose&&loaded.Direction()==Direction::NORTH&&loaded.GetPersonData(VMPersonDataVariable::FootprintExtension)==0&&
         loaded.Footprint&&loaded.Footprint->x1==61&&loaded.Footprint->y1==61&&loaded.Footprint->x2==67&&loaded.Footprint->y2==67,
         "bound native restore reinstates exact saved cache without movement");
}
int main() { try { wire(); provenance(); samePoseFootprint(); std::printf("PASS %d scheduler persistence checks\n",checks); return 0; }
    catch (const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; } }
