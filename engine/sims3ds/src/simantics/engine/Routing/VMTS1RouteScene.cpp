// SPDX-License-Identifier: MPL-2.0
#include "simantics/engine/Routing/VMTS1RouteScene.h"
#include <algorithm>
namespace FSO { namespace SimAntics { namespace Engine { namespace Routing {
namespace {
int32_t Word32(uint32_t bits){
 return bits<=uint32_t(INT32_MAX)?int32_t(bits):int32_t(int64_t(bits)-INT64_C(0x100000000));
}
int32_t Add32(int32_t value,int32_t delta){return Word32(uint32_t(value)+uint32_t(delta));}
int32_t Sub32(int32_t value,int32_t delta){return Word32(uint32_t(value)-uint32_t(delta));}
}
bool TS1MakeRouteObjectObstacle(const TS1RouteRect& bounds,int type,int16_t penalty,
 uint8_t mask,const TS1RouteRect& callerBounds,int margin,TS1RouteRect& output){
 TS1RouteObjectObstacleInput input;input.Bounds=bounds;input.CallerBounds=callerBounds;
 input.ObjectType=int16_t(type);input.RoutePenalty=penalty;input.PortalMask=mask;input.Margin=margin;
 auto result=BuildTS1RouteObjectObstacle(input);if(!result)return false;output=*result;return true;
}
std::optional<TS1RouteRect> BuildTS1RouteObjectObstacle(const TS1RouteObjectObstacleInput& input){
 auto rect=input.Bounds;rect.Weight=input.RoutePenalty?10:INT32_MAX;
 // DVD rectangle operations use 32-bit words, including literal persisted
 // endpoints. Express wrap explicitly instead of invoking signed overflow.
 const auto inflate=[&]{rect.X1=Sub32(rect.X1,input.Margin);rect.Y1=Sub32(rect.Y1,input.Margin);rect.X2=Add32(rect.X2,input.Margin);rect.Y2=Add32(rect.Y2,input.Margin);};
 if(input.ObjectType==8){
  for(unsigned bit:{1u,4u,0x10u,0x40u})if(input.PortalMask&bit){
   rect=input.Bounds;rect.Weight=10000;
   if(bit==1)rect.Y2=Add32(rect.Y1,1);
   else if(bit==4)rect.X1=Sub32(rect.X2,1);
   else if(bit==0x10)rect.Y1=Sub32(rect.Y2,1);
   else rect.X2=Add32(rect.X1,1);
   inflate();
  }
 }
 // This particular source call is Win32 IntersectRect, not graph46966e's
 // four-comparison predicate. Empty or inverted input cannot intersect.
 if(std::max(rect.X1,input.CallerBounds.X1)<std::min(rect.X2,input.CallerBounds.X2)&&
    std::max(rect.Y1,input.CallerBounds.Y1)<std::min(rect.Y2,input.CallerBounds.Y2))return std::nullopt;
 if(input.ObjectType==2||rect.Weight==INT32_MAX||rect.Weight>7)inflate();
 return rect;
}
bool TS1RouteSceneTileAdmitted(const TS1RouteSceneTile& tile,int16_t room,int8_t story){
 if(tile.Room!=room&&tile.Room!=-5&&!tile.PortalToRequestedRoom)return false;
 // 4790bc: first story and signed ground byte other than -1.
 return story!=1||tile.GroundByte==-1||tile.HasObjectFlag1000;
}
TS1RouteSceneResult BuildTS1RouteScene(const TS1RouteSceneInput& input){
 TS1RouteSceneResult result;const int size=input.WorldSize;
 if(size<3||size>128||input.Tiles.size()!=size_t(size)*size||input.MaxRectangles>INT32_MAX)return result;
 const auto tile=[&](int x,int y)->const TS1RouteSceneTile&{return input.Tiles[size_t(y)*size+x];};
 const auto admitted=[&](int x,int y){return TS1RouteSceneTileAdmitted(tile(x,y),input.RequestedRoom,input.Story);};
 bool found=false;int minX=size,maxX=0,minY=size,maxY=0;
 for(int y=1;y<size-1;++y)for(int x=1;x<size-1;++x)if(tile(x,y).Room==input.RequestedRoom){
  found=true;minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);
 }
 if(!found){result.Status=TS1RouteSceneStatus::NoRoom;return result;}
 if(minX>1)--minX;if(maxX<size-2)++maxX;if(minY>1)--minY;if(maxY<size-2)++maxY;
 result.MinX=minX;result.MaxX=maxX;result.MinY=minY;result.MaxY=maxY;
 const auto add=[&](TS1RouteRect r){
  if(result.Rectangles.size()>=input.MaxRectangles){result.Status=TS1RouteSceneStatus::ResourceLimit;return false;}
  result.Rectangles.push_back(r);return true;
 };
 const auto rect=[](int x1,int y1,int x2,int y2,int weight){return TS1RouteRect{x1*16,y1*16,x2*16,y2*16,weight};};
 if(!add(rect(minX-1,minY-1,minX,maxY+2,INT32_MAX))||
    !add(rect(maxX+1,minY-1,maxX+2,maxY+2,INT32_MAX))||
    !add(rect(minX-1,minY-1,maxX+2,minY,INT32_MAX))||
    !add(rect(minX-1,maxY+1,maxX+2,maxY+2,INT32_MAX)))return result;
 const auto overlap=[&](const TS1RouteRect& r){
  for(size_t i=0;i<result.Rectangles.size();++i)if(VMTS1RouteGraph::Overlaps(r,result.Rectangles[i]))return int(i);
  return -1;
 };
 const auto excludedStyle=[](int style){return style==3||style==5||style==6||style==15||style==23;};
 for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x){
  if(!admitted(x,y)){
   const int previous=overlap(rect(x,y,x+1,y+1,INT32_MAX));
   if(previous>=0){const int right=result.Rectangles[previous].X2/16;if(x!=right)x=right-1;continue;}
   int right=x+1;while(right<=maxX&&!admitted(right,y))++right;
   int bottom=y+1;
   for(;;){bool blocked=true;for(int scan=x;scan<right;++scan)if(admitted(scan,bottom)){blocked=false;break;}
    if(!blocked||bottom>maxY)break;++bottom;
   }
   if(!add(rect(x,y,right,bottom,INT32_MAX)))return result;
  }else{
   const auto& t=tile(x,y);
   if((t.WallBits&8)&&!excludedStyle(t.WallStyle8)&&admitted(x,y+1))
    if(!add(rect(x,y+1,x+1,y+1,INT32_MAX)))return result;
   if((t.WallBits&4)&&!excludedStyle(t.WallStyle4)&&admitted(x+1,y))
    if(!add(rect(x+1,y,x+1,y+1,INT32_MAX)))return result;
   if(t.WallBits&0x30){
    auto a=rect(x,y,x+1,y+1,INT32_MAX),b=a;
    if(t.WallBits&0x10){a.Y1+=8;a.X2-=8;b.X1+=8;b.Y2-=8;}
    else{b.X1+=8;b.Y1+=8;a.Y2-=8;a.X2-=8;}
    a.X1+=4;a.Y1+=4;a.X2-=4;a.Y2-=4;b.X1+=4;b.Y1+=4;b.X2-=4;b.Y2-=4;
    if(!add(a)||!add(b))return result;
   }
  }
 }
 result.InflatedPrefix=uint32_t(result.Rectangles.size());
 const bool optional=input.OptionalPhaseRequested&&input.Story==1&&input.RoomRouteFlag72&&input.WorldZoningKind==1;
 if(optional)for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x){
  if(!tile(x,y).FloorWord)continue;
  const int weight=!input.TerrainOverride&&(tile(x,y).TerrainFlags&0x20)?10:0;
  const int previous=overlap(rect(x,y,x+1,y+1,weight));
  if(previous>=0){const int right=result.Rectangles[previous].X2/16;if(x!=right)x=right-1;continue;}
  int right=x+1;while(right<=maxX&&tile(right,y).FloorWord&&admitted(right,y))++right;
  int bottom=y+1;
  for(;;){bool matches=true;for(int scan=x;scan<right;++scan)if(!tile(scan,bottom).FloorWord||!admitted(scan,bottom)){matches=false;break;}
   if(!matches||bottom>maxY)break;++bottom;
  }
  // The source tests only floor/admission while extending: later terrain flags
  // do not split a run or change the first tile's selected weight.
  if(!add(rect(x,y,right,bottom,weight)))return result;
 }
 for(size_t i=0;i<result.InflatedPrefix;++i){auto& r=result.Rectangles[i];r.X1-=7;r.Y1-=7;r.X2+=7;r.Y2+=7;}
 result.Status=TS1RouteSceneStatus::Ready;return result;
}
}}}}
