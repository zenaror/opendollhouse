#include "odh/rect_router.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <new>
#include <utility>

// Own bounded implementation of the corridor mechanics observed at Simitone
// 4906db3156616bfaffc10ce0963cf08062975fef / FreeSO
// f86bbd34b2112296ed786741f640a0535886d180:
// FreeSO/TSOClient/tso.simantics/Engine/Routing/VMRectRouter.cs:18-98,234-466;
// FreeSO/TSOClient/tso.simantics/Model/Routing/VMFreeList.cs:12-69;
// FreeSO/TSOClient/tso.simantics/Model/Routing/VMObstacleSet.cs:119-270,352-360.
// Input obstacle insertion order is explicit; RoughBalanced/.NET Random and
// OptimizeLines are outside this module. This is not original TS1 equivalence.

namespace odh {
namespace {
using Rect = RectRouteObstacle;
using Point = RectRoutePoint;
constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
struct Stop { RectRouteStatus status; };
struct Interval { std::int32_t a, b; };
struct Ref { bool walk; std::size_t index; };
struct TreeNode { Ref ref; unsigned dimension; std::size_t left = none, right = none; };
struct Walk {
    Rect rect;
    std::array<std::vector<Interval>, 4> free;
    std::vector<std::size_t> adjacent;
    std::size_t parent = none;
    Point entry{};
    std::int32_t original_g = 0, g = 0, f = 0;
    std::uint8_t state = 0;
    bool start = false;
};

bool equal(Point a, Point b) { return a.x == b.x && a.y == b.y; }
bool contains(const Rect& r, Point p) {
    return p.x >= r.x1 && p.x <= r.x2 && p.y >= r.y1 && p.y <= r.y2;
}
bool hard_contains(const Rect& r, Point p) {
    return p.x > r.x1 && p.x < r.x2 && p.y > r.y1 && p.y < r.y2;
}
bool intersects(const Rect& a, const Rect& b) {
    return !(b.x1 >= a.x2 || b.x2 <= a.x1 || b.y1 >= a.y2 || b.y2 <= a.y1);
}
bool on_edge(const Rect& a, const Rect& b) {
    return a.x2 == b.x1 || a.x1 == b.x2 || a.y1 == b.y2 || a.y2 == b.y1;
}
Point closest(const Rect& r, Point p) {
    return {std::clamp(p.x, r.x1, r.x2), std::clamp(p.y, r.y1, r.y2)};
}
std::int32_t checked_add(std::int32_t a, std::int32_t b) {
    const auto sum = std::int64_t(a) + b;
    if (sum > std::numeric_limits<std::int32_t>::max())
        throw Stop{RectRouteStatus::unsupported_coordinates};
    return static_cast<std::int32_t>(sum);
}
std::int32_t distance(Point a, Point b) {
    const auto dx = std::int64_t(a.x) - b.x, dy = std::int64_t(a.y) - b.y;
    // The source multiplies/adds signed int before sqrt. Refuse its overflow
    // domain rather than emulate wraparound or silently change route scores.
    const auto squared = dx * dx + dy * dy;
    if (squared > std::numeric_limits<std::int32_t>::max())
        throw Stop{RectRouteStatus::unsupported_coordinates};
    auto value = static_cast<std::int32_t>(std::sqrt(static_cast<double>(squared)));
    while (std::int64_t(value + 1) * (value + 1) <= squared) ++value;
    while (std::int64_t(value) * value > squared) --value;
    return value;
}
bool coordinate_supported(std::int32_t v) {
    const auto hip = std::int64_t(v) * 0x8000;
    return hip >= std::numeric_limits<std::int32_t>::min() &&
           hip <= std::numeric_limits<std::int32_t>::max();
}

class Router {
public:
    Router(const std::vector<Rect>& input, const RectRouteLimits& limits)
        : input_(input), limits_(limits) {
        for (std::size_t i = 0; i < input.size(); ++i) add({false, i});
    }

