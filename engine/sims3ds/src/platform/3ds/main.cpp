/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims — Nintendo 3DS entry point (thin platform layer).
//
// Everything game-related lives in src/app (shared with the host simulator src/tools/sim3ds_host.cpp); this
// file only does what needs libctru:
//   * New 3DS: 804 MHz + L2 cache (osSetSpeedupEnable), both screens as BGR8 framebuffers;
//   * input: hid buttons / touch / circle pad (+ ZL/ZR/C-stick, ir:rst on the New 3DS) -> simapp::AppInput;
//   * output: the app's 32-bit canvases are rotated into libctru's portrait framebuffers
//     (column-major, x -> column, y flipped: dst[(x * 240 + (239 - y)) * 3] = B,G,R);
//   * time: the ARM11 system tick.
//
// SD card layout: see src/app/README.md (sdmc:/3ds/OpenDollhouse/game/GameData/..., game/UserData/...).
#include <3ds.h>
#include <3ds/allocator/mappable.h>
#include <sys/stat.h>
#include <sys/iosupport.h>
#include <unistd.h>
#include <type_traits>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <exception>
#include <malloc.h>
#include <new>
#include <string>
#include "app/App.h"
#include "app/Font.h"
#include "app/HandheldLanguage.h"
#include "odh/game_languages.hpp"
#include "audio/GameAudio.h"
#include "platform/3ds/AudioNdsp.h"
#include "files/FilePool.h"
#include "odh/ConsoleDiagnostics.h"

using namespace simapp;

// TS1(sims): libctru splits the free memory into the app heap (malloc/new) and the linear heap (GPU/DSP-visible),
// giving the linear heap up to 32 MB by default. This app draws in software: linear memory only holds the framebuffers
// (~1.5 MB) and the NDSP wave buffers, so 8 MB is plenty and the game gets ~24 MB more heap (HBL: ~88 -> ~112 MB).
extern "C" { u32 __ctru_linear_heap_size = 8 * 1024 * 1024; }

// FIX(sims): replaces libctru's weak __system_allocateHeaps. With only __ctru_linear_heap_size set, libctru gives the
// app heap ALL remaining memory - but the heap area (OS_HEAP_AREA_BEGIN..END) holds at most 96 MB, so in the CIA's
// 178 MB mode (and large Homebrew Launcher takeovers) svcControlMemory failed and the app stopped in svcBreak before
// main(). Here the app heap is capped to the heap area and the rest goes to the linear heap (max 32 MB, like libctru).
extern "C" {
extern u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap;
extern char* fake_heap_start;
extern char* fake_heap_end;
void __system_allocateHeaps(void) {
    Handle reslimit = 0;
    if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE))) svcBreak(USERBREAK_PANIC);
    s64 maxCommit = 0, currentCommit = 0;
    ResourceLimitType reslimitType = RESLIMIT_COMMIT;
    svcGetResourceLimitLimitValues(&maxCommit, reslimit, &reslimitType, 1);
    svcGetResourceLimitCurrentValues(&currentCommit, reslimit, &reslimitType, 1);
    svcCloseHandle(reslimit);
    u32 remaining = (u32)(maxCommit - currentCommit) & ~0xFFFu;

    const u32 heapAreaMax = (u32)(OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN); // 96 MB
    const u32 linearCap = 32u << 20;
    u32 linear = 8u << 20;
    if (linear > remaining / 2) linear = (remaining / 2) & ~0xFFFu;
    u32 heap = remaining - linear;
    if (heap > heapAreaMax) {
        heap = heapAreaMax;
        linear = remaining - heap;
        if (linear > linearCap) linear = linearCap;
    }
    __ctru_heap_size = heap;
    __ctru_linear_heap_size = linear;

    if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size, MEMOP_ALLOC,
                                  (MemPerm)(MEMPERM_READ | MEMPERM_WRITE))))
        svcBreak(USERBREAK_PANIC);
    if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR,
                                  (MemPerm)(MEMPERM_READ | MEMPERM_WRITE))))
        svcBreak(USERBREAK_PANIC);
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    fake_heap_start = (char*)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}
}

