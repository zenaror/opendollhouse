#pragma once
#include "xa_patch_catalog.hpp"
#include "odh/hit_selection.hpp"
#include "odh/mixer.hpp"
#include "odh/sound_threads.hpp"
#include <iosfwd>

namespace odh_host {
struct BhavAudioConfig {
    std::string hsm_path,hot_path,hit_path;
    std::vector<XaPatchSource> sources;
    odh::HitArguments arguments{};
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