    RectRouteResult route(Point from, Point to, unsigned heading) {
        const auto initial = make_walk({from.x, from.y, from.x, from.y});
        for (unsigned d = 0; d < 4; ++d)
            set_free(initial, d, {{d % 2 == 0 ? from.x : from.y,
                                   d % 2 == 0 ? from.x : from.y}});
        construct_first(initial);
        walk_[initial].start = true;
        extend_from(initial, heading);
        const auto root = walk_[initial].adjacent.empty() ? initial : walk_[initial].adjacent.front();
        walk_[root].adjacent.clear();
        walk_[root].start = true;
        walk_[root].entry = from;
        std::vector<std::size_t> open{root};
        while (!open.empty()) {
            if (expanded_ >= limits_.expansions) throw Stop{RectRouteStatus::budget_exhausted};
            const auto current = open.front();
            open.erase(open.begin());
            ++expanded_;
            if (contains(walk_[current].rect, to)) {
                RectRouteResult result;
                result.status = RectRouteStatus::found;
                auto at = current;
                if (!equal(to, walk_[at].entry)) result.corridor.push_back({walk_[at].rect, walk_[at].entry});
                auto last = walk_[at].entry;
                while (at != root) {
                    at = walk_[at].parent;
                    if (at == none) throw Stop{RectRouteStatus::invalid_input};
                    if (!equal(last, walk_[at].entry)) result.corridor.push_back({walk_[at].rect, walk_[at].entry});
                    last = walk_[at].entry;
                }
                std::reverse(result.corridor.begin(), result.corridor.end());
                finish(result);
                return result;
            }
            walk_[current].state = 2;
            for (unsigned d = 0; d < 4; ++d) extend_from(current, d);
            for (const auto next : walk_[current].adjacent) {
                spend();
                if (walk_[next].state == 2) continue;
                const bool newcomer = walk_[next].state == 0;
                const auto crossing = shared_crossing(walk_[next].rect, walk_[current].rect, walk_[current].entry);
                const auto original_g = checked_add(walk_[current].original_g, distance(walk_[current].entry, crossing));
                const auto near_target = closest(walk_[next].rect, to);
                const auto g = checked_add(original_g, distance(crossing, near_target));
                if (newcomer || g < walk_[next].g) {
                    auto& node = walk_[next];
                    node.state = 1;
                    node.entry = crossing;
                    node.parent = current;
                    node.original_g = original_g;
                    node.g = g;
                    node.f = checked_add(g, distance(near_target, to));
                    if (!newcomer) open.erase(std::remove(open.begin(), open.end(), next), open.end());
                    const auto position = std::find_if(open.begin(), open.end(), [&](std::size_t id) {
                        spend();
                        return walk_[id].f > node.f;
                    });
                    open.insert(position, next);
                }
            }
        }
        RectRouteResult result;
        result.status = RectRouteStatus::no_path;
        finish(result);
        return result;
    }

    void finish(RectRouteResult& r) const {
        r.generated_rectangles = walk_.size();
        r.expanded_rectangles = expanded_;
        r.work_units = visits_;
    }

private:
    const std::vector<Rect>& input_;
    const RectRouteLimits& limits_;
    std::deque<Walk> walk_;
    std::vector<TreeNode> tree_;
    std::size_t regions_ = 0, expanded_ = 0, visits_ = 0;

    void spend() {
        if (visits_ >= limits_.work_units) throw Stop{RectRouteStatus::budget_exhausted};
        ++visits_;
    }

