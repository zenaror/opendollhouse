#pragma once

#include "neighborhood_storage.hpp"
#include "player_preferences.hpp"
#include <filesystem>
#include <string>

namespace odh_host {

using LinuxNeighborhoodStorage = NeighborhoodStorage;

struct LinuxUserConfig {
    int language_code=1;
    std::filesystem::path data_root;
    bool has_saved_settings=false;
    unsigned neighborhood_number=1;
    PlayerPreferences preferences;
};

std::filesystem::path linux_user_config_path();
// Player-generated character resources live outside the installed game tree.
// An empty path means the platform has no usable user data home.
std::filesystem::path linux_player_character_directory(LinuxNeighborhoodStorage storage = {});
bool load_linux_user_config(LinuxUserConfig& config,std::string* error=nullptr);
bool save_linux_user_config(const LinuxUserConfig& config,std::string* error=nullptr);

} // namespace odh_host
