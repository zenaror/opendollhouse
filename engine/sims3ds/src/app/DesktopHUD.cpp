/* This Source Code Form is subject to the terms of the Mozilla Public License,
 * v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain
 * one at http://mozilla.org/MPL/2.0/. */
// PC control panel. Positions are authored TS1 pixels, recovered by the earlier
// OpenDollhouse OriginalControlPanelLayout; the VM and actions remain Sims3DS.
#include "app/LotScreen.h"
#include "app/ui/UIGraphics.h"
#include "app/ui/UIUCP.h"
#ifndef __3DS__
#include "app/ui/OriginalPCUI.h"
#include "app/ui/OriginalTextEncoding.h"
#include "app/ui/DesktopLivePanels.h"
#include "app/ui/DesktopExpansionPanels.h"
#include "world/World.h"
#include "app/ui/GameText.h"
#include "app/live/UILiveMode.h"
#include "app/ui/UIOptionsPanel.h"
#include "app/buy/UIBuyMode.h"
#include "app/build/UIBuildMode.h"
#include "app/front/TS1Font.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/model/VMMotive.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMQueuedAction.h"
#include <algorithm>

namespace simapp {
void LotScreen::DrawDesktop() {
    Canvas& c = *Top;
    const int dy = c.Height - 600;
    Hits.clear();
    Live->_DesktopInventory.popup.visible=false;
    Live->_DesktopFame.popup.visible=false;
    Live->_DesktopCareer.popup.visible=false;
    ViewRect = Rect(0,0,c.Width,c.Height-100); ViewScale = 1;
    auto& original=OriginalPCUI::Get();
    const auto layout=odh_host::original_control_panel_layout(c.Width,c.Height);
    auto rect=[](const odh::UiBitmapRect& r){return Rect(r.x,r.y,r.width,r.height);};
    auto hit=[&](const odh::UiBitmapRect& r,const char* id){Hit(rect(r),id);};
    using PM=odh_host::OriginalPanelMode;using LT=odh_host::LivePanelTab;
    odh_host::OriginalPanelState state;
    state.live_enabled=S->CanEnterLive();
    state.pointer=DesktopPointerPosition();state.press_origin=DesktopPressOrigin();
    state.paused=Speed==0;state.simulation_speed=Speed==1?1:Speed==2?3:Speed==3?10:1;
    state.walls_down=S->WallsMode==1;state.cutaway=S->WallsMode==0;
    state.options_enabled=true;state.options_open=CurrentMode==Mode::Options;
    state.can_level_up=S->World()->State.Level()<2;state.can_level_down=S->World()->State.Level()>1;
    const auto interest_snapshot=original::DesktopLiveSnapshot(*S,LT::interests);
    state.live_tabs={true,true,true,true,odh_host::live_panel_capabilities(interest_snapshot)[4],true};
    if(Live->CurrentTab==UILiveMode::Tab::Interests&&!state.live_tabs[4]){
        Live->CurrentTab=UILiveMode::Tab::Mood;Live->_InterestScroll=0;
    }
    state.inventory_enabled=true;state.inventory_selected=Live->CurrentTab==UILiveMode::Tab::Inventory;
    switch(Live->CurrentTab){case UILiveMode::Tab::Job:state.live_tab=LT::career;break;case UILiveMode::Tab::Personality:state.live_tab=LT::personality;break;case UILiveMode::Tab::Relationships:state.live_tab=LT::relationships;break;case UILiveMode::Tab::House:state.live_tab=LT::house;break;case UILiveMode::Tab::Interests:state.live_tab=LT::interests;break;default:state.live_tab=LT::needs;}
    if(auto sim=S->SelectedSim())state.mood=sim->GetMotiveData(FSO::SimAntics::Model::VMMotive::Mood);
    PM mode=CurrentMode==Mode::Live?PM::live:CurrentMode==Mode::Options?PM::options:CurrentMode==Mode::Buy?PM::buy:PM::build;
    const auto selected=S->SelectedSim();
    if(CurrentMode!=Mode::Live||!selected||selected->Dead||Live->CurrentTab!=UILiveMode::Tab::Inventory)
        Live->_DesktopInventory.ClosePopup();
    if(CurrentMode!=Mode::Live||!selected||selected->Dead||Live->CurrentTab!=UILiveMode::Tab::Job||
       selected->IsPet()||selected->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable::JobType)!=0||
       selected->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable(80))<=0)
        Live->_DesktopFame.ClosePopup();
    const bool ordinary=CurrentMode==Mode::Live&&selected&&!selected->Dead&&Live->CurrentTab==UILiveMode::Tab::Job&&
        !( !selected->IsPet()&&selected->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable::JobType)==0&&
           selected->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable(80))>0);
    if(!ordinary)Live->_DesktopCareer.Reset();
    original.DrawPanel(c,mode,state);
    hit(layout.modes[0],"mode_live");hit(layout.modes[1],"mode_buy");hit(layout.modes[2],"mode_build");
    hit(layout.rotate_left,"rotate_left");hit(layout.rotate_right,"rotate_right");
    hit(layout.zoom_in,"zoom_in");hit(layout.zoom_out,"zoom_out");
    hit(layout.level_up,"level_up");hit(layout.level_down,"level_down");
    hit(layout.walls_up,"walls_up");hit(layout.walls_down,"walls_down");hit(layout.walls_cutaway,"walls_cut");
    hit(layout.options,"mode_options");hit(layout.pause,"speed0");
    hit(layout.speeds[0],"speed1");hit(layout.speeds[1],"speed2");hit(layout.speeds[2],"speed3");
    original::OfferDesktopControlHelp(c,*this);
    front::TextCentered(c,front::TS1Size::S9,Rect(83,535+dy,82,14),UIUCP::ClockText(*this),RGBA(0,255,100));
    if(S->HasFamilyBudget())front::TextCentered(c,front::TS1Size::S11,Rect(96,577+dy,92,18),UIUCP::Money(S->Budget()),RGBA(0,255,100));
    if (CurrentMode == Mode::Live) {
        Live->UpdateMotiveDisplay();
        // The same preserved queue layout supplies drawing and current UID hits.
        Live->_QueueUIDs.clear();
        for(const auto& tile:original.QueueTiles(S->SelectedSim(),c.Width,c.Height)) {
            Hit(rect(tile.box),"queue"+std::to_string(Live->_QueueUIDs.size()));
            Live->_QueueUIDs.push_back(uint16_t(tile.identity.uid));
            // Actual Complete queue hover names the queued interaction. Reuse
            // the same visible tile/authoritative action; hover never dispatches.
            auto actor=S->SelectedSim();
            if(actor && actor->Thread && tile.action_index<actor->Thread->Queue.size()){
                const auto& action=actor->Thread->Queue[tile.action_index];
                if(action && action->UID==tile.identity.uid)
                    OfferDesktopHover(original::UIString(action->Name),
                        "queue:"+std::to_string(reinterpret_cast<std::uintptr_t>(action.get())),
                        "queue:"+std::to_string(actor->ObjectID),uint64_t(tile.identity.uid),rect(tile.box));
            }
        }
        const auto portraits=odh_host::original_portrait_rects(layout,S->Family.size());
        for(size_t i=0;i<S->Family.size()&&i<portraits.size();++i){
            original.DrawPortrait(c,portraits[i],int(i)==S->Selected,S->Family[i]);
            Hit(rect(portraits[i]),"sim"+std::to_string(i));
        }
        hit(layout.tabs[0],"tab_mood");hit(layout.tabs[1],"tab_rel");hit(layout.tabs[2],"tab_job");
        if(state.live_tabs[4])hit(layout.tabs[4],"tab_interest");
        hit(layout.tabs[3],"tab_pers");hit(layout.tabs[5],"tab_house");
        Hit(Rect(layout.tabs[3].x,layout.tabs[5].y,layout.tabs[5].width,layout.tabs[5].height),"tab_inv");
        original::OfferDesktopLiveTabHelp(c,*this);
        auto sim=S->SelectedSim();
        Rect detail(528,520+dy,265,78);
        if (sim) {
            using Tab=UILiveMode::Tab;
            if (Live->CurrentTab==Tab::Mood) {
                std::array<int,8> changes{};
                for(unsigned i=0;i<changes.size();++i)changes[i]=Live->Motives.ChangeArrowState(i);
                original::DrawDesktopNeedsPanel(c,*S,changes);
                original::OfferDesktopNeedsHelp(c,*this);
            } else if(Live->CurrentTab==Tab::Inventory) {
                if(!original::DrawDesktopInventory(c,*this,original::CaptureDesktopInventory(*S),Live->_DesktopInventory))
                    Live->DrawInventory(c,detail,sim);
            }
            // Original004cffcb auto-selects Fame only with no normal job
            // and positive fame score. The static fame_ui debug gate was not
            // accepted by the observed original runtime; no chooser is added.
            else if(Live->CurrentTab==Tab::Job&&!sim->IsPet()&&
                sim->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable::JobType)==0&&
                sim->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable(80))>0) {
                if(!original::DrawDesktopFame(c,*this,original::CaptureDesktopFame(*S),Live->_DesktopFame))
                    Live->DrawJob(c,detail,sim);
            }
            else original::DrawDesktopLivePanel(c,*this,state.live_tab,
                state.live_tab==LT::interests?interest_snapshot:original::DesktopLiveSnapshot(*S,state.live_tab),
                Live->_RelScroll,Live->_InterestScroll,Live->_DesktopCareer);
        }
    } else if(CurrentMode==Mode::Options) Options->Draw(c,Rect(220,500+dy,580,100));
    else if(CurrentMode==Mode::Buy) Buy->Draw(c,Rect(220,500+dy,c.Width-220,100));
    else Build->Draw(c,Rect(220,500+dy,c.Width-220,100));
    if(S->CanEnterLive() && Speed==0 && (CurrentMode==Mode::Live || CurrentMode==Mode::Options))
        original.DrawPauseLabel(c,Str?Str->Get(148,0,"Paused"):"Paused");
    if (Pie.IsOpen) Pie.Draw(c,S->SelectedSim()?FaceFor(S->SelectedSim(),0):nullptr);
}
}

#else
namespace simapp { void LotScreen::DrawDesktop() {} }
#endif
