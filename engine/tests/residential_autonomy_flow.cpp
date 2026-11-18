/* SPDX-License-Identifier: MPL-2.0 */
// Asset-backed affected-case runner. One isolated save root, protected install.
// Run and reopen are separate processes; this is not a data-free CTest fixture.
#include "app/App.h"
#include "app/front/FrontEnd.h"
#include "app/front/GameOptions.h"
#include "app/LotScreen.h"
#include "world/World.h"
#include "world/utils/TriangleRaster.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/engine/VMDialogOperand.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include "content/TS1NeighbourProvider.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "simantics/model/VMMotive.h"
#include "json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <stdexcept>
using namespace simapp;
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
namespace fs=std::filesystem;
static void Require(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
static void Click(App& app,int x,int y){AppInput in;in.DesktopPointer=true;in.TouchX=x;in.TouchY=y;in.TouchDown=in.Touching=true;app.Frame(in,0);in.TouchDown=in.Touching=false;in.TouchUp=true;app.Frame(in,0);}
static void ClickControl(App& app,const std::string& id){int x=0,y=0;app.Frame(AppInput(),0);Require(app.ScriptButtonPos(id,x,y),"missing original control "+id);std::cout<<"CONTROL "<<id<<" x="<<x<<" y="<<y<<" hit="<<(app.Lot()?app.Lot()->HitAt(x,y):"front")<<" dialogs="<<(app.Session()?app.Session()->Dialogs.size():0)<<std::endl;Click(app,x,y);}
static void FreeWill(App& app,bool enabled){
    auto& options=front::GameOptions::Get();options.Current=front::GameOptions::Page::Play;
    app.Lot()->SetMode(LotScreen::Mode::Options);app.Frame(AppInput(),0);
    if(app.Front()->Settings().FreeWill!=enabled)ClickControl(app,"gopt_freewill");
    Require(app.Front()->Settings().FreeWill==enabled,"checkbox did not change preference");
    Require(app.Session()->VM()->FreeWillEnabled==enabled,"preference did not reach the loaded VM");
    Require(app.Session()->VM()->GetGlobalValue(30)==int(enabled),"Free Will preference did not reach script global 30");
    app.Lot()->SetMode(LotScreen::Mode::Live);app.Frame(AppInput(),0);
}
static void OpenHouse(App& app,int house){int x=0,y=0;for(int i=0;i<240&&!app.ScriptHousePos(house,x,y);++i)app.Frame(AppInput(),16);Require(app.ScriptHousePos(house,x,y),"house unavailable");Click(app,x,y);for(int i=0;i<240&&!app.Session();++i)app.Frame(AppInput(),16);Require(app.Session()&&app.CurrentMode()==App::Mode::Live,"house did not open");}
// Trace is an instruction observer only: RunInMyStack temporarily replaces the
// real thread's queue/stack and marks it IsCheck. Cohort/queue decisions belong
// exclusively to AfterUpdate, after the entire Session.Update has returned.
struct GreetDiagnostic {
    VMAvatar* actor;std::function<VMAvatar*()> guest;
    bool stopped=false,queued=false,visitorStarted=false,queuePresent=false,visitorReturned=false;
    std::map<std::pair<bool,int>,size_t> hits;
    std::set<std::tuple<bool,int,int>> events;
    std::string previous;size_t records=0,states=0;
    void Stop(const char* reason){if(!stopped){stopped=true;std::cout<<"SOCIAL_FIRST_DEVIATION "<<reason<<std::endl;}}
    void Trace(Engine::VMStackFrame* f,uint16_t opcode,int result){
        if(!f||!f->Routine||!f->Thread)return;
        auto g=guest();if(f->Caller!=actor&&f->Caller!=g)return;
        const int id=f->Routine->ID;const bool check=f->Thread->IsCheck;
        bool child=false;for(const auto& x:f->Thread->Stack)if(x->Routine&&(x->Routine->ID==4177||x->Routine->ID==4178))child=true;
        if(!child&&f->Caller!=g)return;
        ++hits[{check,id}];if(id==4178&&!check)visitorStarted=true;
        const auto& in=f->GetCurrentInstruction();
        const int branch=(result==0||result==2)?in.TruePointer:(result==1||result==3)?in.FalsePointer:-1;
        const bool terminal=branch==254||branch==255||(result>=4&&result<=6);
        if(id==4178&&!check&&terminal)visitorReturned=true;
        // One entry and first terminal outcome per routine/context; allow-push
        // is recorded separately to explain whether queued work can be admitted.
        const int event=terminal?(branch>=254?branch:result):opcode==17?17:0;
        if((child||opcode==17)&&events.insert({check,id,event}).second&&records++<64){
            std::cout<<"SOCIAL_TRACE context="<<(check?"CHECK":"running")<<" routine="<<id<<" pc="<<int(f->InstructionPointer)<<" opcode="<<opcode<<" result="<<result<<" next="<<branch<<" actor="<<(f->Caller?f->Caller->ObjectID:0)<<" callee="<<(f->Callee?f->Callee->ObjectID:0)<<" owner="<<(f->CodeOwner?f->CodeOwner->GUID:0)<<" stack="<<f->StackObjectID()<<" args=";
            for(auto v:f->Args)std::cout<<v<<',';
            std::cout<<" locals=";for(auto v:f->Locals)std::cout<<v<<',';
            std::cout<<" operand=";for(auto v:f->Routine->Chunk->Instructions.at(f->InstructionPointer).Operand)std::cout<<int(v)<<',';
            std::cout<<std::endl;
        }
    }
    static void Describe(std::ostream& out,VMAvatar* a,bool detail){
        if(!a){out<<" absent";return;}
        auto t=a->Thread.get();out<<" id="<<a->ObjectID<<" dead="<<a->Dead<<" greet="<<a->GetPersonData(VMPersonDataVariable::GreetStatus)<<" priority="<<a->GetPersonData(VMPersonDataVariable::Priority)<<" block="<<int(t->ActiveQueueBlock)<<" check="<<t->IsCheck;
        if(detail)out<<" pos="<<a->Position().x<<','<<a->Position().y<<','<<int(a->Position().Level)<<" room="<<a->GetValue(VMStackObjectVariable::Room)<<" exit="<<int(t->LastStackExitCode);
        out<<" queue=";for(const auto& q:t->Queue)out<<'['<<q->InteractionNumber<<":"<<q->UID<<":"<<q->Priority<<":"<<q->Cancelled<<":"<<(q->ActionRoutine?q->ActionRoutine->ID:0)<<":"<<(q->Callee?q->Callee->ObjectID:0)<<":"<<(q->Callee?q->Callee->Dead:0)<<":"<<(q->CodeOwner?q->CodeOwner->GUID:0)<<']';
        out<<" frames=";for(const auto& f:t->Stack){out<<'['<<(f->Routine?f->Routine->ID:0);if(detail)out<<':'<<f->InstructionPointer<<":"<<f->StackObjectID()<<":"<<(f->Callee?f->Callee->ObjectID:0)<<":"<<(f->CodeOwner?f->CodeOwner->GUID:0)<<":"<<f->ActionTree;if(auto r=dynamic_cast<Engine::VMRoutingFrame*>(f.get()))out<<"/route="<<int(r->State)<<"/held="<<int(r->HeldResult);out<<']';}
    }
    void AfterUpdate(LotSession& s,bool final=false){
        auto g=guest();bool present=false;
        if(g&&g->Thread)for(const auto& q:g->Thread->Queue)if(q->InteractionNumber==52&&q->ActionRoutine&&q->ActionRoutine->ID==4178)present=true;
        std::ostringstream key;Describe(key,actor,false);Describe(key,g,false);
        if(final||(key.str()!=previous&&states++<32)){
            std::cout<<"SOCIAL_STABLE hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<" final="<<final<<" actor";Describe(std::cout,actor,true);std::cout<<" guest";Describe(std::cout,g,true);std::cout<<std::endl;
        }
        if(final)for(auto e:s.VM()->Entities)if(e->Object->GUID==0x3317A9CF){std::cout<<"SOCIAL_CONTROLLER id="<<e->ObjectID<<" dead="<<e->Dead<<" attrs=";for(auto v:e->AttributeData())std::cout<<v<<',';std::cout<<std::endl;}
        previous=key.str();queued|=present;
        if(queuePresent&&!present&&!visitorStarted)Stop("52-left-restored-queue-without-running-4178");
        queuePresent=present;
        if(visitorReturned&&g&&g->GetPersonData(VMPersonDataVariable::GreetStatus)!=1)Stop("running-4178-returned-before-admission");
    }
    void Summary(LotSession& s){AfterUpdate(s,true);std::cout<<"SOCIAL_COUNTS";for(const auto& h:hits)std::cout<<' '<<(h.first.first?"CHECK/":"running/")<<h.first.second<<':'<<h.second;std::cout<<" queue52_seen="<<queued<<" visitor_started="<<visitorStarted<<" visitor_returned="<<visitorReturned<<" stopped="<<stopped<<std::endl;}
};
struct Observation {
    int household=0,visitors=0,pets=0;
    std::set<uint32_t> visitorIds;
    void Attach(LotSession& s){s.VM()->OnAutonomyChoice=[this](VMEntity* e,const std::vector<VMPieMenuInteraction>&,const VMPieMenuInteraction&){auto a=dynamic_cast<VMAvatar*>(e);if(!a)return;if(a->IsPet())++pets;else if(a->GetPersonData(VMPersonDataVariable::PersonType)==1){++visitors;visitorIds.insert(a->Object->GUID);}else ++household;};}
};
static bool Push(LotSession& s,VMEntity* target,const std::string& word,std::shared_ptr<Engine::VMQueuedAction>& queued){
    auto actor=s.SelectedSim();if(!actor||!target)return false;
    for(auto& choice:target->GetPieMenu(s.VM(),actor,false))if(choice.Name.find(word)!=std::string::npos){
        auto before=actor->Thread->Queue;
        if(!s.PushInteraction(target,false,{choice.Name,choice.ID,choice.Param0}))continue;
        for(auto& q:actor->Thread->Queue)if(std::find(before.begin(),before.end(),q)==before.end()){
            queued=q;std::cout<<"MANUAL "<<json_bytes(q->Name)<<" uid="<<q->UID<<" target="<<target->ObjectID<<" target_name="<<json_bytes(target->ToString())<<'\n';return true;
        }
    }
    return false;
}
static void Notices(LotSession& s){while(!s.Dialogs.empty()){
    const auto& d=s.Dialogs.front();
    if(d.Type!=0)std::cout<<"CHOICE_LABELS type="<<int(d.Type)<<" yes="<<json_bytes(d.Yes)<<" no="<<json_bytes(d.No)<<" cancel="<<json_bytes(d.Cancel)<<std::endl;
    Require(d.Type==0,"choice dialog requires an explicit player answer: "+d.Title+" / "+d.Message);
    std::cout<<"NOTICE "<<json_bytes(d.Title)<<'\n';s.RespondDialog(0);
}}
static void Advance(LotSession& s,int minutes,const std::function<void()>& inspect={}){
    const int rate=std::max(1,s.Context()->Clock->TicksPerMinute);
    const auto end=s.Context()->Clock->Ticks+int64_t(minutes)*rate;
    int64_t updates=0;while(s.Context()->Clock->Ticks<end){Require(++updates<int64_t(minutes)*rate*8+1024,"simulation clock stopped advancing");s.Update(2);Notices(s);if(inspect)inspect();}
}
static void Save(App& app){
    app.Lot()->SetMode(LotScreen::Mode::Options);app.Frame(AppInput(),0);
    const auto start=app.Log.size();ClickControl(app,"gopt_save");
    Require(app.Log.substr(start).find("saved house and neighbourhood")!=std::string::npos,"original Save control did not complete successfully");
}
static const std::vector<VMPersonDataVariable> Progress={VMPersonDataVariable::JobType,VMPersonDataVariable::JobPromotionLevel,VMPersonDataVariable::JobPerformance,VMPersonDataVariable::CookingSkill,VMPersonDataVariable::CharismaSkill,VMPersonDataVariable::MechanicalSkill,VMPersonDataVariable::CreativitySkill,VMPersonDataVariable::BodySkill,VMPersonDataVariable::LogicSkill};
static void Snapshot(LotSession& s,const fs::path& path){std::ofstream out(path);Require(bool(out),"cannot record snapshot");out<<s.Budget()<<' '<<s.Family.size()<<'\n';for(auto a:s.Family){out<<a->Object->GUID;for(auto field:Progress)out<<' '<<a->GetPersonData(field);out<<'\n';}}
static void Reopened(LotSession& s,const fs::path& path){std::ifstream in(path);Require(bool(in),"snapshot missing");int budget=0;size_t count=0;in>>budget>>count;Require(s.Budget()==budget&&s.Family.size()==count,"reopen changed household count/budget");for(size_t i=0;i<count;++i){uint32_t guid=0;in>>guid;auto found=std::find_if(s.Family.begin(),s.Family.end(),[&](auto a){return a->Object->GUID==guid;});Require(found!=s.Family.end(),"reopen lost household identity");for(auto field:Progress){int value=0;in>>value;Require((*found)->GetPersonData(field)==value,"reopen changed saved career/skill field "+std::to_string(int(field)));}}Require(bool(in),"invalid progress snapshot");}
int main(int argc,char**argv){try{
    Require(argc>=5&&argc<=7,"usage: residential-autonomy-flow DATA_ROOT SAVE_ROOT CACHE_ROOT run|reopen|school|school-reopen|visitor [offered-NID|inspect] [house], or greet|greet-near [house]");
    const fs::path data=fs::weakly_canonical(argv[1]),save=fs::weakly_canonical(argv[2]);
    Require(save!=data&&save.string().rfind(data.string()+"/",0)!=0,"save root must be outside protected install");
    fs::create_directories(save);fs::create_directories(argv[3]);const std::string phase=argv[4];
    if(phase=="run"&&!fs::exists(save/"autonomy-progress.txt")){
        // Legacy console file: switching the PC checkbox on must not stay
        // disabled because a removed numeric radio once wrote level zero.
        std::ofstream settings(save/"sims_options.txt");settings<<"FreeWill=0\nAutonomyLevel=0\nPlayIntro=0\n";
    }
    App app;App::Config cfg;cfg.DataRoot=data.string();cfg.SaveRoot=save.string();cfg.CacheDir=argv[3];cfg.Desktop=true;cfg.SkipIntro=true;cfg.LanguageCode=1;Require(app.Init(cfg),"app init failed");
    const bool school=phase=="school"||phase=="school-reopen";
    int house=phase=="visitor"&&argc==7?std::stoi(argv[6]):(phase=="greet"||phase=="greet-near")&&argc>=6?std::stoi(argv[5]):69;
    if(school){
        int x=0,y=0;for(int i=0;i<240&&!app.ScriptHousePos(69,x,y);++i)app.Frame(AppInput(),16);
        if(phase=="school-reopen"){std::ifstream in(save/"autonomy-school-house.txt");in>>house;Require(bool(in),"school household snapshot missing");}
        else {house=0;for(const auto& f:app.Front()->Model().Families)if(f.House>0&&std::any_of(f.Members.begin(),f.Members.end(),[](const auto& a){return a.Child&&!a.Pet;})){house=f.House;break;}Require(house>0,"no authentic occupied household with a child");std::ofstream(save/"autonomy-school-house.txt")<<house<<'\n';}
    }
    OpenHouse(app,house);auto& s=*app.Session();
    // Explicit scenario precondition authorized by the coordinator: decline
    // only this known initial help prompt, never an evaluated event choice.
    if(!s.Dialogs.empty()){
        const auto& d=s.Dialogs.front();
        if(d.Title=="Help System"&&d.Message=="The help system can alert you to important events as well as show some features you may not be experienced with. Would you like the help system activated?"&&d.HasNo){
            std::cout<<"PRECONDITION initial Help System: explicit No\n";s.RespondDialog(1);
        }
    }
    Notices(s);app.Frame(AppInput(),0);
    if(school){
        if(phase=="school-reopen"){Reopened(s,save/"autonomy-school-progress.txt");std::cout<<"AUTONOMY_RESULT {\"phase\":\"school-reopen\",\"saved_progress\":true}\n";return 0;}
        FreeWill(app,false);Observation off;off.Attach(s);
        VMAvatar* child=nullptr;for(auto a:s.Family)if(!a->IsPet()&&a->GetPersonData(VMPersonDataVariable::PersonsAge)<18){child=a;break;}
        Require(child!=nullptr,"school child not loaded");std::string lastSchoolAction;bool departed=false,returned=false;
        // Observe the original bus/school scripts with autonomy off. No injected
        // clock, forced school command or invented attendance transition.
        Advance(s,12*60,[&]{
            const auto action=child->Thread->ActiveAction();const auto name=action?action->Name:std::string();
            if(name!=lastSchoolAction){lastSchoolAction=name;std::cout<<"SCHOOL_ACTION hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<" name="<<json_bytes(name)<<" container="<<(child->Container?child->Container->ObjectID:0)<<" hidden="<<child->GetValue(VMStackObjectVariable::Hidden)<<std::endl;}
            if(!departed&&name=="At School"){departed=true;std::cout<<"SCHOOL_DEPART hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<std::endl;}
            if(departed&&!returned&&name!="At School"&&child->Position()!=FSO::LotView::Model::LotTilePos::OUT_OF_WORLD&&!child->GetValue(VMStackObjectVariable::Hidden)){returned=true;std::cout<<"SCHOOL_RETURN hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<std::endl;}
        });
        Require(departed&&returned,"school departure/return not observed in the affected-case window");Require(off.household==0,"household autonomous choice while off during school");
        Save(app);Snapshot(s,save/"autonomy-school-progress.txt");std::cout<<"AUTONOMY_RESULT {\"phase\":\"school\",\"departed\":true,\"returned\":true,\"household_choices\":"<<off.household<<"}\n";return 0;
    }
    Require(!s.VM()->FreeWillEnabled,"saved off preference not applied on loading");
    if(phase=="reopen"){
        Reopened(s,save/"autonomy-progress.txt");Require(app.Front()->Settings().AutonomyLevel==0,"legacy setting was silently rewritten");
        Observation off;off.Attach(s);Advance(s,10);Require(off.household==0,"new household autonomous choice after off reopen");
        std::cout<<"AUTONOMY_RESULT {\"phase\":\"reopen\",\"identities_budget_career_skills\":true,\"free_will\":false,\"household_choices\":"<<off.household<<",\"pet_choices\":"<<off.pets<<"}\n";return 0;
    }
    if(phase=="greet"||phase=="greet-near"){
        using FSO::SimAntics::Engine::Utils::VMTS1Neighbourhood;
        FreeWill(app,false);Observation off;off.Attach(s);
        auto provider=VMTS1Neighbourhood::Provider();Require(provider!=nullptr,"neighborhood unavailable");
        auto live=[&](uint32_t guid)->VMAvatar*{for(auto e:s.VM()->Entities)if(auto a=dynamic_cast<VMAvatar*>(e))if(!a->Dead&&a->Object->GUID==guid)return a;return nullptr;};
        auto cancel=[&]{auto a=s.SelectedSim();auto queued=a->Thread->Queue;for(auto q:queued){std::cout<<"SCRIPTED_CANCEL_BEFORE_SOCIAL "<<json_bytes(q->Name)<<" uid="<<q->UID<<std::endl;s.CancelAction(q->UID);}};
        if(phase=="greet-near"){
            // A normal player prepares to meet a walkby at the sidewalk. The
            // only movement request is the original floor pie's Go Here order,
            // through the same public consumer invoked by its selection callback.
            VMEntity* portal=nullptr;
            for(auto e:s.VM()->Entities)if(!e->Dead&&e->Object->GUID==0x81e6bef9&&e->Position()!=FSO::LotView::Model::LotTilePos::OUT_OF_WORLD)
                if(!portal||e->Position().y<portal->Position().y)portal=e;
            Require(portal!=nullptr,"north pedestrian portal missing");
            s.CenterOn(portal);app.Frame(AppInput(),0);
            int floorX=0,floorY=0;PieChoice go;bool offered=false;
            FSO::LotView::Model::LotTilePos destination;
            for(int dy:{3,4,2}){for(int dx:{0,1,-1}){
                const auto tile=FSO::LotView::Model::LotTilePos::FromBigTile(portal->Position().TileX()+dx,portal->Position().TileY()+dy,1);
                if(s.Context()->IsOutOfBounds(tile)||s.Context()->SolidToAvatars(tile).Solid)continue;
                const auto point=s.World()->State.WorldSpace.GetScreenFromTile(tile.ToVector3())+FSO::LotView::Utils::TileProjectionOrigin(s.World()->State);
                const int x=int(std::lround(point.X)),y=int(std::lround(point.Y));
                if(x<0||y<0||x>=app.DesktopCanvas().Width||y>=app.DesktopCanvas().Height-128)continue;
                std::vector<PieChoice> choices;bool isGoto=false;int16_t picked=0;
                auto target=s.PieTargetAt(x,y,choices,isGoto,picked);
                if(!target||!isGoto)continue;
                auto found=std::find_if(choices.begin(),choices.end(),[](const auto& c){return c.Name=="Go Here";});
                if(found==choices.end())continue;
                floorX=x;floorY=y;go=*found;destination=target->Position();offered=true;break;
            }if(offered)break;}
            Require(offered,"no original Go Here offered on legal floor near north portal");
            cancel();auto before=s.SelectedSim()->Thread->Queue;
            std::cout<<"SOCIAL_PREP GoHere offered="<<json_bytes(go.Name)<<" floor="<<destination.x<<','<<destination.y<<" screen="<<floorX<<','<<floorY<<std::endl;
            Require(s.PushInteraction(s.GotoObject,true,go),"offered Go Here could not be queued through the normal pie callback consumer");
            std::shared_ptr<Engine::VMQueuedAction> walk;
            for(auto q:s.SelectedSim()->Thread->Queue)if(std::find(before.begin(),before.end(),q)==before.end()){walk=q;break;}
            Require(bool(walk),"Go Here consumer did not queue the original movement order");
            auto human=s.SelectedSim();const auto end=s.Context()->Clock->Ticks+int64_t(60)*s.Context()->Clock->TicksPerMinute;
            while(s.Context()->Clock->Ticks<end&&std::find(human->Thread->Queue.begin(),human->Thread->Queue.end(),walk)!=human->Thread->Queue.end()){s.Update(2);Notices(s);}
            Require(std::find(human->Thread->Queue.begin(),human->Thread->Queue.end(),walk)==human->Thread->Queue.end()&&FSO::LotView::Model::LotTilePos::Distance(human->Position(),destination)<=32,"original Go Here did not complete near the chosen sidewalk floor");
            std::cout<<"SOCIAL_PREP_COMPLETE hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<" pos="<<human->Position().x<<','<<human->Position().y<<std::endl;
        }
        uint32_t visitorGUID=0;int16_t visitorNID=0;std::shared_ptr<Engine::VMQueuedAction> greeting;
        const auto searchEnd=s.Context()->Clock->Ticks+int64_t(180)*s.Context()->Clock->TicksPerMinute;
        while(s.Context()->Clock->Ticks<searchEnd&&!greeting){
            auto entities=s.VM()->Entities;
            for(auto e:entities){auto guest=dynamic_cast<VMAvatar*>(e);if(!guest||guest->Dead||guest->IsPet()||guest->GetPersonData(VMPersonDataVariable::PersonType)!=1)continue;
                auto neighbor=provider->GetNeighborByID(guest->GetPersonData(VMPersonDataVariable::NeighborId));
                if(!neighbor||provider->GetHouseOfNeighbor(neighbor)<=0||provider->GetHouseOfNeighbor(neighbor)==house)continue;
                int nearest=-1,distance=0x7fffffff;for(size_t i=0;i<s.Family.size();++i){auto actor=s.Family[i];if(actor->Dead||actor->IsPet()||actor->Position()==FSO::LotView::Model::LotTilePos::OUT_OF_WORLD)continue;const int d=FSO::LotView::Model::LotTilePos::Distance(actor->Position(),guest->Position());if(d<distance){distance=d;nearest=int(i);}}
                if(nearest<0||(phase=="greet-near"&&distance>64))continue;s.Select(nearest);
                const auto choices=guest->GetPieMenu(s.VM(),s.SelectedSim(),false);
                if(std::none_of(choices.begin(),choices.end(),[](const auto& choice){return choice.Name.find("Greet")!=std::string::npos;}))continue;
                visitorGUID=guest->Object->GUID;visitorNID=guest->GetPersonData(VMPersonDataVariable::NeighborId);
                std::cout<<"SOCIAL_TARGET nid="<<visitorNID<<" guid="<<visitorGUID<<" name="<<json_bytes(guest->Name())<<" home="<<provider->GetHouseOfNeighbor(neighbor)<<" hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<std::endl;
                for(const auto& choice:choices)std::cout<<"SOCIAL_OFFERED_MENU "<<json_bytes(choice.Name)<<std::endl;
                cancel();Push(s,guest,"Greet",greeting);Require(bool(greeting),"offered genuine Greet could not be enqueued; inspect exact menu/queue");break;
            }
            if(!greeting){s.Update(2);Notices(s);}
        }
        Require(bool(greeting),"no genuine external walkby with an offered Greet in the bounded three-hour window");
        auto actor=s.SelectedSim();auto routine=greeting->ActionRoutine;
        GreetDiagnostic diagnostic{actor,[&]{return live(visitorGUID);}};
        const int beforeStatus=live(visitorGUID)?live(visitorGUID)->GetPersonData(VMPersonDataVariable::GreetStatus):-1;
        size_t traceHits=0;int lastOpcode=-1,lastResult=-1;const auto oldTrace=s.VM()->OnTraceInstruction;
        const auto traceObserver=[&](Engine::VMStackFrame* frame,uint16_t opcode,int result){
            if(oldTrace)oldTrace(frame,opcode,result);
            diagnostic.Trace(frame,opcode,result);
            if(frame&&frame->Thread==actor->Thread.get()&&std::any_of(actor->Thread->Stack.begin(),actor->Thread->Stack.end(),[&](const auto& item){return item->Routine==routine;})){
                ++traceHits;lastOpcode=opcode;lastResult=result;
            }
        };
        auto observeUntil=[&](int minutes,const std::function<bool()>& done){
            const auto rate=std::max(1,s.Context()->Clock->TicksPerMinute);const auto deadline=s.Context()->Clock->Ticks+int64_t(minutes)*rate;
            for(int64_t updates=0;s.Context()->Clock->Ticks<deadline&&updates<int64_t(minutes)*rate*8+1024;++updates){s.Update(2);Notices(s);if(done())return true;}return false;
        };
        auto routineFinished=[&]{return traceHits>0&&std::none_of(actor->Thread->Stack.begin(),actor->Thread->Stack.end(),[&](const auto& frame){return frame->Routine==routine;})&&std::none_of(actor->Thread->Queue.begin(),actor->Thread->Queue.end(),[&](const auto& q){return q->ActionRoutine==routine;});};
        bool admitted=false;std::vector<VMPieMenuInteraction> afterMenu;
        s.VM()->OnTraceInstruction=traceObserver;
        observeUntil(30,[&]{
            diagnostic.AfterUpdate(s);if(diagnostic.stopped)return true;
            auto guest=live(visitorGUID);if(!guest||guest->Position()==FSO::LotView::Model::LotTilePos::OUT_OF_WORLD){diagnostic.Stop("visitor-left-before-admission");return true;}
            admitted=guest->GetPersonData(VMPersonDataVariable::GreetStatus)==1;return admitted;
        });s.VM()->OnTraceInstruction=oldTrace;
        diagnostic.Summary(s);Require(admitted,"stable social probe stopped before admission; inspect the durable state before changing product");
        afterMenu=live(visitorGUID)->GetPieMenu(s.VM(),actor,false);
        const bool finished=routineFinished();
        const int afterStatus=live(visitorGUID)?live(visitorGUID)->GetPersonData(VMPersonDataVariable::GreetStatus):-1;
        auto relation=VMTS1Neighbourhood::GetRelationship(actor->GetPersonData(VMPersonDataVariable::NeighborId),visitorNID,false);
        std::cout<<"SOCIAL_GREETING routine="<<(routine?routine->ID:0)<<" trace_hits="<<traceHits<<" last_opcode="<<lastOpcode<<" last_result="<<lastResult<<" finisher_complete="<<finished<<" admitted="<<admitted<<" status_before="<<beforeStatus<<" status_after="<<afterStatus<<" hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<" queue="<<app.DescribeQueue()<<std::endl;
        std::cout<<"SOCIAL_RELATION nid="<<visitorNID<<" present="<<bool(relation)<<" values=";if(relation)for(auto value:*relation)std::cout<<value<<',';std::cout<<std::endl;
        for(const auto& choice:afterMenu)std::cout<<"SOCIAL_AFTER_MENU "<<json_bytes(choice.Name)<<std::endl;
        Require(finished&&admitted,"Greet finisher/admission not observed while the genuine visitor remained present");
        if(!relation){
            auto guest=live(visitorGUID);Require(guest!=nullptr,"admitted visitor left before original conversation");
            std::shared_ptr<Engine::VMQueuedAction> talk;Require(Push(s,guest,"Talk",talk),"admitted visitor has no offered original Talk action to establish contact");
            routine=talk->ActionRoutine;traceHits=0;lastOpcode=lastResult=-1;s.VM()->OnTraceInstruction=traceObserver;
            observeUntil(45,[&]{
                if(!live(visitorGUID))return true;
                relation=VMTS1Neighbourhood::GetRelationship(actor->GetPersonData(VMPersonDataVariable::NeighborId),visitorNID,false);
                const bool actionFrames=std::any_of(actor->Thread->Stack.begin(),actor->Thread->Stack.end(),[](const auto& frame){return frame->ActionTree;});
                return relation&&routineFinished()&&!actor->Thread->ActiveAction()&&!actionFrames;
            });s.VM()->OnTraceInstruction=oldTrace;
            std::cout<<"SOCIAL_TALK trace_hits="<<traceHits<<" last_result="<<lastResult<<" contact_present="<<bool(relation)<<" hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<" queue="<<app.DescribeQueue()<<std::endl;
            Require(traceHits>0&&relation,"original conversation did not establish a directional contact in its bounded window");
        }
        Require(relation!=nullptr,"completed original social actions did not create a genuine directional contact");
        auto guest=live(visitorGUID);bool goodbyeIssued=false;
        if(guest&&guest->Position()!=FSO::LotView::Model::LotTilePos::OUT_OF_WORLD){
            for(const auto& choice:guest->GetPieMenu(s.VM(),actor,false))std::cout<<"SOCIAL_AFTER_MENU "<<json_bytes(choice.Name)<<std::endl;
            cancel();std::shared_ptr<Engine::VMQueuedAction> goodbye;Require(Push(s,guest,"Say Goodbye",goodbye),"no original Say Goodbye action offered after greeting");goodbyeIssued=true;observeUntil(60,[&]{auto current=live(visitorGUID);return !current||current->Position()==FSO::LotView::Model::LotTilePos::OUT_OF_WORLD;});
        }else std::cout<<"SOCIAL_VISITOR_DEPARTED_NATURALLY\n";
        guest=live(visitorGUID);Require(!guest||guest->Position()==FSO::LotView::Model::LotTilePos::OUT_OF_WORLD,"original goodbye did not let the visitor leave within one hour");
        Require(off.household==0,"household made autonomous choices during the off social scenario");Save(app);Snapshot(s,save/"autonomy-social-progress.txt");
        std::cout<<"AUTONOMY_RESULT {\"phase\":\"greet\",\"contact_nid\":"<<visitorNID<<",\"contact_guid\":"<<visitorGUID<<",\"greet_finished\":true,\"visitor_departed\":true,\"goodbye_issued\":"<<goodbyeIssued<<",\"saved\":true,\"household_choices\":"<<off.household<<",\"visitor_choices\":"<<off.visitors<<"}\n";return 0;
    }
    if(phase=="visitor"){
        if(fs::exists(save/"autonomy-social-progress.txt")){Reopened(s,save/"autonomy-social-progress.txt");std::cout<<"SOCIAL_FRESH_REOPEN identities_budget_career_skills=PASS"<<std::endl;}
        FreeWill(app,false);Observation off;off.Attach(s);std::shared_ptr<Engine::VMQueuedAction> call;
        for(auto e:s.VM()->Entities)if((e->ToString().find("Phone")!=std::string::npos||e->ToString().find("phone")!=std::string::npos)&&Push(s,e,"Call Neighbor",call))break;
        Require(bool(call),"no authentic Call Neighbor interaction");
        const auto end=s.Context()->Clock->Ticks+int64_t(120)*s.Context()->Clock->TicksPerMinute;
        while(s.Context()->Clock->Ticks<end){s.Update(2);if(!s.Dialogs.empty()&&s.Dialogs.front().Type==10)break;Notices(s);}
        Require(!s.Dialogs.empty()&&s.Dialogs.front().Type==10,"Call Neighbor did not open its original phone book");
        const auto phonebook=s.Dialogs.front();
        std::cout<<"PHONEBOOK_READY "<<phonebook.ChoicesReady<<" diagnostic="<<json_bytes(phonebook.ChoicesDiagnostic)<<std::endl;
        for(const auto& choice:phonebook.Choices)std::cout<<"PHONEBOOK_CONTACT nid="<<choice.Value<<" guid="<<choice.GUID<<" label="<<json_bytes(choice.Label)<<" portrait="<<choice.Portrait.Width<<"x"<<choice.Portrait.Height<<std::endl;
        app.Frame(AppInput(),0);
        if(argc==5||std::string(argv[5])=="inspect"){
            const auto capture=save.parent_path()/"phonebook-selector.ppm";std::ofstream out(capture,std::ios::binary);
            Require(bool(out),"cannot write Canvas capture");const auto& canvas=app.DesktopCanvas();out<<"P6\n"<<canvas.Width<<' '<<canvas.Height<<"\n255\n";
            for(auto pixel:canvas.Px){out.put(char(pixel));out.put(char(pixel>>8));out.put(char(pixel>>16));}out.close();Require(bool(out),"Canvas capture write failed");
            std::cout<<"CANVAS_CAPTURE "<<capture<<" dimensions="<<canvas.Width<<'x'<<canvas.Height<<" before_any_contact_selection=true\n";
            std::cout<<"AUTONOMY_PENDING explicit offered NID required for invitation scenario\n";return 3;
        }
        const auto requested=std::stoi(argv[5]);
        if(requested==-1){
            auto caller=dynamic_cast<VMAvatar*>(s.VM()->GetObjectById(phonebook.CallerID));Require(caller&&caller->Thread,"phonebook caller missing");
            const auto source=std::static_pointer_cast<FSO::SimAntics::Primitives::VMDialogResult>(caller->Thread->BlockingState);
            Require(source&&!source->Responded,"phonebook source not pending before Cancel pointer");ClickControl(app,"simdlg1");
            Require(source->Responded&&source->ResponseCode==0&&source->ResponseText=="-1","original Cancel pointer did not answer code0/text-1");
            Require(s.Dialogs.empty()||s.Dialogs.front().Type!=10,"Cancel pointer left phonebook open");
            std::cout<<"AUTONOMY_RESULT {\"phase\":\"phonebook-cancel\",\"pointer_cancel\":true,\"response_code\":0,\"response_text\":\"-1\"}\n";return 0;
        }
        const auto target=std::find_if(phonebook.Choices.begin(),phonebook.Choices.end(),[&](const auto& entry){return entry.Value==requested;});
        Require(phonebook.ChoicesReady&&target!=phonebook.Choices.end(),"requested invitation target was not offered by the original phonebook");
        Require(std::none_of(s.Family.begin(),s.Family.end(),[&](auto member){return member->Object->GUID==target->GUID;}),"invitation scenario requires an actual nonresident contact; roster remains unchanged");
        for(size_t step=0;step<=size_t(target-phonebook.Choices.begin());++step){AppInput down;down.Down=BTN_DOWN;app.Frame(down,0);}
        std::cout<<"SCRIPTED_PHONEBOOK_CALL nid="<<requested<<" name="<<json_bytes(target->Label)<<std::endl;ClickControl(app,"simdlg0");
        const auto visitEnd=s.Context()->Clock->Ticks+int64_t(180)*s.Context()->Clock->TicksPerMinute;
        bool invitation=false;
        while(s.Context()->Clock->Ticks<visitEnd){
            s.Update(2);
            if(!s.Dialogs.empty()&&s.Dialogs.front().Type==1){
                const auto d=s.Dialogs.front();
                if(!invitation&&d.Yes=="Invite Over"){
                    std::cout<<"SCRIPTED_INVITE_OVER "<<json_bytes(d.Title)<<" "<<json_bytes(d.Message)<<std::endl;
                    app.Frame(AppInput(),0);ClickControl(app,"simdlg0");invitation=true;
                }else Notices(s);
            }else Notices(s);
            if(invitation&&off.visitorIds.count(target->GUID)>0){std::cout<<"INVITED_VISITOR_AUTONOMY guid="<<target->GUID<<" hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<std::endl;break;}
        }
        Require(invitation,"original Invite Over choice did not appear; inspect observed dialog");
        Require(off.household==0,"household chose autonomous action during off invitation");
        Require(off.visitorIds.count(target->GUID)>0,"invited Sim did not make an observed autonomous choice while household off");
        Save(app);std::cout<<"AUTONOMY_RESULT {\"phase\":\"visitor\",\"invited_nid\":"<<requested<<",\"visitor_choices\":"<<off.visitors<<",\"household_choices\":"<<off.household<<"}\n";return 0;
    }
    Require(phase=="run","school case is reported separately; run or reopen expected");
    FreeWill(app,true);Require(app.Front()->Settings().AutonomyLevel==0,"PC toggle invented a numeric level");FreeWill(app,false);
    auto human=s.SelectedSim();Require(human&&!human->IsPet(),"selected household human missing");
    Require(std::any_of(s.Family.begin(),s.Family.end(),[](auto a){return a->IsPet();}),"authentic household pet missing");
    Observation off;off.Attach(s);Advance(s,60);std::shared_ptr<Engine::VMQueuedAction> manual;
    for(auto e:s.VM()->Entities)if(Push(s,e,"Snack",manual))break;
    Require(bool(manual),"no authentic manual snack interaction available");
    const int hungerBefore=human->GetMotiveData(VMMotive::Hunger);int hungerPeak=hungerBefore;bool manualStarted=false,manualFinished=false,greetQueued=false,greetStarted=false,greetFinished=false;std::shared_ptr<Engine::VMQueuedAction> greeting;
    std::cout<<"OFF_BEGIN hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<std::endl;
    Advance(s,360,[&]{
        hungerPeak=std::max(hungerPeak,int(human->GetMotiveData(VMMotive::Hunger)));
        if(human->Thread->ActiveAction()==manual)manualStarted=true;
        if(manualStarted&&std::find(human->Thread->Queue.begin(),human->Thread->Queue.end(),manual)==human->Thread->Queue.end())manualFinished=true;
        if(greeting&&human->Thread->ActiveAction()==greeting)greetStarted=true;
        if(greetStarted&&!greetFinished&&std::find(human->Thread->Queue.begin(),human->Thread->Queue.end(),greeting)==human->Thread->Queue.end()){greetFinished=true;std::cout<<"GREET_FINISHED hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<std::endl;}
        if(!greetQueued&&manualFinished)for(auto e:s.VM()->Entities){auto a=dynamic_cast<VMAvatar*>(e);if(a&&!a->IsPet()&&a->GetPersonData(VMPersonDataVariable::PersonType)==1){for(auto& c:a->GetPieMenu(s.VM(),human,false))std::cout<<"VISITOR_MENU "<<json_bytes(c.Name)<<std::endl;if(Push(s,a,"Greet",greeting)){greetQueued=true;break;}}}
    });
    std::cout<<"OFF_END hour="<<s.Context()->Clock->Hours<<" minute="<<s.Context()->Clock->Minutes<<" household="<<off.household<<" pets="<<off.pets<<" visitors="<<off.visitors<<" greeted="<<greetQueued<<std::endl;
    std::cout<<"HUMAN_QUEUE "<<app.DescribeQueue()<<std::endl;
    for(auto e:s.VM()->Entities)if(auto a=dynamic_cast<VMAvatar*>(e))if(a->GetPersonData(VMPersonDataVariable::PersonType)==1&&!a->IsPet())std::cout<<"VISITOR "<<a->Object->GUID<<" name="<<json_bytes(a->Name())<<" greet="<<a->GetPersonData(VMPersonDataVariable::GreetStatus)<<" queue="<<a->Thread->Queue.size()<<std::endl;
    Require(off.household==0,"household chose a new autonomous action while off");Require(manualStarted&&manualFinished,"manual order did not start and terminate while off");Require(off.pets>0,"pet did not make an autonomous choice while household off");const bool visitorPending=off.visitors==0;
    std::cout<<"MANUAL_EFFECT hunger_before="<<hungerBefore<<" hunger_peak="<<hungerPeak<<std::endl;
    Require(hungerPeak>hungerBefore,"manual snack terminated without an observed hunger improvement");
    if(visitorPending)for(auto e:s.VM()->Entities)if(e->ToString().find("Phone")!=std::string::npos||e->ToString().find("phone")!=std::string::npos)for(const auto& c:e->GetPieMenu(s.VM(),human,false))std::cout<<"PHONE_MENU "<<json_bytes(e->ToString())<<" "<<json_bytes(c.Name)<<std::endl;
    Observation on;on.Attach(s);FreeWill(app,true);Advance(s,20);
    if(on.household==0&&human->Thread->ActiveAction()){
        auto active=human->Thread->ActiveAction();std::cout<<"MANUAL_CANCEL before on observation "<<json_bytes(active->Name)<<" uid="<<active->UID<<std::endl;s.CancelAction(active->UID);
        Advance(s,100);
    }
    Require(on.household>0,"household autonomy did not return after checkbox on");FreeWill(app,false);
    Save(app);
    Snapshot(s,save/"autonomy-progress.txt");
    std::cout<<"AUTONOMY_RESULT {\"phase\":\"run\",\"off_household_choices\":"<<off.household<<",\"off_pet_choices\":"<<off.pets<<",\"off_visitor_choices\":"<<off.visitors<<",\"manual_started\":"<<manualStarted<<",\"manual_finished\":"<<manualFinished<<",\"hunger_before\":"<<hungerBefore<<",\"hunger_after\":"<<human->GetMotiveData(VMMotive::Hunger)<<",\"on_household_choices\":"<<on.household<<",\"resets\":"<<s.Resets<<"}\n";
    if(visitorPending){std::cerr<<"AUTONOMY_PENDING visitor autonomous choice not observed; independent saved dimensions retained\n";return 2;}
    return 0;
}catch(const std::exception& e){std::cerr<<"AUTONOMY_FAILURE "<<e.what()<<'\n';return 1;}}
