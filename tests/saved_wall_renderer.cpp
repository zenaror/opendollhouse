#include "saved_wall_renderer.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
int checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("saved wall assertion #"+std::to_string(checks));}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const odh::FormatError&){rejected=true;}check(rejected);}
void set(std::string& b,std::size_t p,std::uint32_t v,unsigned n=4,bool big=false){
    for(unsigned i=0;i<n;++i)b.at(p+i)=static_cast<char>((v>>((big?n-1-i:i)*8))&255);
}
std::string chunk(const std::string& type,std::uint16_t id,const std::string& raw){
    std::string b(76,'\0');b.replace(0,4,type);set(b,4,static_cast<std::uint32_t>(raw.size()+76),4,true);set(b,8,id,2,true);return b+raw;
}
std::string header(){std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');return b;}
std::string spr(bool hole=false,bool empty=false,unsigned frames=4,std::uint16_t palette=7){
    std::string b(12+4*frames,'\0');set(b,0,1000);set(b,4,frames);set(b,8,palette);
    for(unsigned i=0;i<frames;++i){set(b,12+i*4,static_cast<std::uint32_t>(b.size()));
        std::string f(8,'\0');set(f,4,empty?0:1,2);set(f,6,empty?0:1,2);
        if(!empty){if(hole)f+=std::string("\4\4\1\1",4);
            else f+=std::string("\4\6\2\1",4)+std::string(1,static_cast<char>(i))+std::string(1,'\0');}
        f+=std::string("\5\0",2);b+=f;
    }return b;
}
std::string palette(){std::string b(28,'\0');set(b,0,1);set(b,4,4);
    for(unsigned i=0;i<4;++i){b[16+3*i]=static_cast<char>(10+i*50);b[17+3*i]=20;b[18+3*i]=30;}return b;}
std::string objd(){std::string b(216,'\0');set(b,0,138);set(b,18,4,2);set(b,28,123);set(b,92,40,2);set(b,162,5000,2);return b;}
std::string far(const std::string& asset,bool duplicate=false){
    const auto manifest=static_cast<std::uint32_t>(16+asset.size());
    std::string b(16,'\0');b.replace(0,8,"FAR!byAZ");set(b,8,1);set(b,12,manifest);b+=asset;
    b.resize(manifest+4,'\0');set(b,manifest,duplicate?2:1);
    for(unsigned i=0;i<(duplicate?2U:1U);++i){std::string e(16,'\0');set(e,0,static_cast<std::uint32_t>(asset.size()));set(e,4,static_cast<std::uint32_t>(asset.size()));set(e,8,16);set(e,12,8);b+=e+"test.iff";}
    return b;
}
std::string globals(bool missing_palette=false,bool missing_pattern=false,bool mask_hole=false,bool empty=false){
    std::string b=header();if(!missing_palette)b+=chunk("PALT",7,palette());
    for(unsigned zoom=1;zoom<=3;++zoom){
        b+=chunk("SPR#",static_cast<std::uint16_t>(2+(zoom-1)*512),spr(mask_hole));
        for(unsigned p=0;p<10;++p)if(!missing_pattern||p!=2)
            b+=chunk("SPR#",static_cast<std::uint16_t>(1536+(zoom-1)*256+p),spr(false,empty));
    }return b;
}
struct Temp{std::filesystem::path path;Temp(){path=std::filesystem::temp_directory_path()/("odh-walls-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
void write(const std::filesystem::path& path,const std::string& bytes){std::ofstream f(path,std::ios::binary);f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));if(!f)throw std::runtime_error("fixture write failed");}
odh::SavedArchitecture architecture(){odh::SavedArchitecture a;a.width=a.height=3;for(auto& l:a.levels)l.walls.resize(9);a.altitude=std::vector<std::array<std::uint8_t,4>>(9,{0,0,0,0});return a;}
odh::RasterFrame base(){return {128,128,std::vector<std::array<std::uint8_t,4>>(16384,{255,255,255,255}),std::vector<float>(16384,INFINITY),19,7};}
odh::WallAppearanceFace override_face(unsigned x,unsigned y,unsigned side,unsigned raw,std::uint32_t guid=0){
    odh::WallAppearanceStyle style{static_cast<std::uint16_t>(raw),guid,{}};
    if(guid)for(unsigned i=0;i<6;++i)style.sprites[i]=static_cast<std::uint16_t>(5000+i);
    return {x,y,1,static_cast<std::uint8_t>(side),style,11,11,1,1,true,true};
}
}
int main(){try{
    auto a=architecture();auto& wall=a.levels[0].walls[4];wall={15,0,1,1,2,3,4,5};
    a.levels[0].walls[5].segments=1;a.levels[0].walls[5].top_left_style=12;
    a.levels[0].walls[7].segments=2;a.levels[0].walls[7].top_right_style=13;
    odh::WallAppearance appearance;appearance.faces={override_face(1,1,1,40,123),override_face(1,1,2,2),override_face(2,1,1,3),override_face(1,2,2,3)};
    const unsigned patterns[4][2]={{2,3},{4,2},{5,4},{3,5}};
    const unsigned styles[4][2]={{40,2},{13,40},{12,13},{2,12}};
    for(unsigned rot=0;rot<4;++rot){const auto all=saved_wall_faces(a,appearance,static_cast<std::uint8_t>(rot));
        std::vector<SavedWallFace> faces;for(const auto& face:all)if(face.x==1&&face.y==1)faces.push_back(face);
        check(faces.size()==2);
        for(unsigned i=0;i<2;++i){check(faces[i].pattern==patterns[rot][i]&&faces[i].orientation==i&&faces[i].style.raw_style==styles[rot][i]);}
        check(faces[0].x==1&&faces[0].y==1&&faces[1].level==1);
    }
    const auto owned=saved_wall_faces(a,appearance,2);check(owned[0].owner_x==2&&owned[0].owner_y==1&&owned[0].owner_segment==1);
    check(owned[1].owner_x==1&&owned[1].owner_y==2&&owned[1].owner_segment==2);
    wall.top_left_style=2;check(saved_wall_faces(a,appearance,0)[0].style.raw_style==2);
    for(unsigned raw:{3U,22U}){wall.top_left_style=static_cast<std::uint16_t>(raw);
        check(saved_wall_faces(a,appearance,0)[0].style.resource_guid==123&&wall.top_left_style==raw);}
    wall.top_left_style=1;
    auto clear=appearance;clear.faces[0].style={0,0,{}};check(saved_wall_faces(a,clear,0)[0].style.raw_style==1);
    for(unsigned diagonal:{16U,32U})for(unsigned rot=0;rot<4;++rot){wall.segments=static_cast<std::uint8_t>(diagonal);
        const auto face=saved_wall_faces(a,appearance,static_cast<std::uint8_t>(rot))[0];
        const bool flip=diagonal==16?(rot==1||rot==2):(rot==2||rot==3);
        check(face.pattern==(flip?4:5));check(face.orientation==((diagonal==16)!=(rot%2!=0)?2:3));
        check(face.style.raw_style==(rot==0?2:1));
    }
    wall.segments=15;
    auto malformed=a;malformed.levels[1].walls.pop_back();rejects([&]{saved_wall_faces(malformed,appearance,0);});
    malformed=a;malformed.levels[0].walls[0].segments=17;rejects([&]{saved_wall_faces(malformed,appearance,0);});
    malformed=a;malformed.levels[0].walls[0].segments=48;rejects([&]{saved_wall_faces(malformed,appearance,0);});
    rejects([&]{saved_wall_faces(a,appearance,4);});auto duplicates=appearance;duplicates.faces.push_back(duplicates.faces[0]);rejects([&]{saved_wall_faces(a,duplicates,0);});
    duplicates=appearance;duplicates.faces[0].x=3;rejects([&]{saved_wall_faces(a,duplicates,0);});
    duplicates=appearance;duplicates.faces[0].style.resource_guid=0;rejects([&]{saved_wall_faces(a,duplicates,0);});

    Temp tmp;const auto global=tmp.path/"walls.iff",object=tmp.path/"object.iff";
    write(global,globals());std::string asset=header()+chunk("OBJD",1,objd());
    for(unsigned i=0;i<3;++i)asset+=chunk("SPR#",static_cast<std::uint16_t>(5000+i),spr()); // No PALT: opaque-black fallback mask.
    write(object,asset);std::istringstream ob(objd());const auto def=odh::read_object_definition(odh::View(ob,objd().size()));
    odh::DefinitionIndex index({{object.string(),"",1,def}});SavedWallRenderer renderer(global.string(),index);
    odh::WorldProjection camera;camera.width=camera.height=128;camera.center_tile={1,1,0};
    a=architecture();a.levels[0].walls[4]={1,0,1,0,2,0,0,0};appearance.faces={override_face(1,1,1,40,123)};
    auto result=renderer.render(a,appearance,camera,base(),false);
    check(result.draws.size()==1&&result.cached_assets==2&&result.cached_frames==2&&result.cached_pixels==2&&result.cached_frame_bytes==10);
    const auto& draw=result.draws[0];check(draw.pattern_sprite==2050&&draw.mask_sprite==5002&&draw.pattern_palette==7&&draw.mask_palette==7);
    check(!draw.mask_fallback&&!draw.pattern_palette_missing&&draw.mask_palette_missing);
    check(draw.x==0&&draw.y==-175&&draw.width==64&&draw.height==271);
    check(result.composite.shaded_samples==64*96&&result.composite.image.pixels[0]==std::array<std::uint8_t,4>{10,20,30,255});
    check(result.composite.image.tested_samples==19&&result.composite.image.shaded_samples==7);
    const auto again=renderer.render(a,appearance,camera,base(),false);check(again.cached_frames==2&&again.composite.image.pixels==result.composite.image.pixels);
    check(std::abs(draw.back_depth-(-.3f*.7071067811865475f*.8660254037844386f))<.000002f);
    check(std::abs(draw.front_depth_delta+3.6742346141747673f)<.000002f);
    for(unsigned zoom=1;zoom<=3;++zoom){camera.zoom=static_cast<std::uint8_t>(zoom);result=renderer.render(a,appearance,camera,base(),false);
        const unsigned width=16U<<(zoom-1);check(result.draws[0].width==width&&result.draws[0].pattern_sprite==1538+(zoom-1)*256&&result.draws[0].mask_sprite==4999+zoom);
        check(result.draws[0].height==(zoom==1?68:zoom==2?136:271));}
    camera.zoom=3;
    result=renderer.render(a,{},camera,base(),false);check(!result.draws[0].mask_fallback&&result.draws[0].mask_sprite==1026);
    a.levels[0].walls[4].top_left_style=2;result=renderer.render(a,appearance,camera,base(),false);
    check(result.draws[0].mask_fallback&&result.mask_fallbacks==1&&result.draws[0].mask_sprite==1028&&result.draws[0].face.style.resource_guid==0);
    a.levels[0].walls[4].top_left_style=1;
    auto opaque=base();opaque.depth.assign(16384,-100);result=renderer.render(a,{},camera,opaque,false);check(result.composite.shaded_samples==0&&result.composite.occluded_samples==result.composite.tested_samples);
    // Center uses integer mean raw-byte1*10, wraps right/bottom; edges zero.
    (*a.altitude)[4][1]=1;(*a.altitude)[5][1]=2;(*a.altitude)[7][1]=3;(*a.altitude)[8][1]=5;
    result=renderer.render(a,{},camera,base(),true,7);
    const float alt=(27.f-7.f)*3.f/160.f;
    const auto projected=odh::project_tile(camera,{1,1,alt});check(result.draws[0].y==static_cast<std::int32_t>(projected[1]+77-348+32));
    a.levels[0].walls[4].segments=0;a.levels[0].walls[0]={1,0,1,0,2,0,0,0};
    const auto edge=renderer.render(a,{},camera,base(),true,1000);const auto noheight=renderer.render(a,{},camera,base(),false);check(edge.draws[0].y==noheight.draws[0].y);
    a.levels[0].walls[0].segments=0;a.levels[0].walls[8]={1,0,1,0,2,0,0,0};
    (*a.altitude)[8][1]=1;(*a.altitude)[6][1]=2;(*a.altitude)[2][1]=3;(*a.altitude)[0][1]=4;
    const auto wrap=renderer.render(a,{},camera,base(),true);const auto wrapped_origin=odh::project_tile(camera,{2,2,25.f*3.f/160.f});check(wrap.draws[0].y==static_cast<std::int32_t>(wrapped_origin[1]+77-348+32));
    a=architecture();a.levels[0].walls[4]={1,0,1,0,2,0,0,0};
    auto wrong_camera=camera;wrong_camera.width=64;rejects([&]{renderer.render(a,{},wrong_camera,base(),false);});
    a.altitude.reset();rejects([&]{renderer.render(a,{},camera,base(),true);});
    a.levels[0].walls[4].top_left_pattern=256;rejects([&]{renderer.render(a,{},camera,base(),false);});
    a.levels[0].walls[4].top_left_pattern=2;
    odh::DefinitionIndex absent({});SavedWallRenderer missing_index(global.string(),absent);rejects([&]{missing_index.render(a,appearance,camera,base(),false);});
    odh::DefinitionIndex ambiguous({{object.string(),"",1,def},{object.string(),"",1,def}});SavedWallRenderer ambiguous_renderer(global.string(),ambiguous);rejects([&]{ambiguous_renderer.render(a,appearance,camera,base(),false);});
    for(unsigned mode=0;mode<3;++mode){const auto path=tmp.path/("object"+std::to_string(mode)+".far");write(path,far(asset,mode==1));
        odh::DefinitionIndex defs({{path.string(),mode==2?"absent.iff":"test.iff",1,def}});SavedWallRenderer archive(global.string(),defs);
        if(mode)rejects([&]{archive.render(a,appearance,camera,base(),false);});
        else{const auto first=archive.render(a,appearance,camera,base(),false);check(first.draws[0].mask_asset=="test.iff"&&first.draws[0].mask_source==path.string()&&first.cached_assets==2);check(archive.render(a,appearance,camera,base(),false).composite.image.pixels==first.composite.image.pixels);}
    }
    {const auto path=tmp.path/"changed.iff";auto changed=asset;set(changed,64+76+28,999);write(path,changed);
        odh::DefinitionIndex defs({{path.string(),"",1,def}});SavedWallRenderer changed_renderer(global.string(),defs);rejects([&]{changed_renderer.render(a,appearance,camera,base(),false);});}
    {auto binding=appearance;binding.faces[0].style.sprites[2]=5000;rejects([&]{renderer.render(a,binding,camera,base(),false);});}
    {auto repeated=architecture();repeated.width=repeated.height=32;
        for(auto& level:repeated.levels)level.walls.assign(1024,{});
        for(auto& tile:repeated.levels[0].walls)tile={1,0,1,0,2,0,0,0};
        auto offscreen=camera;offscreen.center_tile={5000,5000,0};
        rejects([&]{renderer.render(repeated,{},offscreen,base(),false);});
        check(repeated.levels[0].walls[0].top_left_style==1&&repeated.levels[0].walls[0].top_left_pattern==2);}
    for(unsigned mode=0;mode<6;++mode){const auto path=tmp.path/("variant"+std::to_string(mode)+".iff");auto content=globals(mode==0,mode==1,mode==2,mode==3);
        if(mode==4)content+=chunk("SPR#",2050,spr());
        if(mode==5){content=header()+chunk("PALT",7,palette())+chunk("SPR#",2050,spr(false,false,1));}
        write(path,content);SavedWallRenderer variant(path.string(),absent);
        if(mode==1||mode==3||mode==4)rejects([&]{variant.render(a,{},camera,base(),false);});
        else if(mode==0){const auto black=variant.render(a,{},camera,base(),false);check(black.draws[0].pattern_palette_missing&&black.draws[0].mask_palette_missing&&!black.draws[0].mask_fallback);check(black.composite.image.pixels[0]==std::array<std::uint8_t,4>{0,0,0,255});}
        else if(mode==2){const auto hole=variant.render(a,{},camera,base(),false);check(hole.composite.shaded_samples==0&&hole.composite.transparent_samples==hole.composite.tested_samples);check(std::isinf(hole.composite.image.depth[0]));}
        else{auto diagonal=a;diagonal.levels[0].walls[4].segments=16;diagonal.levels[0].walls[4].bottom_right_pattern=2;rejects([&]{variant.render(diagonal,{},camera,base(),false);});}
    }
    SavedWallRenderer moved(std::move(renderer));rejects([&]{renderer.render(a,{},camera,base(),false);});check(moved.render(a,{},camera,base(),false).draws.size()==1);
    a.levels[0].walls[4].segments=0;check(moved.render(a,{},camera,base(),false).composite.image.pixels==base().pixels);
    std::cout<<checks<<" saved wall renderer checks passed (synthetic only)\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
