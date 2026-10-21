#include "saved_lot_renderer.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
namespace {
int checks=0;
void check(bool value){++checks;if(!value)throw std::runtime_error("external architecture assertion #"+std::to_string(checks));}
template<class F>void rejects(F fn){bool caught=false;try{fn();}catch(const std::exception&){caught=true;}check(caught);}
void set(std::string& b,std::size_t p,std::uint32_t v,unsigned n=4,bool big=false){for(unsigned i=0;i<n;++i)b.at(p+i)=static_cast<char>((v>>((big?n-1-i:i)*8))&255);}
std::string word(std::uint32_t v){std::string b(2,'\0');set(b,0,v,2);return b;}
std::string header(){std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');return b;}
std::string chunk(const std::string& type,std::uint16_t id,const std::string& payload){std::string b(76,'\0');b.replace(0,4,type);set(b,4,static_cast<std::uint32_t>(76+payload.size()),4,true);set(b,8,id,2,true);return b+payload;}
std::string palette(unsigned red,unsigned green){std::string b(19,'\0');set(b,0,1);set(b,4,1);b[16]=static_cast<char>(red);b[17]=static_cast<char>(green);b[18]=10;return b;}
std::string floor_sprite(unsigned pal){std::string b(32,'\0');set(b,0,1000);set(b,4,1);set(b,8,pal);set(b,12,16);set(b,16,2,2);set(b,18,2,2);set(b,20,1);
 for(int row=0;row<2;++row)b+=word(6)+word((6<<13)|2)+std::string("\0\0",2);return b+word(5<<13);}
std::string wall_sprite(unsigned pal,unsigned frames=4){std::string b(12+4*frames,'\0');set(b,0,1000);set(b,4,frames);set(b,8,pal);
 for(unsigned i=0;i<frames;++i){set(b,12+i*4,static_cast<std::uint32_t>(b.size()));std::string f(8,'\0');set(f,4,1,2);set(f,6,1,2);b+=f+std::string("\4\6\2\1\0\0\5\0",8);}return b;}
std::string floors(bool duplicate=false,int missing=0,unsigned base=30){auto b=header();for(unsigned z=1;z<=3;++z){b+=chunk("PALT",static_cast<std::uint16_t>(100+z),palette(base*z,0));if(int(z)!=missing)b+=chunk("SPR2",static_cast<std::uint16_t>(1+(z-1)*256),floor_sprite(100+z));}
 if(duplicate)b+=chunk("SPR2",513,floor_sprite(103));return b;}
std::string walls(bool global=false,bool duplicate=false,int missing=0){auto b=header();const unsigned ids[]={1,1793,2049};
 for(unsigned z=1;z<=3;++z){b+=chunk("PALT",static_cast<std::uint16_t>(200+z),palette(0,30*z));if(int(z)!=missing)b+=chunk("SPR#",static_cast<std::uint16_t>(global?1537+(z-1)*256:ids[z-1]),wall_sprite(200+z));
  if(global){b+=chunk("SPR#",static_cast<std::uint16_t>(2+(z-1)*512),wall_sprite(200+z));b+=chunk("SPR#",static_cast<std::uint16_t>(4095+z),wall_sprite(200+z,46));}}
 if(duplicate)b+=chunk("SPR#",2049,wall_sprite(203));return b;}
std::string far(const std::vector<std::pair<std::string,std::string>>& entries){std::string b(16,'\0');b.replace(0,8,"FAR!byAZ");set(b,8,1);std::vector<std::uint32_t> offsets;
 for(const auto& e:entries){offsets.push_back(static_cast<std::uint32_t>(b.size()));b+=e.second;}
 const auto manifest=b.size();set(b,12,static_cast<std::uint32_t>(manifest));b.resize(b.size()+4,'\0');set(b,manifest,static_cast<std::uint32_t>(entries.size()));
 for(std::size_t i=0;i<entries.size();++i){const auto& e=entries[i];std::string record(16,'\0');set(record,0,static_cast<std::uint32_t>(e.second.size()));set(record,4,static_cast<std::uint32_t>(e.second.size()));set(record,8,offsets[i]);set(record,12,static_cast<std::uint32_t>(e.first.size()));b+=record+e.first;}return b;}
void write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));if(!file)throw std::runtime_error("synthetic fixture write failed");}
struct Temp{std::filesystem::path path=std::filesystem::temp_directory_path()/("odh-external-architecture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
odh::ArchitectureMap map(std::uint16_t id,const std::string& name,std::uint8_t alias){
 std::string payload(16,'\0');set(payload,4,1);set(payload,12,name.empty()?0:1);
 if(!name.empty()){payload+=name;payload.push_back('\0');if(name.size()%2==0)payload.push_back('\0');
  payload+=std::string(4,'\0');payload.push_back(static_cast<char>(alias));payload+=std::string(5+2*id,'\0');}
 const auto bytes=header()+chunk("FLRm",id,payload);std::istringstream stream(bytes);odh::ArchitectureMap result;
 odh::iff_chunks(odh::View(stream,bytes.size()),[&](const auto& c){std::vector<std::uint8_t> raw(c.payload.size());c.payload.read(0,raw.data(),raw.size());result=odh::read_architecture_map(raw,c.id);});
 return result;
}
SavedArchitectureResource architecture(){SavedArchitectureResource result;auto& a=result.architecture;a.width=a.height=64;
 for(auto& level:a.levels){level.floors.resize(4096);level.walls.resize(4096);}a.levels[0].flags=std::vector<std::uint8_t>(4096,0);
 a.pools=a.water=std::vector<std::uint8_t>(4096,0);a.altitude=std::vector<std::array<std::uint8_t,4>>(4096,{0,0,0,0});a.grass=std::vector<std::uint8_t>(4096,127);
 a.levels[0].floors[65]=42;a.levels[0].walls[65]={1,0,1,0,43,0,0,0};
 // Deliberately put unrelated ID7 and fallback ID0 before ID1 in the vector.
 result.maps={{"FLRm",map(7,"absent.flr",42)},{"FLRm",map(0,"wrong.flr",42)},{"FLRm",map(1,"FOO.flr",42)},
              {"WALm",map(0,"wrong.wll",43)},{"WALm",map(1,"BAR.wll",43)}};return result;}
odh::SavedArchitecture renderable(const odh::PreparedArchitecture& prepared){odh::SavedArchitecture result;result.width=prepared.width;result.height=prepared.height;
 for(std::size_t i=0;i<2;++i){result.levels[i].floors=prepared.levels[i].floors;result.levels[i].walls=prepared.levels[i].walls;}return result;}
void family_render_checks(const odh::ArchitectureResourceCatalog& catalog,const std::string& global_floors,const std::string& global_walls){
 auto raw=architecture();const auto aliases=odh::resolve_architecture_aliases(*selected_architecture_map(raw,"FLRm"),*selected_architecture_map(raw,"WALm"),catalog.floors(),catalog.walls());
 check(aliases.floor_map_complete()&&aliases.wall_map_complete());auto prepared=odh::prepare_saved_architecture(raw.architecture,aliases.aliases,{false,4});auto a=renderable(prepared);
 check(prepared.levels[0].floors[5]==256&&prepared.levels[0].walls[5].top_left_pattern==256);check(raw.architecture.levels[0].floors[65]==42);
 odh::DefinitionIndex definitions({});SavedFloorRenderer floor(global_floors,catalog);SavedWallRenderer wall(global_walls,definitions,catalog);
 odh::WorldProjection camera;camera.width=camera.height=128;camera.center_tile={1,1,0};
 for(std::uint8_t z=1;z<=3;++z){camera.zoom=z;const auto f=floor.render(a,camera,false,0,1);check(f.draws.size()==1&&f.materials[0].pattern==256&&f.materials[0].sprite_id==1+(z-1)*256&&f.materials[0].palette_id==100+z);
  bool colored=false;for(std::size_t i=0;i<f.image.depth.size();++i)if(std::isfinite(f.image.depth[i])){colored=true;check(f.image.pixels[i]==std::array<std::uint8_t,4>{static_cast<std::uint8_t>(30*z),0,10,255});}check(colored);
  const auto blank=odh::rasterize(128,128,{},{});auto w=wall.render(a,{},camera,blank,false);const unsigned ids[]={1,1793,2049};
  check(w.draws.size()==1&&w.draws[0].pattern_sprite==ids[z-1]&&w.draws[0].pattern_palette==200+z);
  check(w.draws[0].mask_sprite==2+(z-1)*512&&w.draws[0].mask_source==global_walls&&w.draws[0].pattern_source!=global_walls);
  colored=false;for(std::size_t i=0;i<w.composite.image.depth.size();++i)if(std::isfinite(w.composite.image.depth[i])){colored=true;check(w.composite.image.pixels[i]==std::array<std::uint8_t,4>{0,static_cast<std::uint8_t>(30*z),10,255});}check(colored);
 }
 // Global byte patterns still use the original shared global resources.
 a.levels[0].floors[5]=1;a.levels[0].walls[5].top_left_pattern=1;camera.zoom=3;
 check(floor.render(a,camera,false).materials[0].source==global_floors);
 check(wall.render(a,{},camera,odh::rasterize(128,128,{},{}),false).draws[0].pattern_source==global_walls);
}
}
int main(){try{
 Temp temp;const auto gf=temp.path/"floors.iff",gw=temp.path/"walls.iff",flr=temp.path/"foo.flr",wll=temp.path/"bar.wll",archive=temp.path/"materials.far";
 write(gf,floors());write(gw,walls(true));write(flr,floors());write(wll,walls());
 auto catalog=odh::discover_architecture_resources({{flr.string()},{wll.string()}});family_render_checks(catalog,gf.string(),gw.string());
 write(archive,far({{"foo.flr",floors(false,0,10)},{"bar.wll",walls()},{"foo.flr",floors()}}));
 auto far_catalog=odh::discover_architecture_resources({{archive.string(),odh::ArchitectureResourceSourceKind::far1_archive}});
 check(far_catalog.diagnostics.duplicate_floor_names==1);family_render_checks(far_catalog,gf.string(),gw.string());
 odh::DefinitionIndex definitions({});odh::SavedWorld saved;saved.world=std::make_unique<odh::World>();
 SavedLotMaterialOptions options;options.catalog=far_catalog;options.preparation.target_size=4;
 auto raw=architecture();SavedLotRenderer lot(raw,saved,definitions,gf.string(),gw.string(),{},options);
 check(lot.material_aliases()->diagnostics.floor_map_resolved==1&&lot.prepared_architecture()->levels[0].floors[5]==256);
 check(lot.runtime_room_map_available());
 const auto runtime_room=lot.runtime_room_at({24,24,1,0});
 check(runtime_room&&runtime_room==odh::geometric_room_at(
     odh::generate_prepared_room_map(*lot.prepared_architecture()),24,24,1));
 check(lot.runtime_room_at({32767,32767,1,0})==0);
 odh::WorldProjection camera;camera.width=camera.height=128;camera.center_tile={1,1,0};
 auto result=lot.render(camera,odh::rasterize(128,128,{},{}));check(result.floor_draws==1&&result.wall_layers==1&&result.wall_junctions==2);
 check(raw.architecture.levels[0].floors[65]==42&&raw.maps[2].second.entries[0].name=="FOO.flr");
 // ID1 existence, not nonempty content, selects it; fallback only if absent.
 auto selection=raw;selection.maps[2].second.entries.clear();check(selected_architecture_map(selection,"FLRm")->chunk_id==1&&selected_architecture_map(selection,"FLRm")->entries.empty());
 selection.maps.erase(selection.maps.begin()+2);check(selected_architecture_map(selection,"FLRm")->chunk_id==0);
 selection.maps.erase(selection.maps.begin()+1);check(!selected_architecture_map(selection,"FLRm"));
 selection=raw;selection.maps.push_back(selection.maps[2]);rejects([&]{selected_architecture_map(selection,"FLRm");});
 // Missing declarations fail instead of painting an unrelated global pattern.
 auto missing=raw;missing.maps[2].second.entries[0].name="missing.flr";rejects([&]{SavedLotRenderer r(missing,saved,definitions,gf.string(),gw.string(),{},options);});
 auto limited=options;limited.alias_limits.map_entries=0;rejects([&]{SavedLotRenderer r(raw,saved,definitions,gf.string(),gw.string(),{},limited);});
 limited=options;limited.preparation_limits.retained_bytes=1;rejects([&]{SavedLotRenderer r(raw,saved,definitions,gf.string(),gw.string(),{},limited);});
 auto malformed=catalog;malformed.resources.push_back(malformed.resources[0]);rejects([&]{SavedFloorRenderer r(gf.string(),malformed);});
 malformed=catalog;malformed.sources[0].path=std::string(4097,'a');rejects([&]{SavedWallRenderer r(gw.string(),definitions,malformed);});
 auto a=renderable(*lot.prepared_architecture());a.levels[0].floors[5]=257;SavedFloorRenderer floor(gf.string(),catalog);rejects([&]{floor.render(a,camera,false);});
 a.levels[0].floors[5]=256;a.levels[0].walls[5].top_left_pattern=257;SavedWallRenderer wall(gw.string(),definitions,catalog);rejects([&]{wall.render(a,{},camera,odh::rasterize(128,128,{},{}),false);});
 // External paint must keep the separately resolved object mask binding.
 const auto object=temp.path/"object.iff";std::string objd(216,'\0');set(objd,0,138);set(objd,18,4,2);set(objd,28,123);set(objd,92,40,2);set(objd,162,5000,2);
 auto object_bytes=header()+chunk("OBJD",1,objd)+chunk("PALT",7,palette(100,100));
 for(unsigned z=0;z<3;++z)object_bytes+=chunk("SPR#",static_cast<std::uint16_t>(5000+z),wall_sprite(7));write(object,object_bytes);
 std::istringstream object_stream(objd);const auto def=odh::read_object_definition(odh::View(object_stream,objd.size()));
 odh::DefinitionIndex object_definitions({{object.string(),"",1,def}});SavedWallRenderer object_wall(gw.string(),object_definitions,far_catalog);
 odh::WallAppearance appearance;odh::WallAppearanceStyle style{40,123,{}};for(unsigned n=0;n<6;++n)style.sprites[n]=static_cast<std::uint16_t>(5000+n);
 appearance.faces.push_back({1,1,1,1,style,11,11,1,1,false,false});a.levels[0].walls[5].top_left_pattern=256;
 const auto object_result=object_wall.render(a,appearance,camera,odh::rasterize(128,128,{},{}),false);
 check(object_result.draws[0].pattern_source==archive.string()&&object_result.draws[0].pattern_palette==203&&object_result.draws[0].mask_source==object.string()&&object_result.draws[0].mask_sprite==5002&&object_result.draws[0].mask_palette==7);
 // Both families share the 16-source cache cap with their global IFF.
 std::vector<odh::ArchitectureResourceSource> many_sources;
 for(unsigned n=0;n<16;++n){const auto f=temp.path/("budget"+std::to_string(n)+".flr"),w=temp.path/("budget"+std::to_string(n)+".wll");write(f,floors());write(w,walls());many_sources.push_back({f.string()});many_sources.push_back({w.string()});}
 const auto many=odh::discover_architecture_resources(many_sources);SavedFloorRenderer many_floors(gf.string(),many);SavedWallRenderer many_walls(gw.string(),definitions,many);
 for(std::uint16_t id=256;id<271;++id){a.levels[0].floors[5]=id;a.levels[0].walls[5].top_left_pattern=id;check(many_floors.render(a,camera,false).draws.size()==1);check(many_walls.render(a,{},camera,odh::rasterize(128,128,{},{}),false).draws.size()==1);}
 a.levels[0].floors[5]=271;a.levels[0].walls[5].top_left_pattern=271;rejects([&]{many_floors.render(a,camera,false);});rejects([&]{many_walls.render(a,{},camera,odh::rasterize(128,128,{},{}),false);});
 a.levels[0].floors[5]=256;
 // Selected resource chunks are strict about absence and ambiguity at each family.
 write(flr,floors(true));auto bad=odh::discover_architecture_resources({{flr.string()},{wll.string()}});SavedFloorRenderer duplicate_floor(gf.string(),bad);rejects([&]{duplicate_floor.render(a,camera,false);});
 write(flr,floors(false,3));bad=odh::discover_architecture_resources({{flr.string()},{wll.string()}});SavedFloorRenderer absent_floor(gf.string(),bad);rejects([&]{absent_floor.render(a,camera,false);});
 a.levels[0].walls[5].top_left_pattern=256;write(wll,walls(false,true));bad=odh::discover_architecture_resources({{wll.string()}});SavedWallRenderer duplicate_wall(gw.string(),definitions,bad);rejects([&]{duplicate_wall.render(a,{},camera,odh::rasterize(128,128,{},{}),false);});
 write(wll,walls(false,false,3));bad=odh::discover_architecture_resources({{wll.string()}});SavedWallRenderer absent_wall(gw.string(),definitions,bad);rejects([&]{absent_wall.render(a,{},camera,odh::rasterize(128,128,{},{}),false);});
 // Metadata locators must remain consistent when reopened; no filename fallback.
 auto stale=far_catalog;stale.resources[0].source.offset++;SavedFloorRenderer changed(gf.string(),stale);rejects([&]{changed.render(a,camera,false);});
 std::cout<<checks<<" external architecture renderer checks passed (synthetic only)\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
