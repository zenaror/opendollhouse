#include "linux_family_overlay_store.hpp"
#include "linux_user_config.hpp"
#include "odh/sha256.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <vector>

namespace odh_host {
namespace {
constexpr std::size_t max_overlay_bytes = 44 + 48 * 4096;

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
        const auto written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            io_error("cannot write family overlay", path);
        }
        if (written == 0) {
            errno = EIO;
            io_error("cannot finish family overlay", path);
        }
        offset += static_cast<std::size_t>(written);
    }
}

void sync_file(int fd, const std::filesystem::path& path) {
    while (::fsync(fd) != 0) {
        if (errno == EINTR) continue;
        io_error("cannot flush family overlay", path);
    }
}

std::string identity_hex(const std::array<std::uint8_t, 32>& identity) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(identity.size() * 2);
    for (const auto byte : identity) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}
}

std::filesystem::path linux_family_overlay_path(
        const std::array<std::uint8_t, 32>& neighborhood_identity) {
    const auto config = linux_user_config_path();
    if (config.empty()) throw std::runtime_error("HOME and XDG_CONFIG_HOME are unset");
    return config.parent_path() / "player-state" /
        ("neighborhood-" + identity_hex(neighborhood_identity) + ".odhp");
}

std::array<std::uint8_t, 32> linux_neighborhood_identity(
        const std::filesystem::path& neighborhood_iff) {
    std::ifstream input(neighborhood_iff, std::ios::binary);
    if (!input) io_error("cannot open Neighborhood.iff for family overlay", neighborhood_iff);
    odh::Sha256 hash;
    std::array<std::uint8_t, 64 * 1024> block{};
    while (input) {
        input.read(reinterpret_cast<char*>(block.data()),
                   static_cast<std::streamsize>(block.size()));
        const auto count = input.gcount();
        if (count > 0) hash.update(block.data(), static_cast<std::size_t>(count));
    }
    if (!input.eof()) throw std::runtime_error("cannot read Neighborhood.iff for family overlay");
    return hash.finish();
}

std::optional<odh::FamilyOverlay> read_linux_family_overlay(
        const std::filesystem::path& path) {
    FileDescriptor file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (file.get() < 0) {
        if (errno == ENOENT) return std::nullopt;
        io_error("cannot open family overlay", path);
    }
    struct stat info {};
    if (::fstat(file.get(), &info) != 0) io_error("cannot inspect family overlay", path);
    if (!S_ISREG(info.st_mode) || info.st_size < 44 ||
        static_cast<std::uintmax_t>(info.st_size) > max_overlay_bytes)
        throw odh::FormatError("family overlay is not a bounded regular file");

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::read(file.get(), bytes.data() + offset, bytes.size() - offset);
        if (count < 0) {
            if (errno == EINTR) continue;
            io_error("cannot read family overlay", path);
        }
        if (count == 0) throw odh::FormatError("family overlay changed or was truncated while reading");
        offset += static_cast<std::size_t>(count);
    }
    std::uint8_t extra = 0;
    ssize_t count = 0;
    do { count = ::read(file.get(), &extra, 1); } while (count < 0 && errno == EINTR);
    if (count < 0) io_error("cannot finish reading family overlay", path);
    if (count != 0) throw odh::FormatError("family overlay changed while reading");
    return odh::decode_family_overlay(bytes);
}

std::optional<odh::FamilyOverlay> read_linux_family_overlay_for_neighborhood(
        const std::filesystem::path& neighborhood_iff) {
    const auto identity = linux_neighborhood_identity(neighborhood_iff);
    auto overlay = read_linux_family_overlay(linux_family_overlay_path(identity));
    if (overlay && overlay->neighborhood_identity != identity)
        throw odh::FormatError("saved family overlay identity does not match Neighborhood.iff");
    return overlay;
}