    const Rect& rect(Ref r) const { return r.walk ? walk_[r.index].rect : input_[r.index]; }
    static std::int32_t dimension(const Rect& r, unsigned d) {
        switch (d) { case 0: return r.y1; case 1: return r.x1; case 2: return r.y2; default: return r.x2; }
    }
    void add(Ref r) {
        if (tree_.empty()) { spend(); tree_.push_back({r, 1}); return; }
        std::size_t at = 0;
        while (true) {
            spend();
            const bool right = dimension(rect(r), tree_[at].dimension) >
                               dimension(rect(tree_[at].ref), tree_[at].dimension);
            const auto child = right ? tree_[at].right : tree_[at].left;
            if (child != none) { at = child; continue; }
            const auto id = tree_.size();
            const auto next_dimension = (tree_[at].dimension + 1) % 4;
            tree_.push_back({r, next_dimension});
            if (right) tree_[at].right = id; else tree_[at].left = id;
            return;
        }
    }
    std::vector<Ref> query(const Rect& search, bool edge) {
        std::vector<Ref> result;
        if (tree_.empty()) return result;
        std::vector<std::size_t> stack{0};
        while (!stack.empty()) {
            spend();
            const auto at = stack.back(); stack.pop_back();
            const auto& node = tree_[at];
            const auto& r = rect(node.ref);
            if (edge ? on_edge(r, search) : intersects(r, search)) result.push_back(node.ref);
            unsigned omit = 0;
            switch (node.dimension) {
            case 0: if (edge ? search.y2 < r.y1 : search.y2 <= r.y1) omit = 2; break;
            case 1: if (edge ? search.x2 < r.x1 : search.x2 <= r.x1) omit = 2; break;
            case 2: if (edge ? search.y1 > r.y2 : search.y1 >= r.y2) omit = 1; break;
            case 3: if (edge ? search.x1 > r.x2 : search.x1 >= r.x2) omit = 1; break;
            }
            if (omit != 2 && node.right != none) stack.push_back(node.right);
            if (omit != 1 && node.left != none) stack.push_back(node.left);
        }
        return result;
    }
    std::size_t make_walk(Rect r) {
        if (walk_.size() >= limits_.rectangles) throw Stop{RectRouteStatus::budget_exhausted};
        walk_.push_back(Walk{});
        walk_.back().rect = r;
        return walk_.size() - 1;
    }
    void set_free(std::size_t id, unsigned d, std::vector<Interval> value) {
        const auto old = walk_[id].free[d].size();
        if (value.size() > limits_.free_regions || regions_ - old > limits_.free_regions - value.size())
            throw Stop{RectRouteStatus::budget_exhausted};
        regions_ = regions_ - old + value.size();
        walk_[id].free[d] = std::move(value);
    }
    void subtract(std::size_t id, unsigned d, Interval removed) {
        std::vector<Interval> result;
        for (const auto part : walk_[id].free[d]) {
            spend();
            if (removed.a >= part.b || removed.b <= part.a) result.push_back(part);
            else if (part.a >= removed.a && removed.b < part.b) result.push_back({removed.b, part.b});
            else if (removed.b >= part.b && removed.a > part.a) result.push_back({part.a, removed.a});
            else if (removed.a > part.a && removed.b < part.b) {
                result.push_back({part.a, removed.a}); result.push_back({removed.b, part.b});
            }
        }
        set_free(id, d, std::move(result));
    }
    void connect(std::size_t a, std::size_t b) {
        // Each connection consumes work too; repeated adjacency is preserved.
        spend();
        walk_[a].adjacent.push_back(b); walk_[b].adjacent.push_back(a);
    }
    void construct_first(std::size_t id) {
        const auto r = walk_[id].rect;
        for (const auto ref : query(r, true)) {
            const auto& other = rect(ref);
            if (other.y2 == r.y1 && !(other.x2 <= r.x1 || other.x1 >= r.x2)) subtract(id, 0, {other.x1, other.x2});
            if (other.x1 == r.x2 && !(other.y2 <= r.y1 || other.y1 >= r.y2)) subtract(id, 1, {other.y1, other.y2});
            if (other.y1 == r.y2 && !(other.x2 <= r.x1 || other.x1 >= r.x2)) subtract(id, 2, {other.x1, other.x2});
            if (other.x2 == r.x1 && !(other.y2 <= r.y1 || other.y1 >= r.y2)) subtract(id, 3, {other.y1, other.y2});
        }
    }
    void construct_free(std::size_t id, const std::array<bool, 4>& enabled) {
        const auto r = walk_[id].rect;
        for (unsigned d = 0; d < 4; ++d) if (enabled[d]) {
            const auto a = d % 2 == 0 ? r.x1 : r.y1, b = d % 2 == 0 ? r.x2 : r.y2;
            set_free(id, d, a == b ? std::vector<Interval>{} : std::vector<Interval>{{a, b}});
        }
        for (const auto ref : query(r, true)) {
            if (ref.walk && ref.index == id) continue;
            const auto other = rect(ref);
            const bool touches[] = {
                other.y2 == r.y1 && !(other.x2 <= r.x1 || other.x1 >= r.x2),
                other.x1 == r.x2 && !(other.y2 <= r.y1 || other.y1 >= r.y2),
                other.y1 == r.y2 && !(other.x2 <= r.x1 || other.x1 >= r.x2),
                other.x2 == r.x1 && !(other.y2 <= r.y1 || other.y1 >= r.y2)};
            for (unsigned d = 0; d < 4; ++d) if (enabled[d] && touches[d]) {
                subtract(id, d, d % 2 == 0 ? Interval{other.x1, other.x2} : Interval{other.y1, other.y2});
                if (ref.walk) {
                    subtract(ref.index, (d + 2) % 4, d % 2 == 0 ? Interval{r.x1, r.x2} : Interval{r.y1, r.y2});
                    connect(id, ref.index);
                }
            }
        }
    }
    struct Extension { std::int32_t boundary; std::vector<Ref> best; };
    Extension extend(unsigned d, Interval line, std::int32_t p) {
        const auto low = std::numeric_limits<std::int32_t>::min();
        const auto high = std::numeric_limits<std::int32_t>::max();
        Extension result{d == 0 || d == 3 ? low : high, {}};
        Rect range{};
        switch (d) {
        case 0: range = {line.a, low, line.b, p}; break;
        case 1: range = {p, line.a, high, line.b}; break;
        case 2: range = {line.a, p, line.b, high}; break;
        case 3: range = {low, line.a, p, line.b}; break;
        }
        for (const auto ref : query(range, false)) {
            const auto& r = rect(ref);
            std::int32_t boundary = 0;
            if (d == 0 || d == 2) {
                if (r.x1 >= line.b || r.x2 <= line.a) continue;
                boundary = d == 0 ? r.y2 : r.y1;
            } else {
                if (r.y1 >= line.b || r.y2 <= line.a) continue;
                boundary = d == 1 ? r.x1 : r.x2;
            }
            if ((d == 0 || d == 3) ? boundary > p : boundary < p) continue;
            const bool better = d == 0 || d == 3 ? boundary > result.boundary : boundary < result.boundary;
            if (better) { result.boundary = boundary; result.best.clear(); result.best.push_back(ref); }
            else if (boundary == result.boundary) result.best.push_back(ref);
        }
        return result;
    }
    void extend_from(std::size_t source, unsigned d) {
        const auto free = walk_[source].free[d];
        const auto source_rect = walk_[source].rect;
        const auto p = d == 0 ? source_rect.y1 : d == 1 ? source_rect.x2 : d == 2 ? source_rect.y2 : source_rect.x1;
        for (const auto line : free) {
            const auto extension = extend(d, line, p);
            const auto boundary = extension.boundary;
            if (boundary == p || boundary == std::numeric_limits<std::int32_t>::min() ||
                boundary == std::numeric_limits<std::int32_t>::max()) continue;
            Rect r{};
            switch (d) {
            case 0: r = {line.a, boundary, line.b, p}; break;
            case 1: r = {p, line.a, boundary, line.b}; break;
            case 2: r = {line.a, p, line.b, boundary}; break;
            case 3: r = {boundary, line.a, p, line.b}; break;
            }
            const auto id = make_walk(r);
            connect(source, id);
            const Interval bounds = d % 2 == 0 ? Interval{r.x1, r.x2} : Interval{r.y1, r.y2};
            set_free(id, d, bounds.a == bounds.b ? std::vector<Interval>{} : std::vector<Interval>{{bounds}});
            for (const auto ref : extension.best) {
                const auto obstacle = rect(ref);
                subtract(id, d, d % 2 == 0 ? Interval{obstacle.x1, obstacle.x2} : Interval{obstacle.y1, obstacle.y2});
                if (ref.walk) { subtract(ref.index, (d + 2) % 4, bounds); connect(ref.index, id); }
            }
            set_free(id, (d + 2) % 4, {});
            construct_free(id, {d % 2 == 1, d % 2 == 0, d % 2 == 1, d % 2 == 0});
            if (!walk_[source].start) add({true, id});
        }
    }
    static Point shared_crossing(const Rect& a, const Rect& b, Point p) {
        bool vertical = true;
        std::int32_t edge = 0;
        if (a.x1 == b.x2) { vertical = false; edge = a.x1; }
        if (a.x2 == b.x1) { vertical = false; edge = a.x2; }
        if (a.y1 == b.y2) { vertical = true; edge = a.y1; }
        if (a.y2 == b.y1) { vertical = true; edge = a.y2; }
        if (vertical) return {std::clamp(p.x, std::max(a.x1, b.x1), std::min(a.x2, b.x2)), edge};
        return {edge, std::clamp(p.y, std::max(a.y1, b.y1), std::min(a.y2, b.y2))};
    }
};
} // namespace

RectRouteResult route_rectangles(const std::vector<RectRouteObstacle>& input,
                                 RectRoutePoint from, RectRoutePoint to,
                                 std::uint8_t heading, const RectRouteLimits& limits) {
    RectRouteResult result;
    if (heading > 3 || !limits.obstacles || limits.obstacles > 16384 ||
        !limits.rectangles || limits.rectangles > 16384 || !limits.expansions || limits.expansions > 16384 ||
        !limits.free_regions || limits.free_regions > 1048576 || !limits.work_units || limits.work_units > 16777216)
        return result;
    if (input.size() > limits.obstacles) {
        result.status = RectRouteStatus::budget_exhausted; return result;
    }
    for (const auto& r : input) {
        if (r.x1 >= r.x2 || r.y1 >= r.y2) return result;
        if (!coordinate_supported(r.x1) || !coordinate_supported(r.x2) ||
            !coordinate_supported(r.y1) || !coordinate_supported(r.y2)) {
            result.status = RectRouteStatus::unsupported_coordinates; return result;
        }
    }
    if (!coordinate_supported(from.x) || !coordinate_supported(from.y) ||
        !coordinate_supported(to.x) || !coordinate_supported(to.y)) {
        result.status = RectRouteStatus::unsupported_coordinates; return result;
    }
    if (equal(from, to)) { result.status = RectRouteStatus::found; return result; }
    for (const auto& r : input) if (hard_contains(r, from)) {
        result.status = RectRouteStatus::blocked_start; return result;
    }
    try {
        Router router(input, limits);
        try { return router.route(from, to, heading); }
        catch (const Stop& stop) { result.status = stop.status; router.finish(result); }
    } catch (const Stop& stop) { result.status = stop.status; }
      catch (const std::bad_alloc&) { result.status = RectRouteStatus::budget_exhausted; }
    return result;
}

namespace {
class DotNetSeedOneRandom {
public:
    DotNetSeedOneRandom() {
        constexpr std::int32_t mbig = 2147483647;
        constexpr std::int32_t mseed = 161803398;
        std::int32_t mj = mseed - 1;
        seed_[55] = mj;
        std::int32_t mk = 1;
        for (std::int32_t i = 1; i < 55; ++i) {
            const auto ii = (21 * i) % 55;
            seed_[static_cast<std::size_t>(ii)] = mk;
            mk = mj - mk;
            if (mk < 0) mk += mbig;
            mj = seed_[static_cast<std::size_t>(ii)];
        }
        for (std::int32_t pass = 1; pass < 5; ++pass) {
            for (std::int32_t i = 1; i < 56; ++i) {
                seed_[static_cast<std::size_t>(i)] -=
                    seed_[static_cast<std::size_t>(1 + (i + 30) % 55)];
                if (seed_[static_cast<std::size_t>(i)] < 0)
                    seed_[static_cast<std::size_t>(i)] += mbig;
            }
        }
    }

