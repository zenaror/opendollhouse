#pragma once

namespace odh_host {
class LinuxWindow;
}

// Run the person/lot runtime using the existing command-line options. When
// render-window is requested, an optional shared host window is reused.
int odh_person_run(int argc, char** argv, odh_host::LinuxWindow* shared_window);

// Integrated Linux entry point selected by the executable wrapper when the
// user supplies --game-root.
int odh_linux_game_entry(int argc, char** argv);
