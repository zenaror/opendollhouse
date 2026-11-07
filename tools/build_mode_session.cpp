// SPDX-License-Identifier: MPL-2.0
#include "build_mode_session.hpp"
#include "odh/projection.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
namespace odh_host {
namespace {
using namespace odh;
constexpr std::size_t page_size=16;
std::size_t kind_index(ArchitectureEditMaterialKind kind){
    switch(kind){case ArchitectureEditMaterialKind::wall_style:return 0;case ArchitectureEditMaterialKind::wallpaper:return 1;case ArchitectureEditMaterialKind::floor:return 2;}
    throw FormatError("unknown build material kind");
}
std::string fold(std::string key){for(auto& c:key){if(c=='/')c='\\';else if(c>='A'&&c<='Z')c=char(c-'A'+'a');}return key;}
bool valid(const BuildModePoint&p){return std::isfinite(p.x)&&std::isfinite(p.y)&&p.x>=0&&p.y>=0&&p.x<=32766&&p.y<=32766&&p.level>=1&&p.level<=2;}
// Math.Round default in the pinned UI tools is midpoint-to-even, independent
// of the process floating-point rounding mode.
int even_round(double value){const double lo=std::floor(value),f=value-lo;return static_cast<int>(f<.5?lo:f>.5?lo+1:std::fmod(lo,2)==0?lo:lo+1);}
bool same(const ArchitectureEditCommand&a,const ArchitectureEditCommand&b){return a.type==b.type&&a.x==b.x&&a.y==b.y&&a.x2==b.x2&&a.y2==b.y2&&a.level==b.level&&a.pattern==b.pattern&&a.style==b.style;}
void bitmap(RasterFrame& frame,const BitmapImage& image,const BuyUiRect& box,unsigned state=0,bool strip=false){
    const auto span=strip?image.width/4:image.width;
    if(!span||!image.height||image.pixels.size()!=std::size_t(image.width)*image.height||box.width<1||box.height<1||state>3||(strip&&image.width%4))throw FormatError("invalid build artwork dimensions");
    for(int y=std::max(0,box.y);y<std::min<int>(frame.height,box.y+box.height);++y)for(int x=std::max(0,box.x);x<std::min<int>(frame.width,box.x+box.width);++x){
        const auto sx=(strip?state*span:0)+std::uint64_t(x-box.x)*span/box.width,sy=std::uint64_t(y-box.y)*image.height/box.height;
        const auto& pixel=image.pixels[sy*image.width+sx];if(!pixel[3]||(pixel[0]==255&&pixel[1]==0&&pixel[2]==255))continue;frame.pixels[std::size_t(y)*frame.width+x]=pixel;
    }
}
void line(RasterFrame& f,std::array<float,3>a,std::array<float,3>b){
    // Bounded overlay only: never writes depth, pick IDs, World or geometry.
    const auto dx=b[0]-a[0],dy=b[1]-a[1];
    const auto count=static_cast<unsigned>(std::min(4096.0f,
        std::ceil(std::max(std::abs(dx),std::abs(dy)))))+1;
    for(unsigned n=0;n<=count;++n){const auto t=float(n)/count;const auto x=std::lround(a[0]+dx*t),y=std::lround(a[1]+dy*t);if(x>=0&&y>=0&&x<f.width&&y<f.height)f.pixels[std::size_t(y)*f.width+x]={235,220,80,255};}
}
}
BuildModeLayout build_mode_layout(std::uint32_t width,std::uint32_t height){
    const auto buy=buy_mode_layout(width,height);const auto common=original_control_panel_layout(width,height);BuildModeLayout l;
    l.panel=buy.panel;l.toggle=common.modes[2];l.cancel=buy.cancel;l.previous=buy.previous;l.next=buy.next;l.materials=buy.items;
    const auto s=common.scale,base=float(height)-600*s;const auto rect=[&](float x,float y,float w,float h){return BuyUiRect{int(std::lround(x*s)),int(std::lround(base+y*s)),int(std::lround(w*s)),int(std::lround(h*s))};};
    for(unsigned n=0;n<3;++n)l.categories[n]=rect(230+45*n,514,38,38);
    // Original tools use Shift/Ctrl for rectangles, removal and flood fills.
    // Do not expose a second debug toolbar over the lot.
    l.cancel={};
    return l;
}
BuildModeSession::BuildModeSession(const std::filesystem::path& game,std::shared_ptr<const OriginalControlPanelArt> panel,
    std::vector<BuildModeMaterial> materials,BuildModeText text,BuildModeIconLoader loader)
    :panel_(std::move(panel)),info_view_(game),materials_(std::move(materials)),text_(std::move(text)),icon_loader_(std::move(loader)){
    if(!panel_||materials_.size()>8192)throw FormatError("build catalog/panel budget invalid");
    std::set<std::pair<ArchitectureEditMaterialKind,std::uint16_t>> seen;std::size_t bytes=0;
    for(const auto&m:materials_){const auto index=kind_index(m.kind);if(m.name.size()>4096||m.description.size()>16384||m.name.size()+m.description.size()>4*1048576-bytes||!seen.emplace(m.kind,m.id).second)throw FormatError("ambiguous or oversized build catalog");bytes+=m.name.size()+m.description.size();if(!selected_[index])selected_[index]=m.id;}
    // Original tool/category art is read once, outside frame/input callbacks.
    const auto root=std::filesystem::canonical(game),path=std::filesystem::canonical(root/"UIGraphics/UIGraphics.far");
    const auto relative=path.lexically_relative(root);if(relative.empty()||relative.is_absolute()||std::any_of(relative.begin(),relative.end(),[](const auto&p){return p=="..";}))throw FormatError("build artwork outside game root");
    std::ifstream stream(path,std::ios::binary);if(!stream)throw FormatError("build artwork unavailable");View archive(stream,std::filesystem::file_size(path));Limits limits;limits.far_entries=16384;limits.far_name_bytes=1048576;
    const auto entries=far1_index(archive,limits);const char* keys[]={"cpanel\\buttons\\toolbtnwall.bmp","cpanel\\buttons\\toolbtnwallpaper.bmp","cpanel\\buttons\\toolbtnfloor.bmp"};
    for(unsigned n=0;n<3;++n){const FarEntry* selected=nullptr;for(const auto&e:entries)if(fold(e.name)==keys[n]){if(selected)throw FormatError("ambiguous original build category artwork");selected=&e;}
        if(!selected)throw FormatError("original build category artwork missing");
        BitmapLimits budget;budget.width=1024;budget.height=256;budget.pixels=65536;category_art_[n]=read_bmp(archive.subview(selected->offset,selected->length),budget);
        if(category_art_[n].width<4||category_art_[n].width%4)throw FormatError("invalid original build button strip");}
}
ArchitectureEditMaterialKind BuildModeSession::material_kind()const{
    switch(tool_){case ArchitectureEditType::wall_line:case ArchitectureEditType::wall_delete:case ArchitectureEditType::wall_rect:return ArchitectureEditMaterialKind::wall_style;
    case ArchitectureEditType::pattern_dot:case ArchitectureEditType::pattern_fill:return ArchitectureEditMaterialKind::wallpaper;
    default:return ArchitectureEditMaterialKind::floor;}
}
const BuildModeMaterial* BuildModeSession::selected_material()const{const auto kind=material_kind();const auto id=selected_[kind_index(kind)];if(!id)return nullptr;for(const auto&m:materials_)if(m.kind==kind&&m.id==*id)return &m;return nullptr;}
BuildModePresentationState BuildModeSession::presentation_state()const{return {tool_,selected_,page_};}
bool BuildModeSession::restore(const BuildModePresentationState& state){
    if(pending_||drawing_||static_cast<unsigned>(state.tool)>6)return false;
    std::array<std::size_t,3> counts{};
    std::array<bool,3> found{};
    for(const auto& m:materials_){const auto index=kind_index(m.kind);++counts[index];if(state.selected_material_ids[index]==m.id)found[index]=true;}
    for(std::size_t n=0;n<counts.size();++n)
        if(counts[n]?(!state.selected_material_ids[n]||!found[n]):state.selected_material_ids[n].has_value())return false;
    const auto type=static_cast<unsigned>(state.tool);
    const auto index=type<=2?0:type<=4?1:2;
    const auto last_page=counts[index]?(counts[index]-1)/page_size:0;
    if(state.page>last_page)return false;
    const auto old=presentation_state();
    tool_=state.tool;selected_=state.selected_material_ids;page_=state.page;
    // Page preparation commits its caches only after every asset is validated.
    // Preserve the previous selection as well if a loader fails.
    try{if(active_)prepare_page();}catch(...){tool_=old.tool;selected_=old.selected_material_ids;page_=old.page;throw;}
    info_open_=false;info_scroll_.reset();origin_.reset();preview_.clear();diagnostic_.clear();
    return true;
}
std::optional<CatalogInfoContent> BuildModeSession::catalog_info()const{
    if(!active_||!info_open_)return {};
    const auto* material=selected_material();if(!material)return {};
    const auto icon=icons_.find({material->kind,material->id});
    return CatalogInfoContent{material->name,material->description,
        "§"+std::to_string(material->price),{},icon==icons_.end()?nullptr:&icon->second};
}
void BuildModeSession::prepare_page(){
    std::vector<const BuildModeMaterial*> all;for(const auto&m:materials_)if(m.kind==material_kind())all.push_back(&m);
    page_=std::min(page_,all.empty()?std::size_t{0}:(all.size()-1)/page_size);
    std::vector<const BuildModeMaterial*> next;for(std::size_t n=page_*page_size;n<std::min(all.size(),(page_+1)*page_size);++n)next.push_back(all[n]);
    if(next==page_items_)return;
    std::map<std::pair<ArchitectureEditMaterialKind,std::uint16_t>,BitmapImage> icons;std::size_t pixels=0;
    for(const auto*m:next){const auto key=std::make_pair(m->kind,m->id);const auto old=icons_.find(key);std::optional<BitmapImage> image;if(old!=icons_.end())image=old->second;else if(icon_loader_)image=icon_loader_(m->kind,m->id);
        if(image){if(!image->width||image->width>256||!image->height||image->height>256||image->pixels.size()!=std::size_t(image->width)*image->height||image->pixels.size()>1048576-pixels)throw FormatError("build page artwork budget exceeded");pixels+=image->pixels.size();icons.emplace(key,std::move(*image));}}
    page_items_=std::move(next);icons_=std::move(icons);
}
void BuildModeSession::open(){if(pending_)return;active_=true;info_open_=false;info_scroll_.reset();prepare_page();}
void BuildModeSession::abort_gesture(){
    if(pending_)return;
    drawing_=false;origin_.reset();preview_.clear();info_open_=false;info_scroll_.reset();
}
void BuildModeSession::select_tool(ArchitectureEditType type){
    if(pending_)return;
    if(static_cast<unsigned>(type)>6)throw FormatError("unknown build tool");
    info_open_=false;info_scroll_.reset();tool_=type;drawing_=false;origin_.reset();preview_.clear();page_=0;diagnostic_.clear();if(active_)prepare_page();
}
bool BuildModeSession::select_material(ArchitectureEditMaterialKind kind,std::uint16_t id){if(pending_)return false;const auto index=kind_index(kind);for(const auto&m:materials_)if(m.kind==kind&&m.id==id){selected_[index]=id;info_scroll_.reset();preview_.clear();drawing_=false;origin_.reset();diagnostic_.clear();return true;}return false;}
void BuildModeSession::cancel(){if(pending_)return;if(info_open_){info_open_=false;info_scroll_.reset();return;}if(drawing_||!preview_.empty()){drawing_=false;origin_.reset();preview_.clear();diagnostic_.clear();}else active_=false;}
void BuildModeSession::reject(std::string message){if(!pending_)return;pending_=false;drawing_=false;origin_.reset();if(message.size()>4096)message.resize(4096);diagnostic_=std::move(message);}
bool BuildModeSession::panel_input(const BuildModeInput&i,const BuildModeLayout& l){
    if(!i.press)return false;
    if(l.toggle.contains(i.x,i.y)){cancel();return true;}
    if(l.cancel.contains(i.x,i.y)){cancel();return true;}
    for(unsigned n=0;n<7;++n)if(l.tools[n].contains(i.x,i.y)){select_tool(static_cast<ArchitectureEditType>(n));return true;}
    for(unsigned n=0;n<3;++n)if(l.categories[n].contains(i.x,i.y)){select_tool(n==0?ArchitectureEditType::wall_line:n==1?ArchitectureEditType::pattern_dot:ArchitectureEditType::floor_rect);return true;}
    if(!l.panel.contains(i.x,i.y))return false;
    if(l.previous.contains(i.x,i.y)){info_open_=false;info_scroll_.reset();if(page_)--page_;prepare_page();return true;}
    if(l.next.contains(i.x,i.y)){info_open_=false;info_scroll_.reset();++page_;prepare_page();return true;}
    for(std::size_t n=0;n<std::min(page_items_.size(),l.materials.size());++n)if(l.materials[n].contains(i.x,i.y)){info_scroll_.reset();info_open_=select_material(page_items_[n]->kind,page_items_[n]->id);return true;}
    return true;
}
void BuildModeSession::update_preview(const BuildModePoint& p,const BuildModeInput&i,std::uint8_t rotation){
    if(!origin_||!valid(p)||p.level!=origin_->level){preview_.clear();return;}
    auto type=tool_;const bool walls=type==ArchitectureEditType::wall_line||type==ArchitectureEditType::wall_delete||type==ArchitectureEditType::wall_rect;
    if(walls){if(i.shift)type=ArchitectureEditType::wall_rect;else if(i.control)type=ArchitectureEditType::wall_delete;}
    else if(type==ArchitectureEditType::pattern_dot&&i.shift)type=ArchitectureEditType::pattern_fill;
    else if(type==ArchitectureEditType::floor_rect&&i.shift)type=ArchitectureEditType::floor_fill;
    const auto* material=selected_material();if(!material&&type!=ArchitectureEditType::wall_delete){preview_.clear();return;}
    ArchitectureEditCommand c;c.type=type;c.level=p.level;
    if(walls){c.x=even_round(origin_->x);c.y=even_round(origin_->y);const auto x=even_round(p.x),y=even_round(p.y);
        if(type==ArchitectureEditType::wall_rect){c.x2=std::abs(x-c.x);c.y2=std::abs(y-c.y);c.x=std::min(c.x,x);c.y=std::min(c.y,y);if(!c.x2&&!c.y2){preview_.clear();return;}}
        else{const auto dx=x-c.x,dy=y-c.y;c.x2=even_round(std::hypot(double(dx),double(dy)));if(!c.x2){preview_.clear();return;}c.y2=(even_round(std::atan2(double(dy),double(dx))/(3.14159265358979323846/4))+8)%8;}
        c.style=material?material->id:0;c.pattern=material?material->default_pattern:0;preview_={c};return;
    }
    c.x=static_cast<int>(p.x);c.y=static_cast<int>(p.y);c.pattern=i.control?0:material->id;
    const auto fx=p.x-c.x,fy=p.y-c.y;
    if(type==ArchitectureEditType::floor_rect){const auto sx=static_cast<int>(origin_->x),sy=static_cast<int>(origin_->y);c.x2=std::abs(c.x-sx);c.y2=std::abs(c.y-sy);c.x=std::min(c.x,sx);c.y=std::min(c.y,sy);c.style=fx-fy>0?(fx+fy>1?2:1):(fx+fy>1?3:0);}
    if(type==ArchitectureEditType::pattern_dot){
        switch(rotation){case 0:c.x2=fx-fy>0?1:0;c.y2=fx-fy>0?0:1;break;case 1:c.x2=fx+fy>1?3:0;c.y2=fx+fy>1?0:3;break;case 2:c.x2=fx-fy>0?2:3;c.y2=fx-fy>0?3:2;break;default:c.x2=fx+fy>1?2:1;c.y2=fx+fy>1?1:2;break;}
        if(!preview_.empty()&&preview_.front().type!=type)preview_.clear();
        if(std::none_of(preview_.begin(),preview_.end(),[&](const auto&old){return same(old,c);})){if(preview_.size()==4096){preview_.clear();drawing_=false;origin_.reset();throw FormatError("wallpaper gesture exceeds source command budget");}preview_.push_back(c);}
    }else preview_={c};
}
bool BuildModeSession::scroll_info(CatalogInfoScrollInput input,const WorldProjection& camera,int x,int y){
    const auto info=catalog_info();if(!info)return false;
    return info_scroll_.consume(input,info_scroll_.prepare(camera.width,camera.height,*info,text_.info_measure),x,y);
}
std::optional<std::vector<ArchitectureEditCommand>> BuildModeSession::handle(const BuildModeInput&i,const WorldProjection& camera,const BuildModeServices&s){
    if(camera.rotation>3||camera.zoom<1||camera.zoom>3)throw FormatError("invalid build camera");
    const auto l=build_mode_layout(camera.width,camera.height);if(!active_){if(i.toggle||(i.press&&l.toggle.contains(i.x,i.y)))open();return std::nullopt;}
    if(pending_)return std::nullopt;
    if(i.toggle){info_open_=false;info_scroll_.reset();drawing_=false;origin_.reset();preview_.clear();active_=false;return std::nullopt;}
    if(i.escape||i.cancel){cancel();return std::nullopt;}
    // The original consultation closes when the pointer leaves the catalog.
    // Until then, its visible pixels own a direct click instead of starting a
    // building gesture behind the popup.
    const auto info=catalog_info();
    const bool in_info=info&&info_scroll_.prepare(camera.width,camera.height,*info,text_.info_measure).panel.contains(i.x,i.y);
    if(i.motion&&!l.panel.contains(i.x,i.y)&&!in_info){info_open_=false;info_scroll_.reset();}
    if((i.press||i.release||i.motion)&&in_info)return std::nullopt;
    if(panel_input(i,l)){
        if(const auto content=catalog_info())(void)info_scroll_.prepare(camera.width,camera.height,*content,text_.info_measure);
        return std::nullopt;
    }
    const auto common=original_control_panel_layout(camera.width,camera.height);
    bool panel=l.panel.contains(i.x,i.y)||common.left_patch.contains(i.x,i.y)||l.cancel.contains(i.x,i.y);
    for(const auto& box:l.tools)panel=panel||box.contains(i.x,i.y);
    if(i.press&&!panel){const auto p=s.ground_pick?s.ground_pick(i.x,i.y):std::nullopt;if(p&&valid(*p)){origin_=p;drawing_=true;preview_.clear();diagnostic_.clear();update_preview(*p,i,camera.rotation);}}
    else if(drawing_&&(i.motion||i.release)&&!panel){const auto p=s.ground_pick?s.ground_pick(i.x,i.y):std::nullopt;if(p&&valid(*p))update_preview(*p,i,camera.rotation);else preview_.clear();}
    if(i.release&&drawing_){drawing_=false;origin_.reset();if(panel){preview_.clear();return std::nullopt;}if(!preview_.empty()){pending_=true;return preview_;}}
    return std::nullopt;
}
void BuildModeSession::draw(RasterFrame& frame,const WorldProjection& camera,BuyModeTextPainter text,OriginalPanelState state)const{
    if(!active_)return;
    if(frame.width!=camera.width||frame.height!=camera.height||frame.pixels.size()!=std::size_t(frame.width)*frame.height)throw FormatError("build frame/camera mismatch");
    const auto project=[&](double x,double y,int level){return project_tile(camera,{float(x),float(y),float((level-1)*2.95)});};
    const int dx[]={1,1,0,-1,-1,-1,0,1},dy[]={0,1,1,1,0,-1,-1,-1};
    // Amber intent outline only. Actual candidate geometry/price/permission is
    // supplied by the architecture service, never guessed by presentation.
    for(const auto& c:preview_){auto a=project(c.x,c.y,c.level);
        if(c.type==ArchitectureEditType::wall_line||c.type==ArchitectureEditType::wall_delete)line(frame,a,project(c.x+dx[c.y2]*c.x2,c.y+dy[c.y2]*c.x2,c.level));
        else if(c.type==ArchitectureEditType::wall_rect||c.type==ArchitectureEditType::floor_rect){const auto extra=c.type==ArchitectureEditType::floor_rect?1:0;auto b=project(c.x+c.x2+extra,c.y,c.level),d=project(c.x,c.y+c.y2+extra,c.level),e=project(c.x+c.x2+extra,c.y+c.y2+extra,c.level);line(frame,a,b);line(frame,b,e);line(frame,e,d);line(frame,d,a);}
        else{a=project(c.x+.5,c.y+.5,c.level);auto b=a;a[0]-=5;b[0]+=5;line(frame,a,b);a=b;a[0]-=5;a[1]-=5;b=a;b[1]+=10;line(frame,a,b);}}
    state.paused=true;panel_->draw(frame,OriginalPanelMode::build,state);const auto l=build_mode_layout(frame.width,frame.height);
    for(unsigned n=0;n<3;++n)bitmap(frame,category_art_[n],l.categories[n],kind_index(material_kind())==n?1:0,true);
    for(std::size_t n=0;n<page_items_.size();++n){
        const auto* material=page_items_[n];const auto icon=icons_.find({material->kind,material->id});
        const auto& slot=l.materials[n];
        if(icon!=icons_.end()){
            const auto& image=icon->second;
            const auto scale=std::min({1.0,double(slot.width-4)/image.width,double(slot.height-4)/image.height});
            const int width=std::max(1,int(image.width*scale)),height=std::max(1,int(image.height*scale));
            bitmap(frame,image,{slot.x+(slot.width-width)/2,slot.y+(slot.height-height)/2,width,height});
        }else if(text)text(frame,material->name,slot,selected_[kind_index(material->kind)]==material->id);
    }
    if(const auto info=catalog_info())info_view_.draw(frame,*info,text_.info_painter?text_.info_painter:text,text_.info_measure,&info_scroll_,text_.info_scroll_painter);
    if(text&&(pending_||!diagnostic_.empty()))
        text(frame,pending_?text_.pending:text_.failure,{l.panel.x+int(120*original_control_panel_layout(frame.width,frame.height).scale),l.panel.y-30,360,24},true);
}
}
