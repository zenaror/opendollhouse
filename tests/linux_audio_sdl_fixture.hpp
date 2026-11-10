#pragma once
#include <cstddef>
namespace audio_fixture {
enum Counter : std::size_t {init_calls,init_success,quit_subsystem,global_quit,open_calls,close_calls,
    live_references,live_devices,premature_quit,bad_device,version_calls,count};
enum Failure {none,init_error,open_error,old_version,changed_format};
}
