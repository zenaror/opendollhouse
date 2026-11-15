/* SPDX-License-Identifier: MPL-2.0 */
// Synthetic resource-resolution contract; no installed game data or real saves.
#include "app/front/NhoodModel.h"
#include "app/ui/OriginalNeighborhoodResources.h"
#include "app/LotSession.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "content/TS1CharacterFiles.h"
using namespace FSO::Content::TS1;
#include "files/formats/iff/IffRawFile.h"
#include "core/io.h"
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace simapp::front;
using namespace FSO::Files::Formats::IFF;
namespace fs = std::filesystem;
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static std::vector<uint8_t> bmp(uint8_t red){
    st::Writer b(st::Endian::Little);b.WriteCString("BM",2);b.WriteUInt32(102);b.WriteUInt32(0);b.WriteUInt32(54);
    b.WriteUInt32(40);b.WriteUInt32(4);b.WriteUInt32(4);b.WriteUInt16(1);b.WriteUInt16(24);b.WriteBytes(std::vector<uint8_t>(24));
    for(int i=0;i<16;i++){b.WriteByte(0);b.WriteByte(0);b.WriteByte(red);}return b.ToArray();
}
static void house(const fs::path& file,uint8_t red){IffRawFile f;f.Set("BMP_",512,bmp(red));f.Set("BMP_",513,bmp(red));f.Set("THMB",512,std::vector<uint8_t>(20));require(f.Save(file.string()),"fixture write");}
static void mapArchive(const fs::path& path){
    constexpr unsigned size=54+800*600*3;st::Writer b(st::Endian::Little);
    b.WriteCString("BM",2);b.WriteUInt32(size);b.WriteUInt32(0);b.WriteUInt32(54);b.WriteUInt32(40);b.WriteUInt32(800);b.WriteUInt32(600);b.WriteUInt16(1);b.WriteUInt16(24);b.WriteBytes(std::vector<uint8_t>(size-30));
    st::Writer f(st::Endian::Little);f.WriteCString("FAR!byAZ",8);f.WriteUInt32(1);f.WriteUInt32(16+size);f.WriteBytes(b.ToArray());f.WriteUInt32(6);
    for(unsigned mode:{1,2,3,4,5,7}){auto name=odh::neighborhood_map_config(mode)->resource;f.WriteUInt32(size);f.WriteUInt32(size);f.WriteUInt32(16);f.WriteUInt32(name.size());f.WriteCString(name,int(name.size()));}
    auto data=f.ToArray();std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());require(bool(out),"map archive write");
}
static void locations(const fs::path& path,int x=120,int y=130){IffRawFile f;
    f.Set("STR#",1,RawIff::MakeSTRFormat3({"1,210,220"}));f.Set("STR#",3,RawIff::MakeSTRFormat3({"40,"+std::to_string(x)+","+std::to_string(y)}));f.Set("STR#",4,RawIff::MakeSTRFormat3({"50,230,240"}));require(f.Save(path.string()),"map table write");}
