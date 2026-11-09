#include "linux_family_session_journal.hpp"
#include "linux_family_overlay_store.hpp"
#include "linux_session_archive_store.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <cerrno>
#include <exception>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace odh_host {
namespace {
std::string hex(const std::array<std::uint8_t, 32>& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
bool same(const std::optional<odh::FamilyOverlay>& a, const std::optional<odh::FamilyOverlay>& b) {
    return a.has_value() == b.has_value() && (!a || odh::encode_family_overlay(*a) == odh::encode_family_overlay(*b));
}
bool same(const odh::SessionFamilyLease& a, const odh::SessionFamilyLease& b) {
    return a.neighborhood_identity == b.neighborhood_identity && a.family_identity == b.family_identity &&
        a.family_chunk_id == b.family_chunk_id && a.occupancy_epoch == b.occupancy_epoch;
}
bool same(const odh::HouseholdTransferTicket& a, const odh::HouseholdTransferTicket& b) {
    return same(a.source_lease, b.source_lease) && a.source_house == b.source_house &&
        a.family_number == b.family_number && a.checkpoint_budget == b.checkpoint_budget &&
        a.archive_sha256 == b.archive_sha256;
}
bool same_identity(const odh::SessionHouseholdMember& a, const odh::SessionHouseholdMember& b) {
    return a.guid == b.guid && a.neighbor_id == b.neighbor_id &&
        a.object_definition_id == b.object_definition_id && a.character_asset == b.character_asset &&
        a.character_fingerprint == b.character_fingerprint;
}
// Refuse links without changing permissions of an existing user directory.
// Flush the parent after mkdir so the archive's durable namespace survives too.
void ensure_directory(const std::filesystem::path& path) {
    int created=::mkdir(path.c_str(),0700);
    if(created!=0&&errno==ENOENT){
        // Original adoption may be the profile's FIRST player-state write.
        // A move-in used to create these parents before the first checkpoint.
        // Create only missing parents, retaining the same no-follow directory
        // verification and durability checks at every level we create.
        const auto parent=path.parent_path();
        if(parent.empty()||parent==path)throw std::runtime_error("family session archive has no existing parent");
        ensure_directory(parent);
        created=::mkdir(path.c_str(),0700);
    }
    if(created!=0&&errno!=EEXIST)
        throw std::runtime_error("cannot create family session archive directory");
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) throw std::runtime_error("family session archive path is not a real directory");
    ::close(fd);
    const int parent = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (parent < 0) throw std::runtime_error("cannot open family session directory parent");
    int result;
    do { result = ::fsync(parent); } while (result != 0 && errno == EINTR);
    ::close(parent);
    if (result != 0) throw std::runtime_error("cannot flush family session directory parent");
}
}

FamilySessionJournal::FamilySessionJournal(std::vector<odh::FamilyEntry> source,
        std::array<std::uint8_t, 32> identity, std::optional<odh::FamilyOverlay> expected,
        std::uint16_t house, std::uint16_t family_id, std::optional<odh::FamilyOverlay> pending_move_in,
        LinuxNeighborhoodStorage storage)
    :FamilySessionJournal(std::move(source),identity,std::move(expected),house,family_id,
        std::move(pending_move_in),true,storage) {}