// TS1(sims) hardware pass: every file system operation of the main thread on the SD card, counted and timed (system
// ticks around the FS service call) into FSO::Files::Fs for the step / heartbeat lines of log.txt (src/app/README.md
// "Calibrated SD cost model"). libctru's sdmc device table lives in writable memory: its entries are wrapped in place.
// Each wrapper forwards whatever the device function takes (signature-agnostic templates), so it builds with any
// newlib devoptab layout.
namespace sdhook {
static devoptab_t g_Orig;
enum Kind { kOpen, kClose, kRead, kWrite, kSeek, kStat, kDirOpen, kDirNext, kOther };
static void Account(int kind, double ms, long long bytes) {
    auto& f = FSO::Files::Fs;
    switch (kind) {
        case kOpen: f.Opens++; f.OpenMs += ms; break;
        case kClose: f.Closes++; f.CloseMs += ms; break;
        case kRead: f.Reads++; f.ReadMs += ms; if (bytes > 0) f.BytesRead += (uint64_t)bytes; break;
        case kWrite: f.Writes++; f.WriteMs += ms; if (bytes > 0) f.BytesWritten += (uint64_t)bytes; break;
        case kSeek: f.Seeks++; f.SeekMs += ms; break;
        case kStat: f.Stats++; f.StatMs += ms; break;
        case kDirOpen: f.DirOpens++; f.DirMs += ms; break;
        case kDirNext: f.DirReads++; f.DirMs += ms; break;
        default: break;
    }
}
static bool g_InHook = false;
template <typename T, T devoptab_t::*Field, int K> struct Hook;
template <typename R, typename... A, R (*devoptab_t::*Field)(A...), int K> struct Hook<R (*)(A...), Field, K> {
    static R Call(A... a) {
        // other threads (the music stream) and the log writes made by the heartbeat itself are not counted
        if (threadGetCurrent() != nullptr || g_InHook) return (g_Orig.*Field)(a...);
        u64 t0 = svcGetSystemTick();
        R r = (g_Orig.*Field)(a...);
        double ms = (double)(svcGetSystemTick() - t0) / (double)(SYSCLOCK_ARM11 / 1000);
        long long bytes = 0;
        if constexpr (std::is_integral<R>::value) bytes = (long long)r;
        Account(K, ms, bytes);
        // the heartbeat only after reads / opens / stats / directory reads: a write or close can be stdout's own flush
        // (printing from inside it would re-enter that stream)
        if (FSO::Files::FsOps::OnOp && (K == kOpen || K == kRead || K == kStat || K == kDirOpen || K == kDirNext)) {
            g_InHook = true; FSO::Files::FsOps::OnOp(); g_InHook = false;
        }
        return r;
    }
};
#define SIMS_HOOK(field, kind) if (dev->field) dev->field = &Hook<decltype(devoptab_t::field), &devoptab_t::field, kind>::Call
static void Install() {
    devoptab_t* dev = const_cast<devoptab_t*>(GetDeviceOpTab("sdmc"));
    if (dev == nullptr) return;
    g_Orig = *dev;
    SIMS_HOOK(open_r, kOpen);
    SIMS_HOOK(close_r, kClose);
    SIMS_HOOK(read_r, kRead);
    SIMS_HOOK(write_r, kWrite);
    SIMS_HOOK(seek_r, kSeek);
    SIMS_HOOK(stat_r, kStat);
    SIMS_HOOK(diropen_r, kDirOpen);
    SIMS_HOOK(dirnext_r, kDirNext);
}
#undef SIMS_HOOK
} // namespace sdhook

static const char* APP_ROOT = "sdmc:/3ds/OpenDollhouse";
static const char* DATA_ROOT = "sdmc:/3ds/OpenDollhouse/game";
static const char* SAVE_ROOT = "sdmc:/3ds/OpenDollhouse/saves";
static const char* CACHE_DIR = "sdmc:/3ds/OpenDollhouse/cache";

static double NowMs() {
    return (double)svcGetSystemTick() / (double)(SYSCLOCK_ARM11 / 1000);
}

