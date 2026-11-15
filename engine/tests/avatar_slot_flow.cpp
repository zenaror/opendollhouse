// SPDX-License-Identifier: MPL-2.0
// Asset-backed containment API fixture, not a player spell or visual-acceptance test.
// Requires a private copy of an occupied house with two Sims, Pink Flamingo and
// Spellbound Wand Charger definitions. No time/stat/inventory injection or save.
// Invocation explicitly opts the supplied SAVE_ROOT into transient fixture actions.
#include "app/App.h"
#include "app/front/FrontEnd.h"
#include "app/LotSession.h"
#include "content/ContentManager.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/primitives/VMCreateObjectInstance.h"
#include "world/components/AvatarComponent.h"
#include <filesystem>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
using namespace simapp;
static void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Click(App& app,int x,int y){AppInput in;in.DesktopPointer=true;in.TouchX=x;in.TouchY=y;in.TouchDown=in.Touching=true;app.Frame(in,0);in.TouchDown=in.Touching=false;in.TouchUp=true;app.Frame(in,0);}
static int Matrix(LotSession& session){int failures=0;
auto se=&session;Require(se->Family.size()>=2,"fixture needs two household Sims");auto vm=se->VM();auto a=se->Family[0];auto b=se->Family[1];auto ctx=vm->Context;
 uint32_t fixtureGuid=0;for(auto e:vm->Entities)if(e->ToString()=="Pink Flamingo"){fixtureGuid=e->Object->OBJ->GUID;break;}
 Require(fixtureGuid!=0,"fixture needs an existing Pink Flamingo definition");
 std::vector<FSO::SimAntics::VMEntity*> created;auto require=[&](bool ok,const char* why){printf("[slot-test] %s %s\n",ok?"PASS":"FAIL",why);if(!ok)++failures;return ok;};
 FSO::SimAntics::Engine::VMStackFrame f;f.Thread=a->Thread.get();f.Caller=a;f.Callee=a;f.CodeOwner=a->Object;f.Args.resize(4);f.Locals.resize(4);
 auto create=[&](int pos,int flags,int cb=0){f.StackObject=b;FSO::SimAntics::Primitives::VMCreateObjectInstanceOperand op;op.GUID=fixtureGuid;op.Position=(FSO::SimAntics::Primitives::VMCreateObjectPosition)pos;op.Flags=flags;op.InteractionCallback=cb;FSO::SimAntics::Primitives::VMCreateObjectInstance primitive;auto r=primitive.Execute(&f,&op);auto c=f.StackObject;if(r==FSO::SimAntics::Engine::VMPrimitiveExitCode::GOTO_TRUE&&c&&c!=b)created.push_back(c);return std::pair{r,c};};

 for(int pos:{2,4,10,11,12,13,14})for(int flags:{0,0x20,0x40,0x60}) {
  const auto qa=a->Thread->Queue.size();const auto qb=b->Thread->Queue.size();auto r=create(pos,flags);auto owner=(pos==2||pos==10||pos==11)?a:b;int k=pos<10?0:(pos==10||pos==12)?2:(pos==11||pos==13)?3:4;
  bool ok=r.first==FSO::SimAntics::Engine::VMPrimitiveExitCode::GOTO_TRUE&&r.second&&r.second!=b&&r.second->Container==owner&&r.second->ContainerSlot==k&&owner->GetSlot(k)==r.second;
  require(ok,("mode="+std::to_string(pos)+" flags="+std::to_string(flags)+" caller/stack/reciprocal").c_str());
  require(a->Thread->Queue.size()==qa&&b->Thread->Queue.size()==qb,"TS1 flags40 do not queue TSO callback");
  if(ok){auto entities=vm->Entities.size();auto rejected=create(pos,flags);require(rejected.first==FSO::SimAntics::Engine::VMPrimitiveExitCode::GOTO_FALSE&&vm->Entities.size()==entities&&owner->GetSlot(k)==r.second,"occupied rejects before allocation");owner->ClearSlot(k);r.second->Delete(true,ctx);}
 }
 auto one=create(10,0);auto c=one.second;require(c&&c!=b,"bounds fixture allocated");if(c&&c!=b){a->ClearSlot(2);require(!a->PlaceInSlot(c,-1,false,ctx)&&!a->PlaceInSlot(c,5,false,ctx),"invalid indices reject without containment");require(!a->PlaceInSlot(a,2,false,ctx),"self containment rejected");c->Dead=true;require(!a->PlaceInSlot(c,2,false,ctx),"dead child rejected");c->Dead=false;c->Delete(true,ctx);}
 auto posa=a->Position();auto posb=b->Position();require(a->PlaceInSlot(b,2,false,ctx),"avatar child fixture");require(!b->PlaceInSlot(a,3,false,ctx),"ancestor cycle rejected");a->ClearSlot(2);b->Position(posb);b->PositionChange(ctx,true);
 std::vector<FSO::SimAntics::VMEntity*> together;for(int pos:{2,10,11}){auto r=create(pos,0);together.push_back(r.second);}f.StackObject=a;FSO::SimAntics::Primitives::VMCreateObjectInstanceOperand op;op.GUID=fixtureGuid;op.Position=FSO::SimAntics::Primitives::VMCreateObjectPosition::InSlot4OfStackObject;FSO::SimAntics::Primitives::VMCreateObjectInstance primitive;auto z=primitive.Execute(&f,&op);together.push_back(f.StackObject);require(z==FSO::SimAntics::Engine::VMPrimitiveExitCode::GOTO_TRUE&&a->GetSlot(0)&&a->GetSlot(2)&&a->GetSlot(3)&&a->GetSlot(4),"four independent slots including hand coexist");
 auto moved=posa;moved.x+=16;a->Position(moved);a->PositionChange(ctx,true);for(int k:{0,2,3,4})if(auto child=a->GetSlot(k)){require(child->Position()==moved,"all children follow parent VM movement");if(k>=2){auto p=child->WorldUI->DrawPosition();auto q=a->WorldUI->DrawPosition();require(p.X==q.X&&p.Y==q.Y&&p.Z==q.Z,"extra dynamic sprite follows parent render transform");}}
 auto hand=a->GetSlot(0);auto carry=a->CarryAnimationState;auto extra=a->GetSlot(4);a->ClearSlot(4);require(a->GetSlot(0)==hand&&a->CarryAnimationState==carry&&hand&&hand->Container==a&&hand->ContainerSlot==0,"clearing extra4 preserves carried object and pose");if(extra)extra->Delete(true,ctx);
 for(int k:{0,2,3,4})if(auto child=a->GetSlot(k)){a->ClearSlot(k);child->Delete(true,ctx);}a->Position(posa);a->PositionChange(ctx,true);
 // Keep the online-game callback contract on the original six-position path.
 FSO::SimAntics::VMEntity* source=nullptr;for(auto e:vm->Entities)if(e->ToString().find("Spellbound Wand Charger")!=std::string::npos){source=e;break;}
 if(source){auto saved=a->Thread->Queue;auto active=a->Thread->ActiveQueueBlock;auto sourceStack=source;f.StackObject=sourceStack;op.Position=FSO::SimAntics::Primitives::VMCreateObjectPosition::OutOfWorld;op.Flags=0x40;op.InteractionCallback=5;bool old=vm->TS1;vm->TS1=false;auto result=primitive.Execute(&f,&op);vm->TS1=old;require(result==FSO::SimAntics::Engine::VMPrimitiveExitCode::GOTO_TRUE&&f.StackObject==sourceStack&&a->Thread->Queue.size()==saved.size()+1,"nonTS1 flag40 retains callback dispatch");a->Thread->Queue=saved;a->Thread->ActiveQueueBlock=active;}else require(false,"callback fixture source available");
 printf("[slot-test] matrix finished failures=%d fixture-only/no-save\n",failures);
 return failures?1:0;
}
int main(int argc,char** argv){try{
 Require(argc==4||argc==5,"usage: avatar-slot-flow DATA_ROOT PRIVATE_SAVE_ROOT CACHE_ROOT [house=9]");
 const auto data=std::filesystem::weakly_canonical(argv[1]);const auto save=std::filesystem::weakly_canonical(argv[2]);
 Require(save!=data&&save.string().rfind(data.string()+"/",0)!=0,"fixture save root must be outside protected installation");
 std::filesystem::create_directories(argv[3]);App app;App::Config cfg;cfg.Desktop=true;cfg.SkipIntro=true;cfg.LanguageCode=1;cfg.DataRoot=data.string();cfg.SaveRoot=save.string();cfg.CacheDir=argv[3];Require(app.Init(cfg),"fixture App initialization failed");
 app.Front()->FinishBootNow();app.Frame(AppInput(),0);int house=argc==5?std::stoi(argv[4]):9;int x=0,y=0;Require(app.ScriptHousePos(house,x,y),"fixture house unavailable");Click(app,x,y);for(int i=0;i<20&&!app.Session();++i)app.Frame(AppInput(),0);Require(app.Session()!=nullptr,"fixture house did not open");
 const auto updates=app.Session()->Updates;const auto budget=app.Session()->Budget();const auto residents=app.Session()->Family.size();int status=Matrix(*app.Session());Require(app.Session()->Updates==updates&&app.Session()->Budget()==budget&&app.Session()->Family.size()==residents,"fixture changed clock, budget or household");return status;
}catch(const std::exception& e){std::fprintf(stderr,"avatar slot fixture failed: %s\n",e.what());return 1;}}
