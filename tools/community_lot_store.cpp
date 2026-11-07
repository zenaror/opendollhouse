// SPDX-License-Identifier: MPL-2.0
#include "community_lot_store.hpp"
#include "linux_session_archive_store.hpp"
#include "neighborhood_storage.hpp"
#include "odh/sha256.hpp"
#include <fstream>
#include <algorithm>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

namespace odh_host {
namespace {
constexpr odh::SessionArchiveLimits limits{32*1024*1024,16*1024*1024,3};
std::array<std::uint8_t,32> fingerprint(const std::filesystem::path&p){
    if(std::filesystem::is_symlink(std::filesystem::symlink_status(p))||!std::filesystem::is_regular_file(p)||std::filesystem::file_size(p)>64u*1024u*1024u)
        throw odh::FormatError("community lot provenance source must be a bounded regular file");
    std::ifstream f(p,std::ios::binary);if(!f)throw odh::FormatError("cannot read community lot provenance");
    odh::Sha256 hash;std::array<char,65536> buffer{};
    while(f){f.read(buffer.data(),buffer.size());if(f.gcount()>0)hash.update(buffer.data(),std::size_t(f.gcount()));}
    if(!f.eof())throw odh::FormatError("cannot finish community lot provenance");return hash.finish();
}
std::filesystem::path checkpoint(const CommunityLotStoreOptions&o){
    if(o.private_directory.empty()||o.game_root.empty())throw odh::FormatError("community lot private directory is absent");
    const auto private_path=std::filesystem::weakly_canonical(o.private_directory),game=std::filesystem::canonical(o.game_root);
    const auto relative=private_path.lexically_relative(game);
    if(private_path==game||(!relative.empty()&&*relative.begin()!=".."))throw odh::FormatError("community edits cannot be stored inside the installation");
    auto probe=o.private_directory;
    while(!probe.empty()){
        if(std::filesystem::is_symlink(std::filesystem::symlink_status(probe)))throw odh::FormatError("community private path cannot contain a symbolic link");
        if(probe==probe.parent_path())break;probe=probe.parent_path();
    }
    return o.private_directory/"architecture.odhl";
}
std::optional<StoredCommunityLot> read(const CommunityLotStoreOptions&o){
    const auto path=checkpoint(o);const auto status=std::filesystem::symlink_status(path);
    if(!std::filesystem::exists(status))return {};
    if(!std::filesystem::is_regular_file(status))throw odh::FormatError("community checkpoint is not a regular file");
    // Open before measuring: another writer may atomically replace the path,
    // but this stream owns one inode and never pairs its data with a later head.
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if(!stream)throw odh::FormatError("cannot open community checkpoint");
    const auto size=stream.tellg();
    if(size<0||static_cast<std::uint64_t>(size)>32u*1024u*1024u)
        throw odh::FormatError("community checkpoint exceeds bounded profile");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));stream.seekg(0);
    if(!bytes.empty())stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!stream||stream.peek()!=std::char_traits<char>::eof())throw odh::FormatError("cannot read complete community checkpoint");
    auto document=decode_community_lot_document(odh::decode_session_archive(bytes,limits));
    if(!same_community_lot(document.identity,community_lot_identity(o)))throw odh::FormatError("community checkpoint belongs to different source lot or neighborhood");
    return StoredCommunityLot{std::move(document),odh::sha256(bytes.data(),bytes.size()),path};
}
}
CommunityLotIdentity community_lot_identity(const CommunityLotStoreOptions&o){
    NeighborhoodStorage storage(o.neighborhood_number);storage.verify_neighborhood_source(o.user_data/"Neighborhood.iff");
    CommunityLotIdentity result;result.neighborhood=std::uint8_t(o.neighborhood_number);result.mode=o.mode;result.house=o.house;
    result.neighborhood_sha256=fingerprint(o.user_data/"Neighborhood.iff");result.lot_sha256=fingerprint(o.lot_path);
    if(result.mode<1||result.mode>7||result.mode==6||result.house<1||result.house>98)throw odh::FormatError("community editor lot number or mode invalid");
    if(o.expected_identity&&!same_community_lot(result,*o.expected_identity))
        throw odh::FormatError("community source changed while editing");
    return result;
}
StoredCommunityLot load_community_lot_blob(const CommunityLotStoreOptions&o,const std::array<std::uint8_t,32>& head){
    if(std::all_of(head.begin(),head.end(),[](auto b){return b==0;}))throw odh::FormatError("property blob head is absent");
    static constexpr char digits[]="0123456789abcdef";std::string name;
    for(auto b:head){name.push_back(digits[b>>4]);name.push_back(digits[b&15]);}
    const auto directory=checkpoint(o).parent_path()/"blobs";
    if(std::filesystem::is_symlink(std::filesystem::symlink_status(directory)))throw odh::FormatError("property blob directory is a symbolic link");
    const auto path=directory/(name+".odhs");const auto archive=read_session_archive(path,limits);
    const auto encoded=odh::encode_session_archive(archive,limits);
    if(odh::sha256(encoded.data(),encoded.size())!=head)throw odh::FormatError("property blob content does not match its exact head");
    auto document=decode_community_lot_document(archive);
    if(!same_community_lot(document.identity,community_lot_identity(o)))throw odh::FormatError("property blob belongs to another installed lot");
    return {std::move(document),head,path};
}
StoredCommunityLot publish_community_lot_blob(const CommunityLotStoreOptions&o,const CommunityLotDocument& document){
    if(!same_community_lot(document.identity,community_lot_identity(o)))throw odh::FormatError("property blob publication belongs to another installed lot");
    const auto archive=encode_community_lot_document(document);const auto encoded=odh::encode_session_archive(archive,limits);
    const auto head=odh::sha256(encoded.data(),encoded.size());const auto directory=checkpoint(o).parent_path()/"blobs";
    const auto status=std::filesystem::symlink_status(directory);
    if(std::filesystem::is_symlink(status)||(std::filesystem::exists(status)&&!std::filesystem::is_directory(status)))
        throw odh::FormatError("property blob directory must be a real directory");
    std::filesystem::create_directories(directory);
    const auto path=publish_session_archive_immutable(directory,archive,limits);
    auto reread=load_community_lot_blob(o,head);
    if(reread.checkpoint!=path)throw odh::FormatError("property blob immutable path differs from its verified digest");
    return reread;
}
std::optional<StoredCommunityLot> load_private_community_lot(const CommunityLotStoreOptions&o){return read(o);}
std::optional<odh::ArchitectureSnapshot> load_private_community_architecture(const CommunityLotStoreOptions&o){
    const auto stored=read(o);if(!stored)return {};return stored->document.architecture;
}
std::optional<std::array<std::uint8_t,32>> community_lot_checkpoint_head(const CommunityLotStoreOptions&o){
    const auto stored=read(o);if(!stored)return {};return stored->head;
}
StoredCommunityLot publish_private_community_lot(const CommunityLotStoreOptions&o,
        const std::optional<std::array<std::uint8_t,32>>& expected,const CommunityLotDocument& document){
    const auto path=checkpoint(o);std::filesystem::create_directories(path.parent_path());
    const auto lock_path=path.string()+".lock";
    const int fd=::open(lock_path.c_str(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)throw odh::FormatError("cannot lock community checkpoint");
    struct Lock{int fd;~Lock(){::flock(fd,LOCK_UN);::close(fd);}} lock{fd};
    if(::flock(fd,LOCK_EX))throw odh::FormatError("cannot acquire community checkpoint lock");
    const auto current=read(o);
    if((current?std::optional<std::array<std::uint8_t,32>>{current->head}:std::nullopt)!=expected)
        throw odh::FormatError("community checkpoint changed; reopen the lot");
    if(!same_community_lot(document.identity,community_lot_identity(o)))
        throw odh::FormatError("community publication belongs to a different source");
    const auto archive=encode_community_lot_document(document);
    const auto encoded=odh::encode_session_archive(archive,limits);
    const auto own_head=odh::sha256(encoded.data(),encoded.size());
    write_session_archive_atomic(path,archive,limits);
    auto reopened=read(o);
    if(!reopened||reopened->head!=own_head)throw odh::FormatError("community checkpoint published but could not be verified");
    return std::move(*reopened);
}
std::filesystem::path publish_private_community_architecture(const CommunityLotStoreOptions&o,
        const std::optional<std::array<std::uint8_t,32>>& expected,const odh::ArchitectureSnapshot&state){
    auto current=read(o);
    auto document=current?std::move(current->document):CommunityLotDocument{community_lot_identity(o),state};
    document.architecture=state;
    return publish_private_community_lot(o,expected,document).checkpoint;
}
}