// Logging stays on the main thread; detailed loading-step lines remain opt-in.
static bool g_logReady = false;
static void LogFailure(const char* action) {
    const int error=errno;
    char text[160];
    const int n = std::snprintf(text, sizeof text, "OpenDollhouse: log %s failed errno=%d\n", action,error);
    if (n > 0) svcOutputDebugString(text, std::min(n, int(sizeof text - 1)));
}
static bool OpenLog() {
    constexpr const char* path = "sdmc:/3ds/OpenDollhouse/cache/opendollhouse.log";
    // Share one libctru descriptor state, rather than reopening the SD file twice.
    FILE* log=std::fopen(path,"a");
    if (!log) { LogFailure("open"); return false; }
    const int source=fileno(log), out=fileno(stdout), err=fileno(stderr);
    if (source<0 || out<0 || err<0) {
        const int error=errno;std::fclose(log);errno=error;
        LogFailure("descriptor lookup");return false;
    }
    if (dup2(source,out)<0) {
        const int error=errno;std::fclose(log);errno=error;
        LogFailure("stdout capture");return false;
    }
    if (dup2(source,err)<0) {
        const int error=errno;std::fclose(log);errno=error;
        LogFailure("stderr capture");return false;
    }
    if (std::fclose(log)!=0) { LogFailure("descriptor close");return false; }
    if (std::setvbuf(stdout, nullptr, _IOLBF, 0) != 0 ||
        std::setvbuf(stderr, nullptr, _IONBF, 0) != 0) {
        LogFailure("buffering"); return false;
    }
    if (std::printf("[3ds-stdio] stream=stdout ready=1\n") < 0 ||
        std::fprintf(stderr, "[3ds-stdio] stream=stderr ready=1\n") < 0 ||
        std::fflush(stdout) != 0 || std::fflush(stderr) != 0) {
        LogFailure("health check"); return false;
    }
    g_logReady = true;
    return true;
}
static void FlushLog() {
    // Failed freopen closes that stream; only flush after capture completed.
    if (!g_logReady) return;
    const int out=std::fflush(stdout);
    const int err=std::fflush(stderr);
    if (out != 0 || err != 0) LogFailure("flush");
}

