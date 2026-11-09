#include "odh/wall_route_state.hpp"
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
odh::SavedArchitecture lot(){
    odh::SavedArchitecture result;result.width=result.height=4;
    for(auto& level:result.levels){level.floors.resize(16);level.walls.resize(16);}
    result.levels[0].walls[1*4+2].segments=odh::wall_top_left|odh::wall_top_right;
    return result;
}
odh::WallAppearanceInput wall_object(std::int16_t id,std::uint16_t flags2,std::uint16_t style){
    return {id,static_cast<std::uint64_t>(id),123,odh::EntityKind::object,
        odh::EntityPosition{40,24,1,0},false,flags2,8,
        odh::ObjdWallAppearance{138,123,style,3,164,0}};
}
}
int main(){
    try{
        auto architecture=lot();
        const auto empty=odh::resolve_initial_wall_edges(architecture,{},1);
        require(empty.status==odh::WallEdgeBuildStatus::ready&&empty.edges.size()==16,
                "empty appearance produces bounded row-major edge state");
        const auto edge=std::size_t(1)*architecture.width+2;
        require(empty.edges[edge].top_left==odh::WallEdgeKnowledge::solid&&
                empty.edges[edge].top_right==odh::WallEdgeKnowledge::solid,
                "unknown door faces remain conservatively solid");
        auto ray=odh::saved_wall_raycast(architecture,1,1,1,2,1,empty.edges);
        require(ray.state==odh::WallRaycastState::blocked&&ray.tile_x==2&&ray.tile_y==1&&ray.top_left,
                "solid resolved edge blocks a ray crossing");

        const auto door=wall_object(1,0x8000,3);
        auto window=wall_object(2,0x4000,22);
        auto appearance=odh::derive_wall_appearance_restore_order(architecture,{door,window});
        auto resolved=odh::resolve_initial_wall_edges(architecture,appearance,1);
        require(resolved.status==odh::WallEdgeBuildStatus::ready&&
                resolved.edges[edge].top_left==odh::WallEdgeKnowledge::non_solid,
                "window overlay preserves a prior door opening in the edge map");
        ray=odh::saved_wall_raycast(architecture,1,1,1,2,1,resolved.edges);
        require(ray.state==odh::WallRaycastState::clear,
                "door-marked edge is open in the initial-restore raycast profile");

        auto clear=wall_object(3,0x8000,0);
        appearance=odh::derive_wall_appearance_restore_order(architecture,{door,window,clear});
        resolved=odh::resolve_initial_wall_edges(architecture,appearance,1);
        require(resolved.status==odh::WallEdgeBuildStatus::ready&&
                resolved.edges[edge].top_left==odh::WallEdgeKnowledge::solid,
                "later style-zero door write restores solid edge state");

        auto detached_clear=appearance;auto& cleared=detached_clear.faces.front();
        cleared.owner=0;cleared.generation=0;cleared.style={0,0,{}};cleared.door=false;cleared.door_owner=0;cleared.door_generation=0;
        require(odh::resolve_initial_wall_edges(architecture,detached_clear,1).status==odh::WallEdgeBuildStatus::ready,
            "retired door clear remains an authoritative solid edge without old owner");
        detached_clear.faces.front().style.raw_style=1;
        require(odh::resolve_initial_wall_edges(architecture,detached_clear,1).status==odh::WallEdgeBuildStatus::unsupported,
            "ownerless nonzero style cannot forge a wall edge");
        auto malformed=appearance;
        malformed.faces[0].segment_present=false;
        require(odh::resolve_initial_wall_edges(architecture,malformed,1).status==
                    odh::WallEdgeBuildStatus::unsupported,
                "face/architecture segment disagreement is unsupported");
        require(odh::resolve_initial_wall_edges(architecture,appearance,0).status==
                    odh::WallEdgeBuildStatus::unsupported,
                "invalid level is unsupported");
        require(odh::resolve_initial_wall_edges(architecture,appearance,1,0).status==
                    odh::WallEdgeBuildStatus::unsupported,
                "face budget is enforced");
        auto duplicate=appearance;
        duplicate.faces.push_back(duplicate.faces.front());
        require(odh::resolve_initial_wall_edges(architecture,duplicate,1).status==
                    odh::WallEdgeBuildStatus::unsupported,
                "duplicate face identity is unsupported");
        auto outside=appearance;
        outside.faces.front().x=architecture.width;
        require(odh::resolve_initial_wall_edges(architecture,outside,1).status==
                    odh::WallEdgeBuildStatus::unsupported,
                "face outside the architecture extent is unsupported");
        std::cout<<checks<<" initial wall route-state checks passed (synthetic only)\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
