#pragma once
#include "odh/pcm_transport.hpp"
#include "odh/sound.hpp"
#include <filesystem>
#include <iosfwd>
#include <memory>

namespace odh_host {
// Host assembly; platform audio is injected. World and sink must outlive this
// service. Single owner thread, explicit bounded nonlooping HIT/XA profile.
// Filesystem discovery is read-only and belongs to this host layer, not core.
class GameAudio {
public:
    GameAudio(odh::World&,const std::filesystem::path& game_root,odh::PcmSink&);
    ~GameAudio();
    GameAudio(const GameAudio&)=delete;
    GameAudio& operator=(const GameAudio&)=delete;
    bool submit(const odh::SoundRequest&);
    void simulation_tick();
    void pump(); // PCM only; never advances World, animation or HIT.
    void stop_owner(std::int16_t,std::uint64_t);
    void clear();
    bool output_empty() const;
    void emit_json(std::ostream&) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