void write_linux_family_overlay_atomic(const std::filesystem::path& path,
        const odh::FamilyOverlay& overlay) {
    const auto bytes = odh::encode_family_overlay(overlay);
    if (path.empty() || path.filename().empty())
        throw std::invalid_argument("family overlay path must name a file");
    auto directory = path.parent_path();
    if (directory.empty()) directory = ".";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) throw std::runtime_error("cannot create family overlay directory: " + ec.message());
    const auto directory_status = std::filesystem::symlink_status(directory, ec);
    if (ec || std::filesystem::is_symlink(directory_status) ||
        !std::filesystem::is_directory(directory_status))
        throw std::runtime_error("family overlay directory is not a real directory");
    std::string pattern = (directory / (path.filename().string() + ".tmp.XXXXXX")).string();
    std::vector<char> temporary_name(pattern.begin(), pattern.end());
    temporary_name.push_back('\0');
    FileDescriptor temporary(::mkstemp(temporary_name.data()));
    if (temporary.get() < 0) io_error("cannot create temporary family overlay beside", path);
    const std::filesystem::path temporary_path(temporary_name.data());
    bool renamed = false;
    try {
        if (::fchmod(temporary.get(), S_IRUSR | S_IWUSR) != 0)
            io_error("cannot protect temporary family overlay", temporary_path);
        write_all(temporary.get(), bytes, temporary_path);
        sync_file(temporary.get(), temporary_path);
        const auto fd = temporary.release();
        if (::close(fd) != 0) io_error("cannot close temporary family overlay", temporary_path);
        if (::rename(temporary_path.c_str(), path.c_str()) != 0)
            io_error("cannot atomically replace family overlay", path);
        renamed = true;
        FileDescriptor parent(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (parent.get() < 0) io_error("cannot open family overlay directory", directory);
        sync_file(parent.get(), directory);
    } catch (...) {
        if (!renamed) {
            const int saved_errno = errno;
            (void)::unlink(temporary_path.c_str());
            errno = saved_errno;
        }
        throw;
    }
}

void compare_exchange_linux_family_overlay_atomic(const std::filesystem::path& path,
        const std::optional<odh::FamilyOverlay>& expected,
        const odh::FamilyOverlay& replacement) {
    if (path.empty() || path.filename().empty())
        throw std::invalid_argument("family overlay path must name a file");
    auto directory = path.parent_path();
    if (directory.empty()) directory = ".";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) throw std::runtime_error("cannot create family overlay directory: " + ec.message());
    const auto directory_status = std::filesystem::symlink_status(directory, ec);
    if (ec || std::filesystem::is_symlink(directory_status) ||
        !std::filesystem::is_directory(directory_status))
        throw std::runtime_error("family overlay directory is not a real directory");

    const auto lock_path = path.string() + ".lock";
    FileDescriptor lock(::open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
                               S_IRUSR | S_IWUSR));
    if (lock.get() < 0) io_error("cannot open family overlay transaction lock", lock_path);
    struct stat lock_info {};
    if (::fstat(lock.get(), &lock_info) != 0)
        io_error("cannot inspect family overlay transaction lock", lock_path);
    if (!S_ISREG(lock_info.st_mode) || lock_info.st_nlink != 1)
        throw std::runtime_error("family overlay transaction lock is not a private regular file");
    if (::fchmod(lock.get(), S_IRUSR | S_IWUSR) != 0)
        io_error("cannot protect family overlay transaction lock", lock_path);
    while (::flock(lock.get(), LOCK_EX) != 0) {
        if (errno == EINTR) continue;
        io_error("cannot lock family overlay transaction", lock_path);
    }

    const auto current = read_linux_family_overlay(path);
    const bool matches = current.has_value() == expected.has_value() &&
        (!current || odh::encode_family_overlay(*current) ==
                     odh::encode_family_overlay(*expected));
    if (!matches)
        throw std::runtime_error("family overlay changed during move-in; retry from the neighborhood map");
    write_linux_family_overlay_atomic(path, replacement);
}

} // namespace odh_host
