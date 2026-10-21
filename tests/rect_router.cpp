#include "odh/rect_router.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
bool contains(const odh::RectRouteObstacle& r, odh::RectRoutePoint p) {
    return p.x >= r.x1 && p.x <= r.x2 && p.y >= r.y1 && p.y <= r.y2;
}
std::vector<odh::RectRouteObstacle> room() {
    return {{-16, -16, 116, 0}, {-16, 100, 116, 116},
            {-16, -16, 0, 116}, {100, -16, 116, 116}};
}
void valid_corridor(const odh::RectRouteResult& result,
                    const std::vector<odh::RectRouteObstacle>& obstacles,
                    odh::RectRoutePoint from, odh::RectRoutePoint to) {
    require(result.status == odh::RectRouteStatus::found && !result.corridor.empty(),
            "expected a nonempty found corridor");
    require(result.corridor.front().entry.x == from.x && result.corridor.front().entry.y == from.y,
            "corridor must start at the supplied point");
    require(contains(result.corridor.back().rectangle, to), "destination must be inside the final free rectangle");
    for (const auto& step : result.corridor) {
        require(contains(step.rectangle, step.entry), "each entry must belong to its closed free rectangle");
        for (const auto& obstacle : obstacles) {
            const auto& r = step.rectangle;
            require(r.x1 >= obstacle.x2 || r.x2 <= obstacle.x1 ||
                    r.y1 >= obstacle.y2 || r.y2 <= obstacle.y1,
                    "free corridor must not overlap positive-area obstacle interiors");
        }
    }
}
bool same(const odh::RectRouteResult& a, const odh::RectRouteResult& b) {
    if (a.status != b.status || a.corridor.size() != b.corridor.size()) return false;
    for (std::size_t i = 0; i < a.corridor.size(); ++i) {
        const auto& x = a.corridor[i]; const auto& y = b.corridor[i];
        if (x.entry.x != y.entry.x || x.entry.y != y.entry.y ||
            x.rectangle.x1 != y.rectangle.x1 || x.rectangle.x2 != y.rectangle.x2 ||
            x.rectangle.y1 != y.rectangle.y1 || x.rectangle.y2 != y.rectangle.y2) return false;
    }
    return true;
}
}

