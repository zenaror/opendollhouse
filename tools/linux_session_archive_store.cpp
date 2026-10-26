#include "linux_session_archive_store.hpp"
#include "odh/sha256.hpp"

#include <algorithm>
#include <array>
#include <atomic>
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

std::filesystem::path publish_session_archive_immutable(
        const std::filesystem::path& directory, const odh::SessionArchive& archive,
        odh::SessionArchiveLimits limits) {
    const auto bytes = odh::encode_session_archive(archive, limits);
    const auto digest = odh::sha256(bytes.data(), bytes.size());
    constexpr char hex[] = "0123456789abcdef";
    std::string name;
    name.reserve(69);
    for (const auto byte : digest) { name += hex[byte >> 4]; name += hex[byte & 15]; }
    name += ".odhs";
    const auto path = directory / name;
    FileDescriptor parent(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (parent.get() < 0) io_error("cannot open immutable session directory", directory);

    // Exclusive openat creation keeps every operation anchored to the same
    // directory, including when its pathname is concurrently renamed.
    static std::atomic<std::uint64_t> sequence{0};
    std::string temporary_name;
    int temporary_fd = -1;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        temporary_name = ".session-tmp-" + std::to_string(::getpid()) + "-" +
                         std::to_string(sequence.fetch_add(1));
        temporary_fd = ::openat(parent.get(), temporary_name.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
        if (temporary_fd >= 0) break;
        if (errno != EEXIST) io_error("cannot create immutable session temporary file", directory);
    }
    if (temporary_fd < 0) io_error("cannot allocate immutable session temporary file", directory);
    FileDescriptor temporary(temporary_fd);
    bool temporary_exists = true;
    try {
        if (::fchmod(temporary.get(), S_IRUSR | S_IWUSR) != 0)
            io_error("cannot protect immutable session temporary file", directory);
        write_all(temporary.get(), bytes, path);
        sync_file(temporary.get(), path);
        if (::linkat(parent.get(), temporary_name.c_str(), parent.get(), name.c_str(), 0) != 0 && errno != EEXIST)
            io_error("cannot publish immutable session archive", path);
        // Only our temporary name is removed, never the published archive.
        if (::unlinkat(parent.get(), temporary_name.c_str(), 0) != 0)
            io_error("cannot remove immutable session temporary name", directory);
        temporary_exists = false;

        FileDescriptor published(::openat(parent.get(), name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
        if (published.get() < 0) io_error("cannot verify immutable session archive", path);
        struct stat info {};
        if (::fstat(published.get(), &info) != 0) io_error("cannot inspect immutable session archive", path);
        if (!S_ISREG(info.st_mode) || (info.st_mode & 07777) != 0600 ||
            info.st_uid != ::geteuid() || info.st_nlink != 1 || info.st_size < 0 ||
            static_cast<std::uintmax_t>(info.st_size) != bytes.size())
            throw odh::FormatError("immutable session archive is not a matching private regular file");
        std::array<std::uint8_t,4096> block{};
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto wanted = std::min(block.size(), bytes.size() - offset);
            const auto got = ::read(published.get(), block.data(), wanted);
            if (got < 0 && errno == EINTR) continue;
            if (got < 0) io_error("cannot verify immutable session bytes", path);
            if (got == 0 || !std::equal(block.begin(), block.begin() + got, bytes.begin() + offset))
                throw odh::FormatError("immutable session archive bytes do not match its digest name");
            offset += static_cast<std::size_t>(got);
        }
        std::uint8_t extra = 0;
        ssize_t got;
        do { got = ::read(published.get(), &extra, 1); } while (got < 0 && errno == EINTR);
        if (got < 0) io_error("cannot finish immutable session verification", path);
        if (got != 0) throw odh::FormatError("immutable session archive changed during verification");
        sync_file(published.get(), path);
        sync_file(parent.get(), directory);
        return path;
    } catch (...) {
        if (temporary_exists) {
            const int saved_errno = errno;
            (void)::unlinkat(parent.get(), temporary_name.c_str(), 0);
            errno = saved_errno;
        }
        throw;
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
        FileDescriptor parent(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (parent.get() < 0) io_error("cannot open session save directory", directory);
        sync_file(parent.get(), directory);
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
