/* SPDX-License-Identifier: MPL-2.0 */
#include "app/ui/DesktopOptionsPanel.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include "world/model/Blueprint.h"
#include "world/components/AvatarComponent.h"
#include "vitaboy/SimAvatar.h"
// Data-free fixture access only: no product debug API or alternate renderer.
#define private public
#include "world/World.h"
#include "app/front/FrontEnd.h"
#include "app/App.h"
#undef private
#include "app/front/GameOptions.h"
using namespace simapp;using namespace simapp::original;
using Id=odh_host::OriginalSettingId;
void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
std::string read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(){try{
    const auto dir=std::filesystem::temp_directory_path()/("odh-native-options-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    need(std::filesystem::create_directory(dir),"new disposable fixture root");
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}}cleanup{dir};
    const auto source=dir/"source.txt";
    {std::ofstream f(source);f<<"Shadows=0\nLighting=0\nInterfaceEffects=0\nTerrainDetail=1\nCharacterDetail=0\nSoundVolume=31\nMusicVolume=62\nVoxVolume=93\nFreeWill=0\nAutonomyLevel=2\nAutoCentering=0\nEdgeScrolling=1\nQuickTips=0\nAutoSnapshot=0\nLivePIP=1\n";}
    const auto legacyPortable=dir/"old-handheld.txt";
    {std::ofstream f(legacyPortable);f<<"Shadows=1\nLighting=1\nInterfaceEffects=1\nTerrainDetail=2\nCharacterDetail=2\nMusicVolume=27\n";}
    front::FrontSettings handheld;
    handheld.Load(legacyPortable.string(),true);
    need(handheld.HandheldGraphics&&!handheld.Shadows&&!handheld.Lighting&&!handheld.InterfaceEffects&&
         handheld.TerrainDetail==0&&handheld.CharacterDetail==0&&handheld.MusicVolume==27,"old handheld graphics migrate independently of other options");
    handheld.Shadows=true;
    handheld.Save(legacyPortable.string());
    front::FrontSettings reopenedHandheld;
    reopenedHandheld.Load(legacyPortable.string(),true);
    need(reopenedHandheld.Shadows&&reopenedHandheld.TerrainDetail==0,"handheld player's new shadow choice survives restart");
    front::FrontSettings freshHandheld;
    freshHandheld.Load((dir/"nonexistent").string(),true);
    need(!freshHandheld.Shadows&&!freshHandheld.Lighting&&!freshHandheld.InterfaceEffects&&
         freshHandheld.TerrainDetail==0&&freshHandheld.CharacterDetail==0,"low handheld profile applies on first boot");
    front::FrontEnd fe;fe.Settings().Load(source.string());auto& st=fe.Settings();
    const auto snapshot=CaptureDesktopOptions(st,true,false);
    need(snapshot.state.values[size_t(Id::sound_effects)]==31&&snapshot.state.values[size_t(Id::music)]==62&&
        snapshot.state.values[size_t(Id::voices)]==93&&snapshot.state.values[size_t(Id::free_will)]==0,"producer reads real loaded native settings");
    need(!snapshot.state.values[size_t(Id::background_simulation)]&&!snapshot.state.values[size_t(Id::export_html)],"missing native preferences are not invented");
    need(!snapshot.capabilities[size_t(Id::shadows)]&&!snapshot.capabilities[size_t(Id::lighting)]&&
        snapshot.capabilities[size_t(Id::music)]&&snapshot.capabilities[size_t(Id::free_will)]&&snapshot.capabilities[size_t(Id::edge_scrolling)],"capabilities require real selected consumers");
    st.Save((dir/"before.txt").string());const auto before=read(dir/"before.txt");
    for(const auto id:{Id::shadows,Id::lighting,Id::terrain_detail,Id::auto_center,Id::quick_tips,Id::auto_snapshot,Id::live_pip,Id::background_simulation,Id::export_html})
        need(!ApplyDesktopOption(st,{id,1},snapshot.capabilities),"unavailable control refuses state mutation");
    st.Save((dir/"refused.txt").string());need(read(dir/"refused.txt")==before,"refusals preserve all persisted preferences and schema");
    need(ApplyDesktopOption(st,{Id::music,17},snapshot.capabilities)&&ApplyDesktopOption(st,{Id::free_will,1},snapshot.capabilities),"authorized native settings edits apply");
    need(st.SoundVolume==31&&st.VoxVolume==93&&st.AutonomyLevel==2&&!st.Shadows&&!st.AutoCentering&&st.EdgeScrolling,
        "volume and desktop FreeWill edits preserve other native preferences");
    need(!ApplyDesktopOption(st,{Id::music,101},snapshot.capabilities)&&!ApplyDesktopOption(st,{Id::free_will,2},snapshot.capabilities),"malformed events refuse");
    const auto live=CaptureDesktopOptions(st,true,false);
    need(live.state.values[size_t(Id::free_will)]==0&&st.FreeWill,"actual VM state takes precedence over a saved preference without rewriting it");
    need(ApplyDesktopOption(st,{Id::free_will,1},live.capabilities),"valid FreeWill command still reaches callback when stored preference already matches");
    const auto absent=CaptureDesktopOptions(st,false,{});
    need(!ApplyDesktopOption(st,{Id::music,22},absent.capabilities)&&!ApplyDesktopOption(st,{Id::free_will,0},absent.capabilities),"unavailable audio/VM consumers refuse editing");
    need(ApplyDesktopOption(st,{Id::edge_scrolling,0},snapshot.capabilities)&&!st.EdgeScrolling,"native camera preference can be disabled");
    need(!ApplyDesktopOption(st,{Id::edge_scrolling,2},snapshot.capabilities)&&!st.EdgeScrolling,"malformed camera preference refuses mutation");
    st.Save((dir/"saved.txt").string());front::FrontSettings reopened;reopened.Load((dir/"saved.txt").string());
    need(reopened.MusicVolume==17&&reopened.FreeWill&&reopened.AutonomyLevel==2&&!reopened.Shadows&&!reopened.EdgeScrolling,
        "actual native SafeFile settings save/reopen retains edits and unavailable preferences");
    // Exercise the native preference -> App settings callback -> actual World
    // consumer and rasterizer. No content, simulation updates or game saves.
    App app;
    app._Front=std::make_unique<front::FrontEnd>();
    app._Front->_Cfg.Desktop=true;
    app._Front->_Cfg.SaveRoot=dir.string();
    app._Front->Settings().Load(source.string());
    app._Session=std::make_unique<LotSession>();
    app._Session->_World=std::make_unique<FSO::LotView::World>(320,240);
    auto& world=*app._Session->World();
    auto& graphics=app._Front->Settings();
    app._Front->SettingsChanged=[&](const front::FrontSettings&){app.ApplyPlaySettings();};
    need(world.DrawAvatarShadows&&!graphics.Shadows,"fixture separates default World from loaded preference");
    app.ApplyPlaySettings(); // Same boundary invoked after each successful lot load.
    need(!world.DrawAvatarShadows,"loaded desktop preference reaches the current World");
    const auto graphicsSnapshot=CaptureDesktopOptions(graphics,false,{},&world);
    need(graphicsSnapshot.capabilities[size_t(Id::shadows)]&&
        !graphicsSnapshot.capabilities[size_t(Id::lighting)]&&
        !graphicsSnapshot.capabilities[size_t(Id::interface_effects)]&&
        !graphicsSnapshot.capabilities[size_t(Id::terrain_detail)]&&
        !graphicsSnapshot.capabilities[size_t(Id::character_detail)],
        "only the existing shadow renderer is offered; missing quality services remain unavailable");
    need(!ApplyDesktopOption(graphics,{Id::shadows,2},graphicsSnapshot.capabilities),"malformed shadow commands rejected");
    const auto unbound=CaptureDesktopOptions(graphics,false,{});
    need(!ApplyDesktopOption(graphics,{Id::shadows,1},unbound.capabilities),"no World means no editable shadow capability");
    need(ApplyDesktopOption(graphics,{Id::shadows,1},graphicsSnapshot.capabilities),"shadow checkbox accepts a valid command");
    app._Front->SaveSettings();
    need(world.DrawAvatarShadows,"actual save-settings callback updates the current World");
    front::FrontSettings graphicsReopened;graphicsReopened.Load((dir/"sims_options.txt").string());
    need(graphicsReopened.Shadows&&!graphicsReopened.Lighting&&!graphicsReopened.InterfaceEffects&&
        graphicsReopened.TerrainDetail==1&&graphicsReopened.CharacterDetail==0,
        "reopen retains the shadow command and untouched unavailable graphics preferences");
    // Existing rasterizer, geometry-free avatar: only its floor shadow can alter
    // pixels. Compare colour and depth independently, then restore the option.
    FSO::LotView::Model::Blueprint blueprint(32,32);
    FSO::Vitaboy::SimAvatar geometryFreeAvatar;
    FSO::LotView::Components::AvatarComponent avatar;
    avatar.Avatar=&geometryFreeAvatar;avatar.Position=st::Vector3(16,16,0);avatar.Level=1;
    blueprint.AddAvatar(&avatar);world.InitBlueprint(&blueprint);
    world.State.Level(1);world.State.CenterTile(st::Vector2(16,16));
    // The normal World2D pass supplies this scroll before the avatar pass.
    // This geometry-free fixture skips that pass, so establish its camera state.
    const auto offset=world.State.WorldSpace.GetScreenOffset();
    world.State._2D->SetScroll(st::Vector2(-offset.X,-offset.Y));
    FSO::LotView::Framebuffer pixels(320,240);pixels.Clear(FSO::LotView::Framebuffer::Pack(255,255,255,255));
    const auto blank=pixels.Color;const auto initialDepth=pixels.Depth;
    world.DrawAvatarComponents(pixels);
    const auto shadowed=pixels.Color;
    need(shadowed!=blank&&pixels.Depth==initialDepth,"enabled actual shadow pass darkens pixels without depth writes");
    need(ApplyDesktopOption(graphics,{Id::shadows,0},graphicsSnapshot.capabilities),"shadow checkbox can disable the effect");
    app._Front->SaveSettings();pixels.Clear(FSO::LotView::Framebuffer::Pack(255,255,255,255));
    world.DrawAvatarComponents(pixels);
    need(pixels.Color==blank&&pixels.Depth==initialDepth,"disabled consumer skips the shadow without altering pixels/depth");
    need(ApplyDesktopOption(graphics,{Id::shadows,1},graphicsSnapshot.capabilities),"shadow checkbox can restore the effect");
    app._Front->SaveSettings();pixels.Clear(FSO::LotView::Framebuffer::Pack(255,255,255,255));world.DrawAvatarComponents(pixels);
    need(pixels.Color==shadowed,"on-off-on restores the same real render result");
    world.DrawAvatarShadows=false; // External host changed only the live state.
    const auto mismatch=CaptureDesktopOptions(graphics,false,{},&world);
    need(mismatch.state.values[size_t(Id::shadows)]==0&&graphics.Shadows,
        "display uses current renderer state while preserving the saved preference");
    need(ApplyDesktopOption(graphics,{Id::shadows,1},mismatch.capabilities),"matching saved value still delivers renderer command");
    app._Front->SaveSettings();need(world.DrawAvatarShadows,"same-value command repairs live state through callback");
    app._Session->_World=std::make_unique<FSO::LotView::World>();
    app._Session->World()->DrawAvatarShadows=false;app.ApplyPlaySettings();
    need(app._Session->World()->DrawAvatarShadows,"new World receives the preference on the load/travel boundary");
    app._Front->_Cfg.Desktop=false;app._Session->World()->DrawAvatarShadows=false;app.ApplyPlaySettings();
    need(!app._Session->World()->DrawAvatarShadows,"portable hosts retain their existing rendering defaults");
    auto& options=front::GameOptions::Get();auto* previous=options.FE;const auto pending=options.Pending;
    struct Restore{front::GameOptions& o;front::FrontEnd* fe;front::GameOptions::Action a;~Restore(){o.FE=fe;o.Pending=a;}}restore{options,previous,pending};
    options.FE=&fe;DesktopOptionsPanel controller;
    need(!controller.Page(),"first native Options page starts empty");
    need(controller.Tap("pcopt_4")&&controller.Page()==odh_host::OriginalSettingsPage::sound,"Sound opens observed original page");
    controller.Reset();need(!controller.Page(),"mode exit/reentry clears page as observed in original");
    for(unsigned i=0;i<3;++i){need(controller.Tap("pcopt_"+std::to_string(i)),"original action dispatch");
        const auto expected=i==0?front::GameOptions::Action::Save:i==1?front::GameOptions::Action::Neighbourhood:front::GameOptions::Action::Quit;
        need(options.TakeAction()==expected,"actual existing AppFlow pending-action contract preserved");}
    need(!controller.Tap("pcopt_99")&&!controller.Tap("gopt_lighting"),"stale/unknown controls cannot bypass capabilities");
    std::cout<<"PASS native Options producer/persistence/capabilities/action lifecycle\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
