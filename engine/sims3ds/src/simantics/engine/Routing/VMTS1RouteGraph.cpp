// SPDX-License-Identifier: MPL-2.0
#include "simantics/engine/Routing/VMTS1RouteGraph.h"
#include "common/TS1/TS1AutonomyFloat.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace FSO { namespace SimAntics { namespace Engine { namespace Routing {
namespace {
using Common::TS1::AutonomyFloat::Round;
float Distance(st::Point a,st::Point b){
 const float x=Round(float(a.X-b.X)),y=Round(float(a.Y-b.Y));
 return Round(std::sqrt(Round(Round(x*x)+Round(y*y))));
}
st::Point Center(const TS1RouteRect& r){return {(r.X1+r.X2)/2,(r.Y1+r.Y2)/2};}
bool Inside(const TS1RouteRect& a,const TS1RouteRect& b){return a.X1>=b.X1&&a.X2<=b.X2&&a.Y1>=b.Y1&&a.Y2<=b.Y2;}
int32_t Word(uint32_t v){return v<=0x7fffffff?int32_t(v):int32_t(int64_t(v)-INT64_C(0x100000000));}
}
bool VMTS1RouteGraph::Overlaps(const TS1RouteRect& a,const TS1RouteRect& b){return a.X1<b.X2&&b.X1<a.X2&&a.Y1<b.Y2&&b.Y1<a.Y2;}
TS1RouteRect VMTS1RouteGraph::Portal(const TS1RouteRect& a,const TS1RouteRect& b){
 TS1RouteRect r{std::max(a.X1,b.X1)-1,std::max(a.Y1,b.Y1)-1,std::min(a.X2,b.X2)+1,std::min(a.Y2,b.Y2)+1,0};
 return r.X1<r.X2&&r.Y1<r.Y2?r:TS1RouteRect{};
}
int VMTS1RouteGraph::Filter(const TS1RouteRect& r)const{
 int found=-1;
 for(size_t i=0;i<State.Weights.size();++i)if(Overlaps(r,State.Weights[i])){
  found=int(i);if(State.Weights[i].Weight==std::numeric_limits<int32_t>::max())break;
 }
 return found;
}
int VMTS1RouteGraph::Region(const TS1RouteRect& r)const{
 for(size_t i=0;i<State.Regions.size();++i)if(Overlaps(r,State.Regions[i]))return int(i);
 return -1;
}
bool VMTS1RouteGraph::Grow(TS1RouteRect& r){
 const auto& o=State.Options;const auto seed=r;
 const auto tooLarge=[&]{return o.LimitSize&&(r.X2-r.X1>o.MaxSize||r.Y2-r.Y1>o.MaxSize);};
 const int initial=Filter(r);
 if(tooLarge()||(initial>=0&&State.Weights[initial].Weight==INT32_MAX)||Region(r)>=0)return false;
 const int step=o.LimitSize?std::max(1,o.MaxSize/4):32;
 int steps[4]={-step,-step,step,step};int zeros=0;
 for(int attempt=0;attempt<200&&zeros<4;++attempt){
  const int side=attempt&3;bool accepted=false;
  // 469d75 retries THIS side after halving. Only acceptance or zero advances
  // the side and consumes the source's 200-visit budget.
  while(steps[side]){
   ++State.GrowthAttempts;const int delta=steps[side];zeros=0;
   int32_t* edges[]={&r.X1,&r.Y1,&r.X2,&r.Y2};*edges[side]+=delta;
   const int filter=Filter(r);
   if(tooLarge()||filter!=initial||(filter>=0&&State.Weights[filter].Weight==INT32_MAX)||
      Region(r)>=0||(initial>=0&&!Inside(r,State.Weights[initial]))){
    *edges[side]-=delta;steps[side]/=2;
   }else{accepted=true;break;}
  }
  if(!accepted)++zeros;
 }

 if(o.LimitAspect){
  const int w=r.X2-r.X1,h=r.Y2-r.Y1;
  if(h<w&&Round(float(o.MaxAspect))<Round(float(w)/float(h))){
   const int width=o.MaxAspect*h;const int x=(seed.X1+seed.X2)/2-width/2;
   r.X1=std::clamp(x,r.X1,r.X2-width);r.X2=r.X1+width;
  }else if(w<=h&&Round(float(o.MaxAspect))<Round(float(h)/float(w))){
   const int height=o.MaxAspect*w;const int y=(seed.Y1+seed.Y2)/2-height/2;
   r.Y1=std::clamp(y,r.Y1,r.Y2-height);r.Y2=r.Y1+height;
  }
 }
 return r.X1<r.X2&&r.Y1<r.Y2;
}
int VMTS1RouteGraph::AddRegion(const TS1RouteRect& r){
 if(State.Regions.size()>=State.Options.MaxRegions){State.Stage=TS1RouteGraphStage::ResourceLimit;return -1;}
 State.Regions.push_back(r);return int(State.Regions.size()-1);
}
int VMTS1RouteGraph::AddNode(int candidate,int region,int weight){
 if(State.Nodes.size()>=State.Options.MaxNodes){State.Stage=TS1RouteGraphStage::ResourceLimit;return -1;}
 TS1RouteGraphNode node;node.Candidate=candidate;node.Region=region;node.Weight=weight;
 State.Nodes.push_back(node);return int(State.Nodes.size()-1);
}
bool VMTS1RouteGraph::AppendEdge(int node){
 if(State.Edges.size()>=State.Options.MaxEdges){State.Stage=TS1RouteGraphStage::ResourceLimit;return false;}
 State.Edges.push_back(node);return true;
}
bool VMTS1RouteGraph::Begin(st::Point start,const std::vector<TS1RouteGraphGoal>& goals,
 const std::vector<TS1RouteRect>& a,const std::vector<TS1RouteRect>& b,const TS1RouteGraphOptions& options){
 State={};State.Options=options;State.Start=start;State.Goals=goals;
 // Defensive coordinate domain leaves headroom for source growth/midpoints.
 const auto coord=[](int x){return x>=-0x10000000&&x<=0x10000000;};
 const auto rect=[&](const TS1RouteRect& r){return coord(r.X1)&&coord(r.X2)&&coord(r.Y1)&&coord(r.Y2)&&r.X1<r.X2&&r.Y1<r.Y2;};
 if(!coord(start.X)||!coord(start.Y)||goals.size()>65535||a.size()>65536||b.size()>65536-a.size()||
    options.MaxRegions>INT32_MAX||options.MaxNodes>INT32_MAX||options.MaxEdges>INT32_MAX||
    (options.LimitSize&&(options.MaxSize<1||options.MaxSize>0x10000000))||(options.LimitAspect&&options.MaxAspect<1)||
    !std::all_of(a.begin(),a.end(),rect)||!std::all_of(b.begin(),b.end(),rect)||
    !std::all_of(goals.begin(),goals.end(),[&](const auto& g){return rect(g.Rect);})){State.Stage=TS1RouteGraphStage::Failed;return false;}
 State.Weights=a;State.Weights.insert(State.Weights.end(),b.begin(),b.end());
 TS1RouteRect initial{start.X,start.Y,start.X+1,start.Y+1,0};Grow(initial);
 if(AddRegion(initial)<0||AddNode(-1,0,0)<0||AddNode(-1,-1,0)<0)return false;
 for(size_t i=0;i<goals.size();++i){
  State.Starts.push_back(-1);State.Ends.push_back(-1);
  auto r=goals[i].Rect;const int filter=Filter(r);
  if(filter>=0&&State.Weights[filter].Weight==INT32_MAX)continue;
  int region=Region(r);
  if(region<0){if(!Grow(r))continue;r.Weight=filter<0?0:State.Weights[filter].Weight;region=AddRegion(r);if(region<0)return false;}
  const int end=AddNode(int(i),region,State.Regions[region].Weight);if(end<0)return false;
  State.Ends.back()=end;const int begin=AddNode(int(i),0,goals[i].Rect.Weight);if(begin<0)return false;State.Starts.back()=begin;
 }
 State.Nodes[0].Entry=start;State.Nodes[0].G=State.Nodes[0].H=State.Nodes[0].F=0;State.Open.push_back(0);
 State.Stage=TS1RouteGraphStage::Searching;return true;
}
bool VMTS1RouteGraph::Perimeter(int node){
 const int candidate=State.Nodes[node].Candidate;const auto region=State.Regions[State.Nodes[node].Region];
 TS1RouteRect seed{region.X1,region.Y1-1,region.X1+1,region.Y1,0};
 for(int side=0;side<4;){
  const int filter=Filter(seed);TS1RouteRect hit;
  if(filter>=0&&State.Weights[filter].Weight==INT32_MAX)hit=State.Weights[filter];
  else{
   int regionID=Region(seed);
   if(regionID<0){hit=seed;if(!Grow(hit)){State.Stage=TS1RouteGraphStage::InvalidGrowth;return false;}
    hit.Weight=filter<0?State.Options.DefaultWeight:State.Weights[filter].Weight;
    regionID=AddRegion(hit);if(regionID<0)return false;
   }
   int neighbor=-1;
   for(size_t i=0;i<State.Nodes.size();++i)if(State.Nodes[i].Candidate==candidate&&State.Nodes[i].Region==regionID){neighbor=int(i);break;}
   if(neighbor<0){neighbor=AddNode(candidate,regionID,State.Regions[regionID].Weight);if(neighbor<0)return false;}
   if(!AppendEdge(neighbor))return false;++State.Nodes[node].NeighborCount;hit=State.Regions[regionID];
  }
  int dx=0,dy=0;
  if(side==0){if(region.X2<seed.X2||region.X2<=hit.X2){dx=region.X2-seed.X1;dy=1;++side;}else dx=hit.X2-seed.X1;}
  else if(side==1){if(seed.Y2<=region.Y2&&hit.Y2<region.Y2)dy=hit.Y2-seed.Y1;else{dx=-1;dy=region.Y2-seed.Y1;++side;}}
  else if(side==2){if(region.X1<=seed.X1&&region.X1<hit.X1)dx=hit.X1-seed.X1-1;else{dx=region.X1-seed.X1-1;dy=-1;++side;}}
  else{if(region.Y1<=seed.Y1&&region.Y1<hit.Y1)dy=hit.Y1-seed.Y1-1;else{dx=1;dy=region.Y1-seed.Y1-1;++side;}}
  if(!dx&&!dy){State.Stage=TS1RouteGraphStage::Failed;return false;}
  seed.X1+=dx;seed.X2+=dx;seed.Y1+=dy;seed.Y2+=dy;
 }
 return true;
}
bool VMTS1RouteGraph::Neighbors(int node){
 if(State.Nodes[node].NeighborCount>=0)return true;
 State.Nodes[node].NeighborCount=0;State.Nodes[node].NeighborBase=int(State.Edges.size());
 auto add=[&](int value){if(!AppendEdge(value))return false;++State.Nodes[node].NeighborCount;return true;};
 if(node==0){for(int start:State.Starts)if(start>=0&&!add(start))return false;return true;}
 if(node==1)return true;
 if(!Perimeter(node))return false;
 if(std::find(State.Ends.begin(),State.Ends.end(),node)!=State.Ends.end()&&!add(1))return false;
 if(std::find(State.Starts.begin(),State.Starts.end(),node)!=State.Starts.end())
  for(int end:State.Ends)if(end>=0&&State.Nodes[end].Region==State.Nodes[node].Region&&State.Nodes[end].Candidate==State.Nodes[node].Candidate&&!add(end))return false;
 return true;
}
float VMTS1RouteGraph::EdgeCost(int from,int to,st::Point& entry)const{
 const auto& a=State.Nodes[from];const auto& b=State.Nodes[to];entry=a.Entry;
 if(from==0)return float(State.Goals[b.Candidate].Rect.Weight);
 if(to==1)return 0;
 if(a.Region!=b.Region)entry=Center(Portal(State.Regions[a.Region],State.Regions[b.Region]));
 const int32_t sum=Word(uint32_t(a.Weight)+uint32_t(b.Weight)+2u);
 const int32_t weight=sum>=0?sum/2:int32_t((int64_t(sum)-1)/2);
 return Round(float(weight)*Round(Distance(a.Entry,entry)+1.f));
}
float VMTS1RouteGraph::Heuristic(int node)const{
 const auto& n=State.Nodes[node];return node<2?0:Distance(n.Entry,Center(State.Goals[n.Candidate].Rect));
}
bool VMTS1RouteGraph::CompletePath(){
 std::vector<int> reverse;int n=State.Current;
 for(size_t limit=0;n>=0&&limit<=State.Nodes.size();++limit){
  if(size_t(n)>=State.Nodes.size())return false;reverse.push_back(n);
  if(State.SelectedCandidate<0&&State.Nodes[n].Candidate>=0)State.SelectedCandidate=State.Nodes[n].Candidate;
  n=State.Nodes[n].Parent;
 }
 if(n>=0||State.SelectedCandidate<0)return false;
 int last=-1;
 for(auto i=reverse.rbegin();i!=reverse.rend();++i){const int region=State.Nodes[*i].Region;
  if(region>=0&&region!=last){State.PathNodes.push_back(*i);last=region;}}
 for(int node:State.PathNodes)State.Points.push_back(State.Nodes[node].Entry);
 if(State.Options.AppendGoalCenter&&!State.Points.empty())State.Points.push_back(Center(State.Goals[State.SelectedCandidate].Rect));
 return !State.Points.empty();
}
bool VMTS1RouteGraph::RefineSweep(){
 if(State.PathNodes.size()<2)return true;
 for(;;){
  const int64_t pass=int64_t(State.RefinePass)+1;const int step=std::max<int64_t>(1,100/(pass*pass));
  size_t improvements=0;
  for(size_t i=1;i<State.PathNodes.size();++i){
   const auto bounds=Portal(State.Regions[State.Nodes[State.PathNodes[i-1]].Region],State.Regions[State.Nodes[State.PathNodes[i]].Region]);
   const auto previous=State.Points[i-1],current=State.Points[i],next=i+1<State.Points.size()?State.Points[i+1]:current;
   auto best=current;float cost=Round(Distance(previous,current)+Distance(current,next));
   auto probe=[&](st::Point point){const float candidate=Round(Distance(previous,point)+Distance(point,next));if(candidate<cost){cost=candidate;best=point;++improvements;}};
   if(bounds.X2-bounds.X1>2){if(current.X<bounds.X2-step)probe({current.X+step,current.Y});if(bounds.X1+step<current.X)probe({current.X-step,current.Y});}
   if(bounds.Y2-bounds.Y1>2){if(current.Y<bounds.Y2-step)probe({current.X,current.Y+step});if(bounds.Y1+step<current.Y)probe({current.X,current.Y-step});}
   State.Points[i]=best;
  }
  if(improvements)return false;
  ++State.RefinePass;if(step<=1)return true;
 }
}
TS1RouteGraphStage VMTS1RouteGraph::Step(){
 if(State.Stage==TS1RouteGraphStage::Refining){++State.RefineSteps;if(RefineSweep())State.Stage=TS1RouteGraphStage::Done;return State.Stage;}
 if(State.Stage!=TS1RouteGraphStage::Searching)return State.Stage;
 ++State.SearchSteps;const int current=State.Current;
 if(current==1){if(!CompletePath())State.Stage=TS1RouteGraphStage::Failed;else State.Stage=State.Options.Refine?TS1RouteGraphStage::Refining:TS1RouteGraphStage::Done;return State.Stage;}
 if(!Neighbors(current))return State.Stage;
 const int base=State.Nodes[current].NeighborBase,count=State.Nodes[current].NeighborCount;
 for(int k=0;k<count;++k){const int neighbor=State.Edges[base+k];st::Point entry;
  const float g=Round(State.Nodes[current].G+EdgeCost(current,neighbor,entry));
  const auto closed=std::find(State.Closed.begin(),State.Closed.end(),neighbor),open=std::find(State.Open.begin(),State.Open.end(),neighbor);
  if(closed!=State.Closed.end()){if(State.Nodes[neighbor].G<=g)continue;State.Closed.erase(closed);}
  if(open!=State.Open.end()){if(!(g<State.Nodes[neighbor].G))continue;}else State.Open.push_back(neighbor);
  auto& node=State.Nodes[neighbor];node.G=g;node.Entry=entry;node.H=Heuristic(neighbor);node.F=Round(node.G+node.H);node.Parent=current;
 }
 State.Open.erase(std::remove(State.Open.begin(),State.Open.end(),current),State.Open.end());State.Closed.push_back(current);
 if(State.Open.empty()){State.Stage=TS1RouteGraphStage::Failed;return State.Stage;}
 size_t best=0;for(size_t i=1;i<State.Open.size();++i)if(State.Nodes[State.Open[i]].F<State.Nodes[State.Open[best]].F)best=i;
 State.Current=State.Open[best];return State.Stage;
}
uint32_t VMTS1RouteGraph::SelectedOriginalIndex()const{
 return State.SelectedCandidate>=0&&size_t(State.SelectedCandidate)<State.Goals.size()?State.Goals[State.SelectedCandidate].OriginalIndex:UINT32_MAX;
}
}}}}
