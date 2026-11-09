// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/session_bundle.hpp"
#include "odh/sha256.hpp"
#include <memory>

namespace odh {
class SessionSourceCheckpoint;
// Expected digest comes from the already accepted journal/private source head.
// Parse exactly the owned bytes whose digest is checked; never substitute the
// current writer's encoding for the serialized identity of an older archive.
inline SessionSourceCheckpoint decode_session_source_checkpoint(std::vector<std::uint8_t>,
    const std::array<std::uint8_t,32>& expected_raw_digest,SessionBundleLimits limits={});
class SessionSourceCheckpoint {
public:
    const SessionBundle& envelope() const{return state().envelope;}
    const SessionBundle& active() const{return state().visited?*state().visited:state().envelope;}
    const std::vector<std::uint8_t>& envelope_bytes() const{return state().archive;}
    const std::vector<std::uint8_t>& active_bytes() const{
        const auto& value=state();
        return value.envelope.travel_visited_session?*value.envelope.travel_visited_session:value.archive;
    }
    const std::array<std::uint8_t,32>& digest() const{return state().digest;}
private:
    struct State {
        std::vector<std::uint8_t> archive;
        std::array<std::uint8_t,32> digest;
        SessionBundle envelope;
        std::optional<SessionBundle> visited;
    };
    explicit SessionSourceCheckpoint(std::shared_ptr<const State> state):state_(std::move(state)){}
    const State& state() const{
        if(!state_)throw FormatError("source checkpoint no longer owns its authenticated bytes");
        return *state_;
    }
    // One immutable state keeps raw/model/digest bound when copied or moved;
    // moved-from readers fail rather than publishing a detached digest.
    std::shared_ptr<const State> state_;
    friend SessionSourceCheckpoint decode_session_source_checkpoint(std::vector<std::uint8_t>,
        const std::array<std::uint8_t,32>&,SessionBundleLimits);
};
inline SessionSourceCheckpoint decode_session_source_checkpoint(std::vector<std::uint8_t> archive,
        const std::array<std::uint8_t,32>& expected_raw_digest,SessionBundleLimits limits){
    if(archive.size()>limits.archive.max_file_bytes)throw FormatError("source checkpoint exceeds archive byte budget");
    const auto digest=sha256(archive.data(),archive.size());
    if(digest!=expected_raw_digest)throw FormatError("source checkpoint bytes differ from accepted raw head");
    auto envelope=decode_session_bundle(archive,std::nullopt,limits);
    std::optional<SessionBundle> visited;
    if(envelope.travel_visited_session)visited=decode_session_bundle(*envelope.travel_visited_session,std::nullopt,limits);
    return SessionSourceCheckpoint(std::make_shared<const SessionSourceCheckpoint::State>(
        SessionSourceCheckpoint::State{std::move(archive),digest,std::move(envelope),std::move(visited)}));
}
} // namespace odh
