#ifndef __3DS__
/* SPDX-License-Identifier: MPL-2.0 */
#include "app/ui/OriginalPCUI.h"
#include "app/ui/OriginalPresentation.h"
#include "app/front/TS1Font.h"
#include "app/front/ResGraphics.h"
#include "linux_text_renderer.hpp"
#include "content/ContentManager.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/BMP.h"
#include "files/formats/iff/chunks/STR.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "vitaboy/SimAvatar.h"
#include "character_ui_images.hpp"
#include "catalog_info_text.hpp"
#include "catalog_object_thumbnail.hpp"
#include "original_button_state.hpp"
#include "files/formats/iff/chunks/DGRP.h"
#include "files/formats/iff/chunks/SPR2.h"
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <climits>
#include <set>
#include "app/ui/OriginalTextEncoding.h"
#include "linux_cas_preview.hpp"
#include "resource.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
namespace simapp {
namespace {
using Avatar=FSO::SimAntics::VMAvatar;
using Entity=FSO::SimAntics::VMEntity;
using BMP=FSO::Files::Formats::IFF::Chunks::BMP;
using STR=FSO::Files::Formats::IFF::Chunks::STR;
Canvas canvas(const odh::BitmapImage& b){Canvas c(int(b.width),int(b.height));for(size_t i=0;i<c.Px.size();++i){const auto& p=b.pixels[i];c.Px[i]=RGBA(p[0],p[1],p[2],b.bit_count==32?p[3]:255);if(p[0]==255&&p[1]==0&&p[2]==255)c.Px[i]=0;}return c;}
odh::AvatarBodyStrings body_input(STR* s){
    odh::AvatarBodyStrings body;
    if(!s)return body;
    body.avatar_type=s->GetString(0);body.body=s->GetString(1);body.head=s->GetString(2);
    // BodySTR can author alternate daywear entries; source uses its first
    // segment for the immutable descriptor, not an arbitrary new outfit.
    body.body=body.body.substr(0,body.body.find(';'));
    for(size_t i=0;i<6;++i)body.hands[i]=s->GetString(int(i+17));
    return body;
}
odh::AvatarBodyStrings body_input(Avatar* a){
    return body_input(a->Object->Resource->Get<STR>(a->Object->OBJ->BodyStringID));
}
}
struct OriginalPCUI::Impl {
    std::string root,last_source;
    odh_host::OriginalControlPanelArt panel;
    odh_host::OriginalQueueView queue;
    odh_host::CatalogInfoView catalog;
    odh_host::CatalogInfoScrollState catalog_scroll;
    odh_host::CatalogInfoLayout catalog_layout{};
    std::string catalog_selection;
    bool catalog_open=false;
    std::uint32_t thumbnail_guid=0;
    odh_host::CatalogObjectThumbnailResult thumbnail;
    std::unique_ptr<odh_host::LinuxCasPreview> portraits;
    odh::RasterFrame surface{};
    front::TS1Font pause_font;
    bool pause_font_prepared=false;
    std::string pause_key;
    Canvas pause_label;
    odh_host::OriginalPanelState last_state{};odh_host::OriginalControlPanelLayout last_layout{};
    struct Faces {std::string source,input;odh_host::CharacterUiImages images;std::vector<Canvas> canvases;std::optional<odh::BitmapImage> generated;};
    std::map<uint32_t,Faces> characters;
    std::map<uint32_t,std::optional<odh::BitmapImage>> icons;
    struct AvatarIcon {std::vector<uint8_t> bytes;std::optional<odh::BitmapImage> image;};
    std::map<uint32_t,AvatarIcon> avatar_icons;
    std::map<uint32_t,AvatarIcon> queue_faces;
    explicit Impl(const std::string& r):root(r),panel(r),queue(r),catalog(r){}
    odh_host::LinuxCasPreview& preview(){
        if (!portraits) {
            odh::LiveInstallDirectory cached_directory = nullptr;
#if defined(__vita__) || defined(__SWITCH__)
            cached_directory = FSO::Files::VisitLiveInstallDirectory;
#endif
            portraits = std::make_unique<odh_host::LinuxCasPreview>(root, FSO::Content::Content::Get()->CacheDir, cached_directory);
        }
        return *portraits;
    }
    void copy_in(const Canvas& c,int y,int h){
        original::Raster(c,surface,Rect{0,y,c.Width,h});
    }
    void copy_out(Canvas& c,int y,int h){original::Copy(surface,c,Rect{0,y,c.Width,h});}
    const odh::BitmapImage* generated(const odh::AvatarBodyStrings& body){
        auto image=preview().hud_portrait(body);
        last_source=portraits->portrait_source();
        if(!image)throw odh::FormatError("original HUD portrait: "+portraits->diagnostic());
        return image;
    }
    Faces& faces(Avatar* a){
        const uint32_t guid=a->Object->GUID;
        const auto body=body_input(a);
        std::string input="hud-cell-v2:"+a->Object->Resource->Name;
        input+='\0';input+=body.avatar_type;
        for(const auto* part:{&body.body,&body.head,&body.hands[0],&body.hands[1],&body.hands[2],&body.hands[3],&body.hands[4],&body.hands[5]}){input+='\0';input+=*part;}
        auto found=characters.find(guid);
        if(found!=characters.end()){
            if(found->second.input==input){last_source=found->second.source;return found->second;}
            characters.erase(found);
        }
        if(characters.size()>=64)characters.erase(characters.begin());
        Faces f;f.input=std::move(input);
        // The household bar needs only this resident's face strip. Other panels
        // request their own images when opened.
        auto* iff=a->Object->Resource->MainIff();
        if(auto* strip=iff ? iff->Get<BMP>(2002) : nullptr){
            for(auto* chunk:iff->SilentListAll())
                if(chunk!=strip && chunk->ChunkID==2002 && chunk->ChunkType==BMP::TYPE)
                    throw odh::FormatError("duplicate character UI image resource");
            const auto& bytes=strip->GetData();
            f.images=odh_host::read_character_ui_image(2002,odh::View(bytes.data(),bytes.size()));
        }
        if(!f.images.faces.empty()){
            for(const auto& face:f.images.faces)f.canvases.push_back(canvas(face));
            last_source="authored BMP2002 guid="+std::to_string(guid)+" resource="+a->Object->Resource->Name;
        }else{
            f.generated=*generated(body);f.canvases.push_back(canvas(*f.generated));
        }
        f.source=last_source;return characters.emplace(guid,std::move(f)).first->second;
    }
    const odh::BitmapImage* queue_face(Avatar* avatar){
        auto raw=avatar->Object->Resource->Get<BMP>(2002);
        if(!raw)return nullptr;
        const auto& bytes=raw->GetData();
        if(bytes.size()>1024*1024)return nullptr;
        auto found=queue_faces.find(avatar->Object->GUID);
        if(found!=queue_faces.end()&&found->second.bytes==bytes)
            return found->second.image?&*found->second.image:nullptr;
        if(queue_faces.size()>=64)queue_faces.erase(queue_faces.begin());
        auto& entry=queue_faces[avatar->Object->GUID];entry.bytes=bytes;entry.image.reset();
        try{
            std::string data(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            std::istringstream stream(data,std::ios::binary);
            entry.image=odh_host::read_queue_portrait(odh::View(stream,data.size()));
        }catch(const odh::FormatError&){return nullptr;}
        return &*entry.image;
    }
    const odh::BitmapImage* avatar_icon(Avatar* avatar){
        // Authored queue thumbnails are independent of BodySTR, other BMPs and
        // the availability of an outfit renderer. Never consult those first.
        if(auto raw=avatar->Object->Resource->Get<BMP>(2004)){
            const auto& bytes=raw->GetData();
            if(bytes.size()>1024*1024)return nullptr;
            auto found=avatar_icons.find(avatar->Object->GUID);
            if(found!=avatar_icons.end() && found->second.bytes==bytes)
                return found->second.image?&*found->second.image:nullptr;
            if(avatar_icons.size()>=64)avatar_icons.erase(avatar_icons.begin());
            auto& entry=avatar_icons[avatar->Object->GUID];entry.bytes=bytes;entry.image.reset();
            try{
                std::string data(reinterpret_cast<const char*>(bytes.data()),bytes.size());
                std::istringstream stream(data,std::ios::binary);
                odh::BitmapLimits limits;limits.width=limits.height=256;limits.pixels=65536;
                entry.image=odh::read_bmp(odh::View(stream,data.size()),limits,odh::BmpExtentPolicy::iff_container);
            }catch(const odh::FormatError&){return nullptr;}
            return &*entry.image;
        }
        if(!avatar->Avatar || !avatar->Avatar->BaseSkeleton)return nullptr;
        odh_host::LiveAvatarPortraitInput input;
        input.skeleton=avatar->Avatar->BaseSkeleton->Name;
        input.head_outfit=avatar->HeadOutfit();input.body_outfit=avatar->BodyOutfit();
        const auto appearance=avatar->Avatar->Appearance.Value();
        input.skin_tone=int(appearance);
        const auto part=[&](FSO::Vitaboy::Outfit* outfit,const char* label){
            if(!outfit)return std::string{};
            const auto selected=outfit->GetAppearance(appearance);
            if(!selected.HasFileName || selected.FileName.empty())
                throw odh::FormatError("TS1 queue portrait requires a named current outfit");
            std::string name=selected.FileName;
            std::string suffix=name.size()>=4?name.substr(name.size()-4):std::string{};
            std::transform(suffix.begin(),suffix.end(),suffix.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(suffix==".apr")name.resize(name.size()-4);
            if(!outfit->TS1TextureID.empty())name+=std::string(",")+label+"="+outfit->TS1TextureID;
            return name;
        };
        try{
            input.head=part(avatar->Avatar->Head.Value(),"HEAD-HEAD");
            input.body=part(avatar->Avatar->Body.Value(),"BODY");
            // Use the current handgroup's real TS1 records, not BodySTR defaults.
            // The frontal renderer selects its static idle gesture.
            auto handgroup=avatar->Avatar->Handgroup.Value();
            if(handgroup && handgroup->LiteralHandgroup){
                const auto& hands=handgroup->LiteralHandgroup->LightSkin;
                const FSO::Vitaboy::Gesture* gestures[]={&hands.LeftHand.Idle,&hands.RightHand.Idle,
                    &hands.LeftHand.Pointing,&hands.RightHand.Pointing,&hands.LeftHand.Fist,&hands.RightHand.Fist};
                for(size_t i=0;i<6;++i)if(!gestures[i]->Name.empty() && gestures[i]->Name!=".apr"){
                    auto name=gestures[i]->Name;
                    if(name.size()>=4 && name.substr(name.size()-4)==".apr")name.resize(name.size()-4);
                    input.hands[i]=name;
                    // SimAvatar uses the idle texture for every gesture.
                    const auto& texture=i%2?hands.RightHand.Idle.TexName:hands.LeftHand.Idle.TexName;
                    if(!texture.empty())input.hands[i]+=",HAND="+texture;
                }
            }
            if(input.head.empty() && input.body.empty())return nullptr;
            input.accessories.assign(avatar->BoundAppearances.begin(),avatar->BoundAppearances.end());
            std::sort(input.accessories.begin(),input.accessories.end());
            return preview().live_portrait(input);
        }catch(const odh::FormatError&){return nullptr;}
    }
    const odh::BitmapImage* icon(Entity* e){
        if(!e || !e->Object || !e->Object->Resource)return nullptr;
        if(auto avatar=dynamic_cast<Avatar*>(e))return avatar_icon(avatar);
        const uint32_t guid=e->Object->GUID;auto found=icons.find(guid);if(found!=icons.end())return found->second?&*found->second:nullptr;
        if(icons.size()>=16)icons.erase(icons.begin());
        auto& image=icons[guid];
        auto obj=dynamic_cast<FSO::SimAntics::VMGameObject*>(e);if(!obj)return nullptr;
        auto definition=obj->MasterDefinition?obj->MasterDefinition:obj->Object->OBJ;
        const uint16_t id=definition->CatalogStringsID;
        if(auto raw=e->Object->Resource->Get<BMP>(id)){
            const auto& data=raw->GetData();std::string bytes(reinterpret_cast<const char*>(data.data()),data.size());std::istringstream stream(bytes,std::ios::binary);
            odh::BitmapLimits limits;limits.width=limits.height=256;limits.pixels=65536;
            image=odh::read_bmp(odh::View(stream,bytes.size()),limits,odh::BmpExtentPolicy::iff_container);
        }
        return image?&*image:nullptr;
    }
};
OriginalPCUI::OriginalPCUI(const std::string& root):P(new Impl(root)){}
OriginalPCUI::~OriginalPCUI()=default;
OriginalPCUI& OriginalPCUI::Get(){static std::unique_ptr<OriginalPCUI> instance;auto root=FSO::Content::Content::Get()->SimsCompleteDir;if(!instance||instance->P->root!=root)instance.reset(new OriginalPCUI(root));return *instance;}
void OriginalPCUI::DrawPanel(Canvas& c,odh_host::OriginalPanelMode mode,const odh_host::OriginalPanelState& state){P->last_state=state;P->last_layout=odh_host::original_control_panel_layout(c.Width,c.Height);if(mode!=odh_host::OriginalPanelMode::live){P->last_state.live_tabs.fill(false);P->last_state.inventory_enabled=false;P->last_state.simulation_speed.reset();}P->copy_in(c,c.Height-183,183);P->panel.draw(P->surface,mode,state);P->copy_out(c,c.Height-183,183);}
const Canvas* OriginalPCUI::Face(Avatar* a,int frame){if(!a||!a->Object||!a->Object->Resource)return nullptr;auto& f=P->faces(a);return &f.canvases[std::clamp(frame,0,int(f.canvases.size())-1)];}
void OriginalPCUI::DrawPortrait(Canvas& c,odh::UiBitmapRect r,bool selected,Avatar* a){if(!a)return;auto& f=P->faces(a);const auto* image=f.images.faces.empty()?&*f.generated:&f.images.faces[0];const Rect bounds{r.x,r.y,r.width,r.height};original::Raster(c,P->surface,bounds);const auto contains=[&](const std::optional<std::array<int,2>>& p){return p&&r.contains((*p)[0],(*p)[1]);};const auto state=odh_host::original_button_frame(true,selected,contains(P->last_state.pointer),contains(P->last_state.press_origin));P->panel.draw_portrait_state(P->surface,r,state,image,true);original::Copy(P->surface,c,bounds);}
std::vector<odh_host::QueueTile> OriginalPCUI::QueueTiles(Avatar* a,int width,int height){
    if(!a||!a->Thread)return {};
    odh::InteractionQueueSnapshot snapshot{};snapshot.actor_id=a->ObjectID;snapshot.actor_generation=snapshot.run_generation=1;
    const auto& actions=a->Thread->Queue;
    for(const auto& q:actions){odh::InteractionActionSnapshot item{};item.uid=q->UID;item.priority=q->Priority;item.notify_idle=q->Cancelled;
        using M=FSO::SimAntics::Engine::VMQueueMode;
        item.mode=q->Mode==M::Idle?odh::InteractionQueueMode::idle:q->Mode==M::ParentIdle?odh::InteractionQueueMode::parent_idle:q->Mode==M::ParentExit?odh::InteractionQueueMode::parent_exit:odh::InteractionQueueMode::normal;
        snapshot.actions.push_back(item);
    }
    int active=a->Thread->ActiveQueueBlock;
    if(active>=0&&size_t(active)<actions.size())snapshot.active=odh::InteractionActiveSnapshot{actions[size_t(active)]->UID,0,{}};
    return odh_host::original_queue_layout(snapshot,width,height,a->GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable::Priority));
}
void OriginalPCUI::DrawQueue(Canvas& c,Avatar* a){
    if(!a||!a->Thread)return;
    auto tiles=QueueTiles(a,c.Width,c.Height);
    if(tiles.empty())return;
    // The current engine marks cancellation synchronously on the selected
    // actor's action. A bare UID from another actor must never add a red X.
    for(const auto& tile:tiles){
        const Rect bounds=original::Clip(c,{tile.box.x,tile.box.y,tile.box.width,tile.box.height});
        if (!bounds.W || !bounds.H) continue;
        const auto& action=a->Thread->Queue[tile.action_index];
        auto owner=action->IconOwner();
        const auto avatar=dynamic_cast<Avatar*>(owner);
        const auto face=avatar&&avatar->Object&&avatar->Object->Resource?P->queue_face(avatar):nullptr;
        const auto kind=avatar?(face?odh_host::QueueIconKind::authored_avatar:odh_host::QueueIconKind::avatar_thumbnail):
            odh_host::QueueIconKind::object;
        original::Raster(c,P->surface,bounds);
        P->queue.draw_tile(P->surface,tile,face?face:P->icon(owner),kind);
        original::Copy(P->surface,c,bounds);
    }
}
void OriginalPCUI::DrawPauseLabel(Canvas& target,const std::string& rawCaption) {
    if(!P->pause_font_prepared){
        P->pause_font.LoadCaption(front::ResGraphics::Get().Raw("Fonts/variablesans_48.ffn"));
        P->pause_font_prepared=true;
    }
    const auto key=std::to_string(original::TextLanguage())+":"+std::to_string(target.Width)+":"+rawCaption;
    if(P->pause_key!=key){
        P->pause_key=key;
        if(P->pause_font.CanDraw(rawCaption)){
            P->pause_label=Canvas(P->pause_font.Width(rawCaption),P->pause_font.LineHeight());
            P->pause_label.Clear(0);
            P->pause_font.Draw(P->pause_label,0,0,rawCaption,RGBA(255,0,0));
            // Canvas clears/blends onto opaque black. This authored face paints
            // pure red with no shadow layer: its red channel is glyph coverage.
            // Restore that coverage before caching a transparent scene overlay.
            for(auto& pixel:P->pause_label.Px)
                pixel=RGBA(255,0,0,uint8_t(pixel));
        }else{
            // This small authored face only contains its Latin pause alphabet.
            // Preserve selected-language glyphs through the existing host shaper
            // when that face cannot represent them; do not silently drop letters.
            const auto text=original::LegacyToUTF8(rawCaption);
            const auto language=original::TextLanguage();
            const auto extent=odh_host::measure_text_overlay(text,language,64);
            odh::RasterFrame image{};image.width=std::max(1,std::min(target.Width-8,extent.width));
            image.height=std::max(1,extent.height);image.pixels.resize(size_t(image.width)*image.height,{0,0,0,0});
            odh_host::TextOverlayStyle style;style.x=style.y=style.padding=0;
            style.width=image.width;style.height=image.height;style.font_size_px=64;
            style.foreground={255,0,0,255};style.background={0,0,0,0};style.centered=true;
            odh_host::draw_text_overlay(image,text,language,style);
            P->pause_label=canvas(odh::BitmapImage{image.width,image.height,32,true,std::move(image.pixels),0});
        }
    }
    const auto& label=P->pause_label;
    // Original PT1024 caption: origin400,20; 224px advance, exact 3104px
    // opaque-red glyph mask. Position stays centered as the viewport changes.
    target.Blit(label.Px.data(),label.Width,label.Height,(target.Width-label.Width)/2,20,true);
}

void OriginalPCUI::DrawPieHead(Canvas& canvas,Avatar* a,std::array<std::int32_t,2> center,std::array<std::int32_t,2> target){
    if(!a)throw odh::FormatError("pie portrait requires an avatar");
    const auto* image=P->preview().pie_portrait(body_input(a),center,target);
    if(!image)throw odh::FormatError("original pie portrait: "+P->portraits->diagnostic());
    if (image->width==200 && image->height==200)
        original::DrawPortraitBitmap(canvas,*image,center[0]-100,center[1]-100);
}
Canvas OriginalPCUI::PetBody(Avatar* pet, const std::string& outfit) {
    auto body = body_input(pet);
    body.body = outfit;
    auto image = P->preview().pet_body(body);
    return canvas(image);
}
bool OriginalPCUI::GenerateFaces(FSO::Content::GameObject* object,Canvas& faces){
    if(!object||!object->Resource||!object->OBJ)return false;
    auto strings=object->Resource->Get<STR>(object->OBJ->BodyStringID);
    if(!strings)return false;
    // Resolve the immutable character resource through exactly the same
    // descriptor as HUD avatars; no VM/instance or assumed adult appearance.
    return GenerateFaces(body_input(strings),faces);
}
bool OriginalPCUI::GenerateFaces(const odh::AvatarBodyStrings& body,Canvas& faces){
    const auto* raw=P->generated(body);if(!raw)return false;
    odh::RasterFrame frame{};frame.width=frame.height=45;frame.pixels.assign(45*45,{0,0,80,255});
    P->panel.draw_portrait(frame,{0,0,45,45},false,raw,true);
    faces.Resize(105,41);
    // The character format is three 35x41 cells. The provider already owns
    // composition; use the same full-canvas path as live generated portraits.
    // These generated cells do not replace any existing BMP2002 resource.
    for(int i=0;i<3;++i)for(int y=0;y<41;++y)for(int x=0;x<35;++x){const auto& p=frame.pixels[size_t(y+2)*45+x+5];faces.Px[size_t(y)*105+i*35+x]=RGBA(p[0],p[1],p[2],p[3]);}
    return true;
}
std::string OriginalPCUI::ControlAt(int x,int y)const{
    using A=odh_host::OriginalPanelAction;
    switch(P->panel.hit(P->last_layout,x,y,P->last_state)){
        case A::pause:return "speed0";case A::rotate_camera_left:return "rotate_left";case A::rotate_camera_right:return "rotate_right";
        case A::zoom_in:return "zoom_in";case A::zoom_out:return "zoom_out";case A::walls_up:return "walls_up";case A::walls_down:return "walls_down";
        case A::level_up:return "level_up";case A::level_down:return "level_down";case A::walls_cutaway:return "walls_cut";case A::options:return "mode_options";
        case A::live_needs:return "tab_mood";case A::live_relationships:return "tab_rel";case A::live_career:return "tab_job";case A::live_personality:return "tab_pers";case A::live_interests:return "tab_interest";case A::live_house:return "tab_house";
        case A::live_inventory:return "tab_inv";
        case A::speed_normal:return "speed1";case A::speed_fast:return "speed2";case A::speed_ultra:return "speed3";default:return {};
    }
}
bool OriginalPCUI::IsControl(const std::string& id){
    static const std::set<std::string> ids={"speed0","speed1","speed2","speed3","rotate_left","rotate_right","zoom_in","zoom_out","walls_up","walls_down","level_up","level_down","walls_cut","mode_options","tab_mood","tab_rel","tab_job","tab_pers","tab_interest","tab_house","tab_inv"};
    return ids.count(id)!=0;
}
odh_host::CatalogObjectThumbnailResult OriginalPCUI::BuildCatalogThumbnail(std::uint32_t guid){
    return BuildCatalogThumbnail(guid,3,false);
}
odh_host::CatalogObjectThumbnailResult OriginalPCUI::BuildHelpThumbnail(std::uint32_t guid){
    // DVD0050fcbb has nominal168x127 picture slots;0043856d tests authored
    // zoom3→2→1 and requires >2px on each side. Never resize a near bitmap.
    for(std::uint8_t zoom=3;;--zoom){
        auto result=BuildCatalogThumbnail(guid,zoom,true);
        if(result.status!=odh_host::CatalogObjectThumbnailStatus::ready || zoom==1 ||
           ((168-int(result.image.width))/2>2 && (127-int(result.image.height))/2>2))return result;
    }
}
odh_host::CatalogObjectThumbnailResult OriginalPCUI::BuildCatalogThumbnail(std::uint32_t guid,std::uint8_t zoom,bool authoredFront){
    odh_host::CatalogObjectThumbnailResult result;
    std::string source;
    std::size_t part_count=0,layer_count=0,frame_count=0;
    unsigned first_sprite=0,first_frame=0,first_width=0,first_height=0,first_flags=0;
    unsigned selected_chunk=0,master=0,view=0;int sub_index=0;
    const auto report=[&](){
        const char* status=result.status==odh_host::CatalogObjectThumbnailStatus::ready?"ready":
            result.status==odh_host::CatalogObjectThumbnailStatus::no_graphics?"no-graphics":"failed";
        std::fprintf(stderr,"catalog thumbnail guid=%08x source=%.180s objd=%u master=%u sub=%d view=%u/zoom%u parts=%zu layers=%zu frames=%zu first-spr2=%u:%u frame=%ux%u flags=%u status=%s image=%ux%u detail=%.240s\n",
            unsigned(guid),source.c_str(),selected_chunk,master,sub_index,view,unsigned(zoom),part_count,layer_count,frame_count,
            first_sprite,first_frame,first_width,first_height,first_flags,status,result.image.width,
            result.image.height,result.detail.c_str());
        return std::move(result);
    };
    try {
        namespace C=FSO::Files::Formats::IFF::Chunks;
        auto content=FSO::Content::Content::Get();
        if(!content||!content->WorldObjects)throw odh::FormatError("current object provider unavailable");
        auto& provider=*content->WorldObjects;
        auto selected=provider.Get(guid,true);
        if(!selected||!selected->OBJ||!selected->Resource||selected->OBJ->GUID!=guid)
            throw odh::FormatError("selected catalog definition unavailable from current provider");
        if(zoom<1||zoom>3)throw odh::FormatError("thumbnail zoom outside authored range");
        view=authoredFront?((unsigned(selected->OBJ->FrontDirection)+4)&7U):0;
        if(view&1U)throw odh::FormatError("thumbnail authored direction has no orthographic DGRP view");
        source=selected->Resource->Name;selected_chunk=selected->OBJ->ChunkID;
        master=selected->OBJ->MasterID;sub_index=selected->OBJ->SubIndex;
        std::vector<C::OBJD*> parts;
        const bool multi=selected->OBJ->MasterID&&selected->OBJ->SubIndex==-1;
        if(multi){
            for(auto part:selected->Resource->List<C::OBJD>())
                if(part->MasterID==selected->OBJ->MasterID&&part->SubIndex!=-1)parts.push_back(part);
        }else parts.push_back(selected->OBJ);
        part_count=parts.size();
        if(parts.empty()||parts.size()>64)throw odh::FormatError("catalog group part budget exceeded or empty");
        std::set<std::uint32_t> admitted;
        std::map<C::SPR2Frame*,std::shared_ptr<const odh::Spr2Frame>> frames;
        std::vector<odh_host::CatalogObjectThumbnailSprite> sprites;
        std::array<float,3> origin{};
        std::uint64_t decoded_pixels=0;
        const auto signed_byte=[](unsigned n){n&=255;return n<128?int(n):int(n)-256;};
        for(std::size_t n=0;n<parts.size();++n){
            auto part=parts[n];auto resolved=provider.Get(part->GUID,true);
            if(!admitted.insert(part->GUID).second||!resolved||resolved->OBJ!=part||resolved->Resource!=selected->Resource)
                throw odh::FormatError("catalog group part differs from current provider selection");
            const unsigned bits=std::uint16_t(part->SubIndex);
            std::array<float,3> position{float(signed_byte(bits>>8)),float(signed_byte(bits)),float(signed_byte(part->LevelOffset))*2.95f};
            if(!n)origin=position;
            for(unsigned axis=0;axis<3;++axis)position[axis]-=origin[axis];
            auto group=resolved->Resource->Get<C::DGRP>(part->BaseGraphicID);
            if(!group){if(!part->BaseGraphicID||!part->NumGraphics)continue;throw odh::FormatError("catalog initial DGRP unavailable");}
            if(!group->ChunkParent)throw odh::FormatError("catalog DGRP has no authenticated resource owner");
            // Match preserved thumbnail: authored Graphic0, north, near zoom,
            // rotation0, no dynamic sprite flags; never run object Init here.
            auto image=group->GetImage(1,zoom,view/2);
            if(!image)throw odh::FormatError("catalog initial DGRP view unavailable");
            for(const auto& layer:image->Sprites){
                if(layer.SpriteID>=part->DynamicSpriteBaseId&&
                   layer.SpriteID<std::uint32_t(part->DynamicSpriteBaseId)+part->NumDynamicSprites)continue;
                if(sprites.size()>=4096||layer.SpriteID>65535)throw odh::FormatError("catalog sprite layer budget exceeded");
                auto sprite=group->ChunkParent->Get<C::SPR2>(std::uint16_t(layer.SpriteID));
                if(!sprite||layer.SpriteFrameIndex>=sprite->Frames.size())throw odh::FormatError("catalog SPR2 frame unavailable");
                auto native=sprite->Frames[layer.SpriteFrameIndex].get();
                if(!native)throw odh::FormatError("catalog SPR2 frame unavailable");
                auto cached=frames.find(native);
                if(cached==frames.end()){
                    // Check only unique frames against the remaining budget;
                    // another layer can reuse a frame even when it is exhausted.
                    const auto remaining=2*1048576-decoded_pixels;
                    constexpr std::size_t max_encoded=2*1048576*8+2048*16+16;
                    native->DecodeIfRequiredBounded(2048,remaining,max_encoded);
                    if(!native->Width||!native->Height)continue;
                    const auto count=std::uint64_t(native->Width)*native->Height;
                    decoded_pixels+=count;
                    if((native->Flags&3U)!=3U||(native->Flags&~7U)||native->ZBufferData.size()!=count||
                       (!native->PixelData.empty()?native->PixelData.size()!=count:
                        (!native->PalColors||native->PalData.size()!=count||native->AlphaData.size()!=count)))
                        throw odh::FormatError("catalog SPR2 pixel/depth data unavailable");
                    auto frame=std::make_shared<odh::Spr2Frame>();
                    frame->flags=native->Flags;frame->width=std::uint16_t(native->Width);frame->height=std::uint16_t(native->Height);
                    frame->z=native->ZBufferData;frame->pixels.reserve(std::size_t(count));
                    for(std::size_t i=0;i<count;++i){const auto color=native->ColorAt(i);frame->pixels.push_back({color.R,color.G,color.B,color.A});}
                    cached=frames.emplace(native,std::move(frame)).first;
                    frame_count=frames.size();
                    if(frame_count==1){first_sprite=layer.SpriteID;first_frame=layer.SpriteFrameIndex;
                        first_width=native->Width;first_height=native->Height;first_flags=native->Flags;}
                }
                odh::DgrpSprite drawing{};drawing.sprite_id=layer.SpriteID;drawing.frame=layer.SpriteFrameIndex;
                drawing.flags=std::uint32_t(layer.Flags);
                if(!std::isfinite(layer.SpriteOffset.X)||!std::isfinite(layer.SpriteOffset.Y)||
                   double(layer.SpriteOffset.X)<INT32_MIN||double(layer.SpriteOffset.X)>INT32_MAX||
                   double(layer.SpriteOffset.Y)<INT32_MIN||double(layer.SpriteOffset.Y)>INT32_MAX)
                    throw odh::FormatError("catalog sprite offset invalid");
                drawing.x=std::int32_t(layer.SpriteOffset.X);drawing.y=std::int32_t(layer.SpriteOffset.Y);
                const float offsets[]{layer.ObjectOffset.X,layer.ObjectOffset.Y,layer.ObjectOffset.Z};
                for(unsigned axis=0;axis<3;++axis)std::memcpy(&drawing.object_offset_bits[axis],&offsets[axis],sizeof(float));
                sprites.push_back({cached->second,drawing,position,std::uint16_t(n+1)});
                layer_count=sprites.size();
            }
        }
        result=odh_host::CatalogObjectThumbnail::compose_prepared(sprites,std::uint8_t(view/2),zoom);
        return report();
    }catch(const std::exception& error){result.status=odh_host::CatalogObjectThumbnailStatus::failed;result.detail=error.what();return report();}
}
std::string OriginalPCUI::PrepareCatalogThumbnail(std::uint32_t guid){
    // Popup ownership stays local; grid requests never replace this selection.
    P->thumbnail_guid=guid;P->thumbnail={}; // Failure cannot retain an earlier selection.
    P->thumbnail=BuildCatalogThumbnail(guid);
    return P->thumbnail.status==odh_host::CatalogObjectThumbnailStatus::ready?std::string{}:P->thumbnail.detail;
}
const odh::BitmapImage* OriginalPCUI::CatalogThumbnail(std::uint32_t guid)const{
    return P->thumbnail_guid==guid&&P->thumbnail.status==odh_host::CatalogObjectThumbnailStatus::ready?&P->thumbnail.image:nullptr;
}
odh::UiBitmapRect OriginalPCUI::DrawCatalogInfo(Canvas& c,const odh_host::CatalogInfoContent& content,const std::string& selection){
    if(!P->catalog_open||P->catalog_selection!=selection)P->catalog_scroll.reset();
    P->catalog_selection=selection;P->catalog_open=true;
    const auto language=original::TextLanguage();
    const auto measure=odh_host::catalog_info_text_measure(language);
    P->catalog_layout=P->catalog_scroll.prepare(c.Width,c.Height,content,measure);
    const auto panel=P->catalog_layout.panel;
    P->copy_in(c,panel.y,panel.height);
    P->catalog.draw(P->surface,content,odh_host::catalog_info_text_painter(language),measure,
        &P->catalog_scroll,odh_host::catalog_info_scroll_painter(language),odh_host::catalog_info_heading_painter(language));
    P->copy_out(c,panel.y,panel.height);
    return panel;
}
bool OriginalPCUI::ScrollCatalogInfo(int wheel,int x,int y){
    return P->catalog_open&&P->catalog_scroll.consume({-wheel,0},P->catalog_layout,x,y);
}
void OriginalPCUI::ClearCatalogInfo(){P->catalog_open=false;P->catalog_selection.clear();P->catalog_scroll.reset();}
const std::string& OriginalPCUI::PortraitSource()const{return P->last_source;}
}

#endif
