#pragma once
#include <cstdint>
#include <vector>
#include <cstddef>
#include <optional>
#include <memory>
#include <array>

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
    std::size_t cells() const {return pages_.empty()?0:cells_;}
    // Pure value comparison; no hash or shared room label grants equivalence.
    bool same_contents(const RouteGrid&) const;
    // Derived grid bytes only. Equal pages share; later block/wall changes
    // detach their pages before publication, preserving ordinary value copies.
    void share_identical_pages_from(const std::vector<const RouteGrid*>&);
    static std::size_t storage_cells(const std::vector<const RouteGrid*>&);
    std::size_t index(RoutePoint p) const;
    RoutePoint point(std::size_t index) const;
private:
    bool cardinal_step(RoutePoint,RoutePoint) const;
    std::uint32_t width_,height_;
    RoutePoint origin_;
    static constexpr std::size_t page_cells=4096;
    struct Page {std::array<std::uint8_t,page_cells> blocked{},walls{};};
    std::size_t cells_=0;
    std::vector<std::shared_ptr<Page>> pages_;
    void make_page_writable(std::size_t);
    std::uint8_t blocked(std::size_t n) const {return pages_[n/page_cells]->blocked[n%page_cells];}
    std::uint8_t walls(std::size_t n) const {return pages_[n/page_cells]->walls[n%page_cells];}
};
// Coalesce exact immutable derived page values, never controllers or jobs.
// The returned count charges complete allocated pages, including tail padding.
std::size_t coalesce_route_grid_cache(const std::vector<RouteGrid*>&);
enum class RouteStatus { found,no_path,invalid_input,budget_exhausted };
struct RouteLimits {std::size_t expanded=262144,frontier=262144,candidates=4096;};
struct RoutePath {
    RouteStatus status;
    std::vector<RoutePoint> points; // Includes start and goal.
    std::uint32_t cost=0; // Grid metric: cardinal10, diagonal14; not duration.
    std::size_t expanded=0;
};
struct RankedRoutePath {
    RoutePath path;
    std::optional<std::size_t> selected;
};
// Multi-goal Dijkstra; deterministic tie order. No room/portal/animation policy.
RoutePath find_route(const RouteGrid&,RoutePoint start,const std::vector<RoutePoint>& goals,
                     RouteLimits limits={});
// One Dijkstra traversal; among reachable goals, prefer the earliest input.
// If a higher-ranked goal is disconnected, search the reachable component once.
RankedRoutePath find_ranked_route(const RouteGrid&,RoutePoint start,
                                 const std::vector<RoutePoint>& goals,RouteLimits limits={});
} // namespace odh
