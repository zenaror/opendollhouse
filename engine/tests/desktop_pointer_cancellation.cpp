/* SPDX-License-Identifier: MPL-2.0 */
#include "app/LotScreen.h"
#include "app/App.h"
#include "app/build/UIBuildMode.h"
#include "app/build/UIFloorPainter.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include <cstdlib>
#include "app/buy/UIObjectHolder.h"
#include <cstdio>
#include <stdexcept>

static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Optional protected-data fixture: DATA_ROOT SAVE_ROOT CACHE_ROOT HOUSE.
// No Save call. This supplements the no-data routing cases in ordinary CTest.
static void ArmedBuildReturn(char** argv){
    simapp::App app;simapp::App::Config cfg;cfg.Desktop=true;cfg.SkipIntro=true;
    cfg.DataRoot=argv[1];cfg.SaveRoot=argv[2];cfg.CacheDir=argv[3];
    Require(app.Init(cfg),"isolated build fixture initialization failed");
    int x=0,y=0;Require(app.ScriptHousePos(std::atoi(argv[4]),x,y),"fixture house not found");
    simapp::AppInput press;press.DesktopPointer=true;press.TouchX=x;press.TouchY=y;press.TouchDown=press.Touching=true;
    app.Frame(press,0);press.TouchDown=press.Touching=false;press.TouchUp=true;app.Frame(press,0);
    for(int frame=0;frame<100 && app.CurrentMode()!=simapp::App::Mode::Live;++frame)app.Frame({},0);
    Require(app.CurrentMode()==simapp::App::Mode::Live&&app.Lot()&&app.Session(),"fixture house did not become live");
    auto& lot=*app.Lot();lot.CurrentMode=simapp::LotScreen::Mode::Build;
    lot.Build->CurrentTool=simapp::UIBuildMode::Tool::Floor;lot.Build->Selection=37;
    lot.Build->Control=std::make_unique<simapp::UIFloorPainter>(app.Session(),std::vector<int>{1});
    const auto* selected=lot.Build->Control.get();
    lot.P={};lot.P.Active=lot.P.Down=lot.P.Held=true;lot.P.X=200;lot.P.Y=200;lot.Build->PointerUpdate();
    lot.P.Down=false;lot.P.X=400;lot.P.Y=300;lot.Build->PointerUpdate();
    auto arch=app.Session()->Context()->Architecture();
    bool armed=false;for(const auto& cmd:arch->Commands)armed=armed||cmd.x2>0||cmd.y2>0;
    Require(armed,"fixture did not arm a real multi-tile floor stroke");
    const int budget=app.Session()->Budget();
    simapp::AppInput cancel;cancel.DesktopPointer=true;cancel.PointerCancelled=true;lot.Input(cancel,0,0);
    Require(arch->Commands.empty(),"cancel retained build preview commands");
    lot.P={};lot.P.Active=true;lot.P.X=420;lot.P.Y=320;lot.Build->PointerUpdate();
    Require(!arch->Commands.empty(),"active hover did not restore the selected floor preview");
    for(const auto& cmd:arch->Commands)Require(cmd.x2==0&&cmd.y2==0,"return to active canvas resumed the cancelled multi-tile Drawing stroke");
    Require(lot.Build->Control.get()==selected&&lot.Build->Selection==37,"cancel discarded the selected build tool");
    Require(app.Session()->Budget()==budget,"cancel or hover committed a build transaction");
    lot.Build->Control->Release();lot.Build->Control.reset();
    std::puts("PASS armed real Build cancellation, active return, preserved selection and budget");
}
int main(int argc,char** argv){try{
    // Exercise actual LotScreen routing and actual holder consumers, with an
    // empty session: no game data, simulation, purchase, placement or save.
    simapp::LotSession session;simapp::UIStrings strings;
    simapp::Canvas top(800,600),bottom(800,600);
    simapp::LotScreen lot(&session,&strings,&top,&bottom);lot.Desktop=true;
    for(const auto mode:{simapp::LotScreen::Mode::Buy,simapp::LotScreen::Mode::Options}){
        lot.CurrentMode=mode;
        auto& holder=*lot.Holder;
        holder.Holding=std::make_unique<simapp::UIObjectSelection>();
        holder.Holding->Clicked=true;
        holder.MouseDown(200,200);
        const auto* ghost=holder.Holding.get();
        lot.P.Active=lot.P.Down=lot.P.Held=true;
        simapp::AppInput cancel;cancel.DesktopPointer=true;cancel.PointerCancelled=true;
        // Stale flags must not escape the cancellation route to actions.
        cancel.Down=simapp::BTN_B|simapp::BTN_START;cancel.TouchUp=true;
        cancel.DesktopDelete=true;cancel.MouseRightDown=true;
        lot.Input(cancel,16,100);
        Require(holder.Holding.get()==ghost&&!holder.Holding->Clicked,
                "cancellation must reset the gesture without deleting/placing the ghost");
        holder.MouseUp(200,200);
        Require(holder.Holding.get()==ghost&&!holder.Holding->Clicked,
                "a later button release cannot commit the cancelled gesture");
        Require(!lot.P.Active&&!lot.P.Down&&!lot.P.Held&&!lot.P.Up,
                "cancelled primary pointer cannot remain active or emit Up");
        Require(lot.CurrentMode==mode&&!lot.LeaveRequested&&!lot.QuitRequested,
                "cancel must not become Escape, a mode action or a departure");
    }
    std::puts("PASS actual LotScreen cancellation routing to Buy/Options and holder without action");
    if(argc==5)ArmedBuildReturn(argv);
    else Require(argc==1,"usage: [DATA_ROOT SAVE_ROOT CACHE_ROOT HOUSE]");
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"cancellation routing: %s\n",error.what());return 1;}}
