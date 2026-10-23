#pragma once

#include <cstdint>
#include <functional>
#include <optional>

namespace odh_host {
class LinuxWindow;
using NeighborhoodResumeCallback = std::function<std::optional<std::uint16_t>()>;
}

// Run the person/lot runtime using the existing command-line options. When
// render-window is requested, an optional shared host window is reused.
int odh_person_run(int argc, char** argv, odh_host::LinuxWindow* shared_window,
                    odh_host::NeighborhoodResumeCallback neighborhood_resume = {});

// Integrated Linux entry point selected by the executable wrapper when the
// user supplies --game-root.
int odh_linux_game_entry(int argc, char** argv);
