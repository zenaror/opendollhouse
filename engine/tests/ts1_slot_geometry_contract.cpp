// SPDX-License-Identifier: MPL-2.0
#include "simantics/engine/TS1SlotGeometry.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace FSO::SimAntics::Engine;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
bool Equal(st::Point point,int x,int y) { return point.X==x && point.Y==y; }
SLOTItem Region(int flags=1,int min=16,int max=32,int optimal=16) {
    SLOTItem slot;slot.Type=3;slot.Rsflags=SLOTFlags(flags);
    slot.MinProximity=min;slot.MaxProximity=max;slot.OptimalProximity=optimal;
    slot.Resolution=16;slot.Gradient=.1875f;return slot;
}
void ProjectionControls() {
    // Literal controls from the DVD instruction transcription (x87 PC24 RN).
    // The independent live DVD accepted-candidate capture is checked below.
    struct Control { unsigned Heading;float X,Y;int WorldX,WorldY; };
    const Control controls[] = {
        {0, 16.0f, 32.0f, 424, 488},
        {0, -32.0f, -32.0f, 376, 424},
        {0, 0.75f, 1.25f, 408, 457},
        {0, -0.75f, -1.25f, 407, 454},
        {0, 512.0f, -512.0f, 920, -56},
        {1, 16.0f, 32.0f, 397, 490},
        {1, -32.0f, -32.0f, 408, 411},
        {1, 0.75f, 1.25f, 408, 457},
        {1, -0.75f, -1.25f, 408, 455},
        {1, 512.0f, -512.0f, 1132, 456},
        {2, 16.0f, 32.0f, 376, 472},
        {2, -32.0f, -32.0f, 440, 424},
        {2, 0.75f, 1.25f, 406, 456},
        {2, -0.75f, -1.25f, 409, 456},
        {2, 512.0f, -512.0f, 920, 968},
        {3, 16.0f, 32.0f, 374, 445},
        {3, -32.0f, -32.0f, 453, 456},
        {3, 0.75f, 1.25f, 407, 456},
        {3, -0.75f, -1.25f, 409, 456},
        {3, 512.0f, -512.0f, 408, 1180},
        {4, 16.0f, 32.0f, 392, 424},
        {4, -32.0f, -32.0f, 440, 488},
        {4, 0.75f, 1.25f, 407, 454},
        {4, -0.75f, -1.25f, 408, 457},
        {4, 512.0f, -512.0f, -104, 968},
        {5, 16.0f, 32.0f, 419, 422},
        {5, -32.0f, -32.0f, 408, 501},
        {5, 0.75f, 1.25f, 408, 455},
        {5, -0.75f, -1.25f, 408, 457},
        {5, 512.0f, -512.0f, -316, 456},
        {6, 16.0f, 32.0f, 440, 440},
        {6, -32.0f, -32.0f, 376, 488},
        {6, 0.75f, 1.25f, 409, 456},
        {6, -0.75f, -1.25f, 406, 456},
        {6, 512.0f, -512.0f, -104, -56},
        {7, 16.0f, 32.0f, 442, 467},
        {7, -32.0f, -32.0f, 363, 456},
        {7, 0.75f, 1.25f, 409, 456},
        {7, -0.75f, -1.25f, 407, 456},
        {7, 512.0f, -512.0f, 408, -268},
    };
    for (const auto& c:controls) {
        st::Point actual;
        Check(ProjectTS1SlotPoint(st::Point(408,456),c.Heading,c.X,c.Y,actual),"projection rejected source control");
        Check(Equal(actual,c.WorldX,c.WorldY),"projection differs from source literal");
    }
}
void CapturedSocialCandidates() {
    auto slot=Region(0x8055,32,80,32);
    TS1SlotGeometryContext context;context.Center=st::Point(408,456);context.TargetHeading=7;
    struct Candidate {st::Point Point;int Score,Region;};
    std::vector<Candidate> candidates;
    VisitTS1SlotRegionCells(slot,context,[&](const TS1SlotRegionCell& cell) {
        TS1SlotScoreInput input;input.RadiusPoint=cell.RadiusPoint;input.Weight=1;input.Region=cell.Region;
        int32_t score;
        if (ScoreTS1SlotProbe(slot,input,score)) candidates.push_back({cell.Position,score,cell.Region});
        return TS1SlotRowControl::Continue;
    });
    Check(candidates.size()==72,"social pre-admission candidate count differs");
    struct Accepted {size_t Index;int X,Y,Score,Region;};
    const Accepted accepted[] = {
        {0, 363, 456, 133, 1},
        {1, 374, 445, 135, 1},
        {2, 385, 433, 135, 1},
        {3, 397, 422, 135, 1},
        {6, 363, 433, 132, 1},
        {7, 374, 422, 132, 1},
        {19, 419, 422, 135, 4},
        {20, 431, 433, 135, 4},
        {21, 442, 445, 135, 4},
        {25, 442, 422, 132, 4},
        {26, 453, 433, 132, 4},
        {37, 442, 467, 135, 16},
        {38, 431, 479, 135, 16},
        {39, 419, 490, 135, 16},
        {42, 453, 479, 132, 16},
        {43, 442, 490, 132, 16},
        {57, 374, 467, 135, 64},
        {62, 363, 479, 132, 64},
    };
    // Scene admission is deliberately not inferred: these are the actual DVD
    // accepted records at independently identified pre-admission indices.
    for (const auto& a:accepted) {
        const auto& c=candidates.at(a.Index);
        Check(Equal(c.Point,a.X,a.Y)&&c.Score==a.Score&&c.Region==a.Region,"ordered live DVD accepted record differs");
    }
}
void RowAndPointContracts() {
    TS1SlotGeometryContext context;context.Center=st::Point(408,456);
    auto slot=Region();int count=0;
    auto visited=VisitTS1SlotRegionCells(slot,context,[&](const TS1SlotRegionCell& c) {
        Check(c.Radial==-1,"status3 failed to stop the next radial row");
        return ++count==1?TS1SlotRowControl::StopAfterRow:TS1SlotRowControl::Continue;
    });
    Check(visited==3&&count==3,"status3 must finish all current-row tangents");
    slot.Rsflags=SLOTFlags(5);count=0;
    visited=VisitTS1SlotRegionCells(slot,context,[&](const TS1SlotRegionCell&) {
        ++count;return TS1SlotRowControl::StopAfterRow;
    });
    Check(visited==5&&count==5,"row stop leaked into the next region or tangent dedup changed");
    slot=Region(0,0,0,0);slot.Resolution=0;slot.Offset.X=.75f;slot.Offset.Y=1.25f;
    visited=VisitTS1SlotRegionCells(slot,context,[&](const TS1SlotRegionCell& c) {
        Check(c.PointOnly&&Equal(c.Position,408,457),"point branch incorrectly applied region validation");
        TS1SlotScoreInput input;input.PointOnly=true;int32_t score;
        Check(ScoreTS1SlotProbe(slot,input,score)&&score==100,"point branch score changed");
        return TS1SlotRowControl::Continue;
    });
    Check(visited==1,"zero-step point not visited");
    slot=Region();slot.Offset.X=.75f;slot.Offset.Y=1.25f;
    VisitTS1SlotRegionCells(slot,context,[&](const TS1SlotRegionCell& c) {
        Check(Equal(c.Position,392,441)&&Equal(c.PreferredPosition,393,442),"fractional destination and preferred comparison conflated");
        return TS1SlotRowControl::Stop;
    });
    slot=Region();context.TargetWallMask=1;
    Check(VisitTS1SlotRegionCells(slot,context,[](const TS1SlotRegionCell&){return TS1SlotRowControl::Continue;})==0,"target wall region gate ignored");
    slot.Rsflags=SLOTFlags(0x801);
    Check(VisitTS1SlotRegionCells(slot,context,[](const TS1SlotRegionCell&){return TS1SlotRowControl::Continue;})==8,"IgnoreRooms did not suppress wall gate");
    context.TargetWallMask=0;context.TargetHeading=7;slot=Region(0x201);
    VisitTS1SlotRegionCells(slot,context,[&](const TS1SlotRegionCell& c) {
        Check(c.Heading==0&&Equal(c.Position,392,440),"absolute region incorrectly rotated by target heading");
        return TS1SlotRowControl::Stop;
    });
    slot=Region(1,32,80,32);slot.Sitting=1;slot.Resolution=8;
    context.AllowSitting=true;
    VisitTS1SlotRegionCells(slot,context,[&](const TS1SlotRegionCell& c) {
        Check(c.Step==16&&c.Radial==-1,"chair row expansion or forced step differs");return TS1SlotRowControl::Stop;
    });
    context.AllowSitting=false;
    VisitTS1SlotRegionCells(slot,context,[&](const TS1SlotRegionCell& c) {
        Check(c.Step==8&&c.Radial==-4,"ineligible sitter altered authored standing grid");return TS1SlotRowControl::Stop;
    });
    slot=Region(1,0,32,16);
    Check(VisitTS1SlotRegionCells(slot,context,[](const TS1SlotRegionCell&){return TS1SlotRowControl::Continue;})==0,"invalid region minimum accepted");
}
void ScoringContracts() {
    Check(TS1SlotGoalPenalty(135,134)==16,"weighted score penalty differs");
    Check(TS1SlotGoalPenalty(0x7fffffff,1)==-32,"weighted DWORD penalty wrap differs");
    auto slot=Region();TS1SlotScoreInput input;input.RadiusPoint=st::Point(16,16);input.Weight=1;input.Region=1;
    int32_t score;
    Check(ScoreTS1SlotProbe(slot,input,score)&&score==134,"source integer-radius penalty differs");
    input.RadiusPoint=st::Point(0,32);
    Check(ScoreTS1SlotProbe(slot,input,score)&&score==132,"outer boundary penalty differs");
    input.RadiusPoint=st::Point(16,16);input.TargetWallMask=2;
    Check(ScoreTS1SlotProbe(slot,input,score)&&score==109,"expanded adjacent wall bonus differs");
    input.TargetWallMask=0;input.Preferred=true;
    Check(ScoreTS1SlotProbe(slot,input,score)&&score==154,"preferred point bonus differs");
    input.Preferred=false;slot.Rsflags=SLOTFlags(0x2001);
    Check(ScoreTS1SlotProbe(slot,input,score)&&score==124,"zero RNG sample noise differs");
    input.RandomSample=19;
    Check(ScoreTS1SlotProbe(slot,input,score)&&score==143,"RNG upper noise differs");
    slot.Rsflags=SLOTFlags(1);
    Check(ScoreTS1SlotProbe(slot,input,score)&&score==134,"RNG sample applied without source flag");
    Check(IsTS1SlotProbeInRange(slot,st::Point(0,16))&&IsTS1SlotProbeInRange(slot,st::Point(32,0)),"inclusive radius boundary lost");
    Check(!IsTS1SlotProbeInRange(slot,st::Point(0,15))&&!IsTS1SlotProbeInRange(slot,st::Point(32,1)),"outside radius admitted");
    Check(!IsTS1SlotProbeInRange(slot,st::Point(std::numeric_limits<int>::min(),std::numeric_limits<int>::min())),"huge coordinates admitted");
    input.Weight=std::numeric_limits<int32_t>::min();
    Check(!ScoreTS1SlotProbe(slot,input,score),"zero wrapped source weight admitted");
}
}
int main() {ProjectionControls();CapturedSocialCandidates();RowAndPointContracts();ScoringContracts();std::cout<<"TS1 authored SLOT geometry source contracts passed\n";}