    std::int32_t next(std::int32_t maximum) {
        constexpr std::int32_t mbig = 2147483647;
        if (maximum <= 0) return 0;
        if (++inext_ >= 56) inext_ = 1;
        if (++inextp_ >= 56) inextp_ = 1;
        auto sample = seed_[static_cast<std::size_t>(inext_)] -
                      seed_[static_cast<std::size_t>(inextp_)];
        if (sample == mbig) --sample;
        if (sample < 0) sample += mbig;
        seed_[static_cast<std::size_t>(inext_)] = sample;
        return static_cast<std::int32_t>((static_cast<double>(sample) / mbig) * maximum);
    }

private:
    std::array<std::int32_t, 56> seed_{};
    std::int32_t inext_ = 0;
    std::int32_t inextp_ = 21;
};
} // namespace

bool rough_balance_obstacle_prefix(std::vector<RectRouteObstacle>& obstacles,
                                   std::size_t architectural_count) {
    if (architectural_count > obstacles.size() ||
        architectural_count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return false;
    DotNetSeedOneRandom random;
    for (std::size_t i = 1; i < architectural_count; ++i) {
        const auto remaining = architectural_count - i;
        const auto index = i + static_cast<std::size_t>(random.next(static_cast<std::int32_t>(remaining)));
        std::swap(obstacles[i - 1], obstacles[index]);
    }
    return true;
}

const char* rect_route_status_name(RectRouteStatus status) noexcept {
    switch (status) {
    case RectRouteStatus::found: return "found";
    case RectRouteStatus::no_path: return "no_path";
    case RectRouteStatus::blocked_start: return "blocked_start";
    case RectRouteStatus::invalid_input: return "invalid_input";
    case RectRouteStatus::unsupported_coordinates: return "unsupported_coordinates";
    case RectRouteStatus::budget_exhausted: return "budget_exhausted";
    }
    return "invalid_status";
}
} // namespace odh
