/* SPDX-License-Identifier: MPL-2.0 */
// Asset-backed App frame scheduling fixture. No input saves or state edits.
#include "app/App.h"
#include "app/front/FrontEnd.h"
#include "app/front/ResGraphics.h"
#include "files/formats/iff/chunks/STR.h"
#include "odh/loading_label.hpp"
#include "simantics/VM.h"
#include <iostream>
#include <stdexcept>
using namespace simapp;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void click(App& a,int x,int y){AppInput i;i.DesktopPointer=true;i.TouchX=x;i.TouchY=y;i.TouchDown=i.Touching=true;a.Frame(i,0);i.TouchDown=i.Touching=false;i.TouchUp=true;a.Frame(i,0);}

// Exercise the actual App/front-end draw path. Fixture publication is limited
// to PendingDialog, just as the scheduling modes below use the VM callback.
static void frame16(App& a,AppInput input=AppInput{}){a.Frame(input,16);}
static void button16(App& a,uint32_t button){AppInput input;input.Down=button;frame16(a,input);}
static void click16(App& a,int x,int y){AppInput input;input.DesktopPointer=true;input.TouchX=x;input.TouchY=y;input.TouchDown=input.Touching=true;frame16(a,input);input.TouchDown=input.Touching=false;input.TouchUp=true;frame16(a,input);}
static void reportPresented(const App& a,const char* step){
 const auto& p=a.DrawnFrame();
 std::cout<<"PRESENTED_FIXTURE step="<<step<<" scene="<<int(p.scene)<<" house="<<p.house<<" area="<<p.area
  <<" modal="<<p.modal<<" family_picker="<<p.family_picker<<" destination_picker="<<p.destination_picker
  <<" cheats="<<p.cheats<<" pie="<<p.pie<<"\n";
}
static bool plainMap(const FramePresentation& p){return p.scene==FramePresentation::Scene::Map&&!p.modal&&!p.family_picker&&!p.destination_picker&&!p.cheats&&!p.pie;}
static bool plainLot(const FramePresentation& p){return p.scene==FramePresentation::Scene::Lot&&p.house==9&&!p.modal&&!p.family_picker&&!p.destination_picker&&!p.cheats&&!p.pie;}
static int presented(App& a){
 a.Speed=0;
 bool sawLoading=false,sawTransitionLoading=false,sawMap=false;
 for(int frame=0;frame<1200;++frame){
  frame16(a);
  const auto& p=a.DrawnFrame();
  if(p.scene==FramePresentation::Scene::Loading)sawLoading=true;
  if(a.Front()->CurrentState()==front::FrontEnd::State::Neighbourhood&&p.scene==FramePresentation::Scene::Loading)
   sawTransitionLoading=true;
  if(plainMap(p)){sawMap=true;break;}
 }
 require(sawLoading&&sawTransitionLoading&&sawMap,"natural Loading->map draw boundary missing");
 require(a.CurrentMode()==App::Mode::Front&&!a.Session(),"map observation loaded a house");
 reportPresented(a,"map-after-loading");

 // Normal map Quit/Cancel input, without accepting Quit or writing settings.
 button16(a,BTN_START);
 require(a.DrawnFrame().scene==FramePresentation::Scene::Map&&a.DrawnFrame().modal,"map Quit dialog not painted");
 reportPresented(a,"map-quit-dialog");
 button16(a,BTN_B);
 require(plainMap(a.DrawnFrame())&&!a.QuitRequested,"map Cancel did not restore its input layer");
 reportPresented(a,"map-quit-cancelled");

 // Family-bin presentation is a real existing PC button and cancellation.
 int x=0,y=0;
 require(a.ScriptButtonPos("movein",x,y),"family-bin button missing on drawn map");
 click16(a,x,y);
 require(a.DrawnFrame().scene==FramePresentation::Scene::Map&&a.DrawnFrame().family_picker,"family picker not painted");
 reportPresented(a,"family-picker");
 button16(a,BTN_B);
 require(plainMap(a.DrawnFrame()),"family picker Cancel did not clear its layer");

 // ScriptHousePos may finish boot: call it ONLY after the genuine map above.
 require(plainMap(a.DrawnFrame()),"house lookup preceded drawn map readiness");
 require(a.ScriptHousePos(9,x,y),"house missing from drawn map");
 click16(a,x,y);
 for(int frame=0;frame<240&&!a.Session();++frame)frame16(a);
 require(a.Session()&&a.CurrentMode()==App::Mode::Live,"house not live");
 a.Speed=0;
 frame16(a);
 require(plainLot(a.DrawnFrame()),"completed house draw not observed");
 auto* s=a.Session();auto vm=s->VM();
 require(s->Dialogs.empty(),"unexpected baseline dialog in presented fixture");
 reportPresented(a,"lot");

 PendingDialog regular;regular.Title="Presented fixture";regular.Message="A controlled painted dialogue.";regular.HasYes=true;regular.Block=true;
 s->Dialogs.push_back(regular);
 frame16(a);
 require(a.DrawnFrame().scene==FramePresentation::Scene::Lot&&a.DrawnFrame().modal,"regular dialogue did not report its paint result");
 reportPresented(a,"regular-dialogue");
 button16(a,BTN_A);
 require(s->Dialogs.empty()&&plainLot(a.DrawnFrame()),"normal dialogue response left a painted modal");

 // Publish after SimDialogView::Update, from the actual simulation callback.
 // With no caller/wait source this pet choice must not paint, even though the
 // queue contains it during Draw. Resolve-without-owner permits its ordinary
 // stale-ticket cleanup on the following Update, without a synthetic response.
 bool published=false;const auto before=s->Updates;
 vm->OnTraceInstruction=[&](auto*,uint16_t,int){
  if(published)return;published=true;
  PendingDialog pet;pet.Type=9;pet.Title="Source-less pet fixture";pet.Block=true;pet.ChoiceSourceResolved=true;
  pet.Choices.resize(1);pet.Choices[0].Label="No owner";
  s->Dialogs.push_back(pet);
 };
 a.Speed=1;
 for(int frame=0;frame<120&&!published;++frame)frame16(a);
 vm->OnTraceInstruction=nullptr;a.Speed=0;
 require(published&&s->Updates>before,"pet fixture was not published by a real VM update");
 require(s->Dialogs.size()==1&&s->Dialogs.front().Type==9&&!s->PetChoiceCurrent(s->Dialogs.front()),"source-less pet fixture changed before draw inspection");
 require(plainLot(a.DrawnFrame()),"suppressed pet choice falsely reported a painted modal");
 reportPresented(a,"pet-suppressed");
 frame16(a);
 require(s->Dialogs.empty()&&plainLot(a.DrawnFrame()),"stale pet cleanup retained modal metadata");
 reportPresented(a,"pet-next-frame");

 // Existing chord opens the console; normal Cancel closes it. No cheat text.
 AppInput chord;chord.Held=BTN_L|BTN_R;chord.Down=BTN_SELECT;frame16(a,chord);
 require(a.DrawnFrame().scene==FramePresentation::Scene::Lot&&a.DrawnFrame().cheats,"console overlay not reported");
 reportPresented(a,"cheats-open");
 button16(a,BTN_B);frame16(a);
 require(plainLot(a.DrawnFrame()),"console Cancel retained its presentation flag");

 PendingDialog destination;destination.Type=5;destination.Title="Destination fixture";destination.Block=true;
 s->Dialogs.push_back(destination);
 frame16(a);
 require(a.CurrentMode()==App::Mode::Live&&a.Session()==s,"destination fixture changed the household session");
 require(a.DrawnFrame().scene==FramePresentation::Scene::Map&&a.DrawnFrame().destination_picker&&a.DrawnFrame().house==-1,"destination map inherited lot readiness");
 reportPresented(a,"destination-picker");
 button16(a,BTN_B);
 require(s->Dialogs.empty()&&!a.Front()->Picking(),"normal destination Cancel did not complete");
 require(a.DrawnFrame().scene==FramePresentation::Scene::Map&&a.DrawnFrame().destination_picker,"selection-completion frame lost its drawn destination layer");
 reportPresented(a,"destination-cancel-frame");
 frame16(a);
 require(a.Session()==s&&plainLot(a.DrawnFrame()),"destination Cancel did not restore the drawn house layer");
 reportPresented(a,"lot-after-destination-cancel");
 std::cout<<"PASS presented: real App.Frame draws, controlled dialogue publication, no Save or committed trip\n";
 return 0;
}

