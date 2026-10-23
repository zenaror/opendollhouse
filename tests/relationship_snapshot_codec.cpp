#include "odh/relationship_snapshot_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F>
void rejects(F&& operation, const char* message) {
    try { operation(); }
    catch (const odh::FormatError&) { return; }
    require(false, message);
}
odh::RelationshipSnapshot fixture() {
    return {{4,8,1024}, {
        {odh::RelationshipMatrix::neighbor, -7, 0,
            {{-1,{17}}, {70000,std::vector<std::int16_t>(300,-1)}}},
        {odh::RelationshipMatrix::object, 1, 9, {{0,{}}, {65535,{21,22}}}},
        {odh::RelationshipMatrix::object, 1, 10, {}}
    }, 4, 303};
}
}

int main() {
    try {
        const auto snapshot = fixture();
        const auto encoded = odh::encode_relationship_snapshot(snapshot);
        const auto decoded = odh::decode_relationship_snapshot(encoded);
        require(odh::encode_relationship_snapshot(decoded) == encoded,
                "every relationship value round-trips byte-for-byte");

        odh::RelationshipState restored;
        require(restored.restore_snapshot(decoded), "decoded snapshot restores into a fresh state");
        const auto restored_snapshot = restored.snapshot();
        require(odh::encode_relationship_snapshot(restored_snapshot) == encoded,
                "restored state preserves the encoded values and limits");
        require(restored.value(-7,70000,299) == -1,
                "wide neighbor target and value tail survive restoration");

        auto wrong_counts = encoded;
        wrong_counts[26] = 3;
        rejects([&] { (void)odh::decode_relationship_snapshot(wrong_counts); },
                "stored pair count must match the section");
        auto invalid_matrix = encoded;
        invalid_matrix[46] = 9;
        rejects([&] { (void)odh::decode_relationship_snapshot(invalid_matrix); },
                "unknown matrix kind is rejected");
        auto bad_version = encoded;
        bad_version[0] = 2;
        rejects([&] { (void)odh::decode_relationship_snapshot(bad_version); },
                "unsupported version is rejected");

        auto inconsistent = snapshot;
        --inconsistent.value_count;
        rejects([&] { (void)odh::encode_relationship_snapshot(inconsistent); },
                "inconsistent value accounting is rejected");
        auto invalid_source = snapshot;
        invalid_source.sources[1].generation = 0;
        rejects([&] { (void)odh::encode_relationship_snapshot(invalid_source); },
                "object source without generation is rejected");
        auto unordered_sources = snapshot;
        std::reverse(unordered_sources.sources.begin(), unordered_sources.sources.end());
        rejects([&] { (void)odh::encode_relationship_snapshot(unordered_sources); },
                "non-canonical source order is rejected");
        auto unordered_pairs = snapshot;
        std::reverse(unordered_pairs.sources[0].pairs.begin(), unordered_pairs.sources[0].pairs.end());
        rejects([&] { (void)odh::encode_relationship_snapshot(unordered_pairs); },
                "non-canonical pair order is rejected");

        for (std::size_t size = 0; size < encoded.size(); ++size) {
            const std::vector<std::uint8_t> prefix(encoded.begin(),
                encoded.begin() + static_cast<std::ptrdiff_t>(size));
            rejects([&] { (void)odh::decode_relationship_snapshot(prefix); },
                    "every truncated relationship snapshot is rejected");
        }
        auto tight_limits = odh::RelationshipSnapshotCodecLimits{};
        tight_limits.relationships.pairs = 3;
        rejects([&] { (void)odh::decode_relationship_snapshot(encoded, tight_limits); },
                "relationship limits are enforced before restore");
        rejects([&] { (void)odh::encode_relationship_snapshot(snapshot, {{},45}); },
                "encoder enforces byte budget");

        odh::RelationshipState empty;
        const auto empty_bytes = odh::encode_relationship_snapshot(empty.snapshot());
        require(odh::encode_relationship_snapshot(odh::decode_relationship_snapshot(empty_bytes)) == empty_bytes,
                "empty relationship snapshot round-trips");

        std::cout << "Relationship snapshot codec checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
