/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __3DS__
#include "app/ui/DesktopOptionsPanel.h"
#include "simantics/VM.h"
#include "world/World.h"
#include <algorithm>
#include "app/LotScreen.h"
#include "app/LotSession.h"
#include "app/Canvas.h"
#include "app/Input.h"
#include "app/front/GameOptions.h"
#include "app/front/FrontEnd.h"
#include "app/front/FrontAudio.h"
#include "app/ui/OriginalTextBackend.h"
#include "app/ui/OriginalTextEncoding.h"
#include "content/ContentManager.h"
#include "original_options_view.hpp"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/STR.h"
namespace simapp { namespace original {
namespace {
using Id=odh_host::OriginalSettingId;
size_t index(Id id){return static_cast<size_t>(id);}
constexpr unsigned captions[]={14,17,20,23,28,31,44,47,50,56,59,62,65,72,75,78,81};
constexpr const char* images[]={"AntiAlias","Shadows","Lighting","TransUI","TerrainDetail","CharDetail",
    "SFX","Music","Vox","AutoCenter","FreeWill","EdgeScroll","SimInBack","QuickTips","AutoSnap","LivePIP","Html"};
std::string helpArt(unsigned id){return id==16?"CPanel/PopupExportHTML.bmp":"CPanel/PopupOpt"+std::string(images[id])+".bmp";}
}
DesktopOptionsSnapshot CaptureDesktopOptions(const front::FrontSettings& st,bool audio,std::optional<bool> live_free_will,const FSO::LotView::World* graphics_world,bool host_focus_events){
    DesktopOptionsSnapshot out;
    const auto set=[&](Id id,int value){out.state.values[index(id)]=value;};
    set(Id::shadows,graphics_world?graphics_world->DrawAvatarShadows:st.Shadows);set(Id::lighting,graphics_world?graphics_world->State.ObjectLighting:st.Lighting);set(Id::interface_effects,st.InterfaceEffects);
    set(Id::terrain_detail,st.TerrainDetail);set(Id::character_detail,st.CharacterDetail);
    set(Id::sound_effects,front::OriginalVolumeStep(st.SoundVolume));set(Id::music,front::OriginalVolumeStep(st.MusicVolume));set(Id::voices,front::OriginalVolumeStep(st.VoxVolume));
    set(Id::auto_center,st.AutoCentering);set(Id::free_will,live_free_will.value_or(st.FreeWill));set(Id::edge_scrolling,st.EdgeScrolling);
    set(Id::quick_tips,st.QuickTips);set(Id::auto_snapshot,st.AutoSnapshot);set(Id::live_pip,st.LivePIP);
    if(host_focus_events)set(Id::background_simulation,st.BackgroundSimulation);
    for(const auto id:{Id::sound_effects,Id::music,Id::voices})out.capabilities[index(id)]=audio;
    out.capabilities[index(Id::free_will)]=live_free_will.has_value();
    out.capabilities[index(Id::edge_scrolling)]=true;
    out.capabilities[index(Id::shadows)]=graphics_world!=nullptr;
    out.capabilities[index(Id::lighting)]=graphics_world!=nullptr;
    out.capabilities[index(Id::auto_center)]=graphics_world!=nullptr;
    out.capabilities[index(Id::quick_tips)]=graphics_world!=nullptr;
    out.capabilities[index(Id::background_simulation)]=host_focus_events;
    // No invented AA or ExportHTML service. Background is exposed only by a
    // host that actually supplies focus transitions.
    // Other stored preferences remain visible but unavailable until their actual
    // native consumer is recovered; this does not classify them as C1 or accepted.
    return out;
}
bool ApplyDesktopOption(front::FrontSettings& st,const odh_host::OriginalSettingsEvent& event,
        const odh_host::OriginalSettingsCapabilities& capabilities){
    const auto at=index(event.id);if(at>=capabilities.size() || !capabilities[at])return false;
    if(event.id==Id::shadows){
        if(event.value<0 || event.value>1)return false;
        // The World state may differ from the stored preference (new lot or host).
        // Deliver valid commands even when the stored boolean already matches.
        st.Shadows=event.value!=0;return true;
    }
    if(event.id==Id::edge_scrolling){
        if(event.value<0 || event.value>1 || st.EdgeScrolling==(event.value!=0))return false;
        st.EdgeScrolling=event.value!=0;return true;
    }
    if(event.id==Id::free_will){
        if(event.value<0 || event.value>1)return false;
        st.FreeWill=event.value!=0;return true;
    }
    bool* toggle=event.id==Id::lighting?&st.Lighting:event.id==Id::auto_center?&st.AutoCentering:event.id==Id::quick_tips?&st.QuickTips:
        event.id==Id::background_simulation?&st.BackgroundSimulation:nullptr;
    if(toggle){if(event.value<0||event.value>1)return false;*toggle=event.value!=0;return true;}
    if(event.value<0 || event.value>10)return false;
    int* value=event.id==Id::sound_effects?&st.SoundVolume:event.id==Id::music?&st.MusicVolume:
        event.id==Id::voices?&st.VoxVolume:nullptr;
    if(!value || *value==event.value*10)return false;
    *value=event.value*10;return true;
}
struct DesktopOptionsPanel::Impl {
    std::string root;uint8_t language=0;
    std::unique_ptr<odh_host::OriginalOptionsView> actions;
    std::unique_ptr<odh_host::OriginalSettingsView> settings;
    std::optional<odh_host::OriginalSettingsPage> page;
    std::optional<unsigned> help_press;
    bool clear_help=true;
    odh_host::OriginalSettingsInteraction pointer;
    odh::RasterFrame frame;
    odh_host::OriginalSettingsText text;
    void resources(){
        auto content=FSO::Content::Content::Get();if(!content)return;
        const auto current=TextLanguage();
        if(actions && root==content->SimsCompleteDir && language==current)return;
        root=content->SimsCompleteDir;language=current;pointer.reset();
        actions=std::make_unique<odh_host::OriginalOptionsView>(root);
        // Actual Complete PC observations omit unsupported Anti-alias only.
        FSO::Files::Formats::IFF::IffFile selected(root+"/GameData/UIText.iff");
        const auto strings=selected.Get<FSO::Files::Formats::IFF::Chunks::STR>(145);
        if(!strings)throw odh::FormatError("selected runtime UIText STR145 is unavailable");
        text={};
        for(size_t i=0;i<text.size();++i){
            size_t set=size_t(language-1);
            const auto available=[&](size_t n){return n<strings->LanguageSets.size()&&i<strings->LanguageSets[n].Strings.size();};
            if(!available(set))set=0;
            if(available(set))text[i]=PersonalToUTF8(strings->LanguageSets[set].Strings[i].Value,uint8_t(set+1),
                strings->SourceFormat==-1||strings->SourceFormat==-4);
        }
        settings=std::make_unique<odh_host::OriginalSettingsView>(root,language,false,true,&text);
    }
};
DesktopOptionsPanel::DesktopOptionsPanel():p_(new Impl){}
DesktopOptionsPanel::~DesktopOptionsPanel()=default;
std::optional<odh_host::OriginalSettingsPage> DesktopOptionsPanel::Page()const{return p_->page;}
void DesktopOptionsPanel::Reset(){
    p_->page.reset();p_->pointer.reset();
    p_->help_press.reset();p_->clear_help=true;
    auto& options=front::GameOptions::Get();options.Help.clear();options.HelpTime=0;
}
void DesktopOptionsPanel::Draw(Canvas& canvas,LotScreen& lot){
    InstallTextBackendIfNeeded();p_->resources();if(!p_->actions)return;
    if(p_->clear_help){lot.ClearDesktopSettingsHelp();p_->clear_help=false;}
    if(!lot.S || !lot.S->Dialogs.empty() || lot.Pie.IsOpen)p_->pointer.reset();
    auto fe=front::GameOptions::Get().FE;
    DesktopOptionsSnapshot snapshot;if(fe)snapshot=CaptureDesktopOptions(fe->Settings(),front::FrontAudioAvailable(),lot.S&&lot.S->VM()?std::optional<bool>{lot.S->VM()->FreeWillEnabled}:std::nullopt,lot.S?lot.S->World():nullptr,fe->HasHostFocusEvents());
    auto& frame=p_->frame;frame.width=unsigned(canvas.Width);frame.height=unsigned(canvas.Height);frame.pixels.resize(canvas.Px.size());
    const int top=std::max(0,canvas.Height-100);
    for(int y=top;y<canvas.Height;++y)for(int x=0;x<canvas.Width;++x){const auto v=canvas.Px[size_t(y)*canvas.Width+x];
        frame.pixels[size_t(y)*canvas.Width+x]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};}
    const std::array<bool,6> enabled{{bool(fe),bool(fe),bool(fe),true,true,true}};
    const auto mouse=lot.DesktopPointerPosition();
    std::optional<size_t> pressed;const auto control=lot.DesktopPressedControl();
    if(control.size()==7&&control.compare(0,6,"pcopt_")==0&&control[6]>='0'&&control[6]<='5')pressed=size_t(control[6]-'0');
    std::optional<size_t> selected;if(p_->page)selected=size_t(*p_->page)+3;
    p_->actions->draw(frame,enabled,mouse,pressed,selected);
    const auto layout=odh_host::original_options_layout(frame.width,frame.height);
    for(size_t i=0;i<layout.buttons.size();++i)if(enabled[i]){const auto& r=layout.buttons[i];
        lot.Hit(Rect(r.x,r.y,r.width,r.height),"pcopt_"+std::to_string(i));
        static const unsigned names[6]={1,3,5,7,9,11};
        lot.OfferDesktopHover(p_->text[names[i]],"options-action:"+std::to_string(i),
            "options",p_->page?uint64_t(*p_->page)+1:0,Rect(r.x,r.y,r.width,r.height));
    }
    if(p_->page)p_->settings->draw(frame,*p_->page,snapshot.state,snapshot.capabilities,mouse,p_->pointer.captured());
    if(p_->page){
        const auto settings=p_->settings->layout(frame.width,frame.height,*p_->page);
        const auto offer=[&](unsigned id,const odh_host::BuyUiRect& r){
            if(id>=sizeof(captions)/sizeof(*captions))return;
            const auto i=captions[id];
            const std::string art=helpArt(id);
            lot.OfferDesktopHelp(p_->text[i+1],p_->text[i+2],"setting:"+std::to_string(id),
                "settings:"+std::to_string(unsigned(*p_->page)),0,Rect(r.x,r.y,r.width,r.height),art);
        };
        // Original help triples and automatic timers belong to captions;
        // checkbox/radio/slider glyphs only change the preference.
        for(const auto& label:settings.labels)for(unsigned i=0;i<sizeof(captions)/sizeof(*captions);++i)
            if(captions[i]==label.string_index)offer(i,label.rect);
    }
    for(int y=top;y<canvas.Height;++y)for(int x=0;x<canvas.Width;++x){const auto& v=frame.pixels[size_t(y)*canvas.Width+x];
        canvas.Px[size_t(y)*canvas.Width+x]=RGBA(v[0],v[1],v[2],v[3]);}
}
void DesktopOptionsPanel::Input(const AppInput& in,LotScreen& lot){
    if(in.PointerCancelled || in.MouseRightDown || (in.Down&BTN_B)){p_->pointer.reset();p_->help_press.reset();return;}
    if(!p_->page)return;
    p_->resources();auto fe=front::GameOptions::Get().FE;if(!p_->settings || !fe || !lot.Top){p_->pointer.reset();return;}
    const auto snapshot=CaptureDesktopOptions(fe->Settings(),front::FrontAudioAvailable(),lot.S&&lot.S->VM()?std::optional<bool>{lot.S->VM()->FreeWillEnabled}:std::nullopt,lot.S?lot.S->World():nullptr,fe->HasHostFocusEvents());
    const auto layout=p_->settings->layout(lot.Top->Width,lot.Top->Height,*p_->page);
    const auto captionAt=[&]()->std::optional<unsigned>{
        for(const auto& label:layout.labels)if(Rect(label.rect.x,label.rect.y,label.rect.width,label.rect.height).Contains(in.TouchX,in.TouchY))
            for(unsigned i=0;i<sizeof(captions)/sizeof(*captions);++i)if(captions[i]==label.string_index)return i;
        return {};
    };
    if(in.TouchDown)p_->help_press=captionAt();
    if(in.TouchUp){
        const auto released=captionAt();
        if(released&&released==p_->help_press){const auto id=*released;const auto text=captions[id];
            lot.ToggleDesktopSettingsHelp(p_->text[text+1],p_->text[text+2],"setting:"+std::to_string(id),
                "settings:"+std::to_string(unsigned(*p_->page)),helpArt(id));}
        p_->help_press.reset();
    }
    const auto event=p_->pointer.update(*p_->settings,layout,snapshot.state,snapshot.capabilities,
        {in.TouchX,in.TouchY,in.TouchDown,in.Touching,in.TouchUp,in.PointerCancelled});
    if(event && ApplyDesktopOption(fe->Settings(),*event,snapshot.capabilities)){
        const auto& st=fe->Settings();front::FrontVolumes(st.SoundVolume,st.MusicVolume,st.VoxVolume);
        fe->SaveSettings(); // Existing SafeFile persistence + actual current-VM FreeWill callback.
    }
}
bool DesktopOptionsPanel::Tap(const std::string& id){
    if(id.size()!=7 || id.compare(0,6,"pcopt_") || id[6]<'0' || id[6]>'5')return false;
    const unsigned choice=unsigned(id[6]-'0');auto& options=front::GameOptions::Get();p_->pointer.reset();p_->help_press.reset();p_->clear_help=true;
    if(choice<3){if(!options.FE)return true;
        options.Pending=choice==0?front::GameOptions::Action::Save:choice==1?front::GameOptions::Action::Neighbourhood:front::GameOptions::Action::Quit;
    }else {const auto page=static_cast<odh_host::OriginalSettingsPage>(choice-3);
        if(p_->page==page)p_->page.reset();else p_->page=page;}
    return true;
}
}}
#endif
