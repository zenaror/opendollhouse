#include "odh_thread_state.h"
#include "state_cases.hpp"

#include <cstdio>
#include <cstring>

#ifdef __3DS__
#include <3ds.h>
#endif

int main() {
    const bool passed = odh_state_run_cases() == 0;
#ifdef __3DS__
    gfxInitDefault();
    consoleInit(GFX_TOP, nullptr);
    const char* marker = passed ? "ODH_3DS_NATIVE_STATE_V1:PASS\n"
                                : "ODH_3DS_NATIVE_STATE_V1:FAIL\n";
    svcOutputDebugString(marker, static_cast<s32>(std::strlen(marker)));
#endif
    std::printf("ODH native TS1 state: %s\n", passed ? "PASS" : "FAIL");
    std::printf("Shared native state, Expression and host test cases.\n");
    std::printf("Synthetic state; not the complete game.\n");
#ifdef __3DS__
    std::printf("Press START to exit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
#endif
    return passed ? 0 : 1;
}
