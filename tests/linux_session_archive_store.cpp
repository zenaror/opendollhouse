#include "linux_session_archive_store.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
template<class F>
void rejects(F&& operation, const char* message) {
    try { operation(); }
    catch (const std::exception&) { return; }
    require(false, message);
}
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("odh-session-store-" + std::to_string(stamp));
        if (!std::filesystem::create_directory(path))
            throw std::runtime_error("cannot create temporary session-store directory");
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
odh::SessionArchive archive(std::initializer_list<std::uint8_t> data) {
    odh::SessionArchive result;
    result.sections.push_back({{{'M','E','T','A'}}, true, data});
    return result;
}
std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main() {
    TemporaryDirectory temp;
    const auto path = temp.path / "slot-01.odhs";
    const auto first = archive({1,2,3});
    const auto replacement = archive({9,8,7,6});
    const auto immutable=odh_host::publish_session_archive_immutable(temp.path,first);
    require(immutable.filename().string().size()==69&&immutable.extension()==".odhs",
            "immutable archive uses digest filename");
    struct stat immutable_before{},immutable_after{};
    require(::stat(immutable.c_str(),&immutable_before)==0&&(immutable_before.st_mode&0777)==0600,
            "immutable archive is private");
    require(odh_host::publish_session_archive_immutable(temp.path,first)==immutable&&
            ::stat(immutable.c_str(),&immutable_after)==0&&immutable_before.st_ino==immutable_after.st_ino,
            "identical archive reuses existing inode");
    require(odh_host::read_session_archive(immutable).sections[0].data==first.sections[0].data,
            "immutable archive can be reopened");
    require(odh_host::publish_session_archive_immutable(temp.path,replacement)!=immutable,
            "changed payload publishes a separate archive");
    const auto original_bytes=read_text(immutable);
    {std::ofstream output(immutable,std::ios::binary|std::ios::trunc);auto changed=original_bytes;changed.back()^=1;output<<changed;}
    rejects([&]{(void)odh_host::publish_session_archive_immutable(temp.path,first);},
            "mismatched digest-named bytes reject without replacement");
    {std::ofstream output(immutable,std::ios::binary|std::ios::trunc);output<<original_bytes;}
    require(::chmod(immutable.c_str(),0644)==0,"make existing fixture non-private");
    rejects([&]{(void)odh_host::publish_session_archive_immutable(temp.path,first);},
            "non-private existing immutable file rejects");
    require(::chmod(immutable.c_str(),0600)==0,"restore fixture permissions");
    const auto retained=temp.path/"retained.odhs";
    std::filesystem::rename(immutable,retained);
    std::filesystem::create_symlink(retained.filename(),immutable);
    rejects([&]{(void)odh_host::publish_session_archive_immutable(temp.path,first);},
            "digest-named symlink rejects");
    require(read_text(retained)==original_bytes,"symlink rejection preserves target");
    std::filesystem::remove(immutable);
    std::filesystem::rename(retained,immutable);
    const auto directory_link=temp.path/"directory-link";
    std::filesystem::create_directory_symlink(temp.path,directory_link);
    rejects([&]{(void)odh_host::publish_session_archive_immutable(directory_link,first);},
            "immutable publication refuses symlink directory");
    odh::SessionArchiveLimits small;small.max_file_bytes=1;
    rejects([&]{(void)odh_host::publish_session_archive_immutable(temp.path,first,small);},
            "invalid limits reject before publication");
    for(const auto& entry:std::filesystem::directory_iterator(temp.path))
        require(entry.path().filename().string().find(".session-tmp-")!=0,
                "immutable publication cleans up temporary names");
    odh_host::write_session_archive_atomic(path, first);
    require(odh_host::read_session_archive(path).sections[0].data == first.sections[0].data,
            "new archive can be reopened from disk");
    struct stat info {};
    require(::stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
            "save file is private to its owner");

    odh_host::write_session_archive_atomic(path, replacement);
    require(odh_host::read_session_archive(path).sections[0].data == replacement.sections[0].data,
            "replacement becomes the complete new archive");

    auto invalid = replacement;
    invalid.sections.push_back(invalid.sections.front());
    rejects([&] { odh_host::write_session_archive_atomic(path, invalid); },
            "invalid replacement is rejected before touching the existing save");
    require(odh_host::read_session_archive(path).sections[0].data == replacement.sections[0].data,
            "failed replacement leaves prior save intact");

    const auto damaged = temp.path / "damaged.odhs";
    {
        std::ofstream output(damaged, std::ios::binary);
        output << "short";
    }
    rejects([&] { (void)odh_host::read_session_archive(damaged); },
            "truncated on-disk save is rejected");

    const auto directory_target = temp.path / "kept-directory";
    std::filesystem::create_directory(directory_target);
    const auto marker = directory_target / "marker";
    { std::ofstream output(marker); output << "keep"; }
    rejects([&] { odh_host::write_session_archive_atomic(directory_target, first); },
            "cannot replace a directory with a save file");
    require(std::filesystem::is_directory(directory_target) && read_text(marker) == "keep",
            "failed atomic rename leaves existing target untouched");
    const auto temporary_prefix = directory_target.filename().string() + ".tmp.";
    for (const auto& entry : std::filesystem::directory_iterator(temp.path))
        require(entry.path().filename().string().compare(0, temporary_prefix.size(), temporary_prefix) != 0,
                "failed atomic rename cleans up its temporary file");

    const auto outside = temp.path / "outside.odhs";
    odh_host::write_session_archive_atomic(outside, first);
    const auto link = temp.path / "link.odhs";
    std::filesystem::create_symlink(outside.filename(), link);
    rejects([&] { (void)odh_host::read_session_archive(link); },
            "save reader refuses symbolic links");

    std::cout << "Linux session archive store checks passed\n";
}
