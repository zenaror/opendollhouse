// SPDX-License-Identifier: MPL-2.0
// FreeSO reference revision7e76c1960252ddd033d8937d5edb47d326f5b69c.
#include "odh/architecture_edit.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

// Behavioral port of pinned FreeSO tso.simantics VMArchitecture.RunCommands
// and Utils/VMArchitectureTools (wall/surface methods). See THIRD_PARTY_NOTICES.
namespace odh { namespace {
struct Failure { ArchitectureEditStatus status; std::string text; };
std::int32_t bits(std::uint32_t v) { std::int32_t r;std::memcpy(&r,&v,4);return r; }
std::int32_t add(std::int32_t a,std::int32_t b){return bits(std::uint32_t(a)+std::uint32_t(b));}
std::int32_t neg(std::int32_t a){return bits(0U-std::uint32_t(a));}
std::int32_t mul(std::int32_t a,std::int32_t b){return bits(std::uint32_t(a)*std::uint32_t(b));}
struct Point{int x,y;};
constexpr Point start[8]={{0,0},{0,0},{0,0},{-1,0},{-1,0},{-1,-1},{0,-1},{0,-1}};
constexpr Point step[8]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
constexpr Point sub[4]={{0,-1},{-1,0},{0,-1},{-1,0}};
constexpr std::uint8_t mainseg[8]={2,32,1,16,2,32,1,16},subseg[4]={8,4,8,4};
bool thick_left(const SavedWallTile&w){return (w.segments&1)&&(w.top_left_style==1||w.top_left_style==255);}
bool thick_right(const SavedWallTile&w){return (w.segments&2)&&(w.top_right_style==1||w.top_right_style==255);}
class Edit {
    const ArchitectureEditServices& s; const ArchitectureEditLimits& lim;std::size_t work=0;
public:
    PreparedArchitecture a;
    std::uint64_t writes=0;
    explicit Edit(const PreparedArchitecture&v,const ArchitectureEditServices&ss,const ArchitectureEditLimits&ll):s(ss),lim(ll),a(v){}
    void spend(){if(++work>lim.work){throw Failure{ArchitectureEditStatus::budget,"architecture edit work budget exceeded"};}}
    template<class F,class...Args> bool query(const F&f,const char*name,Args&&...args){
        spend();if(!f){throw Failure{ArchitectureEditStatus::unsupported,std::string("missing architecture authority: ")+name};}
        auto r=f(std::forward<Args>(args)...);if(!r){throw Failure{ArchitectureEditStatus::unsupported,std::string("unknown architecture authority: ")+name};}return *r;
    }
    bool inside(int x,int y,int l){return x>=0&&y>=0&&x<int(a.width)&&y<int(a.height)&&l>=1&&l<=2;}
    bool clip(int x,int y,int l){if(!inside(x,y,l))return false;return query(s.in_clip,"OutsideClip",x,y,std::int8_t(l));}
    std::size_t at(int x,int y)const{return std::size_t(y)*a.width+std::size_t(x);}
    SavedWallTile wall(int x,int y,int l)const{return x<0||y<0||x>=int(a.width)||y>=int(a.height)?SavedWallTile{}:a.levels[l-1].walls[at(x,y)];}
    void setwall(int x,int y,int l,const SavedWallTile&w){++writes;a.levels[l-1].walls[at(x,y)]=w;}
    ArchitectureEditMaterial material(ArchitectureEditMaterialKind kind,std::uint16_t id){
        spend();if(!s.material){throw Failure{ArchitectureEditStatus::unsupported,"missing architecture material catalog"};}auto r=s.material(kind,id);
        if(r.status==ArchitectureEditLookup::unknown){throw Failure{ArchitectureEditStatus::unsupported,"unknown architecture material identity/price"};}return r;
    }
    int price(bool floor,std::uint16_t id){if(floor&&id==1){return 0;}auto r=material(floor?ArchitectureEditMaterialKind::floor:ArchitectureEditMaterialKind::wallpaper,id);return r.status==ArchitectureEditLookup::available?r.price:0;}
    bool support(int x,int y,int l){return l==1||query(s.supported,"Supported",x,y,std::int8_t(l-1));}
    bool setfloor(int x,int y,int l,std::uint16_t p,bool force=false){
        if(!force){
            if(p>65533&&l>1&&query(s.below_outside,"below room outside",x,y,std::int8_t(l-1)))return false;
            if(p>0&&!support(x,y,l))return false;
            if(!query(s.floor_valid,"CheckFloorValid",a,x,y,std::int8_t(l),p))return false;
        }
        ++writes;a.levels[l-1].floors[at(x,y)]=p;return true;
    }
    bool verify(int x,int y,int len,int dir,int l,bool erase){
        x+=start[dir].x;y+=start[dir].y;bool diag=dir%2;
        for(int i=0;i<len;i++,x+=step[dir].x,y+=step[dir].y){
            spend();if(!clip(x,y,l))return false;auto w=wall(x,y,l);
            if(erase)w.segments&=std::uint8_t(~mainseg[dir]);
            else{if((w.segments&48)||(diag&&w.segments))return false;w.segments|=mainseg[dir];}
            if(!query(s.wall_valid,"CheckWallValid",a,x,y,std::int8_t(l),w))return false;
            if(!diag){int xx=x+sub[dir/2].x,yy=y+sub[dir/2].y;
                if(!clip(xx,yy,l))return false;auto n=wall(xx,yy,l);
                if(!erase){if(!support(x,y,l)&&!support(xx,yy,l))return false;if(n.segments&48){return false;}n.segments|=subseg[dir/2];}
                else n.segments&=std::uint8_t(~subseg[dir/2]);
                if(!query(s.wall_valid,"CheckWallValid",a,xx,yy,std::int8_t(l),n))return false;
            }else if(!erase&&!support(x,y,l))return false;
        }return true;
    }
    int draw(int x,int y,int len,int dir,int l,std::uint16_t pattern,std::uint16_t style,bool erase,bool force=false){
        if(!force&&!verify(x,y,len,dir,l,erase))return 0;
        int total=0;x+=start[dir].x;y+=start[dir].y;bool diag=dir%2;
        for(int i=0;i<len;i++,x+=step[dir].x,y+=step[dir].y){spend();auto w=wall(x,y,l);
            if(erase){if(w.segments&mainseg[dir])++total;w.segments&=std::uint8_t(~mainseg[dir]);setwall(x,y,l,w);}
            else if(!(w.segments&(mainseg[dir]|48))&&(!diag||!w.segments)){
                w.segments|=mainseg[dir];
                if(diag){auto floor=a.levels[l-1].floors[at(x,y)];w.top_right_style=style;w.bottom_left_pattern=pattern;w.bottom_right_pattern=pattern;w.top_left_style=floor;w.top_left_pattern=floor;setfloor(x,y,l,0,true);}
                else if(mainseg[dir]==2){w.top_right_style=style;w.top_right_pattern=pattern;}
                else{w.top_left_style=style;w.top_left_pattern=pattern;}
                ++total;setwall(x,y,l,w);
                if(!diag){auto xx=x+sub[dir/2].x,yy=y+sub[dir/2].y;auto n=wall(xx,yy,l);n.segments|=subseg[dir/2];if(subseg[dir/2]==4)n.bottom_right_pattern=pattern;else n.bottom_left_pattern=pattern;setwall(xx,yy,l,n);}
            }
            if(erase&&!diag){auto xx=x+sub[dir/2].x,yy=y+sub[dir/2].y;auto n=wall(xx,yy,l);n.segments&=std::uint8_t(~subseg[dir/2]);setwall(xx,yy,l,n);}
        }return total;
    }
    struct Count{int total=0,cost=0;};
    void count(Count&c,std::uint16_t old,bool floor,bool full=false){auto p=price(floor,old);c.cost=add(c.cost,full?mul(p,2):p);c.total=add(c.total,full?2:1);}
    Count dot(int x,int y,int l,std::uint16_t p,int d,int alt){
        Count c;if(!clip(x,y,l)){c.total=-1;return c;}auto w=wall(x,y,l);std::uint16_t old=0;
        if((w.segments&16)&&w.top_right_style==1){auto&f=d<2?w.bottom_right_pattern:w.bottom_left_pattern;old=f;f=p;}
        else if((w.segments&32)&&w.top_right_style==1){auto&f=d>0&&d<3?w.bottom_left_pattern:w.bottom_right_pattern;old=f;f=p;}
        else{
            if(!(w.segments&(1<<d))){if(w.segments&(1<<alt))d=alt;else{c.total=-1;return c;}}
            std::uint16_t*f=nullptr;
            if(d==0&&thick_left(w))f=&w.top_left_pattern;
            else if(d==1&&thick_right(w))f=&w.top_right_pattern;
            else if(d==2&&thick_left(wall(x+1,y,l)))f=&w.bottom_right_pattern;
            else if(d==3&&thick_right(wall(x,y+1,l)))f=&w.bottom_left_pattern;
            if(f){old=*f;*f=p;}
        }
        setwall(x,y,l,w);if(old==p){c.total=-1;return c;}count(c,old,false);c.total=d;return c;
    }
    void spread(int x,int y,int d,int l,std::uint16_t p,bool floor,std::vector<std::uint8_t>&map,std::vector<Point>&stack,Count&c){
        spend();auto w=wall(x,y,l);auto&flags=map[at(x,y)];
        if((w.segments&16)&&(w.top_right_style==1||floor)){
            if(!floor){auto&f=d<2?w.bottom_left_pattern:w.bottom_right_pattern;if(f!=p){count(c,f,false);f=p;}}
            flags|=d<2?1:2;if(!floor)setwall(x,y,l,w);
        }else if((w.segments&32)&&(w.top_right_style==1||floor)){
            const bool side=d>0&&d<3;
            if(!floor){auto&f=side?w.bottom_right_pattern:w.bottom_left_pattern;if(f!=p){count(c,f,false);f=p;}}
            flags|=side?1:2;if(!floor)setwall(x,y,l,w);
        }else flags=3;
        stack.push_back({x,y});
    }
    Count fill(int x,int y,int l,std::uint16_t p,bool floor){
        Count c;if((floor&&p>65533)||!clip(x,y,l))return c;
        std::vector<std::uint8_t>map(a.width*a.height);std::vector<Point>stack{{x,y}};
        while(!stack.empty()){
            spend();auto item=stack.back();stack.pop_back();x=item.x;y=item.y;if(!clip(x,y,l))continue;
            auto w=wall(x,y,l);auto off=at(x,y);int px=(x+1)%a.width,mx=(x+a.width-1)%a.width,py=(y+1)%a.height,my=(y+a.height-1)%a.height;
            if(w.segments>15){
                if(!floor){continue;}
                auto flags=map[off];if(flags==3){continue;}if(w.segments&16)flags=3-flags;
                if((flags&1)&&w.top_left_pattern!=p){count(c,w.top_left_pattern,true);w.top_left_pattern=p;}
                else if((flags&2)&&w.top_left_style!=p){count(c,w.top_left_style,true);w.top_left_style=p;}
                setwall(x,y,l,w);continue;
            }
            if(floor){auto old=a.levels[l-1].floors[off];if(old!=p&&setfloor(x,y,l,p))count(c,old,true,true);}
            auto wx=wall(px,y,l),wy=wall(x,py,l);
            const Point neighbors[4]={{px,y},{mx,y},{x,py},{x,my}};
            const int indir[4]={0,2,1,3};
            const bool pass[4]={floor?!(w.segments&4):(!(wx.segments&1)||wx.top_left_style!=1),floor?!(w.segments&1):(!(w.segments&1)||w.top_left_style!=1),floor?!(w.segments&8):(!(wy.segments&2)||wy.top_right_style!=1),floor?!(w.segments&2):(!(w.segments&2)||w.top_right_style!=1)};
            std::uint16_t*faces[4]={&w.bottom_right_pattern,&w.top_left_pattern,&w.bottom_left_pattern,&w.top_right_pattern};
            const bool thick[4]={thick_left(wx),thick_left(w),thick_right(wy),thick_right(w)};
            for(int d=0;d<4;++d){auto n=neighbors[d];if(map[at(n.x,n.y)]<3&&pass[d])spread(n.x,n.y,indir[d],l,p,floor,map,stack,c);else if(!floor&&*faces[d]!=p&&thick[d]){count(c,*faces[d],false);*faces[d]=p;}}
            setwall(x,y,l,w);
        }return c;
    }
    Count floorrect(const ArchitectureEditCommand&c){
        Count result;const int l=c.level;auto cell=[&](int x,int y,bool dot){spend();auto w=wall(x,y,l);
            if(w.segments&48){
                if(dot&&c.pattern<65534){bool side=(w.segments&16)?c.style<2:(c.style<1||c.style>2);auto&f=side?w.top_left_style:w.top_left_pattern;if(f!=c.pattern){count(result,f,true);f=c.pattern;}setwall(x,y,l,w);}
                else if(!dot&&c.pattern>=65534){ // source assigns before pricing these two fields.
                    if(w.top_left_style!=c.pattern){w.top_left_style=c.pattern;count(result,w.top_left_style,true);}
                    if(w.top_left_pattern!=c.pattern){w.top_left_pattern=c.pattern;count(result,w.top_left_pattern,true);}setwall(x,y,l,w);
                }
            }else{auto old=a.levels[l-1].floors[at(x,y)];if(old!=c.pattern&&setfloor(x,y,l,c.pattern))count(result,old,true,true);}
        };
        if(c.x2==0&&c.y2==0){if(clip(c.x,c.y,l))cell(c.x,c.y,true);return result;}
        if(!s.rectangle_clip){throw Failure{ArchitectureEditStatus::unsupported,"missing FLOOR_RECT rectangular clip"};}
        auto b=*s.rectangle_clip;if(l>b.levels){return result;}
        if(b.x<0||b.y<0||b.width<0||b.height<0||std::int64_t(b.x)+b.width>a.width||std::int64_t(b.y)+b.height>a.height)throw Failure{ArchitectureEditStatus::invalid,"invalid architecture rectangular clip"};
        auto xe=std::min<std::int64_t>(b.x+b.width,std::int64_t(c.x)+c.x2+1),ye=std::min<std::int64_t>(b.y+b.height,std::int64_t(c.y)+c.y2+1);
        for(int y=std::max(b.y,c.y);y<ye;y++)for(int x=std::max(b.x,c.x);x<xe;x++)cell(x,y,false);return result;
    }
    ArchitectureEditReceipt command(const ArchitectureEditCommand&c,std::size_t index){
        ArchitectureEditReceipt r;r.command=index;
        if(c.x<INT16_MIN||c.x>INT16_MAX||c.y<INT16_MIN||c.y>INT16_MAX){throw Failure{ArchitectureEditStatus::invalid,"architecture origin exceeds source short tile domain"};}
        if(c.level<1||c.level>2){return r;}
        bool iswall=c.type==ArchitectureEditType::wall_line||c.type==ArchitectureEditType::wall_delete||c.type==ArchitectureEditType::wall_rect;
        if(iswall){
            if(c.x2<0||(c.type==ArchitectureEditType::wall_rect?c.y2<0:(c.y2<0||c.y2>7)))throw Failure{ArchitectureEditStatus::invalid,"invalid wall line/rectangle dimensions or direction"};
            if(std::int64_t(c.x2)+c.y2>2*std::int64_t(lim.work))throw Failure{ArchitectureEditStatus::budget,"wall extent exceeds work bound"};
            ArchitectureEditMaterial m;
            if(c.type!=ArchitectureEditType::wall_delete){m=material(ArchitectureEditMaterialKind::wall_style,c.style);if(m.status!=ArchitectureEditLookup::available||!m.purchasable){return r;}}
            if(c.type==ArchitectureEditType::wall_rect){
                if(std::int64_t(c.x)+c.x2>INT32_MAX||std::int64_t(c.y)+c.y2>INT32_MAX)throw Failure{ArchitectureEditStatus::invalid,"wall rectangle coordinate overflow"};
                Point points[4]={{c.x,c.y},{c.x,c.y},{c.x,c.y+c.y2},{c.x+c.x2,c.y}};int lens[4]={c.x2,c.y2,c.x2,c.y2},dirs[4]={0,2,0,2};
                for(int k=0;k<4;++k)if(!verify(points[k].x,points[k].y,lens[k],dirs[k],c.level,false)){r.rejected=true;return r;}
                for(int k=0;k<4;++k)r.changed_units+=draw(points[k].x,points[k].y,lens[k],dirs[k],c.level,c.pattern,c.style,false,true);
            }else{if(!verify(c.x,c.y,c.x2,c.y2,c.level,c.type==ArchitectureEditType::wall_delete)){r.rejected=true;return r;}r.changed_units=draw(c.x,c.y,c.x2,c.y2,c.level,c.pattern,c.style,c.type==ArchitectureEditType::wall_delete,true);}
            r.cost=mul(r.changed_units,c.type==ArchitectureEditType::wall_delete?-7:m.price);return r;
        }
        bool floor=c.type==ArchitectureEditType::floor_rect||c.type==ArchitectureEditType::floor_fill;
        auto m=material(floor?ArchitectureEditMaterialKind::floor:ArchitectureEditMaterialKind::wallpaper,c.pattern);
        if(m.status==ArchitectureEditLookup::absent&&c.pattern!=0){return r;}
        if(c.type==ArchitectureEditType::pattern_dot&&(c.x2<0||c.x2>5||c.y2<0||c.y2>5))throw Failure{ArchitectureEditStatus::invalid,"invalid wallpaper dot side"};
        Count n;
        switch(c.type){
            case ArchitectureEditType::pattern_dot:n=dot(c.x,c.y,c.level,c.pattern,c.x2,c.y2);r.changed_units=n.total<0?0:1;break;
            case ArchitectureEditType::pattern_fill:n=fill(c.x,c.y,c.level,c.pattern,false);r.changed_units=n.total;break;
            case ArchitectureEditType::floor_fill:n=fill(c.x,c.y,c.level,c.pattern,true);r.changed_units=n.total;break;
            case ArchitectureEditType::floor_rect:n=floorrect(c);r.changed_units=n.total;break;
            default:throw Failure{ArchitectureEditStatus::invalid,"unknown architecture edit command"};
        }
        if(r.changed_units>0){int refund=add(n.cost,-(n.cost/5));if(floor)r.cost=add(neg(refund/2),mul(m.price,n.total)/2);else r.cost=add(neg(refund),mul(m.price,r.changed_units));}return r;
    }
};
} // namespace
std::optional<std::vector<std::uint8_t>> regenerate_architecture_support(
    const PreparedArchitecture&a,const GeometricRoomMap&rooms,
    const std::vector<std::uint8_t>&objects,std::string*diagnostic){
    const auto fail=[&](const char*text)->std::optional<std::vector<std::uint8_t>>{
        if(diagnostic){*diagnostic=text;}return std::nullopt;
    };
    if(!a.width||!a.height||a.width>64||a.height>64||rooms.width!=a.width||rooms.height!=a.height)
        return fail("support architecture/room extent is invalid");
    const auto n=std::size_t(a.width)*a.height;
    if(objects.size()!=n||rooms.levels[0].size()!=n||a.levels[1].floors.size()!=n)
        return fail("source ObjectSupport, room or upper floor grid is absent");
    std::vector<std::uint8_t>outside(n),result(n);
    for(std::size_t i=0;i<n;++i){
        if(objects[i]>1){return fail("ObjectSupport contains an unknown value");}
        const auto room=std::uint16_t(rooms.levels[0][i]);
        if(room>=rooms.rooms.size()||rooms.rooms[room].id!=room)
            return fail("support lower room identity is unknown");
        outside[i]=rooms.rooms[room].outside;
    }
    constexpr Point neighbors[4]={{0,1},{1,0},{0,-1},{-1,0}};
    for(int y=0;y<int(a.height);++y)for(int x=0;x<int(a.width);++x){
        const auto at=std::size_t(y)*a.width+x;
        if(objects[at]||!outside[at]){result[at]=1;continue;}
        bool step1=false;
        for(auto d:neighbors){const int xx=x+d.x,yy=y+d.y;
            if(xx>=0&&yy>=0&&xx<int(a.width)&&yy<int(a.height)&&a.levels[1].floors[std::size_t(yy)*a.width+xx]!=0){step1=true;break;}
        }
        if(!step1){continue;}
        for(int dy=-2;dy<3&&!result[at];++dy)for(int dx=-2;dx<3;++dx){
            const int xx=x+dx,yy=y+dy;
            if(xx<0||yy<0||xx>=int(a.width)||yy>=int(a.height))continue;
            const auto near=std::size_t(yy)*a.width+xx;
            if(!outside[near]||(objects[near]&&dx>-2&&dx<2&&dy>-2&&dy<2)){result[at]=1;break;}
        }
    }
    if(diagnostic){diagnostic->clear();}return result;
}
ArchitectureEditResult prepare_architecture_edit(const PreparedArchitecture&a,const std::vector<ArchitectureEditCommand>&commands,const ArchitectureEditServices&s,const ArchitectureEditLimits&limits){
    ArchitectureEditResult r;
    try{
        if(a.width==0||a.height==0||a.width>limits.dimension||a.height>limits.dimension||a.width>64||a.height>64){throw Failure{ArchitectureEditStatus::invalid,"invalid architecture edit grid extent"};}
        const auto cells=std::size_t(a.width)*a.height;
        if(a.buildable.size()!=cells)throw Failure{ArchitectureEditStatus::invalid,"architecture buildable grid extent mismatch"};
        for(const auto&l:a.levels)if(l.walls.size()!=cells||l.floors.size()!=cells)throw Failure{ArchitectureEditStatus::invalid,"architecture floor/wall grid extent mismatch"};
        if(commands.size()>limits.commands)throw Failure{ArchitectureEditStatus::budget,"architecture command budget exceeded"};
        Edit edit(a,s,limits);
        for(std::size_t i=0;i<commands.size();++i){const auto before=edit.writes;auto receipt=edit.command(commands[i],i);receipt.geometry_written=edit.writes!=before;r.cost=add(r.cost,receipt.cost);r.receipts.push_back(receipt);}
        r.rooms=generate_prepared_room_map(edit.a);r.candidate=std::move(edit.a);r.status=ArchitectureEditStatus::ready;
    }catch(const Failure&f){r.status=f.status;r.diagnostic=f.text;r.candidate.reset();r.rooms.reset();r.cost=0;}
    catch(const std::exception&e){r.status=ArchitectureEditStatus::unsupported;r.diagnostic=e.what();r.candidate.reset();r.rooms.reset();r.cost=0;}
    return r;
}
} // namespace odh
