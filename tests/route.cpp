#include "odh/route.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks=0;
void require(bool v,const char* message) {++checks;if(!v)throw std::runtime_error(message);}
void verify(const odh::RouteGrid& grid,const odh::RoutePath& path) {
    std::uint32_t cost=0;
    for(std::size_t n=1;n<path.points.size();++n) {
        const auto a=path.points[n-1],b=path.points[n];
        require(grid.can_step(a,b),"all route segments respect cells and edges");
        cost+=(a.x==b.x || a.y==b.y)?10:14;
    }
    require(cost==path.cost,"reported cost matches path");
}
}
void shared_route_grid_cache_values(){
    const auto make=[](odh::RoutePoint origin=odh::RoutePoint{0,0}){
        odh::RouteGrid grid(128,128,262144,origin);
        for(int page=0;page<4;++page)grid.block({origin.x+page+1,origin.y+page*32+1});
        grid.wall({origin.x+8,origin.y+8},{origin.x+9,origin.y+8});return grid;
    };
    auto controlled=make(),resident=make(),npc_active=make(),npc_idle=make();
    resident.block({20,40});npc_active.block({20,80});
    const auto expected=odh::find_route(npc_active,{0,0},{{127,127}});
    require(expected.status==odh::RouteStatus::found,"independent NPC fixture has a real nontrivial path before cache coalescing");
    require(!controlled.same_contents(resident)&&!resident.same_contents(npc_active)&&controlled.same_contents(npc_idle),
        "different actor exclusions are distinct complete grid values despite shared static pages");
    const auto charged=odh::coalesce_route_grid_cache({&controlled,&resident,&npc_active,&npc_idle,nullptr,&controlled});
    require(charged==6*4096&&charged<controlled.cells()+resident.cells()+npc_active.cells()+npc_idle.cells(),
        "controlled, resident, active and idle NPC caches count exact unique blocked+wall pages without role filtering");
    const auto after=odh::find_route(npc_active,{0,0},{{127,127}});
    require(after.status==expected.status&&after.points==expected.points&&after.cost==expected.cost&&after.expanded==expected.expanded,
        "page sharing preserves deterministic routing values and search work");verify(npc_active,after);
    auto copied=controlled;
    const auto before=odh::RouteGrid::storage_cells({&controlled,&copied});
    require(copied.block({30,30})&&!copied.walkable({30,30})&&controlled.walkable({30,30})&&
        odh::RouteGrid::storage_cells({&controlled,&copied})==before+4096,
        "ordinary grid value copies detach only the changed page before an obstacle write");
    auto wall_copy=controlled;
    require(wall_copy.wall({127,31},{127,32})&&!wall_copy.can_step({127,31},{127,32})&&
        controlled.can_step({127,31},{127,32})&&
        !wall_copy.can_step({127,32},{127,31}),
        "reciprocal walls crossing two storage pages preserve independent live grid values");
    require(wall_copy.wall({127,31},{127,32},false)&&wall_copy.same_contents(controlled),
        "removing a cross-page wall restores the exact original topology");
    odh::RouteGrid empty(64,128),aliases(64,128);
    odh::coalesce_route_grid_cache({&empty,&aliases});
    require(aliases.wall({63,63},{63,64})&&!aliases.can_step({63,63},{63,64})&&empty.can_step({63,63},{63,64}),
        "two logical pages aliasing an interned zero page detach independently before reciprocal mutation");
    auto translated=make({-32,17});
    require(!translated.same_contents(controlled),"grid origins remain part of full value identity");
    odh::coalesce_route_grid_cache({&controlled,&translated});
    require(translated.walkable({-2,47})&&translated.index({-32,17})==0&&translated.point(0)==odh::RoutePoint{-32,17},
        "shared raw page bytes preserve each grid's independent signed coordinate mapping");
    odh::RouteGrid tail(64,65),tail_copy(64,65);
    require(tail.cells()==4160&&odh::coalesce_route_grid_cache({&tail,&tail_copy})==8192,
        "partial tail storage is charged as its complete allocated page without changing logical cell count");
    auto survivor=make();const auto survivor_path=odh::find_route(survivor,{0,0},{{127,127}});
    {auto transient=make();odh::coalesce_route_grid_cache({&transient,&survivor});}
    require(odh::find_route(survivor,{0,0},{{127,127}}).points==survivor_path.points,
        "destroying an idle peer leaves borrowed live grid storage alive");
    odh::RouteGrid all_clear(512,256),all_blocked(512,256);
    for(int y=0;y<256;++y)for(int x=0;x<512;++x)all_blocked.block({x,y});
    // Distinct byte patterns cannot be made equivalent by a room label or hash.
    auto remaining=make();const auto capped=odh::coalesce_route_grid_cache({&all_clear,&all_blocked,&remaining});
    require(capped>262144,"different blocked/wall page values still exceed the unchanged physical admission cap");
    // Force65 unique physical page values: no alias may conceal their allocation.
    std::vector<odh::RouteGrid> unique;unique.reserve(65);std::vector<odh::RouteGrid*> pool;
    for(int n=0;n<65;++n){unique.emplace_back(64,64);unique.back().block({n%64,n/64});pool.push_back(&unique.back());}
    require(odh::coalesce_route_grid_cache(pool)==65*4096&&odh::RouteGrid::storage_cells({&unique.front(),&unique.back()})==8192,
        "65 genuinely different pages still exceed the exact262144 physical-cell admission budget");
    odh::RouteGrid moved=std::move(survivor);
    require(survivor.cells()==0&&!survivor.contains({0,0})&&moved.cells()==16384,
        "moved storage has ordinary empty-source lifetime behavior");
}
int main() {
    try {
        shared_route_grid_cache_values();
        odh::RouteGrid g(9,9);
        auto p=odh::find_route(g,{0,0},{{8,8}});
        require(p.status==odh::RouteStatus::found && p.cost==112 && p.points.size()==9,"open diagonal optimum");verify(g,p);
        require(odh::find_route(g,{0,0},{{8,8}}).points==p.points,"deterministic path");
        for(const auto step:std::vector<odh::RoutePoint>{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}) {
            const auto adjacent=odh::find_route(g,{4,4},{{4+step.x,4+step.y}});
            require(adjacent.status==odh::RouteStatus::found&&adjacent.points.size()==2&&adjacent.points.back()==odh::RoutePoint{4+step.x,4+step.y},
                "parent direction reconstructs every cardinal and diagonal heading");verify(g,adjacent);
        }
        require(g.block({1,0}) && !g.can_step({0,0},{1,1}),"occupied corner cannot be cut");
        p=odh::find_route(g,{0,0},{{2,0}});
        require(p.status==odh::RouteStatus::found && p.cost==40,"detour around obstacle");verify(g,p);
        require(g.wall({0,0},{0,1}) && !g.can_step({0,1},{0,0}),"wall symmetric");
        require(odh::find_route(g,{0,0},{{8,8}}).status==odh::RouteStatus::no_path,"isolated start");
        require(g.wall({0,0},{0,1},false) && g.can_step({0,0},{0,1}),"remove wall");
        p=odh::find_route(g,{0,0},{{8,8},{0,2}});
        require(p.status==odh::RouteStatus::found && p.points.back()==odh::RoutePoint{0,2} && p.cost==20,"nearest reachable multi-goal");
        require(odh::find_route(g,{-1,0},{{1,1}}).status==odh::RouteStatus::invalid_input,"invalid start");
        require(odh::find_route(g,{0,0},{{1,0}}).status==odh::RouteStatus::no_path,"occupied goal");
        require(odh::find_route(g,{0,0},{{0,0}}).points.size()==1,"already at goal");
        require(odh::find_route(g,{0,0},{{8,8}},{0,100}).status==odh::RouteStatus::budget_exhausted,"expansion budget distinct from no path");
        require(odh::find_route(g,{0,0},{{8,8}},{100,0}).status==odh::RouteStatus::budget_exhausted,"frontier budget");
        require(!g.wall({0,0},{1,1}) && !g.block({9,0}),"mutations reject invalid coordinates");
        odh::RouteGrid region(3,2,6,{-4,10});
        p=odh::find_route(region,{-4,10},{{-2,11}});
        require(p.status==odh::RouteStatus::found&&p.points.front()==odh::RoutePoint{-4,10}&&
                p.points.back()==odh::RoutePoint{-2,11},"bounded region preserves absolute signed coordinates");verify(region,p);
        require(!region.contains({-5,10})&&!region.contains({-2,12})&&region.index({-3,11})==4&&
                region.point(4)==odh::RoutePoint{-3,11},"bounded region maps and rejects absolute coordinates");
        bool origin_overflow=false;try {odh::RouteGrid edge(2,1,2,{std::numeric_limits<std::int32_t>::max(),0});}
        catch(const std::invalid_argument&){origin_overflow=true;}
        require(origin_overflow,"bounded region rejects signed coordinate overflow");
        bool rejected=false;try {odh::RouteGrid huge(65535,65535);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"allocation bounded before large grid");
        // Full separating wall, with one door: shortest path must use that door.
        odh::RouteGrid door(7,7);
        for(int y=0;y<7;++y) if(y!=5) door.wall({2,y},{3,y});
        p=odh::find_route(door,{0,1},{{6,1}});
        require(p.status==odh::RouteStatus::found,"door detour exists");verify(door,p);
        bool passed=false;for(auto point:p.points) if(point==odh::RoutePoint{3,5}) passed=true;
        require(passed,"route passes through door rather than wall");
        std::cout<<checks<<" route checks passed (synthetic grid only)\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Route test failed: "<<e.what()<<'\n';return 1;}
}
