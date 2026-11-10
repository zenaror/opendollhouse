#include "saved_lot_renderer.hpp"
#include "saved_wall_appearance.hpp"
#include "odh/ground_owner.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace {
int checks=0;
void check_at(bool ok,int line){++checks;if(!ok)throw std::runtime_error("saved lot assertion #"+std::to_string(checks)+" line "+std::to_string(line));}
#define check(...) check_at((__VA_ARGS__),__LINE__)
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
SavedArchitectureResource pick_architecture(){
 SavedArchitectureResource a;a.architecture.width=a.architecture.height=64;const auto cells=64U*64U;
 for(auto& level:a.architecture.levels){level.floors.resize(cells);level.walls.resize(cells);}
 a.architecture.levels[0].floors[65]=1;a.architecture.levels[0].flags=std::vector<std::uint8_t>(cells);
 a.architecture.altitude=std::vector<std::array<std::uint8_t,4>>(cells,{0,0,0,0});
 a.architecture.grass=std::vector<std::uint8_t>(cells,127);
 a.architecture.pools=std::vector<std::uint8_t>(cells);a.architecture.water=std::vector<std::uint8_t>(cells);
 return a;
}
std::uint32_t bits(float value){std::uint32_t result;std::memcpy(&result,&value,4);return result;}
std::string container_fixture(std::uint32_t guid,std::uint16_t type=0,bool duplicate=false,std::uint32_t x=bits(8)){
 std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');
 std::string objd(42,'\0');set(objd,0,138);set(objd,18,4,2);set(objd,28,guid);set(objd,40,128,2);b+=chunk("OBJD",1,objd);
 std::string slot(86,'\0');set(slot,4,10);slot.replace(8,4,"TOLS");set(slot,12,1);set(slot,16,type,2);set(slot,18,x);
 b+=chunk("SLOT",128,slot);if(duplicate)b+=chunk("SLOT",128,slot);return b;
}
void write(const std::filesystem::path& path,const std::string& data){std::ofstream file(path,std::ios::binary);file<<data;if(!file)throw std::runtime_error("container fixture write failed");}
odh::DefinitionCandidate candidate(const std::filesystem::path&,std::uint32_t);
std::string object_fixture(const odh::WorldProjection& camera,std::size_t pixel,std::uint32_t guid=123){
 std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');
 std::string objd(216,'\0');set(objd,0,138);set(objd,6,10,2);set(objd,8,2,2);set(objd,18,4,2);set(objd,28,guid);set(objd,40,128,2);
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
 check(world.set_position(2,{24,24,1,0})&&world.set_dynamic_sprite_flag(2,0,true));odh::SavedWorldEntity record{};record.saved.object_id=2;record.saved.guid=123;record.common.dynamic_sprite_flags={1};saved.records.push_back(record);
 auto a=architecture();a.architecture.levels[0].walls[5]={1,0,1,0,1,0,0,0};
 SavedLotRenderer renderer(std::move(a),saved,definitions,floors.string(),walls.string());
 const auto empty=odh::rasterize(camera.width,camera.height,{},{});const odh::VmFrame f{odh::routine_key(4096),0,{2,2,2,0},{},{}};
 check(world.write(f,3,34,1));const auto wall=renderer.render(camera,empty);check(wall.objects==0&&wall.object_hidden==1);
 check(world.write(f,3,34,0));const auto near=renderer.render(camera,empty,true);
 check(near.objects==1&&near.object_layers==1&&near.object_shaded_samples==1&&near.object_cached_frame_bytes==6);
 check(world.set_dynamic_sprite_flag(2,0,false));const auto authored_off=renderer.render(camera,empty,true);
 check(authored_off.objects==1&&authored_off.object_layers==0&&saved.records.front().common.dynamic_sprite_flags[0]==1);
 check(world.set_dynamic_sprite_flag(2,0,true)&&renderer.render(camera,empty,true).object_layers==1); // executable World wins over stale original raw flags

 check(near.image.depth[wall_pixel]<wall.image.depth[wall_pixel]);
 check(near.object_pick_ids.size()==empty.pixels.size()&&near.object_pick_ids[wall_pixel]==2);
 // Full-wall cutaway is presentation only; its preflight may not publish a
 // hypothetical Hidden state, even when the source flag changes before draw.
 check(world.write(f,3,8,1024));std::string cutaway_error;
 const auto before_cutaway=world.spatial_revision();
 check(renderer.can_set_walls_down(&cutaway_error)&&cutaway_error.empty()&&!renderer.view().walls_down&&world.spatial_revision()==before_cutaway);
 check(world.write(f,3,8,0)&&renderer.set_walls_down(true));
 check(renderer.render(camera,empty,true).objects==1); // preflight left property false.
 check(renderer.set_walls_down(false)&&world.write(f,3,8,1024));
 const auto source_position=world.find(2)->position;const auto source_generation=world.find(2)->generation;
 const auto before_draw=world.spatial_revision();check(renderer.set_walls_down(true));
 const auto cutaway_frame=renderer.render(camera,empty,true);
 check(cutaway_frame.objects==0&&cutaway_frame.object_hidden==1&&world.find(2)->position==source_position&&world.find(2)->generation==source_generation&&world.spatial_revision()==before_draw);
 check(world.write(f,3,8,0)&&renderer.set_walls_down(false));
 check(renderer.render(camera,empty).object_hidden==1); // Source flagoff retains CutawayHidden.
 check(world.write(f,3,8,1024)&&renderer.render(camera,empty).objects==1); // full walls-up resets it.
 check(renderer.set_walls_down(true)&&renderer.render(camera,empty).object_hidden==1);
 check(world.set_position(2,{24,24,2,0})&&renderer.render(camera,empty).object_above_level==1);
 check(world.write(f,3,8,0)&&world.set_position(2,{24,24,1,0})&&renderer.render(camera,empty).objects==1);
 check(renderer.set_walls_down(false));
 // At the map edge unknown runtime occupancy can change the source result.
 // Refuse the mode before view/World changes instead of assuming occupancy0.
 auto edge_arch=architecture();edge_arch.architecture.levels[0].walls[4]={1,0,1,0,1,0,0,0};
 SavedLotRenderer edge(std::move(edge_arch),saved,definitions,floors.string(),walls.string());
 check(world.write(f,3,8,1024)&&world.set_position(2,{8,24,1,0}));const auto edge_revision=world.spatial_revision();
 check(!edge.can_set_walls_down(&cutaway_error)&&!cutaway_error.empty()&&!edge.set_walls_down(true)&&!edge.view().walls_down&&world.spatial_revision()==edge_revision);
 check(edge.render(camera,empty).objects==1);
 check(world.write(f,3,8,0)&&world.set_position(2,{24,24,1,0}));

 auto under_object=empty;under_object.owner_ids.assign(empty.pixels.size(),0);
 under_object.pixels[wall_pixel]={30,30,30,255};under_object.depth[wall_pixel]=near.image.depth[wall_pixel]+1;
 under_object.owner_ids[wall_pixel]=1;
 const auto object_preempts_avatar=renderer.render(camera,
     std::vector<SavedLotAvatarLayer>{{under_object,41}},true);
 check(object_preempts_avatar.object_pick_ids[wall_pixel]==2&&
       object_preempts_avatar.avatar_pick_ids[wall_pixel]==0);
 const auto picked=renderer.pick(near,static_cast<std::int32_t>(wall_pixel%camera.width),static_cast<std::int32_t>(wall_pixel/camera.width));
 check(picked&&picked->id==2&&picked->guid==123&&picked->generation==world.find(2)->generation&&picked->tree_table_id==128);
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
 const auto full_cut=cutaway.render(camera,empty);check(full_cut.objects==0&&full_cut.object_hidden==1);check(world.write(f,3,8,0));
 check(world.remove(2));check(renderer.render(camera,empty).object_removed==1);check(!renderer.pick(near,static_cast<std::int32_t>(wall_pixel%camera.width),static_cast<std::int32_t>(wall_pixel/camera.width)));
 rejects([&]{saved_wall_appearance(saved,definitions,architecture().architecture);});
 check(world.create_saved_entity(2,odh::EntityKind::object,123,{},{},{},words,{},0));check(world.set_position(2,{24,24,1,0}));rejects([&]{renderer.render(camera,empty);});check(!renderer.pick(near,static_cast<std::int32_t>(wall_pixel%camera.width),static_cast<std::int32_t>(wall_pixel/camera.width)));
}
odh::DefinitionCandidate candidate(const std::filesystem::path& path,std::uint32_t guid){return {path.string(),"",1,{138,guid,4,0,0,false,32,10,128}};}
void event_wall_appearance_checks(const Temp& temp,const std::filesystem::path& floors,
                                  const std::filesystem::path& walls,const odh::WorldProjection& camera){
 const auto file=temp.p/"event-wall-object.iff";write(file,object_fixture(camera,0));
 odh::DefinitionIndex definitions({candidate(file,123)});
 odh::SavedWorld saved;saved.world=std::make_unique<odh::World>();auto& world=*saved.world;
 std::array<std::int16_t,68> words{};
 check(world.create_saved_entity(2,odh::EntityKind::object,123,{},{},{},words,{},0));
 check(world.set_position(2,{24,24,1,0}));odh::SavedWorldEntity raw{};raw.saved.object_id=2;raw.saved.guid=123;saved.records.push_back(raw);
 auto geometry=architecture();geometry.architecture.levels[0].walls[5]={1,0,1,0,1,0,0,0};
 SavedLotRenderer renderer(std::move(geometry),saved,definitions,floors.string(),walls.string());
 check(!renderer.runtime_wall_appearance());
 odh::WallAppearance appearance;appearance.faces.push_back({1,1,1,odh::wall_top_left,{1,0,{}},2,0,world.find(2)->generation,0,false,true});
 appearance.applications.push_back({2,8,0});renderer.set_runtime_wall_appearance(appearance);
 check(renderer.runtime_wall_appearance()&&renderer.runtime_wall_appearance()->faces[0].owner==2);
 // Post-event flags do not rewrite the event-owned historical style.
 check(world.declare_object_word(2,40,-32768)&&world.declare_object_word(2,13,0));
 check(renderer.runtime_wall_appearance()->faces[0].style.raw_style==1);
 const auto empty=odh::rasterize(camera.width,camera.height,{},{});
 check(renderer.render(camera,empty,false,false).wall_layers>0);
 check(world.create(3,odh::EntityKind::avatar,{})&&world.set_position(3,{24,24,1,0}));
 check(world.set_position(3,{40,40,1,0})&&renderer.runtime_wall_appearance().has_value()); // Normal Sim motion keeps wall history.
 auto invalid=appearance;++invalid.faces[0].generation;
 rejects([&]{renderer.set_runtime_wall_appearance(invalid);});check(renderer.runtime_wall_appearance().has_value()); // Invalid refresh is atomic.
 check(world.set_position(2,{40,24,1,0}));check(!renderer.runtime_wall_appearance());
 appearance.faces[0].x=2;renderer.set_runtime_wall_appearance(appearance);check(renderer.runtime_wall_appearance().has_value());
 renderer.invalidate_runtime_wall_appearance();check(!renderer.runtime_wall_appearance());
 renderer.set_runtime_wall_appearance(appearance);check(world.remove(2));check(!renderer.runtime_wall_appearance());
 // A source style0 clear remains explicit after its former owner is gone;
 // event application history does not require resurrecting that numeric ID.
 auto cleared=appearance;cleared.faces[0].owner=0;cleared.faces[0].generation=0;cleared.faces[0].style={0,0,{}};
 renderer.set_runtime_wall_appearance(cleared);
 check(renderer.runtime_wall_appearance()&&odh::is_canonical_cleared_wall_face(renderer.runtime_wall_appearance()->faces.front()));
 renderer.set_runtime_wall_appearance(odh::WallAppearance{});check(renderer.runtime_wall_appearance().has_value());
 check(world.create_saved_entity(4,odh::EntityKind::object,123,{},{},{},words,{},0)&&world.declare_object_word(4,40,0x4000));
 check(renderer.runtime_wall_appearance().has_value()); // OOW has no wall contribution to invalidate.
 check(world.set_position(4,{24,40,1,0}));
 check(!renderer.runtime_wall_appearance()); // A new FREE positioned architectural owner needs its actual event.
 renderer.set_runtime_wall_appearance({});check(!renderer.runtime_wall_appearance());
}
void runtime_object_checks(const Temp& temp,const std::filesystem::path& floors,
                           const odh::WorldProjection& camera,std::size_t pixel){
 const auto original=temp.p/"runtime-original.iff",created=temp.p/"runtime-created.iff";
 write(original,object_fixture(camera,pixel));write(created,object_fixture(camera,pixel,124));
 odh::DefinitionIndex definitions({candidate(original,123),candidate(created,124)});
 odh::SavedWorld saved;saved.world=std::make_unique<odh::World>();auto& world=*saved.world;
 std::array<std::int16_t,68> words{};
 const auto make=[&](std::int16_t id,std::uint32_t guid){
  check(world.create_saved_entity(id,odh::EntityKind::object,guid,{},{},{},words,{},0));
  check(world.set_position(id,{24,24,1,0}));check(world.set_dynamic_sprite_flag(id,0,true));
 };
 make(2,123);odh::SavedWorldEntity raw{};raw.saved.object_id=2;raw.saved.guid=123;
 raw.imported_generation=world.find(2)->generation;
 raw.common.dynamic_sprite_flags={1};saved.records.push_back(raw);
 SavedLotRenderer renderer(architecture(),saved,definitions,floors.string(),{});
 const auto empty=odh::rasterize(camera.width,camera.height,{},{});
 const auto x=static_cast<std::int32_t>(pixel%camera.width),y=static_cast<std::int32_t>(pixel/camera.width);
 const auto old=renderer.render(camera,empty,true);check(old.objects==1&&renderer.pick(old,x,y).has_value());
 make(4,124);check(world.write({odh::routine_key(4096),0,{2,2,2,0},{},{}},3,34,1));
 check(renderer.render(camera,empty,true).objects==0); // World alone grants no artwork authority.
 SavedLotRuntimeObject binding{4,world.find(4)->generation,124,created.string(),"",1};
 renderer.set_runtime_objects({binding});const auto live=renderer.render(camera,empty,true);
 check(live.objects==1&&live.object_pick_ids[pixel]==4&&renderer.pick(live,x,y)->guid==124);
 auto invalid=binding;invalid.asset="wrong.iff";
 rejects([&]{renderer.set_runtime_objects({invalid});});
 rejects([&]{renderer.set_runtime_objects({binding,binding});});
 invalid=binding;++invalid.generation;rejects([&]{renderer.set_runtime_objects({invalid});});
 invalid=binding;invalid.definition_chunk_id=2;rejects([&]{renderer.set_runtime_objects({invalid});});
 check(renderer.render(camera,empty,true).object_pick_ids[pixel]==4); // Invalid refresh is atomic.
 check(world.set_dynamic_sprite_flag(4,0,false));check(renderer.render(camera,empty,true).object_layers==0);
 check(world.set_dynamic_sprite_flag(4,0,true));check(world.set_position(4,{32,24,1,0}));
 check(renderer.render(camera,empty,true).image.pixels!=live.image.pixels);
 check(world.set_position(4,{24,24,1,0}));
 // Runtime container/SLOT placement uses this same World, with no renderer clone.
 check(world.create_saved_entity(3,odh::EntityKind::object,123,{},{},{},words,{},1));
 check(world.set_position(3,{24,24,1,0}));check(world.attach(3,0,4));
 check(renderer.entity_graphics(4).container==3);
 check(world.set_position(3,{24,24,2,0}));check(renderer.render(camera,empty,true).object_above_level==1);
 check(world.detach(3,0));check(world.set_position(4,{24,24,1,0}));
 // Avatar slots use the exported animated anchor, never an object-SLOT or
 // guessed center; the visual includes terrain already and is not raised twice.
 check(world.create(5,odh::EntityKind::avatar,{0,0,1,0}));
 check(world.set_position(5,{24,24,1,0}));check(world.attach(5,0,4));
 rejects([&]{(void)renderer.render(camera,empty,true);});
 SavedLotAvatarSlotAnchor hand{5,world.find(5)->generation,*world.find(5)->position,0,{1,1,1.25f}};
 renderer.set_avatar_slot_anchors({hand});const auto held=renderer.entity_graphics(4);
 check(held.container==5&&held.tile_position==hand.tile_position);
 check(renderer.render(camera,empty,true).objects==1);
 auto stale=hand;++stale.generation;rejects([&]{renderer.set_avatar_slot_anchors({stale});});
 check(renderer.entity_graphics(4).tile_position==hand.tile_position); // Invalid refresh is atomic.
 check(world.set_position(5,{40,24,1,0}));rejects([&]{(void)renderer.entity_graphics(4);});
 hand.logical_position=*world.find(5)->position;hand.tile_position[0]+=1;
 renderer.set_avatar_slot_anchors({hand});check(renderer.entity_graphics(4).tile_position==hand.tile_position);
 check(world.detach(5,0));renderer.set_avatar_slot_anchors({});check(world.set_position(4,{24,24,1,0}));
 check(world.remove(4));check(renderer.render(camera,empty,true).object_removed==1);
 check(!renderer.pick(live,x,y));renderer.set_runtime_objects({});
 // An authenticated creation may reuse an original ID with a different GUID.
 check(world.remove(2));make(2,124);binding.id=2;binding.generation=world.find(2)->generation;
 renderer.set_runtime_objects({binding});const auto reused=renderer.render(camera,empty,true);
 check(reused.objects==1&&reused.object_pick_ids[pixel]==2&&renderer.pick(reused,x,y)->guid==124);
 check(!renderer.pick(old,x,y)); // Old pixels must not select the reused identity.
 // Full World restore + retained old source records + GEXP-derived rebinding.
 odh::SavedWorld reopened;reopened.records=saved.records;reopened.world=std::make_unique<odh::World>();
 check(reopened.world->restore_snapshot(world.snapshot()));
 SavedLotRenderer restored(architecture(),reopened,definitions,floors.string(),{});
 restored.set_runtime_objects({binding});const auto again=restored.render(camera,empty,true);
 check(again.objects==1&&again.image.pixels==reused.image.pixels&&restored.pick(again,x,y)->guid==124);
 check(!restored.pick(old,x,y));
 check(world.remove(2));make(2,124);check(!renderer.pick(reused,x,y));
 rejects([&]{renderer.render(camera,empty,true);});
 binding.generation=world.find(2)->generation;renderer.set_runtime_objects({binding});
 check(renderer.render(camera,empty,true).objects==1&& !renderer.pick(reused,x,y));
 // The same number may later belong to a person. Its sprite is supplied
 // separately by the avatar pass; the extinct object's art/picks stay gone.
 check(world.remove(2));check(world.create(2,odh::EntityKind::avatar,{}));
 renderer.set_runtime_objects({});
 const auto visiting=renderer.render(camera,empty,true);
 check(visiting.objects==0&&visiting.object_removed==1&&!renderer.pick(old,x,y));
 check(saved_wall_appearance(saved,definitions,renderer.render_architecture(),true).inputs.empty());
 rejects([&]{(void)saved_wall_appearance(saved,definitions,renderer.render_architecture());});
 // A reopened renderer must not acquire a same-GUID new object's generation
 // simply because it exists; rendering requires its creation resource.
 check(world.remove(2));make(2,123);
 check(world.declare_object_word(2,40,0x4000));
 SavedLotRenderer reopened_same(architecture(),saved,definitions,floors.string(),{});
 rejects([&]{(void)reopened_same.render(camera,empty,true);});
 check(saved_wall_appearance(saved,definitions,reopened_same.render_architecture(),true).inputs.empty());
 reopened_same.set_runtime_objects({{2,world.find(2)->generation,123,original.string(),"",1}});
 check(reopened_same.render(camera,empty,true).object_pick_ids[pixel]==2&&!reopened_same.pick(old,x,y));
}
void footprint_part_checks(const Temp& temp,const std::filesystem::path& floors,
                           const odh::WorldProjection& camera,std::size_t pixel){
 const auto visible_path=temp.p/"footprint-visible.iff",empty_path=temp.p/"footprint-empty.iff";
 auto visible_bytes=object_fixture(camera,pixel,123),empty_bytes=object_fixture(camera,pixel,124);
 set(visible_bytes,64+76+20,1,2);set(visible_bytes,64+76+22,1,2);
 set(empty_bytes,64+76+20,1,2);set(empty_bytes,64+76+22,0,2);
 set(empty_bytes,64+76+6,0,2);set(empty_bytes,64+76+8,8,2);
 write(visible_path,visible_bytes);write(empty_path,empty_bytes);
 auto visible=candidate(visible_path,123),invisible=candidate(empty_path,124);
 visible.definition.master_id=invisible.definition.master_id=1;
 visible.definition.sub_index=1;invisible.definition.sub_index=0;
 visible.definition.is_multitile=invisible.definition.is_multitile=true;
 odh::DefinitionIndex definitions({visible,invisible});odh::SavedWorld saved;saved.world=std::make_unique<odh::World>();
 auto& world=*saved.world;
 for(const auto pair:{std::pair<std::int16_t,std::uint32_t>{2,123},{3,124}}){
  check(world.create_saved_entity(pair.first,odh::EntityKind::object,pair.second,{},{},{},{},{},0));
  check(world.set_position(pair.first,{24,24,1,0})&&world.set_dynamic_sprite_flag(pair.first,0,true));
  odh::SavedWorldEntity record{};record.saved.object_id=static_cast<std::uint16_t>(pair.first);
  record.saved.guid=pair.second;record.common.dynamic_sprite_flags={1};saved.records.push_back(record);
 }
 SavedLotRenderer renderer(architecture(),saved,definitions,floors.string(),{});
 const auto empty=odh::rasterize(camera.width,camera.height,{},{});
 const auto before=odh::encode_world_snapshot(world.snapshot());
 const auto frame=renderer.render(camera,empty,true);
 check(frame.objects==2&&frame.object_layers==1&&frame.object_pick_ids[pixel]==2&&
       std::find(frame.object_pick_ids.begin(),frame.object_pick_ids.end(),3)==frame.object_pick_ids.end());
 check(world.find(2)&&world.find(3)&&world.find(3)->position&&
       odh::encode_world_snapshot(world.snapshot())==before);
 // The visible part still fails strictly if its selected drawing is missing;
 // neither membership in the group nor its invisible sibling supplies a frame.
 check(world.write({odh::routine_key(4096),0,{2,2,2},{},{}},3,0,2));
 const auto invalid_before=odh::encode_world_snapshot(world.snapshot());
 rejects([&]{renderer.render(camera,empty,true);});
 check(odh::encode_world_snapshot(world.snapshot())==invalid_before);
}
void runtime_group_checks(const Temp& temp,const std::filesystem::path& floors,
                          const odh::WorldProjection& camera,std::size_t pixel){
 const auto file=temp.p/"created-group.iff";
 auto bytes=object_fixture(camera,pixel,123);
 set(bytes,64+76+20,1,2);set(bytes,64+76+22,1,2);
 auto visible=candidate(file,123),footprint=candidate(file,124),master=candidate(file,125);
 for(auto* item:{&visible,&footprint,&master}){item->definition.is_multitile=true;item->definition.master_id=1;}
 visible.definition.sub_index=1;footprint.definition.sub_index=0;master.definition.sub_index=-1;
 footprint.chunk_id=2;master.chunk_id=3;
 for(const auto& item:{footprint,master}){
  std::string objd(216,'\0');set(objd,0,138);set(objd,18,4,2);set(objd,20,1,2);
  set(objd,22,static_cast<std::uint16_t>(item.definition.sub_index),2);set(objd,28,item.definition.guid);
  bytes+=chunk("OBJD",item.chunk_id,objd);
 }
 write(file,bytes);
 odh::DefinitionIndex definitions({visible,footprint,master});odh::SavedWorld saved;
 saved.world=std::make_unique<odh::World>();auto& world=*saved.world;
 for(const auto [id,guid]:{std::pair<std::int16_t,std::uint32_t>{2,123},{3,124}}){
  check(world.create_saved_entity(id,odh::EntityKind::object,guid,{},{},{},{},{},0,125));
  check(world.set_position(id,{id==2?24:41,24,1,0}));check(world.set_dynamic_sprite_flag(id,0,true));
 }
 odh::ObjectGroup group{125,true,2,{{2,world.find(2)->generation,0,0,0},{3,world.find(3)->generation,17,0,0}}};
 std::vector<SavedLotRuntimeObject> bindings{{2,group.members[0].generation,123,file.string(),"",1},
                                           {3,group.members[1].generation,124,file.string(),"",2}};
 auto gradient=architecture();
 for(std::uint32_t y=0;y<4;++y)for(std::uint32_t x=0;x<4;++x)
  (*gradient.architecture.altitude)[y*4+x][1]=static_cast<std::uint8_t>(20*x);
 SavedLotRenderer renderer(gradient,saved,definitions,floors.string(),{});
 const auto z=[&](float units){return odh::terrain_interp_altitude(gradient.architecture,units/16,1.5f);};
 rejects([&]{renderer.set_runtime_objects(bindings);});
 rejects([&]{renderer.set_runtime_objects({bindings.front()},{group});});
 auto stale=group;++stale.members[1].generation;
 rejects([&]{renderer.set_runtime_objects(bindings,{stale});});
 renderer.set_runtime_objects(bindings,{group});
 check(std::fabs(renderer.entity_graphics(2).tile_position[2]-z(32.5f))<.0001f);
 check(std::fabs(renderer.entity_graphics(3).tile_position[2]-z(32.5f))<.0001f);
 const auto empty=odh::rasterize(camera.width,camera.height,{},{});
 const auto frame=renderer.render(camera,empty,true);
 check(frame.objects==2&&frame.object_layers+frame.object_offscreen_layers==1); // One drawable layer, even if outside this camera.
 renderer.restore_runtime_group_placements({group});renderer.set_runtime_objects(bindings,{group});
 check(std::fabs(renderer.entity_graphics(2).tile_position[2]-z(32))<.0001f);
 check(world.place_out_of_world(3));renderer.set_runtime_objects(bindings,{group});
 check(std::fabs(renderer.entity_graphics(2).tile_position[2]-z(32.5f))<.0001f);
 renderer.restore_runtime_group_placements({group});renderer.set_runtime_objects(bindings,{group});
 check(std::fabs(renderer.entity_graphics(2).tile_position[2]-z(-16372))<.0001f);
 check(world.set_position(3,{41,24,1,0}));renderer.set_runtime_objects(bindings,{group});
 check(std::fabs(renderer.entity_graphics(2).tile_position[2]-z(32.5f))<.0001f);
 check(world.set_ui_ghost(3,true));rejects([&]{renderer.set_runtime_objects(bindings,{group});});
}
// One matrix exercises the typed consumer independently of any particular
// dynamic object script: membership, provenance, visuals and rejected refreshes.
void live_association_checks(const Temp& temp,const std::filesystem::path& floors,
                             const odh::WorldProjection& camera,std::size_t pixel){
 const auto root=temp.p/"live-game",data=root/"GameData";
 std::filesystem::create_directories(data);
 std::vector<odh::DefinitionCandidate> entries;
 for(int n=0;n<3;++n){
  const auto file=data/("part"+std::to_string(n)+".iff");
  write(file,object_fixture(camera,pixel,200+n));entries.push_back(candidate(file,200+n));
 }
 odh::DefinitionIndex definitions(entries);
 for(const auto count:{2,3})for(const auto mixed:{false,true})for(const auto multitile:{false,true}){
  odh::SavedWorld saved;saved.world=std::make_unique<odh::World>();auto& world=*saved.world;
  std::vector<SavedLotRuntimeObject> bindings;
  odh::ObjectGroup group;group.kind=odh::ObjectGroupKind::live_association;
  group.multitile=multitile;group.base_id=static_cast<std::int16_t>(count+1); // Base is last.
  for(int n=0;n<count;++n){
   const auto id=static_cast<std::int16_t>(n+2);const auto guid=static_cast<std::uint32_t>(200+n);
   check(world.create_saved_entity(id,odh::EntityKind::object,guid,{},{},{},{},{},0));
   check(world.set_position(id,{24+17*n,24,1,0}));check(world.set_dynamic_sprite_flag(id,0,true));
   const auto generation=world.find(id)->generation;
   odh::ObjectGroupMemberOrigin origin;
   origin.source_kind=mixed&&n==0?odh::ObjectGroupMemberSource::original_import:odh::ObjectGroupMemberSource::native_created;
   origin.guid=guid;origin.source="GameData/part"+std::to_string(n)+".iff";origin.definition_chunk_id=1;
   group.members.push_back({id,generation,static_cast<std::int16_t>(17*n),0,0,origin});
   if(origin.source_kind==odh::ObjectGroupMemberSource::original_import){
    odh::SavedWorldEntity row{};row.saved.object_id=id;row.saved.guid=guid;
    row.definition=entries[n].definition;
    row.imported_generation=generation;row.common.dynamic_sprite_flags={1};saved.records.push_back(row);
    // Old imported placement must not override the live association's mode.
    saved.multitile_groups.push_back({id,{}, {id}});
   }else bindings.push_back({id,generation,guid,entries[n].source,"",1});
  }
  auto gradient=architecture();
  for(std::uint32_t y=0;y<4;++y)for(std::uint32_t x=0;x<4;++x)
   (*gradient.architecture.altitude)[y*4+x][1]=static_cast<std::uint8_t>(20*x);
  SavedLotRenderer renderer(gradient,saved,definitions,floors.string(),{});
  renderer.set_runtime_objects(bindings,{group},root);
  const auto empty=odh::rasterize(camera.width,camera.height,{},{});
  const auto frame=renderer.render(camera,empty,true);check(frame.objects==static_cast<unsigned>(count));
  const auto altitude=[&](float x){return odh::terrain_interp_altitude(gradient.architecture,x/16,1.5f);};
  for(int n=0;n<count;++n)
   check(std::fabs(renderer.entity_graphics(n+2).tile_position[2]-altitude(multitile?24+17*(count-1)/2.0f:24+17*n))<.0001f);
  std::vector<std::pair<std::int16_t,std::uint64_t>> hidden;
  for(const auto& member:group.members)hidden.emplace_back(member.id,member.generation);
  renderer.set_preview_hidden_objects(hidden);const auto preview=renderer.render(camera,empty,true);
  check(preview.objects==0&&preview.object_hidden==static_cast<unsigned>(count));
  renderer.set_preview_hidden_objects({});
  const auto reject_group=[&](const odh::ObjectGroup& invalid){
   rejects([&]{renderer.set_runtime_objects(bindings,{invalid},root);});
   check(renderer.render(camera,empty,true).image.pixels==frame.image.pixels);
  };
  auto invalid=group;++invalid.members.back().generation;reject_group(invalid);
  invalid=group;invalid.base_id=30;reject_group(invalid);
  invalid=group;invalid.definition_guid=200;reject_group(invalid);
  invalid=group;invalid.members.back().origin->asset="forged.iff";reject_group(invalid);
  invalid=group;invalid.members.back().origin->source="Other/part"+std::to_string(count-1)+".iff";reject_group(invalid);
  invalid=group;invalid.members.back().origin->source="GameData/../GameData/part"+std::to_string(count-1)+".iff";reject_group(invalid);
  invalid=group;++invalid.members.back().origin->definition_chunk_id;reject_group(invalid);
  invalid=group;invalid.members.back().origin->source_kind=odh::ObjectGroupMemberSource::original_import;reject_group(invalid);
  if(mixed){
   const auto generation=saved.records.front().imported_generation;++saved.records.front().imported_generation;
   rejects([&]{renderer.set_runtime_objects(bindings,{group},root);});
   saved.records.front().imported_generation=generation;
   check(renderer.render(camera,empty,true).image.pixels==frame.image.pixels);
   const auto guid=saved.records.front().saved.guid;++saved.records.front().saved.guid;
   rejects([&]{renderer.set_runtime_objects(bindings,{group},root);});
   saved.records.front().saved.guid=guid;
   check(renderer.render(camera,empty,true).image.pixels==frame.image.pixels);
  }
  renderer.restore_runtime_group_placements({group});renderer.set_runtime_objects(bindings,{group},root);
  for(int n=0;n<count;++n)
   check(std::fabs(renderer.entity_graphics(n+2).tile_position[2]-altitude(multitile?float((24*count+17*count*(count-1)/2)/count):24+17*n))<.0001f);
 }
}
void graphics_slot_cache_capacity_checks(const Temp& temp){
 std::vector<odh::DefinitionCandidate> candidates;
 odh::World world;
 check(world.create_saved_entity(1,odh::EntityKind::avatar,42,{},std::vector<std::int16_t>(101),std::vector<std::int16_t>(16),{},{},0));
 check(world.set_position(1,{40,40,2,6}));
 for(std::uint32_t i=0;i<18;++i){
  const auto path=temp.p/("slot-cache-"+std::to_string(i)+".iff");const auto guid=1000+i;
  write(path,container_fixture(guid));candidates.push_back(candidate(path,guid));
  const auto id=static_cast<std::int16_t>(i+2);
  check(world.create(id,odh::EntityKind::object,{0,0,1})&&world.define_type(id,guid)&&world.set_position(id,{24,24,1,2}));
 }
 odh::DefinitionIndex definitions(candidates);
 SavedGraphicsResources configured(definitions,{17,4096}),defaults(definitions);
 for(std::int16_t id=2;id<20;++id){
  check(world.attach(id,0,1));
  check(defaults.entity(world,1).container==id);
  if(id<19)check(configured.entity(world,1).container==id);
  else rejects([&]{configured.entity(world,1);});
  check(world.detach(id,0));
 }
 // A refusal for a new table must preserve all earlier cached tables.
 check(world.attach(2,0,1)&&configured.entity(world,1).container==2&&world.detach(2,0));
}
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
 check(renderer.avatar_visual_level(seated)==1);
 check(world.detach(2,0)&&world.set_position(1,{40,40,1,6})&&world.set_position(2,{24,24,2,2})&&world.attach(2,0,1));
 check(renderer.avatar_visual_level(renderer.entity_graphics(1))==2);rejects([&]{renderer.avatar(1);});
 check(world.detach(2,0)&&world.set_position(1,{40,40,2,6})&&world.set_position(2,{24,24,1,2})&&world.attach(2,0,1));
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
 first.saved.guid=123;first.imported_generation=world.find(2)->generation;
 second.imported_generation=world.find(4)->generation;
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
 // New objects cannot borrow an old slope center before artwork refresh.
 check(world.remove(4));check(world.create(4,odh::EntityKind::object)&&world.define_type(4,123));
 check(world.set_position(4,{16,24,1,0}));
 SavedLotRenderer reused_part(slope(),saved,definitions,floors.string(),{});
 check(std::fabs(reused_part.entity_graphics(4).tile_position[2]-
            odh::terrain_interp_altitude(reused_part.render_architecture(),1,1.5f))<.0001f);
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
 SavedLotMaterialOptions material_options;
 SavedLotRenderer pick_renderer(pick_architecture(),saved,definitions,path.string(),{},view,material_options);
 {
  const auto baseline=pick_renderer.render(camera,std::vector<SavedLotAvatarLayer>{},true,false);
  odh_host::RoomLightingView colors;colors.room_colors.assign(pick_renderer.runtime_room_map()->rooms.size(),{0.25f,0.5f,1});
  const auto authority=[&](std::int16_t id,std::uint64_t generation)->std::optional<std::uint16_t>{
   const auto* e=world.find(id);if(!e||e->generation!=generation||!e->position)return {};
   return pick_renderer.runtime_room_at(*e->position);
  };
  rejects([&]{pick_renderer.set_room_lighting(colors);});
  pick_renderer.set_room_lighting(colors,authority);
  const auto lit=pick_renderer.render(camera,std::vector<SavedLotAvatarLayer>{},true,false);
  check(lit.image.pixels!=baseline.image.pixels&&lit.image.depth==baseline.image.depth&&lit.ground_pick_ids==baseline.ground_pick_ids);
  auto avatar=empty;avatar.pixels[0]={200,160,120,255};avatar.depth[0]=-10000;
  avatar.owner_ids.assign(avatar.pixels.size(),0);avatar.owner_ids[0]=1;
  const auto lit_avatar=pick_renderer.render(camera,std::vector<SavedLotAvatarLayer>{{avatar,37,1,world.find(1)->generation}},true,false);
  check(lit_avatar.image.pixels[0]==std::array<std::uint8_t,4>{107,117,120,255}&&lit_avatar.avatar_pick_ids[0]==37);
  rejects([&]{pick_renderer.render(camera,std::vector<SavedLotAvatarLayer>{{avatar,37,1,world.find(1)->generation+1}},true,false);});
  pick_renderer.set_room_lighting({});
  const auto legacy=pick_renderer.render(camera,std::vector<SavedLotAvatarLayer>{},true,false);
  check(legacy.image.pixels==baseline.image.pixels&&legacy.image.depth==baseline.image.depth&&legacy.ground_pick_ids==baseline.ground_pick_ids);
 }
 {
  auto edited=*pick_renderer.prepared_architecture();edited.levels[0].floors[65]=0;
  auto restored_materials=material_options;restored_materials.restored_architecture=odh::bind_architecture_snapshot(edited,{});
  const auto world_before=saved.world->snapshot();
  SavedLotRenderer edited_renderer(pick_architecture(),saved,definitions,path.string(),{},view,restored_materials);
  check(edited_renderer.prepared_architecture()->levels[0].floors[65]==0&&
    pick_renderer.prepared_architecture()->levels[0].floors[65]==1);
  check(edited_renderer.runtime_architecture_resource().architecture.levels[0].floors[65]==0&&
    edited_renderer.architecture_resource().architecture.levels[0].floors[65]==1);
  check(edited_renderer.render(camera,empty,true,false).floor_draws==0);
  check(saved.world->snapshot().entities.size()==world_before.entities.size());
 }
 const auto pick_frame=pick_renderer.render(camera,empty,true);
 check(pick_frame.image.owner_ids.empty()&&pick_frame.ground_pick_ids.size()==empty.pixels.size());
 check(pick_frame.ground_pick_ids[painted]==odh::ground_owner_id(1,1,1));
 const auto floor_pick=pick_renderer.pick_ground(pick_frame,static_cast<std::int32_t>(painted%camera.width),
                                                static_cast<std::int32_t>(painted/camera.width));
 check(floor_pick&&floor_pick->x==1&&floor_pick->y==1&&floor_pick->level==1);
 check(floor_pick->room==*pick_renderer.runtime_room_at({24,24,1,0}));
 auto make_avatar_layer=[&](std::uint16_t owner,std::array<std::uint8_t,4> color,float depth){
  auto layer=empty;layer.owner_ids.assign(layer.pixels.size(),0);layer.pixels[painted]=color;
  layer.depth[painted]=depth;layer.owner_ids[painted]=owner;return layer;
 };
 auto first_resident=make_avatar_layer(1,{200,20,20,255},pick_frame.image.depth[painted]-2);
 auto second_resident=make_avatar_layer(1,{20,20,200,255},pick_frame.image.depth[painted]-1);
 const std::vector<SavedLotAvatarLayer> residents{{second_resident,23},{first_resident,17}};
 const auto resident_frame=pick_renderer.render(camera,residents,true,false);
 check(resident_frame.image.pixels[painted]==first_resident.pixels[painted]&&
       resident_frame.image.depth[painted]==first_resident.depth[painted]);
 check(resident_frame.avatar_pick_ids.size()==empty.pixels.size()&&resident_frame.avatar_pick_ids[painted]==17);
 check(resident_frame.ground_pick_ids[painted]==0&&resident_frame.image.owner_ids.empty());
 const auto legacy_avatar=make_avatar_layer(9,{20,200,20,255},pick_frame.image.depth[painted]-1);
 const auto legacy_avatar_frame=pick_renderer.render(camera,legacy_avatar,true,false);
 check(legacy_avatar_frame.ground_pick_ids[painted]==9&&legacy_avatar_frame.avatar_pick_ids[painted]==0);
 auto too_many_residents=std::vector<SavedLotAvatarLayer>(65);
 rejects([&]{pick_renderer.render(camera,too_many_residents,true,false);});
 std::size_t terrain_pixel=pick_frame.ground_pick_ids.size();
 for(std::size_t i=0;i<pick_frame.ground_pick_ids.size();++i)
  if(pick_frame.ground_pick_ids[i]==odh::ground_owner_id(2,1,1)){terrain_pixel=i;break;}
 check(terrain_pixel<pick_frame.ground_pick_ids.size());
 const auto terrain_pick=pick_renderer.pick_ground(pick_frame,static_cast<std::int32_t>(terrain_pixel%camera.width),
                                                   static_cast<std::int32_t>(terrain_pixel/camera.width));
 check(terrain_pick&&terrain_pick->x==2&&terrain_pick->y==1&&terrain_pick->level==1);
 check(!pick_renderer.pick_ground(pick_frame,-1,0));
 auto malformed_ground_pick=pick_frame;malformed_ground_pick.ground_pick_ids.pop_back();
 rejects([&]{pick_renderer.pick_ground(malformed_ground_pick,0,0);});
 auto foreground=empty;foreground.depth[painted]=ground.image.depth[painted]-1;foreground.pixels[painted]={255,0,0,255};foreground.tested_samples=foreground.shaded_samples=1;
 auto composed=renderer.render(camera,foreground);check(composed.image.pixels[painted]==foreground.pixels[painted]);
 auto pick_foreground=foreground;const auto occluded=pick_renderer.render(camera,pick_foreground,true);
 check(occluded.ground_pick_ids[painted]==0&&!pick_renderer.pick_ground(occluded,
       static_cast<std::int32_t>(painted%camera.width),static_cast<std::int32_t>(painted/camera.width)));
 auto behind=foreground;behind.depth[painted]=ground.image.depth[painted]+1;
 check(renderer.render(camera,behind).image.pixels[painted]==ground.image.pixels[painted]);
 check(ground.image.pixels[painted]==std::array<std::uint8_t,4>{10,20,30,255}&&foreground.depth[painted]<ground.image.depth[painted]);
 check(world.set_position(1,{32,24,2,7}));auto moved=renderer.avatar(1);
 check(moved.tile_position==std::array<float,3>{2,1.5f,2.95f}&&moved.direction==7&&moved.generation==actor.generation);
 check(saved.world.get()==original&&world.find(1)->position==odh::EntityPosition{32,24,2,7});
 auto raised=architecture();for(auto& alt:*raised.architecture.altitude)alt[1]=20;
 SavedLotRenderer slope(std::move(raised),saved,definitions,path.string(),{},view);check(std::fabs(slope.avatar(1).tile_position[2]-6.7f)<.0001f);
 SavedLotView lower;SavedLotRenderer limited(architecture(),saved,definitions,path.string(),{},lower);rejects([&]{limited.avatar(1);});
 check(limited.entity_graphics(1).tile_position==moved.tile_position);
 limited.set_view_level(static_cast<std::uint8_t>(world.find(1)->position->level));
 check(limited.avatar(1).tile_position==moved.tile_position&&limited.view().level==2);
 limited.set_view_level(1);rejects([&]{limited.avatar(1);});
 rejects([&]{renderer.avatar(2);});check(world.create(2,odh::EntityKind::object));check(world.set_position(2,{24,24,1,0}));rejects([&]{renderer.avatar(2);});
 auto wrong=empty;wrong.depth.pop_back();rejects([&]{renderer.render(camera,wrong);});
 const auto walls=temp.p/"walls.iff";{std::ofstream file(walls,std::ios::binary);file<<wall_fixture();if(!file)throw std::runtime_error("wall fixture write failed");}
 auto with_wall=architecture();with_wall.architecture.levels[0].walls[5]={1,0,1,0,1,0,0,0};
 SavedLotRenderer walled(std::move(with_wall),saved,definitions,path.string(),walls.string(),view);
 const auto wall_frame=walled.render(camera,empty);check(wall_frame.wall_layers==1&&wall_frame.wall_junctions==2&&wall_frame.wall_shaded_samples>0);
 std::size_t wall_pixel=wall_frame.image.depth.size();for(std::size_t i=0;i<wall_frame.image.depth.size();++i)
  if(wall_frame.image.pixels[i]==std::array<std::uint8_t,4>{90,100,110,255}){wall_pixel=i;break;}
 check(wall_pixel<wall_frame.image.depth.size());
 auto behind_wall=empty;behind_wall.owner_ids.assign(empty.pixels.size(),0);
 behind_wall.pixels[wall_pixel]={20,200,20,255};
 behind_wall.depth[wall_pixel]=wall_frame.image.depth[wall_pixel]+1;
 behind_wall.owner_ids[wall_pixel]=1;
 const auto wall_preempts_avatar=walled.render(camera,
     std::vector<SavedLotAvatarLayer>{{behind_wall,37}},true,false);
 check(wall_preempts_avatar.image.pixels[wall_pixel]==wall_frame.image.pixels[wall_pixel]&&
       wall_preempts_avatar.avatar_pick_ids[wall_pixel]==0);
 auto live=empty;live.pixels[wall_pixel]={255,0,0,255};live.depth[wall_pixel]=wall_frame.image.depth[wall_pixel]-1;live.tested_samples=live.shaded_samples=1;
 check(walled.render(camera,live).image.pixels[wall_pixel]==live.pixels[wall_pixel]);
 live.depth[wall_pixel]=wall_frame.image.depth[wall_pixel]+1;
 check(walled.render(camera,live).image.pixels[wall_pixel]==wall_frame.image.pixels[wall_pixel]);
 object_checks(temp,path,walls,camera,wall_pixel);
 runtime_object_checks(temp,path,camera,wall_pixel);
 footprint_part_checks(temp,path,camera,wall_pixel);
 runtime_group_checks(temp,path,camera,wall_pixel);
 live_association_checks(temp,path,camera,wall_pixel);
 event_wall_appearance_checks(temp,path,walls,camera);
 auto custom=architecture();custom.maps.push_back({"FLRm",{}});custom.maps.back().second.entries.push_back({});
 rejects([&]{SavedLotRenderer r(std::move(custom),saved,definitions,path.string(),{},view);});
 {
  auto unresolved=architecture();odh::ArchitectureMap floor_map,wall_map;
  odh::ArchitectureMapEntry floor_entry;floor_entry.id=4;floor_entry.name="ExpansionFloor.flr";
  odh::ArchitectureMapEntry wall_entry;wall_entry.id=8;wall_entry.name="ExpansionWall.wll";
  floor_map.entries.push_back(floor_entry);wall_map.entries.push_back(wall_entry);
  unresolved.maps={{"FLRm",floor_map},{"WALm",wall_map}};
  SavedLotMaterialOptions no_resources;std::string diagnostic;
  try{SavedLotRenderer r(unresolved,saved,definitions,path.string(),walls.string(),view,no_resources);}
  catch(const odh::FormatError& error){diagnostic=error.what();}
  check(diagnostic.find("floor_map_misses=1 wall_map_misses=1 catalog_entries=0")!=std::string::npos);
  check(diagnostic.find("floor id=4 name=ExpansionFloor.flr")!=std::string::npos);
  check(diagnostic.find("wall id=8 name=ExpansionWall.wll")!=std::string::npos);
  unresolved.maps[0].second.entries[0].name=std::string(300,'x')+"\n.flr";
  diagnostic.clear();
  try{SavedLotRenderer r(unresolved,saved,definitions,path.string(),walls.string(),view,no_resources);}
  catch(const odh::FormatError& error){diagnostic=error.what();}
  check(!diagnostic.empty()&&diagnostic.size()<1500&&diagnostic.find('\n')==std::string::npos);
 }
 odh::SavedWorld missing;rejects([&]{SavedLotRenderer r(architecture(),missing,definitions,path.string(),{},view);});
 view.level=0;rejects([&]{SavedLotRenderer r(architecture(),saved,definitions,path.string(),{},view);});
 graphics_slot_cache_capacity_checks(temp);
 contained_checks(temp,path);
 std::cout<<checks<<" live saved lot checks passed (synthetic only)\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
