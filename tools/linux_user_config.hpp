#pragma once

#include <filesystem>
#include <string>

namespace odh_host {

struct LinuxUserConfig {
    int language_code=1;
    std::filesystem::path data_root;
    bool has_saved_settings=false;
};

std::filesystem::path linux_user_config_path();
bool load_linux_user_config(LinuxUserConfig& config,std::string* error=nullptr);
bool save_linux_user_config(const LinuxUserConfig& config,std::string* error=nullptr);

} // namespace odh_host
