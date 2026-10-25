#include "linux_session_log.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef ODH_BUILD_VERSION
#define ODH_BUILD_VERSION "unknown"
#endif
#ifndef ODH_BUILD_REVISION
#define ODH_BUILD_REVISION "unknown"
#endif

namespace odh_host {
namespace {
constexpr std::size_t max_log_bytes=1024U*1024U;
constexpr std::size_t reserved_crash_bytes=64;
volatile std::sig_atomic_t signal_log_fd=-1;
constexpr std::array<int,5> fatal_signals={SIGABRT,SIGBUS,SIGFPE,SIGILL,SIGSEGV};
std::array<struct sigaction,5> previous_signal_actions{};
std::size_t installed_signal_count=0;

void fatal_signal_handler(int signal) {
    const int fd=signal_log_fd;
    if(fd>=0) {
        constexpr char message[]="FATAL: process terminated by signal\n";
        const auto written=::write(fd,message,sizeof(message)-1);
        (void)written;
    }
    ::_exit(128+signal);
}

std::filesystem::path log_directory() {
    const char* state=std::getenv("ODH_STATE_DIR");
    if(state&&*state)return std::filesystem::path(state)/"opendollhouse"/"logs";
    const char* xdg=std::getenv("XDG_STATE_HOME");
    if(xdg&&*xdg)return std::filesystem::path(xdg)/"opendollhouse"/"logs";
    const char* home=std::getenv("HOME");
    if(home&&*home)return std::filesystem::path(home)/".local"/"state"/"opendollhouse"/"logs";
    return std::filesystem::temp_directory_path()/"opendollhouse"/"logs";
}

std::string timestamp() {
    const auto now=std::time(nullptr);
    std::tm value{};
    if(!::gmtime_r(&now,&value))return "time-unavailable";
    std::array<char,32> buffer{};
    if(std::strftime(buffer.data(),buffer.size(),"%Y-%m-%dT%H:%M:%SZ",&value)==0)
        return "time-unavailable";
    return buffer.data();
}

void rotate_logs(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path.string()+".3",error);error.clear();
    if(std::filesystem::exists(path.string()+".2",error)&&!error)
        std::filesystem::rename(path.string()+".2",path.string()+".3",error);
    error.clear();
    if(std::filesystem::exists(path.string()+".1",error)&&!error)
        std::filesystem::rename(path.string()+".1",path.string()+".2",error);
    error.clear();
    if(std::filesystem::exists(path,error)&&!error)
        std::filesystem::rename(path,path.string()+".1",error);
}

void restore_fatal_handlers() noexcept {
    signal_log_fd=-1;
    for(std::size_t i=0;i<installed_signal_count;++i)
        (void)::sigaction(fatal_signals[i],&previous_signal_actions[i],nullptr);
    installed_signal_count=0;
}

bool process_alive(pid_t pid) {
    if(pid<=0)return false;
    if(::kill(pid,0)==0)return true;
    return errno==EPERM;
}

std::string clean(std::string_view value) {
    const auto length=std::min<std::size_t>(value.size(),2048);
    std::string result;result.reserve(length);
    for(std::size_t i=0;i<length;++i) {
        const auto byte=static_cast<unsigned char>(value[i]);
        if(byte=='\n'||byte=='\r'||byte=='\t')result.push_back(' ');
        else if(byte>=32)result.push_back(static_cast<char>(byte));
    }
    return result;
}

std::string bounded_stage_name(std::string_view value) {
    constexpr std::size_t maximum_stage_bytes=48;
    std::string result;
    result.reserve(std::min(value.size(),maximum_stage_bytes));
    for(const auto character:value) {
        if(result.size()==maximum_stage_bytes)break;
        const auto byte=static_cast<unsigned char>(character);
        const bool safe=(byte>='a'&&byte<='z')||(byte>='A'&&byte<='Z')||
            (byte>='0'&&byte<='9')||byte=='-'||byte=='_';
        result.push_back(safe?static_cast<char>(byte):'_');
    }
    if(result.empty())result="unknown";
    return result;
}
}

