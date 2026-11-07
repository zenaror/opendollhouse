// SPDX-License-Identifier: MPL-2.0
#include "buy_mode_view.hpp"
#include "buy_mode_session.hpp"
#include "original_queue_view.hpp"
#include "odh/graphics_state.hpp"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <limits>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace {
using namespace odh;using namespace odh_host;
void require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
void put(std::string& b,std::uint32_t n,unsigned bytes){for(unsigned i=0;i<bytes;++i)b.push_back(static_cast<char>((n>>(8*i))&255));}
void at(std::string& b,std::size_t o,std::uint32_t n,unsigned bytes,bool big=false){for(unsigned i=0;i<bytes;++i)b.at(o+i)=static_cast<char>((n>>(8*(big?bytes-1-i:i)))&255);}
std::string chunk(const char* type,std::uint16_t id,const std::string& payload){std::string b(76,'\0');b.replace(0,4,type);at(b,4,static_cast<std::uint32_t>(76+payload.size()),4,true);at(b,8,id,2,true);return b+payload;}
std::string iff(const std::vector<std::string>& chunks){std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');for(const auto& c:chunks)b+=c;return b;}
std::string far(const std::vector<std::pair<std::string,std::string>>& entries){std::string b="FAR!byAZ";put(b,1,4);put(b,0,4);std::vector<std::uint32_t> offsets;
    for(const auto& entry:entries){offsets.push_back(static_cast<std::uint32_t>(b.size()));b+=entry.second;}at(b,12,static_cast<std::uint32_t>(b.size()),4);put(b,static_cast<std::uint32_t>(entries.size()),4);
    for(std::size_t n=0;n<entries.size();++n){put(b,static_cast<std::uint32_t>(entries[n].second.size()),4);put(b,static_cast<std::uint32_t>(entries[n].second.size()),4);put(b,offsets[n],4);put(b,static_cast<std::uint32_t>(entries[n].first.size()),4);b+=entries[n].first;}return b;}
std::string bmp(bool embedded=false){constexpr unsigned w=8,h=2,stride=24;std::string b(54+stride*h,'\0');at(b,0,0x4d42,2);at(b,2,embedded?0:static_cast<unsigned>(b.size()),4);at(b,10,54,4);at(b,14,40,4);at(b,18,w,4);at(b,22,h,4);at(b,26,1,2);at(b,28,24,2);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){b[54+y*stride+x*3]=static_cast<char>(x==0?255:20+x*10);b[54+y*stride+x*3+1]=static_cast<char>(x==0?0:40+x*10);b[54+y*stride+x*3+2]=static_cast<char>(x==0?255:60+x*10);}return b;}
std::string solid_bmp(unsigned width,unsigned height,const std::array<unsigned char,3>& rgb){
    const auto stride=(width*3+3)&~3U;
    std::string b(54+height*stride,'\0');at(b,0,0x4d42,2);at(b,2,static_cast<unsigned>(b.size()),4);
    at(b,10,54,4);at(b,14,40,4);at(b,18,width,4);at(b,22,height,4);at(b,26,1,2);at(b,28,24,2);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned channel=0;channel<3;++channel)
        b[54+y*stride+x*3+channel]=static_cast<char>(rgb[2-channel]);
    return b;
}
std::string tga(unsigned width,unsigned height,unsigned descriptor=8){
    std::string b(18,'\0');b[2]=2;at(b,12,width,2);at(b,14,height,2);b[16]=32;b[17]=static_cast<char>(descriptor);
    for(unsigned n=0;n<width*height;++n){b.push_back(static_cast<char>(n+1));b.push_back(static_cast<char>(n+11));
        b.push_back(static_cast<char>(n+21));b.push_back(static_cast<char>(n?128:0));}
    return b;
}
std::string circular_button(){
    constexpr unsigned side=27,width=4*side,stride=(width*3+3)&~3U;
    auto bytes=solid_bmp(width,side,{255,0,255});
    for(unsigned state=0;state<4;++state)for(int y=0;y<int(side);++y)for(int x=0;x<int(side);++x)
        if((x-13)*(x-13)+(y-13)*(y-13)<=144){
            const auto offset=54+unsigned(y)*stride+(state*side+unsigned(x))*3;
            bytes[offset]=static_cast<char>(40+state);bytes[offset+1]=static_cast<char>(150+state);bytes[offset+2]=static_cast<char>(70+state);
        }
    return bytes;
}
void write(const std::filesystem::path& path,const std::string& bytes){std::ofstream f(path,std::ios::binary);f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));require(bool(f),"write synthetic source");}
struct TemporaryDirectory {std::filesystem::path path;TemporaryDirectory(){path=std::filesystem::temp_directory_path()/("odh-buy-view-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(path/"game/UIGraphics");}~TemporaryDirectory(){std::error_code ec;std::filesystem::remove_all(path,ec);}};
void fixture(){TemporaryDirectory temporary;const auto game=temporary.path/"game";
    {
        odh::InteractionQueueSnapshot queue{};queue.actor_id=3;queue.actor_generation=7;queue.run_generation=8;
        for(const auto mode:{InteractionQueueMode::normal,InteractionQueueMode::idle,InteractionQueueMode::parent_idle,
                             InteractionQueueMode::parent_idle,InteractionQueueMode::parent_exit,InteractionQueueMode::normal}){
            InteractionActionSnapshot item{};item.uid=queue.actions.size()+1;item.mode=mode;queue.actions.push_back(item);
        }
        queue.actions[2].notify_idle=true;queue.actions[2].priority=0;
        queue.active=InteractionActiveSnapshot{1,12};queue.actions[0].priority=50;queue.actions[0].notify_idle=true;
        queue.actions[5].priority=50;queue.actions[5].notify_idle=true;
        const auto tiles=original_queue_layout(queue,100,100,0);
        require(tiles[0].cancelled&&!tiles[2].cancelled,
            "retained active cancellation uses actor priority zero; unrelated pending action keeps its original priority");
        require(!original_queue_layout(queue,100,100,50)[0].cancelled,
            "notification alone is not cancellation while the active actor priority is positive");
        require(tiles.size()==3&&tiles[0].active&&tiles[1].cancelled&&tiles[1].identity.uid==3&&tiles[2].identity.uid==6,
            "real queue ordering hides Idle, inner ParentIdle and later ParentExit");
        require(queue_tile_at(tiles,53,8)==&tiles[1]&&queue_tile_at(tiles,8,53)==&tiles[2]&&!queue_tile_at(tiles,7,8),
            "queue hit rectangles identify exact visible wrapped tile");
        const auto old=tiles[0].identity;++queue.run_generation;
        require(!(old==original_queue_layout(queue,100,100)[0].identity),"reused action UID after restart is not a previous cancel target");
    }
    std::vector<std::pair<std::string,std::string>> ui;for(const auto* key:{"Backgrounds/PanelBack.bmp","Backgrounds/LivePatch.bmp","Backgrounds/BuyPatch.bmp","Backgrounds/BuildPatch.bmp","CatalogUnknown.bmp","Buttons/Objects.bmp","Buttons/People.bmp","Buttons/Arch.bmp","Buttons/ScrollLeft.bmp","Buttons/ScrollRight.bmp","Catalog/BuyFSeating.bmp","Catalog/BuyFSurfaces.bmp","Catalog/BuyFAppliances.bmp","Catalog/BuyFElectronics.bmp","Catalog/BuyFPlumbing.bmp","Catalog/BuyFDecorative.bmp","Catalog/BuyFMisc.bmp","Catalog/BuyFLighting.bmp"})ui.push_back({std::string("cpanel/")+key,bmp()});
    ui.push_back({"cpanel/Backgrounds/PopupInfo.bmp",solid_bmp(559,127,{15,15,90})});
    ui.push_back({"cpanel/Backgrounds/PopupInfoTiles.bmp",solid_bmp(36,36,{20,20,100})});
    write(game/"UIGraphics/UIGraphics.far",far(ui));
    {
        auto entries=ui;
        entries.push_back({"CPanel/Buttons/IconFrame.bmp",solid_bmp(90,45,{0,0,80})});
        entries.push_back({"Other/QueueCancel.bmp",solid_bmp(45,45,{255,0,255})});
        write(game/"UIGraphics/UIGraphics.far",far(entries));
        OriginalQueueView queue_art(game);
        BitmapImage strip{74,37,24,true,std::vector<std::array<std::uint8_t,4>>(74*37,{240,10,10,255}),0};
        for(unsigned y=0;y<37;++y)for(unsigned x=0;x<37;++x)strip.pixels[y*74+x]={20,210,30,255};
        RasterFrame icon_frame;icon_frame.width=icon_frame.height=45;
        icon_frame.pixels.assign(45*45,{0,0,80,255});
        queue_art.draw_object_icon(icon_frame,{0,0,45,45},strip);
        require(std::count(icon_frame.pixels.begin(),icon_frame.pixels.end(),std::array<std::uint8_t,4>{20,210,30,255})==37*37&&
            std::count(icon_frame.pixels.begin(),icon_frame.pixels.end(),std::array<std::uint8_t,4>{240,10,10,255})==0,
            "37px normal object frame stays at one source pixel per screen pixel; highlighted duplicate is excluded");
        write(game/"UIGraphics/UIGraphics.far",far(ui));
    }
    const auto art_key=ui.front().first;
    const auto throws_format=[&](auto operation){try{operation();return false;}catch(const FormatError&){return true;}};
    {
        auto entries=ui;entries.push_back({"unrelated.bmp","not a bitmap"});write(game/"UIGraphics/UIGraphics.far",far(entries));
        OriginalUiArt limited(game,{art_key},{"missing.bmp","CPANEL/BACKGROUNDS/PANELBACK.BMP"},16);
        require(limited.at(art_key).width==8&&!limited.find("missing.bmp")&&!limited.find(ui.back().first),
            "only requested assets decoded; duplicate requests share their pixel budget");
        require(throws_format([&]{OriginalUiArt bad(game,{art_key,ui.back().first},{},31);}),"aggregate artwork budget enforced");
        require(throws_format([&]{OriginalUiArt bad(game,{"missing.bmp"});}),"required missing artwork rejected");
        require(throws_format([&]{OriginalUiArt bad(game,{},{"unrelated.bmp"});}),"present but damaged optional bitmap rejected");
        entries.push_back({"CPANEL/BACKGROUNDS/PANELBACK.BMP",bmp()});write(game/"UIGraphics/UIGraphics.far",far(entries));
        require(throws_format([&]{OriginalUiArt bad(game,{art_key});}),"ambiguous case-folded archive entry rejected");
        write(game/"UIGraphics/UIGraphics.far",far(ui));
    }
    {
        const std::string key="CPanel/Backgrounds/UniversalBack.TGA";
        for(const unsigned descriptor:{8U,24U,40U,56U}){
            auto entries=ui;entries.push_back({key,tga(2,2,descriptor)});write(game/"UIGraphics/UIGraphics.far",far(entries));
            const OriginalUiArt image(game,{key},{},4);const auto& decoded=image.at(key);
            const auto top=(descriptor&32)?0:1,left=(descriptor&16)?1:0;
            require(decoded.bit_count==32&&decoded.pixels[top*2+left]==std::array<std::uint8_t,4>{21,11,1,0}&&
                decoded.pixels[(1-top)*2+1-left]==std::array<std::uint8_t,4>{24,14,4,128},
                "bounded TGA loader retains true alpha and both authored storage orientations");
            require(throws_format([&]{OriginalUiArt bad(game,{key},{},3);}),"TGA aggregate pixel budget enforced");
        }
        for(unsigned invalid=0;invalid<4;++invalid){
            auto broken=tga(2,2);
            if(invalid==0)broken[2]=10;
            if(invalid==1)broken[17]=0;
            if(invalid==2)broken.pop_back();
            if(invalid==3)broken.push_back('x');
            auto entries=ui;entries.push_back({key,broken});write(game/"UIGraphics/UIGraphics.far",far(entries));
            require(throws_format([&]{OriginalUiArt bad(game,{key});}),"unsupported or truncated UI TGA rejected");
        }
        auto footer=tga(2,2);footer.append(8,'\0');footer.append("TRUEVISION-XFILE.",18);
        auto entries=ui;entries.push_back({key,footer});write(game/"UIGraphics/UIGraphics.far",far(entries));
        const OriginalUiArt image(game,{key});require(image.at(key).remaining_bytes==26,"original TGA standard footer retained");
        write(game/"UIGraphics/UIGraphics.far",far(ui));
    }
    {
        auto entries=ui;
        auto overlay=solid_bmp(213,153,{255,0,255});
        constexpr unsigned stride=(213*3+3)&~3U;
        for(unsigned y=0;y<153;++y)for(unsigned x=205;x<213;++x){
            const auto offset=54+y*stride+x*3;
            overlay[offset]=99;overlay[offset+1]=88;overlay[offset+2]=77;
        }
        for(auto& entry:entries)if(entry.first=="cpanel/Backgrounds/LivePatch.bmp")entry.second=overlay;
        for(auto& entry:entries)if(entry.first=="cpanel/Backgrounds/PanelBack.bmp")
            entry.second=solid_bmp(804,100,{9,10,80});
        auto universal=tga(2,2,40);
        for(unsigned n=0;n<4;++n)universal[18+n*4+3]=0;
        universal[18+3*4+3]=static_cast<char>(255);
        entries.push_back({"cpanel/Backgrounds/UniversalBack.TGA",universal});
        entries.push_back({"cpanel/Backgrounds/BackPatch.bmp",solid_bmp(52,100,{64,65,66})});
        write(game/"UIGraphics/UIGraphics.far",far(entries));
        const OriginalControlPanelArt layered(game);
        for(const auto size:{std::array<unsigned,2>{800,600},std::array<unsigned,2>{1024,768},std::array<unsigned,2>{400,300}}){
            const auto geometry=original_control_panel_layout(size[0],size[1]);
            for(const auto mode:{OriginalPanelMode::live,OriginalPanelMode::options}){
                RasterFrame frame;frame.width=size[0];frame.height=size[1];
                frame.pixels.resize(frame.width*frame.height,{11,22,33,255});
                frame.depth.resize(frame.pixels.size(),9);frame.owner_ids.resize(frame.pixels.size(),42);
                layered.draw(frame,mode);
                const auto x=static_cast<unsigned>(std::lround(214*geometry.scale));
                const auto y=static_cast<unsigned>(std::lround(size[1]-20*geometry.scale));
                const auto at=std::size_t(y)*frame.width+x;
                require(frame.pixels[at]==std::array<std::uint8_t,4>{77,88,99,255},
                    "live connector overlays universal base at authored seven-pixel inset across viewports and options");
                require(frame.depth[at]==9&&frame.owner_ids[at]==42,
                    "HUD connector composition preserves world depth and picking");
                const auto backing_x=static_cast<unsigned>(std::lround(200*geometry.scale));
                const auto backing_y=static_cast<unsigned>(std::lround(size[1]-95*geometry.scale));
                require(frame.pixels[std::size_t(backing_y)*frame.width+backing_x]==std::array<std::uint8_t,4>{64,65,66,255},
                    "opaque original backing fills the transparent junction behind the camera controls");
                const auto corner_x=static_cast<unsigned>(std::lround(222*geometry.scale));
                const auto corner_y=static_cast<unsigned>(geometry.base.y);
                auto corner_frame=frame;
                layered.draw(corner_frame,OriginalPanelMode::buy);
                require(corner_frame.pixels[std::size_t(corner_y)*frame.width+corner_x]==std::array<std::uint8_t,4>{9,10,80,255},
                    "panel begins after the backing at its original pixel size");
                require(frame.pixels[std::size_t(y)*frame.width+backing_x]==std::array<std::uint8_t,4>{24,14,4,255},
                    "universal foreground covers backing while transparent live overlay retains it");
            }
        }
        write(game/"UIGraphics/UIGraphics.far",far(ui));
    }
    const OriginalControlPanelArt absent_controls(game);
    const auto control_layout=original_control_panel_layout(800,600);
    require(absent_controls.hit(control_layout,control_layout.pause.x+1,control_layout.pause.y+1)==OriginalPanelAction::none,"missing optional artwork cannot leave invisible interactive controls");
    for(const auto* key:{"Backgrounds/LiveGadget.bmp","Backgrounds/CameraPatch.bmp","Buttons/Mood.bmp","Buttons/Relationship.bmp","Buttons/Job.bmp","Buttons/Personality.bmp","Buttons/Interest.bmp","Buttons/House.bmp","Buttons/RotLeft.bmp","Buttons/RotRight.bmp","Buttons/ZoomIn.bmp","Buttons/ZoomOut.bmp","Buttons/NoCut.bmp","Buttons/NoWall.bmp","Buttons/Pause.bmp","Buttons/Speed1.bmp","Buttons/Speed2.bmp","Buttons/Speed3.bmp","Buttons/Lev1.bmp","Buttons/Lev2.bmp","Buttons/DynaCut.bmp","Buttons/Options.bmp"})ui.push_back({std::string("cpanel/")+key,bmp()});
    for(auto& entry:ui)if(entry.first=="cpanel/Buttons/RotRight.bmp"||entry.first=="cpanel/Buttons/ZoomOut.bmp")entry.second=circular_button();
    ui.push_back({"cpanel/people/peopletemplate.bmp",bmp()});
    ui.push_back({"cpanel/greenbars.bmp",solid_bmp(27,25,{0,222,0})});
    ui.push_back({"cpanel/redbars.bmp",solid_bmp(27,25,{222,0,0})});
    ui.push_back({"cpanel/interestbars.bmp",solid_bmp(40,16,{0,210,80})});
    ui.push_back({"cpanel/backgrounds/interestbarsbackground.bmp",solid_bmp(40,16,{40,40,100})});
    for(const auto key:live_interest_icon_keys)ui.push_back({key,solid_bmp(75,25,{100,100,120})});
    ui.push_back({"cpanel/housesubbars.bmp",solid_bmp(40,16,{0,180,220})});
    ui.push_back({"cpanel/backgrounds/housesubbarsoff.bmp",solid_bmp(40,16,{45,45,90})});
    ui.push_back({"cpanel/jobsubbars.bmp",solid_bmp(40,11,{0,200,240})});
    ui.push_back({"cpanel/backgrounds/jobsubbarsoff.bmp",solid_bmp(40,11,{40,40,100})});
    ui.push_back({"cpanel/backgrounds/persbkg.bmp",solid_bmp(40,14,{40,40,100})});
    write(game/"UIGraphics/UIGraphics.far",far(ui));
    std::string objd(190,'\0');at(objd,0,138,4);at(objd,8,1,2);at(objd,18,4,2);at(objd,28,1234,4);at(objd,36,80,2);at(objd,78,3,2);at(objd,80,1,2);at(objd,82,2001,2);
    at(objd,164,7,2);at(objd,166,static_cast<std::uint16_t>(-3),2);at(objd,176,2,2);at(objd,178,5,2);
    // CTSS -3: one English name/description pair. Texts are authored bytes,
    // and remain selected through the shared STR language fallback helper.
    std::string ctss;put(ctss,0xfffd,2);put(ctss,2,2);ctss.push_back(1);ctss+="Original chair";ctss.push_back(0);ctss.push_back(0);ctss.push_back(1);ctss+="Original description";ctss.push_back(0);ctss.push_back(0);
    const auto resource=game/"chair.iff";const auto original=iff({chunk("OBJD",128,objd),chunk("CTSS",2001,ctss),chunk("BMP_",2001,bmp(true))});write(resource,original);
    std::istringstream stream(objd);DefinitionCandidate candidate{resource.string(),"",128,read_object_definition(View(stream,objd.size()))};ObjectDefinition person{};person.version=138;person.guid=5678;person.object_type=2;
    DefinitionCandidate character{(temporary.path/"private-unread-character.iff").string(),"",128,person};DefinitionIndex index({candidate,character});
    auto catalog=load_buy_mode_catalog(game,{candidate,character},index,14,[](auto text,auto){return std::string(text);});
    require(catalog.items.size()==1&&catalog.items.front().guid==1234&&catalog.items.front().price==80&&catalog.items.front().category==0&&catalog.items.front().name=="Original chair"&&catalog.items.front().description=="Original description","catalog metadata comes from exact source OBJD/CTSS with language fallback");
    const auto& catalog_resource=catalog.resources.at(1234);
    require(catalog_resource.ratings&&*catalog_resource.ratings==std::array<std::int16_t,7>{7,-3,0,0,0,0,2}&&
        catalog_resource.skill_flags==5,"catalog preserves authored signed ratings and skill flags separately from price");
    const auto short_resource=game/"short-chair.iff";
    write(short_resource,iff({chunk("OBJD",128,objd.substr(0,160)),chunk("CTSS",2001,ctss)}));
    auto short_candidate=candidate;short_candidate.source=short_resource.string();
    const DefinitionIndex short_index({short_candidate});
    const auto short_catalog=load_buy_mode_catalog(game,{short_candidate},short_index,14);
    require(short_catalog.items.size()==1&&!short_catalog.resources.at(1234).ratings&&
        !short_catalog.resources.at(1234).skill_flags,"truncated rating fields remain absent instead of becoming known zeroes");
    require(!std::filesystem::exists(character.source),"private character was excluded before any attempted resource I/O");
    const auto outside=temporary.path/"outside-buy.iff";write(outside,original);auto external=candidate;external.source=outside.string();DefinitionIndex external_index({external});
    bool rejected=false;try{(void)load_buy_mode_catalog(game,{external},external_index,14);}catch(const FormatError&){rejected=true;}
    require(rejected,"character filtering does not weaken the filesystem boundary for actual buy resources");
    BuyModeController controller(catalog.items,16);controller.open(665);auto art=std::make_shared<const OriginalControlPanelArt>(game);BuyModeText detail_text;
    detail_text.query_labels={"Fome: %d","Comfort: %d","Hygiene: %d","Banheiro: %d","Energy: %d","Fun: %d","Room: %d",
        "Cooking","Mechanical","Logic","Body","Creativity","Charisma","Study"};
    BuyModeView view(game,std::move(catalog),index,detail_text,art);view.prepare_page(controller);
    const auto layout=buy_mode_layout(800,600);const auto common=original_control_panel_layout(800,600);
    require(view.hover_item(controller,layout,layout.items[0].x+1,layout.items[0].y+1)&&
        view.hover_item(controller,layout,layout.items[0].x+1,layout.items[0].y+1)->price==80&&
        !view.hover_item(controller,layout,layout.items[1].x+1,layout.items[1].y+1)&&
        !view.hover_item(controller,layout,799,1)&&!view.info_visible(controller)&&!controller.held(),
        "catalog hover exposes actual item price only for occupied cells without selecting or opening description");
    require(layout.items.size()>=16&&layout.items.front().width==45&&common.base.y==500&&common.clock.y>=500&&common.budget.y>=500&&common.needs[0].x<common.needs[4].x&&common.needs[0].y==common.needs[4].y,"shared original panel keeps authored catalog frames, clock/funds and paired four-need columns");
    const auto desktop=original_control_panel_layout(1024,768);
    require(desktop.scale==1&&desktop.left_patch.x==0&&desktop.left_patch.y==585&&desktop.left_patch.width==220&&
        desktop.modes[0].x==10&&desktop.modes[0].y==591&&desktop.rotate_left.x==43&&desktop.rotate_left.y==712&&
        desktop.zoom_in.x==26&&desktop.zoom_in.y==690&&desktop.panel_background.x==220&&desktop.panel_background.y==668,
        "native desktop positions match measured original screenshot without enlarging UI with viewport");
    require(desktop.portraits.x==230&&desktop.portraits.y==673&&desktop.live_gadget.x==414&&
        desktop.needs[0].x==540&&desktop.needs[4].x==640&&desktop.needs[0].y+13==701&&
        desktop.needs[3].y+13==758&&desktop.needs[0].width==60,
        "occupied original screenshot anchors portraits, divider and paired five-pixel need meters");
    require(buy_mode_layout(1024,768).items.size()>layout.items.size()&&layout.cancel.width==0&&layout.rotate.width==0&&layout.sell.width==0,
        "wider desktop reveals original-size catalog cells without a provisional text toolbar over the world");
    require(common.portraits.x+common.portraits.width<=common.live_gadget.x&&common.live_gadget.x+common.live_gadget.width<=common.needs[0].x,"original live tabs divide family portraits from needs without covering either region");
    const std::array<std::pair<BuyUiRect,OriginalPanelAction>,7> controls{{
        {common.pause,OriginalPanelAction::pause},{common.rotate_left,OriginalPanelAction::rotate_camera_left},
        {common.rotate_right,OriginalPanelAction::rotate_camera_right},{common.zoom_in,OriginalPanelAction::zoom_in},
        {common.zoom_out,OriginalPanelAction::zoom_out},{common.walls_up,OriginalPanelAction::walls_up},
        {common.walls_down,OriginalPanelAction::walls_down}}};
    for(const auto& control:controls)require(art->hit(common,control.first.x+control.first.width*3/4,
        control.first.y+control.first.height*3/4)==control.second,
        "visible original pause/camera/wall control pixels map to their typed real host action");
    require(art->hit(common,common.zoom_out.x+4,common.zoom_out.y+4)==OriginalPanelAction::none&&
        art->hit(common,common.zoom_out.x+1,common.zoom_out.y+1)==OriginalPanelAction::rotate_camera_right,
        "overlapping transparent button corners neither create a zoom click nor mask an actually visible rotation pixel");
    for(const auto& tab:common.tabs)require(art->hit(common,tab.x+1,tab.y+1)==OriginalPanelAction::none,"unimplemented tabs never pretend to switch game panels");
    for(const auto& speed:common.speeds)require(art->hit(common,speed.x+1,speed.y+1)==OriginalPanelAction::none,"disabled speed controls never imply a simulation speed implementation");
    {
        const std::array<std::uint8_t,3> multipliers{{1,3,10}};
        const std::array<OriginalPanelAction,3> actions{{OriginalPanelAction::speed_normal,
            OriginalPanelAction::speed_fast,OriginalPanelAction::speed_ultra}};
        for(const auto size:{std::array<unsigned,2>{800,600},std::array<unsigned,2>{400,300},std::array<unsigned,2>{1024,768}}){
            const auto speed_layout=original_control_panel_layout(size[0],size[1]);
            for(const auto multiplier:multipliers){
                OriginalPanelState speed_state;speed_state.simulation_speed=multiplier;
                for(std::size_t i=0;i<3;++i){
                    const auto& box=speed_layout.speeds[i];const int x=box.x+box.width*3/4,y=box.y+box.height/2;
                    require(art->hit(speed_layout,x,y,speed_state)==actions[i],
                        "enabled speed hit uses loaded art and exact viewport geometry");
                    require(absent_controls.hit(speed_layout,x,y,speed_state)==OriginalPanelAction::none,
                        "speed capability cannot create an invisible button without artwork");
                    speed_state.paused=true;
                    require(art->hit(speed_layout,x,y,speed_state)==actions[i],"paused speed choice must remain available to resume");
                    speed_state.paused=false;speed_state.options_open=true;
                    require(art->hit(speed_layout,x,y,speed_state)==OriginalPanelAction::none,"options cannot leak speed requests");
                    speed_state.options_open=false;
                }
            }
            for(const auto invalid:{0,2,4,255}){
                OriginalPanelState invalid_state;invalid_state.simulation_speed=static_cast<std::uint8_t>(invalid);
                for(const auto& box:speed_layout.speeds)
                    require(art->hit(speed_layout,box.x+box.width*3/4,box.y+box.height/2,invalid_state)==OriginalPanelAction::none,
                        "invalid host multiplier cannot enable a speed control");
            }
        }
        RasterFrame speed_frame;speed_frame.width=800;speed_frame.height=600;
        speed_frame.pixels.resize(800*600,{17,19,23,255});speed_frame.depth.resize(800*600,std::numeric_limits<float>::infinity());
        const auto ink=[&](std::size_t i){const auto& b=common.speeds[i];
            return speed_frame.pixels[std::size_t(b.y+b.height/2)*800+b.x+b.width*3/4];};
        for(std::size_t selected=0;selected<3;++selected){
            OriginalPanelState actual;actual.simulation_speed=multipliers[selected];
            art->draw(speed_frame,OriginalPanelMode::live,actual);
            for(std::size_t i=0;i<3;++i)require(ink(i)==(i==selected?
                std::array<std::uint8_t,4>{90,70,50,255}:std::array<std::uint8_t,4>{70,50,30,255}),
                "original speed strip selects only the actual running multiplier");
            actual.paused=true;art->draw(speed_frame,OriginalPanelMode::live,actual);
            for(std::size_t i=0;i<3;++i)require(ink(i)==std::array<std::uint8_t,4>{70,50,30,255},
                "paused display does not pretend a remembered running speed is active");
            actual.paused=false;
            for(const auto mode:{OriginalPanelMode::buy,OriginalPanelMode::build,OriginalPanelMode::options}){
                art->draw(speed_frame,mode,actual);
                for(std::size_t i=0;i<3;++i)require(ink(i)==std::array<std::uint8_t,4>{130,110,90,255},
                    "editor/options rendering disables speed strips even if given remembered multiplier");
            }
        }
    }
    {
        RasterFrame speed_frame;speed_frame.width=800;speed_frame.height=600;
        speed_frame.pixels.resize(800*600,{17,19,23,255});speed_frame.depth.resize(800*600,std::numeric_limits<float>::infinity());
        const auto ink=[&](std::size_t i){const auto& b=common.speeds[i];
            return speed_frame.pixels[std::size_t(b.y+b.height/2)*800+b.x+b.width*3/4];};
        const std::array<std::uint8_t,4> normal{70,50,30,255},active{90,70,50,255},hovered{110,90,70,255},disabled{130,110,90,255};
        OriginalPanelState actual;actual.simulation_speed=1;actual.paused=true;
        actual.hovered_simulation_speed=3;art->draw(speed_frame,OriginalPanelMode::live,actual);
        require(ink(1)==hovered&&ink(0)==normal,"hovering a speed paints its hover frame without changing the actual selection");
        actual.pressed_simulation_speed=3;art->draw(speed_frame,OriginalPanelMode::live,actual);
        require(ink(1)==active,"captured speed is pressed only while pointer stays over that same speed");
        actual.hovered_simulation_speed=10;art->draw(speed_frame,OriginalPanelMode::live,actual);
        require(ink(1)==normal&&ink(2)==hovered,"dragging to a different speed releases the captured visual without selecting either speed");
        actual.hovered_simulation_speed.reset();art->draw(speed_frame,OriginalPanelMode::live,actual);
        require(ink(1)==normal,"dragging outside a captured speed removes the pressed frame");
        actual.paused=false;actual.hovered_simulation_speed=3;actual.pressed_simulation_speed.reset();
        art->draw(speed_frame,OriginalPanelMode::live,actual);
        require(ink(0)==active&&ink(1)==hovered,"actual running selection remains distinct from hovered pending choice");
        actual.pressed_simulation_speed=3;actual.simulation_speed.reset();art->draw(speed_frame,OriginalPanelMode::live,actual);
        for(std::size_t i=0;i<3;++i)require(ink(i)==disabled,"missing consumer overrides hover and captured button appearance");
        actual.simulation_speed=1;art->draw(speed_frame,OriginalPanelMode::buy,actual);
        for(std::size_t i=0;i<3;++i)require(ink(i)==disabled,"editor mode ignores stale hover and captured speed appearance");
    }
    OriginalPanelState navigation;navigation.can_level_up=true;navigation.can_level_down=false;navigation.cutaway=false;
    require(art->hit(common,common.level_up.x+common.level_up.width*3/4,common.level_up.y+common.level_up.height*3/4)==OriginalPanelAction::none&&
        art->hit(common,common.level_up.x+common.level_up.width*3/4,common.level_up.y+common.level_up.height*3/4,navigation)==OriginalPanelAction::level_up&&
        art->hit(common,common.level_down.x+common.level_down.width*3/4,common.level_down.y+common.level_down.height*3/4,navigation)==OriginalPanelAction::none&&
        art->hit(common,common.walls_cutaway.x+common.walls_cutaway.width*3/4,common.walls_cutaway.y+common.walls_cutaway.height*3/4,navigation)==OriginalPanelAction::walls_cutaway,
        "floor and cutaway controls need actual host capabilities instead of guessed state");
    OriginalPanelState secondary;
    const auto center=[](const BuyUiRect& box){return std::pair<int,int>{box.x+box.width*3/4,box.y+box.height*3/4};};
    const auto options_point=center(common.options),career_point=center(common.tabs[2]);
    require(art->hit(common,options_point.first,options_point.second,secondary)==OriginalPanelAction::none,
        "options stays non-interactive until a real host consumer is bound");
    secondary.options_enabled=true;secondary.live_tabs[2]=true;secondary.live_tab=LivePanelTab::career;
    require(art->hit(common,options_point.first,options_point.second,secondary)==OriginalPanelAction::options&&
        art->hit(common,career_point.first,career_point.second,secondary)==OriginalPanelAction::live_career,
        "loaded original buttons emit options/career actions only for explicit capabilities");
    secondary.live_tabs.fill(false);
    require(art->hit(common,career_point.first,career_point.second,secondary)==OriginalPanelAction::none,
        "buy/build/options can disable Live hit areas rather than leaking tabs into another mode");
    RasterFrame frame;frame.width=800;frame.height=600;frame.pixels.assign(800*600,{17,19,23,255});frame.depth.assign(800*600,std::numeric_limits<float>::infinity());
    {
        LivePanelSnapshot actual;actual.has_selected_person=true;
        actual.skills={0,100,200,300,400,1000};actual.personality={700,600,300,500,200};
        LiveCareerInfo job;job.employed=true;job.title="Actual job";job.salary=155;job.performance=-30;
        actual.career=job;actual.relationships=std::vector<LiveRelationshipInfo>{};
        LivePanelView live(art);require(live.select(LivePanelTab::career,actual),"real career panel selectable");
        std::vector<std::string> captions;
        live.draw(frame,actual,[&](auto&,auto value,const auto&,bool,LiveTextAlignment){captions.emplace_back(value);});
        require(std::find(captions.begin(),captions.end(),"Actual job")!=captions.end()&&
            std::find(captions.begin(),captions.end(),"§155")!=captions.end()&&
            std::find(captions.begin(),captions.end(),"-30")!=captions.end(),
            "career paints actual title salary and signed performance without fabricating a qualitative label");
        const auto skills_layout=live_panel_layout(common);
        const auto sample=[&](const auto& box,int dx){return frame.pixels[std::size_t(box.y+2)*frame.width+box.x+dx];};
        require(sample(skills_layout.skill_bars[0],2)==std::array<std::uint8_t,4>{40,40,100,255}&&
            sample(skills_layout.skill_bars[1],2)==std::array<std::uint8_t,4>{0,200,240,255}&&
            sample(skills_layout.skill_bars[1],6)==std::array<std::uint8_t,4>{40,40,100,255}&&
            sample(skills_layout.skill_bars[5],38)==std::array<std::uint8_t,4>{0,200,240,255},
            "source raw skills0/100/1000 uncover zero/one/ten authored four-pixel segments");
        const auto measured=live_panel_layout(original_control_panel_layout(1024,768));
        require(measured.skill_bars[0].x==678&&measured.skill_bars[5].y==750&&
            measured.personality_bars[0].x==679&&measured.personality_bars[4].y==748&&
            measured.relationship_faces[0].x==557&&measured.relationship_faces[1].x==599,
            "secondary panels retain original measured columns rather than the Simitone grid");
        actual.has_selected_person=false;actual.house=LiveHouseInfo{};
        actual.house->area_square_feet=171;actual.house->bedrooms=2;actual.house->bathrooms=0;
        actual.house->lot_name="Large";actual.house->ratings={0,10,100,-5,std::nullopt};
        require(live.select(LivePanelTab::house,actual),"House data is selectable independently of Sim selection");
        const auto house_layout=live_house_layout(common);
        const auto untouched=std::array<std::uint8_t,4>{17,19,23,255};
        frame.pixels.assign(frame.pixels.size(),untouched);const auto house_depth=frame.depth;
        captions.clear();live.draw(frame,actual,[&](auto&,auto value,const auto&,bool,LiveTextAlignment){captions.emplace_back(value);});
        const auto house_sample=[&](unsigned row,int x){return sample(house_layout.rating_bars[row],x);};
        require(house_sample(0,2)==std::array<std::uint8_t,4>{45,45,90,255}&&
            house_sample(1,2)==std::array<std::uint8_t,4>{0,180,220,255}&&
            house_sample(1,6)==std::array<std::uint8_t,4>{45,45,90,255}&&
            house_sample(2,38)==std::array<std::uint8_t,4>{0,180,220,255}&&
            house_sample(3,2)==std::array<std::uint8_t,4>{45,45,90,255}&&house_sample(4,2)==untouched,
            "House zero/one/ten segments use original art while unavailable rating paints no fake zero bar");
        require(std::find(captions.begin(),captions.end(),"Sq. Ft.: 171")!=captions.end()&&
            std::find(captions.begin(),captions.end(),"Bedrooms: 2")!=captions.end()&&
            std::find(captions.begin(),captions.end(),"Bathrooms: 0")!=captions.end()&&
            std::find(captions.begin(),captions.end(),"Lot: Large")!=captions.end()&&
            std::find(captions.begin(),captions.end(),"—")!=captions.end()&&frame.depth==house_depth,
            "House captions format real counts including zero and explicitly mark unavailable ratings");
        require(actual.house->ratings[3]==-5&&!actual.house->ratings[4],"House painting never mutates raw source values");
        require(live.hover_text(common,house_layout.rating_bars[4].x+1,house_layout.rating_bars[4].y+1,actual)=="Layout\n—",
            "unavailable House meter hover cannot imply an actual zero value");
        actual.house.reset();live.reconcile(actual);require(live.tab()==LivePanelTab::needs,"removed House authority returns to a supported tab");
        actual.has_selected_person=true;
        for(int n=0;n<12;++n){LiveRelationshipInfo relation;relation.neighbor_id=static_cast<std::int16_t>(n+1);
            relation.name="Known "+std::to_string(n);relation.daily=static_cast<std::int16_t>(n-6);
            if(n%2)relation.long_term=static_cast<std::int16_t>(10*n);actual.relationships->push_back(std::move(relation));}
        require(live.select(LivePanelTab::relationships,actual),"relationships use known directional data");
        const auto rel=live_panel_layout(common);
        require(live.click(common,rel.next.x+1,rel.next.y+1,actual)&&live.relationship_page()==1,
            "relationship pages make people outside the visible viewport reachable");
        require(live.hover_text(common,rel.relationship_faces[0].x+2,rel.relationship_faces[0].y+2,actual)=="Known 5",
            "portrait hover identifies the actual neighbor on the current page");
        actual.relationships->resize(1);live.reconcile(actual);require(live.relationship_page()==0,"shrinking relationship list cannot retain an empty stale page");
        actual.interests=LiveInterestsInfo{};actual.interests->age=27;
        actual.interests->raws[0]=7;actual.interests->raws[1]=1;actual.interests->raws[2]=10;
        actual.interests->raws[3]=-1;actual.interests->raws[4]=32000;
        require(live.select(LivePanelTab::interests,actual),"actual interests are selectable");
        live.draw(frame,actual);
        const auto interest_grid=live_interest_layout(common,27,0);
        const std::array<std::uint8_t,4> interest_on{0,210,80,255},interest_off{40,40,100,255};
        require(sample(interest_grid.cells[0].bar,26)==interest_on&&sample(interest_grid.cells[0].bar,30)==interest_off&&
            sample(interest_grid.cells[1].bar,2)==interest_on&&sample(interest_grid.cells[1].bar,6)==interest_off&&
            sample(interest_grid.cells[2].bar,38)==interest_on&&sample(interest_grid.cells[3].bar,2)==interest_off&&
            sample(interest_grid.cells[4].bar,38)==interest_on,
            "native interest values7/1/10 fill seven/one/ten segments; invalid values only saturate presentation");
        require(actual.interests->raws[0]==7&&actual.interests->raws[3]==-1&&actual.interests->raws[4]==32000,
            "painting interests never changes simulation values");
        actual.has_selected_person=false;live.reconcile(actual);require(live.tab()==LivePanelTab::needs,
            "removing the active person closes a stale person panel");
    }
    art->draw(frame,OriginalPanelMode::live,{false,false});
    const auto family=original_portrait_rects(common,8),couple=original_portrait_rects(common,2);
    require(family[7].x+family[7].width<=common.live_gadget.x&&family[7].y+family[7].height<=600&&
        couple[0].width==45&&family[0].width==45&&!couple[2].contains(0,0),"all eight family portraits keep their original45px frames");
    require(common.needs_title.y+common.needs_title.height<=common.needs[0].y&&
        common.needs[3].y+common.needs[3].height<=600,"title and both four-row need columns fit inside the panel");
    art->draw_portrait(frame,couple[0],false,nullptr);
    const auto portrait_at=static_cast<std::size_t>(couple[0].y+couple[0].height/2)*800+couple[0].x+couple[0].width/2;
    const auto idle_portrait=frame.pixels[portrait_at];art->draw_portrait(frame,couple[0],true,nullptr);
    require(frame.pixels[portrait_at]!=idle_portrait,"active family portrait uses a different authored frame");
    BitmapImage padded_head{9,9,24,true,std::vector<std::array<std::uint8_t,4>>(81,{255,0,255,255}),0};
    for(unsigned y=3;y<6;++y)for(unsigned x=3;x<6;++x)padded_head.pixels[y*9+x]={220,170,120,255};
    art->draw_portrait(frame,couple[0],true,&padded_head);
    {
        auto authored_frame=frame;
        odh::BitmapImage authored{35,41,24,true,std::vector<std::array<std::uint8_t,4>>(35*41,{0,0,80,255}),0};
        authored.pixels[0]={230,11,21,255};authored.pixels.back()={19,220,31,255};
        art->draw_portrait(authored_frame,couple[0],true,&authored,true);
        const int x=couple[0].x+(couple[0].width-35)/2,y=couple[0].y+(couple[0].height-41)/2;
        require(authored_frame.pixels[std::size_t(y)*authored_frame.width+x]==authored.pixels.front()&&
            authored_frame.pixels[std::size_t(y+40)*authored_frame.width+x+34]==authored.pixels.back(),
            "authored original face keeps its exact35x41 pixels without cropping or shrinking into a procedural head");
    }
    require(frame.pixels[std::size_t(couple[0].y+8)*800+couple[0].x+8]==std::array<std::uint8_t,4>{220,170,120,255},
        "visible portrait head fills the authored face frame instead of shrinking its transparent camera padding");
    const auto gauge_pixel=[&](const auto& box,int y){return frame.pixels[static_cast<std::size_t>(box.y+y)*800+box.x+box.width/2];};
    art->draw(frame,OriginalPanelMode::live,{false,false,true,20});
    require(gauge_pixel(common.green_mood,24)==std::array<std::uint8_t,4>{0,222,0,255}&&
        gauge_pixel(common.green_mood,0)!=std::array<std::uint8_t,4>{0,222,0,255},
        "small positive mood uncovers bottom fifth of upper gauge");
    art->draw(frame,OriginalPanelMode::live,{false,false,true,-100});
    require(gauge_pixel(common.red_mood,0)==std::array<std::uint8_t,4>{222,0,0,255}&&
        gauge_pixel(common.red_mood,24)==std::array<std::uint8_t,4>{222,0,0,255},
        "full negative mood uncovers entire lower gauge");
    const auto pause_pixel=frame.pixels[(common.pause.y+5)*800+common.pause.x+10];
    require(frame.pixels[(common.tabs[1].y+11)*800+common.tabs[1].x+14][0]!=17,"actual original live gadget and tabs fill the divider between portraits and needs");
    art->draw(frame,OriginalPanelMode::live,{true,true});
    require(frame.pixels[(common.pause.y+5)*800+common.pause.x+10]!=pause_pixel,"pause display follows actual host state using the original selected button frame");
    std::vector<std::string> painted;view.draw(frame,controller,WorldProjection{800,600,3,0,{0,0,0}},[&](auto&,auto text,const auto&,bool){painted.emplace_back(text);});
    const auto category_ink=std::size_t(layout.categories[0].y+layout.categories[0].height*3/4)*800+
        layout.categories[0].x+layout.categories[0].width*3/4;
    // The fixture's first half-frame is magenta, so a fixed point near the
    // category's left edge proves nothing about painting. Check the selected
    // frame's known opaque texel. Prices belong to delayed hover and the
    // clicked description, never the catalog's permanent thumbnail row.
    require(frame.pixels[category_ink]==std::array<std::uint8_t,4>{90,70,50,255}&&
        std::find(painted.begin(),painted.end(),"§80")==painted.end()&&
        std::find(painted.begin(),painted.end(),"Cancel")==painted.end()&&
        std::find(painted.begin(),painted.end(),"Rotate")==painted.end()&&
        std::find(painted.begin(),painted.end(),"Sell")==painted.end(),
        "view renders original panel artwork without permanent per-item prices or debug toolbar labels");
    const auto& first_cell=layout.items.front();
    // The fixture's normal frame is4x2. Desktop presentation must retain
    // those pixels, not enlarge the image to the former35px price-row height.
    const auto icon_x=first_cell.x+(first_cell.width-4)/2;
    const auto icon_y=first_cell.y+(first_cell.height-2)/2;
    require(frame.pixels[std::size_t(icon_y)*800+icon_x+1]==std::array<std::uint8_t,4>{70,50,30,255}&&
        frame.pixels[std::size_t(icon_y+1)*800+icon_x+3]==std::array<std::uint8_t,4>{90,70,50,255},
        "catalog thumbnail keeps authored pixels and proportions centered inside its full cell");
    require(frame.pixels[550*800]==std::array<std::uint8_t,4>{17,19,23,255},"original magenta cutouts preserve the already-rendered scene instead of pink rectangles");
    require(view.click(controller,layout,layout.items[0].x+1,layout.items[0].y+1)&&controller.held()&&controller.held()->guid==1234&&!controller.take_command(),"catalog artwork hit selects the real resource without running a transaction");
    const auto info=view.info_content(controller);
    require(info&&info->name=="Original chair"&&info->description=="Original description"&&info->price=="§80"&&info->icon&&
        info->effects=="Fome: 7\nComfort: -3\nRoom: 2\nCooking\nLogic",
        "clicked catalog item exposes exact metadata, signed ratings and only known enabled effects");
    painted.clear();view.draw(frame,controller,WorldProjection{800,600,3,0,{0,0,0}},
        [&](auto&,auto text,const auto&,bool){painted.emplace_back(text);});
    require(std::find(painted.begin(),painted.end(),"Original description")!=painted.end()&&
        std::find(painted.begin(),painted.end(),info->effects)!=painted.end(),"original information renderer consumes the selected catalog data");
    view.pointer_motion(layout,799,1);
    require(!view.info_visible(controller)&&controller.held(),"world hover hides information while retaining placement holder");
    view.pointer_motion(layout,layout.items[0].x+1,layout.items[0].y+1);
    require(!view.info_visible(controller),"returning over a catalog icon does not reopen information by hover");
    require(view.click(controller,layout,layout.items[0].x+1,layout.items[0].y+1)&&view.info_visible(controller)&&!controller.take_command(),
        "another catalog click reopens information without placing an object");
    controller.hover(EntityPosition{24,24,1,0});const auto preview=view.prepare_preview(controller,WorldProjection{800,600,3,0,{0,0,0}},std::array<float,3>{1,1,0});
    require(!preview.sprite&&preview.diagnostic=="Selected object has no artwork for its current graphic"&&controller.phase()==BuyModePhase::holding_purchase&&!controller.take_command(),"unavailable artwork preview cannot mutate controller or manufacture a transaction");
    const auto failed_preparations=view.preview_preparations();
    const auto repeated_preview=view.prepare_preview(controller,WorldProjection{800,600,3,0,{0,0,0}},std::array<float,3>{1,1,0});
    require(view.preview_preparations()==failed_preparations&&repeated_preview.diagnostic==preview.diagnostic,"failed source sprite lookups are cached without raster allocation or repeated resource reads");
    view.prepare_preview(controller,WorldProjection{800,600,3,0,{0,0,0}},std::array<float,3>{1,1,1});
    require(view.preview_preparations()==failed_preparations+1,"actual terrain height change invalidates otherwise identical preview");
    view.prepare_preview(controller,WorldProjection{800,600,3,1,{0,0,0}},std::array<float,3>{1,1,1});
    require(view.preview_preparations()==failed_preparations+2,"camera change invalidates cached failed preview");
    require(controller.place(),"one command can be requested through the UI selection");const auto command=controller.take_command();require(command&&command->guid==1234&&command->position.x==24,"view/controller connects exact resource to the common native command adapter");
    controller.command_rejected("technical routine detail stays in logs");painted.clear();view.draw(frame,controller,WorldProjection{800,600,3,0,{0,0,0}},[&](auto&,auto text,const auto&,bool){painted.emplace_back(text);});
    require(std::find(painted.begin(),painted.end(),"technical routine detail stays in logs")==painted.end(),"product UI does not expose internal engine diagnostics");
    {
        BuyModeController native_ui({{1234,80,0,0,"Existing",{}}});native_ui.open(500);
        BuyModePreviewGroup group;group.base_index=1;group.native_head_heading_delta=2;group.native_selected_heading=0;
        group.parts={{1234,0,0,0,0,{}},{1234,0,0,0,0,{}}};
        group.parts[0].native=BuyModeNativePreviewPart{{0,0,0},2,NativeObjectGroupOffset{0,0,0}};
        group.parts[1].native=BuyModeNativePreviewPart{{1,0,0},0,NativeObjectGroupOffset{0,1,0}};
        BuyModeExistingObject selected{{42,7},1234,{24,24,1,0},0,{},group};
        require(native_ui.select_existing(selected),"native view receives actual selected anchor metadata");
        const WorldProjection native_camera{800,600,3,0,{0,0,0}};
        std::vector<EntityPosition> projected;
        const auto project=[&](const EntityPosition& position){projected.push_back(position);return free_graphics_position(EntityKind::object,position);};
        const auto first=view.preview_preparations();
        view.prepare_preview(native_ui,native_camera,std::nullopt,project);
        require(view.preview_preparations()==first+1&&projected==std::vector<EntityPosition>({{24,8,1,2},{24,24,1,0}}),
            "real preview projection consumes selected-relative native geometry and individual headings");
        projected.clear();view.prepare_preview(native_ui,native_camera,std::nullopt,project);
        require(view.preview_preparations()==first+1&&projected.empty(),"unchanged native metadata reuses prepared artwork including failures");
        native_ui.cancel();selected.preview_group->parts[0].native->authored.y=1;
        require(native_ui.select_existing(selected),"reselect actual updated authored native fields");
        view.prepare_preview(native_ui,native_camera,std::nullopt,project);
        require(view.preview_preparations()==first+2&&projected==std::vector<EntityPosition>({{24,8,1,2},{24,24,1,0}}),
            "native authored field changes invalidate preview cache despite unchanged legacy offsets");
        projected.clear();native_ui.cancel();selected.preview_group->native_head_heading_delta=4;
        require(native_ui.select_existing(selected),"reselect actual head heading bias");
        view.prepare_preview(native_ui,native_camera,std::nullopt,project);
        require(view.preview_preparations()==first+3&&projected.front()==EntityPosition{24,8,1,2},
            "native source head heading bias participates in preview cache identity");
        projected.clear();native_ui.cancel();selected.preview_group->parts[0].native->heading_delta=6;
        require(native_ui.select_existing(selected),"reselect actual per-piece heading delta");
        view.prepare_preview(native_ui,native_camera,std::nullopt,project);
        require(view.preview_preparations()==first+4&&projected.front()==EntityPosition{24,8,1,6},
            "individual body heading changes invalidate native preview artwork identity");
        projected.clear();native_ui.cancel();selected.preview_group->parts[0].native->current->x=2;
        require(native_ui.select_existing(selected),"reselect actual imported current native fields");
        view.prepare_preview(native_ui,native_camera,std::nullopt,project);
        require(view.preview_preparations()==first+5&&projected.front()==EntityPosition{56,8,1,6},
            "current native fields invalidate cache and control zero-turn geometry independently of authored fields");
        projected.clear();native_ui.cancel();selected.preview_group->native_selected_heading=2;selected.position.direction=2;
        require(native_ui.select_existing(selected),"capture new actual baseline selected heading");native_ui.rotate(-1);
        view.prepare_preview(native_ui,native_camera,std::nullopt,project);
        require(view.preview_preparations()==first+6&&projected.front()==EntityPosition{40,8,1,6},
            "baseline heading participates in cache identity and selects recomposition at otherwise identical requested pose");
    }
    // Switching category while holding discards only the intent, and does not
    // keep an invisible selection that prevents further catalog browsing.
    require(view.click(controller,layout,layout.categories[1].x+1,layout.categories[1].y+1)&&
        controller.category()==1&&!controller.held()&&!view.info_visible(controller)&&!controller.take_command(),
        "category selection clears held catalog intent without publishing a transaction");
    require(view.click(controller,layout,layout.categories[0].x+1,layout.categories[0].y+1)&&controller.category()==0,
        "category browsing remains available after cancelling the prior holder");
    require(view.click(controller,layout,layout.toggle.x+1,layout.toggle.y+1)&&!controller.active(),"original Buy mode button returns to the live HUD without world edits");
    require(!view.hover_item(controller,layout,layout.items[0].x+1,layout.items[0].y+1),
        "inactive catalog cannot expose a stale item tooltip");
    // The frontend consumer uses the same actual catalog and presentation, not
    // a second mock controller with fixture-only command publication.
    BuyModeText session_text;session_text.pickup_text.messages={"Discard %","Sell % for $","Cannot sell %"};
    session_text.pickup_text.unknown_value="Unknown refund";
    BuyModeSession session(game,{candidate,character},index,14,art,session_text);
    const WorldProjection camera{800,600,3,0,{0,0,0}};
    unsigned ground_calls=0,object_calls=0,visual_calls=0;
    BuyModeServices services;
    services.ground_pick=[&](auto,auto)->std::optional<EntityPosition>{++ground_calls;return EntityPosition{40,56,1,0};};
    services.object_pick=[&](auto,auto)->std::optional<BuyModeExistingObject>{++object_calls;BuyModeExistingObject picked;picked.identity={91,7};picked.guid=1234;picked.position={24,24,1,0};return picked;};
    services.visual_tile_position=[&](const auto&)->std::array<float,3>{++visual_calls;return {2,3,0};};
    BuyModeInput input;input.motion=true;input.x=400;input.y=300;
    require(!session.handle(input,camera,665,services)&&!session.active()&&ground_calls==0&&object_calls==0,"closed consumer does not load or pick unused previews");
    input={};input.click=true;input.x=layout.toggle.x+1;input.y=layout.toggle.y+1;
    require(!session.handle(input,camera,665,services)&&session.active()&&object_calls==0,"opening original Buy button is consumed without lot clickthrough");
    input.x=layout.items[0].x+1;input.y=layout.items[0].y+1;
    require(!session.handle(input,camera,665,services)&&session.controller()->held()&&ground_calls==0&&session.info_visible(),"real catalog hit selects and opens information without world picking");
    const auto session_info=session.info_content();
    require(session_info&&session_info->description=="Original description","frontend info survives catalog item ownership moving to the controller");
    const auto info_layout=catalog_info_layout(camera.width,camera.height,*session_info);
    input={};input.click=true;input.x=info_layout.panel.x+info_layout.panel.width/2;input.y=info_layout.panel.y+info_layout.panel.height/2;
    require(!session.handle(input,camera,665,services)&&ground_calls==0&&session.info_visible()&&session.controller()->phase()==BuyModePhase::holding_purchase,
        "click inside visible information cannot place or pick through its world backdrop");
    input={};input.motion=true;input.x=400;input.y=300;
    require(!session.handle(input,camera,665,services)&&ground_calls==1&&visual_calls==1&&!session.info_visible(),"world motion hides information and updates the real ground pose outside draw");
    const auto prepared=session.preview_preparations();const auto failed_diagnostic=session.preview_diagnostic();
    BuyModeInput idle;
    require(!session.handle(idle,camera,665,services)&&session.preview_preparations()==prepared&&session.preview_diagnostic()==failed_diagnostic,"unchanged frontend polls reuse even failed previews instead of allocating full-frame rasters");
    input.motion=false;input.click=true;
    const auto purchase=session.handle(input,camera,665,services);
    require(purchase&&purchase->kind==BuyModeCommandKind::purchase&&purchase->guid==1234&&purchase->position.x==40,"frontend emits one exact native purchase intent");
    require(!session.handle(input,camera,665,services),"repeated click cannot republish an awaiting command");
    session.reject("actual native placement refusal");input={};input.escape=true;
    require(!session.handle(input,camera,665,services)&&session.active()&&!session.controller()->held(),"actual refusal can be canceled back to catalog without a new command");
    input={};input.click=true;input.x=190;input.y=590;
    require(!session.handle(input,camera,665,services)&&object_calls==0,"opaque empty original panel cannot pick an object behind it");
    input.x=400;input.y=300;
    require(!session.handle(input,camera,665,services)&&object_calls==1&&session.controller()->held()->existing->generation==7,"world selection keeps actual identity generation");
    input={};input.rotate=1;
    require(!session.handle(input,camera,665,services)&&session.controller()->held()->direction==2,"rotate changes only held preview direction");
    input={};input.sell=true;
    const auto sale=session.handle(input,camera,665,services);
    require(sale&&sale->kind==BuyModeCommandKind::sell&&sale->target.id==91&&sale->target.generation==7,"frontend sale reuses native command identity without removing world object");
    session.reject("sale denied");input={};input.escape=true;session.handle(input,camera,665,services);session.handle(input,camera,665,services);
    require(!session.active(),"held cancel then catalog cancel restores live mode");
    unsigned info_calls=0;
    for(unsigned scenario=0;scenario<4;++scenario){
        services.existing_info={};
        if(scenario!=2)services.existing_info=[&](ObjectTradeIdentity target)->std::optional<BuyModeExistingInfo>{
            ++info_calls;BuyModeExistingInfo value;value.content.target=target;
            value.content.name="Authored placed chair";value.content.refund=25;value.content.can_sell=scenario!=1;
            if(scenario==3)++value.content.target.generation;
            return value;
        };
        input={};input.toggle=true;session.handle(input,camera,665,services);
        input={};input.click=true;input.x=400;input.y=300;session.handle(input,camera,665,services);
        auto placed=session.pickup_content();require(placed&&placed->target.id==91&&placed->target.generation==7,
            "pickup snapshot is tied to the actual selected generation");
        if(scenario<2)require(placed->refund==25&&placed->name=="Authored placed chair",
            "pickup uses explicit refund25 rather than catalog80");
        else require(!placed->refund&&!placed->can_sell&&placed->name.empty(),
            "absent or stale callback never reuses the prior placed-object price or name");
        const auto before_calls=info_calls;
        session.handle({},camera,665,services);session.handle({},camera,665,services);
        require(info_calls==before_calls,"idle UI polls never query or prepare pickup resources again");
        std::vector<std::string> pickup_painted;
        session.draw(frame,camera,[&](auto&,auto value,const auto&,bool){pickup_painted.emplace_back(value);});
        const auto expected_message=scenario==0?"Sell Authored placed chair for §25":scenario==1?
            "Cannot sell Authored placed chair":"Unknown refund";
        require(std::find(pickup_painted.begin(),pickup_painted.end(),expected_message)!=pickup_painted.end()&&
            std::find(pickup_painted.begin(),pickup_painted.end(),"§80")==pickup_painted.end()&&info_calls==before_calls,
            "real Session draws the pickup message instead of the catalog or another resource query");
        const auto picks=object_calls,grounds=ground_calls;
        input={};input.click=true;input.x=600;input.y=550;
        const auto intent=session.handle(input,camera,665,services);
        require(bool(intent)==(scenario==0)&&object_calls==picks&&ground_calls==grounds,
            "pickup panel consumes clicks; only authenticated sell intent is published");
        require(session.controller()->category()==0,"pickup panel cannot select an invisible catalog category");
        if(intent){require(intent->kind==BuyModeCommandKind::sell&&intent->target.id==91&&intent->target.generation==7,
            "pickup click preserves exact native sale identity");
            require(!session.handle(input,camera,665,services),"held sell click cannot publish twice");session.reject("test refusal");}
        input={};input.escape=true;session.handle(input,camera,665,services);session.handle(input,camera,665,services);
        require(!session.active()&&!session.pickup_content(),"cancel releases pickup presentation without a world command");
        std::cout<<"pickup_case scenario="<<scenario<<" result=pass\n";
    }
    require(info_calls==3,"each real pickup invokes its optional metadata callback exactly once");
    input={};input.toggle=true;session.handle(input,camera,665,services);
    input={};input.click=true;input.x=layout.items[0].x+1;input.y=layout.items[0].y+1;
    const auto before_image=session.info_image_preparations();session.handle(input,camera,665,services);
    require(session.info_image_preparations()==before_image+1,"catalog to existing to same catalog invalidates the image correctly");
    session.handle({},camera,665,services);
    require(session.info_image_preparations()==before_image+1,"unchanged catalog query reuses its prepared image even after failure");
    {
        std::string long_description;for(unsigned n=0;n<1200;++n)long_description+=u8"ação ";long_description+=u8"終わり";
        std::string effect_label;for(unsigned n=0;n<1500;++n)effect_label+=u8"年";effect_label+=" FINAL_EFFECT";
        unsigned measures=0,scroll_paints=0;bool saw_effect_end=false;
        BuyModeText text; text.query_labels[0]=effect_label;
        text.info_measure=[&](std::string_view value,int){++measures;return value==long_description?1600:value.empty()?0:1200;};
        text.info_painter=[](auto&,auto,const auto&,bool){};
        text.info_scroll_painter=[&](auto&,std::string_view value,const auto& clip,bool effect,int offset){
            ++scroll_paints;require(clip.width>0&&clip.height>0,"buy document paints only bounded positive viewport intersections");
            if(effect&&offset>0){require(value.find("FINAL_EFFECT")!=std::string_view::npos,"full effects remain in the scrolled document");saw_effect_end=true;}
        };
        BuyModeSession scrolling(game,{candidate,character},index,14,art,text,
            [&](std::string_view value,std::uint8_t){return value=="Original description"?long_description:std::string(value);});
        scrolling.open(665);BuyModeInput select;select.click=true;select.x=layout.items[0].x+1;select.y=layout.items[0].y+1;
        scrolling.handle(select,camera,665,services);
        const auto content=scrolling.info_content();require(content&&content->description==long_description&&content->effects.size()>4096,
            "buy consumer retains complete UTF-8 body and effects beyond former renderer limit");
        const auto bounds=catalog_info_layout(camera.width,camera.height,*content,text.info_measure);
        const auto x=bounds.panel.x+3,y=bounds.panel.y+3;const auto preparations=scrolling.info_image_preparations();
        const auto ground_before=ground_calls;BuyModeInput motion;motion.motion=true;motion.x=x;motion.y=y;
        scrolling.handle(motion,camera,665,services);require(scrolling.info_visible()&&ground_calls==ground_before,
            "moving from catalog into popup keeps consultation and blocks world hover");
        const auto measured=measures;
        require(scrolling.scroll_info({0,100},camera,0,0)&&scrolling.info_scroll_offset()==bounds.max_scroll,
            "buy PageDown reaches the exact last effects viewport even outside pointer focus");
        require(scrolling.scroll_info({1,0},camera,x,y)&&scrolling.info_scroll_offset()==bounds.max_scroll,
            "buy wheel at end remains consumed and cannot zoom the camera");
        require(!scrolling.scroll_info({1,0},camera,0,0),"buy wheel outside popup remains available to its host");
        scrolling.draw(frame,camera);require(scroll_paints&&saw_effect_end&&measures==measured&&scrolling.info_image_preparations()==preparations,
            "buy scroll paints final effect rows without remeasurement or icon preparation");
        select={};select.click=true;select.x=x;select.y=y;
        require(!scrolling.handle(select,camera,665,services)&&ground_calls==ground_before,
            "scrolled popup click cannot publish purchase or pick terrain behind it");
        select={};select.escape=true;scrolling.handle(select,camera,665,services);
        require(!scrolling.info_visible()&&scrolling.info_scroll_offset()==0&&!scrolling.scroll_info({0,1},camera,x,y),
            "buy cancel clears scroll and releases page input");
        select={};select.click=true;select.x=layout.items[0].x+1;select.y=layout.items[0].y+1;scrolling.handle(select,camera,665,services);
        require(scrolling.info_scroll_offset()==0,"buy reopening the same item starts at its first description row");
    }
    std::ifstream check(resource,std::ios::binary);require(std::string(std::istreambuf_iterator<char>(check),{})==original,"catalog/page rendering never modifies original asset bytes");
}
}
void catalog_document_metrics(){
    CatalogInfoContent content{"Name",u8"日本語 descrição", "§5",u8"Avaliações\n年"};
    unsigned measures=0;const CatalogInfoTextMeasure metrics=[&](std::string_view value,int width){
        ++measures;require(width>0,"document uses actual measured wrap width");return value==content.description?900:1300;
    };
    CatalogInfoScrollState scroll;const auto first=scroll.prepare(800,450,content,metrics);
    require(first.description_height==900&&first.effects_height==1300&&first.max_scroll==2200-first.body.height,
        "document combines exact independent description/effects extents without byte-based line estimates");
    require(scroll.consume({0,100},first,0,0)&&scroll.offset()==first.max_scroll,"both large sections fit one scrollable logical document");
    const auto cache=measures;scroll.prepare(800,450,content,metrics);require(measures==cache,"unchanged document reuses shaped metrics");
    const auto resized=scroll.prepare(1024,900,content,metrics);require(scroll.offset()<=resized.max_scroll,"resize clamps the presentation offset to exact new bounds");
    content.description=u8"Outra seleção";scroll.prepare(1024,900,content,metrics);require(scroll.offset()==0,"changed text resets shared document offset");
    bool rejected=false;try{catalog_info_layout(800,600,content,[](auto,int){return -1;});}catch(const FormatError&){rejected=true;}
    require(rejected,"invalid text metrics fail explicitly instead of inventing height");
}
void catalog_layout_profiles(){
    for(const auto size:std::array<std::array<std::uint32_t,2>,5>{{{800,600},{1024,768},{800,450},{400,240},{320,200}}}){
        const auto hud=original_control_panel_layout(size[0],size[1]);
        const auto l=catalog_info_layout(size[0],size[1],{"Name","Description","§5","Comfort: 1"});
        require(l.panel.y+l.panel.height==hud.base.y&&l.panel.x+l.panel.width==int(size[0]),
            "catalog information shares HUD anchor in desktop, wide and small viewports");
        require(l.panel.width==std::lround(559*hud.scale)&&l.panel.y>=0,
            "catalog information preserves authored scale and fits above HUD");
    }
}
int main(){try{catalog_document_metrics();catalog_layout_profiles();fixture();std::cout<<"original buy view fixtures passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
