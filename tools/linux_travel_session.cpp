#include "linux_travel_session.hpp"
#include "odh/sha256.hpp"
#include "linux_session_archive_store.hpp"
#include <cerrno>
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <fstream>
#include <iterator>
namespace odh_host {
namespace {
std::vector<std::uint8_t> read_checkpoint(const std::filesystem::path& path,std::size_t limit){
 const auto size=std::filesystem::file_size(path);if(size>limit)throw odh::FormatError("travel checkpoint exceeds bounded archive limit");std::ifstream in(path,std::ios::binary);if(!in)throw odh::FormatError("cannot read current travel checkpoint");std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));if(!bytes.empty())in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!in||in.peek()!=std::char_traits<char>::eof())throw odh::FormatError("travel checkpoint changed during read");return bytes;
}
}
odh::SessionBundle make_travel_session_envelope(const std::vector<std::uint8_t>& suspended_home,const odh::SessionBundle& active,odh::SessionBundleLimits limits){
 if(!active.travel||active.travel_visited_session||active.travel_home_session||active.host.house_number==active.travel->home_house)throw odh::FormatError("active visited session required for travel envelope");
 auto home=odh::decode_session_bundle(suspended_home,std::nullopt,limits);
 if(home.travel_visited_session||home.travel_home_session||home.host.house_number!=active.travel->home_house)throw odh::FormatError("suspended home is not a residential source checkpoint");
 if(home.family_spells&&!active.family_spells)throw odh::FormatError("visited session dropped its originating family spell carrier");
 if(home.type_attributes&&!active.type_attributes)throw odh::FormatError("visited session dropped neighborhood type attributes");
 if(home.personal_state&&!active.personal_state)throw odh::FormatError("visited session dropped neighborhood personal state");
 home.personal_state=active.personal_state;
 home.travel=active.travel;home.neighbor_inventory=active.neighbor_inventory;home.host.family_budget=active.host.family_budget;home.family_spells=active.family_spells;home.type_attributes=active.type_attributes;
 home.travel_home_session=suspended_home;home.travel_visited_session=odh::encode_session_bundle(active,limits);
 odh::validate_ts1_travel_bundle(home,limits);return home;
}
TravelSessionCheckpoint reopen_travel_session_envelope(const odh::SessionBundle& envelope,odh::SessionBundleLimits limits){
 odh::validate_ts1_travel_bundle(envelope,limits);if(!envelope.travel_visited_session||!envelope.travel_home_session)throw odh::FormatError("session is not an active travel envelope");
 return {*envelope.travel_home_session,odh::decode_session_bundle(*envelope.travel_visited_session,std::nullopt,limits)};
}
namespace {
TravelSessionCheckpoint reopen_authenticated_travel_session(const std::vector<std::uint8_t>& bytes,odh::SessionBundleLimits limits){
 auto bundle=odh::decode_session_bundle(bytes,std::nullopt,limits);
 if(bundle.travel_visited_session)return reopen_travel_session_envelope(bundle,limits);
 // A sealed Generic17 home continuation is a complete recovery checkpoint,
 // including its actual advanced VM. Never restart departure from an older save.
 if(!bundle.travel||!bundle.travel->pending||bundle.travel->current_house!=bundle.travel->home_house||bundle.travel_home_session)
  throw odh::FormatError("travel checkpoint is neither a visited envelope nor sealed pending home departure");
 return {bytes,std::move(bundle)};
}
}
TravelSessionCheckpoint reopen_travel_session_checkpoint(const FamilySessionJournal& journal,const std::vector<std::uint8_t>& bytes,odh::SessionBundleLimits limits){journal.validate_restore(bytes);return reopen_authenticated_travel_session(bytes,limits);}
TravelSessionCheckpoint authenticate_managed_visited_restore(const FamilySessionJournal& journal,
 const TravelCheckpointDigest& expected_head,const std::vector<std::uint8_t>& exact_visited,odh::SessionBundleLimits limits){
 journal.verify_current();
 const auto path=journal.checkpoint_path();
 if(!path)throw odh::FormatError("visited restore requires a current managed journal envelope");
 const auto bytes=read_checkpoint(*path,limits.archive.max_file_bytes);
 if(odh::sha256(bytes.data(),bytes.size())!=expected_head)
  throw odh::FormatError("visited restore residential envelope head changed");
 auto checkpoint=reopen_travel_session_checkpoint(journal,bytes,limits);
 const auto envelope=odh::decode_session_bundle(bytes,std::nullopt,limits);
 if(!envelope.travel_visited_session||!envelope.travel_home_session)
  throw odh::FormatError("visited restore requires an authenticated visited envelope, not a pending HOME");
 if(*envelope.travel_visited_session!=exact_visited)
  throw odh::FormatError("visited restore payload differs from the current journal envelope");
 return checkpoint;
}
std::filesystem::path publish_travel_session_checkpoint(FamilySessionJournal& journal,const std::vector<std::uint8_t>& home,const odh::SessionBundle& active,odh::SessionBundleLimits limits){
 // First departure must descend from the current residential head. Later
 // publication must preserve its exact authenticated THOM bytes. No rebasing.
 journal.verify_current();const auto path=journal.checkpoint_path();if(!path)throw odh::FormatError("travel requires current managed residential checkpoint");
 auto current=read_checkpoint(*path,limits.archive.max_file_bytes);journal.validate_restore(current);
 const auto previous=odh::decode_session_bundle(current,std::nullopt,limits);
 const auto& expected=previous.travel_home_session?*previous.travel_home_session:current;
 if(home!=expected)throw odh::FormatError("travel suspended home differs from authenticated checkpoint head");
 auto envelope=make_travel_session_envelope(home,active,limits);
 return journal.publish(std::move(envelope));
}
namespace {
class PrivateTravelLock {
 int fd_=-1;
public:
 explicit PrivateTravelLock(const std::filesystem::path& path){
  if(!path.is_absolute()||path.filename().empty())throw odh::FormatError("private travel path must be an absolute filename");
  auto directory=path.parent_path();std::error_code ec;std::filesystem::create_directories(directory,ec);if(ec)throw odh::FormatError("cannot create private travel directory");
  auto status=std::filesystem::symlink_status(directory,ec);if(ec||!std::filesystem::is_directory(status)||std::filesystem::is_symlink(status))throw odh::FormatError("private travel directory is not a real directory");
  const auto lock=path.string()+".lock";fd_=::open(lock.c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,S_IRUSR|S_IWUSR);if(fd_<0)throw odh::FormatError("cannot open private travel checkpoint lock");
  struct stat info{};if(::fstat(fd_,&info)!=0||!S_ISREG(info.st_mode)||info.st_uid!=::geteuid()||info.st_nlink!=1||(info.st_mode&07777)!=0600){::close(fd_);fd_=-1;throw odh::FormatError("travel checkpoint lock is not a private owned regular file");}
  while(::flock(fd_,LOCK_EX)!=0){if(errno==EINTR)continue;::close(fd_);fd_=-1;throw odh::FormatError("cannot lock private travel checkpoint");}
 }
 ~PrivateTravelLock(){if(fd_>=0)::close(fd_);}
 PrivateTravelLock(const PrivateTravelLock&)=delete;
};
void authenticate_origin(const odh::Ts1TravelState& value,const odh::Ts1TravelState& expected){
 odh::validate_ts1_travel_state(expected);
 if(value.occupancy_epoch||expected.occupancy_epoch||value.home_house!=expected.home_house||value.home_mode!=expected.home_mode||value.family_chunk_id!=expected.family_chunk_id||value.family_number!=expected.family_number||value.family_identity!=expected.family_identity||value.neighborhood_identity!=expected.neighborhood_identity||value.permanent_roster.size()!=expected.permanent_roster.size())throw odh::FormatError("unmanaged travel source identity changed");
 for(std::size_t i=0;i<value.permanent_roster.size();++i)if(!odh::same_ts1_travel_member(value.permanent_roster[i],expected.permanent_roster[i]))throw odh::FormatError("unmanaged travel permanent roster changed");
}
std::optional<std::vector<std::uint8_t>> read_private_head(const std::filesystem::path& path,std::optional<TravelCheckpointDigest> expected,odh::SessionBundleLimits limits){
 struct stat info{};if(::lstat(path.c_str(),&info)!=0){if(errno==ENOENT&&!expected)return std::nullopt;throw odh::FormatError("private travel checkpoint is missing or unreadable");}
 if(!S_ISREG(info.st_mode)||info.st_uid!=::geteuid()||info.st_nlink!=1||(info.st_mode&07777)!=0600)throw odh::FormatError("private travel checkpoint is not a private owned regular file");
 auto bytes=odh::encode_session_archive(read_session_archive(path,limits.archive),limits.archive);
 if(!expected||odh::sha256(bytes.data(),bytes.size())!=*expected)throw odh::FormatError("private travel checkpoint expected head changed");return bytes;
}
}
void adopt_private_checkpoint_namespace(const std::filesystem::path& legacy,
        const std::filesystem::path& scoped,const TravelCheckpointDigest& digest,odh::SessionBundleLimits limits,
        LinuxNeighborhoodStorage storage) {
    if(storage.number()!=1) throw odh::FormatError("legacy private checkpoints belong only to the first neighborhood");
    if(legacy==scoped||legacy.filename()!=scoped.filename()||
       scoped.parent_path().parent_path()!=legacy.parent_path())
        throw odh::FormatError("invalid private save namespace migration");
    PrivateTravelLock legacy_lock(legacy);
    const auto previous=read_private_head(legacy,digest,limits);
    PrivateTravelLock new_lock(scoped);
    struct stat current{};
    if(::lstat(scoped.c_str(),&current)==0){
        (void)read_private_head(scoped,digest,limits);
        return;
    }
    if(errno!=ENOENT)throw odh::FormatError("cannot inspect namespaced private save");
    write_session_archive_atomic(scoped,odh::decode_session_archive(*previous,limits.archive),limits.archive);
}
TravelCheckpointDigest publish_private_travel_session_checkpoint(const std::filesystem::path& path,std::optional<TravelCheckpointDigest> expected_head,const std::vector<std::uint8_t>& home,const odh::SessionBundle& active,const odh::Ts1TravelState& expected_origin,odh::SessionBundleLimits limits){
 if(!active.travel||active.family_lease)throw odh::FormatError("private travel publisher requires an unmanaged family");authenticate_origin(*active.travel,expected_origin);
 auto envelope=make_travel_session_envelope(home,active,limits);const auto encoded=odh::encode_session_bundle(envelope,limits);const auto digest=odh::sha256(encoded.data(),encoded.size());
 PrivateTravelLock lock(path);const auto current=read_private_head(path,expected_head,limits);
 if(current){const auto previous=odh::decode_session_bundle(*current,std::nullopt,limits);if(previous.family_lease)throw odh::FormatError("private travel publisher cannot replace a managed checkpoint");if(previous.travel)authenticate_origin(*previous.travel,expected_origin);const auto& original=previous.travel_home_session?*previous.travel_home_session:*current;if(home!=original)throw odh::FormatError("private travel original home does not match authenticated current head");}
 // No retry or rebase: all preparation/validation precedes this one replacement.
 write_session_archive_atomic(path,odh::decode_session_archive(encoded,limits.archive),limits.archive);return digest;
}
TravelCheckpointDigest publish_private_home_session_checkpoint(const std::filesystem::path& path,std::optional<TravelCheckpointDigest> expected_head,const odh::SessionBundle& home,const odh::Ts1TravelState& expected_origin,odh::SessionBundleLimits limits){
 if(home.family_lease||home.travel_visited_session||home.travel_home_session||home.host.house_number!=expected_origin.home_house||home.host.neighborhood_mode!=expected_origin.home_mode||home.host.family_chunk_id!=expected_origin.family_chunk_id||home.host.family_number!=expected_origin.family_number||home.household_roster.size()!=expected_origin.permanent_roster.size())throw odh::FormatError("private home publisher requires matching unmanaged permanent residence");
 if(home.travel)authenticate_origin(*home.travel,expected_origin);else authenticate_origin(expected_origin,expected_origin);
 for(const auto& m:home.household_roster)if(std::none_of(expected_origin.permanent_roster.begin(),expected_origin.permanent_roster.end(),[&](const auto& p){return m.guid==p.guid&&m.neighbor_id==p.neighbor_id&&m.object_definition_id==p.definition_chunk_id&&m.character_asset==p.character_asset&&m.character_fingerprint==p.character_fingerprint;}))throw odh::FormatError("private home publisher permanent roster resource mismatch");
 const auto encoded=odh::encode_session_bundle(home,limits);const auto digest=odh::sha256(encoded.data(),encoded.size());PrivateTravelLock lock(path);const auto current=read_private_head(path,expected_head,limits);
 if(current){const auto previous=odh::decode_session_bundle(*current,std::nullopt,limits);if(previous.family_lease)throw odh::FormatError("private home publisher cannot replace managed occupancy");if(previous.travel)authenticate_origin(*previous.travel,expected_origin);const auto original=previous.travel_home_session?odh::decode_session_bundle(*previous.travel_home_session,std::nullopt,limits):previous;if(odh::encode_session_resource_identity(original.resources)!=odh::encode_session_resource_identity(home.resources)||original.host.house_number!=home.host.house_number||original.host.family_chunk_id!=home.host.family_chunk_id||original.host.family_number!=home.host.family_number)throw odh::FormatError("private home publication changed source lot resources or identity");}
 write_session_archive_atomic(path,odh::decode_session_archive(encoded,limits.archive),limits.archive);return digest;
}
TravelSessionCheckpoint reopen_private_travel_session_checkpoint(const std::filesystem::path& path,const TravelCheckpointDigest& expected_head,const odh::Ts1TravelState& expected_origin,odh::SessionBundleLimits limits){
 PrivateTravelLock lock(path);const auto bytes=read_private_head(path,expected_head,limits);const auto envelope=odh::decode_session_bundle(*bytes,std::nullopt,limits);if(envelope.family_lease||!envelope.travel)throw odh::FormatError("private travel reopen requires unmanaged TRIP envelope");authenticate_origin(*envelope.travel,expected_origin);return reopen_authenticated_travel_session(*bytes,limits);
}

}
