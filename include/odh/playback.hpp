#pragma once
#include "odh/animation.hpp"
#include "odh/world.hpp"
#include "odh/avatar_hands.hpp"
#include <deque>
#include <memory>

namespace odh {
using AnimationResolver = std::function<std::shared_ptr<const AnimationHeader>(const VmFrame&,std::uint32_t source,std::uint16_t id)>;
using AnimationResetResolver = std::function<std::shared_ptr<const AnimationHeader>(const VmFrame&,std::int16_t normalized_posture)>;
struct PlaybackLimits {std::size_t events=4096;std::uint32_t frames=1048576;std::uint64_t ticks=1048576;};
struct PlaybackEvent {std::uint64_t tick;std::int16_t actor;std::int32_t time_id;AnimationProperties properties;};
// Immutable resource and actor/run identity for a render sample. The shared clip
// remains alive independently of controller cancellation. Coordinates/poses are
// not inferred here; callers bind the sampled actor to their World graphics.
struct PlaybackSample {
    std::shared_ptr<const AnimationHeader> clip;
    std::int16_t actor;
    std::uint64_t actor_generation,vm_generation;
    float frame;
    std::uint32_t frames;
    bool end,loop;
    AvatarHands hands;
};
struct PlaybackProgress {std::string name;float frame;std::uint32_t frames;std::size_t queued_events;bool end;bool loop=false;};
// Silent primary animation controller. Resolver supplies explicit immutable
// resource identity, including posture reset clips. No skeleton, render, audio,
// hand gesture or carry backend is implied.
class Playback {
public:
    Playback(World& world,AnimationResolver resolver,PlaybackLimits limits={},AnimationResetResolver reset_resolver={}):
        world_(world),resolver_(std::move(resolver)),limits_(limits),reset_resolver_(std::move(reset_resolver)) {}
    Playback(const Playback&)=delete;
    Playback& operator=(const Playback&)=delete;
    bool bind(Vm&);
    VmPrimitive primitive();
    // At each consecutive tick, deliver prior queued event/end first, then
    // advance frames. Host resumes VM after this call. One actor per controller.
    bool tick(std::uint64_t,const std::function<void(const PlaybackEvent&)>& observed={});
    void cancel(); // Explicit host cancellation clears clip and any matching wait (false).
    std::optional<PlaybackProgress> progress() const;
    // Read-only. Rejects stale actor/run/wait identity before the next tick;
    // terminal nonblocking loops remain valid while their VM run is unchanged.
    std::optional<PlaybackSample> sample() const;
    // Persists after clip completion/cancellation until another clip starts.
    // Rejects stale actor/run identity; does not derive gestures from raw banks.
    std::optional<AvatarHands> hands() const;
    const std::string& diagnostic() const {return diagnostic_;}
private:
    struct Wait {std::uint64_t ticket;VmFrameBank bank;std::size_t index;std::uint8_t expected;};
    struct State {
        std::shared_ptr<const AnimationHeader> clip;
        std::int16_t actor;std::uint64_t actor_generation,vm_generation;
        float frame,speed;std::uint32_t frames;bool backwards,end=false,loop=false;
        std::vector<AnimationTimeProperty> events;std::size_t low=0,high=0;
        std::deque<std::int16_t> queue;std::uint16_t events_run=0;std::uint64_t ticks=0;
    };
    PrimitiveOutcome request(const VmFrame&,const Instruction&,const VmDispatch&);
    std::optional<State> prepare(std::shared_ptr<const AnimationHeader>,const Entity&,bool backwards,bool loop,bool hurryable,std::string& error) const;
    bool deliver();
    World& world_;AnimationResolver resolver_;PlaybackLimits limits_;
    AnimationResetResolver reset_resolver_;
    Vm* vm_=nullptr;std::optional<State> state_;std::optional<Wait> wait_;
    struct HandsState {AvatarHands value;std::int16_t actor;std::uint64_t actor_generation,vm_generation;};
    std::optional<HandsState> hands_;
    std::optional<std::uint64_t> last_tick_;std::string diagnostic_;bool executing_=false;
};
}
