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
// Refuse links without changing permissions of an existing user directory.
// Flush the parent after mkdir so the archive's durable namespace survives too.
void ensure_directory(const std::filesystem::path& path) {
    if (::mkdir(path.c_str(), 0700) != 0 && errno != EEXIST)
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
        std::uint16_t house, std::uint16_t family_id)
    : source_(std::move(source)), identity_(identity), expected_(std::move(expected)),
      overlay_path_(linux_family_overlay_path(identity)) {
    if (!expected_) throw odh::FormatError("family session journal requires managed player state");
    const auto replay = odh::replay_family_overlay(source_, identity_, *expected_);
    const auto found = std::find_if(replay.states.begin(), replay.states.end(),
        [&](const auto& state) { return state.family_chunk_id == family_id; });
    if (found == replay.states.end() || !found->managed || house == 0 || house > 98 || found->house_number != house)
        throw odh::FormatError("family session journal requires the current managed occupancy");
    state_ = *found;
    const auto family = std::find_if(replay.families.begin(), replay.families.end(),
        [&](const auto& value) { return value.chunk_id == family_id; });
    family_number_ = family->family.family_number;
    member_guids_ = family->family.member_guids;
    std::sort(member_guids_.begin(), member_guids_.end());
    archive_directory_ = overlay_path_.parent_path() / "sessions" / hex(identity_);
    verify_current();
}
odh::SessionFamilyLease FamilySessionJournal::lease() const {
    return {identity_, state_.family_identity, state_.occupancy_epoch, state_.family_chunk_id};
}
std::optional<std::filesystem::path> FamilySessionJournal::checkpoint_path() const {
    if (!state_.checkpoint_sha256 || state_.checkpoint_house != state_.house_number ||
        state_.checkpoint_epoch != state_.occupancy_epoch) return std::nullopt;
    return archive_directory_ / (hex(*state_.checkpoint_sha256) + ".odhs");
}
void FamilySessionJournal::verify_current() const {
    if (!same(read_linux_family_overlay(overlay_path_), expected_))
        throw std::runtime_error("family session player state changed; reopen the neighborhood");
}
void FamilySessionJournal::validate_bundle(const odh::SessionBundle& bundle) const {
    if (!bundle.family_lease || !same(*bundle.family_lease, lease()) ||
        bundle.host.house_number != state_.house_number || bundle.host.family_chunk_id != state_.family_chunk_id ||
        bundle.host.family_number != family_number_ || !bundle.host.family_budget || *bundle.host.family_budget < 0)
        throw odh::FormatError("session archive does not match the managed family occupancy");
    std::vector<std::uint32_t> guids;
    for (const auto& member : bundle.household_roster) guids.push_back(member.guid);
    std::sort(guids.begin(), guids.end());
    if (guids != member_guids_)
        throw odh::FormatError("session archive roster differs from the managed family");
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
std::filesystem::path FamilySessionJournal::publish(odh::SessionBundle bundle) {
    durability_uncertain_ = false;
    verify_current();
    if (state_.checkpoint_sha256 && !checkpoint_path())
        throw odh::FormatError("family move requires portable Sim carryover before a new lot can be saved");
    if (bundle.family_lease && !same(*bundle.family_lease, lease()))
        throw odh::FormatError("cannot publish a session from another family occupancy");
    bundle.family_lease = lease();
    validate_bundle(bundle);
    const auto encoded = odh::encode_session_bundle(bundle);
    const auto digest = odh::sha256(encoded.data(), encoded.size());
    ensure_directory(archive_directory_.parent_path());
    ensure_directory(archive_directory_);
    const auto path = publish_session_archive_immutable(archive_directory_, odh::decode_session_archive(encoded));
    const bool unchanged = checkpoint_path() && state_.checkpoint_sha256 == digest &&
        state_.budget == *bundle.host.family_budget;
    const auto replacement = unchanged ? *expected_ : odh::plan_family_checkpoint_overlay(source_, identity_, &*expected_,
        state_.family_chunk_id, state_.house_number, state_.occupancy_epoch, *bundle.host.family_budget, digest);
    try {
        compare_exchange_linux_family_overlay_atomic(overlay_path_, expected_, replacement);
    } catch (...) {
        // Rename may have committed before directory fsync reported an error.
        // Equality with this exact replacement is the only accepted ambiguity.
        const auto original = std::current_exception();
        try { if (!same(read_linux_family_overlay(overlay_path_), replacement)) std::rethrow_exception(original); }
        catch (...) { std::rethrow_exception(original); }
        durability_uncertain_ = true;
    }
    expected_ = replacement;
    state_.budget = *bundle.host.family_budget;
    state_.checkpoint_sha256 = digest;
    state_.checkpoint_house = state_.house_number;
    state_.checkpoint_epoch = state_.occupancy_epoch;
    return path;
}
} // namespace odh_host
