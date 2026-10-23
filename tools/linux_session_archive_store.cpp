#include "linux_session_archive_store.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace odh_host {
namespace {
class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor() { if (value_ >= 0) ::close(value_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const noexcept { return value_; }
    int release() noexcept { const int value = value_; value_ = -1; return value; }
private:
    int value_;
};

[[noreturn]] void io_error(const char* operation, const std::filesystem::path& path) {
    throw std::runtime_error(std::string(operation) + " " + path.string() + ": " +
                             std::strerror(errno));
}

void write_all(int fd, const std::vector<std::uint8_t>& bytes,
               const std::filesystem::path& path) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto written = ::write(fd, bytes.data() + offset, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            io_error("cannot write session save", path);
        }
        if (written == 0) {
            errno = EIO;
            io_error("cannot finish session save", path);
        }
        offset += static_cast<std::size_t>(written);
    }
}

void sync_file(int fd, const std::filesystem::path& path) {
    while (::fsync(fd) != 0) {
        if (errno == EINTR) continue;
        io_error("cannot flush session save", path);
    }
}
}

void write_session_archive_atomic(
        const std::filesystem::path& path, const odh::SessionArchive& archive,
        odh::SessionArchiveLimits limits) {
    const auto bytes = odh::encode_session_archive(archive, limits);
    if (path.empty() || path.filename().empty())
        throw std::invalid_argument("session save path must name a file");
    auto directory = path.parent_path();
    if (directory.empty()) directory = ".";

    std::string pattern = (directory / (path.filename().string() + ".tmp.XXXXXX")).string();
    std::vector<char> temporary_name(pattern.begin(), pattern.end());
    temporary_name.push_back('\0');
    FileDescriptor temporary(::mkstemp(temporary_name.data()));
    if (temporary.get() < 0) io_error("cannot create temporary session save beside", path);
    const std::filesystem::path temporary_path(temporary_name.data());
    bool renamed = false;
    try {
        if (::fchmod(temporary.get(), S_IRUSR | S_IWUSR) != 0)
            io_error("cannot protect temporary session save", temporary_path);
        write_all(temporary.get(), bytes, temporary_path);
        sync_file(temporary.get(), temporary_path);
        const auto fd = temporary.release();
        if (::close(fd) != 0) io_error("cannot close temporary session save", temporary_path);
        if (::rename(temporary_path.c_str(), path.c_str()) != 0)
            io_error("cannot atomically replace session save", path);
        renamed = true;
    } catch (...) {
        if (!renamed) {
            const int saved_errno = errno;
            ::unlink(temporary_path.c_str());
            errno = saved_errno;
        }
        throw;
    }
}

odh::SessionArchive read_session_archive(
        const std::filesystem::path& path, odh::SessionArchiveLimits limits) {
    FileDescriptor file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (file.get() < 0) io_error("cannot open session save", path);
    struct stat info {};
    if (::fstat(file.get(), &info) != 0) io_error("cannot inspect session save", path);
    if (!S_ISREG(info.st_mode) || info.st_size < 0)
        throw odh::FormatError("session save is not a regular file");
    if (static_cast<std::uintmax_t>(info.st_size) > limits.max_file_bytes)
        throw odh::FormatError("session save exceeds file size limit");

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto got = ::read(file.get(), bytes.data() + offset, bytes.size() - offset);
        if (got < 0) {
            if (errno == EINTR) continue;
            io_error("cannot read session save", path);
        }
        if (got == 0) throw odh::FormatError("session save changed or was truncated while reading");
        offset += static_cast<std::size_t>(got);
    }
    std::uint8_t extra = 0;
    ssize_t got = 0;
    do { got = ::read(file.get(), &extra, 1); } while (got < 0 && errno == EINTR);
    if (got < 0) io_error("cannot finish reading session save", path);
    if (got != 0) throw odh::FormatError("session save changed while reading");
    return odh::decode_session_archive(bytes, limits);
}

} // namespace odh_host
