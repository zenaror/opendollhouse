// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "core/math.h"
#include <cstdint>
#include <vector>
namespace FSO { namespace SimAntics { namespace Engine { namespace Routing {
struct TS1RouteRect {
    int32_t X1=0,Y1=0,X2=0,Y2=0,Weight=0;
};
struct TS1RouteGraphGoal { TS1RouteRect Rect; uint32_t OriginalIndex=0; };
struct TS1RouteGraphOptions {
    bool LimitSize=false,LimitAspect=false,Refine=true,AppendGoalCenter=true;
    int32_t MaxSize=128,MaxAspect=4,DefaultWeight=0;
    // Defensive native resource limits: reaching one fails explicitly.
    uint32_t MaxRegions=65536,MaxNodes=262144,MaxEdges=1048576;
};
enum class TS1RouteGraphStage : uint8_t { Empty,Searching,Refining,Done,Failed,InvalidGrowth,ResourceLimit };
struct TS1RouteGraphNode {
    int32_t Candidate=-1,Region=-1,NeighborCount=-1,NeighborBase=-1,Weight=0;
    st::Point Entry;
    int32_t Parent=-1;
    float F=0,G=0,H=0;
};
// Value-owned scratch for one synchronous original query. The VM consumer
// exhausts the source's work budget in the same call; it never inserts ticks
// between expansions or persists a search against a changing scene.
struct TS1RouteGraphState {
    TS1RouteGraphOptions Options;
    std::vector<TS1RouteGraphGoal> Goals;
    std::vector<TS1RouteRect> Weights,Regions;
    std::vector<TS1RouteGraphNode> Nodes;
    std::vector<int32_t> Starts,Ends,Edges,Open,Closed,PathNodes;
    std::vector<st::Point> Points;
    st::Point Start;
    TS1RouteGraphStage Stage=TS1RouteGraphStage::Empty;
    int32_t Current=0,SelectedCandidate=-1,RefinePass=0;
    uint64_t SearchSteps=0,RefineSteps=0,GrowthAttempts=0;
};
class VMTS1RouteGraph {
public:
    TS1RouteGraphState State;
    bool Begin(st::Point start,const std::vector<TS1RouteGraphGoal>& goals,
               const std::vector<TS1RouteRect>& weightsA,
               const std::vector<TS1RouteRect>& weightsB={},
               const TS1RouteGraphOptions& options={});
    // Original query83023: one expansion OR one refinement sweep per call.
    TS1RouteGraphStage Step();
    uint32_t SelectedOriginalIndex() const;
    static bool Overlaps(const TS1RouteRect&,const TS1RouteRect&);
    static TS1RouteRect Portal(const TS1RouteRect&,const TS1RouteRect&);
private:
    int Filter(const TS1RouteRect&) const;
    int Region(const TS1RouteRect&) const;
    bool Grow(TS1RouteRect&);
    int AddRegion(const TS1RouteRect&);
    int AddNode(int candidate,int region,int weight);
    bool AppendEdge(int node);
    bool Neighbors(int node);
    bool Perimeter(int node);
    float EdgeCost(int from,int to,st::Point& entry) const;
    float Heuristic(int node) const;
    bool CompletePath();
    bool RefineSweep();
};
}}}}