struct ThreeDsPerformance {
    odh::desktop::ConsoleFrameWindow Window;
    double TopBlit=0, BottomBlit=0, Submit=0, Wait=0, LastPoll=0, MaxPollGap=0, AppBegin=0;
    uint64_t ProgressPresents=0, InputPolls=0, ButtonPolls=0, TouchDowns=0, TouchUps=0;
    uint64_t CirclePolls=0, CStickPolls=0, FrameErrors=0;
    int CircleX=0, CircleY=0, CStickX=0, CStickY=0, TouchX=-1, TouchY=-1;
    bool InApp=false;
    FSO::Files::IOStats Io;
    void Reset(double now) {
        Window.Reset(now);
        TopBlit=BottomBlit=Submit=Wait=MaxPollGap=0;
        ProgressPresents=InputPolls=ButtonPolls=TouchDowns=TouchUps=CirclePolls=CStickPolls=FrameErrors=0;
        Io=FSO::Files::FilePool::Stats;
    }
};
static ThreeDsPerformance g_performance;
static const char* Phase(const App& app) {
    switch (app.CurrentMode()) {
    case App::Mode::Boot: return "boot";
    case App::Mode::Front: return "front";
    case App::Mode::Loading: return "loading";
    case App::Mode::Live: return "live";
    case App::Mode::Error: return "error";
    }
    return "unknown";
}
static void ReportPerformance(const App& app, const char* reason, bool force=false) {
    const double now=NowMs();
    auto& p=g_performance;
    const auto& w=p.Window;
    if (!g_logReady || (!force && !w.Due(now))) return;
    const double frames=w.Frames ? double(w.Frames) : 1.0;
    const double presents=w.Presents ? double(w.Presents) : 1.0;
    const auto io=FSO::Files::FilePool::Stats;
    const auto delta=[](uint64_t a,uint64_t b) { return a>=b ? a-b : 0; };
    const auto heap=mallinfo();
    const long long heapFree=static_cast<long long>(__ctru_heap_size)-heap.arena+heap.fordblks;
    const u32 regionCapacity=osGetMemRegionSize(MEMREGION_APPLICATION);
    const u32 regionFree=osGetMemRegionFree(MEMREGION_APPLICATION);
    std::printf("[3ds-perf] reason=%s phase=%s window_ms=%.2f fps=%.2f frames=%llu presents=%llu progress_presents=%llu "
        "frame_call_avg_ms=%.2f frame_call_max_ms=%.2f frame_call_p95_upper_ms=%.2f app_inflight_ms=%.2f "
        "vm_avg_ms=%.2f world_avg_ms=%.2f ui_avg_ms=%.2f updates=%llu frame_errors=%llu "
        "top_blit_avg_ms=%.2f bottom_blit_avg_ms=%.2f submit_avg_ms=%.2f wait_avg_ms=%.2f "
        "io_bytes=%llu io_reads=%llu io_opens=%llu io_seeks=%llu open_files=%d "
        "heap_used_bytes=%llu heap_arena_bytes=%llu heap_capacity_bytes=%llu heap_free_estimate_bytes=%lld "
        "linear_free_bytes=%llu linear_capacity_bytes=%llu region_api_raw_free_bytes=%llu "
        "region_capacity_bytes=%llu region_sample_consistent=%d\n",
        reason,Phase(app),now-w.Since,w.Fps(now),static_cast<unsigned long long>(w.Frames),
        static_cast<unsigned long long>(w.Presents),static_cast<unsigned long long>(p.ProgressPresents),
        w.Work/frames,w.WorkMax,w.P95Upper(),p.InApp ? now-p.AppBegin : 0.0,
        w.Vm/frames,w.World/frames,w.Ui/frames,static_cast<unsigned long long>(w.Updates),
        static_cast<unsigned long long>(p.FrameErrors),p.TopBlit/presents,p.BottomBlit/presents,
        p.Submit/presents,p.Wait/presents,static_cast<unsigned long long>(delta(io.BytesRead,p.Io.BytesRead)),
        static_cast<unsigned long long>(delta(io.Reads,p.Io.Reads)),static_cast<unsigned long long>(delta(io.Opens,p.Io.Opens)),
        static_cast<unsigned long long>(delta(io.Seeks,p.Io.Seeks)),FSO::Files::FilePool::OpenCount(),
        static_cast<unsigned long long>(heap.uordblks),static_cast<unsigned long long>(heap.arena),
        static_cast<unsigned long long>(__ctru_heap_size),heapFree,static_cast<unsigned long long>(linearSpaceFree()),
        static_cast<unsigned long long>(__ctru_linear_heap_size),
        static_cast<unsigned long long>(regionFree),static_cast<unsigned long long>(regionCapacity),
        int(regionFree<=regionCapacity));
    std::printf("[3ds-input] polls=%llu poll_gap_max_ms=%.2f button_polls=%llu touch_downs=%llu touch_ups=%llu "
        "circle_active_polls=%llu cstick_active_polls=%llu circle_x=%d circle_y=%d cstick_x=%d cstick_y=%d touch_x=%d touch_y=%d\n",
        static_cast<unsigned long long>(p.InputPolls),p.MaxPollGap,static_cast<unsigned long long>(p.ButtonPolls),
        static_cast<unsigned long long>(p.TouchDowns),static_cast<unsigned long long>(p.TouchUps),
        static_cast<unsigned long long>(p.CirclePolls),static_cast<unsigned long long>(p.CStickPolls),
        p.CircleX,p.CircleY,p.CStickX,p.CStickY,p.TouchX,p.TouchY);
    FlushLog();
    p.Reset(now);
}