int main() {
    using namespace odh;
    auto obstacles = room();
    auto result = route_rectangles(obstacles, {20, 20}, {80, 80}, 0);
    valid_corridor(result, obstacles, {20, 20}, {80, 80});

    // The zero-width passage lies on two obstacle edges; strict positive-area
    // collision must not treat the shared boundary as an interior collision.
    auto edge_passage = room();
    edge_passage.push_back({0, 0, 100, 50});
    edge_passage.push_back({0, 50, 100, 100});
    result = route_rectangles(edge_passage, {20, 50}, {80, 50}, 1);
    valid_corridor(result, edge_passage, {20, 50}, {80, 50});

    auto center = room(); center.push_back({40, 30, 60, 70});
    result = route_rectangles(center, {20, 50}, {40, 50}, 1);
    valid_corridor(result, center, {20, 50}, {40, 50});
    require(route_rectangles(center, {50, 50}, {80, 50}, 1).status == RectRouteStatus::blocked_start,
            "strictly interior start must fail before corridor generation");
    const auto same_point = route_rectangles(center, {50, 50}, {50, 50}, 1);
    require(same_point.status == RectRouteStatus::found && same_point.corridor.empty(),
            "same-point turn-only request must return an empty found corridor");
    require(route_rectangles(center, {20, 50}, {50, 50}, 1).status == RectRouteStatus::no_path,
            "destination inside an obstacle cannot belong to a free rectangle");

    auto divided = room(); divided.push_back({45, 0, 55, 100});
    require(route_rectangles(divided, {20, 50}, {80, 50}, 1).status == RectRouteStatus::no_path,
            "a wall spanning both bounding bands must separate the room");

    const auto top = route_rectangles(center, {20, 50}, {80, 50}, 0);
    const auto bottom = route_rectangles(center, {20, 50}, {80, 50}, 2);
    valid_corridor(top, center, {20, 50}, {80, 50});
    valid_corridor(bottom, center, {20, 50}, {80, 50});
    require(!same(top, bottom), "initial heading must retain the first-expansion route choice");
    bool top_edge = false, bottom_edge = false;
    for (const auto& step : top.corridor) if (step.entry.y <= 30) top_edge = true;
    for (const auto& step : bottom.corridor) if (step.entry.y >= 70) bottom_edge = true;
    require(top_edge && bottom_edge, "opposite initial headings must retain their symmetric obstacle-side choices");
    require(same(top, route_rectangles(center, {20, 50}, {80, 50}, 0)),
            "equal-score route choices must be deterministic for a fixed insertion order");

    RectRouteLimits low;
    low.obstacles = 1;
    result = route_rectangles(center, {20, 50}, {80, 50}, 0, low);
    require(result.status == RectRouteStatus::budget_exhausted && result.corridor.empty(),
            "obstacle budget must fail closed");
    low = {};
    low.work_units = 1;
    result = route_rectangles(center, {20, 50}, {80, 50}, 0, low);
    require(result.status == RectRouteStatus::budget_exhausted && result.corridor.empty(),
            "query budget must fail closed without returning a partial route");
    low = {}; low.rectangles = 1;
    result = route_rectangles(center, {20, 50}, {80, 50}, 0, low);
    require(result.status == RectRouteStatus::budget_exhausted && result.corridor.empty(),
            "rectangle budget must fail closed");
    low = {}; low.free_regions = 1;
    result = route_rectangles(center, {20, 50}, {80, 50}, 0, low);
    require(result.status == RectRouteStatus::budget_exhausted && result.corridor.empty(),
            "free-region budget must fail closed");
    low = {}; low.expansions = 1;
    result = route_rectangles(center, {20, 50}, {80, 50}, 0, low);
    require(result.status == RectRouteStatus::budget_exhausted && result.corridor.empty(),
            "expansion budget must fail closed");

    require(route_rectangles(obstacles, {20, 20}, {80, 80}, 4).status == RectRouteStatus::invalid_input,
            "invalid cardinal must be rejected");
    require(route_rectangles({{5, 5, 1, 1}}, {0, 0}, {2, 2}, 0).status == RectRouteStatus::invalid_input,
            "reversed obstacle input must be rejected");
    require(route_rectangles(obstacles, {100000, 0}, {80, 80}, 0).status == RectRouteStatus::unsupported_coordinates,
            "source HiP coordinate overflow domain must be unsupported");
    const std::vector<RectRouteObstacle> wide = {{-60001,-60001,60001,-60000},
        {-60001,60000,60001,60001},{-60001,-60001,-60000,60001},{60000,-60001,60001,60001}};
    result = route_rectangles(wide, {-50000,0}, {50000,0}, 0);
    require(result.status == RectRouteStatus::unsupported_coordinates && result.corridor.empty(),
            "source int squared-distance overflow must fail without a partial corridor");
    require(route_rectangles({}, {0, 0}, {80, 80}, 0).status == RectRouteStatus::no_path,
            "unbounded nearest-ray extension cannot fabricate a corridor");
    std::vector<RectRouteObstacle> rough;
    for (std::int32_t i = 0; i < 16; ++i) rough.push_back({i, 0, i + 1, 1});
    rough.push_back({100, 0, 101, 1});
    rough.push_back({101, 0, 102, 1});
    require(rough_balance_obstacle_prefix(rough, 16),
            "RoughBalanced must accept the architectural prefix");
    const std::int32_t net10_prefix[] = {4, 3, 9, 13, 12, 10, 5, 15, 2, 1, 11, 0, 8, 7, 6, 14};
    for (std::size_t i = 0; i < 16; ++i)
        require(rough[i].x1 == net10_prefix[i],
                ".NET Random(1) RoughBalanced permutation must match the pinned Simitone runtime");
    require(rough[16].x1 == 100 && rough[17].x1 == 101,
            "RoughBalanced must leave AttemptWalk's appended bounds/entities in place");
    const auto original = rough;
    require(!rough_balance_obstacle_prefix(rough, 19) && rough.size() == original.size(),
            "invalid RoughBalanced prefix must fail without changing vector size");
    for (std::size_t i = 0; i < rough.size(); ++i)
        require(rough[i].x1 == original[i].x1,
                "invalid RoughBalanced prefix must leave input unchanged");
    std::cout << "Rectangle corridor routing checks passed\n";
}
