// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "linux_user_config.hpp"
namespace odh_host {
// Linux host preference only. Game files and player records are never opened.
struct LinuxLanguageSetup {
    LinuxUserConfig config;
    bool readable=true;
    std::string diagnostic;
    void load();
    bool needs_choice() const { return !readable || !config.has_saved_settings || !config.language_confirmed; }
    // Call only after the visible picker is confirmed. Invalid settings are
    // preserved under an exclusive backup name before replacing the config.
    bool confirm(int language,std::filesystem::path& backup,std::string& error);
};
}