static std::vector<uint8_t> bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static uint32_t pixels(const simapp::Canvas& c){require(c.Width==4&&c.Height==4,"wrong picture source dimensions");return c.Px[5];}
int main(){auto root=fs::temp_directory_path()/("odh-template-reads-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
    auto user=root/"UserData",other=root/"UserData2",tpl=root/"TemplateVacation";
    for(auto p:{user/"Houses",other/"Houses",tpl})fs::create_directories(p);
    auto content=FSO::Content::Content::Get();content->Neighborhood=std::make_unique<FSO::Content::TS1::TS1NeighborhoodProvider>(content);
    auto& provider=*content->Neighborhood;provider.UserPath=user.string();provider.TemplateDirs={tpl.string()};
    provider.LotMaps[3]={{40,120,130}};
    const auto archive=root/"ui.far";mapArchive(archive);locations(user/"LotLocations.iff");locations(other/"LotLocations.iff",180,190);
    NhoodModel model;model.UserDir=user.string();
    auto inspect=[&](uint8_t color,const fs::path& expected){
        require(model.AreaAvailable(Area::Vacation),"available installed/saved region rejected");model.LoadArea(Area::Vacation);
        require(model.Lots.size()==1&&model.Lots[0].Exists,"map and availability disagree");
        simapp::Canvas open,picture;require(model.OpenPicture(model.Lots[0],open),"hover picture absent");require(NhoodModel::HousePicture(model.UserDir,40,picture),"house picture absent");
        require(pixels(open)==pixels(picture)&&pixels(open)==pixels(model.Lots[0].Thumb),"thumbnail/hover/house picture disagree");
        // The literal fixture's center is red in the native RGBA canvas.
        require((pixels(open)&255)==color,"wrong source won precedence");
        simapp::LotSession entry;std::string error;require(entry.OpenHouse(40,false,error),"loader rejected map destination");require(entry.LoadedHousePath==expected.string(),"map and entry use different source");
        const auto pc=simapp::original::PrepareNeighborhoodMapResources(model,archive,3,1,provider);
        require(pc.visual_mode==3&&pc.marker_scale==1&&pc.markers.size()==1&&pc.markers[0].file_exists,"PC marker ignored resolved template/saved source");
        require(pc.houses.size()==1&&pc.houses[0].closed.pixels[5][0]==color&&pc.houses[0].open&&pc.houses[0].open->pixels[5][0]==color,"PC closed/open artwork ignored source precedence");
        require(odh_host::pick_map_lot(pc,120,130)==std::optional<uint16_t>{40},"PC destination hit and native lot admission disagree");
    };
    house(tpl/"House40.iff",17);auto original=bytes(tpl/"House40.iff");
    fs::remove(user/"Houses");inspect(17,tpl/"House40.iff");require(!fs::exists(user/"Houses"),"PC template read created save Houses directory");fs::create_directories(user/"Houses");require(!fs::exists(user/"Houses/House40.iff"),"read copied a template into the save");
    house(user/"Houses/House40.iff",34);inspect(34,user/"Houses/House40.iff");require(bytes(tpl/"House40.iff")==original,"saved preference modified template");
    require(!model.Unleashed(),"absent Old Town selected an expansion map");
    house(tpl/"House50.iff",68);provider.LotMaps[4]={{50,230,240}};
    require(model.Unleashed(),"installed Old Town template ignored");model.LoadArea(Area::Neighbourhood);
    require(model.Lots.size()==1&&model.Lots[0].House==50&&model.Lots[0].Scale==2,"expanded map definition and resolved Old Town destination disagree");
    const auto pcHome=simapp::original::PrepareNeighborhoodMapResources(model,archive,1,1,provider);
    require(pcHome.visual_mode==4&&pcHome.marker_scale==2&&pcHome.markers.size()==1&&pcHome.markers[0].file_exists&&pcHome.houses.size()==1,"PC expanded map omitted installed Old Town lot/art");
    NhoodModel otherMap;otherMap.UserDir=other.string();require(!otherMap.Unleashed(),"foreign neighbourhood borrowed Old Town template");
    fs::remove(tpl/"House50.iff");require(!model.Unleashed(),"removed Old Town template remained available");
    house(user/"Houses/House50.iff",85);require(model.Unleashed(),"saved Old Town lot ignored");fs::remove(user/"Houses/House50.iff");
    // A different neighbourhood cannot borrow the current provider's saved lot or template.
    NhoodModel isolated;isolated.UserDir=other.string();require(!isolated.AreaAvailable(Area::Vacation),"foreign neighbourhood borrowed provider source");isolated.LoadArea(Area::Vacation);require(isolated.Lots.empty(),"foreign map borrowed provider source");
    simapp::Canvas picture;require(!NhoodModel::HousePicture(other.string(),40,picture),"static reader borrowed current neighbourhood");
    auto foreign=simapp::original::PrepareNeighborhoodMapResources(isolated,archive,3,1,provider);
    require(foreign.markers.size()==1&&!foreign.markers[0].file_exists&&foreign.houses.empty()&&foreign.markers[0].location.x==180,"PC foreign neighbourhood borrowed current provider metadata/house");
    house(other/"Houses/House40.iff",51);require(isolated.AreaAvailable(Area::Vacation),"foreign explicit save ignored");require(NhoodModel::HousePicture(other.string(),40,picture)&&((pixels(picture)&255)==51),"foreign explicit picture ignored");
    foreign=simapp::original::PrepareNeighborhoodMapResources(isolated,archive,3,1,provider);
    require(foreign.markers[0].file_exists&&foreign.houses.size()==1&&foreign.houses[0].closed.pixels[5][0]==51,"PC foreign explicit saved house ignored");
    fs::remove(user/"Houses/House40.iff");fs::remove(tpl/"House40.iff");require(!model.AreaAvailable(Area::Vacation),"truly absent region accepted");model.LoadArea(Area::Vacation);require(model.Lots.empty(),"missing destination retained");require(!NhoodModel::HousePicture(user.string(),40,picture),"missing picture retained");
    auto missing=simapp::original::PrepareNeighborhoodMapResources(model,archive,3,1,provider);
    require(!missing.markers[0].file_exists&&missing.houses.empty()&&!odh_host::pick_map_lot(missing,120,130),"PC truly absent lot remained selectable");
    // The explicit template roots do not authorize a saved symlink escaping Houses.
    fs::create_symlink(other/"Houses/House40.iff",user/"Houses/House40.iff");
    auto escaped=simapp::original::PrepareNeighborhoodMapResources(model,archive,3,1,provider);
    require(!escaped.markers[0].file_exists&&escaped.houses.empty()&&!odh_host::pick_map_lot(escaped,120,130),"PC accepted external symlink destination");
    content->Neighborhood.reset();fs::remove_all(root);std::cout<<"PASS template-only/saved-precedence/absence/foreign-neighbourhood; availability, map, entry and pictures agree; Old Town native/PC decision, complete PC resource adapter, strict external-path rejection; no read writes\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<" (fixture "<<root<<")\n";return 1;}}