static int loadingArt(const std::string& root) {
 using namespace simapp::front;
 auto& ui=ResGraphics::Get();
 require(ui.Open(root+"/UIGraphics"),"UI archive unavailable");
 Canvas shown(800,600);
 const auto initial=RGBA(149,87,53);
 shown.Clear(initial);
 DrawLotLoadStart(shown,shown,nullptr,false);
 require(std::all_of(shown.Px.begin(),shown.Px.end(),[&](auto px){return px==initial;}),"home replaced the desktop map");
 for(const auto* resource:{"kNeighborhoodGo","kDTGo","kVacationGo","kVacationGo2","kStudiotownGo","kMagicLandGo"}) {
  Canvas expected;
  require(ui.Load(resource,expected,false)&&expected.Width==800&&expected.Height==600,"destination artwork unavailable");
  DrawLotLoadStart(shown,shown,resource,false);
  require(shown.Px==expected.Px,"desktop destination has extra startup pixels");
  DrawLotLoadStart(shown,shown,resource,true);
  require(shown.Px!=expected.Px,"missing handheld status");
  for(int y=0;y<600;++y)for(int x=0;x<800;++x)
   if(y<560||y>=588||x<310||x>=490)require(shown.Px[y*800+x]==expected.Px[y*800+x],"status damaged destination artwork");
 }
 using namespace FSO::Files::Formats::IFF::Chunks;
 Canvas english,portuguese;
 const auto systemLanguage=STR::DefaultLangCode;
 for(auto code:{STRLangCode::EnglishUS,STRLangCode(14)}) {
  STR::DefaultLangCode=code;
  shown.Clear(initial);
  DrawLotLoadStart(shown,shown,nullptr,true);
  require(shown.Px.front()==initial&&shown.Px[573*800+315]!=initial,"home handheld status or outgoing map missing");
  (code==STRLangCode::EnglishUS?english:portuguese)=shown;
 }
 STR::DefaultLangCode=systemLanguage;
 require(odh_host::loading_label(14)=="Carregando..."&&english.Px!=portuguese.Px,"handheld text did not follow language");
 Canvas top(400,240),bottom(320,240);
 top.Clear(initial); bottom.Clear(initial);
 DrawLotLoadStart(top,bottom,nullptr,true);
 require(top.Px.front()==initial&&top.Px.back()==initial&&bottom.Px.front()!=initial,"dual-screen home transition");
 auto first=bottom.Px;
 DrawLotLoadBar(bottom,0.75f);
 require(bottom.Px!=first,"missing dual-screen load progress");
 std::cout<<"PASS: six clean desktop destination pictures; localized handheld status, single/dual screen homes, progress."<<std::endl;
 return 0;
}
int main(int argc,char**argv){try{
 require(argc==5,"usage: frame-boundary DATA SAVE CACHE dialog|notice|travel|normal|edge|presented");
 if(std::string(argv[4])=="loading-art")return loadingArt(argv[1]);
 App a;App::Config c;c.DataRoot=argv[1];c.SaveRoot=argv[2];c.CacheDir=argv[3];c.Desktop=true;c.SkipIntro=true;c.VMBudgetMs=0;require(a.Init(c),"init failed");
 if(std::string(argv[4])=="presented")return presented(a);
 int x=0,y=0;for(int i=0;i<240&&!a.ScriptHousePos(9,x,y);++i)a.Frame(AppInput{},0);require(a.ScriptHousePos(9,x,y),"house missing");click(a,x,y);
 for(int i=0;i<240&&!a.Session();++i)a.Frame(AppInput{},0);require(a.Session()&&a.CurrentMode()==App::Mode::Live,"house not live");
 auto s=a.Session();require(s->Dialogs.empty(),"unexpected baseline dialog");auto vm=s->VM();const std::string mode=argv[4];
 bool hook=false;int64_t boundary=0;const auto before=s->Updates;
 vm->OnTraceInstruction=[&](auto*,uint16_t,int){if(hook||mode=="normal"||(mode=="edge"&&s->Updates-before<151))return;hook=true;boundary=s->Updates+1;
  if(mode=="travel"){require(bool(vm->OnRequestLotSwitch),"App travel consumer missing");vm->OnRequestLotSwitch(-1);}
  else {PendingDialog d;d.Title="Scheduling fixture";d.Message="A controlled callback boundary.";d.HasYes=true;d.Block=mode=="dialog"||mode=="edge";s->Dialogs.push_back(d);}
 };
 a.Speed=3;a.Frame(AppInput{},1000);vm->OnTraceInstruction=nullptr;
 const auto after=s->Updates;const bool stop=mode=="dialog"||mode=="travel"||mode=="edge";
 std::cout<<"FRAME_BOUNDARY mode="<<mode<<" before="<<before<<" boundary="<<boundary<<" after="<<after<<" hook="<<hook<<"\n";
 require(after>before,"ordinary simulation never advanced");if(stop)require(hook&&after==boundary,"App ran updates after a blocking frontend boundary");else require(after-before>2,"ordinary or nonblocking notice stalled simulation");
 if(mode=="edge") {require(s->Dialogs.size()==1&&s->Dialogs.front().Title=="Scheduling fixture","controlled dialog changed");s->Dialogs.clear();const auto resumed=s->Updates;a.Frame(AppInput{},2);std::cout<<"FRAME_RESUME delta="<<(s->Updates-resumed)<<"\n";require(s->Updates==resumed+1,"discarded frame budget left a negative update debt");}
 std::cout<<"PASS no Save/travel Frame processed, no dialog response, diagnostic callback only\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
