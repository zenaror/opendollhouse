#include "linux_user_config.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
unsigned checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
}

int main(){try{
    const auto root=std::filesystem::temp_directory_path()/
        ("odh-user-config-test-"+std::to_string(static_cast<long long>(::getpid())));
    std::filesystem::remove_all(root);const auto xdg=root/"xdg";
    (void)::setenv("XDG_CONFIG_HOME",xdg.c_str(),1);
    (void)::setenv("HOME",(root/"home-unused").c_str(),1);
    const auto path=odh_host::linux_user_config_path();
    check(path==xdg/"opendollhouse"/"config","XDG config home takes precedence");
    odh_host::LinuxUserConfig config;std::string error;
    check(odh_host::load_linux_user_config(config,&error)&&config.language_code==1&&config.data_root.empty()&&config.neighborhood_number==1,
          "missing config yields first supported language and empty data root");
    check(!config.has_saved_settings,"missing config is recognized as first launch");
    config.language_code=20;config.neighborhood_number=8;config.data_root="/copied/game\nroot";
    config.preferences={false,false,false};
    check(odh_host::save_linux_user_config(config,&error),"settings save successfully");
    struct stat file_stat{},dir_stat{};
    check(::stat(path.c_str(),&file_stat)==0&&(file_stat.st_mode&0777)==0600,"config file is private");
    check(::stat(path.parent_path().c_str(),&dir_stat)==0&&(dir_stat.st_mode&0777)==0700,
          "application config directory is private");
    odh_host::LinuxUserConfig restored;
    check(odh_host::load_linux_user_config(restored,&error)&&restored.language_code==20&&
          restored.data_root==config.data_root&&restored.has_saved_settings&&restored.neighborhood_number==8,
          "language and arbitrary path round-trip and mark settings initialized");
    check(!restored.preferences.lighting&&!restored.preferences.auto_center&&!restored.preferences.edge_scrolling,
          "presentation preferences round trip without changing installation or neighborhood");
    config.language_code=21;
    check(!odh_host::save_linux_user_config(config,&error),"out-of-range language is rejected");
    check(odh_host::load_linux_user_config(restored,&error)&&restored.language_code==20,
          "rejected save leaves previous config intact");
    config.language_code=20;
    for(const unsigned invalid:{0U,9U}){
        config.neighborhood_number=invalid;
        check(!odh_host::save_linux_user_config(config,&error),"invalid neighborhood cannot overwrite settings");
        check(odh_host::load_linux_user_config(restored,&error)&&restored.neighborhood_number==8,
            "rejected selection preserves last saved neighborhood");
    }
    {std::ofstream file(path);file<<"language=20\ndata_root_hex=\n";}
    check(odh_host::load_linux_user_config(restored,&error)&&restored.neighborhood_number==1&&restored.has_saved_settings,
        "legacy config without a neighborhood retains settings and selects town one");
    check(restored.preferences.lighting&&restored.preferences.auto_center&&restored.preferences.edge_scrolling,
          "old configs retain native presentation defaults");
    for(const auto* invalid:{"lighting=2","auto_center=yes","edge_scrolling=-1","lighting=0\nlighting=1"}){
        {std::ofstream file(path);file<<"language=20\n"<<invalid<<'\n';}
        check(!odh_host::load_linux_user_config(restored,&error)&&restored.preferences.lighting,
            "invalid or duplicated presentation preferences do not replace accepted settings");
    }
    for(const std::string invalid:{"","0","9","03","2x","3\nneighborhood=4"}){
        {std::ofstream file(path);file<<"language=20\nneighborhood="<<invalid<<'\n';}
        check(!odh_host::load_linux_user_config(restored,&error)&&restored.neighborhood_number==1,
            "malformed or duplicate neighborhood preferences do not replace previous settings");
    }
    (void)::unsetenv("XDG_CONFIG_HOME");(void)::setenv("HOME",root.c_str(),1);
    check(odh_host::linux_user_config_path()==root/".config"/"opendollhouse"/"config",
          "HOME config fallback follows XDG convention");
    std::filesystem::remove_all(root);
    std::cout<<checks<<" Linux user config checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Linux user config test: "<<error.what()<<'\n';return 1;}}
