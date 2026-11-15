/* SPDX-License-Identifier: MPL-2.0 */
// Old neighbourhood metadata keeps custom positions while newer installed
// expansion tables supplement it. No commercial data is required.
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "content/TS1LotLocationFiles.h"
#include "neighborhood_resources.hpp"
#include "content/TS1CharacterFiles.h"
#include "files/formats/iff/IffRawFile.h"
#include <filesystem>
#include <chrono>
#include <iostream>
#include <fstream>
#include "app/front/UserDataSetup.h"
using namespace FSO::Content::TS1;
using namespace FSO::Files::Formats::IFF;
static void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
// Synthetic 800x600 map art shared by the FAR entries. No window is opened.
static void WriteMapArchive(const std::filesystem::path& path) {
    constexpr unsigned size = 54 + 800 * 600 * 3;
    st::Writer bmp(st::Endian::Little);
    bmp.WriteCString("BM", 2); bmp.WriteUInt32(size); bmp.WriteUInt32(0); bmp.WriteUInt32(54);
    bmp.WriteUInt32(40); bmp.WriteUInt32(800); bmp.WriteUInt32(600);
    bmp.WriteUInt16(1); bmp.WriteUInt16(24);
    bmp.WriteBytes(std::vector<uint8_t>(size - 30));
    st::Writer far(st::Endian::Little);
    far.WriteCString("FAR!byAZ", 8); far.WriteUInt32(1); far.WriteUInt32(16 + size);
    far.WriteBytes(bmp.ToArray());
    far.WriteUInt32(6);
    for (unsigned mode : {1, 2, 3, 4, 5, 7}) {
        const auto name = odh::neighborhood_map_config(mode)->resource;
        far.WriteUInt32(size); far.WriteUInt32(size); far.WriteUInt32(16);
        far.WriteUInt32(name.size()); far.WriteCString(name, int(name.size()));
    }
    std::ofstream out(path, std::ios::binary);
    const auto& data = far.ToArray();
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    Require(bool(out), "write map FAR");
}

