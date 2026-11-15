// SPDX-License-Identifier: MPL-2.0
#include "linux_language_setup.hpp"
#include "language_picker.hpp"
#include <chrono>
namespace odh_host {
void LinuxLanguageSetup::load(){
    config=LinuxUserConfig{};diagnostic.clear();
    readable=load_linux_user_config(config,&diagnostic);
    if(!readable)config=LinuxUserConfig{};
}
bool LinuxLanguageSetup::confirm(int language,std::filesystem::path& backup,std::string& error){
    backup.clear();error.clear();
    if(!valid_game_language(language)){error="Invalid language code";return false;}
    // Preserve current unrelated preferences, including changes since startup.
    LinuxUserConfig current;std::string readError;
    if(!load_linux_user_config(current,&readError)){
        if(readable){error="Settings became unreadable after the picker opened; restart to review their recovery.";return false;}
        const auto original=linux_user_config_path();
        if(original.empty()){error=readError;return false;}
        std::error_code ec;
        if(!std::filesystem::is_regular_file(original,ec)||ec){error="Cannot preserve unreadable settings: "+readError;return false;}
        const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
        for(int attempt=0;attempt<100;++attempt){
            auto candidate=original;candidate += ".invalid-"+std::to_string(stamp)+"-"+std::to_string(attempt);
            ec.clear();
            if(std::filesystem::copy_file(original,candidate,std::filesystem::copy_options::none,ec)){backup=candidate;break;}
            if(ec!=std::errc::file_exists){error="Cannot preserve unreadable settings: "+ec.message();return false;}
        }
        if(backup.empty()){error="Cannot reserve a settings backup name";return false;}
        current=LinuxUserConfig{};
    }
    current.language_code=language;current.language_confirmed=true;
    if(!save_linux_user_config(current,&error))return false;
    current.has_saved_settings=true;config=std::move(current);readable=true;diagnostic.clear();return true;
}
}
