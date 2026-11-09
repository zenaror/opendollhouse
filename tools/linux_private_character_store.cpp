#include "linux_private_character_store.hpp"

#include "odh/character_catalog.hpp"
#include "odh/formats.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <stdexcept>
#include <string>
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace odh_host {
namespace {
constexpr std::uint32_t kTemplateGuid = 0x7FD96B54;
constexpr std::size_t kMaximumCharacterBytes = 16 * 1024 * 1024;

class FileDescriptor {
public:
    explicit FileDescriptor(int fd=-1):fd_(fd){}
    ~FileDescriptor(){if(fd_>=0)::close(fd_);}
    FileDescriptor(const FileDescriptor&)=delete;
    FileDescriptor& operator=(const FileDescriptor&)=delete;
    FileDescriptor(FileDescriptor&& other)noexcept:fd_(other.fd_){other.fd_=-1;}
    FileDescriptor& operator=(FileDescriptor&& other)noexcept{
        if(this!=&other){if(fd_>=0)::close(fd_);fd_=other.fd_;other.fd_=-1;}return *this;
    }
    int get()const{return fd_;}
private:int fd_;
};

[[noreturn]] void io_error(const char* operation,const std::filesystem::path& path) {
    throw std::runtime_error(std::string(operation)+" "+path.string()+": "+std::strerror(errno));
}

void write_all(int fd,const std::vector<std::uint8_t>& bytes,const std::filesystem::path& path) {
    std::size_t position=0;
    while(position<bytes.size()) {
        const auto count=::write(fd,bytes.data()+position,bytes.size()-position);
        if(count<0){if(errno==EINTR)continue;io_error("cannot write private character",path);}
        if(count==0){errno=EIO;io_error("cannot finish private character",path);}
        position+=static_cast<std::size_t>(count);
    }
}

std::uint32_t secure_guid() {
    std::uint32_t guid=0;
    auto* bytes=reinterpret_cast<std::uint8_t*>(&guid);
    std::size_t position=0;
    while(position<sizeof(guid)) {
        const auto count=::getrandom(bytes+position,sizeof(guid)-position,0);
        if(count<0){if(errno==EINTR)continue;throw std::runtime_error("OS entropy source failed for character GUID");}
        if(count==0)throw std::runtime_error("OS entropy source returned no character GUID bytes");
        position+=static_cast<std::size_t>(count);
    }
    return guid;
}

void collect_guids(const std::filesystem::path& directory,std::set<std::uint32_t>& guids,
                   bool may_be_missing) {
    std::error_code error;
    const auto status=std::filesystem::symlink_status(directory,error);
    if(may_be_missing&&(error==std::errc::no_such_file_or_directory||
                       (!error&&status.type()==std::filesystem::file_type::not_found)))return;
    if(error||std::filesystem::is_symlink(status)||!std::filesystem::is_directory(status))
        throw odh::FormatError("character catalog path is not a real directory");
    odh::CharacterCatalogLimits limits;
    if(may_be_missing){limits.files=1024;limits.total_file_bytes=64ull*1024ull*1024ull;limits.person_definitions=4096;}
    const auto candidates=odh::read_character_catalog(directory,limits);
    for(const auto& candidate:candidates)guids.insert(candidate.definition.guid);
}

std::filesystem::path unused_filename(int directory_fd,const std::filesystem::path& directory) {
    for(unsigned number=1;number<=99999;++number) {
        char name[16];
        std::snprintf(name,sizeof(name),"User%05u.iff",number);
        struct stat status{};
        if(::fstatat(directory_fd,name,&status,AT_SYMLINK_NOFOLLOW)==0)continue;
        if(errno==ENOENT)return directory/name;
        io_error("cannot check private character filename",directory/name);
    }
    throw std::runtime_error("private character filename range is exhausted");
}

void write_new_file(int directory_fd,const std::filesystem::path& path,
                    const std::vector<std::uint8_t>& bytes) {
    const auto final_name=path.filename().string();
    std::string temporary;
    FileDescriptor file;
    for(unsigned attempt=0;attempt<100;++attempt) {
        temporary=".character-"+std::to_string(::getpid())+"-"+std::to_string(attempt)+".tmp";
        const int fd=::openat(directory_fd,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,S_IRUSR|S_IWUSR);
        if(fd>=0){file=FileDescriptor(fd);break;}
        if(errno!=EEXIST)io_error("cannot create temporary private character",path);
    }
    if(file.get()<0)throw std::runtime_error("cannot allocate temporary private character file");
    bool published=false;
    try {
        write_all(file.get(),bytes,path);
        while(::fsync(file.get())!=0){if(errno==EINTR)continue;io_error("cannot flush private character",path);}
        if(::syscall(SYS_renameat2,directory_fd,temporary.c_str(),directory_fd,final_name.c_str(),RENAME_NOREPLACE)!=0)
            io_error("cannot publish private character without replacement",path);
        temporary.clear();
        published=true;
        while(::fsync(directory_fd)!=0){if(errno==EINTR)continue;io_error("cannot flush private character directory",path.parent_path());}
    } catch(...) {
        const int saved=errno;
        if(!temporary.empty())(void)::unlinkat(directory_fd,temporary.c_str(),0);
        if(published)(void)::unlinkat(directory_fd,final_name.c_str(),0);
        errno=saved;
        throw;
    }
}
} // namespace

std::uint32_t choose_linux_private_character_guid(const std::filesystem::path& installed,
    const std::filesystem::path& owned,CharacterGuidSource source,const std::function<bool(std::uint32_t)>& available){
    if(!source)source=secure_guid;std::set<std::uint32_t> used;
    collect_guids(installed,used,false);collect_guids(owned,used,true);
    for(unsigned attempt=0;attempt<256;++attempt){const auto guid=source();if(guid&&guid!=kTemplateGuid&&!used.count(guid)&&(!available||available(guid)))return guid;}
    throw std::runtime_error("could not select an unused character GUID before source appearance selection");
}

CreatedPrivateCharacter create_linux_private_character(
        const std::vector<std::uint8_t>& template_iff,
        const std::filesystem::path& installed_characters,
        const std::filesystem::path& private_characters,
        const odh::TemplatePersonRequest& request,
        CharacterGuidSource guid_source,const std::function<bool(std::uint32_t)>& guid_available) {
    if(private_characters.empty()||!private_characters.is_absolute())
        throw std::invalid_argument("private character directory must be an absolute path");
    if(!guid_source)guid_source=secure_guid;
    std::error_code error;
    std::filesystem::create_directories(private_characters,error);
    if(error)throw std::runtime_error("cannot create private character directory: "+error.message());
    const auto status=std::filesystem::symlink_status(private_characters,error);
    if(error||std::filesystem::is_symlink(status)||!std::filesystem::is_directory(status))
        throw odh::FormatError("private character path is not a real directory");
    if(::chmod(private_characters.c_str(),S_IRWXU)!=0)io_error("cannot secure private character directory",private_characters);

    FileDescriptor directory(::open(private_characters.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    if(directory.get()<0)io_error("cannot open private character directory",private_characters);
    FileDescriptor lock(::openat(directory.get(),".create-character.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,S_IRUSR|S_IWUSR));
    if(lock.get()<0)io_error("cannot open private character creation lock",private_characters);
    struct stat lock_status{};
    if(::fstat(lock.get(),&lock_status)!=0)io_error("cannot inspect private character creation lock",private_characters);
    if(!S_ISREG(lock_status.st_mode)||lock_status.st_nlink!=1)
        throw std::runtime_error("private character creation lock is not a private regular file");
    if(::fchmod(lock.get(),S_IRUSR|S_IWUSR)!=0)io_error("cannot secure private character creation lock",private_characters);
    while(::flock(lock.get(),LOCK_EX)!=0){if(errno==EINTR)continue;io_error("cannot lock private character catalog",private_characters);}

    std::set<std::uint32_t> used_guids;
    collect_guids(installed_characters,used_guids,false);
    collect_guids(private_characters,used_guids,true);
    auto path=unused_filename(directory.get(),private_characters);
    std::vector<std::uint8_t> generated;
    std::uint32_t guid=0;
    for(unsigned attempt=0;attempt<256;++attempt) {
        const auto candidate=guid_source();
        if(candidate==0||candidate==kTemplateGuid||(request.source&&candidate==request.source->guid)||
           used_guids.count(candidate)||(guid_available&&!guid_available(candidate)))continue;
        guid=candidate;
        auto final_request=request;
        final_request.guid=guid;
        const auto stem=path.stem().string();
        final_request.object_label=stem;
        generated=odh::clone_template_person(template_iff,final_request);
        break;
    }
    if(guid==0)throw std::runtime_error("could not allocate an unused person GUID");
    if(generated.size()>kMaximumCharacterBytes)throw odh::FormatError("generated character exceeds private file size limit");
    CreatedPrivateCharacter result{guid,path,odh::sha256(generated.data(),generated.size())};
    write_new_file(directory.get(),path,generated);
    return result;
}

bool remove_linux_private_character_if_unchanged(const CreatedPrivateCharacter& created) {
    const auto directory_path=created.path.parent_path();
    if(directory_path.empty()||created.path.filename().empty())
        throw std::invalid_argument("created private character path is invalid");
    FileDescriptor directory(::open(directory_path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    if(directory.get()<0){if(errno==ENOENT)return false;io_error("cannot open private character directory for rollback",directory_path);}
    FileDescriptor lock(::openat(directory.get(),".create-character.lock",O_RDWR|O_CLOEXEC|O_NOFOLLOW));
    if(lock.get()<0){if(errno==ENOENT)return false;io_error("cannot open private character rollback lock",directory_path);}
    struct stat lock_status{};
    if(::fstat(lock.get(),&lock_status)!=0)io_error("cannot inspect private character rollback lock",directory_path);
    if(!S_ISREG(lock_status.st_mode)||lock_status.st_nlink!=1)
        throw std::runtime_error("private character rollback lock is not a private regular file");
    while(::flock(lock.get(),LOCK_EX)!=0){if(errno==EINTR)continue;io_error("cannot lock private character rollback",directory_path);}
    const auto name=created.path.filename().string();
    FileDescriptor file(::openat(directory.get(),name.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW));
    if(file.get()<0){if(errno==ENOENT)return false;io_error("cannot open private character for rollback",created.path);}
    struct stat info{};
    if(::fstat(file.get(),&info)!=0)io_error("cannot inspect private character for rollback",created.path);
    if(!S_ISREG(info.st_mode)||info.st_size<0||static_cast<std::uintmax_t>(info.st_size)>kMaximumCharacterBytes)
        return false;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
    std::size_t offset=0;
    while(offset<bytes.size()) {
        const auto count=::read(file.get(),bytes.data()+offset,bytes.size()-offset);
        if(count<0){if(errno==EINTR)continue;io_error("cannot read private character for rollback",created.path);}
        if(count==0)return false;
        offset+=static_cast<std::size_t>(count);
    }
    if(odh::sha256(bytes.data(),bytes.size())!=created.content_sha256)return false;
    if(::unlinkat(directory.get(),name.c_str(),0)!=0){if(errno==ENOENT)return false;io_error("cannot remove private character during rollback",created.path);}
    while(::fsync(directory.get())!=0){if(errno==EINTR)continue;io_error("cannot flush private character rollback",directory_path);}
    return true;
}
} // namespace odh_host