static void CheckPcMapSources(const std::filesystem::path& root, TS1NeighborhoodProvider& provider) {
    namespace fs = std::filesystem;
    const fs::path user = provider.UserPath;
    const fs::path source = root / "install/TemplateMagictown/LotLocations.iff";
    const auto archive = root / "ui.far";
    WriteMapArchive(archive);
    IffRawFile saved;
    const std::array<int,5> houses{1, 21, 40, 55, 81};
    for (uint16_t mode = 1; mode <= 5; ++mode) {
        saved.Set("STR#", mode, RawIff::MakeSTRFormat3({std::to_string(houses[mode-1]) + ",110,127"}));
    }
    saved.Set("STR#", 6, RawIff::MakeSTRFormat3({"filter settings"}));
    // Conflicting tables at lower priority must not add to or replace user rows.
    IffRawFile magic;
    magic.Set("STR#", 5, RawIff::MakeSTRFormat3({"82,710,711"}));
    magic.Set("STR#", 7, RawIff::MakeSTRFormat3({"91,181,132", "99,0,0"}));
    Require(magic.Save(source.string()), "write installed map tables");
    enum Source { Template, User, Absent };
    for (auto scenario : {Template, User, Absent}) {
        if (scenario == User) saved.Set("STR#", 7, RawIff::MakeSTRFormat3({"92,351,241"}));
        if (scenario == Absent) { saved.Remove("STR#", 7); fs::remove(source); }
        Require(saved.Save((user / "LotLocations.iff").string()), "write user map tables");
        provider.InitSpecific(0);
        const auto files = LotLocationFiles(provider);
        auto prepare = [&](unsigned mode) {
            return odh_host::prepare_neighborhood_map_resources(user, archive, mode, 1, {}, files);
        };
        const auto home = prepare(1);
        Require(home.visual_mode == 4 && home.marker_scale == 2 && home.markers.size()==1 &&
                home.markers.front().location.house == 55, "expanded home map lost the user's table/scale");
        for (auto mode : {1,2,3,4,5}) Require(home.region_available[mode], "existing region was disabled");
        odh_host::NeighborhoodMapPresentation view;
        const auto magicButton = odh_host::region_button(3);
        const auto target = view.target_at(home, magicButton.x + 1, magicButton.y + 1);
        Require(home.region_available[7] == (scenario != Absent) &&
                target == (scenario == Absent ? std::nullopt : std::optional<std::size_t>{7}),
                "PC map artwork/table availability and Magic Town click target disagree");
        const auto studio = prepare(5);
        Require(studio.markers.size()==1 && studio.markers[0].location.house == 81 &&
                studio.markers[0].location.x == 110, "user Studio Town table was overwritten");
        if (scenario == Absent) { Require(!provider.LotMaps.count(7), "provider retained obsolete map 7"); continue; }
        const auto region = prepare(7);
        Require(region.visual_mode==7 && region.marker_scale==1 && region.markers.size()==1, "magic geometry/sentinel");
        const auto& marker = region.markers.front().location;
        const auto& native = provider.LotMaps.at(7).front();
        Require(marker.house==(scenario == User ? 92 : 91) && marker.x==(scenario == User ? 351 : 181) &&
                marker.y==(scenario == User ? 241 : 132) && std::get<0>(native)==marker.house &&
                std::get<1>(native)==marker.x && std::get<2>(native)==marker.y, "map source and native coordinates disagree");
    }
    saved.Remove("STR#", 3);
    saved.Remove("STR#", 5);
    Require(saved.Save((user / "LotLocations.iff").string()), "write partial user map");
    IffRawFile base;
    base.Set("STR#", 3, RawIff::MakeSTRFormat3({"41,201,202"}));
    base.Set("STR#", 5, RawIff::MakeSTRFormat3({"83,301,302"}));
    Require(base.Save((root / "install/GameData/LotLocations.iff").string()), "write base map fallback");
    provider.InitSpecific(0);
    const auto files = LotLocationFiles(provider);
    for (unsigned mode : {3,5}) {
        const auto region = odh_host::prepare_neighborhood_map_resources(user, archive, mode, 1, {}, files);
        const auto& spot = region.markers.at(0).location;
        Require(spot.house == (mode == 3 ? 41 : 81) && spot.x == (mode == 3 ? 201 : 235) &&
                std::get<1>(provider.LotMaps.at(mode).at(0)) == spot.x,
                "Vacation/Studio must use the first source with its missing table");
    }

}
int main(){namespace fs=std::filesystem;auto root=fs::temp_directory_path()/("odh-map-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
struct Cleanup{fs::path Root;~Cleanup(){std::error_code ec;fs::remove_all(Root,ec);}}cleanup{root};
try{
 auto install=root/"install",saves=root/"saves";fs::create_directories(saves/"UserData");fs::create_directories(install/"GameData");fs::create_directories(install/"TemplateMagictown");fs::create_directories(install/"TemplateStudiotown");
 IffRawFile hood;hood.Set("FAMI",1,std::vector<uint8_t>(48));Require(hood.Save((saves/"UserData/Neighborhood.iff").string()),"synthetic neighbourhood write failed");
 auto table=[](IffRawFile& f,int id,const char* value){f.Set("STR#",id,RawIff::MakeSTRFormat3({value}));};
 IffRawFile user;table(user,1,"1,123,456");table(user,5,"81,901,902");Require(user.Save((saves/"UserData/LotLocations.iff").string()),"user table write failed");
 IffRawFile magic;table(magic,7,"91,181,132");Require(magic.Save((install/"TemplateMagictown/LotLocations.iff").string()),"magic table write failed");
 IffRawFile studio;table(studio,5,"81,235,174");Require(studio.Save((install/"TemplateStudiotown/LotLocations.iff").string()),"studio table write failed");
 auto content=FSO::Content::Content::Get();content->SimsCompleteDir=install.string();TS1NeighborhoodProvider provider(content);provider.UserDataRoot=saves.string();provider.InitSpecific(0);
 Require(provider.LotMaps.at(7).size()==1 && std::get<0>(provider.LotMaps.at(7)[0])==91,"missing expansion table not loaded from installed template");
 Require(std::get<1>(provider.LotMaps.at(1)[0])==123,"base custom user position overwritten");
 Require(std::get<1>(provider.LotMaps.at(5)[0])==901,"expansion custom user position overwritten");
 Require(provider.TemplateDirs.front()==(install/"TemplateMagictown").string(),"templates searched under saves instead of install");
 // First-run preparation must not replace customized metadata in an unmarked profile.
 auto bytes=[](const fs::path& p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};
 fs::create_directories(install/"UserData");Require(hood.Save((install/"UserData/Neighborhood.iff").string()),"install seed write failed");
 IffRawFile streets;table(streets,2000,"Custom street");Require(streets.Save((saves/"UserData/StreetNames.iff").string()),"street fixture write failed");
 IffRawFile defaultStreets;table(defaultStreets,2000,"Installed default street");Require(defaultStreets.Save((install/"TemplateMagictown/StreetNames.iff").string()),"conflicting street template write failed");
 const auto oldMap=bytes(saves/"UserData/LotLocations.iff"),oldStreet=bytes(saves/"UserData/StreetNames.iff");
 simapp::front::UserDataSetup setup;setup.Configure(install.string(),saves.string());setup.PlanAll();while(setup.Busy())setup.Step(1<<20);
 Require(bytes(saves/"UserData/LotLocations.iff")==oldMap,"unmarked user map overwritten during setup");
 Require(bytes(saves/"UserData/StreetNames.iff")==oldStreet,"unmarked user street names overwritten during setup");
 fs::create_directories(install/"TemplateUserData");Require(hood.Save((install/"TemplateUserData/Neighborhood.iff").string()),"new seed write failed");
 simapp::front::UserDataSetup fresh;fresh.Configure(install.string(),(root/"fresh").string());fresh.PlanNew(0);while(fresh.Busy())fresh.Step(1<<20);
 IffFile freshMap((root/"fresh/UserData/LotLocations.iff").string());Require(freshMap.Get<Chunks::STR>(7)!=nullptr,"new profile did not receive newest template metadata");
 Require(hood.Save((install/"TemplateMagictown/House93.iff").string()),"template house fixture write failed");
 const auto oldHouse=bytes(install/"TemplateMagictown/House93.iff");IffFile house((install/"TemplateMagictown/House93.iff").string());
 Require(!fs::exists(saves/"UserData/Houses"),"opening an existing profile created a missing houses directory");
 // The direct provider write has a prepared destination. Creating it for this
 // explicit save must not be confused with supplementing a profile on open.
 fs::create_directories(saves/"UserData/Houses");
 Require(provider.SaveHouse(93,&house),"private house save failed");
 Require(fs::exists(saves/"UserData/Houses/House93.iff"),"house save wrote outside private profile");
 Require(bytes(install/"TemplateMagictown/House93.iff")==oldHouse,"house save modified installed template");
 CheckPcMapSources(root, provider);
 std::cout<<"expansion metadata: installed fallback and user table overrides passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
