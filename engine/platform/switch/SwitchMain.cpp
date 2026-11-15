/* SPDX-License-Identifier: MPL-2.0 */
#define main OdhDesktopMain
#include "../desktop/main.cpp"
#undef main
int main(int argc, char** argv) {
    if (argc > 1) return OdhDesktopMain(argc, argv);
    const char* args[] = {"OpenDollhouse", "--data-root", "sdmc:/switch/OpenDollhouse/game",
        "--save-root", "sdmc:/switch/OpenDollhouse/saves", "--cache-dir", "sdmc:/switch/OpenDollhouse/cache"};
    return OdhDesktopMain(sizeof(args)/sizeof(args[0]), const_cast<char**>(args));
}
