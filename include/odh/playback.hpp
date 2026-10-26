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
struct PlaybackEvent {
    std::uint64_t tick;std::int16_t actor;std::int32_t time_id;AnimationProperties properties;
    std::uint64_t actor_generation=0,vm_generation=0;
};
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
// Resolver input captured from opcode 44, including the VM/object context and
// actor age that can select private tables or child/adult variants. Reset
// animations also retain the normalized posture passed to
// AnimationResetResolver; ordinary and carry clips retain the resolved source/id.
struct PlaybackResourceKey {
    enum class Kind : std::uint8_t { animation, posture_reset };
    Kind kind=Kind::animation;
    std::uint32_t source=0;
    std::uint16_t id=0;
    std::int16_t posture=0;
    std::int16_t caller=0,callee=0,stack_object=0,actor_age=0;
    std::uint32_t code_owner=0;
    bool operator==(const PlaybackResourceKey& other) const {
        return kind==other.kind&&source==other.source&&id==other.id&&posture==other.posture&&
               caller==other.caller&&callee==other.callee&&stack_object==other.stack_object&&
               actor_age==other.actor_age&&code_owner==other.code_owner;
    }
};
using PlaybackRestoreResolver=std::function<std::shared_ptr<const AnimationHeader>(const PlaybackResourceKey&)>;
struct PlaybackWaitSnapshot {std::uint64_t ticket=0;VmFrameBank bank=VmFrameBank::arguments;std::size_t index=0;std::uint8_t expected=0;};
struct PlaybackClipSnapshot {
    PlaybackResourceKey resource;
    std::int16_t actor=0;std::uint64_t actor_generation=0,vm_generation=0;
    float frame=0,speed=0;std::uint32_t frames=0;bool backwards=false,end=false,loop=false;
    std::size_t low=0,high=0;std::deque<std::int16_t> queue;
    std::uint16_t events_run=0;std::uint64_t ticks=0;
    std::optional<PlaybackWaitSnapshot> wait;
};
struct PlaybackHandsSnapshot {
    AvatarHands value;std::int16_t actor=0;std::uint64_t actor_generation=0,vm_generation=0;
};
struct PlaybackSnapshot {
    std::optional<PlaybackClipSnapshot> state;
    std::optional<PlaybackResourceKey> carry;
    std::optional<PlaybackHandsSnapshot> hands;
    std::optional<std::uint64_t> last_tick;
};
// Silent animation controller. Resolver supplies explicit immutable resource
// identity, including posture reset clips. Carry mode retains its resource,
// composed by the renderer at the native carry state's static frame0.
// Imported nonzero-frame carry states need a separate provenance profile.
// Sound properties are optional presentation
// notifications: the observer decides whether to submit them. Playback owns no
// audio resources and persists only event cursors, never active sounds.
class Playback {
public:
    Playback(World& world,AnimationResolver resolver,PlaybackLimits limits={},AnimationResetResolver reset_resolver={}):
        world_(world),resolver_(std::move(resolver)),limits_(limits),reset_resolver_(std::move(reset_resolver)) {}
    Playback(const Playback&)=delete;
    Playback& operator=(const Playback&)=delete;
    bool bind(Vm&);
    bool capture_snapshot(PlaybackSnapshot&,std::string* error=nullptr) const;
    bool restore_snapshot(const PlaybackSnapshot&,Vm&,const PlaybackRestoreResolver&,std::string* error=nullptr);
    VmPrimitive primitive();
    // At each consecutive tick, deliver prior queued event/end first, then
    // advance frames. Host resumes VM after this call. One actor per controller.
    bool tick(std::uint64_t,const std::function<void(const PlaybackEvent&)>& observed={});
    void cancel(); // Explicit host cancellation clears clip and any matching wait (false).
    std::optional<PlaybackProgress> progress() const;
    // Resolved carry resource retained by AnimMode 2; the renderer
    // composes this secondary pose after the ordinary animation at frame0.
    std::shared_ptr<const AnimationHeader> carry_clip() const {return carry_clip_;}
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
        PlaybackResourceKey resource;
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
    std::shared_ptr<const AnimationHeader> carry_clip_;
    std::optional<PlaybackResourceKey> carry_resource_;
    struct HandsState {AvatarHands value;std::int16_t actor;std::uint64_t actor_generation,vm_generation;};
    std::optional<HandsState> hands_;
    std::optional<std::uint64_t> last_tick_;std::string diagnostic_;bool executing_=false;
};
}
