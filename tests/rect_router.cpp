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

    const auto empty_lines = optimize_rect_route_lines({}, {1, 1});
    require(empty_lines.status == RectRouteStatus::found && empty_lines.corridor.empty() &&
            !empty_lines.incoming_direction_applied,
            "empty line optimization is a no-op without an incoming direction");
    const std::vector<RectRouteCorridorEntry> one_line = {{{0, 0, 10, 10}, {1, 1}}};
    const auto single_lines = optimize_rect_route_lines(one_line, {9, 9});
    require(single_lines.status == RectRouteStatus::found && single_lines.corridor.size() == 1 &&
            single_lines.corridor[0].entry_hip.x == 32768 && single_lines.corridor[0].line_id == -1,
            "single rectangle must retain its entry and unassigned LineID");

    const std::vector<RectRouteCorridorEntry> straight = {
        {{0, 0, 10, 10}, {1, 1}}, {{10, 0, 20, 10}, {10, 8}}, {{20, 0, 30, 10}, {20, 8}}};
    const auto lines = optimize_rect_route_lines(straight, {29, 9});
    require(lines.status == RectRouteStatus::found && lines.corridor.size() == 3 &&
            !lines.incoming_direction_applied,
            "ordered adjacent rectangles must support a safe line cut");
    // Exact ray arithmetic for (1,1)->(29,9): t=floor((x-1)*32768/28),
    // yHiP=32768+t*8. These expected crossings retain the fractional component.
    require(lines.corridor[1].entry_hip.x == 327680 && lines.corridor[1].entry_hip.y == 117024 &&
            lines.corridor[2].entry_hip.x == 655360 && lines.corridor[2].entry_hip.y == 210648,
            "safe cut must preserve exact fixed-point crossings, including fractions");
    require(lines.corridor[1].entry.y == 3 && lines.corridor[2].entry.y == 6 &&
            lines.corridor[1].line_id == 0 && lines.corridor[2].line_id == -1,
            "source world entries truncate HiP and LineID excludes the immediate predecessor");
    const std::vector<RectRouteCorridorEntry> duplicate = {
        {{0, 0, 10, 10}, {1, 1}}, {{10, 0, 10, 10}, {10, 5}}, {{10, 0, 20, 10}, {10, 5}}};
    const auto deduplicated = optimize_rect_route_lines(duplicate, {19, 5});
    require(deduplicated.status == RectRouteStatus::found && deduplicated.corridor.size() == 2 &&
            deduplicated.corridor.front().entry.x == 1 &&
            deduplicated.corridor.back().rectangle.x2 == 20,
            "duplicate pass must remove only an intermediate node, preserving first and last");
    const std::vector<RectRouteCorridorEntry> gap = {
        {{0, 0, 10, 10}, {1, 5}}, {{20, 0, 30, 10}, {20, 5}}};
    const auto gap_lines = optimize_rect_route_lines(gap, {25, 5});
    require(gap_lines.status == RectRouteStatus::found && gap_lines.corridor.size() == 2 &&
            gap_lines.corridor[1].entry_hip.x == 20 * 32768 &&
            gap_lines.corridor[1].entry_hip.y == 5 * 32768 && gap_lines.corridor[1].line_id == -1,
            "a ray leaving the previous rectangle outside the next cannot cut across the gap");
    // The direct ray to (18,19) misses the middle rectangle. The previous
    // approved ray to the final rectangle's entry (18,10) must be retained.
    const std::vector<RectRouteCorridorEntry> turn = {
        {{0, 0, 10, 10}, {1, 1}}, {{10, 0, 20, 10}, {10, 8}}, {{10, 10, 20, 20}, {18, 10}}};
    const auto turn_lines = optimize_rect_route_lines(turn, {18, 19});
    require(turn_lines.status == RectRouteStatus::found &&
            turn_lines.corridor[1].entry_hip.x == 10 * 32768 &&
            turn_lines.corridor[1].entry_hip.y == 188891 &&
            turn_lines.corridor[2].entry_hip.x == 18 * 32768 &&
            turn_lines.corridor[2].entry_hip.y == 10 * 32768,
            "failed longer cut must retain the last approved endpoint and its crossings");
    RectRouteLineLimits line_limits;
    line_limits.corridor_entries = 1;
    const auto limited_lines = optimize_rect_route_lines(straight, {29, 9}, line_limits);
    require(limited_lines.status == RectRouteStatus::budget_exhausted && limited_lines.corridor.empty(),
            "line entry budget must fail without partial output");
    line_limits = {}; line_limits.work_units = 5;
    const auto limited_work = optimize_rect_route_lines(straight, {29, 9}, line_limits);
    require(limited_work.status == RectRouteStatus::budget_exhausted && limited_work.corridor.empty(),
            "line work budget must fail without partially optimized output");
    const auto overflow_lines = optimize_rect_route_lines({{{-60000,-1,60000,1}, {-50000,0}},
                                                          {{-60000,-1,60000,1}, {50000,0}}}, {55000,0});
    require(overflow_lines.status == RectRouteStatus::unsupported_coordinates && overflow_lines.corridor.empty(),
            "line source subtraction overflow must fail closed");
    require(optimize_rect_route_lines(one_line, {20, 20}).status == RectRouteStatus::invalid_input,
            "line destination must belong to the final corridor rectangle");
    std::cout << "Rectangle corridor routing checks passed\n";
}
