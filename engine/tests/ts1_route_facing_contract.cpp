// SPDX-License-Identifier: MPL-2.0
// DVD45c4fa/452129/4222bd controls, including a live original path witness.
#include "simantics/engine/Routing/VMTS1RoutePath.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace FSO::SimAntics::Engine::Routing;
namespace {
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
bool Equal(TS1MotionPoint p,int32_t x,int32_t y){return p.X==x&&p.Y==y;}
std::vector<TS1MotionPoint> Physical(){return {{361,486},{369,471},{371,467},{381,449},{397,445}};}
}
int main(){try {
 TS1RouteFacing facing;facing.Facing=-2;facing.Flags=0x40ff;facing.Target={408,456};
 auto path=Physical();Check(TS1AppendRouteFacing(path,facing),"source route facing rejected");
 Check(path.size()==6&&Equal(path.back(),413,461),"live DVD orientation point differs");
 Check(Equal(path[4],397,445),"orientation overwrote physical goal");
 static constexpr int offset[8][2]={{0,-16},{16,-16},{16,0},{16,16},{0,16},{-16,16},{-16,0},{-16,-16}};
 for(unsigned target=0;target<8;++target)for(int mode=0;mode<8;++mode){
  facing.Facing=mode;facing.TargetHeading=target;facing.Flags=0x200;path=Physical();
  Check(TS1AppendRouteFacing(path,facing),"fixed heading rejected");auto& p=offset[(target+unsigned(mode))&7];
  Check(Equal(path.back(),397+p[0],445+p[1]),"fixed final heading used search Absolute flag or normalized diagonal");
 }
 facing.Facing=-3;path=Physical();Check(TS1AppendRouteFacing(path,facing)&&Equal(path.back(),413,441),"preserve approach did not extrapolate last segment");
 for(unsigned h=0;h<8;++h){path={{397,445},{397,445}};facing.CallerHeading=h;
  Check(TS1AppendRouteFacing(path,facing)&&Equal(path.back(),397+offset[h][0],445+offset[h][1]),"duplicate final segment did not use caller heading");}
 facing.Target={408,456};facing.TargetHeading=5;facing.Flags=1;facing.Facing=-1;path=Physical();
 Check(TS1AppendRouteFacing(path,facing)&&Equal(path.back(),381,429),"opposite facing did not reverse quantized target direction");
 for(int mode:{-1,-2}){facing.Facing=mode;facing.Flags=0x101;path=Physical();
  Check(TS1AppendRouteFacing(path,facing)&&Equal(path.back(),408,456),"direct-target branch quantized or reversed actual target");}
 facing.Facing=-2;facing.Flags=0x100;path=Physical();
 Check(TS1AppendRouteFacing(path,facing)&&Equal(path.back(),413,461),"point-only slot incorrectly took region direct-target branch");
 facing.Flags=0;facing.Target={397,445};facing.TargetHeading=6;path=Physical();
 Check(TS1AppendRouteFacing(path,facing)&&Equal(path.back(),381,445),"coincident target did not use its heading");
 facing.Facing=-1;path=Physical();Check(TS1AppendRouteFacing(path,facing)&&Equal(path.back(),413,445),"coincident opposite facing differs");
 facing.Flags=0x101;path=Physical();const auto n=path.size();
 Check(!TS1AppendRouteFacing(path,facing)&&path.size()==n,"zero direct-target tangent accepted or mutated path");
 path.clear();Check(!TS1AppendRouteFacing(path,facing)&&path.empty(),"empty physical path fabricated");
 path={{0,0}};Check(!TS1AppendRouteFacing(path,facing)&&path.size()==1,"single physical node fabricated");
 facing.Facing=2;facing.TargetHeading=0;path={{0,0},{INT32_MAX,0}};
 Check(TS1AppendRouteFacing(path,facing)&&Equal(path.back(),INT32_MIN+15,0),"DWORD endpoint addition overflow differs");
 std::cout<<"PASS source route-facing controls: "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
