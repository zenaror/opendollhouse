#pragma once
#include "odh/animation.hpp"
#include "odh/world.hpp"
#include "odh/avatar_hands.hpp"
#include <array>
#include <deque>
#include <memory>

namespace odh {
using AnimationResolver = std::function<std::shared_ptr<const AnimationHeader>(const VmFrame&,std::uint32_t source,std::uint16_t id)>;
using AnimationResetResolver = std::function<std::shared_ptr<const AnimationHeader>(const VmFrame&,std::int16_t normalized_posture)>;
// Explicit actor walk/swim table index from the pinned routing controller.
using RouteAnimationResolver = std::function<std::shared_ptr<const AnimationHeader>(const VmFrame&,std::uint16_t index,bool swimming)>;
// nullopt means unknown; an empty string is a proven empty actor table entry.
using SourceWalkAnimationNameResolver = std::function<std::optional<std::string>(const VmFrame&,std::uint16_t index,bool swimming)>;
struct PlaybackLimits {std::size_t events=4096;std::uint32_t frames=1048576;std::uint64_t ticks=1048576;};
struct PlaybackEvent {
    std::uint64_t tick;std::int16_t actor;std::int32_t time_id;AnimationProperties properties;
    std::uint64_t actor_generation=0,vm_generation=0;
    std::uint8_t layer=0;
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
struct WeightedPlaybackSample {PlaybackSample sample{};float weight=0;};
struct PlaybackSampleCollection {std::array<WeightedPlaybackSample,3> layers{};std::uint8_t count=0;};
struct PlaybackProgress {std::string name;float frame;std::uint32_t frames;std::size_t queued_events;bool end;bool loop=false;};
// Resolver input captured from opcode 44, including the VM/object context and
// actor age that can select private tables or child/adult variants. Reset
// animations also retain the normalized posture passed to
// AnimationResetResolver; ordinary and carry clips retain the resolved source/id.
// Turn clips retain source0 walk/source1 swim and actor table index3..9.
// Locomotion clips use a distinct kind and table indices3/20/21/25.
struct PlaybackResourceKey {
    enum class Kind : std::uint8_t { animation, posture_reset, route_walk, source_locomotion, reach };
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
// Shared exact binary32 weight profiles: source initial values or velocity0..8.
bool source_walk_weights_valid(const std::array<float,3>&) noexcept;
struct PlaybackSourceWalkSnapshot {
    std::array<PlaybackClipSnapshot,2> secondary{};
    std::array<float,3> weights{};
};
struct PlaybackSnapshot {
    std::optional<PlaybackClipSnapshot> state;
    std::optional<PlaybackResourceKey> carry;
    std::optional<PlaybackHandsSnapshot> hands;
    std::optional<std::uint64_t> last_tick;
    // state is source CurrentAnimationState (lane0); secondary lanes retain
    // individual frame/event cursors, but all xevt queues target lane0.
    std::optional<PlaybackSourceWalkSnapshot> source_walk;
};
// Silent animation controller. Resolver supplies explicit immutable resource
// identity, including posture reset clips. Carry mode retains its resource,
// composed by the renderer at the native carry state's static frame0.
// Imported nonzero-frame carry states need a separate provenance profile.
// Sound properties are optional presentation
// notifications: the observer decides whether to submit them. Playback owns no
// audio resources and persists only event cursors, never active sounds.
enum class AnimationAvailability { available, absent, unavailable };
struct AnimationLookup {
    AnimationAvailability availability=AnimationAvailability::unavailable;
    std::shared_ptr<const AnimationHeader> clip;
    std::string diagnostic;
};
using AnimationLookupResolver=std::function<AnimationLookup(const VmFrame&,std::uint32_t source,std::uint16_t id)>;
enum class ResetAnimationAvailability { available, absent, unavailable };
struct ResetAnimationLookup {
    ResetAnimationAvailability availability=ResetAnimationAvailability::unavailable;
    std::shared_ptr<const AnimationHeader> clip;
    std::string diagnostic;
};
using ResetAnimationLookupResolver=std::function<ResetAnimationLookup(const VmFrame&,std::int16_t normalized_posture)>;
class Playback {
public:
    // Optional typed source authority. Legacy nullptr resolvers remain unknown,
    // while verified authored emptiness may complete a reset without a pose.
    void set_reset_lookup_resolver(ResetAnimationLookupResolver resolver){reset_lookup_resolver_=std::move(resolver);}
    // Ordinary opcode44 lookup: only proven authored absence may take the
    // source TRUE_NEXT_TICK exit. Legacy nullable resolvers remain unavailable.
    void set_animation_lookup_resolver(AnimationLookupResolver resolver){animation_lookup_resolver_=std::move(resolver);}

