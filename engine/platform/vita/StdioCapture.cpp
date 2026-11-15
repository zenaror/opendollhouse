/* SPDX-License-Identifier: MPL-2.0 */
#include "StdioCapture.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <sys/reent.h>
#include <psp2/kernel/clib.h>

// VitaSDK's installed headers select _impure_ptr for stdout/stderr, while
// its libc printf selects this thread's reentrancy object. The SDK exports
// this standard newlib function but only declares it with __DYNAMIC_REENT__.
extern "C" struct _reent* __getreent(void);
extern "C" void __sinit(struct _reent*);

namespace odh::vita {
namespace {
struct Stream {
    FILE* File = nullptr;
    const char* Name = nullptr;
    bool Error = false;
};
std::array<Stream, 4> captured{};
unsigned capturedCount = 0;

[[noreturn]] void Fail(const char* action, const char* stream, const char* path) {
    sceClibPrintf("OpenDollhouse: log %s failed for %s (%s)\n", action, stream, path);
    throw std::runtime_error(std::string("Application log ") + action +
        " failed for " + stream + ": " + path);
}
}

void CaptureStdio(const char* path) {
    if (!path || !*path) Fail("path", "stdio", path ? path : "null");
    if (capturedCount) Fail("reinitialization", "stdio", path);
    auto* thread = __getreent();
    if (!_impure_ptr || !thread) Fail("lookup", "stdio", path);
    // Initialize before reopening: later __sfp/fopen must not reset these FILEs.
    __sinit(_impure_ptr);
    if (thread != _impure_ptr) __sinit(thread);
    const std::array<Stream, 4> candidates{{
        {_impure_ptr->_stdout, "legacy-stdout", false},
        {_impure_ptr->_stderr, "legacy-stderr", true},
        {thread->_stdout, "thread-stdout", false},
        {thread->_stderr, "thread-stderr", true}
    }};
    // Deduplicate stable initialized FILE addresses before reopening them.
    for (const auto& stream : candidates) {
        if (!stream.File) Fail("lookup", stream.Name, path);
        unsigned i = 0;
        while (i < capturedCount && captured[i].File != stream.File) ++i;
        if (i < capturedCount) {
            captured[i].Error = captured[i].Error || stream.Error;
        } else {
            captured[capturedCount++] = stream;
        }
    }
    for (unsigned i = 0; i < capturedCount; ++i) {
        const auto& stream = captured[i];
        if (!std::freopen(path, "a", stream.File)) Fail("open", stream.Name, path);
        if (std::setvbuf(stream.File, nullptr, stream.Error ? _IONBF : _IOLBF, 0) != 0)
            Fail("buffering", stream.Name, path);
        if (std::fprintf(stream.File, "[vita-stdio] stream=%s ready=1\n", stream.Name) < 0 ||
            std::fflush(stream.File) != 0)
            Fail("health check", stream.Name, path);
    }
}

}
