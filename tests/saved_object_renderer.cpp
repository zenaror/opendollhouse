#include "saved_object_renderer.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
namespace {
int checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("saved object renderer assertion failed #"+std::to_string(checks));}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}check(caught);}
void set(std::string& b,std::size_t p,std::uint32_t v,unsigned n=4,bool big=false){for(unsigned i=0;i<n;++i)b.at(p+i)=static_cast<char>((v>>((big?n-1-i:i)*8))&255);}
std::string word(std::uint32_t v){std::string b(2,'\0');set(b,0,v,2);return b;}
std::string chunk(const std::string& type,std::uint16_t id,const std::string& payload){std::string b(76,'\0');b.replace(0,4,type);set(b,4,static_cast<std::uint32_t>(76+payload.size()),4,true);set(b,8,id,2,true);return b+payload;}
std::string objd(){std::string b(216,'\0');set(b,0,138);set(b,6,10,2);set(b,8,1,2);set(b,18,4,2);set(b,28,123);set(b,128,5,2);set(b,130,1,2);return b;}
std::string sprite(unsigned flags=3){std::string b(32,'\0');set(b,0,1000);set(b,4,1);set(b,8,0x12340007);set(b,12,16);set(b,16,1,2);set(b,18,1,2);set(b,20,flags);return b+word(6)+word((1<<13)|1)+std::string("\xff\0",2)+word(5<<13);}
std::string palette(){std::string b(19,'\0');set(b,0,1);set(b,4,1);b[16]=10;b[17]=20;b[18]=30;return b;}
std::string empty_sprite(){std::string b(32,'\0');set(b,0,1000);set(b,4,1);set(b,8,7);set(b,12,16);set(b,20,3);return b+word(5<<13);}
std::string large_sprite(){std::string b(32,'\0');set(b,0,1000);set(b,4,1);set(b,8,7);set(b,12,16);set(b,16,256,2);set(b,18,256,2);set(b,20,3);std::string pixels;for(unsigned i=0;i<256;++i)pixels+=std::string("\xff\0",2);const auto row=word(516)+word((1<<13)|256)+pixels;for(unsigned i=0;i<256;++i)b+=row;return b+word(5<<13);}
std::string large_transparent_sprite(){std::string b(32,'\0');set(b,0,1000);set(b,4,1);set(b,8,7);set(b,12,16);set(b,16,1024,2);set(b,18,1024,2);set(b,20,3);return b+word((4<<13)|1024)+word(5<<13);}
std::string rotation_group(){std::string b(6+4*(12+32),'\0');set(b,0,20004,2);set(b,2,4);for(unsigned i=0;i<4;++i){const auto image=6+i*44;set(b,image,1U<<(2*i));set(b,image+4,3);set(b,image+8,1);const auto sprite=image+12;set(b,sprite,5+i);set(b,sprite+4,0);}return b;}
std::string rotation_iff(){std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');b+=chunk("OBJD",1,objd());b+=chunk("DGRP",10,rotation_group());b+=chunk("PALT",7,palette());for(std::uint16_t id=5;id<9;++id)b+=chunk("SPR2",id,large_transparent_sprite());return b;}
std::string group(){std::string b(82,'\0');set(b,0,20004,2);set(b,2,1);set(b,6,1);set(b,10,3);set(b,14,2);set(b,18,5);set(b,50,6);set(b,58,1);return b;}
std::string iff(unsigned flags=3,bool duplicate=false,bool spr1=false,bool empty=false){std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');b+=chunk("OBJD",1,objd());b+=chunk("DGRP",10,group());b+=chunk("PALT",7,palette());b+=chunk(spr1?"SPR#":"SPR2",5,empty?empty_sprite():sprite(flags));b+=chunk("SPR2",6,sprite(flags));if(duplicate)b+=chunk("DGRP",10,group());return b;}
struct Temp {std::filesystem::path p;Temp(){p=std::filesystem::temp_directory_path()/("odh-saved-objects-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(p);}~Temp(){std::error_code e;std::filesystem::remove_all(p,e);}};
void write(const std::filesystem::path& p,const std::string& b){std::ofstream f(p,std::ios::binary);f.write(b.data(),static_cast<std::streamsize>(b.size()));if(!f)throw std::runtime_error("fixture write failed");}
odh::DefinitionCandidate candidate(const std::filesystem::path& p){std::istringstream stream(objd());return {p.string(),"",1,odh::read_object_definition(odh::View(stream,objd().size()))};}
}
int main(){try{
 Temp tmp;const auto path=tmp.p/"one.iff";write(path,iff());odh::DefinitionIndex index({candidate(path)});SavedObjectRenderer renderer(index);
 odh::WorldProjection camera;camera.width=camera.height=128;
 odh::RasterFrame base{128,128,std::vector<std::array<std::uint8_t,4>>(16384,{255,255,255,255}),std::vector<float>(16384,std::numeric_limits<float>::infinity())};
 SavedObjectVisual visual{1,123,0,0,{0,0,0},{1}};
 auto rendered=renderer.render({visual},camera,base);check(rendered.layers==2);check(rendered.records.size()==1&&rendered.records[0].omitted_dynamic_layers==0);check(rendered.cached_assets==1&&rendered.cached_groups==1&&rendered.cached_frames==2);check(rendered.cached_pixels==2&&rendered.cached_frame_bytes==12);check(rendered.composite.shaded_samples==2);check(base.pixels[0]==std::array<std::uint8_t,4>{255,255,255,255});
 const auto first=rendered.composite.image.pixels;rendered=renderer.render({visual},camera,base);check(rendered.composite.image.pixels==first&&rendered.cached_frames==2&&rendered.cached_pixels==2);
 const auto picked=renderer.render({visual},camera,base,true);check(picked.composite.owner_ids.size()==base.pixels.size());
 std::size_t owned=0;bool valid_owners=true;for(auto id:picked.composite.owner_ids){valid_owners=valid_owners&&(id==0||id==1);if(id)++owned;}check(valid_owners&&owned==2);
 visual.dynamic_sprite_flags={0};rendered=renderer.render({visual},camera,base);check(rendered.layers==1&&rendered.records[0].omitted_dynamic_layers==1);visual.dynamic_sprite_flags={-1};check(renderer.render({visual},camera,base).layers==2);visual.dynamic_sprite_flags.clear();check(renderer.render({visual},camera,base).layers==1);visual.dynamic_sprite_flags={1};
 auto bad=visual;bad.id=0;rejects([&]{renderer.render({bad},camera,base);});rejects([&]{renderer.render({visual,visual},camera,base);});bad=visual;bad.direction=8;rejects([&]{renderer.render({bad},camera,base);});bad=visual;bad.graphic=-1;rejects([&]{renderer.render({bad},camera,base);});bad.graphic=1;rejects([&]{renderer.render({bad},camera,base);});bad=visual;bad.guid=124;rejects([&]{renderer.render({bad},camera,base);});bad=visual;bad.direction=2;rejects([&]{renderer.render({bad},camera,base);});bad=visual;bad.tile_position[0]=std::numeric_limits<float>::quiet_NaN();rejects([&]{renderer.render({bad},camera,base);});
 auto wrong_camera=camera;wrong_camera.width=33;rejects([&]{renderer.render({visual},wrong_camera,base);});rejects([&]{renderer.render(std::vector<SavedObjectVisual>(4097,visual),camera,base);});bad=visual;bad.dynamic_sprite_flags.resize(65537);rejects([&]{renderer.render({bad},camera,base);});
 odh::DefinitionIndex ambiguous({candidate(path),candidate(path)});SavedObjectRenderer ambiguous_renderer(ambiguous);rejects([&]{ambiguous_renderer.render({visual},camera,base);});
 for(unsigned flags:{1U,2U,11U}){const auto p=tmp.p/("flags"+std::to_string(flags)+".iff");write(p,iff(flags));odh::DefinitionIndex defs({candidate(p)});SavedObjectRenderer r(defs);rejects([&]{r.render({visual},camera,base);});}
 {const auto p=tmp.p/"duplicate.iff";write(p,iff(3,true));odh::DefinitionIndex defs({candidate(p)});SavedObjectRenderer r(defs);rejects([&]{r.render({visual},camera,base);});}
 {const auto p=tmp.p/"spr1.iff";write(p,iff(3,false,true));odh::DefinitionIndex defs({candidate(p)});SavedObjectRenderer r(defs);rejects([&]{r.render({visual},camera,base);});}
 {const auto p=tmp.p/"empty.iff";write(p,iff(3,false,false,true));odh::DefinitionIndex defs({candidate(p)});SavedObjectRenderer r(defs);const auto result=r.render({visual},camera,base);check(result.layers==1&&result.records[0].omitted_empty_layers==1);check(result.cached_frames==2&&result.cached_pixels==1&&result.composite.shaded_samples==1);}
 {const auto p=tmp.p/"nographics.iff";auto b=iff();set(b,64+76+8,0,2);write(p,b);odh::DefinitionIndex defs({candidate(p)});SavedObjectRenderer r(defs);
  check(r.render({visual},camera,base).layers==2);auto missing=visual;missing.graphic=2;auto result=r.render({missing},camera,base);
  check(result.records[0].no_graphics&&result.layers==0&&result.composite.image.pixels==base.pixels);}
 for(bool negative:{false,true}){const auto p=tmp.p/(negative?"negative-wrap.iff":"positive-wrap.iff");auto b=iff();
  set(b,64+76+6,negative?0:65535,2);set(b,b.find("DGRP")+8,negative?65535:0,2,true);write(p,b);
  odh::DefinitionIndex defs({candidate(p)});SavedObjectRenderer r(defs);auto v=visual;v.graphic=negative?-1:1;
  const auto result=r.render({v},camera,base);check(result.layers==2&&result.records[0].dgrp_id==(negative?65535:0));}
 {const auto p=tmp.p/"repeated.iff";auto b=iff();auto start=b.find("SPR2");b.resize(start);b+=chunk("SPR2",5,large_sprite());b+=chunk("SPR2",6,large_sprite());write(p,b);odh::DefinitionIndex defs({candidate(p)});SavedObjectRenderer r(defs);
  std::vector<SavedObjectVisual> offscreen;for(std::int16_t i=1;i<=40;++i){auto v=visual;v.id=i;v.tile_position={10000,10000,0};offscreen.push_back(v);}
  const auto culled=r.render(offscreen,camera,base);check(culled.layers==0&&culled.offscreen_layers==80&&culled.cached_pixels==131072);
  check(culled.composite.tested_samples==0&&culled.composite.image.pixels==base.pixels&&culled.records[0].offscreen_layers==2);
  std::vector<SavedObjectVisual> visible;for(std::int16_t i=1;i<=32;++i){auto v=visual;v.id=i;v.tile_position={0,0,0};visible.push_back(v);}
  const auto result=r.render(visible,camera,base);check(result.layers==64&&result.offscreen_layers==0&&result.cached_pixels==131072);
  check(result.composite.tested_samples>0&&result.composite.shaded_samples>0);
  auto v=visual;v.id=33;v.tile_position={0,0,0};visible.push_back(v);rejects([&]{r.render(visible,camera,base);});}
 {const auto p=tmp.p/"rotation-cache.iff";write(p,rotation_iff());odh::DefinitionIndex defs({candidate(p)});SavedObjectRenderer r(defs);auto v=visual;v.tile_position={10000,10000,0};camera.rotation=0;auto a=r.render({v},camera,base);check(a.cached_frames==1&&a.cached_pixels==1048576);camera.rotation=1;auto b=r.render({v},camera,base);check(b.cached_frames==1&&b.cached_pixels==1048576);camera.rotation=0;auto c=r.render({v},camera,base);check(c.cached_frames==1&&c.cached_pixels==1048576);camera.rotation=0;}
 {const auto p=tmp.p/"changed.iff";auto entry=candidate(p);auto b=iff();set(b,64+76+28,999);write(p,b);odh::DefinitionIndex defs({entry});SavedObjectRenderer r(defs);rejects([&]{r.render({visual},camera,base);});}
 SavedObjectRenderer moved(std::move(renderer));rejects([&]{renderer.render({visual},camera,base);});check(moved.render({visual},camera,base).cached_pixels==2);
 check(moved.render({},camera,base).composite.image.pixels==base.pixels);
 std::cout<<checks<<" saved object renderer checks passed (synthetic only)\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
