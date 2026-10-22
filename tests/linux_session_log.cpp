#include "linux_session_log.hpp"

#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
unsigned checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& path){std::ifstream input(path);return {std::istreambuf_iterator<char>(input),{}};}
}

int main(){try{
    const auto root=std::filesystem::temp_directory_path()/
        ("odh-session-log-test-"+std::to_string(static_cast<long long>(::getpid())));
    std::filesystem::remove_all(root);
    (void)::setenv("ODH_STATE_DIR",root.c_str(),1);
    const auto directory=root/"opendollhouse"/"logs";
    std::filesystem::path log_path;
    {
        odh_host::LinuxSessionLog log;log_path=log.path();
        check(log.enabled()&&log_path.filename()=="odh-person.log","default local log opens under state directory");
        struct stat log_stat{},directory_stat{};
        check(::stat(log_path.c_str(),&log_stat)==0&&(log_stat.st_mode&0777)==0600,
              "log permissions are private to the current user");
        check(::stat(directory.c_str(),&directory_stat)==0&&(directory_stat.st_mode&0777)==0700,
              "log directory permissions are private to the current user");
        log.stage("resource-load");log.event("language=14");
        const auto text=read(log_path);
        check(text.find("session started")!=std::string::npos&&text.find("stage=resource-load")!=std::string::npos,
              "session header and stage are written");
        check(text.find("language=14")!=std::string::npos,"diagnostic event is written");
        const auto child=::fork();
        if(child<0)throw std::runtime_error("fork for concurrent logger test failed");
        if(child==0){odh_host::LinuxSessionLog second;const bool separate=second.enabled()&&second.path()!=log_path;
            second.event("concurrent session");second.finish(0);::_exit(separate?0:1);}
        int child_status=0;
        if(::waitpid(child,&child_status,0)!=child)throw std::runtime_error("waitpid for concurrent logger failed");
        check(WIFEXITED(child_status)&&WEXITSTATUS(child_status)==0,
              "a concurrent process receives its own log instead of rotating the active one");
        log.event("parent session continues");
        check(read(log_path).find("parent session continues")!=std::string::npos,
              "concurrent session leaves the original log attached to the original process");
        log.finish(0);
    }
    std::size_t active_markers=0;
    for(const auto& item:std::filesystem::directory_iterator(directory))
        if(item.path().filename().string().rfind("active-",0)==0)++active_markers;
    check(active_markers==0,"normal finish removes the active marker");

    {
        std::ofstream stale(directory/"active-999999");stale<<"pid=999999\nstage=scene-render\n";
    }
    {
        odh_host::LinuxSessionLog log;
        const auto text=read(log.path());
        check(text.find("previous session ended unexpectedly")!=std::string::npos&&
              text.find("stage=scene-render")!=std::string::npos,
              "next run reports a stale session and its last stage");
        log.finish(0);
    }

    const auto crash_root=root/"crash-state";
    (void)::setenv("ODH_STATE_DIR",crash_root.c_str(),1);
    const auto child=::fork();
    if(child<0)throw std::runtime_error("fork for isolated signal test failed");
    if(child==0){
        odh_host::LinuxSessionLog log;log.stage("scene-render");(void)::raise(SIGSEGV);::_exit(99);
    }
    int status=0;
    if(::waitpid(child,&status,0)!=child)throw std::runtime_error("waitpid for isolated signal test failed");
    check(WIFEXITED(status)&&WEXITSTATUS(status)==128+SIGSEGV,"fatal signal handler exits without unsafe cleanup");
    {
        odh_host::LinuxSessionLog log;
        const auto text=read(log.path());
        const auto rotated=read(log.path().string()+".1");
        check(text.find("previous session ended unexpectedly")!=std::string::npos&&
              text.find("stage=scene-render")!=std::string::npos,
              "restart identifies crash and last recorded stage");
        check(rotated.find("FATAL: process terminated by signal")!=std::string::npos,
              "fatal signal writes a short crash record");
        check(rotated.find('\0')==std::string::npos,"fatal record contains no binary NUL byte");
        log.finish(0);
    }

    const auto post_finish_child=::fork();
    if(post_finish_child<0)throw std::runtime_error("fork for restored signal test failed");
    if(post_finish_child==0){odh_host::LinuxSessionLog log;log.finish(0);(void)::raise(SIGSEGV);::_exit(99);}
    int post_finish_status=0;
    if(::waitpid(post_finish_child,&post_finish_status,0)!=post_finish_child)
        throw std::runtime_error("waitpid for restored signal test failed");
    check(WIFSIGNALED(post_finish_status)&&WTERMSIG(post_finish_status)==SIGSEGV,
          "finishing the logger restores the process signal behavior");

    const auto cap_root=root/"cap-state";
    (void)::setenv("ODH_STATE_DIR",cap_root.c_str(),1);
    {
        odh_host::LinuxSessionLog log;
        const std::string record(2048,'x');
        for(unsigned i=0;i<800;++i)log.event(record);
        check(std::filesystem::file_size(log.path())<=1024U*1024U,
              "one session log is capped at one MiB");
        check(read(log.path()).find("log size limit reached")!=std::string::npos,
              "the log reports when its size limit suppresses more records");
        log.finish(0);
    }
    std::filesystem::remove_all(root);
    std::cout<<checks<<" Linux session log checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Linux session log test: "<<error.what()<<'\n';return 1;}}
