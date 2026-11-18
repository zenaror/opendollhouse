// SPDX-License-Identifier: MPL-2.0
// Real registration/removal/sandbox and compressed-record order controls.
// No retail content, simulation tick, or display is required.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMGameObject.h"
#include "files/formats/iff/chunks/OBJMWriter.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace {
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void Reject(F action,const char* why){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}Check(rejected,why);}
OBJTEntry Type();
struct Fixture {
    VMContext Context{nullptr};
    VM Machine{&Context,nullptr};
    IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"manager-order",true};
    OBJD Definition;
    FSO::Content::GameObject Object;
    Fixture(){Definition.GUID=0xf093a511;Object.OBJ=&Definition;Object.GUID=Definition.GUID;Object.Resource=&Resource;}
    VMEntity* Birth(int16_t id,bool ghost=false){
        auto* entity=Machine.OwnEntity(new VMGameObject(&Object,nullptr));
        entity->GhostImage=ghost;
        Machine.PendingIDsByGUID[Definition.GUID]=id;
        Machine.AddEntity(entity);
        Check(entity->ObjectID==id,"saved fixture ID not assigned");return entity;
    }
    OBJM Classic(std::vector<int32_t> ids){
        OBJM result;result.Decoded=true;result.RecordOrder=ids;
        for(auto id:ids){OBJM::MappedObject object;object.ObjectID=id;object.GUID=Definition.GUID;result.ObjectData.emplace(id,object);}return result;
    }
};
void Lifecycle(){
    Fixture f;
    auto* a=f.Birth(20);Check(!f.Machine.TS1ObjectManagerOrderKnown&&f.Machine.TS1ObjectManagerOrder.empty(),"unknown synthetic VM invented manager provenance");
    auto saved=f.Classic({20});Check(f.Machine.EstablishTS1ObjectManagerOrderFromClassic(saved),"complete classic order was not installed");
    auto* b=f.Birth(4);auto* c=f.Birth(15);auto* ghost=f.Birth(9,true);
    Check(f.Machine.TS1ObjectManagerOrder==std::vector<VMEntity*>{c,b,a},"birth order was sorted by ID instead of prepended");
    Check(std::is_sorted(f.Machine.Entities.begin(),f.Machine.Entities.end(),[](auto* x,auto* y){return x->ObjectID<y->ObjectID;}),"manager order changed dispatch sorting");
    f.Machine.ValidateTS1ObjectManagerOrder();
    saved=f.Classic({20,4});Check(f.Machine.EstablishTS1ObjectManagerOrderFromClassic(saved),"saved suffix not restored");
    Check(f.Machine.TS1ObjectManagerOrder==std::vector<VMEntity*>{c,a,b},"saved suffix/fresh prefix merge changed order");
    Check(std::find(f.Machine.TS1ObjectManagerOrder.begin(),f.Machine.TS1ObjectManagerOrder.end(),ghost)==f.Machine.TS1ObjectManagerOrder.end(),"ghost entered persistent manager order");
    Check(!f.Machine.HasTS1ObjectIdentity(9,f.Definition.GUID),"owned ghost became historical deletion evidence");
    f.Machine.RemoveEntity(ghost);
    Check(!f.Machine.HasTS1ObjectIdentity(9,f.Definition.GUID),"ghost removal created genuine deletion evidence");
    auto immutableType=Type();saved.ObjectData[20].Instance=std::make_shared<OBJMInstance>();
    saved.ObjectData[20].Instance->OBJT=&immutableType;saved.ObjectData[20].GUID=0xf00dbad;
    Check(f.Machine.EstablishTS1ObjectManagerOrderFromClassic(saved),"mutable mapped master GUID replaced immutable saved part identity");
    Check(f.Machine.QueueTS1Removal(b,true),"paused script removal did not queue");
    f.Machine.ValidateTS1ObjectManagerOrder();Check(f.Machine.TS1ObjectManagerOrder.back()==b,"queued live removal disappeared before drain");
    auto sandbox=f.Machine.Sandbox();Check(f.Machine.TS1ObjectManagerOrderKnown&&f.Machine.TS1ObjectManagerOrder.empty(),"sandbox inherited live order");
    auto* isolated=f.Birth(2);Check(f.Machine.TS1ObjectManagerOrder==std::vector<VMEntity*>{isolated},"isolated birth order missing");
    f.Machine.RemoveEntity(isolated);
    Check(f.Machine.HasTS1ObjectIdentity(2,f.Definition.GUID),"genuine sandbox removal lacked isolated tombstone");
    f.Birth(3);
    f.Machine.SandboxRestore(std::move(sandbox));
    Check(f.Machine.TS1ObjectManagerOrder==std::vector<VMEntity*>{c,a,b}&&f.Machine.HasTS1PendingRemovals(),"sandbox restore lost order or pending removal");
    Check(!f.Machine.HasTS1ObjectIdentity(2,f.Definition.GUID),"sandbox tombstone leaked into historical deletion evidence");
    Check(!f.Machine.HasTS1ObjectIdentity(3,f.Definition.GUID),"discarded live sandbox identity leaked into outer history");
    f.Machine.RemoveEntity(c);Check(f.Machine.TS1ObjectManagerOrder==std::vector<VMEntity*>{a,b},"removal did not unlink exact instance");
    Check(f.Machine.HasTS1ObjectIdentity(15,f.Definition.GUID),"removed owned historical identity was forgotten");
    auto* reused=f.Birth(15);Check(reused!=c&&f.Machine.TS1ObjectManagerOrder.front()==reused,"reused ID inherited removed manager position");
    f.Machine.ValidateTS1ObjectManagerOrder();
    const auto original=f.Machine.TS1ObjectManagerOrder;f.Machine.TS1ObjectManagerOrder.push_back(a);
    Reject([&]{f.Machine.ValidateTS1ObjectManagerOrder();},"duplicate order was accepted");f.Machine.TS1ObjectManagerOrder=original;
    auto malformed=f.Classic({20,20});Reject([&]{f.Machine.EstablishTS1ObjectManagerOrderFromClassic(malformed);},"duplicate classic record order accepted");
    Check(f.Machine.TS1ObjectManagerOrder==original,"rejected classic order changed live order");
}
void Unknown(){
    Fixture f;f.Birth(20);f.Birth(4);auto saved=f.Classic({20});
    Check(!f.Machine.EstablishTS1ObjectManagerOrderFromClassic(saved)&&!f.Machine.TS1ObjectManagerOrderKnown&&f.Machine.TS1ObjectManagerOrder.empty(),"unknown extra births gained provenance from ID order");
    saved=f.Classic({4,20});Check(f.Machine.EstablishTS1ObjectManagerOrderFromClassic(saved),"complete disk domain did not establish authoritative order");
    saved=f.Classic({4,20,31});Check(!f.Machine.EstablishTS1ObjectManagerOrderFromClassic(saved)&&f.Machine.TS1ObjectManagerOrder.empty(),"missing historical record claimed a complete original manager");
}
OBJTEntry Type(){
    st::Writer out(st::Endian::Little);out.WriteUInt32(0xf093a511);for(int i=0;i<4;++i)out.WriteUInt16(0);
    out.WriteUInt16(1);out.WriteUInt16(uint16_t(OBJDType::Normal));for(auto c:std::string("fixture"))out.WriteByte(c);out.WriteByte(0);
    auto bytes=out.TakeArray();st::Reader input(bytes);return OBJTEntry(input,2);
}
void RecordOrder(){
    auto type=Type();const auto resource=[&](uint16_t){OBJMResource r;r.OBJT=&type;return r;};
    const auto record=[&](int id){OBJMRecordOut r;r.ObjectID=id;r.TypeID=1;r.HasLinkedSlot=false;r.Instance.OBJT=&type;r.Instance.Level=1;
        r.Instance.ObjectData.resize(OBJMInstance::ObjectDataCount);r.Instance.ObjectData[11]=id;r.Instance.StackFlags=512;return r;};
    const auto decode=[&](OBJM& objects,const std::vector<OBJMRecordOut>& records){auto bytes=WriteOBJMChunk(0,0x33,records,{});st::Reader input(bytes);objects.Read(nullptr,input);return objects.Prepare(resource);};
    OBJM objects;Check(decode(objects,{record(20),record(4),record(15)}),"compressed fixture could not reopen");
    Check(objects.RecordOrder==std::vector<int32_t>{20,4,15}&&objects.ObjectData.begin()->first==4,"compressed record order collapsed into map order");
    Check(!decode(objects,{record(20),record(20)})&&!objects.Decoded&&objects.RecordOrder.empty(),"duplicate compressed records inherited prior order");
    Check(decode(objects,{})&&objects.RecordOrder.empty()&&objects.ObjectData.empty(),"empty snapshot retained stale manager order");
}
}
int main(){try{VM::UseWorld(false);Lifecycle();Unknown();RecordOrder();std::printf("PASS TS1 object manager order: %d checks\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
