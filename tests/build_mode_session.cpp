// SPDX-License-Identifier: MPL-2.0
#include "build_mode_session.hpp"
#include <chrono>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <limits>
namespace {
using namespace odh;using namespace odh_host;
void require(bool yes,const char*why){if(!yes)throw std::runtime_error(why);}
void put(std::string&b,std::uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(char(v>>(8*i)));}
void at(std::string&b,std::size_t o,std::uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)b.at(o+i)=char(v>>(8*i));}
std::string bmp(unsigned width=8,unsigned height=2){
    const auto stride=(width*3+3)&~3u;std::string b(54+stride*height,'\0');
    at(b,0,0x4d42,2);at(b,2,static_cast<std::uint32_t>(b.size()),4);at(b,10,54,4);at(b,14,40,4);
    at(b,18,width,4);at(b,22,height,4);at(b,26,1,2);at(b,28,24,2);
    for(std::size_t i=54;i<b.size();++i)b[i]=char(30+i%180);return b;
}
std::string far(const std::vector<std::string>&keys){
    std::string b="FAR!byAZ";put(b,1,4);put(b,0,4);std::vector<std::uint32_t>offs,sizes;
    for(const auto&k:keys){
        const auto image=k=="cpanel/Backgrounds/PopupInfo.bmp"?bmp(559,127):
            k=="cpanel/Backgrounds/PopupInfoTiles.bmp"?bmp(36,36):bmp();
        offs.push_back(std::uint32_t(b.size()));sizes.push_back(std::uint32_t(image.size()));b+=image;
    }
    at(b,12,std::uint32_t(b.size()),4);put(b,std::uint32_t(keys.size()),4);
    for(std::size_t n=0;n<keys.size();++n){put(b,sizes[n],4);put(b,sizes[n],4);put(b,offs[n],4);put(b,std::uint32_t(keys[n].size()),4);b+=keys[n];}return b;
}
struct Fixture {
    std::filesystem::path root;std::shared_ptr<const OriginalControlPanelArt> panel;
    Fixture(){root=std::filesystem::temp_directory_path()/("odh-build-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root/"UIGraphics");
        std::vector<std::string>keys;for(const auto*key:{"Backgrounds/PanelBack.bmp","Backgrounds/LivePatch.bmp","Backgrounds/BuyPatch.bmp","Backgrounds/BuildPatch.bmp","CatalogUnknown.bmp","Buttons/Objects.bmp","Buttons/People.bmp","Buttons/Arch.bmp","Buttons/ScrollLeft.bmp","Buttons/ScrollRight.bmp","Catalog/BuyFSeating.bmp","Catalog/BuyFSurfaces.bmp","Catalog/BuyFAppliances.bmp","Catalog/BuyFElectronics.bmp","Catalog/BuyFPlumbing.bmp","Catalog/BuyFDecorative.bmp","Catalog/BuyFMisc.bmp","Catalog/BuyFLighting.bmp","Buttons/ToolBtnWall.bmp","Buttons/ToolBtnWallpaper.bmp","Buttons/ToolBtnFloor.bmp","Backgrounds/PopupInfo.bmp","Backgrounds/PopupInfoTiles.bmp"})keys.push_back(std::string("cpanel/")+key);
        std::ofstream out(root/"UIGraphics/UIGraphics.far",std::ios::binary);const auto bytes=far(keys);out.write(bytes.data(),std::streamsize(bytes.size()));out.close();panel=std::make_shared<const OriginalControlPanelArt>(root);}
    ~Fixture(){std::error_code ec;std::filesystem::remove_all(root,ec);}
};
std::vector<BuildModeMaterial> materials(){return {{ArchitectureEditMaterialKind::wall_style,1,"Original structural style",70},{ArchitectureEditMaterialKind::wall_style,2,"Original rail style",10,248},{ArchitectureEditMaterialKind::wallpaper,400,"Original wall pattern",9},{ArchitectureEditMaterialKind::floor,500,"Original floor",7,0,"The original floor description in the chosen language."}};}
void gestures(Fixture&f){
    unsigned picks=0,icons=0;BuildModePoint point;BuildModeServices services;services.ground_pick=[&](auto,auto)->std::optional<BuildModePoint>{++picks;return point;};
    BuildModeSession ui(f.root,f.panel,materials(),{},[&](auto,auto)->std::optional<BitmapImage>{++icons;return {};});
    const WorldProjection camera{800,600,3,0,{10,10,0}};BuildModeInput input;input.motion=true;input.x=400;input.y=300;
    require(!ui.handle(input,camera,services)&&picks==0,"closed build UI does not consume world input");ui.open();require(icons==2,"open prepares bounded first material page before initial draw");
    ui.abort_gesture();require(ui.active()&&!ui.drawing()&&!ui.pending(),"idle focus/overflow abort preserves Build mode instead of restoring live simulation");
    const auto gesture=[&](ArchitectureEditType type,BuildModePoint from,BuildModePoint to,bool control=false,bool shift=false){
        ui.select_tool(type);point=from;BuildModeInput i;i.x=400;i.y=300;i.press=true;i.control=control;i.shift=shift;require(!ui.handle(i,camera,services)&&ui.drawing(),"press records original gesture without publishing");point=to;i.press=false;i.motion=true;require(!ui.handle(i,camera,services),"motion previews source command without publication");i.motion=false;i.release=true;auto commands=ui.handle(i,camera,services);require(commands&&ui.pending(),"release emits one pending batch");const auto pending_size=ui.preview_commands().size();ui.abort_gesture();require(ui.active()&&ui.pending()&&ui.preview_commands().size()==pending_size,"focus/overflow cannot reopen or clear an already emitted pending command");require(!ui.handle(i,camera,services),"replayed release cannot duplicate pending publication");ui.reject("actual backend refusal");require(!ui.pending()&&!ui.diagnostic().empty(),"real rejection releases pending state and retains diagnostic");return *commands;
    };
    auto c=gesture(ArchitectureEditType::wall_line,{10.5,12.5,1},{13.5,12.5,1});require(c.size()==1&&c[0].x==10&&c[0].y==12&&c[0].x2==4&&c[0].y2==0&&c[0].style==1,"wall line uses source midpoint-even endpoints and length/direction grammar");
    require(ui.select_material(ArchitectureEditMaterialKind::wall_style,2),"select exact source rail descriptor");
    c=gesture(ArchitectureEditType::wall_line,{10,10,1},{12,10,1});require(c[0].style==2&&c[0].pattern==248,"structural style carries provider-authenticated default pattern rather than invented blank wallpaper");
    require(ui.select_material(ArchitectureEditMaterialKind::wall_style,1),"restore source solid style selection");
    c=gesture(ArchitectureEditType::wall_delete,{14,14,1},{10,10,1});require(c[0].type==ArchitectureEditType::wall_delete&&c[0].x2==6&&c[0].y2==5,"wall erase keeps original Euclidean rounded diagonal length");
    c=gesture(ArchitectureEditType::wall_rect,{14,16,1},{10,12,1});require(c[0].x==10&&c[0].y==12&&c[0].x2==4&&c[0].y2==4,"wall rectangle normalizes reverse drag to origin and extents");
    c=gesture(ArchitectureEditType::pattern_dot,{10.8,11.2,1},{10.2,11.8,1});require(c.size()==2&&c[0].x2==1&&c[0].y2==0&&c[1].x2==0&&c[1].y2==1&&c[0].pattern==400,"wallpaper drag retains distinct source side candidates and suppresses repeat at release");
    for(unsigned rotation=0;rotation<4;++rotation){auto turned=camera;turned.rotation=std::uint8_t(rotation);ui.select_tool(ArchitectureEditType::pattern_dot);point={10.8,11.3,1};BuildModeInput i;i.press=true;i.x=400;i.y=300;ui.handle(i,turned,services);i.press=false;i.release=true;const auto batch=ui.handle(i,turned,services);const int direction[]={1,3,2,2},alternate[]={0,0,3,1};require(batch&&batch->size()==1&&batch->front().x2==direction[rotation]&&batch->front().y2==alternate[rotation],"wallpaper source quadrant uses actual camera rotation for both candidate faces");ui.reject("fixture command not published");}
    c=gesture(ArchitectureEditType::pattern_fill,{10.8,11.2,1},{12.2,13.8,1});require(c.size()==1&&c[0].x==12&&c[0].y==13&&c[0].pattern==400,"wallpaper fill retains current seed rather than pretending to know flood geometry");
    c=gesture(ArchitectureEditType::floor_rect,{10.2,12.3,1},{12.8,14.7,1});require(c.size()==1&&c[0].x2==2&&c[0].y2==2&&c[0].style==2&&c[0].pattern==500,"floor rectangle preserves inclusive extents and diagonal half selection");
    c=gesture(ArchitectureEditType::floor_fill,{10.2,12.3,1},{12.8,14.7,1},true);require(c[0].type==ArchitectureEditType::floor_fill&&c[0].pattern==0,"control floor fill requests source removal without invented material zero descriptor");
    c=gesture(ArchitectureEditType::wall_line,{10,10,1},{12,12,1},false,true);require(c[0].type==ArchitectureEditType::wall_rect,"shift wall drag selects source rectangle branch");
    c=gesture(ArchitectureEditType::wall_line,{10,10,1},{12,10,1},true);require(c[0].type==ArchitectureEditType::wall_delete,"control wall drag selects source deletion branch");
    c=gesture(ArchitectureEditType::pattern_dot,{10,10,1},{12,10,1},false,true);require(c[0].type==ArchitectureEditType::pattern_fill,"shift wallpaper uses source fill branch");
    c=gesture(ArchitectureEditType::floor_rect,{10,10,1},{12,10,1},false,true);require(c[0].type==ArchitectureEditType::floor_fill,"shift floor uses source fill branch");
    ui.select_tool(ArchitectureEditType::wall_line);point={10,10,1};input={};input.press=true;input.x=400;input.y=300;ui.handle(input,camera,services);input={};input.escape=true;require(!ui.handle(input,camera,services)&&ui.active()&&!ui.drawing()&&ui.preview_commands().empty(),"cancel held drag discards all intent without closing mode");input={};input.release=true;input.x=400;input.y=300;require(!ui.handle(input,camera,services),"release after cancellation never manufactures a command");
    input={};input.press=true;input.x=400;input.y=300;ui.handle(input,camera,services);point={12,10,1};input.press=false;input.motion=true;ui.handle(input,camera,services);require(!ui.preview_commands().empty(),"actual drag has an unpublished preview before focus loss");ui.abort_gesture();require(ui.active()&&!ui.drawing()&&ui.preview_commands().empty(),"focus loss clears the whole drag while keeping Build mode active");input.motion=false;input.release=true;require(!ui.handle(input,camera,services)&&!ui.pending(),"release after focus/overflow abort cannot publish the abandoned gesture");
    const auto before=picks;input={};input.press=true;input.x=180;input.y=590;require(!ui.handle(input,camera,services)&&picks==before,"opaque empty build panel does not click through into lot");
    ui.select_tool(ArchitectureEditType::floor_rect);point={10,10,1};input={};input.press=true;input.x=400;input.y=300;ui.handle(input,camera,services);point={12,12,2};input.press=false;input.release=true;require(!ui.handle(input,camera,services)&&ui.preview_commands().empty(),"floor changes invalidate a drag instead of editing a different storey");
    point={std::numeric_limits<double>::quiet_NaN(),10,1};input={};input.press=true;input.x=400;input.y=300;require(!ui.handle(input,camera,services)&&!ui.drawing(),"nonfinite ground facts cannot enter command state");
    ui.select_tool(ArchitectureEditType::pattern_dot);point={0.8,10.2,1};input={};input.press=true;input.x=400;input.y=300;ui.handle(input,camera,services);input.press=false;input.motion=true;
    bool oversized=false;try{for(unsigned n=1;n<=4096;++n){point.x=n+.8;ui.handle(input,camera,services);}}catch(const FormatError&){oversized=true;}
    require(oversized&&!ui.drawing()&&ui.preview_commands().empty()&&!ui.pending(),"oversized wallpaper gesture discards the whole unpublished batch rather than retaining a partial operation");input.motion=false;input.release=true;require(!ui.handle(input,camera,services),"release cannot publish a prefix after an oversized gesture");
    RasterFrame frame;frame.width=800;frame.height=600;frame.pixels.assign(800*600,{1,2,3,255});frame.depth.assign(800*600,std::numeric_limits<float>::infinity());std::vector<std::string>painted;const auto old_icons=icons;ui.draw(frame,camera,[&](auto&,auto s,const auto&,bool){painted.emplace_back(s);});require(!painted.empty()&&icons==old_icons,"original build presentation has no resource loading or price computation during draw");
    bool failed=false;auto invalid=materials();invalid.push_back(invalid.front());try{BuildModeSession duplicate(f.root,f.panel,invalid);}catch(const FormatError&){failed=true;}require(failed,"duplicate prepared material identities cannot choose arbitrary source precedence");
}
void consultation(Fixture& f){
    unsigned picks=0,icons=0;
    BuildModeSession ui(f.root,f.panel,materials(),{},[&](auto,auto)->std::optional<BitmapImage>{++icons;return {};});
    BuildModeServices services;services.ground_pick=[&](auto,auto)->std::optional<BuildModePoint>{++picks;return BuildModePoint{10,10,1};};
    const WorldProjection camera{800,600,3,0,{10,10,0}};
    ui.open();require(!ui.catalog_info(),"entering Build does not invent a selected-item consultation");
    ui.select_tool(ArchitectureEditType::floor_rect);const auto l=build_mode_layout(camera.width,camera.height);
    BuildModeInput input;input.press=true;input.x=l.materials[0].x+1;input.y=l.materials[0].y+1;
    require(!ui.handle(input,camera,services)&&picks==0&&!ui.drawing()&&!ui.pending(),"consulting material never picks ground or emits construction");
    auto info=ui.catalog_info();require(info&&info->name=="Original floor"&&info->description=="The original floor description in the chosen language."&&info->price=="§7", "consultation uses prepared material name, description and actual price");
    RasterFrame frame;frame.width=camera.width;frame.height=camera.height;frame.pixels.assign(std::size_t(frame.width)*frame.height,{1,2,3,255});frame.depth.assign(frame.pixels.size(),123.f);frame.owner_ids.assign(frame.pixels.size(),42);
    const auto old_icons=icons;std::vector<std::string> painted;ui.draw(frame,camera,[&](auto&,auto text,const auto&,bool){painted.emplace_back(text);});
    require(icons==old_icons&&picks==0&&std::find(painted.begin(),painted.end(),info->description)!=painted.end(),"description is drawn without resource loading or world queries");
    require(std::all_of(frame.owner_ids.begin(),frame.owner_ids.end(),[](auto x){return x==42;})&&std::all_of(frame.depth.begin(),frame.depth.end(),[](auto x){return x==123.f;}),"catalog popup retains picking and depth buffers");
    const auto popup=catalog_info_layout(camera.width,camera.height,*info);
    input={};input.press=true;input.x=popup.panel.x+5;input.y=popup.panel.y+5;
    require(!ui.handle(input,camera,services)&&picks==0&&!ui.drawing(),"click on visible consultation cannot build behind it");
    input={};input.escape=true;require(!ui.handle(input,camera,services)&&ui.active()&&!ui.catalog_info(),"Escape dismisses consultation without closing Build or spending money");
    input={};input.motion=true;input.x=l.materials[0].x+1;input.y=l.materials[0].y+1;ui.handle(input,camera,services);require(!ui.catalog_info(),"hover alone does not reopen a dismissed consultation");
    input.motion=false;input.press=true;ui.handle(input,camera,services);require(ui.catalog_info().has_value(),"another click reopens prepared material information");
    input={};input.motion=true;input.x=400;input.y=300;ui.handle(input,camera,services);require(!ui.catalog_info()&&ui.active()&&picks==0,"moving to world dismisses consultation without creating a gesture");
    input.motion=false;input.press=true;ui.handle(input,camera,services);input.press=false;input.release=true;
    require(ui.handle(input,camera,services).has_value()&&ui.pending(),"material remains usable for the existing real construction command after consultation");
    ui.reject("test does not publish transaction");
    input={};input.press=true;input.x=l.materials[0].x+1;input.y=l.materials[0].y+1;ui.handle(input,camera,services);ui.abort_gesture();require(ui.active()&&!ui.catalog_info(),"focus loss clears the consultation while keeping Build mode");
}
bool same_state(const BuildModePresentationState& a,const BuildModePresentationState& b){return a.tool==b.tool&&a.selected_material_ids==b.selected_material_ids&&a.page==b.page;}
std::vector<BuildModeMaterial> paged_materials(){
    std::vector<BuildModeMaterial> result;
    for(unsigned kind=0;kind<3;++kind)for(unsigned n=0;n<35;++n)
        result.push_back({static_cast<ArchitectureEditMaterialKind>(kind),std::uint16_t(kind*100+n+1),"Prepared material "+std::to_string(n),7,std::uint16_t(n+40)});
    return result;
}
void long_consultation(Fixture& f){
    auto catalog=materials();
    const auto description=std::string(u8"Descrição 日本語\n")+std::string(6000,'x')+u8"\n終端";
    for(auto& material:catalog)if(material.kind==ArchitectureEditMaterialKind::floor)material.description=description;
    unsigned measures=0,paints=0,icons=0,picks=0;int last_offset=0;
    BuildModeText text;text.info_measure=[&](std::string_view value,int){++measures;require(value==description,"build measures complete original document");return 2200;};
    text.info_painter=[](auto&,auto,const auto&,bool){};
    text.info_scroll_painter=[&](auto&,std::string_view value,const auto& clip,bool effect,int offset){
        ++paints;require(value==description&&!effect&&clip.height>0,"build clipped painting keeps complete UTF-8 description");last_offset=offset;
    };
    BuildModeSession session(f.root,f.panel,catalog,text,[&](auto,auto)->std::optional<BitmapImage>{++icons;return {};});
    WorldProjection camera{800,600,3,0,{0,0,0}};BuildModeServices services;
    services.ground_pick=[&](auto,auto)->std::optional<BuildModePoint>{++picks;return BuildModePoint{10,10,1};};
    session.open();session.select_tool(ArchitectureEditType::floor_rect);
    const auto layout=build_mode_layout(camera.width,camera.height);BuildModeInput input;input.press=true;
    input.x=layout.materials[0].x+1;input.y=layout.materials[0].y+1;session.handle(input,camera,services);
    const auto info=session.catalog_info();require(info&&info->description==description,"build selection exposes all original text");
    const auto popup=catalog_info_layout(camera.width,camera.height,*info,text.info_measure);
    const int x=popup.panel.x+3,y=popup.panel.y+3;const auto prepared_icons=icons;
    input={};input.motion=true;input.x=x;input.y=y;session.handle(input,camera,services);
    require(session.catalog_info().has_value()&&!picks,"build pointer may enter its popup without closing it or drawing terrain");
    const auto measured=measures;
    require(session.scroll_info({0,100},camera,0,0)&&session.info_scroll_offset()==popup.max_scroll,
        "build page input reaches exact end of measured document");
    require(session.scroll_info({1,0},camera,x,y)&&!session.scroll_info({1,0},camera,0,0),
        "build scroll end is consumed inside popup and wheel outside is left to camera");
    RasterFrame frame{800,600,std::vector<std::array<std::uint8_t,4>>(800*600,{0,0,0,255}),{}};
    session.draw(frame,camera);require(paints&&last_offset==popup.max_scroll&&measures==measured&&icons==prepared_icons,
        "build paints final text without reading icons or measuring on scroll/draw");
    input={};input.press=true;input.x=x;input.y=y;require(!session.handle(input,camera,services)&&!picks&&!session.drawing(),
        "build scrolled popup click never starts a hidden terrain gesture");
    input={};input.escape=true;session.handle(input,camera,services);
    require(!session.catalog_info()&&session.info_scroll_offset()==0&&!session.scroll_info({0,1},camera,x,y),"build cancel resets document viewport");
    input={};input.press=true;input.x=layout.materials[0].x+1;input.y=layout.materials[0].y+1;session.handle(input,camera,services);
    require(session.info_scroll_offset()==0,"build reopening starts at first row");
    session.scroll_info({0,100},camera,x,y);session.select_tool(ArchitectureEditType::pattern_dot);
    require(session.info_scroll_offset()==0&&!session.catalog_info(),"switching build category discards popup scroll");
}
void presentation_continuation(Fixture& f){
    const auto catalog=paged_materials();const WorldProjection camera{800,600,3,0,{10,10,0}};
    const auto layout=build_mode_layout(camera.width,camera.height);
    BuildModePoint point{10.2,12.3,1};BuildModeServices services;
    services.ground_pick=[&](auto,auto)->std::optional<BuildModePoint>{return point;};
    const auto command=[&](BuildModeSession& ui){
        BuildModeInput input;input.x=400;input.y=300;input.press=true;point={10.2,12.3,1};
        require(!ui.handle(input,camera,services)&&ui.drawing(),"continuation starts a fresh gesture");
        point={12.8,14.7,1};input.press=false;input.release=true;
        auto batch=ui.handle(input,camera,services);require(batch&&ui.pending(),"continuation emits the next command");return *batch;
    };
    // Every tool carries all three material selections, including ones outside
    // the active category. Each kind is independently varied across the matrix.
    for(unsigned tool=0;tool<7;++tool)for(unsigned varied_kind=0;varied_kind<3;++varied_kind){
        BuildModeSession source(f.root,f.panel,catalog);source.open();source.select_tool(static_cast<ArchitectureEditType>(tool));
        for(unsigned kind=0;kind<3;++kind)require(source.select_material(static_cast<ArchitectureEditMaterialKind>(kind),std::uint16_t(kind*100+18+(kind==varied_kind))),"select exact non-default materials in every category");
        BuildModeInput next;next.press=true;next.x=layout.next.x+1;next.y=layout.next.y+1;
        source.handle(next,camera,services);
        const auto before=source.presentation_state();require(before.page==1,"source captures an actual nonzero catalog page");
        const auto expected=command(source);const auto saved=source.presentation_state();
        require(same_state(saved,before),"pending command contributes no gesture or command state to presentation");
        require(!source.restore(saved)&&source.pending(),"restore cannot reopen an emitted pending command");
        BuildModeSession restored(f.root,f.panel,catalog);require(restored.restore(saved)&&!restored.active(),"new session restores presentation before opening");
        require(!restored.pending()&&!restored.drawing()&&restored.preview_commands().empty()&&restored.diagnostic().empty()&&!restored.catalog_info(),"new session carries no pending preview diagnostic or consultation");
        restored.open();require(same_state(restored.presentation_state(),saved),"open preserves restored tool all selections and page");
        const auto active_kind=tool<=2?0:tool<=4?1:2;
        require(restored.selected_material()&&restored.selected_material()->kind==static_cast<ArchitectureEditMaterialKind>(active_kind)&&restored.selected_material()->id==*saved.selected_material_ids[active_kind],"restored tool selects its own material kind");
        BuildModeInput release;release.release=true;release.x=400;release.y=300;
        require(!restored.handle(release,camera,services)&&!restored.pending(),"old release cannot duplicate a command after reload");
        const auto actual=command(restored);require(actual.size()==expected.size(),"following gesture retains source command batch size");
        for(std::size_t n=0;n<actual.size();++n){const auto&a=actual[n];const auto&b=expected[n];require(a.type==b.type&&a.x==b.x&&a.y==b.y&&a.x2==b.x2&&a.y2==b.y2&&a.level==b.level&&a.pattern==b.pattern&&a.style==b.style,"following command uses restored tool and material without reselection");}
        require(!restored.handle(release,camera,services),"new command still publishes once");
        restored.reject("fixture does not apply changes");require(restored.restore(saved)&&restored.diagnostic().empty()&&restored.preview_commands().empty(),"restoring an idle rejected session discards old transient feedback");
        next={};next.press=true;next.x=layout.materials[0].x+1;next.y=layout.materials[0].y+1;
        restored.handle(next,camera,services);require(restored.catalog_info().has_value(),"restored page prepares clickable item information");
        require(restored.catalog_info()->name=="Prepared material 16","nonzero restored page displays the correct material group");
        require(restored.restore(saved)&&!restored.catalog_info(),"restoration does not preserve an open popup");
        std::cout<<"continuation_case tool="<<tool<<" varied_kind="<<varied_kind<<" page="<<saved.page
                 <<" materials="<<*saved.selected_material_ids[0]<<','<<*saved.selected_material_ids[1]<<','<<*saved.selected_material_ids[2]<<" result=pass\n";
    }
    BuildModeSession ui(f.root,f.panel,catalog);ui.open();const auto original=ui.presentation_state();
    const auto invalid=[&](BuildModePresentationState state){require(!ui.restore(state)&&same_state(ui.presentation_state(),original),"invalid presentation restore leaves all presentation fields unchanged");};
    auto state=original;state.tool=static_cast<ArchitectureEditType>(255);invalid(state);
    state=original;state.tool=ArchitectureEditType::floor_fill;state.selected_material_ids[0]=65000;state.page=1;invalid(state);
    for(unsigned kind=0;kind<3;++kind){state=original;state.selected_material_ids[kind].reset();invalid(state);state=original;state.selected_material_ids[kind]=std::uint16_t(((kind+1)%3)*100+1);invalid(state);}
    state=original;state.page=3;invalid(state);state.page=std::numeric_limits<std::size_t>::max();invalid(state);
    // Exact page boundaries, including no items and exactly full final pages.
    for(const unsigned count:{0,16,17,32,33}){
        auto subset=catalog;subset.erase(std::remove_if(subset.begin(),subset.end(),[&](const auto&m){return m.kind==ArchitectureEditMaterialKind::floor&&m.id>200+count;}),subset.end());
        BuildModeSession bounded(f.root,f.panel,subset);auto boundary=bounded.presentation_state();boundary.tool=ArchitectureEditType::floor_fill;
        boundary.page=count?(count-1)/16:0;require(bounded.restore(boundary),"last actual page validates at catalog boundary");bounded.open();require(same_state(bounded.presentation_state(),boundary),"open does not alter validated boundary page");
        ++boundary.page;const auto previous=bounded.presentation_state();require(!bounded.restore(boundary)&&same_state(previous,bounded.presentation_state()),"page immediately beyond category boundary fails atomically");
        if(!count){boundary=previous;boundary.selected_material_ids[2]=201;require(!bounded.restore(boundary),"empty category cannot restore a material identity");require(!bounded.selected_material(),"empty active category remains explicitly unselected");}
    }
    state=original;state.tool=ArchitectureEditType::floor_rect;state.page=1;state.selected_material_ids[2]=234;
    auto stale=catalog;stale.erase(std::remove_if(stale.begin(),stale.end(),[](const auto&m){return m.kind==ArchitectureEditMaterialKind::floor&&m.id==234;}),stale.end());
    BuildModeSession changed(f.root,f.panel,stale);const auto defaults=changed.presentation_state();require(!changed.restore(state)&&same_state(changed.presentation_state(),defaults),"removed exact ID cannot silently select another material in rebuilt catalog");
    BuildModeInput press;press.x=400;press.y=300;press.press=true;point={10,10,1};ui.handle(press,camera,services);
    require(ui.drawing()&&!ui.restore(original)&&ui.drawing(),"restore cannot replace an unpublished active gesture");ui.abort_gesture();
    bool fail_assets=false;BuildModeSession assets(f.root,f.panel,catalog,{},[&](auto,std::uint16_t id)->std::optional<BitmapImage>{if(fail_assets&&id==17)throw FormatError("fixture failed icon");return {};});
    assets.open();const auto asset_original=assets.presentation_state();state=asset_original;state.page=1;fail_assets=true;
    bool threw=false;try{assets.restore(state);}catch(const FormatError&){threw=true;}
    require(threw&&same_state(assets.presentation_state(),asset_original),"asset preparation failure also rolls back presentation atomically");
}
}
int main(){try{Fixture f;gestures(f);consultation(f);long_consultation(f);presentation_continuation(f);std::cout<<"build mode consumer fixtures passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
