// SPDX-License-Identifier: MPL-2.0
// Primitive/request ownership controls; actual motion and DVD timing require
// the separate paired household journey.
#include "simantics/primitives/VMLookTowards.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace {
int checks=0;
void Check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); ++checks; }
template<class F> void Reject(F f,const char* why) {
    bool rejected=false;try {f();}catch(const std::exception&) {rejected=true;}Check(rejected,why);
}
struct Fixture {
    VMContext Context{nullptr};VM Machine{&Context,nullptr};IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"body-facing",true};
    OBJD Def;FSO::Content::GameObject Object;
    std::unique_ptr<VMAvatar> Actor;std::unique_ptr<VMGameObject> Target;
    VMRoutine Routine;std::shared_ptr<VMStackFrame> Frame;
    VMLookTowards Primitive;VMLookTowardsOperand Operand;
    Fixture() {
        Machine.TS1=true;Def.GUID=0x12345678;Object.OBJ=&Def;Object.GUID=Def.GUID;Object.Resource=&Resource;
        Actor=std::make_unique<VMAvatar>(&Object);Target=std::make_unique<VMGameObject>(&Object,nullptr);
        Actor->ObjectID=11;Target->ObjectID=12;
        Actor->Thread=std::make_unique<VMThread>(&Context,Actor.get(),16);
        Machine.AddEntity(Actor.get());Machine.AddEntity(Target.get());
        Routine.ID=4096;Routine.Instructions.resize(1);Routine.Instructions[0].Opcode=22;
        Frame=std::make_shared<VMStackFrame>();Frame->Thread=Actor->Thread.get();
        Frame->Caller=Actor.get();Frame->Callee=Actor.get();Frame->StackObject=Target.get();
        Frame->CodeOwner=&Object;Frame->Routine=&Routine;Actor->Thread->Stack.push_back(Frame);
        Actor->SetPersonData(Model::VMPersonDataVariable::Posture,0);
    }
    VMPrimitiveExitCode Run(int mode) {
        Operand.Mode=static_cast<VMLookTowardsMode>(mode);return Primitive.Execute(Frame.get(),&Operand);
    }
};
}
int main() {try {
    VM::UseWorld(false);
    {
        Fixture f;
        Check(f.Run(0)==VMPrimitiveExitCode::GOTO_TRUE,"head request did not finish immediately");
        Check(f.Actor->GetPersonData(static_cast<Model::VMPersonDataVariable>(41))==f.Target->ObjectID&&f.Actor->GetPersonData(static_cast<Model::VMPersonDataVariable>(42))==1&&f.Actor->GetPersonData(static_cast<Model::VMPersonDataVariable>(43))==0&&
              f.Actor->GetPersonData(static_cast<Model::VMPersonDataVariable>(44))==1&&f.Actor->GetPersonData(static_cast<Model::VMPersonDataVariable>(45))==0,"head request fields differ");
        f.Frame->StackObject=nullptr;f.Frame->StackObjectRawID=0;
        Check(f.Run(0)==VMPrimitiveExitCode::GOTO_TRUE&&f.Actor->GetPersonData(static_cast<Model::VMPersonDataVariable>(41))==0&&
              f.Actor->GetPersonData(static_cast<Model::VMPersonDataVariable>(42))==0,"head clear retained enabled target");
        Check(!f.Actor->TS1ModelMotion.Token,"head request created a body movement");
        Reject([&]{f.Run(-1);},"negative original mode was not rejected");
        Reject([&]{f.Run(256);},"original mode was truncated to one byte");
    }
    for(int mode:{1,2,3}) {
        Fixture f;auto self=f.Actor->Position();auto target=f.Target->Position();
        Check(f.Run(mode)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,"body request did not poll pending immediately");
        const auto& m=f.Actor->TS1ModelMotion;
        Check(m.Kind==Routing::TS1MotionKind::Body&&m.Path.size()==3&&f.Frame->PrimitiveState==1&&
              m.Token&&m.Token==f.Frame->TS1MotionToken,"body request ownership differs");
        Check(m.Path[0].X==self.x&&m.Path[0].Y==self.y&&m.Path[1].X==self.x&&m.Path[1].Y==self.y,
              "body request does not preserve duplicate physical anchors");
        if(mode==1) Check(m.Path[2].X==self.x+16&&m.Path[2].Y==self.y+16,"camera quadrant zero target differs");
        else if(mode==2) Check(m.Path[2].X==target.x&&m.Path[2].Y==target.y,"body target was not copied");
        else Check(m.Path[2].X==2*int32_t(self.x)-target.x&&m.Path[2].Y==2*int32_t(self.y)-target.y,
                   "away request did not reflect target around actor");
        const auto token=m.Token;const auto copied=m.Path[2];
        f.Machine.RemoveEntity(f.Target.get());f.Actor->SetPersonData(Model::VMPersonDataVariable::Posture,1);
        Check(f.Run(mode)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&m.Token==token&&
              m.Path[2].X==copied.X&&m.Path[2].Y==copied.Y,"continuation re-resolved missing target or changed posture");
        f.Actor->TS1ModelMotion.Status=0;
        Check(f.Run(mode)==VMPrimitiveExitCode::GOTO_TRUE&&!f.Actor->TS1ModelMotion.Token&&!f.Frame->TS1MotionToken,
              "source poll zero did not finish and retire request");
    }
    {
        Fixture f;f.Actor->SetPersonData(Model::VMPersonDataVariable::Posture,1);
        Check(f.Run(2)==VMPrimitiveExitCode::GOTO_FALSE&&!f.Frame->PrimitiveState&&!f.Actor->TS1ModelMotion.Token,
              "fresh seated body request mutated model");
        f.Actor->SetPersonData(Model::VMPersonDataVariable::Posture,0);f.Frame->StackObject=nullptr;
        f.Frame->StackObjectRawID=99;
        Reject([&]{f.Run(2);},"missing fresh original target did not fail");
        f.Frame->PrimitiveState=1;
        Check(f.Run(2)==VMPrimitiveExitCode::GOTO_TRUE,"historic continuation without native request did not follow poll-zero completion");
    }
    std::cout<<"PASS body-facing producer controls: "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
