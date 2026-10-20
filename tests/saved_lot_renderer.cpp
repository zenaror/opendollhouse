#include "saved_lot_renderer.hpp"
#include "saved_wall_appearance.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace {
int checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("saved lot assertion #"+std::to_string(checks));}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}check(caught);}
void set(std::string& b,std::size_t p,std::uint32_t v,unsigned n=4,bool big=false){for(unsigned i=0;i<n;++i)b.at(p+i)=static_cast<char>((v>>((big?n-1-i:i)*8))&255);}
std::string word(std::uint32_t v){std::string b(2,'\0');set(b,0,v,2);return b;}
std::string chunk(const std::string& type,std::uint16_t id,const std::string& payload){std::string b(76,'\0');b.replace(0,4,type);set(b,4,static_cast<std::uint32_t>(76+payload.size()),4,true);set(b,8,id,2,true);return b+payload;}
std::string fixture(){
 std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');
 std::string pal(19,'\0');set(pal,0,1);set(pal,4,1);pal[16]=10;pal[17]=20;pal[18]=30;b+=chunk("PALT",7,pal);
 std::string sprite(32,'\0');set(sprite,0,1000);set(sprite,4,1);set(sprite,8,7);set(sprite,12,16);set(sprite,16,2,2);set(sprite,18,2,2);set(sprite,20,1);
 for(int row=0;row<2;++row)sprite+=word(6)+word((6<<13)|2)+std::string("\0\0",2);
 sprite+=word(5<<13);b+=chunk("SPR2",513,sprite);return b;
}
std::string wall_fixture(){
 std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');
 std::string pal(19,'\0');set(pal,0,1);set(pal,4,1);pal[16]=90;pal[17]=100;pal[18]=110;b+=chunk("PALT",7,pal);
 auto spr=[](unsigned count){std::string s(12+4*count,'\0');set(s,0,1000);set(s,4,count);set(s,8,7);
  for(unsigned i=0;i<count;++i){set(s,12+i*4,static_cast<std::uint32_t>(s.size()));std::string f(8,'\0');set(f,4,1,2);set(f,6,1,2);
   s+=f+std::string("\4\6\2\1\0\0\5\0",8);}return s;};
 return b+chunk("SPR#",4098,spr(46))+chunk("SPR#",1026,spr(4))+chunk("SPR#",2049,spr(4));
}
struct Temp {
 std::filesystem::path p=std::filesystem::temp_directory_path()/("odh-live-lot-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 Temp(){std::filesystem::create_directory(p);}
 ~Temp(){std::error_code e;std::filesystem::remove_all(p,e);}
};
SavedArchitectureResource architecture(){SavedArchitectureResource a;a.architecture.width=a.architecture.height=4;
 for(auto& l:a.architecture.levels){l.floors.resize(16);l.walls.resize(16);}a.architecture.levels[0].floors[5]=1;
 a.architecture.altitude=std::vector<std::array<std::uint8_t,4>>(16,{0,0,0,0});a.architecture.grass=std::vector<std::uint8_t>(16,127);return a;}
std::uint32_t bits(float value){std::uint32_t result;std::memcpy(&result,&value,4);return result;}
std::string container_fixture(std::uint32_t guid,std::uint16_t type=0,bool duplicate=false,std::uint32_t x=bits(8)){
 std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');
 std::string objd(42,'\0');set(objd,0,138);set(objd,18,4,2);set(objd,28,guid);set(objd,40,128,2);b+=chunk("OBJD",1,objd);
 std::string slot(86,'\0');set(slot,4,10);slot.replace(8,4,"TOLS");set(slot,12,1);set(slot,16,type,2);set(slot,18,x);
 b+=chunk("SLOT",128,slot);if(duplicate)b+=chunk("SLOT",128,slot);return b;
}
void write(const std::filesystem::path& path,const std::string& data){std::ofstream file(path,std::ios::binary);file<<data;if(!file)throw std::runtime_error("container fixture write failed");}
odh::DefinitionCandidate candidate(const std::filesystem::path&,std::uint32_t);
std::string object_fixture(const odh::WorldProjection& camera,std::size_t pixel){
 std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');
 std::string objd(216,'\0');set(objd,0,138);set(objd,6,10,2);set(objd,8,2,2);set(objd,18,4,2);set(objd,28,123);set(objd,40,128,2);
 set(objd,128,5,2);set(objd,130,1,2);b+=chunk("OBJD",1,objd);
 std::string pal(19,'\0');set(pal,0,1);set(pal,4,1);pal[16]=static_cast<char>(210);pal[17]=10;pal[18]=20;b+=chunk("PALT",7,pal);
 std::string spr(32,'\0');set(spr,0,1000);set(spr,4,1);set(spr,8,7);set(spr,12,16);set(spr,16,1,2);set(spr,18,1,2);set(spr,20,7);
 spr+=word(8)+word((2<<13)|1)+std::string("\0\0\20\0",4)+word(5<<13);b+=chunk("SPR2",5,spr);
 for(unsigned i=0;i<2;++i){
  odh::DgrpSprite sprite{};sprite.object_offset_bits[2]=bits(i?-50.f:50.f);
  odh::Spr2Frame frame{};frame.width=frame.height=1;
  const auto p=odh::place_sprite(camera,sprite,frame,{1,1,0},0);
  const auto target_x=static_cast<std::int32_t>(pixel%camera.width),target_y=static_cast<std::int32_t>(pixel/camera.width);
  sprite.x=target_x-p.x;sprite.y=target_y-p.y;
  const auto adjusted=odh::place_sprite(camera,sprite,frame,{1,1,0},0);
  sprite.x+=target_x-adjusted.x;sprite.y+=target_y-adjusted.y;
  std::string group(50,'\0');set(group,0,20004,2);set(group,2,1);set(group,6,1);set(group,10,camera.zoom);set(group,14,1);set(group,18,5);
  set(group,26,static_cast<std::uint32_t>(sprite.x));set(group,30,static_cast<std::uint32_t>(sprite.y));set(group,34,sprite.object_offset_bits[2]);b+=chunk("DGRP",static_cast<std::uint16_t>(10+i),group);
 }
 std::string slot(86,'\0');set(slot,4,10);slot.replace(8,4,"TOLS");set(slot,12,1);set(slot,18,bits(8));b+=chunk("SLOT",128,slot);
 return b;
}
void object_checks(const Temp& temp,const std::filesystem::path& floors,const std::filesystem::path& walls,
                   const odh::WorldProjection& camera,std::size_t wall_pixel){
 const auto path=temp.p/"object.iff";write(path,object_fixture(camera,wall_pixel));
 odh::DefinitionIndex definitions({candidate(path,123)});odh::SavedWorld saved;saved.world=std::make_unique<odh::World>();auto& world=*saved.world;
 std::array<std::int16_t,68> words{};check(world.create_saved_entity(2,odh::EntityKind::object,123,{},{},{},words,{},0));
 check(world.set_position(2,{24,24,1,0}));odh::SavedWorldEntity record{};record.saved.object_id=2;record.saved.guid=123;record.common.dynamic_sprite_flags={1};saved.records.push_back(record);
 auto a=architecture();a.architecture.levels[0].walls[5]={1,0,1,0,1,0,0,0};
 SavedLotRenderer renderer(std::move(a),saved,definitions,floors.string(),walls.string());
 const auto empty=odh::rasterize(camera.width,camera.height,{},{});const odh::VmFrame f{odh::routine_key(4096),0,{2,2,2,0},{},{}};
 check(world.write(f,3,34,1));const auto wall=renderer.render(camera,empty);check(wall.objects==0&&wall.object_hidden==1);
 check(world.write(f,3,34,0));const auto near=renderer.render(camera,empty,true);
 check(near.objects==1&&near.object_layers==1&&near.object_shaded_samples==1&&near.object_cached_frame_bytes==6);
 check(near.image.depth[wall_pixel]<wall.image.depth[wall_pixel]);
 check(near.object_pick_ids.size()==empty.pixels.size()&&near.object_pick_ids[wall_pixel]==2);
 const auto picked=renderer.pick(near,static_cast<std::int32_t>(wall_pixel%camera.width),static_cast<std::int32_t>(wall_pixel/camera.width));
 check(picked&&picked->id==2&&picked->guid==123&&picked->generation==world.find(2)->generation);
 saved.records[0].saved.guid=124;check(!renderer.pick(near,static_cast<std::int32_t>(wall_pixel%camera.width),static_cast<std::int32_t>(wall_pixel/camera.width)));saved.records[0].saved.guid=123;
 check(world.write(f,3,34,1));check(!renderer.pick(near,static_cast<std::int32_t>(wall_pixel%camera.width),static_cast<std::int32_t>(wall_pixel/camera.width)));check(world.write(f,3,34,0));
 check(!renderer.pick(near,-1,0)&&!renderer.pick(near,0,-1)&&!renderer.pick(near,static_cast<std::int32_t>(camera.width),0)&&!renderer.pick(near,0,static_cast<std::int32_t>(camera.height)));
 auto invalid_pick=near;invalid_pick.object_pick_ids.pop_back();rejects([&]{renderer.pick(invalid_pick,0,0);});
 constexpr unsigned alpha=16*255/31;const std::array<std::uint8_t,4> color{210,10,20,255};
 for(unsigned c=0;c<3;++c)check(near.image.pixels[wall_pixel][c]==(color[c]*alpha+wall.image.pixels[wall_pixel][c]*(255-alpha)+127)/255);
 // A nearer translucent object must blend over the wall, not over the floor.
 // Changing Graphic live selects the far DGRP at the same pixel and is hidden.
 check(world.write(f,3,0,1));const auto far=renderer.render(camera,empty,true);check(far.objects==1&&far.object_shaded_samples==0&&far.image.pixels==wall.image.pixels);check(far.object_pick_ids[wall_pixel]==0);
 check(world.write(f,3,0,0));check(world.set_position(2,{24,24,2,0}));check(renderer.render(camera,empty).object_above_level==1);
 check(world.set_position(2,{32,24,1,0}));check(renderer.render(camera,empty).image.pixels!=near.image.pixels);
 check(world.create_saved_entity(3,odh::EntityKind::object,123,{},{},{},words,{},1));check(world.set_position(3,{24,24,1,0}));
 check(world.set_position(2,{24,24,2,0}));check(world.attach(3,0,2));
 const auto contained=renderer.render(camera,empty);check(contained.objects==1&&contained.object_above_level==0);
 check(world.set_position(3,{24,24,2,0}));check(renderer.render(camera,empty).object_above_level==1);check(world.detach(3,0));
 check(world.set_position(2,{24,24,1,0}));check(world.write(f,3,8,1024));SavedLotView down;down.walls_down=true;
 auto cut=architecture();cut.architecture.levels[0].walls[5]={1,0,1,0,1,0,0,0};SavedLotRenderer cutaway(std::move(cut),saved,definitions,floors.string(),walls.string(),down);
 rejects([&]{cutaway.render(camera,empty);});check(world.write(f,3,8,0));
 check(world.remove(2));check(renderer.render(camera,empty).object_removed==1);check(!renderer.pick(near,static_cast<std::int32_t>(wall_pixel%camera.width),static_cast<std::int32_t>(wall_pixel/camera.width)));
 rejects([&]{saved_wall_appearance(saved,definitions,architecture().architecture);});
 check(world.create_saved_entity(2,odh::EntityKind::object,123,{},{},{},words,{},0));check(world.set_position(2,{24,24,1,0}));rejects([&]{renderer.render(camera,empty);});check(!renderer.pick(near,static_cast<std::int32_t>(wall_pixel%camera.width),static_cast<std::int32_t>(wall_pixel/camera.width)));
}
odh::DefinitionCandidate candidate(const std::filesystem::path& path,std::uint32_t guid){return {path.string(),"",1,{138,guid,4,0,0,false,32,10}};}
void contained_checks(const Temp& temp,const std::filesystem::path& floors){
 const auto path=temp.p/"chair.iff";write(path,container_fixture(123));
 odh::DefinitionIndex definitions({candidate(path,123)});
 odh::SavedWorld saved;saved.world=std::make_unique<odh::World>();auto& world=*saved.world;
 check(world.create_saved_entity(1,odh::EntityKind::avatar,42,{},std::vector<std::int16_t>(101),std::vector<std::int16_t>(16),{},{},0));
 check(world.set_position(1,{40,40,2,6}));
 check(world.create(2,odh::EntityKind::object,{0,0,1}));check(world.define_type(2,123));check(world.set_position(2,{24,24,1,2}));
 check(world.attach(2,0,1));check(!world.find(1)->attachment->positioned);
 SavedLotRenderer renderer(architecture(),saved,definitions,floors.string(),{});
 auto seated=renderer.avatar(1);check(seated.container==2&&seated.direction==6&&seated.tile_position==std::array<float,3>{1.5f,2,0});
 check(world.find(1)->position==odh::EntityPosition{40,40,2,6}); // Copied child position is not added or rewritten.
 SavedGraphicsResources fallback(definitions);check(fallback.entity(world,1).tile_position==seated.tile_position);
 check(world.set_position(2,{32,24,1,4}));auto moved=renderer.avatar(1);
 check(moved.tile_position==std::array<float,3>{1.5f,1.5f,0}&&moved.direction==6&&moved.generation==seated.generation);
 auto gradient=architecture();for(std::uint32_t y=0;y<4;++y)for(std::uint32_t x=0;x<4;++x)
  (*gradient.architecture.altitude)[y*4+x][1]=static_cast<std::uint8_t>(20*x);
 SavedLotRenderer sloped(std::move(gradient),saved,definitions,floors.string(),{});
 check(std::fabs(sloped.avatar(1).tile_position[2]-7.5f)<.0001f); // Container terrain, not SLOT-displaced XY.
 // A seat member samples terrain at its saved multitile group center, with
 // integer division in sixteenth-tile units before conversion to float.
 check(world.create(4,odh::EntityKind::object));check(world.set_position(4,{49,24,1,0}));
 odh::SavedWorldEntity first{},second{};first.saved.object_id=2;first.saved.prefix.x=32;first.saved.prefix.y=24;
 second.saved.object_id=4;second.saved.prefix.x=49;second.saved.prefix.y=24;
 saved.records={first,second};saved.multitile_groups={{2,{}, {2,4}}};
 auto slope=[](){auto a=architecture();for(std::uint32_t y=0;y<4;++y)for(std::uint32_t x=0;x<4;++x)
  (*a.architecture.altitude)[y*4+x][1]=static_cast<std::uint8_t>(20*x);return a;};
 SavedLotRenderer grouped(slope(),saved,definitions,floors.string(),{});
 check(std::fabs(grouped.avatar(1).tile_position[2]-9.375f)<.0001f);
 check(grouped.avatar(1).tile_position[0]==1.5f&&world.find(2)->position->x==32&&world.find(4)->position->x==49);
 saved.records[1].saved.prefix.x=32767;rejects([&]{SavedLotRenderer r(slope(),saved,definitions,floors.string(),{});});saved.records[1].saved.prefix.x=49;
 saved.multitile_groups[0].members={2,4,4};rejects([&]{SavedLotRenderer r(slope(),saved,definitions,floors.string(),{});});
 saved.multitile_groups[0].members={2,5};rejects([&]{SavedLotRenderer r(slope(),saved,definitions,floors.string(),{});});
 saved.multitile_groups[0].members.clear();rejects([&]{SavedLotRenderer r(slope(),saved,definitions,floors.string(),{});});
 saved.multitile_groups[0].members.assign(4097,2);rejects([&]{SavedLotRenderer r(slope(),saved,definitions,floors.string(),{});});
 saved.multitile_groups[0].members={2,4};
 SavedGraphicsResources no_tables(definitions,{0,4096});rejects([&]{no_tables.entity(world,1);});
 SavedGraphicsResources no_records(definitions,{16,0});rejects([&]{no_records.entity(world,1);});
 for(unsigned kind=0;kind<4;++kind){const auto bad=temp.p/("bad-chair-"+std::to_string(kind)+".iff");
  write(bad,container_fixture(kind==0?124:123,kind==1?3:0,kind==2,kind==3?0x7fc00000:bits(8)));
  odh::DefinitionIndex index({candidate(bad,123)});SavedGraphicsResources resources(index);
  rejects([&]{resources.entity(world,1);});
 }
 const auto other=temp.p/"other-chair.iff";write(other,container_fixture(124));
 odh::DefinitionIndex pair({candidate(path,123),candidate(other,124)});SavedGraphicsResources limited(pair,{16,1});
 check(limited.entity(world,1).container==2);
 check(world.detach(2,0));check(world.create(3,odh::EntityKind::object,{0,0,1}));check(world.define_type(3,124));check(world.set_position(3,{24,24,1,0}));check(world.attach(3,0,1));
 rejects([&]{limited.entity(world,1);}); // Aggregate record cap, after a successful table.
 check(world.find(1)->attachment->container==3&&world.find(3)->slots[0]==1);
}
}
int main(){try{
 Temp temp;const auto path=temp.p/"floors.iff";{std::ofstream file(path,std::ios::binary);file<<fixture();if(!file)throw std::runtime_error("fixture write failed");}
 odh::SavedWorld saved;saved.world=std::make_unique<odh::World>();auto& world=*saved.world;
 check(world.create(1,odh::EntityKind::avatar));check(world.set_position(1,{24,24,1,3}));
 const odh::DefinitionIndex definitions({});SavedLotView view;view.level=2;view.terrain_theme=odh::TerrainTheme::grass;
 SavedLotRenderer renderer(architecture(),saved,definitions,path.string(),{},view);
 const auto* original=&world;const auto actor=renderer.avatar(1);check(actor.tile_position==std::array<float,3>{1.5f,1.5f,0});check(actor.direction==3);
 odh::WorldProjection camera;camera.width=camera.height=128;camera.center_tile={1.5f,1.5f,.75f};
 auto empty=odh::rasterize(camera.width,camera.height,{},{});auto ground=renderer.render(camera,empty);
 check(ground.floor_draws==1&&ground.floor_triangles==2&&ground.terrain_triangles==6&&ground.floor_texture_bytes==16);
 std::size_t painted=ground.image.depth.size();for(std::size_t i=0;i<ground.image.depth.size();++i)
  if(ground.image.pixels[i]==std::array<std::uint8_t,4>{10,20,30,255}){painted=i;break;}
 check(painted<ground.image.depth.size());
 auto foreground=empty;foreground.depth[painted]=ground.image.depth[painted]-1;foreground.pixels[painted]={255,0,0,255};foreground.tested_samples=foreground.shaded_samples=1;
 auto composed=renderer.render(camera,foreground);check(composed.image.pixels[painted]==foreground.pixels[painted]);
 auto behind=foreground;behind.depth[painted]=ground.image.depth[painted]+1;
 check(renderer.render(camera,behind).image.pixels[painted]==ground.image.pixels[painted]);
 check(ground.image.pixels[painted]==std::array<std::uint8_t,4>{10,20,30,255}&&foreground.depth[painted]<ground.image.depth[painted]);
 check(world.set_position(1,{32,24,2,7}));auto moved=renderer.avatar(1);
 check(moved.tile_position==std::array<float,3>{2,1.5f,2.95f}&&moved.direction==7&&moved.generation==actor.generation);
 check(saved.world.get()==original&&world.find(1)->position==odh::EntityPosition{32,24,2,7});
 auto raised=architecture();for(auto& alt:*raised.architecture.altitude)alt[1]=20;
 SavedLotRenderer slope(std::move(raised),saved,definitions,path.string(),{},view);check(std::fabs(slope.avatar(1).tile_position[2]-6.7f)<.0001f);
 SavedLotView lower;SavedLotRenderer limited(architecture(),saved,definitions,path.string(),{},lower);rejects([&]{limited.avatar(1);});
 rejects([&]{renderer.avatar(2);});check(world.create(2,odh::EntityKind::object));check(world.set_position(2,{24,24,1,0}));rejects([&]{renderer.avatar(2);});
 auto wrong=empty;wrong.depth.pop_back();rejects([&]{renderer.render(camera,wrong);});
 const auto walls=temp.p/"walls.iff";{std::ofstream file(walls,std::ios::binary);file<<wall_fixture();if(!file)throw std::runtime_error("wall fixture write failed");}
 auto with_wall=architecture();with_wall.architecture.levels[0].walls[5]={1,0,1,0,1,0,0,0};
 SavedLotRenderer walled(std::move(with_wall),saved,definitions,path.string(),walls.string(),view);
 const auto wall_frame=walled.render(camera,empty);check(wall_frame.wall_layers==1&&wall_frame.wall_junctions==2&&wall_frame.wall_shaded_samples>0);
 std::size_t wall_pixel=wall_frame.image.depth.size();for(std::size_t i=0;i<wall_frame.image.depth.size();++i)
  if(wall_frame.image.pixels[i]==std::array<std::uint8_t,4>{90,100,110,255}){wall_pixel=i;break;}
 check(wall_pixel<wall_frame.image.depth.size());
 auto live=empty;live.pixels[wall_pixel]={255,0,0,255};live.depth[wall_pixel]=wall_frame.image.depth[wall_pixel]-1;live.tested_samples=live.shaded_samples=1;
 check(walled.render(camera,live).image.pixels[wall_pixel]==live.pixels[wall_pixel]);
 live.depth[wall_pixel]=wall_frame.image.depth[wall_pixel]+1;
 check(walled.render(camera,live).image.pixels[wall_pixel]==wall_frame.image.pixels[wall_pixel]);
 object_checks(temp,path,walls,camera,wall_pixel);
 auto custom=architecture();custom.maps.push_back({"FLRm",{}});custom.maps.back().second.entries.push_back({});
 rejects([&]{SavedLotRenderer r(std::move(custom),saved,definitions,path.string(),{},view);});
 odh::SavedWorld missing;rejects([&]{SavedLotRenderer r(architecture(),missing,definitions,path.string(),{},view);});
 view.level=0;rejects([&]{SavedLotRenderer r(architecture(),saved,definitions,path.string(),{},view);});
 contained_checks(temp,path);
 std::cout<<checks<<" live saved lot checks passed (synthetic only)\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
