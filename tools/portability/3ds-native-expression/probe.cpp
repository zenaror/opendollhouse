#include "odh_expression.h"
#include "expression_cases.hpp"

#include <cstdio>
#include <cstring>

#ifdef __3DS__
#include <3ds.h>
#endif

int main() {
    const bool passed = odh_expression_abi_version() == 1 &&
        odh_expression_run_cases() == 0;
#ifdef __3DS__
    gfxInitDefault();
    consoleInit(GFX_TOP, nullptr);
    const char* marker = passed ? "ODH_3DS_NATIVE_EXPRESSION_V1:PASS\n"
                                : "ODH_3DS_NATIVE_EXPRESSION_V1:FAIL\n";
    svcOutputDebugString(marker, static_cast<s32>(std::strlen(marker)));
#endif
    std::printf("ODH native TS1 Expression: %s\n", passed ? "PASS" : "FAIL");
    std::printf("Shared native executor and host test cases.\n");
    std::printf("Synthetic memory; not the complete game.\n");
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
