#pragma once

#include <filesystem>
#include <string_view>

namespace odh_host {

// Linux-side session log. It writes bounded, local diagnostics and leaves a
// small active marker so the next run can report an unexpected termination.
class LinuxSessionLog {
public:
    LinuxSessionLog();
    ~LinuxSessionLog();
    LinuxSessionLog(const LinuxSessionLog&)=delete;
    LinuxSessionLog& operator=(const LinuxSessionLog&)=delete;

    bool enabled() const noexcept;
    const std::filesystem::path& path() const noexcept;
    void stage(std::string_view name);
    void event(std::string_view message);
    void finish(int exit_code);

private:
    std::filesystem::path log_path_,marker_path_;
    int log_fd_=-1,marker_fd_=-1,lock_fd_=-1;
    bool finished_=false,limit_reported_=false;
    void append(std::string_view message) noexcept;
};

} // namespace odh_host
