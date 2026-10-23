#include "linux_user_config.hpp"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace odh_host {
namespace {
void set_error(std::string* error,const std::string& message) {if(error)*error=message;}

std::string encode_path(const std::filesystem::path& path) {
    static constexpr char digits[]="0123456789abcdef";
    const auto value=path.string();std::string result;result.reserve(value.size()*2);
    for(const unsigned char byte:value){result.push_back(digits[byte>>4]);result.push_back(digits[byte&15]);}
    return result;
}

bool decode_path(std::string_view value,std::filesystem::path& path) {
    if(value.size()%2!=0)return false;
    std::string decoded;decoded.reserve(value.size());
    const auto digit=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1;};
    for(std::size_t i=0;i<value.size();i+=2){const int hi=digit(value[i]),lo=digit(value[i+1]);
        if(hi<0||lo<0)return false;
        decoded.push_back(static_cast<char>((hi<<4)|lo));}
    path=std::filesystem::path(decoded);return true;
}

bool write_all(int fd,std::string_view value) {
    std::size_t offset=0;
    while(offset<value.size()){const auto count=::write(fd,value.data()+offset,value.size()-offset);
        if(count<0){if(errno==EINTR)continue;return false;}if(count==0)return false;offset+=static_cast<std::size_t>(count);}
    return true;
}
}

std::filesystem::path linux_user_config_path() {
    const char* xdg=std::getenv("XDG_CONFIG_HOME");
    if(xdg&&*xdg)return std::filesystem::path(xdg)/"opendollhouse"/"config";
    const char* home=std::getenv("HOME");
    if(home&&*home)return std::filesystem::path(home)/".config"/"opendollhouse"/"config";
    return {};
}

bool load_linux_user_config(LinuxUserConfig& config,std::string* error) {
    const auto path=linux_user_config_path();
    if(path.empty()){set_error(error,"HOME and XDG_CONFIG_HOME are unset");return false;}
    std::ifstream input(path);
    if(!input){if(errno==ENOENT||!std::filesystem::exists(path)) {config=LinuxUserConfig{};return true;}
        set_error(error,"cannot open user config");return false;}
    LinuxUserConfig loaded;std::string line;bool have_language=false;
    while(std::getline(input,line)){
        if(line.rfind("language=",0)==0){const auto code=line.substr(9);char* end=nullptr;
            errno=0;const long value=std::strtol(code.c_str(),&end,10);
            if(errno||end==code.c_str()||*end!='\0'||value<1||value>20){set_error(error,"invalid language code");return false;}
            loaded.language_code=static_cast<int>(value);have_language=true;
        } else if(line.rfind("data_root_hex=",0)==0){
            if(!decode_path(std::string_view(line).substr(14),loaded.data_root)){set_error(error,"invalid data root encoding");return false;}
        }
    }
    if(!input.eof()){set_error(error,"error reading user config");return false;}
    if(!have_language){set_error(error,"user config has no language code");return false;}
    loaded.has_saved_settings=true;
    config=std::move(loaded);if(error)error->clear();return true;
}

bool save_linux_user_config(const LinuxUserConfig& config,std::string* error) {
    if(config.language_code<1||config.language_code>20){set_error(error,"language code must be between 1 and 20");return false;}
    const auto path=linux_user_config_path();
    if(path.empty()){set_error(error,"HOME and XDG_CONFIG_HOME are unset");return false;}
    const auto directory=path.parent_path();std::error_code ec;
    std::filesystem::create_directories(directory,ec);
    if(ec){set_error(error,"cannot create user config directory: "+ec.message());return false;}
    if(::chmod(directory.c_str(),0700)!=0){set_error(error,"cannot secure user config directory");return false;}
    const std::string contents="language="+std::to_string(config.language_code)+"\ndata_root_hex="+
        encode_path(config.data_root)+"\n";
    std::string pattern=(directory/".config-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end());name.push_back('\0');
    const int fd=::mkstemp(name.data());
    if(fd<0){set_error(error,"cannot create temporary user config");return false;}
    bool ok=::fchmod(fd,0600)==0&&write_all(fd,contents)&&::fsync(fd)==0;
    const int close_result=::close(fd);if(close_result!=0)ok=false;
    if(ok&&::rename(name.data(),path.c_str())!=0)ok=false;
    if(ok){const int dirfd=::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        if(dirfd>=0){if(::fsync(dirfd)!=0)ok=false;::close(dirfd);}}
    if(!ok){const int saved=errno;(void)::unlink(name.data());set_error(error,"cannot atomically write user config: "+std::error_code(saved,std::generic_category()).message());return false;}
    if(error)error->clear();
    return true;
}

} // namespace odh_host