// Canvas (row-major RGBA, R in the low byte) -> libctru BGR8 framebuffer (rotated 90 degrees: the LCDs are
// portrait panels). Processed in 8-column strips so the source reads stay in cache.
static void BlitCanvas(const Canvas& c, gfxScreen_t screen) {
    u16 fbW = 0, fbH = 0; // fbW = 240 (screen height), fbH = 400/320 (screen width)
    u8* fb = gfxGetFramebuffer(screen, GFX_LEFT, &fbW, &fbH);
    if (fb == nullptr) return;
    const int W = c.Width, H = c.Height;
    if (W > fbH || H > fbW) return;
    const uint32_t* src = c.Px.data();
    for (int x0 = 0; x0 < W; x0 += 8) {
        int x1 = x0 + 8 < W ? x0 + 8 : W;
        for (int y = 0; y < H; y++) {
            const uint32_t* row = src + (size_t)y * W;
            for (int x = x0; x < x1; x++) {
                uint32_t p = row[x];
                u8* d = fb + ((size_t)x * fbW + (size_t)(fbW - 1 - y)) * 3;
                d[0] = (u8)(p >> 16); // B
                d[1] = (u8)(p >> 8);  // G
                d[2] = (u8)p;         // R
            }
        }
    }
}

// The picker runs before localized content is opened. Its font and colors are
// built into the app, so it also works on installations with no prepared cache.
static std::optional<uint8_t> StartupLanguage() {
    const auto saved = ReadHandheldLanguage(SAVE_ROOT);
    hidScanInput();
    if (saved && !(hidKeysHeld() & KEY_L)) {
        std::printf("[language] saved code=%u\n", unsigned(*saved));
        return saved;
    }
    std::printf("[language] picker opened previous=%u\n", unsigned(saved.value_or(0)));
    Canvas top(400,240), bottom(320,240);
    constexpr int rows = 10, rowHeight = 20;
    constexpr int cols = 2;
    static_assert(rows * cols == odh_host::game_languages.size());
    const Rect confirm(164,207,150,28), cancel(6,207,150,28);
    const auto navy = RGBA(12,24,68), blue = RGBA(33,69,132), white = RGBA(245,248,255);
    int selected = int(saved.value_or(1)) - 1;
    bool writeError = false, redraw = true;
    while (aptMainLoop()) {
        if (redraw) {
            top.Clear(navy);
            DrawTextCentered(top,FontId::Title,{0,47,400,34},"Choose your language",white);
            const auto& lang = odh_host::game_languages[selected];
            DrawTextCentered(top,FontId::Bold,{0,87,400,24},std::string(lang.ascii_name)+" ("+lang.tag+")",white);
            DrawTextCentered(top,FontId::Small,{0,139,400,24},"Use the D-pad or touch a language, then select OK.",white);
            if (writeError) DrawTextCentered(top,FontId::Small,{0,171,400,24},"Could not save the language. Check the SD card and retry.",white);
            bottom.Clear(navy);
            for (int n=0; n<rows*cols; ++n) {
                const Rect cell((n/rows)*160+3,(n%rows)*rowHeight+1,154,rowHeight-1);
                if (n==selected) bottom.FillRoundRect(cell,3,blue);
                const auto& item = odh_host::game_languages[n];
                // The built-in font supports Latin-1. Other scripts keep their readable ASCII labels.
                auto label = static_cast<unsigned char>(item.name[0]) < 128 ? ToLatin1(item.name) : std::string(item.ascii_name);
                DrawTextCentered(bottom,FontId::Small,cell,label,white);
            }
            bottom.FillRoundRect(confirm,4,blue);
            bottom.FillRoundRect(cancel,4,blue);
            DrawTextCentered(bottom,FontId::Bold,confirm,"A   OK",white);
            DrawTextCentered(bottom,FontId::Bold,cancel,"B   Back",white);
            BlitCanvas(top,GFX_TOP); BlitCanvas(bottom,GFX_BOTTOM);
            gfxFlushBuffers(); gfxSwapBuffers();
            redraw = false;
        }
        gspWaitForVBlank();
        hidScanInput();
        const u32 down = hidKeysDown();
        const int before = selected;
        if (down & KEY_DUP) selected = (selected/rows)*rows + (selected%rows+rows-1)%rows;
        if (down & KEY_DDOWN) selected = (selected/rows)*rows + (selected+1)%rows;
        if (down & (KEY_DLEFT|KEY_DRIGHT)) selected = (selected+rows)%(rows*cols);
        bool accept = (down & KEY_A) != 0, back = (down & KEY_B) != 0;
        if (down & KEY_TOUCH) {
            touchPosition touch{};
            hidTouchRead(&touch);
            if (touch.px < 320 && touch.py < rows*rowHeight) selected = (touch.px/160)*rows + touch.py/rowHeight;
            else if (confirm.Contains(touch.px,touch.py)) accept = true;
            else if (cancel.Contains(touch.px,touch.py)) back = true;
        }
        if (back) return saved;
        if (accept) {
            const uint8_t code = uint8_t(selected+1);
            if (WriteHandheldLanguage(SAVE_ROOT,code)) {
                std::printf("[language] confirmed code=%u\n", unsigned(code));
                return code;
            }
            writeError = true;
        }
        redraw = selected != before || accept;
    }
    return {};
}

