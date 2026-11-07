#include "odh/route_geometry.hpp"
#include "odh/prepared_architecture.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::SavedArchitecture lot(std::uint32_t width=5, std::uint32_t height=5) {
    odh::SavedArchitecture result;
    result.width=width; result.height=height;
    const auto cells=std::size_t(width)*height;
    for (auto& level:result.levels) {
        level.floors.resize(cells);
        level.walls.resize(cells);
    }
    for (std::size_t i=0;i<cells;++i) {
        result.levels[0].floors[i]=static_cast<std::uint16_t>(100+i);
        result.levels[1].floors[i]=static_cast<std::uint16_t>(200+i);
    }
    return result;
}
std::size_t at(const odh::SavedArchitecture& a,int x,int y) {
    return std::size_t(y)*a.width+static_cast<std::size_t>(x);
}
}
int main() {
    try {
        auto architecture=lot();
        require(odh::saved_precise_floor(architecture,16+15,32+2,1)==111,
                "ordinary precise floor uses row-major tile and local coordinates");
        require(odh::saved_precise_floor(architecture,16+15,32+2,2)==211,
                "precise floor selects requested level");
        architecture.levels[0].floors[at(architecture,1,2)]=0;
        require(odh::saved_precise_floor(architecture,16,32,1)==0,
                "zero floor pattern remains a valid value");
        auto& wall=architecture.levels[0].walls[at(architecture,1,2)];
        wall.segments=32; wall.top_left_style=0x1234; wall.top_left_pattern=0x5678;
        require(odh::saved_precise_floor(architecture,16+10,32+5,1)==0x5678,
                "vertical diagonal chooses pattern when local x exceeds local y");
        require(odh::saved_precise_floor(architecture,16+5,32+10,1)==0x1234,
                "vertical diagonal chooses style otherwise");
        wall.segments=16;
        require(odh::saved_precise_floor(architecture,16+8,32+8,1)==0x5678,
                "horizontal diagonal uses strict local sum greater than fifteen");
        require(odh::saved_precise_floor(architecture,16+8,32+7,1)==0x1234,
                "horizontal diagonal uses style at the threshold");
        wall.segments=32|16;
        require(odh::saved_precise_floor(architecture,16+10,32+5,1)==0x5678,
                "vertical diagonal takes precedence when both bits are set");
        require(!odh::saved_precise_floor(architecture,-1,0,1)&&
                !odh::saved_precise_floor(architecture,80,0,1)&&
                !odh::saved_precise_floor(architecture,0,0,0),
                "invalid precise-floor coordinates and levels are unsupported");

        odh::PreparedArchitecture prepared;
        prepared.width=prepared.height=2;
        for(auto& level:prepared.levels){level.floors={0,11,12,13};level.walls.resize(4);}
        require(odh::prepared_precise_floor(prepared,16,16,1)==13&&
                odh::prepared_precise_floor(prepared,16,16,2)==13&&
                odh::prepared_precise_floor(prepared,0,0,1)==0,
                "prepared precise floor reads row-major values on either level and preserves zero");
        auto& prepared_vertical=prepared.levels[0].walls[1];
        prepared_vertical.segments=odh::wall_vertical_diagonal;
        prepared_vertical.top_left_style=21;prepared_vertical.top_left_pattern=22;
        require(odh::prepared_precise_floor(prepared,16+4,5,1)==21&&
                odh::prepared_precise_floor(prepared,16+5,5,1)==21&&
                odh::prepared_precise_floor(prepared,16+6,5,1)==22,
                "prepared vertical diagonal uses strict x-minus-y threshold");
        auto& prepared_horizontal=prepared.levels[1].walls[2];
        prepared_horizontal.segments=odh::wall_horizontal_diagonal;
        prepared_horizontal.top_left_style=31;prepared_horizontal.top_left_pattern=32;
        require(odh::prepared_precise_floor(prepared,8,16+6,2)==31&&
                odh::prepared_precise_floor(prepared,8,16+7,2)==31&&
                odh::prepared_precise_floor(prepared,8,16+8,2)==32,
                "prepared horizontal diagonal uses strict sum threshold");
        auto malformed_prepared=prepared;malformed_prepared.levels[1].floors.pop_back();
        require(!odh::prepared_precise_floor(malformed_prepared,0,0,1)&&
                !odh::prepared_precise_floor(prepared,-1,0,1)&&
                !odh::prepared_precise_floor(prepared,0,32,1)&&
                !odh::prepared_precise_floor(prepared,0,0,3),
                "prepared precise floor rejects malformed extent, bounds, and level");
        auto malformed=architecture;malformed.levels[1].floors.pop_back();
        require(!odh::saved_precise_floor(malformed,0,0,1),
                "malformed unused level rejects the architecture snapshot");

        architecture=lot();
        std::vector<odh::ResolvedWallEdges> resolved(25);
        const auto ray=[&](int x1,int y1,int x2,int y2) {
            return odh::saved_wall_raycast(architecture,1,x1,y1,x2,y2,resolved);
        };
        for (const auto [x,y]:std::array<std::array<int,2>,8>{{{{4,1}},{{4,3}},{{1,4}},{{3,4}},{{0,3}},{{0,1}},{{3,0}},{{1,0}}}}) {
            require(ray(2,2,x,y).state==odh::WallRaycastState::clear,
                    "empty architecture ray clears in each octant");
        }
        auto& horizontal=architecture.levels[0].walls[at(architecture,2,2)];
        horizontal.segments=1;
        auto& horizontal_state=resolved[at(architecture,2,2)].top_left;
        horizontal_state=odh::WallEdgeKnowledge::solid;
        require(ray(1,2,4,2).state==odh::WallRaycastState::blocked,
                "top-left wall blocks eastbound ray");
        require(ray(4,2,1,2).state==odh::WallRaycastState::blocked,
                "top-left wall blocks reverse ray");
        horizontal_state=odh::WallEdgeKnowledge::non_solid;
        require(ray(1,2,4,2).state==odh::WallRaycastState::clear,
                "explicit non-solid wall edge allows the ray");
        horizontal_state=odh::WallEdgeKnowledge::unresolved;
        const auto unknown=ray(1,2,4,2);
        require(unknown.state==odh::WallRaycastState::unsupported&&unknown.tile_x==2&&unknown.tile_y==2&&unknown.top_left,
                "unresolved door/wall edge fails closed with location");

        architecture=lot();resolved.assign(25,{});
        auto& vertical=architecture.levels[1].walls[at(architecture,2,2)];
        vertical.segments=2;
        resolved[at(architecture,2,2)].top_right=odh::WallEdgeKnowledge::solid;
        require(odh::saved_wall_raycast(architecture,2,2,1,2,4,resolved).state==odh::WallRaycastState::blocked,
                "top-right wall blocks southbound ray on selected level");
        require(odh::saved_wall_raycast(architecture,1,2,1,2,4,resolved).state==odh::WallRaycastState::clear,
                "wall raycast selects independent level data");
        architecture.levels[0].walls[at(architecture,2,2)].segments=16;
        require(odh::saved_wall_raycast(architecture,1,1,2,4,2,resolved).state==odh::WallRaycastState::clear,
                "reference wall raycast does not test diagonal wall bits");
        require(odh::saved_wall_raycast(architecture,1,-1,2,4,2,resolved).state==odh::WallRaycastState::clear,
                "source GetWall outside-map cells are empty rather than an unsupported endpoint");
        require(odh::saved_wall_raycast(architecture,3,1,2,4,2,resolved).failure==odh::WallRaycastFailure::level&&
                odh::saved_wall_raycast(architecture,1,1,2,4,2,{}).failure==odh::WallRaycastFailure::resolver_extent,
                "unknown level and resolver extent retain distinct unsupported diagnostics");
        architecture=lot();resolved.assign(25,{});
        for(const auto [x,y]:std::array<std::array<int,2>,8>{{{{2047,1}},{{2047,2047}},{{1,2047}},{{-2048,2047}},{{-2048,1}},{{-2048,-2048}},{{1,-2048}},{{2047,-2048}}}}){
            require(ray(2,2,x,y).state==odh::WallRaycastState::clear&&ray(x,y,2,2).state==odh::WallRaycastState::clear,
                "outside-map endpoints preserve source empty walls in all octants and reverse directions");
        }
        require(ray(-2048,-2048,2047,2047).state==odh::WallRaycastState::clear&&
                ray(-2048,2047,2047,-2048).state==odh::WallRaycastState::clear&&
                ray(-2048,-2048,-2048,-2048).state==odh::WallRaycastState::clear,
                "full source tile domain and stationary OOW query remain bounded and exact");
        architecture.levels[0].walls[at(architecture,2,2)].segments=1;
        resolved[at(architecture,2,2)].top_left=odh::WallEdgeKnowledge::solid;
        require(ray(-2048,2,2047,2).state==odh::WallRaycastState::blocked&&
                ray(2047,2,-2048,2).state==odh::WallRaycastState::blocked,
                "long outside-to-outside rays still test actual interior walls both ways");
        resolved[at(architecture,2,2)].top_left=odh::WallEdgeKnowledge::unresolved;
        const auto exterior_unknown=ray(-2048,2,2047,2);
        require(exterior_unknown.state==odh::WallRaycastState::unsupported&&
                exterior_unknown.failure==odh::WallRaycastFailure::unresolved_edge&&exterior_unknown.tile_x==2&&
                exterior_unknown.tile_y==2&&exterior_unknown.top_left,
                "outside traversal preserves exact unknown interior edge instead of assuming it empty");
        require(ray(-2049,2,4,2).failure==odh::WallRaycastFailure::coordinate_domain&&
                ray(2,2,2048,2).failure==odh::WallRaycastFailure::coordinate_domain&&
                ray(INT32_MIN,2,INT32_MAX,2).failure==odh::WallRaycastFailure::coordinate_domain,
                "unrepresented int32 ranges fail before subtraction or unbounded iteration");
        auto malformed_walls=architecture;malformed_walls.levels[0].walls.pop_back();
        require(odh::saved_wall_raycast(malformed_walls,1,1,2,4,2,resolved).failure==odh::WallRaycastFailure::architecture,
                "malformed architecture rejects wall queries");

        std::cout<<checks<<" route geometry checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
