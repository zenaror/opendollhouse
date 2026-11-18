// SPDX-License-Identifier: MPL-2.0
// Pure source-algorithm controls. A real scene producer/captured graph is a
// separate integration gate; these controls do not assert DVD scene parity.
#include "simantics/engine/Routing/VMTS1RouteGraph.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
using namespace FSO::SimAntics::Engine::Routing;
using st::Point;
namespace {
void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::vector<TS1RouteRect> Room(){return {{-32,-32,0,132,INT32_MAX},{100,-32,132,132,INT32_MAX},{0,-32,100,0,INT32_MAX},{0,100,100,132,INT32_MAX}};}
void Run(VMTS1RouteGraph& graph){for(size_t i=0;i<10000&&(graph.State.Stage==TS1RouteGraphStage::Searching||graph.State.Stage==TS1RouteGraphStage::Refining);++i)graph.Step();Check(graph.State.Stage==TS1RouteGraphStage::Done,"source query did not finish");}
TS1RouteGraphGoal Goal(int x,int y,int weight,uint32_t id){return {{x,y,x+1,y+1,weight},id};}
void RectangleControls(){
 TS1RouteRect a{0,0,10,10,0},side{10,0,20,10,0},corner{10,10,20,20,0},inside{2,2,8,8,0};
 Check(!VMTS1RouteGraph::Overlaps(a,side)&&!VMTS1RouteGraph::Overlaps(a,corner)&&VMTS1RouteGraph::Overlaps(a,inside),"source strict overlap changed");
 auto p=VMTS1RouteGraph::Portal(a,side);Check(p.X1==9&&p.X2==11&&p.Y1==-1&&p.Y2==11,"source portal must inflate both rectangles before intersection");
 p=VMTS1RouteGraph::Portal(a,corner);Check(p.X1==9&&p.X2==11&&p.Y1==9&&p.Y2==11,"source corner portal inflation changed");
}
void QueryControls(){
 VMTS1RouteGraph graph;Check(graph.Begin({20,50},{Goal(30,50,0,41),Goal(60,50,0,99)},Room()),"begin failed");
 Check(graph.State.Regions[0].X1==0&&graph.State.Regions[0].X2==100&&graph.State.Regions[0].Y1==0&&graph.State.Regions[0].Y2==100,"ordered seed growth did not fill empty room");
 Check(graph.State.Stage==TS1RouteGraphStage::Searching&&graph.State.SearchSteps==0,"Begin prematurely executed search");
 Run(graph);Check(graph.SelectedOriginalIndex()==41,"source near goal lost ordered initial OPEN comparison");
 Check(graph.State.Points.size()==2&&graph.State.Points.front()==Point(20,50)&&graph.State.Points.back()==Point(30,50),"same-region physical path changed");
 Check(graph.State.Nodes[1].G==1.f,"source same-region root-goal edge cost changed");
 Check(graph.Begin({20,50},{Goal(30,50,100,41),Goal(60,50,0,99)},Room()),"weighted begin failed");Run(graph);
 Check(graph.SelectedOriginalIndex()==99,"source weighted branch choice changed");
 Check(graph.Begin({20,50},{Goal(30,50,0,7),Goal(30,50,0,8)},Room()),"tie begin failed");Run(graph);Check(graph.SelectedOriginalIndex()==7,"equal OPEN cost changed authored order");
 auto blocks=Room();blocks.push_back({25,45,35,55,INT32_MAX});
 Check(graph.Begin({20,50},{Goal(30,50,0,7),Goal(60,50,0,8)},blocks),"blocked-goal begin failed");Run(graph);Check(graph.SelectedOriginalIndex()==8,"filtered goal changed source candidate index");
 // Retain the whole value-owned planner after one original search step.
 Check(graph.Begin({20,50},{Goal(30,50,0,17)},Room()),"resume begin failed");graph.Step();VMTS1RouteGraph restored;restored.State=graph.State;Run(graph);Run(restored);
 Check(restored.SelectedOriginalIndex()==graph.SelectedOriginalIndex()&&restored.State.Points==graph.State.Points&&restored.State.SearchSteps==graph.State.SearchSteps,"value-owned query resume rebuilt or lost state");
 TS1RouteGraphOptions limit;limit.MaxNodes=2;Check(!graph.Begin({20,50},{Goal(30,50,0,1)},Room(),{},limit)&&graph.State.Stage==TS1RouteGraphStage::ResourceLimit,"resource limit silently truncated graph");
}
void RefinementControls(){
 VMTS1RouteGraph graph;auto& state=graph.State;state.Stage=TS1RouteGraphStage::Refining;
 state.Regions={{-20,-20,20,20,0},{-20,-20,20,20,0}};
 TS1RouteGraphNode a,b;a.Region=0;b.Region=1;state.Nodes={a,b};state.PathNodes={0,1};
 state.Points={{10,0},{0,0},{0,10}};
 Check(graph.Step()==TS1RouteGraphStage::Refining&&state.RefinePass==2&&state.Points[1]==Point(11,0),"source sweep must choose +X before equal +Y and preserve its pass after improvement");
 VMTS1RouteGraph corridor;auto& c=corridor.State;c.Stage=TS1RouteGraphStage::Refining;
 c.Regions={{0,-50,50,50,0},{50,-50,100,50,0}};c.Nodes={a,b};c.PathNodes={0,1};c.Points={{0,0},{50,30},{100,0}};
 Run(corridor);Check(c.Points==std::vector<Point>({{0,0},{50,0},{100,0}}),"portal-constrained refinement changed endpoints or missed straight optimum");
 Check(c.RefineSteps>1,"refinement collapsed original resumable sweeps into one state transition");
}
void CapturedDVDGraph(){
 // Original DVD first OP45 graph, actor105/counter232561, x87 CW007f.
 // Only geometric inputs/outputs are retained; no commercial asset bytes.
 VMTS1RouteGraph graph;TS1RouteGraphOptions options;options.MaxSize=64;options.MaxAspect=0;
 const std::vector<TS1RouteRect> a={{349,445,371,467,10},{450,482,462,494,2147483647},{445,445,467,467,2147483647},{385,449,399,463,2147483647},{369,481,383,495,2147483647},{381,477,403,499,2147483647},{381,461,403,483,2147483647},{397,477,419,499,2147483647},{397,461,419,483,2147483647},{333,413,355,435,2147483647},{333,429,355,451,2147483647},{333,461,355,483,2147483647},{333,477,355,499,2147483647},{333,445,355,467,2147483647},{429,477,451,499,10},{397,413,419,435,10},{445,461,467,483,10},{429,413,451,435,10}};
 const std::vector<TS1RouteRect> b={{297,377,327,535,2147483647},{473,377,503,535,2147483647},{297,377,503,407,2147483647},{297,505,503,535,2147483647},{313,393,407,423,2147483647},{409,393,439,423,2147483647},{441,393,487,423,2147483647},{313,409,343,519,2147483647},{457,409,487,471,2147483647},{457,473,487,519,2147483647},{329,489,439,519,2147483647},{441,489,487,519,2147483647}};
 const std::vector<TS1RouteGraphGoal> goals={{{397,445,398,446,0},0},{{408,440,409,441,0},1},{{419,445,420,446,0},2},{{424,456,425,457,0},3},{{419,467,420,468,0},4}};
 Check(graph.Begin({361,486},goals,a,b,options),"captured DVD graph Begin failed");
 {const std::vector<TS1RouteRect> expected={{355,467,369,489,0},{371,435,445,449,0},{399,449,445,461,0},{419,461,445,477,0}};Check(graph.State.Regions.size()==expected.size(),"captured initial region count differs");for(size_t i=0;i<expected.size();++i){const auto& x=graph.State.Regions[i];const auto& e=expected[i];Check(x.X1==e.X1&&x.Y1==e.Y1&&x.X2==e.X2&&x.Y2==e.Y2&&x.Weight==e.Weight,"captured initial region order/bounds/weight differs");}}
 {const int32_t expected[][5]={{-1,0,-1,-1,0},{-1,-1,-1,-1,0},{0,1,-1,-1,0},{0,0,-1,-1,0},{1,1,-1,-1,0},{1,0,-1,-1,0},{2,1,-1,-1,0},{2,0,-1,-1,0},{3,2,-1,-1,0},{3,0,-1,-1,0},{4,3,-1,-1,0},{4,0,-1,-1,0}};Check(graph.State.Nodes.size()==sizeof(expected)/sizeof(expected[0]),"captured initial node count differs");for(size_t i=0;i<graph.State.Nodes.size();++i){const auto& n=graph.State.Nodes[i];const auto& e=expected[i];Check(n.Candidate==e[0]&&n.Region==e[1]&&n.NeighborCount==e[2]&&n.NeighborBase==e[3]&&n.Weight==e[4],"captured initial node fields differ");}}
 for(size_t n=0;n<1000&&graph.State.Stage==TS1RouteGraphStage::Searching;++n)graph.Step();
 Check(graph.State.Stage==TS1RouteGraphStage::Refining,"captured search did not reach refinement");
 Check(graph.State.SearchSteps==10,"captured source search dispatch count differs");
 {const std::vector<TS1RouteRect> expected={{355,467,369,489,0},{371,435,445,449,0},{399,449,445,461,0},{419,461,445,477,0},{355,445,371,467,10},{369,467,381,481,0},{371,449,381,467,0},{381,449,385,461,0},{355,423,397,435,0},{397,423,419,435,10},{419,423,429,435,0},{429,423,451,435,10},{445,435,457,445,0},{355,435,371,445,0}};Check(graph.State.Regions.size()==expected.size(),"captured searched region count differs");for(size_t i=0;i<expected.size();++i){const auto& x=graph.State.Regions[i];const auto& e=expected[i];Check(x.X1==e.X1&&x.Y1==e.Y1&&x.X2==e.X2&&x.Y2==e.Y2&&x.Weight==e.Weight,"captured searched region order/bounds/weight differs");}}
 Check(graph.State.Edges==std::vector<int32_t>{3,5,7,9,11,12,13,12,14,3,2,15,13,12,2,14,16,17,18,19,16,20,11,21,22,23,24,25,26,15,14,12,27,1},"captured Edges order differs");
 Check(graph.State.PathNodes==std::vector<int32_t>{0,13,14,2},"captured PathNodes order differs");
 Check(graph.State.Points==std::vector<Point>{{361,486},{369,474},{376,467},{376,449},{397,445}},"captured initial path differs");
 {const int32_t expected[][5]={{-1,0,5,0,0},{-1,-1,-1,-1,0},{0,1,11,23,0},{0,0,2,5,0},{1,1,-1,-1,0},{1,0,2,18,0},{2,1,-1,-1,0},{2,0,-1,-1,0},{3,2,-1,-1,0},{3,0,-1,-1,0},{4,3,-1,-1,0},{4,0,2,16,0},{0,4,-1,-1,10},{0,5,3,7,0},{0,6,4,10,0},{0,7,2,14,0},{4,4,-1,-1,10},{4,5,3,20,0},{1,4,-1,-1,10},{1,5,-1,-1,0},{4,6,-1,-1,0},{0,8,-1,-1,0},{0,9,-1,-1,10},{0,10,-1,-1,0},{0,11,-1,-1,10},{0,12,-1,-1,0},{0,2,-1,-1,0},{0,13,-1,-1,0}};Check(graph.State.Nodes.size()==sizeof(expected)/sizeof(expected[0]),"captured searched node count differs");for(size_t i=0;i<graph.State.Nodes.size();++i){const auto& n=graph.State.Nodes[i];const auto& e=expected[i];Check(n.Candidate==e[0]&&n.Region==e[1]&&n.NeighborCount==e[2]&&n.NeighborBase==e[3]&&n.Weight==e[4],"captured searched node fields differ");}}
 auto bits=[](float f){uint32_t u;std::memcpy(&u,&f,sizeof u);return u;};
 {const auto& n=graph.State.Nodes[0];Check(n.Entry==Point(361,486)&&n.Parent==-1&&bits(n.F)==0u&&bits(n.G)==0u&&bits(n.H)==0u,"captured reached-node 0 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[1];Check(n.Parent==2&&bits(n.F)==1110788460u&&bits(n.G)==1110788460u&&bits(n.H)==0u,"captured reached-node 1 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[2];Check(n.Entry==Point(376,449)&&n.Parent==14&&bits(n.F)==1116038661u&&bits(n.G)==1110788460u&&bits(n.H)==1101727037u,"captured reached-node 2 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[3];Check(n.Entry==Point(361,486)&&n.Parent==0&&bits(n.F)==1113210720u&&bits(n.G)==0u&&bits(n.H)==1113210720u,"captured reached-node 3 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[5];Check(n.Entry==Point(361,486)&&n.Parent==0&&bits(n.F)==1115916171u&&bits(n.G)==0u&&bits(n.H)==1115916171u,"captured reached-node 5 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[7];Check(n.Entry==Point(361,486)&&n.Parent==0&&bits(n.F)==1116606059u&&bits(n.G)==0u&&bits(n.H)==1116606059u,"captured reached-node 7 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[9];Check(n.Entry==Point(361,486)&&n.Parent==0&&bits(n.F)==1116442227u&&bits(n.G)==0u&&bits(n.H)==1116442227u,"captured reached-node 9 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[11];Check(n.Entry==Point(361,486)&&n.Parent==0&&bits(n.F)==1114907025u&&bits(n.G)==0u&&bits(n.H)==1114907025u,"captured reached-node 11 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[12];Check(n.Entry==Point(370,467)&&n.Parent==13&&bits(n.F)==1120230016u&&bits(n.G)==1115645178u&&bits(n.H)==1108037638u,"captured reached-node 12 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[13];Check(n.Entry==Point(369,474)&&n.Parent==3&&bits(n.F)==1113517848u&&bits(n.G)==1098301786u&&bits(n.H)==1109475010u,"captured reached-node 13 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[14];Check(n.Entry==Point(376,467)&&n.Parent==13&&bits(n.F)==1113780522u&&bits(n.G)==1104319192u&&bits(n.H)==1106464637u,"captured reached-node 14 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[15];Check(n.Entry==Point(381,455)&&n.Parent==14&&bits(n.F)==1114423863u&&bits(n.G)==1109477740u&&bits(n.H)==1100411286u,"captured reached-node 15 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[16];Check(n.Entry==Point(370,467)&&n.Parent==17&&bits(n.F)==1122087549u&&bits(n.G)==1115645178u&&bits(n.H)==1111752704u,"captured reached-node 16 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[17];Check(n.Entry==Point(369,474)&&n.Parent==11&&bits(n.F)==1115935189u&&bits(n.G)==1098301786u&&bits(n.H)==1112142675u,"captured reached-node 17 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[18];Check(n.Entry==Point(362,467)&&n.Parent==5&&bits(n.F)==1127055120u&&bits(n.G)==1123045578u&&bits(n.H)==1112890027u,"captured reached-node 18 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[19];Check(n.Entry==Point(369,474)&&n.Parent==5&&bits(n.F)==1116099306u&&bits(n.G)==1098301786u&&bits(n.H)==1112470909u,"captured reached-node 19 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[20];Check(n.Entry==Point(376,467)&&n.Parent==17&&bits(n.F)==1116382390u&&bits(n.G)==1104319192u&&bits(n.H)==1110179840u,"captured reached-node 20 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[21];Check(n.Entry==Point(384,435)&&n.Parent==2&&bits(n.F)==1117630947u&&bits(n.G)==1115277549u&&bits(n.H)==1099118003u,"captured reached-node 21 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[22];Check(n.Entry==Point(408,435)&&n.Parent==2&&bits(n.F)==1133109535u&&bits(n.G)==1132622404u&&bits(n.H)==1097718635u,"captured reached-node 22 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[23];Check(n.Entry==Point(424,435)&&n.Parent==2&&bits(n.F)==1123695206u&&bits(n.G)==1119921334u&&bits(n.H)==1105614529u,"captured reached-node 23 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[24];Check(n.Entry==Point(437,435)&&n.Parent==2&&bits(n.F)==1139411137u&&bits(n.G)==1138060078u&&bits(n.H)==1109716122u,"captured reached-node 24 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[25];Check(n.Entry==Point(445,440)&&n.Parent==2&&bits(n.F)==1126443640u&&bits(n.G)==1122488311u&&bits(n.H)==1111558642u,"captured reached-node 25 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[26];Check(n.Entry==Point(422,449)&&n.Parent==2&&bits(n.F)==1122715524u&&bits(n.G)==1119397046u&&bits(n.H)==1103792952u,"captured reached-node 26 coordinates/predecessor/floatbits differ");}
 {const auto& n=graph.State.Nodes[27];Check(n.Entry==Point(371,440)&&n.Parent==2&&bits(n.F)==1118187518u&&bits(n.G)==1113749542u&&bits(n.H)==1104400301u,"captured reached-node 27 coordinates/predecessor/floatbits differ");}
 Check(graph.State.Open==std::vector<int32_t>{7,9,12,16,18,19,20,21,22,23,24,25,26,27,1},"captured Open order differs");
 Check(graph.State.Closed==std::vector<int32_t>{0,3,13,14,15,11,5,17,2},"captured Closed order differs");
 graph.Step();Check(graph.State.Points==std::vector<Point>{{361,486},{369,474},{372,467},{380,449},{397,445}}&&graph.State.RefinePass==4,"captured refinement sweep differs");
 graph.Step();Check(graph.State.Points==std::vector<Point>{{361,486},{369,472},{372,467},{380,449},{397,445}}&&graph.State.RefinePass==5,"captured refinement sweep differs");
 graph.Step();Check(graph.State.Points==std::vector<Point>{{361,486},{369,472},{371,467},{381,449},{397,445}}&&graph.State.RefinePass==7,"captured refinement sweep differs");
 graph.Step();Check(graph.State.Points==std::vector<Point>{{361,486},{369,471},{371,467},{381,449},{397,445}}&&graph.State.RefinePass==7,"captured refinement sweep differs");
 graph.Step();Check(graph.State.Points==std::vector<Point>{{361,486},{369,471},{371,467},{381,449},{397,445}}&&graph.State.RefinePass==8,"captured refinement sweep differs");
 Check(graph.State.Stage==TS1RouteGraphStage::Done&&graph.State.RefineSteps==5&&graph.SelectedOriginalIndex()==0,"captured query final state differs");
}
}
int main(){try{RectangleControls();QueryControls();RefinementControls();CapturedDVDGraph();std::cout<<"PASS source TS1 route graph controls\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