// Frames that took longer than a refresh (~16.7 ms) do not wait for VBlank again: the swap queued by
// gfxSwapBuffers has long taken effect by the time the next frame is written, and waiting would quantize a
// 35 ms frame to 50 ms (20 fps) instead of ~28 fps.
static double g_lastPresent = 0;
static void PresentFrame(App& app, bool progress=false) {
    const double begin=NowMs();
    BlitCanvas(app.Top, GFX_TOP);
    const double topDone=NowMs();
    BlitCanvas(app.Bottom, GFX_BOTTOM);
    const double bottomDone=NowMs();
    gfxFlushBuffers();
    gfxSwapBuffers();
    const double submitted=NowMs();
    if (submitted - g_lastPresent < 15.0) gspWaitForVBlank();
    g_lastPresent = NowMs();
    auto& p=g_performance;
    p.TopBlit+=topDone-begin;p.BottomBlit+=bottomDone-topDone;
    p.Submit+=submitted-bottomDone;p.Wait+=g_lastPresent-submitted;
    p.Window.Presentation(bottomDone-begin,g_lastPresent-bottomDone);
    if(progress)++p.ProgressPresents;
    ReportPerformance(app,progress ? "progress" : "frame");
}

// FIX(sims): HOME menu / sleep. Pause the game's own sound mixing while the app is suspended (HOME menu, sleep, power
// menu) and flush the log, so nothing competes with the system applets for CPU/SD while they run.
static aptHookCookie g_aptCookie;
static void OnAptEvent(APT_HookType hook, void*) {
    switch (hook) {
    case APTHOOK_ONSUSPEND: case APTHOOK_ONSLEEP:
        simaudio::GameAudio::Get().SetSuspended(true); FlushLog(); break;
    case APTHOOK_ONRESTORE: case APTHOOK_ONWAKEUP:
        simaudio::GameAudio::Get().SetSuspended(false); break;
    // FIX(sims) SAVE: closed from the HOME menu / power button. APT hooks run inside aptMainLoop() / aptExit() on the
    // main thread, and every save is a blocking call on that thread (files/SafeFile.h: .new written, flushed, closed,
    // then renamed), so no save is ever half-done here; give back the read handles and push the log out.
    case APTHOOK_ONEXIT:
        FSO::Files::FilePool::CloseAll(); FlushLog(); break;
    default: break;
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    osSetSpeedupEnable(true); // New 3DS: 804 MHz + L2 cache (no effect on the old 3DS)
    gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, false);
    gfxSet3D(false);
    bool isNew3DS = false;
    APT_CheckNew3DS(&isNew3DS);
    aptHook(&g_aptCookie, OnAptEvent, nullptr);
    // New 3DS ZL/ZR/C-stick: libctru's hidInit starts ir:rst itself and merges ZL/ZR into hidKeysHeld()

    sdhook::Install(); // TS1(sims) hardware pass: SD operations counted + timed for log.txt
    mkdir(APP_ROOT, 0777);
    mkdir(SAVE_ROOT, 0777);
    mkdir(CACHE_DIR, 0777);
    if (!OpenLog()) {
        aptUnhook(&g_aptCookie);
        gfxExit();
        return 1;
    }
    g_performance.Reset(NowMs());
    // The optional debug file enables detailed load steps; quiet performance logs are always active.
    bool logSteps = false;
    if (FILE* dbg = fopen("sdmc:/3ds/OpenDollhouse/debug", "r")) {
        fclose(dbg);
        logSteps = true;
    }
    printf("sims: %s 3DS, application memory %.1f MB (region API raw free %.1f MB), app heap %.1f MB, linear heap %.1f MB (free %.1f MB)\n",
           isNew3DS ? "New" : "Old", osGetMemRegionSize(MEMREGION_APPLICATION) / 1048576.0,
           osGetMemRegionFree(MEMREGION_APPLICATION) / 1048576.0, __ctru_heap_size / 1048576.0,
           __ctru_linear_heap_size / 1048576.0, linearSpaceFree() / 1048576.0);
    FlushLog();

    const auto language = StartupLanguage();
    if (!language) {
        FlushLog();
        aptUnhook(&g_aptCookie);
        gfxExit();
        return 0;
    }

    // sound: SoundData/, Music/ and the pack folders next to GameData (src/audio/README.md); silent without them
    {
        simaudio::GameAudio::Config acfg;
        acfg.DataRoot = DATA_ROOT;
        acfg.OutputRate = 32728;
        if (simaudio::GameAudio::Get().Init(acfg))
            simaudio::GameAudio::Get().SetBackend(std::unique_ptr<simaudio::AudioBackend>(new simaudio::NdspBackend()));
    }

    App* app = new App();
    App::Config cfg;
    cfg.LanguageCode = *language;
    cfg.DataRoot = DATA_ROOT;
    cfg.SaveRoot = SAVE_ROOT;
    cfg.CacheDir = CACHE_DIR;
    cfg.ScrollBuffer = 64;      // 3 static layers x 464x304 x 8 bytes = 3.4 MB
    cfg.VMBudgetMs = 14.0;      // at most ~14 ms of SimAntics per frame; ultra speed degrades gracefully
    cfg.ThumbInterval = 2;      // touch-screen copy of the lot at half rate (saves ~1.5 M instructions/frame)
    cfg.NowMs = NowMs;
    // TS1(sims): one timestamped line per loading step in log.txt with the memory left (App::InstallLoadHooks)
    cfg.LogSteps = logSteps;
    // FIX(sims) hardware pass: "app free" was osGetMemRegionFree(APPLICATION), which never changes after start-up
    // (__system_allocateHeaps commits the whole app heap before main: 46.3 MB on every line of the first log was the
    // part of the 178 MB that is in neither heap). Now: what malloc can still hand out = the heap's size minus what
    // the arena took from it, plus the free blocks inside the arena.
    cfg.MemInfo = []() {
        struct mallinfo mi = mallinfo();
        double heapFree = ((double)__ctru_heap_size - (double)mi.arena + (double)mi.fordblks) / 1048576.0;
        char buf[192];
        snprintf(buf, sizeof buf, "heap used %.1f MB (arena %.1f MB of %.1f), heap free %.1f MB, linear free %.1f MB",
                 mi.uordblks / 1048576.0, mi.arena / 1048576.0, __ctru_heap_size / 1048576.0, heapFree, linearSpaceFree() / 1048576.0);
        return std::string(buf);
    };
    app->Present = [app]() { PresentFrame(*app, true); };
    ReportPerformance(*app,"startup",true);
    try {
        app->Init(cfg);
    } catch (std::bad_alloc&) {
        printf("init: out of memory\n");
        fflush(stdout);
    } catch (std::exception& e) {
        printf("init: %s\n", e.what());
        fflush(stdout);
    }

    AppInput in;
    double last = NowMs();
    bool wasTouching = false;
    while (aptMainLoop() && !app->QuitRequested) {
        const double poll=NowMs();
        auto& diagnostics=g_performance;
        if(diagnostics.LastPoll>0)diagnostics.MaxPollGap=std::max(diagnostics.MaxPollGap,poll-diagnostics.LastPoll);
        diagnostics.LastPoll=poll;++diagnostics.InputPolls;
        hidScanInput(); // also scans ir:rst on the New 3DS
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();
        auto map = [](u32 k) {
            uint32_t r = 0;
            if (k & KEY_A) r |= BTN_A;
            if (k & KEY_B) r |= BTN_B;
            if (k & KEY_X) r |= BTN_X;
            if (k & KEY_Y) r |= BTN_Y;
            if (k & KEY_L) r |= BTN_L;
            if (k & KEY_R) r |= BTN_R;
            if (k & KEY_ZL) r |= BTN_ZL;
            if (k & KEY_ZR) r |= BTN_ZR;
            if (k & KEY_START) r |= BTN_START;
            if (k & KEY_SELECT) r |= BTN_SELECT;
            if (k & KEY_DUP) r |= BTN_UP;      // d-pad only (KEY_UP also includes the circle pad)
            if (k & KEY_DDOWN) r |= BTN_DOWN;
            if (k & KEY_DLEFT) r |= BTN_LEFT;
            if (k & KEY_DRIGHT) r |= BTN_RIGHT;
            return r;
        };
        in.Down = map(down);
        in.Held = map(held);
        circlePosition cp;
        hidCircleRead(&cp);
        in.CircleX = cp.dx / 156.0f;
        in.CircleY = cp.dy / 156.0f;
        in.CStickX = in.CStickY = 0;
        circlePosition cs{};
        if (isNew3DS) {
            hidCstickRead(&cs);
            in.CStickX = cs.dx / 156.0f;
            in.CStickY = cs.dy / 156.0f;
        }
        bool touching = (held & KEY_TOUCH) != 0;
        if (touching) {
            touchPosition tp;
            hidTouchRead(&tp);
            in.TouchX = tp.px;
            in.TouchY = tp.py;
        }
        in.Touching = touching;
        in.TouchDown = touching && !wasTouching;
        in.TouchUp = !touching && wasTouching; // TouchX/Y keep the last position
        wasTouching = touching;
        if(down & ~KEY_TOUCH)++diagnostics.ButtonPolls;
        if(in.TouchDown)++diagnostics.TouchDowns;
        if(in.TouchUp)++diagnostics.TouchUps;
        if(cp.dx||cp.dy)++diagnostics.CirclePolls;
        if(cs.dx||cs.dy)++diagnostics.CStickPolls;
        diagnostics.CircleX=cp.dx;diagnostics.CircleY=cp.dy;
        diagnostics.CStickX=cs.dx;diagnostics.CStickY=cs.dy;
        diagnostics.TouchX=in.TouchX;diagnostics.TouchY=in.TouchY;

        double now = NowMs();
        double dt = now - last;
        last = now;
        if (dt > 250) dt = 250; // after a suspend (HOME menu / sleep)
        const double frameBegin=NowMs();
        diagnostics.InApp=true;diagnostics.AppBegin=frameBegin;
        bool frameCompleted=false;
        try {
            app->Frame(in, dt);
            frameCompleted=true;
        } catch (std::bad_alloc&) {
            ++diagnostics.FrameErrors;
            printf("frame: out of memory | %s\n", cfg.MemInfo().c_str());
            fflush(stdout);
        } catch (std::exception& e) {
            ++diagnostics.FrameErrors;
            printf("frame: %s\n", e.what());
            fflush(stdout);
        }
        diagnostics.InApp=false;
        const double frameMs=NowMs()-frameBegin;
        diagnostics.Window.Frame(frameMs,frameMs,frameCompleted ? app->Stats.Vm : 0.0,
            frameCompleted ? app->Stats.World : 0.0,frameCompleted ? app->Stats.Ui : 0.0,
            frameCompleted ? static_cast<uint64_t>(std::max(app->Stats.Updates,0)) : 0);
        PresentFrame(*app);
    }
    ReportPerformance(*app,"shutdown",true);
    delete app;
    simaudio::GameAudio::Get().Shutdown();
    FlushLog();
    aptUnhook(&g_aptCookie);
    gfxExit();
    return 0;
}
