// SPDX-License-Identifier: MPL-2.0
#include "odh/walk_path.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
constexpr auto h=odh::walk_path_hip;
using Point=odh::RectRoutePoint;
using Kind=odh::WalkSegmentKind;
using Status=odh::WalkPathStatus;
bool same(Point a,Point b){return a.x==b.x&&a.y==b.y;}
std::uint32_t bits(float x){std::uint32_t n;std::memcpy(&n,&x,sizeof(n));return n;}
bool same(odh::WalkVector a,odh::WalkVector b){return bits(a.x)==bits(b.x)&&bits(a.y)==bits(b.y);}
odh::RectRouteLineEntry entry(odh::RectRouteObstacle bounds,Point point,std::int32_t line=-1){
    return {bounds,point,{point.x*h,point.y*h},line};
}
odh::WalkSegmentGeometry cubic(std::int32_t extent=3){return {Kind::cubic,{0,0},{0,0},{extent*h,0},{extent*h,0}};}
void line_arithmetic(){
    odh::WalkPathSegment line({Kind::line,{0,0},{},{},{3*h,4*h}});
    require(line.next_point_and_velocity(0).status==Status::invalid_input,"line cannot sample before a positive duration is established");
    const auto duration=line.calculate_total_frames();
    require(duration.status==Status::success&&duration.frames==50&&line.total_frames()==50,"line source 3-4-5 triangle takes floor(distance)*10 frames");
    const auto middle=line.next_point_and_velocity(25);
    require(middle.status==Status::success&&same(middle.position,{1,2})&&middle.visual.x==0.09375f&&middle.visual.y==0.125f,
        "line physical coordinates truncate HiP while visual interpolation retains fractions");
    require(bits(middle.velocity.x)==bits((3.0f*(1.0f/50))/16)&&bits(middle.velocity.y)==bits((4.0f*(1.0f/50))/16),
        "line velocity retains MonoGame reciprocal multiplication before tile scaling");
    require(line.update_total_frames(10)==Status::success&&same(line.next_point_and_velocity(10).position,{3,4}),"source duration scaling preserves the exact destination");
    require(line.update_total_frames(0)==Status::success&&line.total_frames()==1,"UpdateTotalFrames clamps even zero to one");
    odh::WalkPathSegment fraction({Kind::line,{0,0},{},{},{h/2,0}});
    require(fraction.calculate_total_frames().frames==1,"sub-unit line truncates duration before multiplication and then clamps to one");
    odh::WalkPathSegment negative({Kind::line,{0,0},{},{},{-3*h,0}});
    require(negative.calculate_total_frames().frames==30&&same(negative.next_point_and_velocity(15).position,{-1,0}),"negative line coordinates truncate toward zero rather than floor");
    require(negative.next_point_and_velocity(31).status==Status::invalid_input,"scheduler frames beyond the established segment extent are explicit invalid input");
}
void cubic_arithmetic(){
    odh::WalkPathSegment curve(cubic());
    const auto duration=curve.calculate_total_frames();
    require(duration.status==Status::success&&duration.frames==30&&curve.total_frames()==30,"straight cubic still derives duration from twenty sampled float arc lengths");
    // Fixed binary32 values from the pinned polynomial and twenty equal t
    // subdivisions. These distinguish cubic arc sampling from linear distance.
    const std::array<std::uint32_t,20> arc={0x3cb22d0f,0x3dac0831,0x3e3a9fbf,0x3e9fbe77,0x3ef00000,
        0x3f25e354,0x3f58624e,0x3f872b03,0x3fa34bc6,0x3fc00000,0x3fdcb439,0x3ff8d4fe,
        0x4009e76c,0x4016872b,0x40220000,0x402c0831,0x40345604,0x403a9fbe,0x403e9ba6,0x40400000};
    for(unsigned n=0;n<20;++n)require(bits(curve.arc_lengths()[n])==arc[n],"fixed source binary32 arc-length sample");
    const auto first=curve.next_point_and_velocity(0);
    require(first.status==Status::success&&same(first.position,{0,0})&&bits(first.velocity.x)==0x3bc80491,
        "cubic velocity samples the next arc-normalized point rather than endpoint delta");
    const auto middle=curve.next_point_and_velocity(15);
    require(middle.status==Status::success&&same(middle.position,{2,0})&&bits(middle.visual.x)==0x3dc00000&&bits(middle.velocity.x)==0x3bcd5940,
        "cubic half coordinate1.5 rounds midpoint-even to2 while preserving continuous visual and velocity");
    odh::WalkPathSegment negative(cubic(-3));
    require(negative.calculate_total_frames().frames==30&&same(negative.next_point_and_velocity(15).position,{-2,0}),"negative cubic midpoint follows nearest-even rather than line truncation");
    curve.reset_to_frame(15);
    require(curve.last_arc_index()==0&&!curve.last_velocity(),"source ResetToFrame discards lookup cursor and previous velocity");
    const auto again=curve.next_point_and_velocity(15);
    require(same(middle.visual,again.visual)&&same(middle.velocity,again.velocity),"reset reconstructs the same current cubic sample from the beginning of the table");
    const auto penultimate=curve.next_point_and_velocity(29);
    const auto final=curve.next_point_and_velocity(30);
    require(penultimate.status==Status::success&&final.status==Status::success&&same(final.position,{3,0})&&same(final.velocity,penultimate.velocity),"terminal cubic sampling preserves the last live velocity");
    odh::WalkPathSegment loaded;
    require(loaded.restore(curve.snapshot())==Status::success&&loaded.last_arc_index()==0&&!loaded.last_velocity(),"marshal restore rebuilds the arc table and resets derived live caches");
    const auto loaded_final=loaded.next_point_and_velocity(30);
    require(loaded_final.status==Status::success&&bits(loaded_final.velocity.x)==0x3bccccce&&!same(loaded_final.velocity,final.velocity),
        "source restored final-frame velocity uses endpoint fallback because Last is deliberately unsaved");
    curve.reset_to_frame(0);const auto live16=curve.next_point_and_velocity(16);
    loaded.reset_to_frame(0);const auto loaded16=loaded.next_point_and_velocity(16);
    require(same(live16.position,loaded16.position)&&same(live16.visual,loaded16.visual)&&same(live16.velocity,loaded16.velocity),"fresh segment restore reproduces nonterminal physical, visual and velocity state");
    const auto before=loaded.snapshot();auto invalid=before;invalid.geometry.kind=static_cast<Kind>(99);
    require(loaded.restore(invalid)==Status::invalid_input&&loaded.snapshot().total_frames==before.total_frames&&same(loaded.geometry().d,before.geometry.d),"invalid snapshot leaves the existing segment untouched");
    auto clamped=before;clamped.total_frames=-1;
    require(loaded.restore(clamped)==Status::success&&loaded.total_frames()==1,
        "marshal UpdateTotalFrames preserves the source clamp even for saved nonpositive outer durations");
    odh::WalkPathSegment collapsed({Kind::cubic,{0,0},{0,0},{0,0},{0,0}});
    require(collapsed.calculate_total_frames().frames==1&&collapsed.total_frames()==0&&collapsed.update_total_frames(1)==Status::success&&
        collapsed.next_point_and_velocity(0).status==Status::unsupported_arithmetic,"zero-length cubic source duration quirk is retained, while undefined arc division fails explicitly");
}
void generation(){
    odh::RectRouteLineResult straight;straight.status=odh::RectRouteStatus::found;
    straight.corridor={entry({0,0,16,16},{8,8})};
    const auto path=odh::generate_walk_path(straight,{8,16},true,0.0f,static_cast<float>(3.14159265358979323846/2));
    require(path.status==Status::success&&path.segments.size()==1&&path.segments[0].kind==Kind::cubic&&
        same(path.segments[0].a,{8*h,8*h})&&same(path.segments[0].b,{8*h,2*h})&&
        same(path.segments[0].c,{0,16*h})&&same(path.segments[0].d,{8*h,16*h}),
        "incoming six-unit and outgoing sixteen-unit tangent controls clamp within the source free rectangle");
    odh::RectRouteLineResult corner;corner.status=odh::RectRouteStatus::found;
    corner.corridor={entry({0,0,10,10},{0,0}),entry({10,0,20,10},{10,0})};
    const auto bend=odh::generate_walk_path(corner,{10,10},true);
    require(bend.status==Status::success&&bend.segments.size()==2&&same(bend.segments[0].b,{0,0})&&
        same(bend.segments[0].c,{10*h-77234,0})&&same(bend.segments[1].b,{10*h+77234,0}),
        "two-cubic tangent averaging and alternating integer clamp propagation retain fixed source controls");
    odh::RectRouteLineResult groups;groups.status=odh::RectRouteStatus::found;
    const odh::RectRouteObstacle roomy{-100,-100,100,100};
    groups.corridor={entry(roomy,{0,0}),entry(roomy,{10,0},7),entry(roomy,{20,0},7),entry(roomy,{30,0})};
    const auto mixed=odh::generate_walk_path(groups,{40,0},true);
    require(mixed.status==Status::success&&mixed.segments.size()==3&&mixed.segments[0].kind==Kind::cubic&&
        mixed.segments[1].kind==Kind::line&&mixed.segments[2].kind==Kind::cubic&&
        same(mixed.segments[1].a,{10*h,0})&&same(mixed.segments[1].d,{30*h,0})&&
        same(mixed.segments[1].b,{0,0})&&same(mixed.segments[1].c,{0,0})&&
        mixed.segments[0].c.x<10*h&&mixed.segments[2].b.x>30*h,
        "LineID combines optimized rectangles into a line and connects both cubic-to-line and line-to-cubic controls");
    const auto lines=odh::generate_walk_path(groups,{40,0},false);
    require(lines.status==Status::success&&lines.segments.size()==4&&lines.segments[0].kind==Kind::line&&same(lines.segments.back().d,{40*h,0}),"explicit line tuning retains every ordered HiP crossing before its destination");
    auto fractional=straight;fractional.corridor[0].entry_hip.x+=h/2;
    const auto fractional_path=odh::generate_walk_path(fractional,{8,16},false);
    require(fractional_path.status==Status::success&&fractional_path.segments[0].a.x==8*h+h/2,"generation never rounds optimized fractional HiP crossings back to grid points");
    auto duplicate=groups;duplicate.corridor[1]=duplicate.corridor[0];duplicate.corridor[2].line_id=-1;
    const auto skipped=odh::generate_walk_path(duplicate,{40,0},true);
    require(skipped.status==Status::success&&skipped.segments.size()==3,"cubic generation skips duplicate crossings while retaining later controls");
    odh::RectRouteLineResult empty;empty.status=odh::RectRouteStatus::found;
    require(odh::generate_walk_path(empty,{0,0},true).status==Status::success&&odh::generate_walk_path(empty,{0,0},true).segments.empty(),"empty found corridor is a successful turn-only path, never a synthetic segment");
    auto cap=odh::WalkPathLimits{};cap.segments=1;
    const auto limited=odh::generate_walk_path(groups,{40,0},true,{},{},cap);
    require(limited.status==Status::budget_exhausted&&limited.segments.empty(),"segment cap returns no partial executable path");
    cap={};cap.work_units=1;
    require(odh::generate_walk_path(groups,{40,0},true,{},{},cap).status==Status::budget_exhausted,"bounded generation work cannot silently produce a shorter path");
    auto bad=straight;bad.corridor[0].entry_hip.x=17*h;
    require(odh::generate_walk_path(bad,{8,16},true).status==Status::invalid_input,"crossing outside its free rectangle cannot become executable geometry");
    require(odh::generate_walk_path(straight,{8,16},true,std::numeric_limits<float>::infinity()).status==Status::invalid_input,"nonfinite tangent is invalid before generating controls");
}
void overflow_and_polyline(){
    odh::WalkPathSegment overflow({Kind::line,{INT32_MIN,0},{},{},{INT32_MAX,0}});
    require(overflow.calculate_total_frames().status==Status::unsupported_arithmetic,"source signed Point subtraction overflow stays explicit and has no sample");
    odh::WalkPathSegment bad_kind({static_cast<Kind>(3),{},{},{},{}});
    require(bad_kind.calculate_total_frames().status==Status::invalid_input,"unknown segment tags cannot masquerade as lines");
    const auto line=odh::walk_segment_polyline({Kind::line,{0,0},{},{},{3*h,4*h}});
    const auto curve=odh::walk_segment_polyline(cubic());
    const auto without=odh::walk_segment_polyline(cubic(),false);
    require(line.status==Status::success&&line.points.size()==2&&line.points.back().x==3.0f/16&&line.points.back().y==4.0f/16&&
        curve.status==Status::success&&curve.points.size()==31&&without.points.size()==30&&curve.points.back().x==3.0f/16,
        "portable AddToPath export matches line endpoints and thirty cubic subdivisions without cache mutation");
}
void degenerate_control_transitions(){
    const odh::RectRouteObstacle roomy{-100,-100,100,100};
    odh::RectRouteLineResult route;route.status=odh::RectRouteStatus::found;
    route.corridor={entry(roomy,{0,0}),entry(roomy,{10,0},7),entry(roomy,{10,0}),entry(roomy,{20,0})};
    const auto mixed=odh::generate_walk_path(route,{30,0},true);
    require(mixed.status==Status::success&&mixed.segments.size()==4&&mixed.segments[1].kind==Kind::line&&
        same(mixed.segments[1].a,mixed.segments[1].d)&&same(mixed.segments[0].c,mixed.segments[0].d)&&
        same(mixed.segments[2].b,mixed.segments[2].a),
        "source zero-length grouped line survives with NaN-to-zero controls in both adjacent cubic transitions");
    route.corridor={entry(roomy,{0,0}),entry(roomy,{10,0})};
    for(const Point destination: {Point{0,0},Point{10,0}}){
        const auto path=odh::generate_walk_path(route,destination,true);
        require(path.status==Status::success&&path.segments.size()==2&&
            same(path.segments[0].c,path.segments[0].d)&&same(path.segments[1].b,path.segments[1].a),
            "source opposite or zero-length cubic tangent average casts NaN to zero for both controls");
    }
    // Full optimized corridor emitted by the shared planner at tick48625.
    // No object identity participates in the geometry contract.
    route.corridor={entry({24,-16,41,600},{24,600}),entry({41,-16,89,569},{41,569}),
        entry({89,375,121,569},{89,559}),entry({121,375,185,553},{121,553}),
        entry({135,553,185,567},{185,553},1),entry({185,553,418,567},{185,553}),
        entry({333,515,418,553},{333,553}),entry({403,502,418,515},{415,515},2),
        entry({418,502,445,515},{418,514})};
    route.corridor[7].entry_hip={13630721,16875520};
    route.corridor[8].entry_hip={13697024,16845234};
    const auto actual=odh::generate_walk_path(route,{440,504},true,0.0f,-6.283185482025146484375f);
    require(actual.status==Status::success&&actual.segments.size()==9&&actual.segments[4].kind==Kind::line&&
        same(actual.segments[4].a,{6062080,18120704})&&same(actual.segments[4].a,actual.segments[4].d)&&
        same(actual.segments[3].c,actual.segments[3].d)&&same(actual.segments[5].b,actual.segments[5].a),
        "observed nine-rectangle corridor generates source segments without special-casing actor or coordinates");
    for(const auto& geometry:actual.segments){
        odh::WalkPathSegment segment(geometry);
        const auto duration=segment.calculate_total_frames();
        require(duration.status==Status::success&&segment.update_total_frames(duration.frames)==Status::success&&
            segment.next_point_and_velocity(0).status==Status::success,
            "observed generator output initializes finite native playback, including its zero-length line");
    }
}
}
int main(){try{line_arithmetic();cubic_arithmetic();generation();overflow_and_polyline();degenerate_control_transitions();std::cout<<checks<<" walk path checks passed\n";return 0;}
catch(const std::exception& error){std::cerr<<"walk path: "<<error.what()<<'\n';return 1;}}
