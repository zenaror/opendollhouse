#pragma once
#include "xa_patch_catalog.hpp"
#include "odh/hit_selection.hpp"
#include "odh/mixer.hpp"
#include "odh/sound_threads.hpp"
#include <iosfwd>

namespace odh_host {
inline constexpr std::array<std::uint8_t,9> native_control_receipt_ids{{17,18,19,20,21,25,46,47,50}};
using BhavNativeControlReceipt = std::array<std::optional<std::int32_t>,9>;
struct BhavAudioConfig {
    std::string hsm_path,hot_path,hit_path;
    std::vector<XaPatchSource> sources;
    odh::HitArguments arguments{};
    // Raw post-spatial extvol/pan for the partial fresh ArgStyle 2 profile.
    std::optional<std::array<std::int32_t,2>> native_controls;
    bool caller_arguments=false; // Resolved World source + nonzero HOT overrides.
    odh::HitJumpOperand jump=odh::HitJumpOperand::FreeSoHeuristic;
    odh::PcmControls controls{};
    std::uint32_t output_rate=44100;
};

// Diagnostic, explicit-resource audio path for the constructed World probe.
// World outlives this object. Submission passes each full SoundRequest through the
// existing collection; no caller-unit or spatial conversion is performed.
class BhavAudio {
public:
    BhavAudio(odh::World&,BhavAudioConfig);
    ~BhavAudio();
    BhavAudio(const BhavAudio&)=delete;
    BhavAudio& operator=(const BhavAudio&)=delete;
    bool submit(const odh::SoundRequest&);
    // Tuple prepared for the latest submit attempt, if resolution got that far.
    std::optional<odh::HitArguments> last_arguments() const;
    // Compact initial controls adopted by the latest successful submit only.
    std::optional<BhavNativeControlReceipt> last_native_controls() const;
    // Call once after each existing VM/playback scheduling boundary.
    void simulation_tick();
    // Host-only PCM pump after simulation. Never ticks World, VM or HIT threads.
    void drain(std::uint16_t milliseconds);
    void emit_json(std::ostream&,std::uint64_t simulation_ticks) const;
    bool unhealthy() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
