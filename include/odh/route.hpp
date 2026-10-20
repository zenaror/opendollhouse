#pragma once
#include <cstdint>
#include <vector>
#include <cstddef>

namespace odh {
struct RoutePoint {
    std::int32_t x,y;
    bool operator==(const RoutePoint& p) const { return x==p.x && y==p.y; }
};
// Integer lattice in absolute world units chosen by the adapter (TS1: 1/16 tile).
// A bounded subregion may use a nonzero signed origin. Blocked cells and edges
// must already include the caller's clearance footprint.
class RouteGrid {
public:
    RouteGrid(std::uint32_t width,std::uint32_t height,std::size_t cell_limit=262144,
              RoutePoint origin={0,0});
    bool contains(RoutePoint) const;
    bool block(RoutePoint,bool value=true);
    bool wall(RoutePoint a,RoutePoint b,bool value=true); // Adjacent cardinal cells.
    bool walkable(RoutePoint) const;
    bool can_step(RoutePoint a,RoutePoint b) const;
    std::size_t cells() const {return blocked_.size();}
    std::size_t index(RoutePoint p) const;
    RoutePoint point(std::size_t index) const;
private:
    bool cardinal_step(RoutePoint,RoutePoint) const;
    std::uint32_t width_,height_;
    RoutePoint origin_;
    std::vector<std::uint8_t> blocked_,walls_;
};
enum class RouteStatus { found,no_path,invalid_input,budget_exhausted };
struct RouteLimits {std::size_t expanded=262144,frontier=262144;};
struct RoutePath {
    RouteStatus status;
    std::vector<RoutePoint> points; // Includes start and goal.
    std::uint32_t cost=0; // Grid metric: cardinal10, diagonal14; not duration.
    std::size_t expanded=0;
};
// Multi-goal Dijkstra; deterministic tie order. No room/portal/animation policy.
RoutePath find_route(const RouteGrid&,RoutePoint start,const std::vector<RoutePoint>& goals,
                     RouteLimits limits={});
} // namespace odh
