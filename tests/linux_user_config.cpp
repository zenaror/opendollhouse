#include "linux_user_config.hpp"

#include <filesystem>
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
    check(odh_host::load_linux_user_config(config,&error)&&config.language_code==1&&config.data_root.empty(),
          "missing config yields first supported language and empty data root");
    config.language_code=20;config.data_root="/copied/game\nroot";
    check(odh_host::save_linux_user_config(config,&error),"settings save successfully");
    struct stat file_stat{},dir_stat{};
    check(::stat(path.c_str(),&file_stat)==0&&(file_stat.st_mode&0777)==0600,"config file is private");
    check(::stat(path.parent_path().c_str(),&dir_stat)==0&&(dir_stat.st_mode&0777)==0700,
          "application config directory is private");
    odh_host::LinuxUserConfig restored;
    check(odh_host::load_linux_user_config(restored,&error)&&restored.language_code==20&&
          restored.data_root==config.data_root,"language and arbitrary path round-trip");
    config.language_code=21;
    check(!odh_host::save_linux_user_config(config,&error),"out-of-range language is rejected");
    check(odh_host::load_linux_user_config(restored,&error)&&restored.language_code==20,
          "rejected save leaves previous config intact");
    (void)::unsetenv("XDG_CONFIG_HOME");(void)::setenv("HOME",root.c_str(),1);
    check(odh_host::linux_user_config_path()==root/".config"/"opendollhouse"/"config",
          "HOME config fallback follows XDG convention");
    std::filesystem::remove_all(root);
    std::cout<<checks<<" Linux user config checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Linux user config test: "<<error.what()<<'\n';return 1;}}
