#include "linux_travel_session.hpp"
#include "odh/sha256.hpp"
#include "linux_session_archive_store.hpp"
#include "linux_family_overlay_store.hpp"
#include <exception>
#include <algorithm>
#include <cerrno>
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
void reset_original_frontend_load_globals(odh::RuntimeGlobals& globals,const odh::Ts1TravelState& destination,bool native_restore){
 odh::validate_ts1_travel_state(destination);
 if(native_restore||destination.protocol!=odh::Ts1TravelProtocol::original_6e598||
    destination.current_house==destination.home_house||destination.family_activated||destination.pending)
  throw odh::FormatError("original frontend globals require the authenticated fresh destination load phase");
 //004f1b96 calls46dae0(32,0) on both sides of2808b;32 is not an
 // alias for CurrentFamily9. Never replace legitimate synthetic family0.
 if(!globals.write(32,0))throw odh::FormatError("original frontend Simless backing is unavailable");
}
odh::SessionBundle make_travel_session_envelope(const std::vector<std::uint8_t>& suspended_home,const odh::SessionBundle& active,odh::SessionBundleLimits limits){
 if(!active.travel||active.travel_visited_session||active.travel_home_session||active.host.house_number==active.travel->home_house)throw odh::FormatError("active visited session required for travel envelope");
 auto home=odh::decode_session_bundle(suspended_home,std::nullopt,limits);
 if(home.travel_visited_session||home.travel_home_session||home.host.house_number!=active.travel->home_house)throw odh::FormatError("suspended home is not a residential source checkpoint");
 if(home.family_spells&&!active.family_spells)throw odh::FormatError("visited session dropped its originating family spell carrier");
 if(home.type_attributes&&!active.type_attributes)throw odh::FormatError("visited session dropped neighborhood type attributes");
 if(home.personal_state&&!active.personal_state)throw odh::FormatError("visited session dropped neighborhood personal state");
 if(home.family_vacation&&!active.family_vacation)throw odh::FormatError("visited session dropped shared family vacation state");
 home.family_vacation=active.family_vacation;
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
 if(value.protocol!=expected.protocol||value.occupancy_epoch||expected.occupancy_epoch||value.home_house!=expected.home_house||value.home_mode!=expected.home_mode||value.family_chunk_id!=expected.family_chunk_id||value.family_number!=expected.family_number||value.family_identity!=expected.family_identity||value.neighborhood_identity!=expected.neighborhood_identity||value.permanent_roster.size()!=expected.permanent_roster.size())throw odh::FormatError("unmanaged travel source identity changed");
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
namespace {
void reject_adopted_legacy_head(const odh::Ts1TravelState& origin,LinuxNeighborhoodStorage storage){
 const auto current=read_linux_family_overlay(linux_family_overlay_path(origin.neighborhood_identity,storage));
 if(current&&current->family_vacation&&odh::family_vacation_private_head(*current->family_vacation,origin.family_chunk_id))
  throw odh::FormatError("adopted private vacation head belongs only to its typed overlay owner");
}
}
TravelCheckpointDigest publish_private_travel_session_checkpoint(const std::filesystem::path& path,std::optional<TravelCheckpointDigest> expected_head,const std::vector<std::uint8_t>& home,const odh::SessionBundle& active,const odh::Ts1TravelState& expected_origin,odh::SessionBundleLimits limits,LinuxNeighborhoodStorage storage){
 reject_adopted_legacy_head(expected_origin,storage);
 if(!active.travel||active.family_lease)throw odh::FormatError("private travel publisher requires an unmanaged family");authenticate_origin(*active.travel,expected_origin);
 auto envelope=make_travel_session_envelope(home,active,limits);const auto encoded=odh::encode_session_bundle(envelope,limits);const auto digest=odh::sha256(encoded.data(),encoded.size());
 PrivateTravelLock lock(path);reject_adopted_legacy_head(expected_origin,storage);const auto current=read_private_head(path,expected_head,limits);
 if(current){const auto previous=odh::decode_session_bundle(*current,std::nullopt,limits);if(previous.family_lease)throw odh::FormatError("private travel publisher cannot replace a managed checkpoint");if(previous.travel)authenticate_origin(*previous.travel,expected_origin);const auto& original=previous.travel_home_session?*previous.travel_home_session:*current;if(home!=original)throw odh::FormatError("private travel original home does not match authenticated current head");}
 // No retry or rebase: all preparation/validation precedes this one replacement.
 write_session_archive_atomic(path,odh::decode_session_archive(encoded,limits.archive),limits.archive);return digest;
}
TravelCheckpointDigest publish_private_home_session_checkpoint(const std::filesystem::path& path,std::optional<TravelCheckpointDigest> expected_head,const odh::SessionBundle& home,const odh::Ts1TravelState& expected_origin,odh::SessionBundleLimits limits,LinuxNeighborhoodStorage storage){
 reject_adopted_legacy_head(expected_origin,storage);
 if(home.family_lease||home.travel_visited_session||home.travel_home_session||home.host.house_number!=expected_origin.home_house||home.host.neighborhood_mode!=expected_origin.home_mode||home.host.family_chunk_id!=expected_origin.family_chunk_id||home.host.family_number!=expected_origin.family_number||home.household_roster.size()!=expected_origin.permanent_roster.size())throw odh::FormatError("private home publisher requires matching unmanaged permanent residence");
 if(home.travel)authenticate_origin(*home.travel,expected_origin);else authenticate_origin(expected_origin,expected_origin);
 for(const auto& m:home.household_roster)if(std::none_of(expected_origin.permanent_roster.begin(),expected_origin.permanent_roster.end(),[&](const auto& p){return m.guid==p.guid&&m.neighbor_id==p.neighbor_id&&m.object_definition_id==p.definition_chunk_id&&m.character_asset==p.character_asset&&m.character_fingerprint==p.character_fingerprint;}))throw odh::FormatError("private home publisher permanent roster resource mismatch");
 const auto encoded=odh::encode_session_bundle(home,limits);const auto digest=odh::sha256(encoded.data(),encoded.size());PrivateTravelLock lock(path);reject_adopted_legacy_head(expected_origin,storage);const auto current=read_private_head(path,expected_head,limits);
 if(current){const auto previous=odh::decode_session_bundle(*current,std::nullopt,limits);if(previous.family_lease)throw odh::FormatError("private home publisher cannot replace managed occupancy");if(previous.travel)authenticate_origin(*previous.travel,expected_origin);const auto original=previous.travel_home_session?odh::decode_session_bundle(*previous.travel_home_session,std::nullopt,limits):previous;if(odh::encode_session_resource_identity(original.resources)!=odh::encode_session_resource_identity(home.resources)||original.host.house_number!=home.host.house_number||original.host.family_chunk_id!=home.host.family_chunk_id||original.host.family_number!=home.host.family_number)throw odh::FormatError("private home publication changed source lot resources or identity");}
 write_session_archive_atomic(path,odh::decode_session_archive(encoded,limits.archive),limits.archive);return digest;
}
TravelSessionCheckpoint reopen_private_travel_session_checkpoint(const std::filesystem::path& path,const TravelCheckpointDigest& expected_head,const odh::Ts1TravelState& expected_origin,odh::SessionBundleLimits limits,LinuxNeighborhoodStorage storage){
 reject_adopted_legacy_head(expected_origin,storage);
 PrivateTravelLock lock(path);reject_adopted_legacy_head(expected_origin,storage);const auto bytes=read_private_head(path,expected_head,limits);const auto envelope=odh::decode_session_bundle(*bytes,std::nullopt,limits);if(envelope.family_lease||!envelope.travel)throw odh::FormatError("private travel reopen requires unmanaged TRIP envelope");authenticate_origin(*envelope.travel,expected_origin);return reopen_authenticated_travel_session(*bytes,limits);
}

namespace {
std::string vacation_hex(const TravelCheckpointDigest& value){
 constexpr char digits[]="0123456789abcdef";std::string out;
 for(auto byte:value){out+=digits[byte>>4];out+=digits[byte&15];}return out;
}
bool same_vacation_overlay(const std::optional<odh::FamilyOverlay>& a,const std::optional<odh::FamilyOverlay>& b){
 return a.has_value()==b.has_value()&&(!a||odh::encode_family_overlay(*a)==odh::encode_family_overlay(*b));
}
void vacation_directory(const std::filesystem::path& path){
 if(::mkdir(path.c_str(),0700)!=0){
  if(errno==ENOENT){const auto parent=path.parent_path();if(parent.empty()||parent==path)throw odh::FormatError("vacation archive directory has no parent");vacation_directory(parent);if(::mkdir(path.c_str(),0700)!=0&&errno!=EEXIST)throw odh::FormatError("cannot create vacation archive directory");}
  else if(errno!=EEXIST)throw odh::FormatError("cannot create vacation archive directory");
 }
 const int fd=::open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)throw odh::FormatError("vacation archive directory is not a real directory");::close(fd);
 const int parent=::open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
 if(parent<0)throw odh::FormatError("cannot open vacation archive directory parent");
 int result;do{result=::fsync(parent);}while(result!=0&&errno==EINTR);::close(parent);
 if(result!=0)throw odh::FormatError("cannot flush vacation archive directory parent");
}
const odh::FamilyVacationRow& vacation_row(const odh::FamilyVacationSnapshot& state,std::uint16_t id){
 const auto it=std::find_if(state.families.begin(),state.families.end(),[&](const auto& row){return row.family_chunk_id==id&&row.active;});
 if(it==state.families.end()||!it->current_word)throw odh::FormatError("vacation family has no complete admitted booking word");return *it;
}
odh::FamilyVacationPhase vacation_phase(const odh::SessionBundle& sealed,const TravelCheckpointDigest& head,
 const odh::FamilyVacationSnapshot& state,odh::FamilyVacationPhaseKind kind,std::uint16_t family,std::uint16_t destination){
 const auto bytes=odh::encode_session_bundle(sealed);
 if(odh::sha256(bytes.data(),bytes.size())!=head)throw odh::FormatError("vacation source bytes differ from sealed head");
 odh::validate_ts1_travel_bundle(sealed);
 const auto& row=vacation_row(state,family);
 if(!sealed.travel||sealed.travel->protocol!=odh::Ts1TravelProtocol::original_6e598||
    sealed.travel->neighborhood_identity!=state.neighborhood_identity||sealed.travel->family_chunk_id!=family)
  throw odh::FormatError("vacation phase source family or neighborhood changed");
 return {kind,family,destination,row.family_identity,head,odh::family_vacation_digest(state),*row.current_word,
  kind==odh::FamilyVacationPhaseKind::set_before_avatars?static_cast<std::uint32_t>(destination):0U};
}
}
PrivateVacationSession::PrivateVacationSession(std::vector<odh::FamilyEntry> source,TravelCheckpointDigest neighborhood,
 std::optional<odh::FamilyOverlay> expected,std::uint16_t family,std::uint16_t home,std::uint8_t mode,
 std::optional<std::filesystem::path> legacy,std::optional<TravelCheckpointDigest> legacy_head,LinuxNeighborhoodStorage storage):
 source_(std::move(source)),neighborhood_(neighborhood),expected_(std::move(expected)),
 overlay_path_(linux_family_overlay_path(neighborhood,storage)),legacy_path_(std::move(legacy)),legacy_head_(legacy_head){
 const auto state=odh::make_family_vacation_state(source_,neighborhood_,expected_?&*expected_:nullptr).snapshot();
 const auto& row=vacation_row(state,family);
 identity_.family_chunk_id=family;identity_.home_house=home;identity_.home_mode=mode;
 identity_.serialized_family_number=row.serialized_family_number;identity_.family_identity=row.family_identity;
 archive_directory_=overlay_path_.parent_path()/"sessions"/vacation_hex(neighborhood_);
 if(legacy_head_&&!legacy_path_)throw odh::FormatError("private vacation legacy head has no source path");
 if(expected_&&expected_->family_vacation){
  if(const auto* known=odh::family_vacation_private_head(*expected_->family_vacation,family)){
   auto key=*known;key.checkpoint_sha256=identity_.checkpoint_sha256;
   if(key.family_identity!=identity_.family_identity||key.home_house!=home||key.home_mode!=mode||key.serialized_family_number!=identity_.serialized_family_number)
    throw odh::FormatError("private vacation checkpoint identity changed");
  }
 }
 verify_current();
}
bool PrivateVacationSession::adopted() const noexcept {
 return expected_&&expected_->family_vacation&&odh::family_vacation_private_head(*expected_->family_vacation,identity_.family_chunk_id);
}
odh::FamilyVacationOverlayState PrivateVacationSession::vacation_overlay() const {
 if(expected_&&expected_->family_vacation)return *expected_->family_vacation;
 odh::FamilyVacationOverlayState out;out.state=odh::make_family_vacation_state(source_,neighborhood_,expected_?&*expected_:nullptr).snapshot();return out;
}
std::optional<TravelCheckpointDigest> PrivateVacationSession::head() const {
 if(adopted())return odh::family_vacation_private_head(*expected_->family_vacation,identity_.family_chunk_id)->checkpoint_sha256;
 return legacy_head_;
}
std::optional<std::filesystem::path> PrivateVacationSession::checkpoint_path() const {
 if(adopted())return archive_directory_/(vacation_hex(*head())+".odhs");
 return legacy_path_;
}
void PrivateVacationSession::verify_current() const {
 if(!same_vacation_overlay(read_linux_family_overlay(overlay_path_),expected_))throw odh::FormatError("private vacation whole-overlay head changed");
}
std::vector<std::uint8_t> PrivateVacationSession::read_current() const {
 verify_current();const auto path=checkpoint_path();const auto expected=head();
 if(!path||!expected)throw odh::FormatError("private vacation session has no accepted head");
 auto bytes=read_private_head(*path,*expected,{});
 if(!bytes)throw odh::FormatError("private vacation immutable head is missing");
 verify_current();return std::move(*bytes);
}
void PrivateVacationSession::validate_restore(const std::vector<std::uint8_t>& bytes) const {
 verify_current();if(!head()||odh::sha256(bytes.data(),bytes.size())!=*head())throw odh::FormatError("private vacation restore is not its accepted head");
 const auto saved=odh::decode_session_bundle(bytes);
 if(saved.family_lease||saved.host.family_chunk_id!=identity_.family_chunk_id||saved.host.family_number!=identity_.serialized_family_number||
    saved.host.house_number!=identity_.home_house||saved.host.neighborhood_mode!=identity_.home_mode)
  throw odh::FormatError("private vacation checkpoint changed its unmanaged home identity");
}
void PrivateVacationSession::commit_overlay(const odh::FamilyOverlay& replacement){
 durability_uncertain_=false;
 try{compare_exchange_linux_family_overlay_atomic(overlay_path_,expected_,replacement);}
 catch(...){const auto error=std::current_exception();
  try{if(!same_vacation_overlay(read_linux_family_overlay(overlay_path_),replacement))std::rethrow_exception(error);}
  catch(...){std::rethrow_exception(error);}durability_uncertain_=true;
 }
 expected_=replacement;legacy_head_.reset();legacy_path_.reset();
}
void PrivateVacationSession::adopt_legacy_home(){
 verify_current();
 if(adopted())return;
 if(!expected_||!expected_->family_vacation||!legacy_path_||!legacy_head_)
  throw odh::FormatError("legacy vacation head adoption requires an existing shared bank and accepted private source");
 const auto replay=odh::replay_family_overlay(source_,neighborhood_,*expected_);
 for(const auto& state:replay.states)if(state.family_chunk_id==identity_.family_chunk_id&&state.managed)
  throw odh::FormatError("legacy private head cannot replace a managed family checkpoint");
 PrivateTravelLock lock(*legacy_path_);const auto bytes=read_current();validate_restore(bytes);
 const auto saved=odh::decode_session_bundle(bytes);
 if(saved.travel||saved.travel_home_session||saved.travel_visited_session)
  throw odh::FormatError("legacy private active travel requires an explicit source phase, not HOME adoption");
 auto owned=vacation_overlay();const auto& row=vacation_row(owned.state,identity_.family_chunk_id);
 if(!row.seed_word||row.current_word!=row.seed_word)
  throw odh::FormatError("legacy private HOME cannot replace changed booking state");
 if(saved.family_vacation){const auto& old=vacation_row(*saved.family_vacation,identity_.family_chunk_id);
  if(old.family_identity!=row.family_identity||old.seed_word!=row.seed_word||old.current_word!=row.current_word||
     old.fami_version!=row.fami_version||old.serialized_family_number!=row.serialized_family_number)
   throw odh::FormatError("legacy HOME booking metadata does not match current own-family row");
 }
 if(saved.family_spells&&(saved.family_spells->family_chunk_id!=row.family_chunk_id||
    saved.family_spells->family_number!=row.serialized_family_number||saved.family_spells->extension_dword!=*row.seed_word))
  throw odh::FormatError("legacy HOME spell metadata cannot authorize changed booking state");
 auto pointer=identity_;pointer.checkpoint_sha256=*legacy_head_;odh::set_family_vacation_private_head(owned,pointer);
 const auto replacement=odh::plan_family_vacation_overlay(source_,neighborhood_,&*expected_,owned);
 (void)odh::encode_family_overlay(replacement);vacation_directory(archive_directory_);
 (void)publish_session_archive_immutable(archive_directory_,odh::decode_session_archive(bytes));commit_overlay(replacement);
}
std::filesystem::path PrivateVacationSession::publish(odh::SessionBundle bundle){
 verify_current();auto owned=vacation_overlay();
 if(bundle.family_lease||bundle.host.family_chunk_id!=identity_.family_chunk_id||bundle.host.family_number!=identity_.serialized_family_number||
    bundle.host.house_number!=identity_.home_house||bundle.host.neighborhood_mode!=identity_.home_mode||!bundle.family_vacation||
    odh::encode_family_vacation_snapshot(*bundle.family_vacation)!=odh::encode_family_vacation_snapshot(owned.state))
  throw odh::FormatError("private vacation publication changed its home identity or current shared bank");
 std::unique_ptr<PrivateTravelLock> legacy_lock;
 if(!adopted()&&legacy_path_){legacy_lock=std::make_unique<PrivateTravelLock>(*legacy_path_);
  (void)read_private_head(*legacy_path_,legacy_head_,{});verify_current();}
 if(bundle.travel_visited_session){
  if(!bundle.travel_home_session||!head())throw odh::FormatError("private vacation visit has no accepted suspended HOME source");
  const auto previous_bytes=read_current();const auto previous=odh::decode_session_bundle(previous_bytes);
  const auto& immutable_home=previous.travel_home_session?*previous.travel_home_session:previous_bytes;
  if(*bundle.travel_home_session!=immutable_home)throw odh::FormatError("private vacation visit substituted immutable suspended HOME");
 }
 const auto bytes=odh::encode_session_bundle(bundle);const auto digest=odh::sha256(bytes.data(),bytes.size());
 auto pointer=identity_;pointer.checkpoint_sha256=digest;odh::set_family_vacation_private_head(owned,pointer);
 odh::clear_family_vacation_phase(owned,identity_.family_chunk_id);
 auto replacement=odh::plan_family_vacation_overlay(source_,neighborhood_,expected_?&*expected_:nullptr,owned);
 if(expected_&&expected_->personal_state&&!bundle.personal_state)throw odh::FormatError("private vacation save omitted shared personal state");
 if(expected_&&expected_->type_attributes&&!bundle.type_attributes)throw odh::FormatError("private vacation save omitted shared type attributes");
 if(bundle.personal_state)replacement=odh::plan_neighborhood_personal_overlay(neighborhood_,&replacement,*bundle.personal_state);
 if(bundle.type_attributes)replacement=odh::plan_neighborhood_type_attributes_overlay(neighborhood_,&replacement,*bundle.type_attributes);
 (void)odh::encode_family_overlay(replacement);vacation_directory(archive_directory_);
 const auto path=publish_session_archive_immutable(archive_directory_,odh::decode_session_archive(bytes));
 commit_overlay(replacement);return path;
}
void PrivateVacationSession::publish_phase(const odh::FamilyVacationPhase& phase,const odh::FamilyVacationSnapshot& preview){
 verify_current();if(phase.family_chunk_id!=identity_.family_chunk_id||phase.family_identity!=identity_.family_identity||!head()||phase.source_head!=*head())
  throw odh::FormatError("private vacation phase differs from accepted source head");
 auto owned=vacation_overlay();
 if(odh::family_vacation_phase_matches(owned,phase)){
  if(odh::encode_family_vacation_snapshot(preview)!=odh::encode_family_vacation_snapshot(owned.state))
   throw odh::FormatError("idempotent private vacation phase cannot change its accepted bank");
  return;
 }
 auto checked=odh::make_family_vacation_state(source_,neighborhood_,expected_?&*expected_:nullptr);
 const auto exact=checked.prepare(phase);
 if(odh::encode_family_vacation_snapshot(exact.preview_snapshot())!=odh::encode_family_vacation_snapshot(preview))
  throw odh::FormatError("private vacation preview is not its exact prepared phase");
 std::unique_ptr<PrivateTravelLock> legacy_lock;
 if(!adopted()&&legacy_path_)legacy_lock=std::make_unique<PrivateTravelLock>(*legacy_path_);
 const auto bytes=read_current();validate_restore(bytes);
 owned.state=preview;odh::set_family_vacation_phase(owned,phase);
 auto pointer=identity_;pointer.checkpoint_sha256=phase.source_head;odh::set_family_vacation_private_head(owned,pointer);
 auto replacement=odh::plan_family_vacation_overlay(source_,neighborhood_,expected_?&*expected_:nullptr,owned);
 (void)odh::encode_family_overlay(replacement);vacation_directory(archive_directory_);
 (void)publish_session_archive_immutable(archive_directory_,odh::decode_session_archive(bytes));commit_overlay(replacement);
}
odh::FamilyVacationPhase prepare_vacation_entry_phase(const odh::SessionBundle& sealed,const TravelCheckpointDigest& head,
 const odh::FamilyVacationSnapshot& current){
 odh::validate_ts1_travel_bundle(sealed);
 const auto active=sealed.travel_visited_session?odh::decode_session_bundle(*sealed.travel_visited_session):sealed;
 if(!active.travel||!active.travel->pending||active.travel->protocol!=odh::Ts1TravelProtocol::original_6e598)
  throw odh::FormatError("vacation entry requires the sealed original Generic17 source");
 const auto& request=*active.travel->pending;
 if(!request.original||request.departure_transit!=1||request.original->source_temps[2]!=1||request.destination<40||request.destination>49)
  throw odh::FormatError("vacation entry lacks original family1/destination source phase");
 const auto& context=request.source_frame.context;
 const auto it=std::find_if(active.world.entities.begin(),active.world.entities.end(),[&](const auto& e){return e.id==context.caller;});
 if(it==active.world.entities.end()||it->generation!=context.caller_generation||!context.entity_references_bound||
    it->kind!=odh::EntityKind::avatar||it->dead||it->ui_ghost||it->person_data.size()<=61||!it->person_data[61])
  throw odh::FormatError("vacation entry actual CallerPD61 source unavailable");
 const auto family=static_cast<std::uint16_t>(it->person_data[61]);
 return vacation_phase(sealed,head,current,odh::FamilyVacationPhaseKind::set_before_avatars,family,static_cast<std::uint16_t>(request.destination));
}
odh::FamilyVacationPhase prepare_vacation_clear_phase(const odh::SessionBundle& sealed,const TravelCheckpointDigest& head,
 const odh::FamilyVacationSnapshot& current,std::uint16_t family,std::uint16_t home,odh::OriginalLotClass actual_class){
 odh::validate_ts1_travel_bundle(sealed);
 const auto& travel=sealed.travel;
 if(!travel||!travel->pending||travel->current_house==travel->home_house||!family||travel->home_house!=home||
    (travel->pending->destination!=-1&&travel->pending->destination!=home)||actual_class!=odh::OriginalLotClass::residential)
  throw odh::FormatError("vacation clear lacks authenticated frontend residential return phase");
 return vacation_phase(sealed,head,current,odh::FamilyVacationPhaseKind::clear_residential_load,family,home);
}

}