LinuxSessionLog::LinuxSessionLog() {
    try {
        const auto directory=log_directory();
        std::filesystem::create_directories(directory);
        std::error_code permission_error;
        std::filesystem::permissions(directory,std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace,permission_error);
        if(permission_error)return;
        log_path_=directory/"odh-person.log";
        const auto lock_path=directory/"odh-person.lock";
        lock_fd_=::open(lock_path.c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);
        if(lock_fd_>=0&&::fchmod(lock_fd_,0600)!=0){::close(lock_fd_);lock_fd_=-1;return;}
        if(lock_fd_>=0&&::flock(lock_fd_,LOCK_EX|LOCK_NB)==0) {
            rotate_logs(log_path_);
        } else {
            if(lock_fd_>=0){::close(lock_fd_);lock_fd_=-1;}
            const auto session_id=std::to_string(static_cast<long long>(::getpid()))+"-"+
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            log_path_=directory/("odh-person-"+session_id+".log");
        }
        log_fd_=::open(log_path_.c_str(),O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);
        if(log_fd_<0)return;
        if(::fchmod(log_fd_,0600)!=0)return;

        for(const auto& item:std::filesystem::directory_iterator(directory)) {
            const auto name=item.path().filename().string();
            if(name.rfind("active-",0)!=0)continue;
            const auto contents=item.path();
            const int marker=::open(contents.c_str(),O_RDONLY|O_CLOEXEC);
            if(marker<0)continue;
            std::array<char,512> buffer{};
            const auto count=::read(marker,buffer.data(),buffer.size()-1);
            ::close(marker);
            if(count<=0)continue;
            const std::string text(buffer.data(),static_cast<std::size_t>(count));
            const auto pid_position=text.find("pid=");
            const auto stage_position=text.find("stage=");
            if(pid_position==std::string::npos)continue;
            const auto pid_start=pid_position+4;
            char* end=nullptr;
            const auto pid_value=std::strtol(text.c_str()+pid_start,&end,10);
            if(end==text.c_str()+pid_start||pid_value<=0||pid_value>std::numeric_limits<pid_t>::max())continue;
            if(process_alive(static_cast<pid_t>(pid_value)))continue;
            std::string prior="previous session ended unexpectedly pid="+std::to_string(pid_value);
            if(stage_position!=std::string::npos) {
                auto stage_end=text.find('\n',stage_position);
                if(stage_end==std::string::npos)stage_end=text.size();
                prior+=" "+text.substr(stage_position,stage_end-stage_position);
            }
            append(prior);
            std::error_code ignored;std::filesystem::remove(contents,ignored);
        }

        const auto session_id=std::to_string(static_cast<long long>(::getpid()))+"-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        marker_path_=directory/("active-"+session_id);
        marker_fd_=::open(marker_path_.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
        if(marker_fd_<0)return;
        stage("startup");
        signal_log_fd=static_cast<std::sig_atomic_t>(log_fd_);
        struct sigaction action{};action.sa_handler=fatal_signal_handler;sigemptyset(&action.sa_mask);
        action.sa_flags=SA_RESETHAND;
        installed_signal_count=0;
        for(std::size_t i=0;i<fatal_signals.size();++i) {
            if(::sigaction(fatal_signals[i],&action,&previous_signal_actions[i])!=0)break;
            ++installed_signal_count;
        }
        event(std::string("session started; build version ")+ODH_BUILD_VERSION+
              " revision "+ODH_BUILD_REVISION);
    } catch(...) {
        if(log_fd_>=0){::close(log_fd_);log_fd_=-1;}
        if(marker_fd_>=0){::close(marker_fd_);marker_fd_=-1;}
    }
}

LinuxSessionLog::~LinuxSessionLog() {
    if(signal_log_fd==log_fd_)restore_fatal_handlers();
    if(log_fd_>=0)::close(log_fd_);
    if(marker_fd_>=0)::close(marker_fd_);
    if(lock_fd_>=0){(void)::flock(lock_fd_,LOCK_UN);::close(lock_fd_);}
}

bool LinuxSessionLog::enabled() const noexcept {return log_fd_>=0&&marker_fd_>=0;}
const std::filesystem::path& LinuxSessionLog::path() const noexcept {return log_path_;}

void LinuxSessionLog::append(std::string_view message) noexcept {
    if(log_fd_<0||limit_reported_)return;
    const auto text=clean(message);
    const auto line=timestamp()+" "+text+"\n";
    struct stat info{};
    if(::fstat(log_fd_,&info)!=0||info.st_size<0||
       static_cast<std::uint64_t>(info.st_size)+line.size()>max_log_bytes-reserved_crash_bytes) {
        constexpr char limit[]="log size limit reached; further records suppressed\n";
        if(!limit_reported_&&::write(log_fd_,limit,sizeof(limit)-1)>=0)limit_reported_=true;
        return;
    }
    std::size_t offset=0;
    while(offset<line.size()) {
        const auto written=::write(log_fd_,line.data()+offset,line.size()-offset);
        if(written<0){if(errno==EINTR)continue;return;}
        offset+=static_cast<std::size_t>(written);
    }
}

void LinuxSessionLog::stage(std::string_view name) {
    if(!enabled())return;
    const auto bounded=bounded_stage_name(name);
    const auto content="pid="+std::to_string(static_cast<long long>(::getpid()))+
        "\nstage="+bounded+"\n";
    if(::ftruncate(marker_fd_,0)==0&&::lseek(marker_fd_,0,SEEK_SET)>=0) {
        const auto written=::write(marker_fd_,content.data(),content.size());
        (void)written;
    }
    event("stage="+bounded);
}

void LinuxSessionLog::event(std::string_view message) {append(message);}

void LinuxSessionLog::finish(int exit_code) {
    if(finished_)return;
    event("session ended exit_code="+std::to_string(exit_code));
    if(signal_log_fd==log_fd_)restore_fatal_handlers();
    if(marker_fd_>=0){::close(marker_fd_);marker_fd_=-1;}
    if(!marker_path_.empty()){std::error_code ignored;std::filesystem::remove(marker_path_,ignored);}
    if(lock_fd_>=0){(void)::flock(lock_fd_,LOCK_UN);::close(lock_fd_);lock_fd_=-1;}
    finished_=true;
}

} // namespace odh_host
