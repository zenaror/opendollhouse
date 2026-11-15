/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __3DS__
#include "app/ui/OriginalPhonebook.h"
#include "app/ui/OriginalTextEncoding.h"
#include "app/ui/OriginalPCUI.h"
#include "app/LotSession.h"
#include "app/front/ResGraphics.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "content/TS1PersonalText.h"
#include "simantics/VM.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "files/formats/iff/chunks/BMP.h"
#include "files/formats/iff/chunks/STR.h"
#include "odh/avatar_body.hpp"
#include "phonebook_candidates.hpp"
#include <map>
namespace simapp { namespace original {
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF::Chunks;
void PreparePhonebook(LotSession& session,PendingDialog& dialog){
    odh_host::PhonebookSnapshot snapshot;
    auto content=FSO::Content::Content::Get();
    auto provider=content?content->Neighborhood.get():nullptr;
    auto caller=dynamic_cast<VMAvatar*>(session.VM()->GetObjectById(dialog.CallerID));
    auto nid=caller?caller->GetPersonData(VMPersonDataVariable::NeighborId):0;
    auto owner=provider?provider->GetNeighborByID(nid):nullptr;
    snapshot.caller_current=caller&&!caller->Dead&&owner&&owner->Unknown1==1&&owner->GUID==caller->Object->OBJ->GUID;
    snapshot.relationships_current=snapshot.caller_current;
    if(caller){dialog.ChoiceCallerGUID=caller->Object->OBJ->GUID;dialog.ChoiceCallerNID=nid;dialog.ChoiceCallerThread=caller->Thread.get();}
    if(owner)for(auto key:owner->OrderedRelationshipKeys()){
        snapshot.relationship_keys.push_back(key);
        const auto id=static_cast<int16_t>(static_cast<uint32_t>(key)&65535U);
        auto neighbor=provider->GetNeighborByID(id);if(!neighbor||neighbor->Unknown1!=1)continue;
        const bool data=neighbor->PersonData.size()>65;
        snapshot.neighbors.push_back({id,neighbor->GUID,data,data?neighbor->PersonData[61]:int16_t(0),data?neighbor->PersonData[65]:int16_t(0)});
    }
    const auto names=[&](int16_t,uint32_t guid)->std::optional<std::string>{
        auto object=content->WorldObjects->Get(guid,true);
        if(!object||!object->Resource||!object->OBJ)return {};
        const auto personal=FSO::Content::TS1::ResolvePersonalText(object->Resource->Get<CTSS>(object->OBJ->CatalogStringsID));
        if(personal.Ambiguous||(!personal.SourceLanguage&&!personal.UTF8)||personal.Value(0).empty())return {};
        return PersonalLabelUTF8(personal.Value(0),personal.SourceLanguage,personal.UTF8,personal.Ambiguous);
    };
    const auto result=odh_host::select_phonebook_choices(snapshot,names);
    dialog.ChoicesReady=result.ready;dialog.ChoicesDiagnostic=result.diagnostic;
    if(!result.ready)printf("[phonebook] contact authority unavailable: %s\n",result.diagnostic.c_str());
    for(const auto& choice:result.choices){
        PendingDialogChoice entry;entry.Label=choice.label;entry.Value=choice.value;
        auto neighbor=provider->GetNeighborByID(int16_t(choice.value));entry.GUID=neighbor->GUID;
        auto object=content->WorldObjects->Get(entry.GUID,true);
        if(auto bmp=object->Resource->Get<BMP>(2002)){
            Canvas faces;if(front::DecodeImage(bmp->GetData(),faces,true)&&faces.Width>=3)front::Crop(faces,entry.Portrait,0,0,faces.Width/3,faces.Height);
        }else if(auto bmp=object->Resource->Get<BMP>(2004))front::DecodeImage(bmp->GetData(),entry.Portrait,true);
        if(entry.Portrait.Width==0){
            if(auto strings=object->Resource->Get<STR>(object->OBJ->BodyStringID)){
                odh::AvatarBodyStrings body;body.avatar_type=strings->GetString(0);body.body=strings->GetString(1);body.body=body.body.substr(0,body.body.find(';'));body.head=strings->GetString(2);
                for(size_t i=0;i<6;++i)body.hands[i]=strings->GetString(int(i+17));
                Canvas faces;if(OriginalPCUI::Get().GenerateFaces(body,faces))front::Crop(faces,entry.Portrait,0,0,faces.Width/3,faces.Height);
            }
        }
        dialog.Choices.push_back(std::move(entry));
    }
}
}}
#endif
