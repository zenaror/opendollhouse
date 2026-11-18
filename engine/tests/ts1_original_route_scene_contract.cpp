// SPDX-License-Identifier: MPL-2.0
// Pure source producer controls. Original captured B rectangles alone are not
// input tile evidence; matching the actual VM scene is a separate integration gate.
#include "simantics/engine/Routing/VMTS1RouteScene.h"
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics::Engine::Routing;
namespace {
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
TS1RouteSceneInput Grid(bool full=true){
 TS1RouteSceneInput in;in.WorldSize=7;in.RequestedRoom=1;in.Tiles.resize(49);
 if(full)for(int y=1;y<6;++y)for(int x=1;x<6;++x)in.Tiles[y*7+x].Room=1;
 return in;
}
TS1RouteSceneTile& Tile(TS1RouteSceneInput& in,int x,int y){return in.Tiles[y*in.WorldSize+x];}
bool Equal(const TS1RouteRect& a,const TS1RouteRect& b){return a.X1==b.X1&&a.Y1==b.Y1&&a.X2==b.X2&&a.Y2==b.Y2&&a.Weight==b.Weight;}
void Expected(const TS1RouteSceneResult& r,const std::vector<TS1RouteRect>& expected){
 Check(r.Status==TS1RouteSceneStatus::Ready&&r.Rectangles.size()==expected.size(),"source scene status/count differs");
 for(size_t i=0;i<expected.size();++i)Check(Equal(r.Rectangles[i],expected[i]),"source scene ordered rectangle differs");
}
void Admission(){
 TS1RouteSceneTile tile;tile.Room=1;
 Check(TS1RouteSceneTileAdmitted(tile,1,1),"room tile without special ground rejected");
 tile.GroundByte=0;Check(!TS1RouteSceneTileAdmitted(tile,1,1),"special ground tile omitted object flag requirement");
 tile.HasObjectFlag1000=true;Check(TS1RouteSceneTileAdmitted(tile,1,1),"object flag1000 did not admit special ground");
 tile.HasObjectFlag1000=false;Check(TS1RouteSceneTileAdmitted(tile,1,2),"nonfirst story incorrectly used first-story ground check");
 tile.GroundByte=-1;tile.Room=-5;Check(TS1RouteSceneTileAdmitted(tile,1,1),"room sentinel -5 rejected");
 tile.Room=2;Check(!TS1RouteSceneTileAdmitted(tile,1,1),"foreign room admitted without portal");
 tile.PortalToRequestedRoom=true;Check(TS1RouteSceneTileAdmitted(tile,1,1),"matching portal did not admit foreign-room tile");
 tile.GroundByte=-2;Check(!TS1RouteSceneTileAdmitted(tile,1,1),"signed ground check treated all negative bytes as sentinel -1");
}
void OuterAndMerge(){
 auto in=Grid();auto result=BuildTS1RouteScene(in);
 Expected(result,{{-7,-7,23,119,INT32_MAX},{89,-7,119,119,INT32_MAX},{-7,-7,119,23,INT32_MAX},{-7,89,119,119,INT32_MAX}});
 Check(result.InflatedPrefix==4&&result.MinX==1&&result.MaxX==5&&result.MinY==1&&result.MaxY==5,"border room bounds expanded outside source interior");
 in=Grid(false);Tile(in,3,3).Room=1;result=BuildTS1RouteScene(in);
 Expected(result,{{9,9,39,103,INT32_MAX},{73,9,103,103,INT32_MAX},{9,9,103,39,INT32_MAX},{9,73,103,103,INT32_MAX},
  {25,25,87,55,INT32_MAX},{25,41,55,87,INT32_MAX},{57,41,87,87,INT32_MAX},{41,57,87,87,INT32_MAX}});
 Check(result.MinX==2&&result.MaxX==4&&result.MinY==2&&result.MaxY==4,"single room tile source envelope differs");
 in=Grid(false);Check(BuildTS1RouteScene(in).Status==TS1RouteSceneStatus::NoRoom,"empty room invented bounds");
 in=Grid();in.MaxRectangles=3;Check(BuildTS1RouteScene(in).Status==TS1RouteSceneStatus::ResourceLimit,"rectangle resource limit silently truncated result");
 in=Grid();in.Tiles.pop_back();Check(BuildTS1RouteScene(in).Status==TS1RouteSceneStatus::InvalidInput,"short source layer admitted");
}
void Walls(){
 auto in=Grid();auto& tile=Tile(in,3,3);tile.WallBits=0x3c;
 auto result=BuildTS1RouteScene(in);Check(result.Rectangles.size()==8&&result.InflatedPrefix==8,"orthogonal+diagonal wall count differs");
 const std::vector<TS1RouteRect> expected={{41,57,71,71,INT32_MAX},{57,41,71,71,INT32_MAX},{45,53,59,67,INT32_MAX},{53,45,67,59,INT32_MAX}};
 for(size_t i=0;i<expected.size();++i)Check(Equal(result.Rectangles[4+i],expected[i]),"wall bit order or diagonal shrink/inflate differs");
 tile.WallBits=0x20;result=BuildTS1RouteScene(in);
 Check(result.Rectangles.size()==6&&Equal(result.Rectangles[4],{45,45,59,59,INT32_MAX})&&Equal(result.Rectangles[5],{53,53,67,67,INT32_MAX}),"other diagonal producer differs");
 tile.WallBits=0x0c;
 for(int style:{3,5,6,15,23}){tile.WallStyle8=tile.WallStyle4=int16_t(style);Check(BuildTS1RouteScene(in).Rectangles.size()==4,"excluded wall style emitted blocking line");}
 for(int style:{0,1,7}){tile.WallStyle8=tile.WallStyle4=int16_t(style);Check(BuildTS1RouteScene(in).Rectangles.size()==6,"ordinary wall style lost blocking line");}
}
void OptionalWeights(){
 auto in=Grid();in.RoomRouteFlag72=true;in.WorldZoningKind=1;
 for(int y=1;y<6;++y)for(int x=1;x<6;++x)Tile(in,x,y).FloorWord=1;
 Tile(in,1,1).TerrainFlags=0x20;
 auto result=BuildTS1RouteScene(in);
 Check(result.Status==TS1RouteSceneStatus::Ready&&result.InflatedPrefix==4&&result.Rectangles.size()==5,"optional phase prefix/count differs");
 Check(Equal(result.Rectangles[4],{16,16,96,96,10}),"optional merged floor weight or prefix-only inflation differs");
 // Later terrain flag changes do not split the source rectangle: source uses
 // the first tile's weight and tests only floor/admission while extending.
 Tile(in,4,4).TerrainFlags=0x20;Check(Equal(BuildTS1RouteScene(in).Rectangles[4],result.Rectangles[4]),"optional run incorrectly split by later terrain flags");
 in.TerrainOverride=true;Check(BuildTS1RouteScene(in).Rectangles[4].Weight==0,"global terrain override ignored");
 in.OptionalPhaseRequested=false;Check(BuildTS1RouteScene(in).Rectangles.size()==4,"disabled optional phase emitted weights");in.OptionalPhaseRequested=true;
 in.RoomRouteFlag72=false;Check(BuildTS1RouteScene(in).Rectangles.size()==4,"missing room flag72 enabled optional phase");in.RoomRouteFlag72=true;
 in.WorldZoningKind=0;Check(BuildTS1RouteScene(in).Rectangles.size()==4,"wrong world zoning enabled optional phase");in.WorldZoningKind=1;
 in.Story=2;Check(BuildTS1RouteScene(in).Rectangles.size()==4,"nonfirst story enabled optional phase");in.Story=1;
 Tile(in,3,3).FloorWord=0;result=BuildTS1RouteScene(in);
 for(size_t i=result.InflatedPrefix;i<result.Rectangles.size();++i){const auto&r=result.Rectangles[i];Check(!(r.X1<56&&56<r.X2&&r.Y1<56&&56<r.Y2),"optional floor merge covered missing floor tile");}
}
void ObjectGeometry(){
 TS1RouteObjectObstacleInput input;input.Bounds={10,20,30,40,0};input.CallerBounds={100,100,110,110,0};
 auto result=BuildTS1RouteObjectObstacle(input);
 Check(result&&Equal(*result,{7,17,33,43,INT32_MAX}),"solid object bounds or final margin differs");
 input.RoutePenalty=1;result=BuildTS1RouteObjectObstacle(input);
 Check(result&&Equal(*result,{7,17,33,43,10}),"penalized object source weight differs");
 input.ObjectType=8;
 const uint8_t masks[]={1,4,0x10,0x40,0x55};
 const std::vector<TS1RouteRect> expected={{4,14,36,27,10000},{23,14,36,46,10000},{4,33,36,46,10000},{4,14,17,46,10000},{4,14,17,46,10000}};
 for(size_t i=0;i<5;++i){input.PortalMask=masks[i];result=BuildTS1RouteObjectObstacle(input);Check(result&&Equal(*result,expected[i]),"portal edge overwrite or two margin stages differ");}
 input.PortalMask=1;input.CallerBounds={10,18,12,20,0};
 Check(!BuildTS1RouteObjectObstacle(input),"caller test omitted the portal's preliminary margin");
 input.ObjectType=0;input.PortalMask=0;input.CallerBounds={32,20,40,30,0};
 result=BuildTS1RouteObjectObstacle(input);
 Check(result&&Equal(*result,{7,17,33,43,10}),"ordinary caller intersection incorrectly ran after final inflation");
 input.CallerBounds={29,20,40,30,0};Check(!BuildTS1RouteObjectObstacle(input),"object overlapping caller admitted");
 input.CallerBounds={30,20,40,30,0};Check(bool(BuildTS1RouteObjectObstacle(input)),"strict edge-only caller contact rejected");
 input.CallerBounds={20,30,20,30,0};
 Check(bool(BuildTS1RouteObjectObstacle(input)),"Win32 caller test rejected an empty numeric rectangle");
 input.CallerBounds={25,35,15,25,0};
 Check(bool(BuildTS1RouteObjectObstacle(input)),"Win32 caller test normalized an inverted numeric rectangle");
 input.CallerBounds={0,0,100,100,0};input.Bounds={20,30,20,30,0};
 Check(bool(BuildTS1RouteObjectObstacle(input)),"empty source object was excluded by graph overlap instead of Win32 intersection");
 input.Bounds={25,35,15,25,0};
 Check(bool(BuildTS1RouteObjectObstacle(input)),"inverted source object was excluded by graph overlap instead of Win32 intersection");
 input.CallerBounds={100,100,110,110,0};input.Bounds={INT32_MIN,20,INT32_MAX,40,0};
 result=BuildTS1RouteObjectObstacle(input);
 Check(result&&Equal(*result,{INT32_MAX-2,17,INT32_MIN+2,43,10}),"literal endpoint inflation did not preserve source32-bit arithmetic");
 input.ObjectType=8;input.PortalMask=1;input.Bounds={10,INT32_MAX,30,INT32_MAX,0};
 result=BuildTS1RouteObjectObstacle(input);
 Check(result&&Equal(*result,{4,INT32_MAX-6,36,INT32_MIN+6,10000}),"portal edge endpoint addition/inflation overflowed or normalized");
 input.PortalMask=4;input.Bounds={INT32_MIN,20,INT32_MIN,40,0};
 result=BuildTS1RouteObjectObstacle(input);
 Check(result&&Equal(*result,{INT32_MAX-6,14,INT32_MIN+6,46,10000}),"portal endpoint subtraction/inflation did not use source32-bit words");
}
}
int main(){try{Admission();OuterAndMerge();Walls();OptionalWeights();ObjectGeometry();std::cout<<"PASS source TS1 scene rectangle controls\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
