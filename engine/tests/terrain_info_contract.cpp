// SPDX-License-Identifier: MPL-2.0
// Data-free contract: real registered handler, architecture map and entity selection.
// Expected values are fixed examples, not a second implementation of the formulas.
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMPrimitiveRegistration.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/primitives/VMGetTerrainInfo.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/OBJD.h"
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::LotView::Model::LotTilePos;
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){try{
 VMEntity::UseWorld=false;
 IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"terrain-query-fixture",true);
 OBJD definition;definition.GUID=0x12345678;definition.ChunkLabel="fixture";
 FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
 VMGameObject caller(&object,nullptr),stack(&object,nullptr);
 VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;
 context.Architecture(std::make_unique<VMArchitecture>(3,3,nullptr,&context));
 auto& arch=*context.Architecture();
 arch.Terrain->Heights={0,10,20,30,50,80,-15,-35,-65};
 // Preview and render centres intentionally disagree. Queries must use committed vertices.
 arch.Terrain->VisHeights.assign(9,990);arch.Terrain->Centers.assign(9,770);
 VMThread thread(&context,&caller,0);VMStackFrame frame;frame.Thread=&thread;frame.Caller=&caller;frame.StackObject=&stack;
 VMGetTerrainInfoOperand op;op.Unknown.assign(5,0);
 auto handler=dynamic_cast<VMGetTerrainInfo*>(context.Primitives[63]->GetHandler());
 need(handler!=nullptr,"opcode63 must dispatch the real terrain handler");
 auto check=[&](const char* label,LotTilePos pos,std::array<int16_t,4> alt,std::array<int16_t,2> slope){
  for(bool useMe:{false,true})for(bool interpolate:{false,true})for(int direction:{1,4,16,64}){
   caller.Position(useMe?pos:LotTilePos::OUT_OF_WORLD);stack.Position(useMe?LotTilePos::OUT_OF_WORLD:pos);
   auto selected=useMe?&caller:&stack;selected->Direction(static_cast<VMEntity::DirectionT>(direction));
   op.Flags=(useMe?2:0)|(interpolate?1:0);
   const auto map=arch.Terrain->Heights;
   for(int mode:{0,1}){
    thread.TempRegisters.fill(1234);op.Mode=mode;
    need(handler->Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_TRUE,"valid terrain query result");
    const size_t count=mode==0?4:2;
    for(size_t i=0;i<count;i++)need(thread.TempRegisters[i]==(mode==0?alt[i]:slope[i]),label);
    for(size_t i=count;i<thread.TempRegisters.size();i++)need(thread.TempRegisters[i]==1234,"unrequested temporaries preserved");
    need(arch.Terrain->Heights==map&&selected->Position()==pos,"read-only query preserves map and position");
   }
  }
  std::cout<<"PASS "<<label<<" (UseMe/StackObject, flag, rotation)\n";
 };
 arch.Terrain->Heights.assign(9,350);
 check("flat map",LotTilePos(24,24,1),{35,35,35,35},{0,0});
 arch.Terrain->Heights={0,10,20,30,50,80,-15,-35,-65};
 // Asymmetric 3x3 map: detects vertex ordering, x/y sign and edge clamping.
 check("interior",LotTilePos(8,8,1),{0,1,3,5},{-1,3});
 check("subtile and level invariance",LotTilePos(15,15,4),{0,1,3,5},{-1,3});
 check("interior negative corners",LotTilePos(24,24,1),{5,8,-3,-6},{0,-11});
 check("right edge",LotTilePos(40,8,1),{2,2,8,8},{0,6});
 check("bottom edge",LotTilePos(8,40,1),{-1,-3,-1,-3},{2,0});
 check("corner",LotTilePos(40,40,1),{-6,-6,-6,-6},{0,0});
 // FreeSO linear ushort GetOffset aliases these coordinates; do not add a
 // coordinate clamp or interpolate them into a new behavior.
 check("negative-x linear alias",LotTilePos(-8,24,1),{2,3,8,-1},{4,0});
 check("past-right linear alias",LotTilePos(56,8,1),{3,2,-1,8},{-4,0});
 arch.Terrain->Heights={-19,0,0,0,-1,0,0,0,0};
 check("division truncates toward zero",LotTilePos(8,8,1),{-1,0,0,0},{0,0});
 arch.Terrain->Heights={-32768,32767,0,-32768,32767,0,0,0,0};
 check("wide signed intermediate",LotTilePos(8,8,1),{-3276,3276,-3276,3276},{-6553,0});
 for(int mode:{0,1})for(bool useMe:{false,true}){
  op.Flags=useMe?2:0;op.Mode=mode;caller.Position(LotTilePos::OUT_OF_WORLD);stack.Position(LotTilePos::OUT_OF_WORLD);
  thread.TempRegisters.fill(1234);need(handler->Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_FALSE,"OOW false before reading architecture");
  for(auto v:thread.TempRegisters)need(v==1234,"OOW preserves all temporaries");
  auto selected=useMe?&caller:&stack;
  for(auto pos:{LotTilePos(-8,8,1),LotTilePos(8,56,1)}){
   selected->Position(pos);bool threw=false;try{handler->Execute(&frame,&op);}catch(const VMSimanticsException&){threw=true;}
   need(threw,"out-of-array coordinates raise caught VM exception instead of unchecked C++ access");
  }
  selected->Position(LotTilePos(8,8,1));op.Flags=useMe?2:0;
  if(useMe)frame.Caller=nullptr;else frame.StackObject=nullptr;
  bool threw=false;try{handler->Execute(&frame,&op);}catch(const VMSimanticsException&){threw=true;}
  need(threw,"missing selected entity remains VM exception");frame.Caller=&caller;frame.StackObject=&stack;
 }
 std::cout<<"PASS false/exception paths\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