    Playback(World& world,AnimationResolver resolver,PlaybackLimits limits={},AnimationResetResolver reset_resolver={},RouteAnimationResolver route_resolver={},SourceWalkAnimationNameResolver walk_name_resolver={}):
        world_(world),resolver_(std::move(resolver)),limits_(limits),reset_resolver_(std::move(reset_resolver)),route_resolver_(std::move(route_resolver)),walk_name_resolver_(std::move(walk_name_resolver)) {}
    Playback(const Playback&)=delete;
    Playback& operator=(const Playback&)=delete;
    bool bind(Vm&);
    // Host safe-point hooks for the pinned VMAvatar slot contract. Call attach
    // only after a NEW successful slot0 attachment, never an already-in-slot
    // no-op: it always replaces custom carry with reset_resolver(frame,-1).
    // These hooks preserve the primary clip, wait, gestures and event cursors;
    // they neither dispatch the VM nor advance animation. Expected identities
    // belong to the captured source frame and its still-bound execution graph.
    bool attach_default_carry(const VmFrame& source_frame,const Vm& source_vm,
        std::uint64_t actor_generation,std::uint64_t vm_generation,
        std::int16_t carried_object,std::uint64_t carried_generation,std::string* error=nullptr);
    // Source ClearSlot clears carry after ANY avatar slot is cleared, including
    // head/pelvis slots while slot0 remains occupied. Call after World detaches.
    bool clear_slot_carry(const VmFrame& source_frame,const Vm& source_vm,
        std::uint64_t actor_generation,std::uint64_t vm_generation,
        std::size_t cleared_slot,std::string* error=nullptr);
    // Prepared avatar effects for one World slot move. The initiating frame is
    // the real opcode43/46/47 dispatch; the carrier resource context comes from this
    // controller's own bound VM. Combine source clearing and destination slot0
    // reset in one plan when both containers are the same avatar.
    class PreparedCarryChange {
    public:
        PreparedCarryChange(PreparedCarryChange&&) noexcept;
        PreparedCarryChange& operator=(PreparedCarryChange&&) noexcept;
        ~PreparedCarryChange();
        PreparedCarryChange(const PreparedCarryChange&)=delete;
        PreparedCarryChange& operator=(const PreparedCarryChange&)=delete;
        bool can_commit() const noexcept; // Before the World move.
        bool can_publish() const noexcept; // After its exact reciprocal move.
        bool publish() noexcept; // Revalidates, then swaps only carry state.
    private:
        struct Data;std::unique_ptr<Data> data_;
        explicit PreparedCarryChange(std::unique_ptr<Data>) noexcept;
        friend class Playback;
    };
    std::optional<PreparedCarryChange> prepare_slot_carry_change(const PreparedSlotMove&,
        const VmFrame& source_frame,const Vm& source_vm,const Instruction&,const VmDispatch&,
        std::int16_t carrier,std::uint64_t carrier_generation,std::uint64_t carrier_vm_generation,
        std::string* error=nullptr);
    class PreparedFloorCarryClear {
    public:
        PreparedFloorCarryClear(PreparedFloorCarryClear&&) noexcept;
        PreparedFloorCarryClear& operator=(PreparedFloorCarryClear&&) noexcept;
        ~PreparedFloorCarryClear();
        PreparedFloorCarryClear(const PreparedFloorCarryClear&)=delete;
        PreparedFloorCarryClear& operator=(const PreparedFloorCarryClear&)=delete;
        bool can_commit() const noexcept;
        bool can_publish() const noexcept;
        bool publish() noexcept;
    private:
        struct Data;std::unique_ptr<Data> data_;
        explicit PreparedFloorCarryClear(std::unique_ptr<Data>) noexcept;
        friend class Playback;
    };
    // Prepare before a floor/OOW move. Publish after the exact item detaches
    // and reaches the requested position. No allocation/resolver at publish.
    std::optional<PreparedFloorCarryClear> prepare_floor_carry_clear(
        const VmFrame&,const Vm& source_vm,const Instruction&,const VmDispatch&,
        std::int16_t item,std::uint64_t item_generation,std::optional<EntityPosition> destination,
        std::int16_t carrier,std::uint64_t carrier_generation,std::uint64_t carrier_vm_generation,
        std::string* error=nullptr);
    // Routing safe-point service: replace only the primary animation, preserving
    // carry and current hands. No opcode44 wait or VM dispatch is manufactured.
    // Index3 is the looping reset (speed1); indices4..9 are nonlooping turns
    // at speed1 or speed2. advance() remains the sole per-actor tick owner.
    bool start_route_animation(const VmFrame& source_frame,const Vm& source_vm,
        std::uint64_t actor_generation,std::uint64_t vm_generation,
        std::uint16_t index,bool swimming,float speed,bool loop,std::string* error=nullptr);
    // Pinned StartWalkAnimation: one primary idle plus full/half walk states.
    // Names are looked up before the source's first-two-name early return;
    // only a proven empty half entry selects its full-walk fallback.
    bool start_source_walk_animation(const VmFrame&,const Vm& source_vm,
        std::uint64_t actor_generation,std::uint64_t vm_generation,
        bool swimming,std::int16_t walk_style,bool is_pet,std::string* error=nullptr);
    bool set_source_walk_velocity(const VmFrame&,const Vm& source_vm,
        std::uint64_t actor_generation,std::uint64_t vm_generation,
        std::int32_t velocity,std::string* error=nullptr);
    // Root PreExit only: retain the actual primary state/cursors, drop extras,
    // and give that retained idle full weight. Carry and current hands persist.
    bool end_source_walk_animation(const VmFrame&,const Vm& source_vm,
        std::uint64_t actor_generation,std::uint64_t vm_generation,std::string* error=nullptr,
        const Instruction* lifecycle_instruction=nullptr,const VmDispatch* lifecycle_dispatch=nullptr);
    bool capture_snapshot(PlaybackSnapshot&,std::string* error=nullptr) const;
    bool restore_snapshot(const PlaybackSnapshot&,Vm&,const PlaybackRestoreResolver&,std::string* error=nullptr);
    VmPrimitive primitive();
    // Pinned VMReach47 uses its real instruction and retries at the next tick.
    // Profiles 0..3 name sit/floor/seat/table clips, never opcode44 table IDs.
    struct ReachServices {
        // Authored SLOT0 height in source units. Missing slot is nullopt;
        // unavailable resource/identity must throw rather than invent a height.
        std::function<std::optional<float>(std::int16_t,std::size_t)> slot_height;
        std::function<std::shared_ptr<const AnimationHeader>(std::uint16_t)> animation;
        // Exact caller dispatch, atomic reciprocal World move and avatar carry.
        // nullopt is unsupported; false is an ordinary placement refusal, which
        // source Reach consumes without branching false.
        std::function<std::optional<bool>(const VmFrame&,const Instruction&,const VmDispatch&,
            std::int16_t item,std::int16_t container,std::size_t slot,std::string&)> transfer;
    };
    VmPrimitive reach_primitive(ReachServices);
    // Source RunImmediately shares the avatar animation state. Park only the
    // exact native wait; clips, carry, gestures and event cursors remain live.
    bool validates_immediate_parent(const VmPending&,const VmFrame&) const;
    bool can_park_for_immediate(const VmPending&,const VmFrame&) const;
    bool park_for_immediate(std::uint64_t parent_ticket);
    bool retry_immediate_parent(std::uint64_t parent_ticket);
    // Source per-entity phases: select immediate first, then release a current
    // 44 wait for real budgeted reexecution, and advance animation AFTER VM work.
    // Both return success, including valid no-job/no-animation states.
    bool service_wait();
    bool advance(std::uint64_t,const std::function<void(const PlaybackEvent&)>& observed={});
    // At each consecutive tick, deliver prior queued event/end first, then
    // advance frames. Host resumes VM after this call. One actor per controller.
    bool tick(std::uint64_t,const std::function<void(const PlaybackEvent&)>& observed={});
    void cancel(); // Explicit host cancellation clears clip and any matching wait (false).
    bool discard_for_source_reset(); // Retire services without completing old VM tickets.
    bool finish_source_reset(); // Preserve carry/hand state across the actual Reset run change.
    std::optional<PlaybackProgress> progress() const;
    // Resolved carry resource retained by AnimMode 2; the renderer
    // composes this secondary pose after the ordinary animation at frame0.
    std::shared_ptr<const AnimationHeader> carry_clip() const {return carry_clip_;}
    // Read-only. Rejects stale actor/run/wait identity before the next tick;
    // terminal nonblocking loops remain valid while their VM run is unchanged.
    // A blend has no meaningful single render sample; use sample_collection.
    std::optional<PlaybackSample> sample() const;
    std::optional<PlaybackSampleCollection> sample_collection() const;
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
    PrimitiveOutcome reach(const VmFrame&,const Instruction&,const VmDispatch&,const ReachServices&);
    std::optional<State> prepare(std::shared_ptr<const AnimationHeader>,const Entity&,bool backwards,bool loop,bool hurryable,std::string& error) const;
    struct SourceWalkState {std::array<State,2> secondary;std::array<float,3> weights;};
    bool deliver();
    bool tick_impl(std::uint64_t,const std::function<void(const PlaybackEvent&)>&,
                   bool deliver_pending,bool report_success);
    bool valid_slot_context(const VmFrame&,const Vm&,std::uint64_t actor_generation,
        std::uint64_t vm_generation,std::string* error,
        const Instruction* lifecycle_instruction=nullptr,const VmDispatch* lifecycle_dispatch=nullptr) const;
    World& world_;AnimationResolver resolver_;PlaybackLimits limits_;
    AnimationLookupResolver animation_lookup_resolver_;
    ResetAnimationLookupResolver reset_lookup_resolver_;
    AnimationResetResolver reset_resolver_;RouteAnimationResolver route_resolver_;
    SourceWalkAnimationNameResolver walk_name_resolver_;
    Vm* vm_=nullptr;std::optional<State> state_;std::optional<Wait> wait_;
    std::optional<SourceWalkState> source_walk_;
    std::shared_ptr<const AnimationHeader> carry_clip_;
    std::optional<PlaybackResourceKey> carry_resource_;
    struct HandsState {AvatarHands value;std::int16_t actor;std::uint64_t actor_generation,vm_generation;};
    std::optional<HandsState> hands_;
    std::optional<std::uint64_t> last_tick_;std::string diagnostic_;bool executing_=false,reach_executing_=false;
};
}
