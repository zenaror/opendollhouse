/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __3DS__
#include "app/ui/DesktopHousePanel.h"
#include "app/ui/OriginalTextEncoding.h"
#include "app/LotSession.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/model/VMStackObjectVariable.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include "files/formats/iff/chunks/FAMI.h"
#include "files/formats/iff/chunks/SIMI.h"
#include "files/formats/iff/chunks/ARRY.h"
#include "files/formats/iff/chunks/STR.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "files/FAR1/FAR1Archive.h"
#include "house_panel_metrics.hpp"
#include "house_panel_resources.hpp"
#include "linux_ui_strings.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace simapp { namespace original {
namespace {
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF::Chunks;
struct Resources {
    const FSO::Files::Formats::IFF::IffFile* selected=nullptr;
    std::optional<odh_host::HousePanelResources> values;
};
const odh_host::HousePanelResources* house_resources(){
    const auto content=FSO::Content::Content::Get();
    const auto provider=content?content->WorldObjectGlobals.get():nullptr;
    const auto global=provider?provider->Get("global",true):nullptr;
    const auto iff=global&&global->Resource?global->Resource->Iff:nullptr;
    static Resources cache;
    if(cache.selected==iff)return cache.values?&*cache.values:nullptr;
    cache={};cache.selected=iff;
    if(!iff||!provider->GlobalFar)return nullptr;
    try{
        // Bind the complete FCNS traversal to the actual selected TS1 Global,
        // never guess a loose file from an installation path. Unknown FCNS is
        // not registered by IffFile, so absence must be proved in the FAR member.
        if(iff->SourcePath!=provider->GlobalFar->Path())throw odh::FormatError("House selected Global source differs from provider");
        std::vector<uint8_t> bytes;bool found=false;
        for(const auto& entry:provider->GlobalFar->GetAllFarEntries()){
            auto name=entry.Filename;for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
            if(name!="global.iff")continue;
            if(found)throw odh::FormatError("House selected Global has duplicate members");
            found=true;bytes=provider->GlobalFar->GetEntry(entry);
        }
        if(!found)throw odh::FormatError("House selected Global member unavailable");
        std::string raw(reinterpret_cast<const char*>(bytes.data()),bytes.size());std::istringstream stream(raw,std::ios::binary);
        auto values=odh_host::read_house_panel_resources(odh::View(stream,bytes.size()));
        // Use the selected runtime STR505, which may have an admitted patch.
        // Tunings above retain complete raw FCNS authentication and defaults.
        values.size_curve.reset();values.furnishings_curve.reset();
        if(auto curves=iff->Get<STR>(505)){
            if(!curves->LanguageSets.empty()){
                const auto& english=curves->LanguageSets[0].Strings;
                if(!english.empty())values.size_curve=odh::parse_ts1_curve(english[0].Value);
                if(english.size()>1)values.furnishings_curve=odh::parse_ts1_curve(english[1].Value);
            }
        }
        cache.values=std::move(values);
    }catch(const std::exception& error){std::fprintf(stderr,"[house panel] resources unavailable: %s\n",error.what());}
    return cache.values?&*cache.values:nullptr;
}
std::optional<odh::GeometricRoomMap> room_snapshot(const VMArchitecture& arch){
    if(arch.Width<1||arch.Width>64||arch.Height!=arch.Width||arch.Rooms.size()<2)return {};
    odh::GeometricRoomMap out;out.width=arch.Width;out.height=arch.Height;out.floor_resources_resolved=true;
    uint32_t last=0;
    for(unsigned level=0;level<2;++level){
        if(arch.Rooms[level].Map.size()!=size_t(arch.Width)*arch.Height)return {};
        out.levels[level]=arch.Rooms[level].Map;
        for(const auto packed:out.levels[level])last=std::max({last,packed&0xffffU,(packed>>16)&0x7fffU});
    }
    if(last>=arch.RoomData.size()||last>=32768)return {};
    out.rooms.resize(last+1);std::vector<bool> assigned(last+1);
    for(unsigned i=0;i<=last;++i){out.rooms[i].id=uint16_t(i);out.rooms[i].outside=arch.RoomData[i].IsOutside;}
    for(unsigned level=0;level<2;++level)for(const auto packed:out.levels[level])
        for(const auto id:{packed&0xffffU,(packed>>16)&0x7fffU})if(id){
            if(assigned[id]&&out.rooms[id].level!=level)return {};
            assigned[id]=true;out.rooms[id].level=uint8_t(level);
        }
    return out;
}
// Retain original ARRY9 exactly until current pool topology differs. Changes
// regenerate masks with the same VMTS1LotSaver rule; no grass substitute.
std::optional<std::vector<uint8_t>> current_pools(LotSession& session,const VMArchitecture& arch){
    auto source=session.LoadedHouseResource();if(!source||arch.Floors.empty())return {};
    auto array=source->Get<ARRY>(9);if(!array||array->Width<arch.Width||array->Height<arch.Height)return {};
    const auto original=array->TransposeData();const auto cells=size_t(arch.Width)*arch.Height;
    if(original.size()!=size_t(array->Width)*array->Height||arch.Floors[0].size()!=cells)return {};
    std::vector<uint8_t> out(cells);bool changed=false;
    for(int y=0;y<arch.Height;++y)for(int x=0;x<arch.Width;++x){const auto n=size_t(y)*arch.Width+x;
        out[n]=original[size_t(y)*array->Width+x];
        const bool was=out[n]<16;
        // A water overlay may obscure a retained pool. Do not claim current
        // pool authority for that unsupported overlap.
        if(arch.Floors[0][n].Pattern==65534&&was)return {};
        const bool now=arch.Floors[0][n].Pattern==65535;changed|=was!=now;
    }
    if(changed)for(int y=0;y<arch.Height;++y)for(int x=0;x<arch.Width;++x){
        const auto pool=[&](int px,int py){return px>=0&&py>=0&&px<arch.Width&&py<arch.Height&&arch.Floors[0][size_t(py)*arch.Width+px].Pattern==65535;};
        out[size_t(y)*arch.Width+x]=!pool(x,y)?255:uint8_t((pool(x,y-1)?1:0)|(pool(x-1,y)?2:0)|(pool(x,y+1)?4:0)|(pool(x+1,y)?8:0));
    }
    return out;
}
}
std::optional<odh_host::LiveHouseInfo> DesktopHouseSnapshot(LotSession& session){
    const auto content=FSO::Content::Content::Get();const auto hood=content?content->Neighborhood.get():nullptr;
    const auto vm=session.VM();const auto context=session.Context();
    const auto family=vm?FSO::SimAntics::Engine::Utils::VMTS1Neighbourhood::CurrentFamily(vm):nullptr;
    const auto source=session.LoadedHouseResource();
    if(!hood||!vm||!context||!family||!source)return {};
    std::optional<int16_t> zoning;
    const auto zone=hood->ZoningDictionary.find(int16_t(session.HouseNumber));
    if(zone!=hood->ZoningDictionary.end())zoning=zone->second;
    if(!odh_host::house_panel_is_home(uint16_t(session.HouseNumber),family->HouseNumber,zoning,
        hood->GameState.VisitingHouse!=0,hood->GameState.PendingLotSwitch))return {};
    const auto simis=source->List<SIMI>();
    if(simis.size()!=1||simis[0]->ChunkID!=1||simis[0]->GlobalData.size()<=23)return {};
    const auto simi=simis[0];
    const auto arch=context->Architecture();if(!arch)return {};
    const auto map=room_snapshot(*arch);if(!map)return {};
    odh_host::HousePanelMetrics metrics{*map};metrics.family_members=uint32_t(family->FamilyGUIDs.size());
    if(const auto resources=house_resources()){
        metrics.tuning=&resources->tuning;
        if(resources->size_curve)metrics.size_curve=&*resources->size_curve;
        if(resources->furnishings_curve)metrics.furnishings_curve=&*resources->furnishings_curve;
    }
    metrics.objects_complete=true;metrics.object_words_current=true;
    for(const auto entity:vm->Entities){
        if(!entity){metrics.objects_complete=false;break;}
        if(entity->Dead)continue;
        if(metrics.objects.size()>=4096){metrics.objects_complete=false;break;}
        odh_host::HousePanelObjectMetrics row;
        row.positioned=entity->Position()!=FSO::LotView::Model::LotTilePos::OUT_OF_WORLD;
        row.hidden=entity->GetValue(VMStackObjectVariable::Hidden);row.room=row.positioned?int16_t(context->GetObjectRoom(entity)):int16_t(-5);
        row.category=entity->GetValue(VMStackObjectVariable::Category);row.impact=entity->GetValue(VMStackObjectVariable::RoomImpact);
        row.repair=entity->GetValue(VMStackObjectVariable(15));row.dirty=entity->GetValue(VMStackObjectVariable::DirtyLevel);
        const auto definition=entity->Object?entity->Object->OBJ:nullptr;
        if(definition)for(unsigned i=0;i<3;++i){const size_t word=i==0?39:i==1?40:69;
            // RawData omits the two version words; retain own definition,
            // never substitute a multitile master for Upkeep eligibility.
            if(word-2<definition->RawData.size())row.upkeep_definition_words[i]=int16_t(definition->RawData[word-2]);
        }
        metrics.objects.push_back(std::move(row));
    }
    std::optional<std::vector<uint32_t>> good;
    if(const auto pools=current_pools(session,*arch)){
        odh::SavedArchitecture terrain;terrain.width=arch->Width;terrain.height=arch->Height;terrain.pools=*pools;
        good=odh_host::house_pool_good_cells(terrain,*map);
    }
    if(good)metrics.outdoor_good_terrain_cells=[&](uint16_t room)->std::optional<uint32_t>{
        return room<good->size()?std::optional<uint32_t>{(*good)[room]}:std::nullopt;};
    const auto calculated=odh_host::prepare_house_panel_metrics(metrics);
    odh_host::LiveHouseInfo out;out.area_square_feet=calculated.area_square_feet;
    out.bedrooms=calculated.bedrooms;out.bathrooms=calculated.bathrooms;out.ratings=calculated.ratings;
    if(const auto classification=odh_host::house_lot_class(simi->GlobalData[23])){
        odh_host::GameUiStrings labels(content->SimsCompleteDir,odh_host::GameUiStringResource::live_panels);
        const auto text=labels.get(138,*classification,TextLanguage(),"");if(!text.empty())out.lot_name=text;
    }
    // Furnishings requires current complete object/architecture valuation;
    // Layout requires actual routing history. Neither is a cached SIMI total.
    return out;
}
}}
#endif
