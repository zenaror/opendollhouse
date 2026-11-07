// SPDX-License-Identifier: MPL-2.0
#include "odh/architecture_edit.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace odh;
namespace {
void require(bool p,const char*s){if(!p){std::cerr<<s<<'\n';std::exit(1);}}
PreparedArchitecture lot(){PreparedArchitecture a;a.width=a.height=8;a.buildable.assign(64,1);for(auto&l:a.levels){l.walls.resize(64);l.floors.assign(64,5);}return a;}
ArchitectureEditServices services(){
 ArchitectureEditServices s;s.rectangle_clip=ArchitectureEditRectangleClip{0,0,8,8,2};
 s.material=[](auto kind,std::uint16_t id){if(id==99)return ArchitectureEditMaterial{ArchitectureEditLookup::absent,0,false};return ArchitectureEditMaterial{ArchitectureEditLookup::available,kind==ArchitectureEditMaterialKind::wall_style?10:(id==5?5:9),true};};
 s.in_clip=[](auto,auto,auto)->std::optional<bool>{return true;};
 s.wall_valid=[](const auto&,auto,auto,auto,const auto&)->std::optional<bool>{return true;};
 s.floor_valid=[](const auto&,auto,auto,auto,auto)->std::optional<bool>{return true;};
 s.supported=[](auto,auto,auto)->std::optional<bool>{return true;};s.below_outside=[](auto,auto,auto)->std::optional<bool>{return false;};return s;
}
ArchitectureEditCommand cmd(ArchitectureEditType t,int x,int y,int x2=0,int y2=0,std::uint16_t p=7,std::uint16_t style=1,int level=1){return {t,x,y,x2,y2,std::int8_t(level),p,style};}
const PreparedArchitecture& ready(const ArchitectureEditResult&r){require(r.status==ArchitectureEditStatus::ready&&r.candidate&&r.rooms,"candidate and regenerated rooms must both be ready");return *r.candidate;}
}
int main(){
 auto a=lot();auto s=services();
 auto r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_line,2,2,2,0)},s);auto b=ready(r);
 require(r.cost==20&&r.receipts[0].changed_units==2,"wall line source segment price");
 require(b.levels[0].walls[18].segments==2&&b.levels[0].walls[10].segments==8,"cardinal wall publishes reciprocal main and secondary segments");
 require(a.levels[0].walls[18].segments==0,"source architecture is immutable");
 r=prepare_architecture_edit(b,{cmd(ArchitectureEditType::wall_delete,2,2,2,0)},s);
 require(r.cost==-14&&ready(r).levels[0].walls[18].segments==0,"wall erase fixed seven refund and mirrored clearing");
 require(ready(r).levels[0].walls[18].top_right_pattern==7,"source erase retains now-unused wall metadata");
 r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_rect,2,2,2,2)},s);b=ready(r);
 require(r.cost==80&&r.receipts[0].changed_units==8,"rectangle preflights four sides then draws source order");
 require(r.rooms->rooms.size()>2,"closed room causes full room regeneration");
 r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_line,2,2,1,1)},s);b=ready(r);
 require(b.levels[0].walls[18].segments==32&&b.levels[0].walls[18].top_left_style==5&&b.levels[0].walls[18].top_left_pattern==5&&b.levels[0].floors[18]==0,"diagonal moves floor into two wall half fields");
 for(int d=0;d<8;d++){r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_line,3,3,1,d)},s);require(r.cost==10&&r.receipts[0].changed_units==1,"all eight line directions supported");ready(r);}
 auto denied=s;denied.wall_valid=[](const auto&,auto x,auto,auto,const auto&)->std::optional<bool>{return x!=4;};
 r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_line,3,3,3,0)},denied);
 require(r.cost==0&&r.receipts[0].rejected&&ready(r).levels[0].walls[27].segments==0,"line validation denies whole line before first segment");
 denied=s;denied.supported=[](auto,auto,auto)->std::optional<bool>{return false;};
 r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_line,3,3,1,1,7,1,2)},denied);
 require(r.receipts[0].rejected&&r.cost==0,"upper diagonal needs actual support");
 auto wallpaper=a;auto&w=wallpaper.levels[0].walls[18];w.segments=2;w.top_right_style=1;w.top_right_pattern=5;
 r=prepare_architecture_edit(wallpaper,{cmd(ArchitectureEditType::pattern_dot,2,2,1,0)},s);
 require(r.cost==5&&ready(r).levels[0].walls[18].top_right_pattern==7,"wallpaper dot new price less source eighty percent refund");
 r=prepare_architecture_edit(*r.candidate,{cmd(ArchitectureEditType::pattern_dot,2,2,1,0)},s);require(r.cost==0&&r.receipts[0].changed_units==0,"painting identical wallpaper is source no-op");
 r=prepare_architecture_edit(wallpaper,{cmd(ArchitectureEditType::pattern_dot,2,2,0,1)},s);require(ready(r).levels[0].walls[18].top_right_pattern==7,"wallpaper dot alternate side is used when primary absent");
 r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::floor_rect,2,2)},s);require(r.cost==5&&ready(r).levels[0].floors[18]==7&&r.receipts[0].changed_units==2,"full floor counted as two halves with exact integer refund");
 r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::floor_rect,2,2,1,1)},s);require(r.cost==20&&r.receipts[0].changed_units==8,"floor rectangle width and height are inclusive offsets");
 auto diag=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_line,2,2,1,1)},s).candidate.value();
 r=prepare_architecture_edit(diag,{cmd(ArchitectureEditType::floor_rect,2,2,0,0,7,0)},s);require(r.receipts[0].changed_units==1&&r.cost==2&&ready(r).levels[0].walls[18].top_left_style==7,"diagonal dot changes exact half with half-floor integer cost");
 r=prepare_architecture_edit(diag,{cmd(ArchitectureEditType::floor_rect,2,2,1,1)},s);require(ready(r).levels[0].walls[18].top_left_style==5,"source ordinary floor rectangle skips diagonal halves");
 auto box=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_rect,2,2,2,2)},s).candidate.value();
 r=prepare_architecture_edit(box,{cmd(ArchitectureEditType::floor_fill,2,2)},s);require(r.receipts[0].changed_units==8&&r.cost==20&&ready(r).levels[0].floors[0]==5,"floor fill respects closed four-tile room");
 r=prepare_architecture_edit(box,{cmd(ArchitectureEditType::pattern_fill,2,2,0,0,8)},s);require(r.receipts[0].changed_units==8&&r.cost==14,"wallpaper fill paints only inward wall faces with source refund");ready(r);
 auto fence=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_rect,2,2,2,2,7,2)},s).candidate.value();
 r=prepare_architecture_edit(fence,{cmd(ArchitectureEditType::floor_fill,2,2)},s);require(r.receipts[0].changed_units==8,"fences retain floor-fill barriers although wallpaper treats style nonthick");
 auto missing=s;missing.material=[](auto,std::uint16_t)->ArchitectureEditMaterial{return {};};
 r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::floor_rect,2,2)},missing);require(r.status==ArchitectureEditStatus::unsupported&&!r.candidate&&!r.diagnostic.empty(),"unknown material is not a free texture");
 r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::floor_rect,2,2,0,0,99)},s);require(r.cost==0&&ready(r).levels[0].floors[18]==5,"known absent requested nonzero material is source no-op");
 missing=s;missing.floor_valid={};r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_line,2,2,1,0),cmd(ArchitectureEditType::floor_rect,2,2)},missing);
 require(r.status==ArchitectureEditStatus::unsupported&&!r.candidate&&a.levels[0].walls[18].segments==0,"unknown later authority publishes no partial batch");
 denied=s;denied.floor_valid=[](const auto&,auto x,auto,auto,auto)->std::optional<bool>{return x!=2;};
 r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::floor_rect,2,2,1,0)},denied);require(r.receipts[0].changed_units==2&&ready(r).levels[0].floors[18]==5&&r.candidate->levels[0].floors[19]==7,"known floor denial is per cell as in source");
 missing=s;missing.rectangle_clip.reset();r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::floor_rect,2,2,1,0)},missing);require(r.status==ArchitectureEditStatus::unsupported&&!r.candidate,"rectangular clip is distinct explicit authority from fine OutsideClip");
 ArchitectureEditLimits limits;limits.work=1;r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_rect,2,2,2,2)},s,limits);require(r.status==ArchitectureEditStatus::budget&&!r.candidate,"work exhaustion cannot publish partial geometry or charge");
 r=prepare_architecture_edit(a,{cmd(ArchitectureEditType::wall_line,2,2,1,8)},s);require(r.status==ArchitectureEditStatus::invalid,"invalid direction is not masked");
 auto bad=a;bad.levels[1].floors.pop_back();r=prepare_architecture_edit(bad,{},s);require(r.status==ArchitectureEditStatus::invalid,"malformed second-story extent cannot be hidden by ground command");
 auto support_lot=lot();support_lot.levels[1].floors.assign(64,0);
 auto rooms=generate_prepared_room_map(support_lot);std::vector<std::uint8_t>objects(64);
 auto sup=regenerate_architecture_support(support_lot,rooms,objects);
 require(sup&&(*sup)[27]==0,"unsupported upper exterior remains unsupported");
 objects[27]=1;sup=regenerate_architecture_support(support_lot,rooms,objects);
 require(sup&&(*sup)[27]==1&&(*sup)[28]==0,"category8 support supports own tile, not a guessed radius without upper neighbors");
 support_lot.levels[1].floors[29]=7;sup=regenerate_architecture_support(support_lot,rooms,objects);
 require(sup&&(*sup)[28]==1,"upper neighbor floor plus source nearby object support enables bounded spread");
 objects[27]=0;sup=regenerate_architecture_support(support_lot,rooms,objects);
 require(sup&&(*sup)[28]==0,"removing source support removes spread on regeneration");
 require(!regenerate_architecture_support(support_lot,rooms,{}),"unknown ObjectSupport cannot default to empty grid");
 std::cout<<"architecture edit source command matrix passed\n";
}
