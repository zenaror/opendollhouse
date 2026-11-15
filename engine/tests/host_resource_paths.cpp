/* SPDX-License-Identifier: MPL-2.0 */
#include "odh/host_filesystem.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
namespace fs=std::filesystem;
static bool contained(const fs::path& root,const fs::path& selected) {
    const auto relative=selected.lexically_relative(root);
    if(relative.empty()||odh::hostfs::is_absolute(relative))return false;
    for(const auto& part:relative)if(part=="..")return false;
    return true;
}
int main(int argc,char**argv) {
    assert(argc==2);const auto work=fs::absolute(argv[1])/("host-paths-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(work);
    const auto cwd=fs::current_path();fs::current_path(work);
#if defined(__SWITCH__)
    const fs::path game="sdmc:/switch/OpenDollhouse/game",save="sdmc:/switch/OpenDollhouse/saves/UserData";
#elif defined(__vita__)
    const fs::path game="ux0:/data/OpenDollhouse/game",save="ux0:/data/OpenDollhouse/saves/UserData";
#else
    const fs::path game=work/"game",save=work/"saves/UserData";
#endif
    fs::create_directories(game/"UIGraphics");fs::create_directories(save/"Houses");fs::create_directories(work/"outside");
    std::ofstream(game/"UIGraphics/UIGraphics.far")<<"authored";
    std::ofstream(save/"Neighborhood.iff")<<"profile";
    std::ofstream(save/"Houses/House01.iff")<<"house";
    const auto root=odh::hostfs::canonical(game);
    assert(odh::hostfs::is_absolute(root));
    assert(contained(root,odh::hostfs::canonical(game/"UIGraphics/UIGraphics.far")));
    assert(!contained(root,odh::hostfs::canonical(save/"Neighborhood.iff")));
    assert(odh::hostfs::canonical(save/"Houses/../Neighborhood.iff")==odh::hostfs::canonical(save/"Neighborhood.iff"));
    std::error_code error;
    assert(odh::hostfs::canonical(save/"missing.iff",error).empty()&&error);
    assert(!odh::hostfs::weakly_canonical(save/"future/child",error).empty()&&!error);
    const auto bad_prefix=game.parent_path()/"game-other";fs::create_directories(bad_prefix);
    std::ofstream(bad_prefix/"outside.iff")<<"outside";
    assert(!contained(root,odh::hostfs::canonical(bad_prefix/"outside.iff")));
#if defined(__SWITCH__) || defined(__vita__)
    assert(odh::hostfs::absolute("UserData/Houses",error).empty()&&error);
    assert(odh::hostfs::absolute("other:/escape",error).empty()&&error);
    assert(odh::hostfs::absolute(std::string(odh::hostfs::device_mount())+":/../../escape",error).empty()&&error);
    assert(odh::hostfs::absolute(std::string(odh::hostfs::device_mount())+":/game\\escape",error).empty()&&error);
    const auto normalized=odh::hostfs::absolute(game/"./UIGraphics/../UIGraphics");
    assert(normalized==game/"UIGraphics");
#else
    std::ofstream(work/"outside/file")<<"outside";
    fs::create_symlink(work/"outside/file",game/"escape-link");
    assert(odh::hostfs::canonical(game/"escape-link")==fs::canonical(work/"outside/file"));
    assert(!contained(root,odh::hostfs::canonical(game/"escape-link")));
    assert(fs::is_symlink(odh::hostfs::symlink_status(game/"escape-link")));
    assert(odh::hostfs::absolute("relative")==fs::absolute("relative"));
    assert(odh::hostfs::weakly_canonical(game/"future")==fs::weakly_canonical(game/"future"));
#endif
    assert(fs::is_regular_file(odh::hostfs::symlink_status(game/"UIGraphics/UIGraphics.far")));
    fs::current_path(cwd);fs::remove_all(work);std::cout<<"PASS selected roots, missing paths, containment and host/device contract\n";
}
