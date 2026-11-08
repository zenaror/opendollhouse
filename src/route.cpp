#include "odh/route.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace odh {
namespace {
constexpr std::array<RoutePoint,8> directions={{{0,-1},{1,0},{0,1},{-1,0},{1,-1},{1,1},{-1,1},{-1,-1}}};
int cardinal_direction(RoutePoint a,RoutePoint b) {
    for(int n=0;n<4;++n) if(a.x+directions[n].x==b.x && a.y+directions[n].y==b.y) return n;
    return -1;
}
}
RouteGrid::RouteGrid(std::uint32_t width,std::uint32_t height,std::size_t limit,RoutePoint origin)
    :width_(width),height_(height),origin_(origin) {
    const auto cells=static_cast<std::uint64_t>(width)*height;
    if(!width || !height || width>32767 || height>32767 || cells>limit ||
       cells>std::numeric_limits<std::uint32_t>::max()/14U ||
       static_cast<std::int64_t>(origin.x)+width-1>std::numeric_limits<std::int32_t>::max() ||
       static_cast<std::int64_t>(origin.y)+height-1>std::numeric_limits<std::int32_t>::max())
        throw std::invalid_argument("route grid exceeds dimensions or cell budget");
    cells_=static_cast<std::size_t>(cells);
    pages_.reserve((cells_+page_cells-1)/page_cells);
    for(std::size_t offset=0;offset<cells_;offset+=page_cells){
        pages_.push_back(std::make_shared<Page>());
    }
}
void RouteGrid::make_page_writable(std::size_t page){
    if(!pages_[page].unique())pages_[page]=std::make_shared<Page>(*pages_[page]);
}
bool RouteGrid::same_contents(const RouteGrid& other) const {
    if(width_!=other.width_||height_!=other.height_||!(origin_==other.origin_)||cells_!=other.cells_||pages_.size()!=other.pages_.size())return false;
    for(std::size_t n=0;n<pages_.size();++n)
        if(pages_[n]!=other.pages_[n]&&(pages_[n]->blocked!=other.pages_[n]->blocked||pages_[n]->walls!=other.pages_[n]->walls))return false;
    return true;
}
void RouteGrid::share_identical_pages_from(const std::vector<const RouteGrid*>& grids){
    const auto fingerprint=[](const Page& page){
        std::uint64_t hash=14695981039346656037ULL;
        for(const auto& bank:{&page.blocked,&page.walls})for(const auto value:*bank){hash^=value;hash*=1099511628211ULL;}
        hash^=page.blocked.size();return hash;
    };
    std::unordered_map<std::uint64_t,std::vector<const std::shared_ptr<Page>*>> candidates;
    for(const auto* grid:grids)if(grid&&grid!=this)for(const auto& page:grid->pages_)
        candidates[fingerprint(*page)].push_back(&page);
    for(auto& page:pages_){const auto found=candidates.find(fingerprint(*page));if(found==candidates.end())continue;
        for(const auto* other:found->second)
            if(page==*other||(page->blocked==(*other)->blocked&&page->walls==(*other)->walls)){
                page=*other;break; // Hash is only a filter; BOTH banks match byte for byte.
            }
    }
}
std::size_t RouteGrid::storage_cells(const std::vector<const RouteGrid*>& grids){
    std::unordered_set<const Page*> seen;std::size_t total=0;
    for(const auto* grid:grids)if(grid)for(const auto& page:grid->pages_)if(seen.insert(page.get()).second){
        if(page->blocked.size()>std::numeric_limits<std::size_t>::max()-total)
            throw std::length_error("route grid physical cell accounting overflow");
        total+=page->blocked.size();
    }
    return total;
}
std::size_t coalesce_route_grid_cache(const std::vector<RouteGrid*>& grids){
    std::vector<const RouteGrid*> previous;
    for(auto* grid:grids)if(grid&&std::find(previous.begin(),previous.end(),grid)==previous.end()){
        grid->share_identical_pages_from(previous);previous.push_back(grid);
    }
    return RouteGrid::storage_cells(previous);
}
bool RouteGrid::contains(RoutePoint p) const {
    if(pages_.empty())return false;
    const auto x=static_cast<std::int64_t>(p.x)-origin_.x;
    const auto y=static_cast<std::int64_t>(p.y)-origin_.y;
    return x>=0 && y>=0 && static_cast<std::uint64_t>(x)<width_ && static_cast<std::uint64_t>(y)<height_;
}
std::size_t RouteGrid::index(RoutePoint p) const {
    if(!contains(p)) throw std::out_of_range("route point outside grid");
    return static_cast<std::size_t>(static_cast<std::int64_t>(p.y)-origin_.y)*width_+
           static_cast<std::size_t>(static_cast<std::int64_t>(p.x)-origin_.x);
}
RoutePoint RouteGrid::point(std::size_t n) const {
    if(n>=cells()) throw std::out_of_range("route index outside grid");
    return {static_cast<std::int32_t>(static_cast<std::int64_t>(origin_.x)+static_cast<std::int64_t>(n%width_)),
            static_cast<std::int32_t>(static_cast<std::int64_t>(origin_.y)+static_cast<std::int64_t>(n/width_))};
}
bool RouteGrid::block(RoutePoint p,bool value) {
    if(!contains(p)) return false;
    const auto n=index(p);make_page_writable(n/page_cells);
    pages_[n/page_cells]->blocked[n%page_cells]=static_cast<std::uint8_t>(value);return true;
}
bool RouteGrid::wall(RoutePoint a,RoutePoint b,bool value) {
    if(!contains(a) || !contains(b)) return false;
    const auto d=cardinal_direction(a,b);
    if(d<0) return false;
    const auto reverse=(d+2)%4;
    // Prepare both page copies before changing either reciprocal wall bit.
    make_page_writable(index(a)/page_cells);make_page_writable(index(b)/page_cells);
    for(const auto& pair:{std::pair<std::size_t,int>{index(a),d},{index(b),reverse}}) {
        const auto bit=static_cast<std::uint8_t>(1U<<pair.second);
        auto& flags=pages_[pair.first/page_cells]->walls[pair.first%page_cells];
        if(value)flags|=bit;
        else flags&=static_cast<std::uint8_t>(~bit);
    }
    return true;
}
bool RouteGrid::walkable(RoutePoint p) const {return contains(p) && blocked(index(p))==0;}
bool RouteGrid::cardinal_step(RoutePoint a,RoutePoint b) const {
    if(!walkable(a) || !walkable(b)) return false;
    const auto d=cardinal_direction(a,b);
    return d>=0 && (walls(index(a))&(1U<<d))==0;
}
bool RouteGrid::can_step(RoutePoint a,RoutePoint b) const {
    if(!walkable(a) || !walkable(b)) return false;
    const auto dx=b.x-a.x,dy=b.y-a.y;
    if(dx==0 || dy==0) return cardinal_step(a,b);
    if((dx!=1 && dx!=-1) || (dy!=1 && dy!=-1)) return false;
    const RoutePoint horizontal{b.x,a.y},vertical{a.x,b.y};
    // Both orthogonal passages must be clear: no cutting corners or wall ends.
    return cardinal_step(a,horizontal) && cardinal_step(horizontal,b) &&
           cardinal_step(a,vertical) && cardinal_step(vertical,b);
}
namespace {
RankedRoutePath route_search(const RouteGrid& grid,RoutePoint start,const std::vector<RoutePoint>& goals,
                             RouteLimits limits,bool ranked) {
    RankedRoutePath result{{RouteStatus::invalid_input,{},0,0},std::nullopt};
    if(!grid.walkable(start) || goals.empty()) return result;
    if(ranked&&(goals.size()>limits.candidates||
       goals.size()>std::numeric_limits<std::uint16_t>::max())) {
        result.path.status=RouteStatus::budget_exhausted;return result;
    }
    constexpr auto no_goal=std::numeric_limits<std::uint16_t>::max();
    std::vector<std::uint8_t> target;
    std::vector<std::uint16_t> target_rank;
    if(ranked)target_rank.assign(grid.cells(),no_goal);
    else target.resize(grid.cells());
    std::vector<std::uint8_t> closed(grid.cells());
    std::uint16_t best_possible_rank=no_goal;
    bool any=false;
    for(std::size_t rank=0;rank<goals.size();++rank) {
        if(!grid.walkable(goals[rank])) continue;
        any=true;
        const auto n=grid.index(goals[rank]);
        if(ranked) {
            target_rank[n]=std::min(target_rank[n],static_cast<std::uint16_t>(rank));
            best_possible_rank=std::min(best_possible_rank,static_cast<std::uint16_t>(rank));
        } else target[n]=1;
    }
    result.path.status=RouteStatus::no_path;
    if(!any) return result;
    constexpr auto infinity=std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> distance(grid.cells(),infinity);
    // A predecessor is one of the eight adjacent steps, not a size_t cell
    // index. This keeps the largest per-cell scratch compact on 32/64-bit hosts.
    std::vector<std::uint8_t> parent_direction(grid.cells(),0xFF);
    // RouteGrid bounds the cell count below uint32 max, so pack (cost,index)
    // into one 64-bit key. This preserves deterministic cost/index ordering
    // without the padding of pair<uint32_t,size_t> on 64-bit hosts.
    using Entry=std::uint64_t;
    const auto entry_key=[](std::uint32_t cost,std::size_t index) {
        return (std::uint64_t(cost)<<32)|static_cast<std::uint32_t>(index);
    };
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> frontier;
    if(limits.frontier==0) {result.path.status=RouteStatus::budget_exhausted;return result;}
    const auto first=grid.index(start);distance[first]=0;frontier.push(entry_key(0,first));
    std::size_t selected_node=grid.cells();
    while(!frontier.empty()) {
        const auto entry=frontier.top();frontier.pop();
        const auto entry_cost=static_cast<std::uint32_t>(entry>>32);
        const auto n=static_cast<std::size_t>(static_cast<std::uint32_t>(entry));
        if(closed[n] || entry_cost!=distance[n]) continue;
        if(result.path.expanded>=limits.expanded) {result.path.status=RouteStatus::budget_exhausted;return result;}
        closed[n]=1;++result.path.expanded;
        const bool is_target=ranked?target_rank[n]!=no_goal:target[n]!=0;
        if(is_target && (!ranked || target_rank[n]==best_possible_rank)) {
            selected_node=n;
            if(ranked)result.selected=target_rank[n];
            break;
        }
        const auto p=grid.point(n);
        for(std::size_t d=0;d<directions.size();++d) {
            const RoutePoint next{p.x+directions[d].x,p.y+directions[d].y};
            if(!grid.can_step(p,next)) continue;
            const auto m=grid.index(next);
            const auto cost=d<4?10U:14U;
            if(closed[m] || distance[n]>infinity-cost) continue;
            const auto candidate=distance[n]+cost;
            if(candidate>=distance[m]) continue;
            if(frontier.size()>=limits.frontier) {result.path.status=RouteStatus::budget_exhausted;return result;}
            distance[m]=candidate;parent_direction[m]=static_cast<std::uint8_t>(d);frontier.push(entry_key(candidate,m));
        }
    }
    if(selected_node==grid.cells() && ranked) {
        for(std::size_t n=0;n<grid.cells();++n)
            if(closed[n]&&target_rank[n]!=no_goal&&
               (selected_node==grid.cells()||target_rank[n]<*result.selected)) {
                selected_node=n;result.selected=target_rank[n];
            }
    }
    if(selected_node==grid.cells()) return result;
    auto at=selected_node;
    for(std::size_t chain=0;chain<grid.cells();++chain) {
        result.path.points.push_back(grid.point(at));
        if(at==first) break;
        const auto direction=parent_direction[at];
        if(direction>=directions.size()) {
            result.path.points.clear();result.path.status=RouteStatus::invalid_input;
            result.selected.reset();return result;
        }
        const auto current=grid.point(at);
        const RoutePoint previous{current.x-directions[direction].x,current.y-directions[direction].y};
        if(!grid.contains(previous)) {
            result.path.points.clear();result.path.status=RouteStatus::invalid_input;
            result.selected.reset();return result;
        }
        at=grid.index(previous);
    }
    if(result.path.points.empty()||!(result.path.points.back()==start)) {
        result.path.points.clear();result.path.status=RouteStatus::invalid_input;
        result.selected.reset();return result;
    }
    std::reverse(result.path.points.begin(),result.path.points.end());
    result.path.cost=distance[selected_node];result.path.status=RouteStatus::found;
    return result;
}
} // namespace
RoutePath find_route(const RouteGrid& grid,RoutePoint start,const std::vector<RoutePoint>& goals,RouteLimits limits) {
    return route_search(grid,start,goals,limits,false).path;
}
RankedRoutePath find_ranked_route(const RouteGrid& grid,RoutePoint start,
                                  const std::vector<RoutePoint>& goals,RouteLimits limits) {
    return route_search(grid,start,goals,limits,true);
}
} // namespace odh
