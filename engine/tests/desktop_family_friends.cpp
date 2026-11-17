/* SPDX-License-Identifier: MPL-2.0 */
#include "app/ui/DesktopFamilyFriends.h"
#ifndef ODH_FAMILY_FRIENDS_HELPER_ONLY
#include "app/ui/DesktopLivePanels.h"
#include "app/front/TS1Font.h"
#include "app/front/ResGraphics.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "files/formats/iff/chunks/FCNS.h"
#include <filesystem>
#endif
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <functional>
#ifndef ODH_FAMILY_FRIENDS_HELPER_ONLY
#define private public
#include "app/LotSession.h"
#undef private
#endif
using namespace simapp;
using namespace simapp::original;
using namespace FSO::Files::Formats::IFF::Chunks;
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
#ifdef ODH_FAMILY_FRIENDS_HELPER_ONLY
    if(argc>1) {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};
        need(bool(input)||input.eof(),"FCNS fixture payload read");
        const auto constants=odh::read_named_float_constants(odh::View(bytes.data(),bytes.size()));
        need(DesktopFamilyFriendThreshold(&constants)==50,"actual DVD FCNS2 threshold50");
        std::cout<<"actual FCNS2 threshold50 passed\n";
    }
#endif
    NBRS neighbors;
    auto add=[&](int16_t id,uint32_t guid,int family,int gender=0)->Neighbour&{
        auto n=std::make_unique<Neighbour>();n->NeighbourID=id;n->GUID=guid;
        n->PersonData.resize(80);n->PersonData[61]=family;n->PersonData[65]=gender;
        auto* result=n.get();neighbors.Entries.push_back(std::move(n));
        neighbors.NeighbourByID[id]=result;neighbors.DefaultNeighbourByGUID[guid]=id;return *result;
    };
    auto& member=add(1,101,1);auto& otherMember=add(2,102,1);
    auto& friend1=add(3,103,2);auto& friend2=add(4,104,3);
    FAMI family;family.HouseNumber=9;family.FamilyGUIDs={101,102};family.FamilyFriends=777;
    family.RuntimeSubset={101};
    auto pair=[](Neighbour& a,Neighbour& b,int ab,int ba){a.Relationships[b.NeighbourID]={int16_t(ab),0,-100};b.Relationships[a.NeighbourID]={int16_t(ba),0,-100};};
    int threshold=DesktopFamilyFriendThreshold(nullptr);
    need(threshold==25,"constructor fallback25 only when property absent");
    auto count=[&]{return DesktopFamilyFriendCount(family,neighbors,threshold);};
    need(count()==0,"missing relationships do not count");
    for(const auto ab:{24,25})for(const auto ba:{24,25}){
        pair(member,friend1,ab,ba);need(count()==(ab==25&&ba==25),"mutual slot0 boundary24/25");
        std::cout<<"BOUNDARY member_to_external="<<ab<<" external_to_member="<<ba<<" count="<<count()<<'\n';
    }
    odh::NamedFloatConstants tuning;tuning.records.push_back({"friendship threshold",50.f,{}});
    threshold=DesktopFamilyFriendThreshold(&tuning);
    need(threshold==50,"loaded named FCNS property overrides fallback");
    for(const auto ab:{49,50})for(const auto ba:{49,50}){
        pair(member,friend1,ab,ba);need(count()==(ab==50&&ba==50),"loaded mutual slot0 boundary49/50");
        std::cout<<"LOADED_BOUNDARY member_to_external="<<ab<<" external_to_member="<<ba<<" count="<<count()<<'\n';
    }
    tuning.records[0].value=25.f;threshold=DesktopFamilyFriendThreshold(&tuning);
    need(threshold==25,"alternate loaded threshold is respected");
    tuning.records[0].value=49.9f;need(DesktopFamilyFriendThreshold(&tuning)==49,"original truncation toward zero");
    pair(member,friend1,25,25);
    pair(otherMember,friend1,100,100);need(count()==1,"shared friend counts once across full family, not runtime subset");
    pair(member,otherMember,100,100);need(count()==1,"intrafamily excluded");
    pair(member,friend2,50,50);need(count()==2,"second external friend and threshold50 also qualify");
    friend2.PersonData[61]=0;need(count()==1,"familyzero excluded");friend2.PersonData[61]=3;
    friend2.PersonData[65]=2;need(count()==1,"external pet excluded");friend2.PersonData[65]=1;
    friend2.PersonData[58]=9;need(count()==2,"no age or person-type filter added");
    pair(member,friend2,-100,100);need(count()==1,"one-way high relation insufficient");
    friend2.Relationships[1].clear();need(count()==1,"empty relationship insufficient");
    pair(otherMember,friend2,25,25);otherMember.PersonData[65]=2;
    need(count()==2,"original predicate does not filter family member species");
    need(family.FamilyFriends==777,"presentation must not mutate persisted FAMI cache");
    std::cout<<"family friend predicate passed\n";
