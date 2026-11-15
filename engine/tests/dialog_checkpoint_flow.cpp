// SPDX-License-Identifier: MPL-2.0
// Asset-backed persistence boundary test. Supply isolated SAVE/CACHE roots.
// Saves an ordinary cooking notice before and immediately after normal UI OK.
// Saving with a modal through SaveLot is a prepared boundary, not a claim that
// the player's Save button is accessible while that modal is displayed.
#include "app/App.h"
#include "app/front/FrontEnd.h"
#include "app/front/LotSave.h"
#include "simantics/VM.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMDialogOperand.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include "files/formats/iff/IffRawFile.h"
#include "tools/json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace fs = std::filesystem;
using namespace simapp;
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::Files::Formats::IFF;
static void need(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
static void click(App& app, int x, int y) {
    AppInput i; i.DesktopPointer=true; i.TouchX=x; i.TouchY=y;
    i.TouchDown=i.Touching=true; app.Frame(i,0);
    i.TouchDown=i.Touching=false; i.TouchUp=true; app.Frame(i,0);
}
static void button(App& app, const std::string& name) {
    int x=0,y=0; need(app.ScriptButtonPos(name,x,y),"missing button "+name); click(app,x,y);
}
static std::vector<uint8_t> state(LotSession& session) {
    IffRawFile raw; std::string error;
    need(raw.Load(session.LoadedHousePath,&error),error);
    FSO::SimAntics::Engine::Utils::VMTS1InteractionCheckpoint::Capture(*session.VM(),raw,false);
    const auto chunk=raw.Find("ODHI",1); need(chunk&&chunk->Data.size()>8,"missing ODHI");
    auto bytes=chunk->Data; std::fill(bytes.begin()+4,bytes.begin()+8,0); return bytes;
}
static std::vector<uint8_t> read(const fs::path& path) {
    std::ifstream in(path,std::ios::binary); need(bool(in),"cannot read state");
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
static void save(App& app, const fs::path& expected) {
    auto s=app.Session(); const auto before=state(*s); const int budget=s->Budget();
    std::string error,log; need(front::SaveLot(*s,error,log),"SaveLot: "+error);
    need(app.Front()->SaveNeighbourhood(),"SaveNeighbourhood: "+app.Front()->Error);
    const auto after=state(*s); need(after==before&&s->Budget()==budget,"Save mutated live state");
    std::ofstream out(expected,std::ios::binary);
    out.write(reinterpret_cast<const char*>(after.data()),after.size()); need(bool(out),"cannot write expected state");
}
static bool cooking(const PendingDialog& d) {
    return d.Block&&d.Title=="Congratulations!"&&d.Message.find(" has received 1 Cooking Skill Point.")!=std::string::npos;
}
int main(int argc,char** argv) {
    fs::path output; std::string result,error,message; int caller=0; bool exact=false;
    try {
        need(argc==8,"usage: pending-save|pending-fresh|answered-fresh|save-current|fresh-current DATA SAVE CACHE HOUSE REPORT STATE");
        const std::string mode=argv[1]; output=argv[6];
        need(mode=="pending-save"||mode=="pending-fresh"||mode=="answered-fresh"||mode=="save-current"||mode=="fresh-current","unknown mode");
        const auto data=fs::weakly_canonical(argv[2]),saveRoot=fs::weakly_canonical(argv[3]),cache=fs::weakly_canonical(argv[4]);
        const auto inside=[](const fs::path& a,const fs::path& b) { auto r=a.lexically_relative(b); return !r.empty()&&*r.begin()!=".."; };
        need(data!=saveRoot&&!inside(saveRoot,data)&&!inside(data,saveRoot)&&!inside(cache,data)&&!inside(cache,saveRoot),"separate DATA/SAVE/CACHE roots required");
        for(const auto& artifact:{fs::weakly_canonical(output),fs::weakly_canonical(argv[7])})
            need(!inside(artifact,data)&&!inside(artifact,saveRoot)&&!inside(artifact,cache),"report/state must be outside data/save/cache roots");
        App app; App::Config cfg; cfg.Desktop=true; cfg.SkipIntro=true; cfg.VMBudgetMs=0; cfg.LanguageCode=1;
        cfg.DataRoot=data.string(); cfg.SaveRoot=saveRoot.string(); cfg.CacheDir=cache.string(); need(app.Init(cfg),"App init failed");
        app.Front()->FinishBootNow(); app.Speed=0; app.Frame(AppInput{},0);
        int x=0,y=0; need(app.ScriptHousePos(std::stoi(argv[5]),x,y),"house absent"); click(app,x,y);
        for(int i=0;i<240&&!app.Session()&&app.CurrentMode()!=App::Mode::Error;++i)app.Frame(AppInput{},0);
        auto s=app.Session(); need(s&&app.CurrentMode()==App::Mode::Live,"App load: "+app.Log);
        need(s->Updates==0,"load advanced before fresh inspection");
        if(mode=="save-current") {
            save(app,argv[7]); result="PASS_CURRENT_SAVE_REQUIRES_FRESH";
        } else if(mode=="pending-save") {
            button(app,"speed1");
            for(int i=0;i<1500&&s->Dialogs.empty();++i)app.Frame(AppInput{},1000./60.);
            need(s->Dialogs.size()==1&&cooking(s->Dialogs.front()),"expected single normal cooking notice");
            const auto& notice=s->Dialogs.front(); caller=notice.CallerID; message=notice.Message;
            auto e=s->VM()->GetObjectById(caller); need(e&&e->Thread&&e->Thread->BlockingState,"notice has no wait state");
            need(!e->Thread->BlockingState->Responded,"notice already answered");
            save(app,argv[7]); result="PASS_PENDING_SAVE_REQUIRES_FRESH";
        } else {
            exact=state(*s)==read(argv[7]); need(exact,"fresh execution differs from saved state");
            if(mode=="fresh-current") {
                result="PASS_CURRENT_FRESH_EXACT_EXECUTION";
            } else if(mode=="pending-fresh") {
                need(s->Dialogs.size()==1&&cooking(s->Dialogs.front()),"fresh pending notice not presented before first tick");
                caller=s->Dialogs.front().CallerID; message=s->Dialogs.front().Message;
                auto e=s->VM()->GetObjectById(caller); auto wait=e&&e->Thread?e->Thread->BlockingState:nullptr;
                need(wait&&!wait->Responded,"pending wait missing");
                button(app,"simdlg0");
                need(s->Updates==0&&s->Dialogs.empty()&&wait->Responded&&e->Thread->BlockingState==wait,"normal OK did not retain unconsumed response boundary");
                // Same profile, second isolated process: replace pending snapshot
                // with answered state only after exact restoration was checked.
                save(app,argv[7]); result="PASS_PENDING_FRESH_AND_ANSWERED_SAVE_REQUIRES_FRESH";
            } else {
                need(s->Dialogs.empty(),"accepted response displayed again");
                VMEntity* owner=nullptr;
                for(auto e:s->VM()->Entities)if(e&&!e->Dead&&e->Thread&&e->Thread->BlockingState) {
                    need(!owner,"unexpected additional response state"); owner=e;
                }
                need(owner&&owner->Thread->BlockingState->Responded,"fresh accepted response missing");
                caller=owner->ObjectID; const auto resets=s->Resets; const int budget=s->Budget();
                button(app,"speed1");
                for(int i=0;i<8&&owner->Thread->BlockingState;++i)app.Frame(AppInput{},1000./60.);
                need(!owner->Thread->BlockingState&&s->Resets==resets&&s->Budget()==budget,"accepted response did not continue normally");
                need(std::none_of(s->Dialogs.begin(),s->Dialogs.end(),[&](const auto& d){return d.CallerID==caller;}),"consumed response re-prompted");
                result="PASS_ANSWERED_FRESH_CONSUMED_ONCE";
            }
        }
    } catch(const std::exception& e) { result="FAIL_OR_BLOCKED"; error=e.what(); }
    if(!output.empty()) {
        std::ofstream out(output);
        out<<"{\"status\":"<<json_utf8(result)<<",\"error\":"<<json_utf8(error)<<",\"caller\":"<<caller
           <<",\"message\":"<<json_utf8(message)<<",\"fresh_exact\":"<<(exact?"true":"false")
           <<",\"scope\":\"asset-backed production App/SaveLot; modal Save and zero-dt response boundary are deliberately prepared; no natural player timing claim\"}\n";
    }
    std::cout<<result<<": "<<error<<'\n'; return error.empty()?0:1;
}
