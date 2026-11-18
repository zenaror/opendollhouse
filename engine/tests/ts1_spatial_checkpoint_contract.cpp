// SPDX-License-Identifier: MPL-2.0
// Staged source-cache codec controls; no game assets, tick, rendering or files.
#include "simantics/utils/VMTS1SpatialCheckpoint.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine::Utils;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace {
int checks=0;
void Check(bool good,const char* why){++checks;if(!good)throw std::runtime_error(why);}
template<class F>void Reject(F f,const char* why){bool failed=false;try{f();}catch(const std::exception&){failed=true;}Check(failed,why);}
// Same scalar/count contract as the private production Archive, kept local so
// this header remains independent of the entire interaction codec translation unit.
struct MiniArchive {
    bool out=true,in=false;std::vector<uint8_t> Bytes;size_t Position=0;
    MiniArchive()=default;
    explicit MiniArchive(std::vector<uint8_t> data):out(false),in(true),Bytes(std::move(data)){}
    template<class T>void value(T& v){
        if constexpr(std::is_same_v<T,bool>){uint8_t b=v;value(b);Check(b<=1,"invalid encoded boolean");if(in)v=b!=0;}
        else{using U=std::conditional_t<sizeof(T)==1,uint8_t,std::conditional_t<sizeof(T)==2,uint16_t,std::conditional_t<sizeof(T)==4,uint32_t,uint64_t>>>;U bits=0;
            if(out){std::memcpy(&bits,&v,sizeof v);for(size_t i=0;i<sizeof v;++i)Bytes.push_back(uint8_t(bits>>(8*i)));}
            else{Check(Bytes.size()-Position>=sizeof v,"truncated scalar");for(size_t i=0;i<sizeof v;++i)bits|=U(Bytes[Position++])<<(8*i);std::memcpy(&v,&bits,sizeof v);}
            if constexpr(std::is_floating_point_v<T>)Check(std::isfinite(v),"nonfinite scalar");}
    }
    uint32_t count(size_t size,uint32_t maximum){Check(size<=maximum,"writer collection too large");uint32_t n=uint32_t(size);value(n);Check(n<=maximum,"reader collection too large");return n;}
};
struct Fixture {
    VMContext Context{nullptr};VM Machine{&Context,nullptr};IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"spatial-checkpoint",true};
    OBJD Definition;FSO::Content::GameObject Object;
    VMAvatar* Actor;VMAvatar* Other;VMEntity* Lamp;
    Fixture(){Definition.GUID=0xf0200521;Object.OBJ=&Definition;Object.GUID=Definition.GUID;Object.Resource=&Resource;
        Context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&Context));
        Actor=static_cast<VMAvatar*>(Machine.OwnEntity(new VMAvatar(&Object)));
        Other=static_cast<VMAvatar*>(Machine.OwnEntity(new VMAvatar(&Object)));
        Lamp=Machine.OwnEntity(new VMGameObject(&Object,nullptr));
        Machine.AddEntity(Actor);Machine.AddEntity(Other);Machine.AddEntity(Lamp);
        for(auto* entity : std::array<VMEntity*,3>{Actor,Other,Lamp})
            entity->Thread=std::make_unique<Engine::VMThread>(&Context,entity,16);
        Context.SpatialLights.Initialize(8,8,4);for(auto& heads:Context.SpatialTileHeads)heads.assign(64,0);
        Context.EnvironmentRooms.resize(2);Context.EnvironmentRooms[1].CanonicalID=7;Context.EnvironmentRooms[1].Story=1;
        Context.EnvironmentRooms[1].SpatialLightFactor=.4f;Context.EnvironmentRooms[1].SpatialPeople=2;Context.EnvironmentRooms[1].SpatialLamps=3;
        Context.SpatialLights.Maps[0][10].Slots[0]={-4,4};Context.SpatialLights.Maps[0][10].Slots[1]={127,-128};
        Context.SpatialSchedule.Dirty[2047]=1;Context.SpatialSchedule.StoryMask=2;Context.SpatialPeriod=1;
        Context.VisualOverhead[2001]=1;Context.VisualRoomFlags[2001]=3;
        Context.VisualActorRooms.emplace(Actor,VMContext::VisualActorRoom{2001,true}); // inactive record remains valid
        Context.SpatialPlacements.emplace(Lamp,VMContext::SpatialPlacement{-5,false});
        Context.SpatialTileHeads[1][19]=Lamp->ObjectID;
        Check(Machine.QueueTS1Removal(Lamp,true)&&Machine.QueueTS1Removal(Actor,true),
              "fixture did not queue still-live placement/head/actor identities");
    }
    template<class A>void Ref(A& io,VMEntity*& entity){int16_t id=entity?entity->ObjectID:0;uint32_t guid=entity?entity->Object->OBJ->GUID:0;
        io.value(id);io.value(guid);if(io.in)entity=id?Machine.GetObjectById(id):nullptr;
        Check(id==0?guid==0:entity&&entity->Object->OBJ->GUID==guid,"unresolved staged identity");}
    std::vector<uint8_t> Write(VMTS1SpatialCheckpoint::State state){MiniArchive io;VMTS1SpatialCheckpoint::Fields(io,Machine,state,[&](VMEntity*& e,const char*){Ref(io,e);});return io.Bytes;}
    VMTS1SpatialCheckpoint::State Read(const std::vector<uint8_t>& bytes,bool alias=false){MiniArchive io(bytes);VMTS1SpatialCheckpoint::State state;
        VMTS1SpatialCheckpoint::Fields(io,Machine,state,[&](VMEntity*& e,const char* label){Ref(io,e);if(alias&&std::string(label)=="spatial actor-room key")e=Actor;});Check(io.Position==bytes.size(),"unconsumed spatial bytes");return state;}
};
void Contracts(){
 Fixture f;auto saved=VMTS1SpatialCheckpoint::Capture(f.Machine);const auto bytes=f.Write(saved);auto restored=f.Read(bytes);
 const auto pending=f.Machine.GetTS1PendingRemovalIDs();
 Check(pending.size()==2,"queued live reference coverage missing");
 Check(f.Write(restored)==bytes,"spatial state is not byte-stable on reopen");
 Check(restored.Lights.Maps[0][10].Slots[1].Dy==-128,"empty sentinel's uninterpreted Dy was normalized");
 Check(restored.Schedule.Dirty[2047]==1&&restored.Schedule.StoryMask==2&&restored.Overhead[2001]==1,"inactive record state lost");
 auto bad=saved;bad.Lights.Width=128;Reject([&]{f.Write(bad);},"oversized grid admitted");
 bad=saved;bad.Lights.Maps[0].pop_back();Reject([&]{f.Write(bad);},"wrong source map size admitted");
 bad=saved;bad.Lights.Maps[0][10].Slots[0].Dx=5;Reject([&]{f.Write(bad);},"source outside radius admitted");
 bad=saved;bad.TileHeads[0].pop_back();Reject([&]{f.Write(bad);},"wrong head grid size admitted");
 bad=saved;bad.TileHeads[0][0]=30000;Reject([&]{f.Write(bad);},"unresolved tile-head identity admitted");
 bad=saved;bad.Schedule.Dirty[1]=2;Reject([&]{f.Write(bad);},"nonboolean dirty byte admitted");
 bad=saved;bad.Schedule.StoryMask=4;Reject([&]{f.Write(bad);},"invalid story mask admitted");
 bad=saved;bad.RoomFlags[0]=4;Reject([&]{f.Write(bad);},"unknown visual room flags admitted");
 bad=saved;bad.ActorRooms[f.Actor].Room=-1;Reject([&]{f.Write(bad);},"noncanonical actor room admitted");
 bad=saved;bad.ActorRooms.emplace(f.Lamp,VMContext::VisualActorRoom{0,false});Reject([&]{f.Write(bad);},"nonavatar actor-room key admitted");
 bad=saved;bad.ActorRooms.emplace(f.Other,VMContext::VisualActorRoom{0,false});const auto aliased=f.Write(bad);Reject([&]{f.Read(aliased,true);},"duplicate resolved actor key admitted");
 bad=saved;bad.Rooms[1].CanonicalID=8;Reject([&]{f.Write(bad);},"changed room geometry admitted");
 bad=saved;bad.Rooms[1].Lamps=-1;Reject([&]{f.Write(bad);},"negative cached lamp count admitted");
 auto cut=bytes;cut.pop_back();Reject([&]{f.Read(cut);},"truncated state admitted");
 Check(f.Write(VMTS1SpatialCheckpoint::Capture(f.Machine))==bytes,"failed preparation changed live caches");
 f.Context.SpatialLights.Maps[0][10].Slots[0]={0,0};f.Context.SpatialSchedule.StoryMask=0;f.Context.VisualActorRooms.clear();
 f.Context.SpatialSnapshotDirty=false;f.Context.VisualLightingDirty=false;const auto passes=f.Context.SpatialCollectorPasses;
 VMTS1SpatialCheckpoint::Publish(f.Machine,std::move(restored));
 Check(f.Write(VMTS1SpatialCheckpoint::Capture(f.Machine))==bytes,"publish changed source cache values");
 Check(f.Context.SpatialSnapshotDirty&&f.Context.VisualLightingDirty&&f.Context.SpatialCollectorPasses==passes,"publish collected rooms or failed GPU invalidation");
 Check(f.Machine.GetTS1PendingRemovalIDs()==pending&&!f.Actor->Dead&&!f.Lamp->Dead,
       "spatial staging/publication drained or changed pending live identities");
}
}
int main(){try{VM::UseWorld(false);Contracts();std::printf("PASS TS1 spatial checkpoint: %d checks\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
