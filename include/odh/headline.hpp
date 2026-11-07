#pragma once
#include "odh/world.hpp"

namespace odh {
struct BalloonHeadlineState {
    std::array<std::uint8_t,8> operand{};
    std::int16_t target=0;
    std::uint64_t target_generation=0;
    std::int16_t icon_target=0;
    std::uint64_t icon_generation=0;
    std::int8_t index=0;
    std::int32_t duration=0,anim=0;
};
using BalloonHeadlineSnapshot=std::vector<BalloonHeadlineState>;
// Structural validation only; restoring also checks exact World generations.
bool valid_balloon_headline_snapshot(const BalloonHeadlineSnapshot&);

// FreeSO7e76 VMSetBalloonHeadline + VMNullHeadlineProvider. No graphics/audio,
// no money-renderer exception: the null provider's IsMoney is always false.
// World must outlive the controller and handlers. Serial dispatch only.
class BalloonHeadlines {
public:
    explicit BalloonHeadlines(World& world):world_(world) {}
    BalloonHeadlines(const BalloonHeadlines&)=delete;
    BalloonHeadlines& operator=(const BalloonHeadlines&)=delete;
    VmPrimitive primitive();
    // Call once per actual entity Tick, after its VM slice, not per world tick.
    bool tick(std::int16_t entity_id);
    bool active(std::int16_t entity_id) const { return states_.count(entity_id)!=0; }
    bool remove(std::int16_t entity_id);
    BalloonHeadlineSnapshot snapshot() const;
    // Atomic replacement; no callbacks/pointers are present in snapshots.
    bool restore_snapshot(const BalloonHeadlineSnapshot&);
    const std::string& diagnostic() const { return diagnostic_; }
private:
    PrimitiveOutcome request(const VmFrame&,const Instruction&,const Ts1Temps&);
    World& world_;
    std::map<std::int16_t,BalloonHeadlineState> states_;
    std::string diagnostic_;
};
} // namespace odh
