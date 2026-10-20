#include "odh/wall_appearance.hpp"
#include <iostream>
#include <sstream>
namespace {
int checks=0;void check(bool ok){++checks;if(!ok)throw std::runtime_error("wall appearance assertion #"+std::to_string(checks));}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}check(caught);}
void set(std::string& b,std::size_t at,std::uint32_t v,unsigned n=4){for(unsigned i=0;i<n;++i)b.at(at+i)=static_cast<char>((v>>(8*i))&255);}
odh::SavedArchitecture architecture(){odh::SavedArchitecture a;a.width=a.height=4;for(auto& l:a.levels){l.walls.resize(16);for(auto& w:l.walls)w.segments=3;}return a;}
odh::WallAppearanceInput input(std::int16_t id=1){return {id,1,123,odh::EntityKind::object,odh::EntityPosition{24,40,1,0},false,0x8000,1,odh::ObjdWallAppearance{138,123,3,0,164,0}};}
}
int main(){try{
 for(unsigned version:{136U,138U}){std::string b(216,'\0');set(b,0,version);set(b,28,123);set(b,92,22,2);set(b,162,65535,2);
  std::istringstream s(b);auto r=odh::read_objd_wall_appearance(odh::View(s,b.size()));check(r.version==version&&r.guid==123&&r.wall_style==22);check(r.header_extent==(version==136?94:164));check(bool(r.sprite_base)==(version==138));if(r.sprite_base)check(*r.sprite_base==65535);
  for(std::size_t n=0;n<r.header_extent;++n){std::istringstream truncated(b.substr(0,n));rejects([&]{odh::read_objd_wall_appearance(odh::View(truncated,n));});}}
 auto a=architecture();auto in=input();auto result=odh::derive_wall_appearance(a,{in});check(result.faces.size()==1&&result.faces[0].segment==2&&result.faces[0].door&&result.faces[0].door_owner==1&&result.faces[0].x==1&&result.faces[0].y==2);
 for(std::uint8_t dir=0;dir<8;++dir)for(unsigned placement=0;placement<16;++placement){in=input();in.position->direction=dir;in.placement=static_cast<std::uint16_t>(placement);result=odh::derive_wall_appearance(a,{in});const unsigned off=dir%2?0:dir/2;unsigned applied=0,ignored=0;
  for(unsigned rel=0;rel<4;++rel)if(placement&(1U<<rel)){auto side=(off+rel)%4;if(side==0||side==3)applied|=1U<<side;else ignored|=1U<<side;}
  check(result.applications.size()==1&&result.applications[0].applied_sides==applied&&result.applications[0].ignored_sides==ignored);check(result.faces.size()==unsigned(bool(applied&1))+unsigned(bool(applied&8)));}
 auto door=input(1),window=input(2);window.flags2=0x4000;window.binding->wall_style=22;window.binding->sprite_base=65534;result=odh::derive_wall_appearance(a,{door,window});check(result.overwritten_faces==1&&result.faces[0].owner==2&&result.faces[0].door_owner==1&&result.faces[0].door);check(result.faces[0].style.resource_guid==123&&result.faces[0].style.sprites==std::array<std::uint16_t,6>{65534,65535,0,1,2,3});
 door.binding->wall_style=0;result=odh::derive_wall_appearance(a,{window,door});check(!result.faces[0].door&&result.faces[0].style.raw_style==0&&result.faces[0].owner==1);window.binding->wall_style=0;result=odh::derive_wall_appearance(a,{input(1),window});check(result.faces[0].door&&result.faces[0].style.raw_style==0);
 auto high_clear=input(10),low_door=input(2);high_clear.binding->wall_style=0;low_door.binding->wall_style=3;
 result=odh::derive_wall_appearance_restore_order(a,{high_clear,low_door});check(!result.faces[0].door&&result.faces[0].door_owner==10&&result.faces[0].owner==10);
 auto reversed=odh::derive_wall_appearance_restore_order(a,{low_door,high_clear});check(reversed.faces.size()==result.faces.size()&&!reversed.faces[0].door&&reversed.faces[0].door_owner==10&&reversed.faces[0].owner==10&&reversed.applications[0].id==2&&reversed.applications[1].id==10);
 for(unsigned ignore=0;ignore<4;++ignore){auto ignored=input();ignored.binding.reset();if(ignore==0)ignored.position.reset();if(ignore==1)ignored.contained=true;if(ignore==2)ignored.flags2=0;if(ignore==3)ignored.kind=odh::EntityKind::avatar;check(odh::derive_wall_appearance(a,{ignored}).applications.empty());}
 auto bad=input();bad.binding.reset();rejects([&]{odh::derive_wall_appearance(a,{bad});});bad=input();bad.binding->guid=1;rejects([&]{odh::derive_wall_appearance(a,{bad});});bad=input();bad.binding->version=142;rejects([&]{odh::derive_wall_appearance(a,{bad});});bad=input();bad.binding->wall_style=22;bad.binding->sprite_base.reset();rejects([&]{odh::derive_wall_appearance(a,{bad});});bad=input();bad.binding->wall_style=256;rejects([&]{odh::derive_wall_appearance(a,{bad});});
 for(unsigned invalid=0;invalid<8;++invalid){bad=input();if(invalid==0)bad.id=0;if(invalid==1)bad.generation=0;if(invalid==2)bad.guid=0;if(invalid==3)bad.position->x=-1;if(invalid==4)bad.position->y=64;if(invalid==5)bad.position->level=3;if(invalid==6)bad.position->direction=8;if(invalid==7)bad.kind=static_cast<odh::EntityKind>(8);rejects([&]{odh::derive_wall_appearance(a,{bad});});}
 rejects([&]{odh::derive_wall_appearance(a,{input(),input()});});rejects([&]{odh::derive_wall_appearance(a,{input()},0);});auto original=a.levels[0].walls[9].top_right_style;auto noseg=a;noseg.levels[0].walls[9].segments=0;check(!odh::derive_wall_appearance(noseg,{input()}).faces[0].segment_present);check(a.levels[0].walls[9].top_right_style==original);
 auto old_profile=input();old_profile.binding->version=136;old_profile.binding->sprite_base.reset();check(odh::derive_wall_appearance(a,{old_profile}).faces.size()==1);old_profile.binding->sprite_base=0;rejects([&]{odh::derive_wall_appearance(a,{old_profile});});
 auto older_door=input(1),later_window=input(2);older_door.generation=77;later_window.generation=88;later_window.flags2=0x4000;result=odh::derive_wall_appearance(a,{older_door,later_window});check(result.faces[0].generation==88&&result.faces[0].door_generation==77);
 auto other=input(3);other.position->level=2;other.placement=8;result=odh::derive_wall_appearance(a,{other,input(1)});check(result.faces[0].level==1&&result.faces[1].level==2&&result.faces[1].segment==1);
 std::cout<<checks<<" wall appearance checks passed (synthetic only)\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
