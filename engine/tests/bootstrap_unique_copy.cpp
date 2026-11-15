/* SPDX-License-Identifier: MPL-2.0 */
#include "app/front/UserDataSetup.h"
#include "files/SafeFile.h"
#include <filesystem>
#include <fstream>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <iostream>
#include <chrono>
namespace fs=std::filesystem;
static void write(const fs::path& p,const std::string& s) {fs::create_directories(p.parent_path());std::ofstream(p)<<s;}
static std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv) {
    assert(argc==2);const auto root=fs::absolute(argv[1])/("bootstrap-copy-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(root);
    const auto data=root/"game",saves=root/"saves";
    const std::string names[]{"Neighborhood.iff","LotZoning.iff","LotLocations.iff","StreetNames.iff","Characters/User00001.iff","Houses/House01.iff","PhotoAlbum/album.txt"};
    for(const auto& name:names)write(data/"UserData"/name,"original:"+name);
    simapp::front::UserDataSetup setup;setup.Configure(data.string(),saves.string());setup.PlanAll();assert(setup.Busy());
    const auto before=FSO::Files::SafeFile::Writes;
    while(setup.Step(1u<<20)){}
    const auto writes=FSO::Files::SafeFile::Writes-before;
    assert(writes==8); // Seven unique files plus the completion marker, never fourteen commits.
    for(const auto& name:names)assert(read(saves/"UserData"/name)=="original:"+name);
    const auto complete=read(saves/"UserData/sims_setup.txt");assert(!complete.empty());
    // A completed profile retains an intentionally edited file and deletion.
    write(saves/"UserData/Houses/House01.iff","player edit");fs::remove(saves/"UserData/Characters/User00001.iff");
    setup.Configure(data.string(),saves.string());setup.PlanAll();assert(!setup.Busy());
    assert(read(saves/"UserData/Houses/House01.iff")=="player edit");assert(!fs::exists(saves/"UserData/Characters/User00001.iff"));
    // Resume only the existing durable import intent; keep already copied bytes.
    const auto resume=root/"resume";setup.Configure(data.string(),resume.string());setup.PlanAll();assert(setup.Step(1));
    const auto copied=resume/"UserData/Characters/User00001.iff";assert(fs::exists(copied));write(copied,"kept during resume");
    simapp::front::UserDataSetup next;next.Configure(data.string(),resume.string());next.PlanAll();
    while(next.Step(1u<<20)){}
    assert(read(copied)=="kept during resume");assert(fs::exists(resume/"UserData/sims_setup.txt"));
    assert(!fs::exists(resume/"UserData/sims_bootstrap.txt"));
    fs::remove_all(root);
    std::cout<<"PASS actual bootstrap unique writes, complete-profile edits/deletion and interrupted import resume\n";
}