FamilySessionJournal::FamilySessionJournal(std::vector<odh::FamilyEntry> source,
        std::array<std::uint8_t,32> identity,std::optional<odh::FamilyOverlay> expected,
        std::uint16_t house,std::uint16_t family_id,std::optional<odh::FamilyOverlay> pending_move_in,bool verify_disk,
        LinuxNeighborhoodStorage storage)
    : source_(std::move(source)), identity_(identity), expected_(std::move(expected)),
      pending_move_in_(std::move(pending_move_in)),
      overlay_path_(linux_family_overlay_path(identity,storage)) {
    if (pending_move_in_) {
        const auto old_count = expected_ ? expected_->assignments.size() : 0;
        if (pending_move_in_->assignments.size() != old_count + 1)
            throw odh::FormatError("pending purchase must append exactly one move-in");
        const auto planned = odh::plan_family_managed_move_in_overlay(source_, identity_,
            expected_ ? &*expected_ : nullptr, family_id, house,
            pending_move_in_->assignments.back().purchase_price);
        if (!same(pending_move_in_, planned))
            throw odh::FormatError("pending purchase differs from the exact managed move-in plan");
    }
    if (!runtime_overlay()) throw odh::FormatError("family session journal requires managed player state");
    const auto replay = odh::replay_family_overlay(source_, identity_, *runtime_overlay());
    const auto found = std::find_if(replay.states.begin(), replay.states.end(),
        [&](const auto& state) { return state.family_chunk_id == family_id; });
    if (found == replay.states.end() || !found->managed || house == 0 || house > 98 || found->house_number != house)
        throw odh::FormatError("family session journal requires the current managed occupancy");
    state_ = *found;
    const auto family = std::find_if(replay.families.begin(), replay.families.end(),
        [&](const auto& value) { return value.chunk_id == family_id; });
    if(family==replay.families.end())throw odh::FormatError("managed occupancy lost its retained CurrentFamily object");
    family_number_ = family->family.family_number;
    member_guids_ = family->family.member_guids;
    std::sort(member_guids_.begin(), member_guids_.end());
    archive_directory_ = overlay_path_.parent_path() / "sessions" / hex(identity_);
    if(verify_disk)verify_current();
}
FamilySessionJournal FamilySessionJournal::adopt_original_session(std::vector<odh::FamilyEntry> source,
        std::array<std::uint8_t,32> identity,std::optional<odh::FamilyOverlay> expected,
        std::uint16_t house,std::uint16_t family_id,odh::SessionBundle capture,
        const std::function<void(const odh::SessionBundle&,const std::vector<std::uint8_t>&)>& validate_reopen, LinuxNeighborhoodStorage storage,std::optional<std::int32_t> property_value) {
    if(property_value&&*property_value<0)throw odh::FormatError("original adoption property value is negative");
    if(!validate_reopen||!capture.host.family_budget)
        throw odh::FormatError("original adoption requires actual capture and native restore validation");
    // Obtain the existing source occupancy, not a guessed new epoch. Planning
    // and lease assignment below use only replayed immutable source facts.
    const auto baseline=expected?*expected:odh::FamilyOverlay{identity,{}};
    const auto replay=odh::replay_family_overlay(source,identity,baseline);
    const auto current=std::find_if(replay.states.begin(),replay.states.end(),
        [&](const auto& state){return state.family_chunk_id==family_id;});
    if(current==replay.states.end())throw odh::FormatError("original adoption family is absent");
    const odh::SessionFamilyLease lease{identity,current->family_identity,current->occupancy_epoch,family_id};
    if(capture.family_lease&&!same(*capture.family_lease,lease))
        throw odh::FormatError("original adoption capture has another occupancy lease");
    capture.family_lease=lease;
    const auto encoded=odh::encode_session_bundle(capture);
    (void)odh::decode_session_bundle(encoded); // Full canonical native validation.
    const auto digest=odh::sha256(encoded.data(),encoded.size());
    auto adopted=odh::plan_family_original_adoption_overlay(source,identity,
        expected?&*expected:nullptr,family_id,house,*capture.host.family_budget,digest,property_value);
    if(expected&&expected->type_attributes&&!capture.type_attributes)
        throw odh::FormatError("original adoption cannot omit the current shared neighborhood type attributes");
    if(expected&&expected->personal_state&&!capture.personal_state)
        throw odh::FormatError("original adoption cannot omit shared personal state");
    if(capture.character_family)adopted=odh::plan_neighborhood_character_family_overlay(source,identity,&adopted,capture.character_family->shared);
    if(capture.personal_state)adopted=odh::plan_neighborhood_personal_overlay(identity,&adopted,*capture.personal_state);
    if(capture.type_attributes)
        adopted=odh::plan_neighborhood_type_attributes_overlay(identity,&adopted,*capture.type_attributes);
    if(capture.family_vacation){
        odh::FamilyVacationOverlayState vacation;
        if(expected&&expected->family_vacation)vacation=*expected->family_vacation;
        vacation.state=*capture.family_vacation;odh::clear_family_vacation_phase(vacation,family_id);
        adopted=odh::plan_family_vacation_overlay(source,identity,&adopted,vacation);
    }
    (void)odh::encode_family_overlay(adopted); // Budget/type validation before any archive publication.
    FamilySessionJournal journal(std::move(source),identity,adopted,house,family_id,std::nullopt,false,storage);
    journal.validate_bundle(capture);
    validate_reopen(capture,encoded);
    // The complete validated archive and its real digest are ready before any
    // filesystem mutation. A stale external head never becomes our authority.
    journal.expected_=std::move(expected);
    journal.verify_current();
    ensure_directory(journal.archive_directory_.parent_path());
    ensure_directory(journal.archive_directory_);
    const auto path=publish_session_archive_immutable(journal.archive_directory_,odh::decode_session_archive(encoded));
    if(path!=*journal.checkpoint_path())
        throw odh::FormatError("original adoption archive path differs from its exact digest");
    journal.commit_overlay(adopted);
    return journal;
}
odh::SessionFamilyLease FamilySessionJournal::lease() const {
    return {identity_, state_.family_identity, state_.occupancy_epoch, state_.family_chunk_id};
}
std::optional<std::filesystem::path> FamilySessionJournal::checkpoint_path() const {
    if (!state_.checkpoint_sha256 || state_.checkpoint_house != state_.house_number ||
        state_.checkpoint_epoch != state_.occupancy_epoch) return std::nullopt;
    return retained_checkpoint_path();
}
std::optional<std::filesystem::path> FamilySessionJournal::retained_checkpoint_path() const {
    if (!state_.checkpoint_sha256) return std::nullopt;
    return archive_directory_ / (hex(*state_.checkpoint_sha256) + ".odhs");
}
std::optional<odh::HouseholdTransferTicket> FamilySessionJournal::carry_ticket() const {
    if (!state_.checkpoint_sha256) return std::nullopt;
    if (!state_.checkpoint_budget || !state_.checkpoint_epoch || !state_.checkpoint_house)
        throw odh::FormatError("retained checkpoint lacks financial or occupancy provenance");
    auto source_lease = lease();
    source_lease.occupancy_epoch = state_.checkpoint_epoch;
    return odh::HouseholdTransferTicket{source_lease, state_.checkpoint_house, family_number_,
        *state_.checkpoint_budget, *state_.checkpoint_sha256};
}
bool FamilySessionJournal::carry_accepted() const {
    const auto ticket = carry_ticket();
    return ticket && accepted_carry_ && same(*ticket, *accepted_carry_);
}
void FamilySessionJournal::accept_carry(const odh::AppliedHouseholdTransfer& applied) {
    verify_current();
    const auto ticket = carry_ticket();
    if (!ticket || checkpoint_path() || !same(*ticket, applied.ticket()) ||
        !same(lease(), applied.destination_lease()))
        throw odh::FormatError("carry receipt does not match retained ticket and destination occupancy");
    const auto encoded = odh::encode_session_archive(read_session_archive(*retained_checkpoint_path()));
    if (odh::sha256(encoded.data(), encoded.size()) != ticket->archive_sha256)
        throw odh::FormatError("retained carry archive digest changed");
    const auto source = odh::decode_session_bundle(encoded);
    if (!source.family_lease || !same(*source.family_lease, ticket->source_lease) ||
        source.host.house_number != ticket->source_house || source.host.family_number != ticket->family_number ||
        source.host.family_budget != ticket->checkpoint_budget)
        throw odh::FormatError("retained carry archive provenance differs from receipt");
    std::vector<std::uint32_t> guids;
    for (const auto& member : applied.members()) {
        guids.push_back(member.guid);
        const auto original = std::find_if(source.household_roster.begin(), source.household_roster.end(),
            [&](const auto& value) { return value.guid == member.guid; });
        if (original == source.household_roster.end() || !same_identity(*original, member))
            throw odh::FormatError("carry receipt changed neighbor or character resource identity");
    }
    std::sort(guids.begin(), guids.end());
    if (guids != member_guids_ || source.household_roster.size() != guids.size())
        throw odh::FormatError("carry receipt does not cover the complete managed family");
    auto accepted_members = applied.members();
    carry_members_.swap(accepted_members);
    accepted_carry_ = *ticket;
}
void FamilySessionJournal::commit_overlay(const odh::FamilyOverlay& replacement) {
    try {
        compare_exchange_linux_family_overlay_atomic(overlay_path_, expected_, replacement);
    } catch (...) {
        // Rename may precede a failed directory flush. Only an exact reread
        // proves logical publication; it never proves durability or rebases.
        const auto original = std::current_exception();
        try { if (!same(read_linux_family_overlay(overlay_path_), replacement)) std::rethrow_exception(original); }
        catch (...) { std::rethrow_exception(original); }
        durability_uncertain_ = true;
    }
    expected_ = replacement;
}
void FamilySessionJournal::commit_move_in() {
    verify_current();
    if (!pending_move_in_) return;
    if (state_.checkpoint_sha256 && !checkpoint_path() && !carry_accepted())
        throw odh::FormatError("pending purchase requires complete applied household carryover");
    durability_uncertain_ = false;
    commit_overlay(*pending_move_in_);
    pending_move_in_.reset();
}
void FamilySessionJournal::verify_current() const {
    if (!same(read_linux_family_overlay(overlay_path_), expected_))
        throw std::runtime_error("family session player state changed; reopen the neighborhood");
}
void FamilySessionJournal::validate_bundle(const odh::SessionBundle& bundle,bool publishing) const {
    if (!bundle.family_lease || !same(*bundle.family_lease, lease()) ||
        bundle.host.house_number != state_.house_number || bundle.host.family_chunk_id != state_.family_chunk_id ||
        bundle.host.family_number != family_number_ || !bundle.host.family_budget || *bundle.host.family_budget < 0)
        throw odh::FormatError("session archive does not match the managed family occupancy");
    std::vector<std::uint32_t> guids;
    for (const auto& member : bundle.household_roster) guids.push_back(member.guid);
    std::sort(guids.begin(), guids.end());
    if(!bundle.character_family){
        if((runtime_overlay()&&runtime_overlay()->character_family)||guids!=member_guids_)
            throw odh::FormatError("session archive omits current family state or differs from the legacy managed roster");
        return;
    }
    const auto& shared=bundle.character_family->shared;
    if(shared.neighborhood_identity!=identity_)throw odh::FormatError("session character/family neighborhood differs from journal");
    const auto* before=runtime_overlay()&&runtime_overlay()->character_family?&*runtime_overlay()->character_family:nullptr;
    if(before){
        if(publishing)odh::validate_ts1_character_family_successor(*before,shared);
        else odh::validate_ts1_character_family_successor(shared,*before);
    }
    // HHRD owns exact resource-bound resident Threads. FAMI membership is a
    // different source list: Generic6 leaves RuntimeSubset/Thread untouched,
    // and Generic4 may add an avatar with an existing NPCA construction origin.
    // World/VM/constructor identity validation remains in the complete codec.
    const auto* row=odh::find_ts1_family_roster(shared,state_.family_chunk_id);
    if(row&&row->source_identity!=state_.family_identity)throw odh::FormatError("session current roster changes the managed family identity");
    if(!publishing&&before){
        const auto* current=odh::find_ts1_family_roster(*before,state_.family_chunk_id);
        if(bool(row)!=bool(current)||(row&&(row->revision!=current->revision||row->members!=current->members)))
            throw odh::FormatError("accepted session current family roster is stale");
    }
    const auto accepted=runtime_overlay()?*runtime_overlay():odh::FamilyOverlay{identity_,{}};
    if(publishing)(void)odh::plan_neighborhood_character_family_overlay(source_,identity_,&accepted,shared);
}
void FamilySessionJournal::validate_restore(const std::vector<std::uint8_t>& encoded) const {
    verify_current();
    if (!checkpoint_path() || odh::sha256(encoded.data(), encoded.size()) != *state_.checkpoint_sha256)
        throw odh::FormatError("session archive is not the current family checkpoint");
    const auto bundle = odh::decode_session_bundle(encoded);
    validate_bundle(bundle);
    if (*bundle.host.family_budget != state_.budget)
        throw odh::FormatError("session archive budget differs from the committed checkpoint");
}
std::filesystem::path FamilySessionJournal::publish(odh::SessionBundle bundle,
        std::optional<std::int32_t> property_value) {
    if(property_value&&*property_value<0)throw odh::FormatError("checkpoint property value is negative");
    durability_uncertain_ = false;
    verify_current();
    if (pending_move_in_)
        throw odh::FormatError("pending purchase must commit after runtime initialization before saving");
    if (state_.checkpoint_sha256 && !checkpoint_path() && !carry_accepted())
        throw odh::FormatError("family move requires portable Sim carryover before a new lot can be saved");
    if (bundle.family_lease && !same(*bundle.family_lease, lease()))
        throw odh::FormatError("cannot publish a session from another family occupancy");
    bundle.family_lease = lease();
    validate_bundle(bundle,true);
    if (state_.checkpoint_sha256 && !checkpoint_path() &&
        !odh::same_session_household_roster(carry_members_, bundle.household_roster))
        throw odh::FormatError("first destination checkpoint differs from the applied carry roster");
    const auto encoded = odh::encode_session_bundle(bundle);
    const auto digest = odh::sha256(encoded.data(), encoded.size());
    const bool unchanged = checkpoint_path() && state_.checkpoint_sha256 == digest &&
        state_.budget == *bundle.host.family_budget&&
        (!property_value||state_.value_in_arch==*property_value);
    auto replacement = unchanged ? *expected_ : odh::plan_family_checkpoint_overlay(source_, identity_, &*expected_,
        state_.family_chunk_id, state_.house_number, state_.occupancy_epoch, *bundle.host.family_budget, digest,property_value);
    if(bundle.character_family)replacement=odh::plan_neighborhood_character_family_overlay(source_,identity_,&replacement,bundle.character_family->shared);
    if(expected_->type_attributes&&!bundle.type_attributes)
        throw odh::FormatError("checkpoint cannot omit the current shared neighborhood type attributes");
    if(expected_->personal_state&&!bundle.personal_state)
        throw odh::FormatError("checkpoint cannot omit shared personal state");
    if(bundle.personal_state)replacement=odh::plan_neighborhood_personal_overlay(identity_,&replacement,*bundle.personal_state);
    if(bundle.type_attributes)
        replacement=odh::plan_neighborhood_type_attributes_overlay(identity_,&replacement,*bundle.type_attributes);
    if(expected_->family_vacation&&!bundle.family_vacation)
        throw odh::FormatError("checkpoint cannot omit shared family vacation state");
    if(bundle.family_vacation){
        odh::FamilyVacationOverlayState vacation;
        if(expected_->family_vacation)vacation=*expected_->family_vacation;
        vacation.state=*bundle.family_vacation;
        odh::clear_family_vacation_phase(vacation,state_.family_chunk_id);
        replacement=odh::plan_family_vacation_overlay(source_,identity_,&replacement,vacation);
    }
    (void)odh::encode_family_overlay(replacement); // Fail before I/O if combined checkpoint/bank cannot fit.
    ensure_directory(archive_directory_.parent_path());
    ensure_directory(archive_directory_);
    const auto path = publish_session_archive_immutable(archive_directory_, odh::decode_session_archive(encoded));
    commit_overlay(replacement);
    state_.budget = *bundle.host.family_budget;
    if(property_value)state_.value_in_arch=*property_value;
    state_.checkpoint_sha256 = digest;
    state_.checkpoint_house = state_.house_number;
    state_.checkpoint_epoch = state_.occupancy_epoch;
    state_.checkpoint_budget = state_.budget;
    accepted_carry_.reset();
    carry_members_.clear();
    return path;
}
void FamilySessionJournal::publish_vacation_phase(const odh::FamilyVacationPhase& phase,
        const odh::FamilyVacationSnapshot& preview){
    verify_current();
    if(!state_.checkpoint_sha256||phase.source_head!=*state_.checkpoint_sha256||
       phase.family_chunk_id!=state_.family_chunk_id||phase.family_identity!=state_.family_identity)
        throw odh::FormatError("managed vacation phase differs from accepted family checkpoint");
    const auto path=checkpoint_path();
    if(!path)throw odh::FormatError("managed vacation phase has no current source archive");
    const auto bytes=odh::encode_session_archive(read_session_archive(*path));validate_restore(bytes);
    odh::FamilyVacationOverlayState vacation;
    if(expected_->family_vacation)vacation=*expected_->family_vacation;
    if(expected_->family_vacation&&odh::family_vacation_phase_matches(vacation,phase)){
        if(odh::encode_family_vacation_snapshot(preview)!=odh::encode_family_vacation_snapshot(vacation.state))
            throw odh::FormatError("idempotent managed vacation phase cannot change accepted bank");
        return;
    }
    auto checked=odh::make_family_vacation_state(source_,identity_,&*expected_);
    const auto exact=checked.prepare(phase);
    if(odh::encode_family_vacation_snapshot(exact.preview_snapshot())!=odh::encode_family_vacation_snapshot(preview))
        throw odh::FormatError("managed vacation preview is not its exact prepared phase");
    vacation.state=preview;odh::set_family_vacation_phase(vacation,phase);
    const auto replacement=odh::plan_family_vacation_overlay(source_,identity_,&*expected_,vacation);
    (void)odh::encode_family_overlay(replacement);commit_overlay(replacement);
}

} // namespace odh_host
