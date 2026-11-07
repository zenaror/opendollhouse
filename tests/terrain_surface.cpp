#include "odh/terrain_surface.hpp"
#include <iostream>
#include <cmath>
#include <limits>
namespace {
int checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("terrain surface assertion #"+std::to_string(checks));}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught);}
odh::SavedArchitecture grid(){odh::SavedArchitecture a;a.width=a.height=4;a.altitude=std::vector<std::array<std::uint8_t,4>>(16,{0,20,0,0});a.grass=std::vector<std::uint8_t>(16,127);
 for(auto& l:a.levels){l.floors.resize(16);l.walls.resize(16);}return a;}
}
int main(){try{
 auto a=grid();odh::WorldProjection camera;camera.width=camera.height=128;camera.center_tile={2,2,3.75f};
 for(std::uint8_t zoom=1;zoom<=3;++zoom)for(std::uint8_t rotation=0;rotation<4;++rotation){camera.zoom=zoom;camera.rotation=rotation;
  auto mesh=odh::terrain_surface(a,camera,odh::TerrainTheme::grass);check(mesh.whole_tiles==4&&mesh.half_tiles==0&&mesh.triangles.size()==8);
  check(mesh.room_samples.size()==mesh.triangles.size()&&mesh.room_samples[0]==std::array<std::int32_t,3>{26,21,1}&&
      mesh.room_samples[1]==std::array<std::int32_t,3>{21,26,1});
  for(const auto& tri:mesh.triangles)for(const auto& v:tri.vertices){check(v.normal==std::array<float,3>{0,1,0});check(v.color==std::array<float,3>{80/255.0f,116/255.0f,59/255.0f});}
  odh::RasterOptions options;options.vertex_colors=true;options.lighting=true;options.directional_up_light=true;
  const auto frame=odh::rasterize(camera.width,camera.height,mesh.triangles,{{1,1,32,true,{{255,255,255,255}},0}},options);
  check(frame.shaded_samples>0);for(std::size_t i=0;i<frame.depth.size();++i)if(std::isfinite(frame.depth[i]))check(frame.pixels[i]==std::array<std::uint8_t,4>{80,116,59,255});
 }
 camera.zoom=3;camera.rotation=0;
 (*a.grass)[0]=128;auto mesh=odh::terrain_surface(a,camera,odh::TerrainTheme::grass);check(mesh.triangles[0].vertices[0].color==std::array<float,3>{157/255.0f,117/255.0f,65/255.0f});
 (*a.grass)[0]=127;a.levels[0].floors[5]=1;check(odh::terrain_surface(a,camera,odh::TerrainTheme::grass).triangles.size()==6);
 auto& wall=a.levels[0].walls[5];wall.segments=16;wall.top_left_style=1;a.levels[0].floors[5]=255;
 mesh=odh::terrain_surface(a,camera,odh::TerrainTheme::grass);check(mesh.half_tiles==1&&mesh.whole_tiles==3&&mesh.triangles.size()==7);
 check(mesh.room_samples[0]==std::array<std::int32_t,3>{26,26,1}); // bottom half x+y>47 at tile1,1.
 const auto expected=odh::project_tile(camera,{2,1,odh::terrain_corner_altitude(a,2,1)});check(mesh.triangles[0].vertices[0].position==expected);
 wall.segments=32;mesh=odh::terrain_surface(a,camera,odh::TerrainTheme::grass);check(mesh.triangles[0].vertices[0].position==odh::project_tile(camera,{1,1,odh::terrain_corner_altitude(a,1,1)}));
 check(mesh.room_samples[0]==std::array<std::int32_t,3>{26,21,1}); // right half x>y.
 wall.top_left_style=wall.top_left_pattern=0;a.levels[0].floors[5]=0;mesh=odh::terrain_surface(a,camera,odh::TerrainTheme::grass);check(mesh.half_tiles==2&&mesh.triangles.size()==8);
 wall.segments=48;rejects([&]{odh::terrain_surface(a,camera,odh::TerrainTheme::grass);});wall.segments=0;
 a.levels[0].floors[0]=1;a.levels[0].floors[15]=1;check(odh::terrain_surface(a,camera,odh::TerrainTheme::grass).whole_tiles==4);
 for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)(*a.altitude)[y*4+x][1]=static_cast<std::uint8_t>(10+x+2*y);
 mesh=odh::terrain_surface(a,camera,odh::TerrainTheme::grass);
 const auto& n=mesh.triangles[0].vertices[0].normal;check(n[0]<0&&n[1]>0&&n[2]<n[0]);check(std::abs(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]-1)<.00001f);
 for(unsigned type=0;type<8;++type)check(!odh::terrain_surface(a,camera,static_cast<odh::TerrainTheme>(type)).triangles.empty());
 auto sand=odh::terrain_surface(a,camera,odh::TerrainTheme::sand);check(sand.triangles[0].vertices[0].color==std::array<float,3>{80/255.0f,116/255.0f,59/255.0f});
 (*a.grass)[0]=128;sand=odh::terrain_surface(a,camera,odh::TerrainTheme::sand);check(sand.triangles[0].vertices[0].color==std::array<float,3>{181/255.0f,171/255.0f,149/255.0f});
 rejects([&]{odh::terrain_surface(a,camera,static_cast<odh::TerrainTheme>(8));});
 auto bad=a;bad.grass.reset();rejects([&]{odh::terrain_surface(bad,camera,odh::TerrainTheme::grass);});bad=a;bad.altitude.reset();rejects([&]{odh::terrain_surface(bad,camera,odh::TerrainTheme::grass);});bad=a;bad.height=3;rejects([&]{odh::terrain_surface(bad,camera,odh::TerrainTheme::grass);});bad=a;bad.width=bad.height=65;rejects([&]{odh::terrain_surface(bad,camera,odh::TerrainTheme::grass);});bad=a;bad.levels[0].walls.pop_back();rejects([&]{odh::terrain_surface(bad,camera,odh::TerrainTheme::grass);});bad=a;bad.pools=std::vector<std::uint8_t>(15,1);rejects([&]{odh::terrain_surface(bad,camera,odh::TerrainTheme::grass);});
 auto liquid=grid();liquid.pools=std::vector<std::uint8_t>(16,255);liquid.water=std::vector<std::uint8_t>(16,0);
 (*liquid.pools)[5]=1;check(odh::terrain_surface(liquid,camera,odh::TerrainTheme::grass).whole_tiles==3);
 (*liquid.water)[5]=1;check(odh::terrain_surface(liquid,camera,odh::TerrainTheme::grass).whole_tiles==4);
 auto original=*a.grass;odh::terrain_surface(a,camera,odh::TerrainTheme::grass);check(*a.grass==original);
 std::cout<<checks<<" terrain surface checks passed (synthetic only)\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
