/* SPDX-License-Identifier: MPL-2.0 */
#include <psp2/kernel/processmgr.h>
// The same application and VM own the game on every SDL target.
#define main OdhDesktopMain
#include "../desktop/main.cpp"
#undef main

// Use VitaSDK's default 128 MiB newlib heap. Reserving 256 MiB up front
// can fail before main, when C++ global constructors first allocate memory.
int main(int, char**) {
    const char* args[] = {"OpenDollhouse", "--data-root", "ux0:/data/OpenDollhouse/game",
        "--save-root", "ux0:/data/OpenDollhouse/saves", "--cache-dir", "ux0:/data/OpenDollhouse/cache"};
    const int status = OdhDesktopMain(sizeof(args)/sizeof(args[0]), const_cast<char**>(args));
    sceKernelExitProcess(status);
    return status;
}
