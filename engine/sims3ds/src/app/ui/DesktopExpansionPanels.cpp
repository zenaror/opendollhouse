/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __3DS__
#include "app/ui/DesktopExpansionPanels.h"
#include "app/ui/OriginalTextEncoding.h"
#include "app/ui/UIGraphics.h"
#include "app/LotSession.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "content/TS1PersonalText.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "files/formats/iff/chunks/BMP.h"
#include "files/formats/iff/chunks/STR.h"
#include "files/formats/iff/IffFile.h"
#include "linux_ui_strings.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <charconv>
#include <map>

namespace simapp { namespace original {
using FSO::SimAntics::Model::VMPersonDataVariable;
namespace Chunks=FSO::Files::Formats::IFF::Chunks;
DesktopInventorySnapshot CaptureDesktopInventory(LotSession& session) {
    DesktopInventorySnapshot out;
    auto sim=session.SelectedSim();out.has_selected_person=sim&&!sim->Dead;
    if(!out.has_selected_person)return out;
    const auto nid=sim->GetPersonData(VMPersonDataVariable::NeighborId);
    out.neighbor_id=nid;
    out.language=TextLanguage();
    auto content=FSO::Content::Content::Get();
    if(!content||!content->Neighborhood||!content->Neighborhood->Neighborhood)return out;
    static std::string text_root;
    static std::unique_ptr<odh_host::GameUiStrings> labels;
    if(!labels||text_root!=content->SimsCompleteDir){text_root=content->SimsCompleteDir;
        labels=std::make_unique<odh_host::GameUiStrings>(text_root,odh_host::GameUiStringResource::live_panels);}
    out.title=labels->get(141,0,out.language,"");
    out.items.emplace();
    const auto source=content->Neighborhood->GetInventoryByNID(nid);
    if(!source)return out;
    struct IconEntry {std::array<std::uint8_t,32> source_sha{};std::shared_ptr<const Canvas> strip;};
    static std::map<std::uint32_t,IconEntry> icons;
    static std::string icon_root;
    static const void* icon_provider=nullptr;
    if(icon_root!=content->SimsCompleteDir||icon_provider!=content->WorldObjects.get()) {
        icons.clear();icon_root=content->SimsCompleteDir;icon_provider=content->WorldObjects.get();
    }
    out.items->reserve(source->size());
    for(std::size_t index=0;index<source->size();++index) {
        const auto& item=(*source)[index];
        DesktopInventoryItem view;
        view.source_index=index;view.type=item.Type;view.guid=item.GUID;view.count=item.Count;
        // The current resource provider applies overrides and PIFF before any
        // CTSS/BMP lookup. Never reopen an unpatched FAR object here.
        auto object=content->WorldObjects?content->WorldObjects->Get(item.GUID,true):nullptr;
        if(object&&object->OBJ&&object->Resource) {
            const auto id=object->OBJ->CatalogStringsID;
            if(auto strings=object->Resource->Get<Chunks::CTSS>(id)) {
                const auto text=FSO::Content::TS1::ResolvePersonalText(strings);
                view.text_language=text.SourceLanguage;view.text_utf8=text.UTF8;
                if(!text.Ambiguous&&(text.SourceLanguage||text.UTF8)) {
                    view.name=PersonalToUTF8(text.Value(0),text.SourceLanguage,text.UTF8);
                    view.description=PersonalToUTF8(text.Value(1),text.SourceLanguage,text.UTF8);
                }
            }
            if(auto bitmap=object->Resource->Get<Chunks::BMP>(id)) {
                const auto& bytes=bitmap->GetData();
                const auto digest=odh::sha256(bytes.data(),bytes.size());
                auto cached=icons.find(item.GUID);
                if(cached!=icons.end()&&cached->second.source_sha==digest)view.icon_strip=cached->second.strip;
                else {
                auto strip=std::make_shared<Canvas>();
                if(UIGraphics::DecodeBMP(bytes,*strip)&&strip->Width>0&&strip->Height>0) {
                    // Inventory0050e78c draws the authored first half opaque.
                    // The catalog corner RGB104,164,184 is its visible plate,
                    // not an inventory colour key. DecodeBMP handles magenta.
                    // Preserve both source states without deleting that plate.
                    if(icons.size()>=128)icons.clear();
                    view.icon_strip=strip;icons[item.GUID]={digest,std::move(strip)};
                }
                }
            }
        }
        if(!view.icon_strip) {
            // Original0050e555 loads resource598, CPanel/CatalogUnknown.bmp,
            // when either the object or its authored catalog BMP is absent.
            if(const auto* unknown=UIGraphics::Get().Image("cpanel/CatalogUnknown.bmp"))
                view.icon_strip=std::make_shared<const Canvas>(*unknown);
        }
        out.items->push_back(std::move(view));
    }
    return out;
}
DesktopFameSnapshot CaptureDesktopFame(LotSession& session) {
    DesktopFameSnapshot out;
    auto sim=session.SelectedSim();out.has_selected_person=sim&&!sim->Dead;
    if(!out.has_selected_person)return out;
    out.neighbor_id=sim->GetPersonData(VMPersonDataVariable::NeighborId);
    out.score=sim->GetPersonData(VMPersonDataVariable(80));
    out.half_stars=sim->GetPersonData(VMPersonDataVariable(81));
    out.high_watermark=sim->GetPersonData(VMPersonDataVariable(82));
    static constexpr unsigned indices[]{10,12,11,17,18,15};
    for(std::size_t i=0;i<6;++i)out.skills[i]=sim->GetPersonData(VMPersonDataVariable(indices[i]));
    out.language=TextLanguage();
    auto content=FSO::Content::Content::Get();
    if(!content)return out;
    struct Resources {
        std::string root;
        std::unique_ptr<FSO::Files::Formats::IFF::IffFile> fame;
        std::unique_ptr<odh_host::GameUiStrings> live;
    };
    static Resources resources;
    if(resources.root!=content->SimsCompleteDir) {
        resources=Resources{};resources.root=content->SimsCompleteDir;
        for(const auto* filename:{"/GameData/fame.iff","/GameData/Fame.iff","/GameData/FAME.IFF"}) {
            const auto path=resources.root+filename;
            if(FSO::Content::FileExists(path)) {
                resources.fame=std::make_unique<FSO::Files::Formats::IFF::IffFile>(path);break;
            }
        }
        resources.live=std::make_unique<odh_host::GameUiStrings>(resources.root,odh_host::GameUiStringResource::live_panels);
    }
    auto label=[&](int table,std::size_t index)->std::optional<std::string> {
        if(!resources.fame)return {};
        const auto text=FSO::Content::TS1::ResolvePersonalText(resources.fame->Get<Chunks::STR>(table));
        if(text.Ambiguous||(!text.SourceLanguage&&!text.UTF8)||index>=text.Values.size())return {};
        return PersonalToUTF8(text.Value(index),text.SourceLanguage,text.UTF8);
    };
    auto integer=[](const std::string& value)->std::optional<std::int32_t> {
        std::int32_t result=0;
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
        if(parsed.ec!=std::errc()||parsed.ptr!=value.data()+value.size())return {};
        return result;
    };
    out.title=label(1,0);out.friends_name=label(1,37);out.friends_format=label(1,12);
    if(*out.half_stars>=0&&*out.half_stars<=10) {
        const auto level=std::size_t(*out.half_stars);
        out.level_name=label(1,1+level);out.level_title=label(1,13+level*2);out.level_description=label(1,14+level*2);
        if(const auto value=label(4,level))out.required_friend_stars=integer(*value);
        if(const auto row=label(6,level)) {
            std::size_t first=0;
            for(std::size_t skill=0;skill<6;++skill) {
                const auto end=row->find(',',first);
                if(end==std::string::npos&&skill!=5)break;
                const auto value=integer(row->substr(first,end==std::string::npos?end:end-first));
                if(value&&*value>=0&&*value<=10)out.required_skills[skill]=*value*100;
                if(end==std::string::npos)break;
                first=end+1;
            }
        }
    }
    for(std::size_t i=0;i<6;++i)out.skill_names[i]=resources.live->get(136,int(i+1),out.language,"");
    return out;
}
bool HandleDesktopInventoryInput(LotSession& session,DesktopInventorySelection& state,
                                const std::string& id,std::size_t page_size) {
    if(id.rfind("pc_inv_",0)!=0)return false;
    const auto snapshot=CaptureDesktopInventory(session);
    state.Reconcile(snapshot,page_size);
    if(id=="pc_inv_sort0")state.Sort(DesktopInventorySort::gifts);
    else if(id=="pc_inv_sort1")state.Sort(DesktopInventorySort::magic);
    else if(id=="pc_inv_sort2")state.Sort(DesktopInventorySort::other);
    else if(id=="pc_inv_prev")state.Page(-1,snapshot,page_size);
    else if(id=="pc_inv_next")state.Page(1,snapshot,page_size);
    else if(id=="pc_inv_close")state.ClosePopup();
    else if(id.rfind("pc_inv_item",0)==0) {
        std::size_t source=0;const auto* first=id.data()+11;const auto* last=id.data()+id.size();
        const auto parsed=std::from_chars(first,last,source);
        if(parsed.ec==std::errc()&&parsed.ptr==last&&first!=last)state.Select(source,snapshot);
    }
    // Even a stale/unknown owned hit is consumed rather than reaching the
    // world picker after an inventory update changed the selected identity.
    return true;
}
}}
#endif
