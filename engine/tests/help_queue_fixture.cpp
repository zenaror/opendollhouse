/* SPDX-License-Identifier: MPL-2.0 */
// Manual licensed-data fixture: the real App/LotSession listener and dialog
// primitive must retain accessible waits when nonblocking help fills its queue.
#include "app/App.h"
#include "content/ContentManager.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMClock.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMThread.h"
#include "simantics/primitives/VMDialogPrivateStrings.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/STR.h"
#include <iostream>
#include <stdexcept>
using namespace simapp;
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Click(App& app,int x,int y){AppInput in;in.DesktopPointer=true;in.TouchX=x;in.TouchY=y;in.TouchDown=in.Touching=true;app.Frame(in,0);in.TouchDown=in.Touching=false;in.TouchUp=true;app.Frame(in,0);}
static void Answer(App& app){app.Frame(AppInput{},0);int x=0,y=0;Need(app.ScriptButtonPos("simdlg0",x,y),"retained blocking response is not accessible");Click(app,x,y);}
int main(int argc,char** argv){try{
 Need(argc==4,"data-root private-save-root private-cache-root");
 App app;App::Config cfg;cfg.Desktop=true;cfg.LanguageCode=1;cfg.SkipIntro=true;cfg.DataRoot=argv[1];cfg.SaveRoot=argv[2];cfg.CacheDir=argv[3];Need(app.Init(cfg),"app init failed");
 int x=0,y=0;for(int i=0;i<240&&!app.ScriptHousePos(69,x,y);++i)app.Frame(AppInput{},16);Need(app.ScriptHousePos(69,x,y),"prepared house69 unavailable");Click(app,x,y);
 for(int i=0;i<240&&!app.Session()&&app.CurrentMode()!=App::Mode::Error;++i)app.Frame(AppInput{},0);
 auto session=app.Session();Need(session&&session->HouseNumber==69&&session->Dialogs.empty(),"fixture needs an already-loaded house with no unanswered dialogs");
 Need(app.ScriptButtonPos("speed0",x,y),"pause control absent");Click(app,x,y);Need(app.Speed==0,"fixture did not pause");Need(session->Family.size()>=2,"fixture needs two actual family threads");
 auto actor=session->Family[0],other=session->Family[1];Need(actor&&other&&actor->Thread&&other->Thread,"actual family threads unavailable");
 Need(!actor->Thread->BlockingState&&!other->Thread->BlockingState,"do not overwrite an existing game wait");
 const auto ticks=session->Context()->Clock->Ticks;const auto actionA=actor->Thread->ActiveAction(),actionB=other->Thread->ActiveAction();
 IffFile source;auto strings=new Chunks::STR();strings->ChunkID=301;strings->ChunkProcessed=true;strings->SourceFormat=-3;strings->LanguageSets.resize(20);
 for(const auto* text:{"Synthetic pending help","Synthetic blocking choice"}){Chunks::STRItem item;item.LanguageCode=1;item.Value=text;strings->LanguageSets[0].Strings.push_back(item);}source.AddChunk(strings);
 FSO::Content::GameObjectResource resource(&source,nullptr,nullptr,"help-queue-fixture",true);FSO::Content::GameObject owner;owner.GUID=0xf1200001u;owner.Resource=&resource;
 VMRoutine routine;routine.ID=0x7100;
 auto frame=[&](VMAvatar* caller,int pc){VMStackFrame out;out.Thread=caller->Thread.get();out.Caller=caller;out.Callee=caller;out.StackObjectRawID=caller->ObjectID;out.CodeOwner=&owner;out.Routine=&routine;out.InstructionPointer=pc;return out;};
 VMDialogOperand help;help.Type=VMDialogType::Sims1Tutorial;help.MessageStringID=1;help.Flags=static_cast<VMDialogFlags>(3); // Continue + no icon.
 VMDialogOperand block;block.Type=VMDialogType::YesNo;block.MessageStringID=2;block.Flags=static_cast<VMDialogFlags>(2);
 auto raiseHelp=[&](int pc){auto f=frame(actor,pc);Need(VMDialogPrivateStrings::ExecuteGeneric(&f,&help,strings)==VMPrimitiveExitCode::GOTO_TRUE,"Continue did not finish synchronously");Need(!actor->Thread->BlockingState,"Continue installed a response wait");};
 auto raiseBlock=[&](VMAvatar* caller,int pc){auto f=frame(caller,pc);Need(VMDialogPrivateStrings::ExecuteGeneric(&f,&block,strings)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,"blocking producer did not wait");auto wait=std::dynamic_pointer_cast<VMDialogResult>(caller->Thread->BlockingState);Need(wait&&wait->HasDisplayed&&!wait->Responded,"blocking producer lost the actual wait");return wait;};
 auto continueBlock=[&](VMAvatar* caller,int pc,const std::shared_ptr<VMDialogResult>& wait){Need(wait->Responded&&wait->ResponseCode==0,"pointer response reached a different wait/code");auto f=frame(caller,pc);Need(VMDialogPrivateStrings::ExecuteGeneric(&f,&block,strings)==VMPrimitiveExitCode::GOTO_TRUE&&!caller->Thread->BlockingState,"actual answered primitive did not continue");};
 for(int i=0;i<8;++i)raiseHelp(i);Need(session->Dialogs.size()==8,"help queue preparation failed");
 auto first=raiseBlock(actor,20);Need(session->Dialogs.size()==8&&session->Dialogs.front().Block&&session->Dialogs.front().CallerID==actor->ObjectID,"eight help notices swallowed or hid the new block");
 Answer(app);continueBlock(actor,20,first);Need(session->Dialogs.size()==7,"one block answer discarded unrelated help entries");
 for(const auto& d:session->Dialogs)Need(d.Type==4&&!d.Block,"unexpected remaining block");session->Dialogs.clear(); // These Continue producers have no wait.
 auto old=raiseBlock(other,30);for(int i=0;i<7;++i)raiseHelp(40+i);auto next=raiseBlock(actor,50);
 Need(session->Dialogs.size()==8&&session->Dialogs[0].Block&&session->Dialogs[0].CallerID==other->ObjectID&&session->Dialogs[1].Block&&session->Dialogs[1].CallerID==actor->ObjectID,"help eviction discarded/reordered an existing blocking owner");
 Answer(app);continueBlock(other,30,old);Need(!next->Responded,"answering the old owner also answered the next owner");Answer(app);continueBlock(actor,50,next);
 Need(session->Dialogs.size()==6,"two actual responses discarded remaining help");for(const auto& d:session->Dialogs)Need(d.Type==4&&!d.Block,"remaining queue corrupted");session->Dialogs.clear();
 VMDialogOperand notice=help;notice.Type=VMDialogType::Message;
 for(int i=0;i<8;++i){auto f=frame(actor,60+i);Need(VMDialogPrivateStrings::ExecuteGeneric(&f,&notice,strings)==VMPrimitiveExitCode::GOTO_TRUE,"ordinary Continue producer failed");}
 auto ninth=raiseBlock(actor,70);Need(session->Dialogs.size()==9&&session->Dialogs.back().Block,"full non-help queue discarded the new blocking wait");
 for(int i=0;i<8;++i){Need(!session->Dialogs.front().Block&&session->Dialogs.front().Type==0,"ordinary notice order changed");Answer(app);Need(!ninth->Responded,"ordinary notice answered the hidden blocking owner");}
 Answer(app);continueBlock(actor,70,ninth);Need(session->Dialogs.empty(),"ninth blocking response left an unrelated entry");
 Need(session->Context()->Clock->Ticks==ticks&&session->Resets==0,"queue fixture advanced/reset the world");Need(actor->Thread->ActiveAction()==actionA&&other->Thread->ActiveAction()==actionB,"queue fixture changed real active actions");
 std::cout<<"PASS actual App/LotSession/VMDialogPrivateStrings:8Continue+Block; oldBlock+7Continue+newBlock;8ordinaryContinue+Block9; pointer responses same owners/waits; no VM tick/save/reset/action replacement\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
