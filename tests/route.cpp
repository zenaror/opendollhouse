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
int main() {
    try {
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
