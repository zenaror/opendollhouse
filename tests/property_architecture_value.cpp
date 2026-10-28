#include "odh/property_architecture_value.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
odh::SavedArchitecture grid() {
    odh::SavedArchitecture a;a.width=a.height=2;
    for(auto& l:a.levels){l.floors.resize(4);l.walls.resize(4);l.flags=std::vector<std::uint8_t>(4,0);}
    a.pools=std::vector<std::uint8_t>(4,255);return a;
}
odh::PropertyArchitecturePrices prices() {
    odh::PropertyArchitecturePrices p;p.structural={{0,0},{1,3},{2,7},{7,3},{8,3},{9,3}};
    p.floor_lookup_complete=p.wallpaper_lookup_complete=true;return p;
}
auto resolved(const odh::SavedArchitecture& a,const odh::PropertyArchitecturePrices& p) {
    const auto r=odh::inspect_property_architecture_value(a,p);
    check(r.status==odh::PropertyArchitectureValueStatus::resolved,"expected resolved architecture value");return r;
}
void unchanged(const odh::SavedArchitecture& a,const odh::SavedArchitecture& b) {
    check(a.width==b.width && a.height==b.height && a.pools==b.pools,"input geometry unchanged");
    for(std::size_t s=0;s<2;++s) {
        const auto& x=a.levels[s];const auto& y=b.levels[s];
        check(x.floors==y.floors && x.flags==y.flags && x.walls.size()==y.walls.size(),"input grids unchanged");
        for(std::size_t n=0;n<x.walls.size();++n) {
            const auto& p=x.walls[n];const auto& q=y.walls[n];
            check(p.segments==q.segments && p.unknown==q.unknown && p.top_left_style==q.top_left_style &&
                p.top_right_style==q.top_right_style && p.top_left_pattern==q.top_left_pattern &&
                p.top_right_pattern==q.top_right_pattern && p.bottom_left_pattern==q.bottom_left_pattern &&
                p.bottom_right_pattern==q.bottom_right_pattern,"input wall fields unchanged");
        }
    }
}
}
int main(){try{
    using namespace odh;using R=PropertyArchitectureValueReason;
    auto a=grid();auto p=prices();auto r=resolved(a,p);
    check(r.total==0 && !r.has_architecture,"empty architecture");
    a.levels[0].floors[0]=5;a.levels[1].floors[0]=6;p.floors={{5,11},{6,17}};
    (*a.levels[0].flags)[0]=32;check(resolved(a,p).total==17,"upper story uses own flags");
    (*a.levels[1].flags)[0]=32;(*a.levels[0].flags)[0]=0;
    check(resolved(a,p).total==11,"ground uses own flags");
    a=grid();p=prices();a.levels[0].walls[0].segments=1;a.levels[0].walls[0].top_left_style=1;
    a.levels[1].walls[0]=a.levels[0].walls[0];r=resolved(a,p);
    check(r.structural==6 && r.total==3,"odd structural prices summed before single half");
    p.structural[1]=-3;check(resolved(a,p).total==-3,"negative structural signed half");
    for(const auto style:{std::uint16_t{1},std::uint16_t{7},std::uint16_t{8},std::uint16_t{9},std::uint16_t{2}}) {
        a=grid();p=prices();auto& w=a.levels[0].walls[0];w.segments=1;w.top_left_style=style;w.top_left_pattern=5;p.wallpaper[5]=13;
        check(resolved(a,p).surfaces_and_pools==(style==2?0:13),"wallpaper only styles1/7/8/9");
    }
    a=grid();p=prices();auto& w=a.levels[0].walls[0];
    w.segments=16;w.top_right_style=1;w.bottom_left_pattern=2;w.bottom_right_pattern=3;p.wallpaper={{2,101},{3,17}};
    check(resolved(a,p).surfaces_and_pools==17,"horizontal diagonal BR wallpaper only");
    w.segments=32;check(resolved(a,p).surfaces_and_pools==101,"vertical diagonal BL wallpaper only");
    w.segments=48;r=resolved(a,p);check(r.structural==3 && r.surfaces_and_pools==17,"mixed16/32 skips32");
    w.segments=33;w.top_left_style=2;check(resolved(a,p).structural==7,"cardinal plus32 skips32");
    w.segments=16;w.top_left_style=5;w.top_left_pattern=6;a.levels[0].floors[0]=255;p.floors={{5,3},{6,3}};p.wallpaper.clear();
    check(resolved(a,p).surfaces_and_pools==2,"diagonal halves separate without floor255 charge");
    p.floors={{5,-3},{6,-3}};check(resolved(a,p).surfaces_and_pools==-2,"negative diagonal halves towardzero");
    a=grid();a.levels[0].floors[0]=255;p=prices();r=resolved(a,p);
    check(r.has_architecture && r.total==0,"sentinel255 marks architecture even without halves");
    a=grid();p=prices();a.levels[0].walls[0].segments=4;
    check(inspect_property_architecture_value(a,p).reason==R::missing_neighbor_style,"rawbit4 needs neighbor");
    a.levels[0].walls[1].segments=1;a.levels[0].walls[1].top_left_style=2;r=resolved(a,p);
    check(r.structural==14 && r.total==7,"duplicate positive x edges halved once");
    a.levels[0].walls[1].segments=4;
    check(inspect_property_architecture_value(a,p).reason==R::missing_neighbor_style,"boundary bit4 rejected");
    a=grid();a.levels[0].walls[0].segments=8;
    check(inspect_property_architecture_value(a,p).reason==R::missing_neighbor_style,"rawbit8 needs neighbor");
    a.levels[0].walls[2].segments=2;a.levels[0].walls[2].top_right_style=2;
    check(resolved(a,p).structural==14,"bit8 uses positive y TR style");
    a=grid();p=prices();(*a.pools)[0]=0;
    check(inspect_property_architecture_value(a,p).reason==R::missing_pool_price,"pool0 needs price");
    p.pool_tile_price=100;check(resolved(a,p).surfaces_and_pools==100,"pool below16 charged");
    (*a.pools)[0]=16;p.pool_tile_price.reset();r=resolved(a,p);
    check(r.has_architecture && r.total==0,"pool16 flagonly");
    (*a.pools)[0]=15;(*a.levels[0].flags)[0]=32;r=resolved(a,p);
    check(!r.has_architecture && r.total==0,"flag32 excludes pool before lookup");
    (*a.levels[0].flags)[0]=0;(*a.pools)[0]=32;
    check(!resolved(a,p).has_architecture,"pool32 uncharged and unflagged");
    a=grid();p=prices();a.levels[0].floors[0]=77;p.floor_lookup_complete=false;
    check(inspect_property_architecture_value(a,p).reason==R::incomplete_floor_lookup,"unknown floor not zero");
    p.floors[77]=19;check(resolved(a,p).total==19,"known price without complete domain");
    p.floors.clear();p.floor_lookup_complete=true;check(resolved(a,p).total==0,"certified floor miss zero");
    a=grid();p=prices();a.levels[0].walls[0].segments=1;a.levels[0].walls[0].top_left_style=1;
    a.levels[0].walls[0].top_left_pattern=77;p.wallpaper_lookup_complete=false;
    check(inspect_property_architecture_value(a,p).reason==R::incomplete_wallpaper_lookup,"wallpaper coverage explicit");
    p.wallpaper_lookup_complete=true;check(resolved(a,p).surfaces_and_pools==0,"certified wallpaper miss zero");
    p.structural.erase(1);check(inspect_property_architecture_value(a,p).reason==R::missing_structural_price,"structural missing never zero");
    check(inspect_property_architecture_value(a,p,{0}).reason==R::price_budget,"bounded map entries");
    a=grid();p=prices();a.width=65;check(inspect_property_architecture_value(a,p).reason==R::invalid_extent,"dimension limit");
    a=grid();a.width=0;check(inspect_property_architecture_value(a,p).reason==R::invalid_extent,"zero dimension rejected");
    a=grid();a.height=1;check(inspect_property_architecture_value(a,p).reason==R::invalid_extent,"square required");
    a=grid();a.levels[1].walls.pop_back();check(inspect_property_architecture_value(a,p).reason==R::invalid_extent,"wall extent");
    a=grid();a.levels[1].floors.pop_back();check(inspect_property_architecture_value(a,p).reason==R::invalid_extent,"floor extent");
    a=grid();a.levels[1].flags.reset();check(inspect_property_architecture_value(a,p).reason==R::missing_flags,"own flags mandatory");
    a=grid();a.levels[0].flags->pop_back();check(inspect_property_architecture_value(a,p).reason==R::missing_flags,"flag extent");
    a=grid();a.pools.reset();check(inspect_property_architecture_value(a,p).reason==R::missing_pools,"pool mandatory");
    a=grid();a.pools->pop_back();check(inspect_property_architecture_value(a,p).reason==R::missing_pools,"pool extent");
    a=grid();a.levels[0].walls[0].segments=64;check(inspect_property_architecture_value(a,p).reason==R::unknown_segments,"unknown mask");
    a=grid();p=prices();a.levels[0].floors[0]=1;a.levels[1].floors[0]=1;p.floors[1]=std::numeric_limits<std::int32_t>::max();
    check(inspect_property_architecture_value(a,p).reason==R::arithmetic_overflow,"surface overflow");
    a=grid();p=prices();a.levels[0].walls[0].segments=3;
    a.levels[0].walls[0].top_left_style=a.levels[0].walls[0].top_right_style=1;p.structural[1]=std::numeric_limits<std::int32_t>::max();
    check(inspect_property_architecture_value(a,p).reason==R::arithmetic_overflow,"structural overflow before half");
    a.levels[0].walls[0].segments=1;a.levels[0].floors[0]=1;p.floors[1]=std::numeric_limits<std::int32_t>::max();
    check(inspect_property_architecture_value(a,p).reason==R::arithmetic_overflow,"final overflow");
    a=grid();p=prices();a.levels[0].floors[0]=7;p.floors[7]=-17;const auto original=a;const auto original_prices=p;
    check(resolved(a,p).total==-17,"negative signed prices");unchanged(a,original);
    check(p.structural==original_prices.structural && p.floors==original_prices.floors && p.wallpaper==original_prices.wallpaper &&
        p.pool_tile_price==original_prices.pool_tile_price && p.floor_lookup_complete==original_prices.floor_lookup_complete &&
        p.wallpaper_lookup_complete==original_prices.wallpaper_lookup_complete,"prices unchanged");
    check(std::string(property_architecture_value_reason_name(R::arithmetic_overflow))=="arithmetic_overflow","named reason");
    std::cout<<"raw geometry valuation, resource coverage, component halves and bounds passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