#ifndef ODH_FAMILY_FRIENDS_HELPER_ONLY
    auto* content=FSO::Content::Content::Get();
    auto saved=std::move(content->Neighborhood);
    auto savedGlobals=std::move(content->WorldObjectGlobals);
    struct RestoreGlobals{FSO::Content::Content* c;std::unique_ptr<FSO::Content::WorldGlobalProvider> old;~RestoreGlobals(){c->WorldObjectGlobals=std::move(old);}}restoreGlobals{content,std::move(savedGlobals)};
    content->Neighborhood=std::make_unique<FSO::Content::TS1::TS1NeighborhoodProvider>(content,false);
    struct Restore{FSO::Content::Content* c;std::unique_ptr<FSO::Content::TS1::TS1NeighborhoodProvider> old;~Restore(){c->Neighborhood=std::move(old);}}restore{content,std::move(saved)};
    auto& provider=*content->Neighborhood;
    LotSession session;session._AllowsLive=true;session.HouseNumber=9;
    need(!DesktopFamilyFriendsSnapshot(session),"missing provider data is unavailable");
    provider.Neighbors=&neighbors;provider.FamilyForHouse[9]=&family;
    need(DesktopFamilyFriendsSnapshot(session)==2,"actual app snapshot resolves resident family and NBRS");
    friend2.Relationships[2][0]=24;
    need(DesktopFamilyFriendsSnapshot(session)==1,"live central relationship changes immediately reach HUD producer");
    FAMI travel;travel.FamilyGUIDs={103};provider.GameState.ActiveFamily=&travel;
    need(DesktopFamilyFriendsSnapshot(session)==1,"active travelling family precedes house resident family");
    session._AllowsLive=false;need(!DesktopFamilyFriendsSnapshot(session),"vacant lot does not invent a family");
    if(argc>1){
        need(front::ResGraphics::Get().Open(argv[1]),"read-only UIGraphics open");need(front::LoadTS1Fonts(),"authored fonts load");
        const auto oldRoot=content->SimsCompleteDir;
        struct RestoreRoot{FSO::Content::Content* c;std::string old;~RestoreRoot(){c->SimsCompleteDir=old;}}restoreRoot{content,oldRoot};
        content->SimsCompleteDir=std::filesystem::weakly_canonical(argv[1]).parent_path().string();
        content->WorldObjectGlobals=std::make_unique<FSO::Content::WorldGlobalProvider>(content);
        content->WorldObjectGlobals->Init();
        auto* global=content->WorldObjectGlobals->Get("global",true);
        need(global&&global->Resource,"actual readonly global resource");
        auto* constants=global->Resource->Get<FCNS>(2);
        need(constants&&DesktopFamilyFriendThreshold(&constants->Values)==50,"DVD FCNS2 loads real threshold50");
        provider.GameState.ActiveFamily=&family;session._AllowsLive=true;
        pair(member,friend1,25,25);pair(otherMember,friend1,25,25);
        need(DesktopFamilyFriendsSnapshot(session)==0,"actual resource threshold rejects25 pairs");
        pair(member,friend1,50,50);
        need(DesktopFamilyFriendsSnapshot(session)==1,"actual resource threshold accepts mutual50");
    }
    front::TS1Font counter;
    if(argc>1) {
        need(counter.LoadPanelCounter(front::ResGraphics::Get().Raw("Fonts/variablesans_10.ffn")),"independent original counter face");
        need(counter.Width("$9,999,999")==73&&counter.Width("999")==24&&counter.LineHeight()==16,"original counter profile metrics");
        need(front::TextW(front::TS1Size::S11,"$9,999,999")==103,"existing funds font metrics unchanged");
        const auto bounds=front::FamilyFriendsCounterBounds(600);
        need(bounds.X==81&&bounds.Y==578&&bounds.W==24&&bounds.H==16,"original measured counter bounds");
    }
    const auto green=RGBA(0,255,100),counterColor=RGBA(41,231,90);
    for(const auto text:{"0","1","25","999","1000","1"})for(const int height:{600,720}){
        Canvas got(240,height),expected(240,height);got.Clear(RGBA(10,20,30));expected.Clear(RGBA(10,20,30));
        // Funds and friends have independent retained cells and font metrics.
        front::DrawPanelLabel(got,front::TS1Size::S11,Rect(120,height-23,80,18),"$9,590",green);
        front::DrawPanelLabel(expected,front::TS1Size::S11,Rect(120,height-23,80,18),"$9,590",green);
        front::DrawFamilyFriendsCounter(got,text);
        const auto field=front::FamilyFriendsCounterBounds(height);
        if(counter.Loaded())counter.Draw(expected,field.Right()-counter.Width(text),field.Y,text,counterColor);
        else front::Text(expected,front::TS1Size::S10,field.Right()-front::TextW(front::TS1Size::S10,text),field.Y,text,counterColor);
        need(got.Px==expected.Px,"counter replay equals direct original-profile draw after value and height changes");
    }
    std::cout<<"desktop family friends passed; authored_fonts="<<front::TS1FontsLoaded()<<" counter_width="<<front::FamilyFriendsCounterBounds(600).W<<" counter_line="<<front::FamilyFriendsCounterBounds(600).H<<'\n';
#endif
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
