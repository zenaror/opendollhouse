// SPDX-License-Identifier: MPL-2.0
// C++ port of FreeSO (FSO.SimAntics.Engine.Routing) VMPathLineSegment.cs and
// VMPathBezierSegment.cs at 7e76c1960252ddd033d8937d5edb47d326f5b69c;
// TSOClient/tso.world/Model/LotTilePos.cs:FromVec2 and source marshal cache
// reconstruction in Marshals/Threads/VMRoutingFrameMarshal.cs:149-151.
// Vector2 arithmetic follows MonoGame.Framework.DesktopGL 3.8.4 (the pinned
// FSO.SimAntics.csproj dependency): scalar division multiplies by reciprocal.
// Reference: github.com/MonoGame/MonoGame/blob/v3.8.4/MonoGame.Framework/Vector2.cs
// FreeSO is distributed under Mozilla Public License 2.0:
// https://www.mozilla.org/MPL/2.0/ . Original sources retained under refs/work.
// Generator control normalization retains IEEE nonfinite intermediates and
// MonoGame ToPoint's .NET 9 saturating cast (NaN -> zero). Playback still
// rejects nonfinite samples; integer overflow remains outside this domain.
#include "odh/walk_path.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace odh {
namespace {
using Point=RectRoutePoint;
using Vec=WalkVector;
struct Stop {WalkPathStatus status;};
// Explicit binary32 sequence points prohibit contraction/reassociation from
// changing the source XNA Vector2 arithmetic, including cubic arc sampling.
float add(float a,float b){volatile float result=a+b;return result;}
float sub(float a,float b){volatile float result=a-b;return result;}
float mul(float a,float b){volatile float result=a*b;return result;}
float div(float a,float b){volatile float result=a/b;return result;}
Vec plus(Vec a,Vec b){return {add(a.x,b.x),add(a.y,b.y)};}
Vec minus(Vec a,Vec b){return {sub(a.x,b.x),sub(a.y,b.y)};}
Vec times(Vec a,float n){return {mul(a.x,n),mul(a.y,n)};}
Vec over(Vec a,float n){return times(a,div(1.0f,n));}
bool finite(Vec a){return std::isfinite(a.x)&&std::isfinite(a.y);}
float length(Vec a){return static_cast<float>(std::sqrt(add(mul(a.x,a.x),mul(a.y,a.y))));}
Vec normalize(Vec a){return over(a,length(a));}
bool same(Point a,Point b){return a.x==b.x&&a.y==b.y;}
std::int32_t integer(std::int64_t x){if(x<INT32_MIN||x>INT32_MAX)throw Stop{WalkPathStatus::unsupported_arithmetic};return static_cast<std::int32_t>(x);}
Point plus(Point a,Point b){return {integer(std::int64_t(a.x)+b.x),integer(std::int64_t(a.y)+b.y)};}
Point minus(Point a,Point b){return {integer(std::int64_t(a.x)-b.x),integer(std::int64_t(a.y)-b.y)};}
Vec vec(Point a){return {static_cast<float>(a.x),static_cast<float>(a.y)};}
std::int32_t truncate(float x){if(!std::isfinite(x)||static_cast<double>(x)<INT32_MIN||static_cast<double>(x)>INT32_MAX)throw Stop{WalkPathStatus::unsupported_arithmetic};return static_cast<std::int32_t>(x);}
// Pinned FreeSO targets net9.0, MonoGame 3.8.4 Vector2.ToPoint uses
// (int)X/(int)Y. Since .NET 9 these casts saturate and map NaN to zero:
// https://learn.microsoft.com/dotnet/core/compatibility/jit/9.0/fp-to-integer
// This matters for every control transition involving zero-length or exactly
// opposing segments. Do not replace normalization with a fabricated direction.
std::int32_t control_integer(float x){
    if(std::isnan(x))return 0;
    if(static_cast<double>(x)<=INT32_MIN)return INT32_MIN;
    if(static_cast<double>(x)>=INT32_MAX)return INT32_MAX;
    return static_cast<std::int32_t>(x);
}
Point point(Vec a){return {control_integer(a.x),control_integer(a.y)};}
Point hip(Point a){return {integer(std::int64_t(a.x)*walk_path_hip),integer(std::int64_t(a.y)*walk_path_hip)};}
std::int32_t short_integer(double x){if(!std::isfinite(x)||x<INT16_MIN||x>INT16_MAX)throw Stop{WalkPathStatus::unsupported_arithmetic};return static_cast<std::int32_t>(x);}
double round_even(float x){const double v=x,lo=std::floor(v),f=v-lo;return f<0.5?lo:f>0.5?lo+1:std::fmod(std::abs(lo),2.0)==0?lo:lo+1;}
void valid(const WalkSegmentGeometry& g){
    if(g.kind!=WalkSegmentKind::line&&g.kind!=WalkSegmentKind::cubic)throw Stop{WalkPathStatus::invalid_input};
    if(g.kind==WalkSegmentKind::line&&(!same(g.b,{})||!same(g.c,{})))throw Stop{WalkPathStatus::invalid_input};
    // Source Point subtraction is signed int before conversion to Vector2/long.
    (void)minus(g.d,g.a);
}
Vec interp(const WalkSegmentGeometry& g,float t){
    const float one=sub(1,t),one2=mul(one,one),t2=mul(t,t);
    const auto component=[&](std::int32_t a,std::int32_t b,std::int32_t c,std::int32_t d){
        const auto p0=mul(mul(one2,one),static_cast<float>(a));
        const auto p1=mul(mul(mul(3,one2),t),static_cast<float>(b));
        const auto p2=mul(mul(mul(3,one),t2),static_cast<float>(c));
        const auto p3=mul(mul(t2,t),static_cast<float>(d));
        return div(add(add(add(p0,p1),p2),p3),static_cast<float>(walk_path_hip));
    };
    const Vec result{component(g.a.x,g.b.x,g.c.x,g.d.x),component(g.a.y,g.b.y,g.c.y,g.d.y)};
    if(!finite(result))throw Stop{WalkPathStatus::unsupported_arithmetic};
    return result;
}
struct Builder {
    const WalkPathLimits& limits;std::size_t work=0;
    std::vector<WalkSegmentGeometry> segments;
    void spend(){if(work>=limits.work_units)throw Stop{WalkPathStatus::budget_exhausted};++work;}
    void append(WalkSegmentGeometry value){spend();if(segments.size()>=limits.segments)throw Stop{WalkPathStatus::budget_exhausted};valid(value);segments.push_back(value);}
    static RectRouteObstacle bounds(const RectRouteLineEntry& r){
        const auto a=hip({r.rectangle.x1,r.rectangle.y1}),b=hip({r.rectangle.x2,r.rectangle.y2});
        if(a.x>b.x||a.y>b.y)throw Stop{WalkPathStatus::invalid_input};
        return {a.x,a.y,b.x,b.y};
    }
    static Point clamp(Point p,const RectRouteObstacle& r){return {std::clamp(p.x,r.x1,r.x2),std::clamp(p.y,r.y1,r.y2)};}
    void controls(std::optional<std::size_t> from,WalkSegmentGeometry& to,const RectRouteLineEntry& rfrom,const RectRouteLineEntry& rto){
        spend();const auto from_slice=bounds(rfrom),to_slice=bounds(rto);
        if(!from){
            // Source null->line is a no-op; null->cubic dereferences null.
            // Reject that malformed LineID chain rather than fabricate controls.
            if(to.kind==WalkSegmentKind::cubic)throw Stop{WalkPathStatus::invalid_input};
            return;
        }
        auto& previous=segments[*from];
        if(previous.kind==WalkSegmentKind::cubic){
            auto diff_from=vec(minus(previous.d,previous.a)),diff_to=vec(minus(to.d,to.a));
            const auto len_from=length(diff_from),len_to=length(diff_to);
            if(to.kind==WalkSegmentKind::cubic){
                diff_from=normalize(diff_from);diff_to=normalize(diff_to);
                const auto average=normalize(plus(diff_from,diff_to));
                to.b=plus(to.a,point(over(times(average,len_to),3)));
                previous.c=minus(previous.d,point(over(times(average,len_from),3)));
                int changed=2;bool test_to=true;
                for(unsigned emergency=0;emergency<1000&&changed-- >0;++emergency){
                    spend();
                    if(test_to){
                        const auto n=clamp(to.b,to_slice);
                        if(!same(n,to.b)){
                            changed=1;const auto diff=minus(to.b,to.a),diff2=minus(n,to.a);auto other=minus(previous.c,previous.d);
                            if(diff.x)other.x=integer((std::int64_t(other.x)*diff2.x)/diff.x);
                            if(diff.y)other.y=integer((std::int64_t(other.y)*diff2.y)/diff.y);
                            to.b=n;previous.c=plus(previous.d,other);
                        }
                    }else{
                        const auto n=clamp(previous.c,from_slice);
                        if(!same(n,previous.c)){
                            changed=1;const auto diff=minus(previous.c,previous.d),diff2=minus(n,previous.d);auto other=minus(to.b,to.a);
                            if(diff.x)other.x=integer((std::int64_t(other.x)*diff2.x)/diff.x);
                            if(diff.y)other.y=integer((std::int64_t(other.y)*diff2.y)/diff.y);
                            previous.c=n;to.b=plus(to.a,other);
                        }
                    }
                    test_to=!test_to;
                }
            }else {
                diff_to=normalize(diff_to);
                previous.c=clamp(minus(previous.d,point(over(times(diff_to,len_from),3))),from_slice);
            }
        }else if(to.kind==WalkSegmentKind::cubic){
            const auto direction=normalize(vec(minus(previous.d,previous.a)));
            const auto len=length(vec(minus(to.d,to.a)));
            to.b=clamp(plus(to.a,point(over(times(direction,len),3))),to_slice);
        }
    }
    static Point direction(float radians,std::int32_t magnitude){
        return point(times({static_cast<float>(std::sin(static_cast<double>(radians))),
            static_cast<float>(-std::cos(static_cast<double>(radians)))},static_cast<float>(magnitude)));
    }
};
}
WalkPathResult generate_walk_path(const RectRouteLineResult& input,Point destination,bool bezier,
    std::optional<float> incoming,std::optional<float> outgoing,const WalkPathLimits& limits){
    WalkPathResult result;
    if(input.status!=RectRouteStatus::found||(incoming&&!std::isfinite(*incoming))||(outgoing&&!std::isfinite(*outgoing)))return result;
    Builder builder{limits,0,{}};
    try{
        if(input.corridor.size()>limits.corridor_entries)throw Stop{WalkPathStatus::budget_exhausted};
        const auto dest=hip(destination);
        for(const auto& entry:input.corridor){builder.spend();(void)Builder::bounds(entry);
            if(entry.line_id< -1)throw Stop{WalkPathStatus::invalid_input};
            const auto bounds=Builder::bounds(entry);
            if(!same(Builder::clamp(entry.entry_hip,bounds),entry.entry_hip))throw Stop{WalkPathStatus::invalid_input};
        }
        if(!bezier){
            const RectRouteLineEntry* last=nullptr;
            for(const auto& entry:input.corridor){if(last)builder.append({WalkSegmentKind::line,last->entry_hip,{},{},entry.entry_hip});last=&entry;}
            if(last)builder.append({WalkSegmentKind::line,last->entry_hip,{},{},dest});
        }else{
            const RectRouteLineEntry* last=nullptr;const RectRouteLineEntry* last2=nullptr;
            const RectRouteLineEntry* line_start=nullptr;const RectRouteLineEntry* pre_line_start=nullptr;
            std::optional<std::size_t> last_segment;std::int32_t line_process=-1;
            for(const auto& entry:input.corridor){
                builder.spend();
                if(last){
                    if(line_process!=-1||last->line_id!=-1){
                        if(line_process==-1){line_start=last;pre_line_start=last2;line_process=last->line_id;}
                        if(entry.line_id!=line_process){
                            WalkSegmentGeometry seg{WalkSegmentKind::line,line_start->entry_hip,{},{},entry.entry_hip};
                            builder.controls(last_segment,seg,pre_line_start?*pre_line_start:*line_start,*line_start);
                            builder.append(seg);last_segment=builder.segments.size()-1;line_process=-1;
                        }
                        last2=last;last=&entry;continue;
                    }
                    if(same(entry.entry_hip,last->entry_hip)){last=&entry;continue;}
                    WalkSegmentGeometry seg{WalkSegmentKind::cubic,last->entry_hip,{},{},entry.entry_hip};
                    if(!last_segment)seg.b=incoming?Builder::clamp(plus(seg.a,Builder::direction(*incoming,6*walk_path_hip)),Builder::bounds(*last)):seg.a;
                    else {if(!last2)throw Stop{WalkPathStatus::invalid_input};builder.controls(last_segment,seg,*last2,*last);}
                    builder.append(seg);last_segment=builder.segments.size()-1;
                }
                last2=last;last=&entry;
            }
            if(last){
                WalkSegmentGeometry seg{WalkSegmentKind::cubic,last->entry_hip,{},dest,dest};
                if(outgoing)seg.c=Builder::clamp(minus(seg.d,Builder::direction(*outgoing,16*walk_path_hip)),Builder::bounds(*last));
                if(!last2)seg.b=incoming?Builder::clamp(plus(seg.a,Builder::direction(*incoming,6*walk_path_hip)),Builder::bounds(*last)):seg.a;
                else builder.controls(last_segment,seg,*last2,*last);
                builder.append(seg);
            }
        }
        result.status=WalkPathStatus::success;result.segments=std::move(builder.segments);
    }catch(const Stop& stop){result.status=stop.status;}
    catch(const std::bad_alloc&){result.status=WalkPathStatus::budget_exhausted;}
    result.work_units=builder.work;return result;
}
WalkFrameResult WalkPathSegment::calculate_total_frames() noexcept {
    try{
        valid(geometry_);std::int32_t frames=0;auto arcs=arc_lengths_;
        if(geometry_.kind==WalkSegmentKind::line){
            const auto delta=minus(geometry_.d,geometry_.a);
            const auto x=std::int64_t(delta.x),y=std::int64_t(delta.y);
            const auto square=static_cast<std::uint64_t>(x*x)+static_cast<std::uint64_t>(y*y);
            if(square>INT64_MAX)throw Stop{WalkPathStatus::unsupported_arithmetic};
            const auto distance=std::sqrt(static_cast<double>(square))/walk_path_hip;
            if(!std::isfinite(distance)||distance>INT32_MAX/10)throw Stop{WalkPathStatus::unsupported_arithmetic};
            frames=integer(static_cast<std::int64_t>(distance)*10);if(!frames)frames=1;
        }else{
            auto previous=interp(geometry_,0);float total=0;
            for(unsigned n=0;n<20;++n){const auto current=interp(geometry_,div(static_cast<float>(n+1),20));
                total=add(total,length(minus(current,previous)));if(!std::isfinite(total))throw Stop{WalkPathStatus::unsupported_arithmetic};
                arcs[n]=total;previous=current;}
            frames=truncate(mul(total,10));
        }
        total_frames_=frames;arc_lengths_=arcs;arc_lengths_ready_=geometry_.kind==WalkSegmentKind::cubic;
        return {WalkPathStatus::success,std::max<std::int32_t>(1,frames)};
    }catch(const Stop& stop){return {stop.status,0};}
    catch(...){return {WalkPathStatus::unsupported_arithmetic,0};}
}
WalkPathStatus WalkPathSegment::update_total_frames(std::int32_t frames) noexcept {
    try{valid(geometry_);total_frames_=std::max<std::int32_t>(1,frames);return WalkPathStatus::success;}
    catch(const Stop& stop){return stop.status;}
}
void WalkPathSegment::reset_to_frame(std::int32_t) noexcept {
    if(geometry_.kind==WalkSegmentKind::cubic){last_arc_index_=0;last_velocity_.reset();}
}
WalkPathSample WalkPathSegment::next_point_and_velocity(std::int32_t frame) noexcept {
    WalkPathSample result;
    try{
        valid(geometry_);if(total_frames_<=0||frame<0||frame>total_frames_)throw Stop{WalkPathStatus::invalid_input};
        if(geometry_.kind==WalkSegmentKind::line){
            const auto delta=minus(geometry_.d,geometry_.a);
            const Point at{integer(std::int64_t(geometry_.a.x)+std::int64_t(delta.x)*frame/total_frames_),
                integer(std::int64_t(geometry_.a.y)+std::int64_t(delta.y)*frame/total_frames_)};
            result.position={short_integer(at.x/walk_path_hip),short_integer(at.y/walk_path_hip)};
            const auto amount=div(static_cast<float>(frame),static_cast<float>(total_frames_));
            // XNA Vector2.Lerp is value1 + (value2-value1)*amount.
            const auto visual=plus(vec(geometry_.a),times(minus(vec(geometry_.d),vec(geometry_.a)),amount));
            result.visual=over(visual,walk_path_hip*16);result.velocity=over(over(vec(delta),static_cast<float>(total_frames_)),walk_path_hip*16);
        }else{
            if(!arc_lengths_ready_)throw Stop{WalkPathStatus::invalid_input};
            auto index=last_arc_index_;
            const auto next=[&](std::int32_t f){
                const auto distance=div(mul(static_cast<float>(f),arc_lengths_[19]),static_cast<float>(total_frames_));
                auto arc=arc_lengths_[index];while(distance>arc&&index<19)arc=arc_lengths_[++index];
                const auto before=index?arc_lengths_[index-1]:0.0f;
                const auto fraction=div(sub(distance,before),sub(arc,before));
                const auto t=div(add(static_cast<float>(index),fraction),20);
                return interp(geometry_,t);
            };
            const auto current=next(frame);
            const auto velocity=frame>=total_frames_?last_velocity_.value_or(over(over(vec(minus(geometry_.d,geometry_.a)),walk_path_hip*16),static_cast<float>(total_frames_))):
                over(minus(next(frame+1),current),16);
            result.position={short_integer(round_even(current.x)),short_integer(round_even(current.y))};
            result.visual=over(current,16);result.velocity=velocity;
            if(!finite(result.visual)||!finite(velocity))throw Stop{WalkPathStatus::unsupported_arithmetic};
            last_arc_index_=index;last_velocity_=velocity;
        }
        if(!finite(result.visual)||!finite(result.velocity))throw Stop{WalkPathStatus::unsupported_arithmetic};
        result.status=WalkPathStatus::success;return result;
    }catch(const Stop& stop){result={};result.status=stop.status;return result;}
    catch(...){result={};result.status=WalkPathStatus::unsupported_arithmetic;return result;}
}
WalkPathStatus WalkPathSegment::restore(const WalkSegmentSnapshot& snapshot) noexcept {
    WalkPathSegment prepared(snapshot.geometry);const auto calculated=prepared.calculate_total_frames();
    if(calculated.status!=WalkPathStatus::success)return calculated.status;
    const auto updated=prepared.update_total_frames(snapshot.total_frames);if(updated!=WalkPathStatus::success)return updated;
    prepared.reset_to_frame(0);*this=prepared;return WalkPathStatus::success;
}
WalkPolylineResult walk_segment_polyline(const WalkSegmentGeometry& geometry,bool start){
    WalkPolylineResult result;
    try{valid(geometry);
        if(geometry.kind==WalkSegmentKind::line){if(start)result.points.push_back(over(vec(geometry.a),walk_path_hip*16));result.points.push_back(over(vec(geometry.d),walk_path_hip*16));}
        else for(unsigned n=start?0:1;n<=30;++n)result.points.push_back(over(interp(geometry,div(static_cast<float>(n),30)),16));
        result.status=WalkPathStatus::success;
    }catch(const Stop& stop){result.points.clear();result.status=stop.status;}
    catch(const std::bad_alloc&){result.points.clear();result.status=WalkPathStatus::budget_exhausted;}
    return result;
}
const char* walk_path_status_name(WalkPathStatus status) noexcept {
    switch(status){case WalkPathStatus::success:return "success";case WalkPathStatus::invalid_input:return "invalid_input";
        case WalkPathStatus::unsupported_arithmetic:return "unsupported_arithmetic";case WalkPathStatus::budget_exhausted:return "budget_exhausted";}
    return "invalid_input";
}
} // namespace odh
