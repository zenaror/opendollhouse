#include "saved_floor_renderer.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
namespace {
int checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("saved floor assertion #"+std::to_string(checks));}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught);}
void set(std::string& b,std::size_t p,std::uint32_t v,unsigned n=4,bool big=false){for(unsigned i=0;i<n;++i)b.at(p+i)=static_cast<char>((v>>((big?n-1-i:i)*8))&255);}
std::string word(std::uint32_t v){std::string b(2,'\0');set(b,0,v,2);return b;}
std::string chunk(const std::string& type,std::uint16_t id,const std::string& payload){std::string b(76,'\0');b.replace(0,4,type);set(b,4,static_cast<std::uint32_t>(76+payload.size()),4,true);set(b,8,id,2,true);return b+payload;}
struct Temp {std::filesystem::path p;Temp(){p=std::filesystem::temp_directory_path()/("odh-saved-objects-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(p);}~Temp(){std::error_code e;std::filesystem::remove_all(p,e);}};
void write(const std::filesystem::path& p,const std::string& b){std::ofstream f(p,std::ios::binary);f.write(b.data(),static_cast<std::streamsize>(b.size()));if(!f)throw std::runtime_error("fixture write failed");}

std::string sprite(){std::string b(32,'\0');set(b,0,1000);set(b,4,1);set(b,8,7);set(b,12,16);set(b,16,2,2);set(b,18,2,2);set(b,20,1);
 for(int row=0;row<2;++row)b+=word(6)+word((6<<13)|2)+std::string("\0\0",2);
 return b+word(5<<13);}
std::string iff(bool duplicate=false){std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');
 std::string pal(19,'\0');set(pal,0,1);set(pal,4,1);pal[16]=10;pal[17]=20;pal[18]=30;b+=chunk("PALT",7,pal);
 for(auto id:{1,257,513})b+=chunk("SPR2",static_cast<std::uint16_t>(id),sprite());
 if(duplicate)b+=chunk("SPR2",513,sprite());return b;}
odh::SavedArchitecture architecture(){odh::SavedArchitecture a;a.width=a.height=4;
 for(auto& level:a.levels){level.floors.resize(16);level.walls.resize(16);}a.altitude=std::vector<std::array<std::uint8_t,4>>(16,{0,20,0,0});a.levels[0].floors[5]=1;return a;}
}
int main(){try{
 Temp tmp;const auto path=tmp.p/"floors.iff";write(path,iff());SavedFloorRenderer renderer(path.string());auto a=architecture();
 odh::WorldProjection camera;camera.width=camera.height=128;camera.center_tile={1.5f,1.5f,3.75f};
 for(std::uint8_t zoom=1;zoom<=3;++zoom)for(std::uint8_t rotation=0;rotation<4;++rotation){camera.zoom=zoom;camera.rotation=rotation;
  auto result=renderer.render(a,camera);check(result.draws.size()==1&&result.triangles==2);check(result.texture_bytes==16);
  check(result.materials[0].sprite_id==1+(zoom-1)*256&&result.materials[0].width==2&&result.materials[0].height==2);
  check(result.draws[0].altitude[0]>3.749f&&result.draws[0].altitude[0]<3.751f);
  std::size_t covered=0;for(std::size_t i=0;i<result.image.depth.size();++i)if(std::isfinite(result.image.depth[i])){++covered;check(result.image.pixels[i]==std::array<std::uint8_t,4>{10,20,30,255});}
  check(covered>0);check(renderer.render(a,camera).image.pixels==result.image.pixels);
 }
 camera.zoom=3;camera.rotation=0;
 auto& wall=a.levels[0].walls[5];wall.segments=odh::wall_horizontal_diagonal;wall.top_left_style=1;a.levels[0].floors[5]=255;
 auto result=renderer.render(a,camera);check(result.triangles==1&&result.draws.size()==1&&result.draws[0].half==odh::FloorHalf::horizontal_second);
 wall.top_left_pattern=1;check(renderer.render(a,camera).triangles==2);
 wall.segments=odh::wall_vertical_diagonal;result=renderer.render(a,camera);check(result.draws[0].half==odh::FloorHalf::vertical_first&&result.draws[1].half==odh::FloorHalf::vertical_second);
 wall.top_left_pattern=wall.top_left_style=0;a.levels[0].floors[5]=1;check(renderer.render(a,camera).triangles==2);
 wall.segments=48;rejects([&]{renderer.render(a,camera);});wall.segments=0;
 a.levels[1].floors[5]=1;result=renderer.render(a,camera);check(result.draws.size()==2&&result.triangles==4&&result.draws[1].level==2);
 a.levels[1].floors[5]=0;a.pools=std::vector<std::uint8_t>(16,255);check(renderer.render(a,camera).draws.size()==1);(*a.pools)[0]=1;rejects([&]{renderer.render(a,camera);});a.pools.reset();
 a.levels[0].floors[5]=256;rejects([&]{renderer.render(a,camera);});a.levels[0].floors[5]=2;rejects([&]{renderer.render(a,camera);});a.levels[0].floors[5]=1;
 a.levels[0].floors[15]=1;rejects([&]{renderer.render(a,camera);});a.levels[0].floors[15]=0;
 a.altitude.reset();rejects([&]{renderer.render(a,camera);});check(renderer.render(a,camera,false).draws[0].altitude[0]==0);
 const auto bad=tmp.p/"duplicate.iff";write(bad,iff(true));SavedFloorRenderer dup(bad.string());rejects([&]{dup.render(a,camera,false);});
 {const auto many=tmp.p/"many.iff";auto b=iff();for(std::uint16_t id=514;id<=529;++id)b+=chunk("SPR2",id,sprite());write(many,b);SavedFloorRenderer r(many.string());
  auto grid=architecture();grid.width=grid.height=8;grid.altitude=std::vector<std::array<std::uint8_t,4>>(64,{0,20,0,0});
  for(auto& level:grid.levels){level.floors.assign(64,0);level.walls.assign(64,{});}std::uint16_t pattern=1;
  for(unsigned y=0;y<3;++y)for(unsigned x=0;x<6;++x)grid.levels[0].floors[y*8+x]=pattern++;
  rejects([&]{r.render(grid,camera);});}
 check(a.levels[0].floors[5]==1&&a.levels[1].floors[5]==0);
 std::cout<<checks<<" saved floor renderer checks passed (synthetic only)\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
