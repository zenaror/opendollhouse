// SPDX-License-Identifier: MPL-2.0
// Resource-free tests of the native rectangle-graph adapter. They do not
// establish equivalence to the DVD's graph geometry or edge costs.
#include "simantics/engine/Routing/VMRectRouter.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using FSO::SimAntics::Engine::Routing::VMRectRouter;
using FSO::SimAntics::Engine::Routing::VMWeightedGoal;
using FSO::SimAntics::Engine::Routing::VMWeightedRoute;
using FSO::SimAntics::Model::Routing::VMObstacle;
using st::Point;
static void need(bool b,const char* why) { if (!b) throw std::runtime_error(why); }
// A route is allowed on an obstacle boundary, but must never enter its open interior.
static bool crossesInterior(Point a,Point b,const VMObstacle& o) {
    double lo=0.0,hi=1.0;
    auto axis=[&](int p,int q,int min,int max) {
        if (p==q) return p>min && p<max;
        double l=double(min-p)/double(q-p),r=double(max-p)/double(q-p);
        if (l>r) std::swap(l,r);
        lo=std::max(lo,l);hi=std::min(hi,r);return lo<hi;
    };
    return axis(a.X,b.X,o.x1,o.x2) && axis(a.Y,b.Y,o.y1,o.y2) && lo<hi && hi>0 && lo<1;
}
struct World {
    // Closed room with two walls that force a multi-region detour.
    std::vector<VMObstacle> blocks;
    std::vector<VMObstacle*> map;
    explicit World(bool obstacles=false):blocks{{-10,-10,0,110},{100,-10,110,110},{0,-10,100,0},{0,100,100,110}} {
        if(obstacles) { blocks.emplace_back(35,0,45,75); blocks.emplace_back(65,25,75,100); }
        for(auto& b:blocks) map.push_back(&b);
    }
    VMWeightedRoute route(Point from,std::vector<VMWeightedGoal> goals) {
        VMRectRouter router(map);return router.Route(from,goals);
    }
    void valid(const VMWeightedRoute& r,Point from,Point to) const {
        need(bool(r.Path) && !r.Path->empty(),"missing route");
        need(r.Path->front()==from && r.Path->back()==to,"route endpoints");
        auto i=r.Path->begin(),j=i; ++j;
        for(;j!=r.Path->end();++i,++j)
            for(const auto& obstacle:blocks)
                need(!crossesInterior(*i,*j,obstacle),"segment crosses obstacle interior");
    }
};
int main() { try {
    World empty;
    const Point start(20,50),near(30,50),far(60,50);
    auto tie=empty.route(start,{{near,0},{near,0}});
    need(tie.Goal==0,"equal goals retain order");empty.valid(tie,start,near);
    const Point symmetric(10,50);
    auto tieDistinct=empty.route(start,{{near,0},{symmetric,0}});
    need(tieDistinct.Goal==0,"distinct equidistant goals retain order");empty.valid(tieDistinct,start,near);
    auto tieReverse=empty.route(start,{{symmetric,0},{near,0}});
    need(tieReverse.Goal==0,"reversed tie follows candidate order");empty.valid(tieReverse,start,symmetric);
    auto cheapNear=empty.route(start,{{near,0},{far,0}});
    need(cheapNear.Goal==0,"shorter equal-penalty route wins");empty.valid(cheapNear,start,near);
    auto penalized=empty.route(start,{{near,100},{far,0}});
    need(penalized.Goal==1,"penalty can outweigh shorter path");empty.valid(penalized,start,far);
    auto disabled=empty.route(start,{{near,-1},{far,0}});
    need(disabled.Goal==1,"negative penalty branch disabled");empty.valid(disabled,start,far);
    auto none=empty.route(start,{});need(!none.Path && none.Goal==size_t(-1),"empty goals fail");
    World maze(true);const Point destination(90,50),blocked(40,30);
    auto mixed=maze.route(start,{{blocked,0},{destination,0}});
    need(mixed.Goal==1,"blocked candidate must not win");maze.valid(mixed,start,destination);
    need(mixed.Path->size()>2,"obstacles require intermediate regions");
    auto onlyBlocked=maze.route(start,{{blocked,0}});
    need(!onlyBlocked.Path && onlyBlocked.Goal==size_t(-1),"all blocked returns failure");
    // Two live branches discover the shared maze in different orders. Their
    // independently valid paths exercise the shared dynamically built graph;
    // these black-box checks do not prove a specific closed-node revisit.
    auto forward=maze.route(start,{{Point(90,20),40},{destination,0}});
    need(forward.Goal==1,"maze weighted winner");maze.valid(forward,start,destination);
    auto reversed=maze.route(start,{{destination,0},{Point(90,20),40}});
    need(reversed.Goal==0,"reordered maze branches retain winning goal");maze.valid(reversed,start,destination);
    std::cout << "weighted native rectangle routing contract: PASS\n";return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n';return 1; } }
