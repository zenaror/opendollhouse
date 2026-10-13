#pragma once
#include "scene_renderer.hpp"
#include "odh/graphics_state.hpp"
#include "odh/playback.hpp"
#include <memory>
struct ProbeViewConfig {
    SceneConfig scene;
    std::string frames_directory;
    std::uint16_t stride=8;
    bool window=false,automatic=false,readback=false;
};
// Host-only observer/controller for the constructed chair probe. Owns graphics
// resources, never dispatches VM instructions or mutates World/playback.
class ProbeView {
public:
    explicit ProbeView(ProbeViewConfig);
    ~ProbeView();
    bool ready(const odh::World&,const odh::Playback&,const odh::SlotTable&,std::uint64_t tick,std::size_t phase);
    void draw(const odh::World&,const odh::Playback&,const odh::SlotTable&,std::uint64_t tick,std::size_t phase,bool force=false);
    bool quit() const;
    bool failed() const;
    const std::string& diagnostic() const;
    unsigned long native_id() const; // Own preview window only; zero without backend.
    bool action() const;
    void clear_action();
    bool manual() const;
    std::string report() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
