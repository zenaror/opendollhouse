/* SPDX-License-Identifier: MPL-2.0 */
#include "app/ui/DesktopOptionsPanel.h"
#include "app/front/FrontAudio.h"
#include "app/ui/OriginalHover.h"
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
#include "world/components/TerrainComponent.h"
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
    front::FrontSettings defaults;
    need(defaults.CharacterDetailRevision==0,"quality apply history starts empty and is not a saved preference");
    need(defaults.ApplyCharacterDetail(1)&&defaults.CharacterDetail==1&&defaults.CharacterDetailRevision==1,
        "explicit quality command carries raw global medium");
    need(defaults.ApplyCharacterDetail(1)&&defaults.CharacterDetailRevision==2,
        "reapplying selected quality is still an explicit material event");
    need(!defaults.ApplyCharacterDetail(-1)&&!defaults.ApplyCharacterDetail(3)&&
         defaults.CharacterDetail==1&&defaults.CharacterDetailRevision==2,"invalid quality command preserves event history");
    need(defaults.SoundVolume==80&&defaults.MusicVolume==80&&defaults.VoxVolume==80&&!defaults.BackgroundSimulation,
        "original factory volume8 and background-off defaults");
    const int originalLevels[]={0,300,390,490,590,690,790,840,895,960,1024};
    for(int i=0;i<=10;++i)need(front::OriginalVolumeStep(i*10)==i&&front::OriginalVolumeGain(i*10)==originalLevels[i]/1024.f,
        "all eleven original audio group gains, including mute and unity");
    need(front::OriginalVolumeStep(31)==3&&front::OriginalVolumeStep(62)==6&&front::OriginalVolumeStep(93)==9&&
        front::OriginalVolumeStep(-5)==0&&front::OriginalVolumeStep(105)==10,"legacy percentages use bounded nearest original step");
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
    const auto malformed=dir/"malformed.txt";
    {std::ofstream f(malformed);f<<"SoundVolume=-12\nMusicVolume=777\nVoxVolume=60\nTerrainDetail=-2\nCharacterDetail=99\nAutonomyLevel=7\n";}
    front::FrontSettings bounded;bounded.Load(malformed.string());
    need(bounded.SoundVolume==0&&bounded.MusicVolume==100&&bounded.VoxVolume==60&&bounded.TerrainDetail==0&&
         bounded.CharacterDetail==2&&bounded.AutonomyLevel==3,"old malformed numeric preferences clamp to supported domains");
    front::FrontEnd fe;fe.Settings().Load(source.string());auto& st=fe.Settings();
    const auto snapshot=CaptureDesktopOptions(st,true,false);
    need(snapshot.state.values[size_t(Id::sound_effects)]==3&&snapshot.state.values[size_t(Id::music)]==6&&
        snapshot.state.values[size_t(Id::voices)]==9&&snapshot.state.values[size_t(Id::free_will)]==0,"producer reads real loaded native settings");
    need(!snapshot.state.values[size_t(Id::background_simulation)]&&!snapshot.state.values[size_t(Id::export_html)],"missing native preferences are not invented");
    need(!snapshot.capabilities[size_t(Id::shadows)]&&!snapshot.capabilities[size_t(Id::lighting)]&&
        snapshot.capabilities[size_t(Id::music)]&&snapshot.capabilities[size_t(Id::free_will)]&&snapshot.capabilities[size_t(Id::edge_scrolling)],"capabilities require real selected consumers");
    st.Save((dir/"before.txt").string());const auto before=read(dir/"before.txt");
    for(const auto id:{Id::shadows,Id::lighting,Id::terrain_detail,Id::auto_center,Id::quick_tips,Id::auto_snapshot,Id::live_pip,Id::background_simulation,Id::export_html})
        need(!ApplyDesktopOption(st,{id,1},snapshot.capabilities),"unavailable control refuses state mutation");
    st.Save((dir/"refused.txt").string());need(read(dir/"refused.txt")==before,"refusals preserve all persisted preferences and schema");
    need(ApplyDesktopOption(st,{Id::music,7},snapshot.capabilities)&&ApplyDesktopOption(st,{Id::free_will,1},snapshot.capabilities),"authorized native settings edits apply");
    need(st.SoundVolume==31&&st.VoxVolume==93&&st.AutonomyLevel==2&&!st.Shadows&&!st.AutoCentering&&st.EdgeScrolling,
        "volume and desktop FreeWill edits preserve other native preferences");
    need(!ApplyDesktopOption(st,{Id::music,11},snapshot.capabilities)&&!ApplyDesktopOption(st,{Id::free_will,2},snapshot.capabilities),"malformed events refuse");
    const auto live=CaptureDesktopOptions(st,true,false);
    need(live.state.values[size_t(Id::free_will)]==0&&st.FreeWill,"actual VM state takes precedence over a saved preference without rewriting it");
    need(ApplyDesktopOption(st,{Id::free_will,1},live.capabilities),"valid FreeWill command still reaches callback when stored preference already matches");
    const auto absent=CaptureDesktopOptions(st,false,{});
    need(!ApplyDesktopOption(st,{Id::music,22},absent.capabilities)&&!ApplyDesktopOption(st,{Id::free_will,0},absent.capabilities),"unavailable audio/VM consumers refuse editing");
    need(ApplyDesktopOption(st,{Id::edge_scrolling,0},snapshot.capabilities)&&!st.EdgeScrolling,"native camera preference can be disabled");
    need(!ApplyDesktopOption(st,{Id::edge_scrolling,2},snapshot.capabilities)&&!st.EdgeScrolling,"malformed camera preference refuses mutation");
    st.Save((dir/"saved.txt").string());front::FrontSettings reopened;reopened.Load((dir/"saved.txt").string());
    need(reopened.MusicVolume==70&&reopened.FreeWill&&reopened.AutonomyLevel==2&&!reopened.Shadows&&!reopened.EdgeScrolling,
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
        graphicsSnapshot.capabilities[size_t(Id::lighting)]&&
        !graphicsSnapshot.capabilities[size_t(Id::interface_effects)]&&
        !graphicsSnapshot.capabilities[size_t(Id::terrain_detail)]&&
        !graphicsSnapshot.capabilities[size_t(Id::character_detail)],
        "shadow/object-light offered; unbound terrain and missing character consumer remain unavailable");
    world.State.OriginalRoofLighting=true;
    const auto roofSnapshot=CaptureDesktopOptions(graphics,false,{},&world);
    need(roofSnapshot.capabilities[size_t(Id::character_detail)],"source-bound roof exposes original material detail command");
    const auto originalDetail=graphics.CharacterDetail;
    need(ApplyDesktopOption(graphics,{Id::character_detail,1},roofSnapshot.capabilities)&&
         graphics.CharacterDetail==1&&graphics.CharacterDetailRevision==1,"original radio handler emits medium apply");
    need(ApplyDesktopOption(graphics,{Id::character_detail,1},roofSnapshot.capabilities)&&
         graphics.CharacterDetailRevision==2,"selected radio emits a second material apply");
    need(!ApplyDesktopOption(graphics,{Id::character_detail,3},roofSnapshot.capabilities)&&
         graphics.CharacterDetailRevision==2,"out of range radio emits no event");
    graphics.CharacterDetail=originalDetail;graphics.CharacterDetailRevision=0;
    world.State.OriginalRoofLighting=false;
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
    // The recovered terrain renderer already owns source density/pool0..2.
    // Exercise the actual options -> SafeFile -> App callback -> renderer path,
    // including a first paused frame before the terrain has drawn anything.
    using Terrain=FSO::LotView::Components::TerrainComponent;
    using TerrainType=FSO::LotView::Components::TerrainType;
    // The geometry-free avatar fixture had no floor/pool admission layers.
    // Source blades require an explicit ordinary no-floor, non-pool tile;
    // absent layers intentionally suppress blades at every quality level.
    std::vector<std::vector<FSO::LotView::Model::FloorTile>> terrainFloors(
        2,std::vector<FSO::LotView::Model::FloorTile>(32*32));
    blueprint.Floors=&terrainFloors;
    blueprint.TerrainPoolBlocked.assign(32*32,0);
    blueprint.Terrain=std::make_unique<Terrain>(st::Rectangle(0,0,32,32));
    auto& terrain=*blueprint.Terrain;
    terrain.SetGrassState(std::vector<uint8_t>(32*32,0),32);
    terrain.SetHeights(std::vector<float>(32*32,0),32);
    const std::vector<uint8_t> sourceHeights(32*32*4,0),sourceGrass(32*32,127);
    terrain.SetOriginalSource(sourceHeights,sourceGrass,32);
    auto terrainOptions=CaptureDesktopOptions(graphics,false,{},&world);
    need(terrainOptions.capabilities[size_t(Id::terrain_detail)],"source-bound terrain detail is editable before first draw");
    need(!terrainOptions.capabilities[size_t(Id::interface_effects)]&&
         !terrainOptions.capabilities[size_t(Id::auto_snapshot)]&&
         !terrainOptions.capabilities[size_t(Id::live_pip)]&&
         !terrainOptions.capabilities[size_t(Id::export_html)],"terrain wiring exposed unsupported services");
    terrain.LightType=TerrainType::SAND;
    need(!CaptureDesktopOptions(graphics,false,{},&world).capabilities[size_t(Id::terrain_detail)],"non-GRASS fallback falsely offered original source detail");
    terrain.LightType=TerrainType::GRASS;
    terrain.SetOriginalSource({},sourceGrass,32);
    need(!CaptureDesktopOptions(graphics,false,{},&world).capabilities[size_t(Id::terrain_detail)],"missing authored source falsely offered detail");
    terrain.SetOriginalSource(sourceHeights,sourceGrass,32);
    graphics.HandheldGraphics=true;
    need(!CaptureDesktopOptions(graphics,false,{},&world).capabilities[size_t(Id::terrain_detail)],"portable low preset exposed desktop quality editing");
    graphics.HandheldGraphics=false;
    terrainOptions=CaptureDesktopOptions(graphics,false,{},&world);
    const auto storedTerrainDetail=graphics.TerrainDetail;
    need(!ApplyDesktopOption(graphics,{Id::terrain_detail,-1},terrainOptions.capabilities)&&
         !ApplyDesktopOption(graphics,{Id::terrain_detail,3},terrainOptions.capabilities)&&
         graphics.TerrainDetail==storedTerrainDetail,"invalid terrain quality changed preference");
    terrain.RegenTerrain(world.State,&blueprint);world.State.TerrainPeriod=0;
    std::array<std::vector<uint32_t>,3> terrainPixels;
    const int levels[]={0,1,2,0,2};
    for(int detail:levels){
        need(ApplyDesktopOption(graphics,{Id::terrain_detail,detail},terrainOptions.capabilities),"original terrain detail command did not apply");
        app._Front->SaveSettings();
        need(world.State.TerrainDetail==detail,"settings-change callback did not reach real terrain state");
        front::FrontSettings reopenedDetail;reopenedDetail.Load((dir/"sims_options.txt").string());
        need(reopenedDetail.TerrainDetail==detail,"terrain detail did not survive preference reopen");
        FSO::LotView::Framebuffer target(320,240);target.Clear(FSO::LotView::Framebuffer::Pack(255,255,255,255));
        terrain.DrawInto(world.State,target);
        need(terrain.LastStats.Pixels>0,"terrain fixture did not expose any actual raster pixels");
        if(terrainPixels[size_t(detail)].empty())terrainPixels[size_t(detail)]=target.Color;
        else need(target.Color==terrainPixels[size_t(detail)],"terrain on-off-on did not restore exact source raster");
    }
    need(terrainPixels[0]!=terrainPixels[1]&&terrainPixels[1]!=terrainPixels[2]&&terrainPixels[0]!=terrainPixels[2],
         "three authored terrain settings did not produce distinct actual pixels");
    graphics.TerrainDetail=storedTerrainDetail;app._Front->SaveSettings();
    need(world.State.TerrainDetail==storedTerrainDetail,"terrain control did not restore fixture preference");
    // Continue existing avatar/light controls with the same World/blueprint.
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
    // Renderer light toggle must retain daylight and simulation lighting data.
    blueprint.Light.resize(3);blueprint.OutsideColor=st::Color(100,120,140,255);
    blueprint.Light[1].OutsideLight=100;blueprint.Light[2].OutsideLight=25;
    blueprint.Light[2].AmbientLight=50;blueprint.Light[2].RoomScore=77;
    need(ApplyDesktopOption(graphics,{Id::lighting,1},graphicsSnapshot.capabilities),"light control accepts enable");
    app._Front->SaveSettings();need(world.State.ObjectLighting,"settings callback enables real renderer light");
    blueprint.GenerateRoomLights(world.State.ObjectLighting);const auto bright=blueprint.RoomColors[2];const auto outside=blueprint.RoomColors[1];
    need(ApplyDesktopOption(graphics,{Id::lighting,0},graphicsSnapshot.capabilities),"light control accepts disable");
    app._Front->SaveSettings();need(!world.State.ObjectLighting,"settings callback disables real renderer light");
    blueprint.GenerateRoomLights(world.State.ObjectLighting);
    need(blueprint.RoomColors[2].R<bright.R&&blueprint.RoomColors[2].R>0&&blueprint.RoomColors[1].PackedValue()==outside.PackedValue(),
        "object-light off darkens interiors while preserving ambient/daylight");
    need(blueprint.Light[2].AmbientLight==50&&blueprint.Light[2].OutsideLight==25&&blueprint.Light[2].RoomScore==77,
        "graphics option does not rewrite simulation contribution or room score");
    need(std::any_of(blueprint.Damage.begin(),blueprint.Damage.end(),[](const auto& d){return d.Type==FSO::LotView::Model::BlueprintDamageType::LIGHTING_CHANGED;}),
        "settings changes invalidate retained lighting composite");
    need(ApplyDesktopOption(graphics,{Id::quick_tips,0},graphicsSnapshot.capabilities)&&!graphics.QuickTips&&
        ApplyDesktopOption(graphics,{Id::quick_tips,1},graphicsSnapshot.capabilities)&&graphics.QuickTips,"Quick Tips preference reaches the F5 help policy");
    odh_host::UiHover f5Help({251});odh_host::UiHoverTarget caption{17,{0,0,100,20},"original F5 help"};
    odh_host::UiHoverInput hoverInput;hoverInput.pointer=std::array<int32_t,2>{10,10};
    need(!f5Help.update(0,caption,hoverInput)&&!f5Help.update(250,caption,hoverInput)&&bool(f5Help.update(251,caption,hoverInput)),
        "F5 automatic help requires elapsed strictly greater than250ms");
    need(ApplyDesktopOption(graphics,{Id::quick_tips,0},graphicsSnapshot.capabilities),"Quick Tips can disable automatic help");
    hoverInput.enabled=graphics.QuickTips;
    need(!f5Help.update(252,caption,hoverInput)&&!f5Help.pending(),"disabled automatic help clears its pending/visible state");
    need(ApplyDesktopOption(graphics,{Id::quick_tips,1},graphicsSnapshot.capabilities),"Quick Tips can re-enable automatic help");
    hoverInput.enabled=graphics.QuickTips;
    need(!f5Help.update(1000,caption,hoverInput)&&!f5Help.update(1250,caption,hoverInput)&&bool(f5Help.update(1251,caption,hoverInput)),
        "re-enabled help starts a fresh dwell rather than reviving an old timer");
    const auto focusSnapshot=CaptureDesktopOptions(graphics,false,{},&world,true);
    need(focusSnapshot.capabilities[size_t(Id::background_simulation)]&&focusSnapshot.state.values[size_t(Id::background_simulation)]==0,
        "only a focus-aware host exposes background simulation");
    app._Cfg.HostFocusEvents=true;app.Speed=3;app.SetHostFocused(false);
    need(app.HostPausesSimulation()&&app.Speed==3,"focus pause retains selected speed");
    need(ApplyDesktopOption(graphics,{Id::background_simulation,1},focusSnapshot.capabilities)&&!app.HostPausesSimulation(),
        "enabled background simulation permits advancement while unfocused");
    app.Speed=0;app.SetHostFocused(true);need(!app.HostPausesSimulation()&&app.Speed==0,"returning focus never unpauses an explicit pause");
    need(ApplyDesktopOption(graphics,{Id::background_simulation,0},focusSnapshot.capabilities),"background option disables");
    app.SetHostFocused(false);need(app.HostPausesSimulation()&&app.Speed==0,"losing focus preserves explicit pause");
    app.SetHostFocused(true);need(!app.HostPausesSimulation()&&app.Speed==0,"pause remains after repeated focus transition");
    app._Front->SaveSettings();front::FrontSettings extraReopened;extraReopened.Load((dir/"sims_options.txt").string());
    need(!extraReopened.BackgroundSimulation&&extraReopened.QuickTips&&!extraReopened.Lighting,"new effects preferences save and reopen");
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
    need(controller.Tap("pcopt_4")&&!controller.Page(),"clicking the selected category closes its contents");
    need(controller.Tap("pcopt_3")&&controller.Page()==odh_host::OriginalSettingsPage::graphics&&controller.Tap("pcopt_5")&&controller.Page()==odh_host::OriginalSettingsPage::play,"switching categories selects the new page");
    controller.Reset();need(!controller.Page(),"mode exit/reentry clears page as observed in original");
    for(unsigned i=0;i<3;++i){need(controller.Tap("pcopt_"+std::to_string(i)),"original action dispatch");
        const auto expected=i==0?front::GameOptions::Action::Save:i==1?front::GameOptions::Action::Neighbourhood:front::GameOptions::Action::Quit;
        need(options.TakeAction()==expected,"actual existing AppFlow pending-action contract preserved");}
    need(!controller.Tap("pcopt_99")&&!controller.Tap("gopt_lighting"),"stale/unknown controls cannot bypass capabilities");
    std::cout<<"PASS native Options producer/persistence/capabilities/action lifecycle\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
