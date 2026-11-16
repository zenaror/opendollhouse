/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. See https://mozilla.org/MPL/2.0/. */
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include "app/App.h"
#include "app/front/FrontEnd.h"
#include "audio/GameAudio.h"
#include "core/png.h"
#include "SdlAudio.h"
#include "InputSequence.h"
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
#include "SdlCanvasUpload.h"
#include "SdlPortableLanguage.h"
#include "app/HandheldLanguage.h"
#endif
#include "SdlPortableInput.h"
#include "SdlCanvasWindow.h"
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH) || defined(__ANDROID__)
#define ODH_PORTABLE_DIAGNOSTICS 1
#include "ConsoleDiagnostics.h"
#include <malloc.h>
#endif
#if defined(ODH_PLATFORM_VITA)
#include <psp2/kernel/sysmem.h>
#include "../vita/StdioCapture.h"
#elif defined(ODH_PLATFORM_SWITCH)
#include <switch.h>
#elif defined(__ANDROID__)
#include "../android/AndroidDiagnostics.h"
#endif
#include "app/buy/UIObjectHolder.h"
#include "DesktopHotkeys.h"
#include "SdlOriginalCursor.h"
#include "language_picker.hpp"
#include "reference_patch_setup.hpp"
#if !defined(ODH_PLATFORM_VITA) && !defined(ODH_PLATFORM_SWITCH)
#include "DesktopPaths.h"
#endif
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__)
#define ODH_DESKTOP_LANGUAGE_SETUP 1
#include "linux_language_setup.hpp"
#include "app/ui/OriginalTextBackend.h"
#endif
#ifdef ODH_DESKTOP_PANGO
#include "linux_text_runtime.hpp"
#endif
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
#include "../console/DevicePaths.h"
#endif
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;

struct Options {
    fs::path data, saves, cache, screenshot, patches, importPatches;
    int width = 800, height = 600, frames = 0;
    uint8_t language = 1;
    bool skipIntro = false, silent = false;
    bool languageExplicit = false, chooseLanguage = false;
};

void Usage() {
    std::puts("OpenDollhouse — native desktop application\n"
        "Usage: opendollhouse [options]\n\n"
        "  --data-root DIR    Installed data override (default: parent of odl-launcher)\n"
        "  --save-root DIR    Writable saves override (default: user profile)\n"
        "  --cache-dir DIR    Writable cache override (default: user profile)\n"
        "  --resource-patches DIR  TS1 patches override for this run (read source)\n"
        "  --import-reference-patches DIR  Copy and select your four reference PIFF files\n"
        "  --width N         Canvas width, at least 800 (default 800)\n"
        "  --height N        Canvas height, at least 600 (default 600)\n"
        "  --language CODE   TS1 code 1..20 or locale tag (for example en-US, pt-BR)\n"
        "  --choose-language Show the startup language picker on supported hosts\n"
        "  --silent          Disable audio output\n"
        "  --skip-intro      Skip the introduction\n"
        "  --frames N        Diagnostic: exit after N frames without saving\n"
        "  --screenshot PNG  Diagnostic: save the single PC canvas on exit\n"
        "  --help            Show this help without opening a window\n\n"
        "The PC uses one screen. Saves, cache and data directories must not overlap.");
}

int Integer(const std::string& value, int low, int high) {
    size_t used = 0;
    int result = 0;
    try { result = std::stoi(value, &used); }
    catch (...) { throw std::runtime_error("Invalid number: " + value); }
    if (used != value.size() || result < low || result > high)
        throw std::runtime_error("Number out of range: " + value);
    return result;
}

Options Parse(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        auto value = [&]() -> std::string {
            if (++i >= argc || !argv[i][0]) throw std::runtime_error("Missing value for " + key);
            return argv[i];
        };
        if (key == "--data-root") result.data = value();
        else if (key == "--save-root") result.saves = value();
        else if (key == "--cache-dir") result.cache = value();
        else if (key == "--resource-patches") result.patches = value();
        else if (key == "--import-reference-patches") result.importPatches = value();
        else if (key == "--language") {
            const auto code = value();
            const auto parsed=odh_host::parse_game_language(code);
            if(!parsed)throw std::runtime_error("Unsupported language code: "+code);
            result.language=*parsed;result.languageExplicit=true;
        }
        else if (key == "--width") result.width = Integer(value(), 800, 3840);
        else if (key == "--height") result.height = Integer(value(), 600, 2160);
        else if (key == "--frames") result.frames = Integer(value(), 1, 10000000);
        else if (key == "--screenshot") result.screenshot = value();
        else if (key == "--skip-intro") result.skipIntro = true;
        else if (key == "--choose-language") result.chooseLanguage = true;
        else if (key == "--silent") result.silent = true;
        else throw std::runtime_error("Unknown option: " + key);
    }
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
    if (result.data.empty() || result.saves.empty() || result.cache.empty())
        throw std::runtime_error("--data-root, --save-root and --cache-dir are required; use --help.");
#endif
#if !defined(ODH_DESKTOP_LANGUAGE_SETUP) && !defined(ODH_PLATFORM_VITA) && !defined(ODH_PLATFORM_SWITCH)
    if(result.chooseLanguage && !result.languageExplicit)
        throw std::runtime_error("Use --language on this platform.");
#endif
    return result;
}

bool Contains(const fs::path& parent, const fs::path& child) {
    auto p = parent.begin(), c = child.begin();
    for (; p != parent.end(); ++p, ++c)
        if (c == child.end() || *p != *c) return false;
    return true;
}

void Prepare(Options& o) {
#if !defined(ODH_PLATFORM_VITA) && !defined(ODH_PLATFORM_SWITCH)
    if (o.data.empty()) {
        std::unique_ptr<char, decltype(&SDL_free)> base(SDL_GetBasePath(), SDL_free);
        o.data = odh_host::desktop_installed_data_root(base ? fs::u8path(base.get()) : fs::path{});
    }
    const auto environment = [](const char* name) {
#ifdef _WIN32
        // Environment values are UTF-16 on Windows; narrow getenv uses the ACP.
        std::wstring wideName;
        for (const char c : std::string(name)) wideName.push_back(static_cast<wchar_t>(c));
        const wchar_t* value = _wgetenv(wideName.c_str());
        return value ? fs::path(value).u8string() : std::string{};
#else
        const char* value = std::getenv(name); return value ? std::string(value) : std::string{};
#endif
    };
    odh_host::DesktopUserEnvironment user{environment("ODH_SAVE_ROOT"), environment("ODH_CACHE_ROOT"),
        environment("XDG_DATA_HOME"), environment("XDG_CACHE_HOME"), environment("HOME")};
#if defined(__linux__) && !defined(__ANDROID__)
    odh_host::desktop_writable_defaults(o.saves, o.cache, {}, user, true);
#else
    // Fully explicit paths do not need a writable platform preference directory.
    std::unique_ptr<char, decltype(&SDL_free)> prefs(
        ((o.saves.empty() && user.saves.empty()) || (o.cache.empty() && user.cache.empty()))
            ? SDL_GetPrefPath(nullptr, "opendollhouse") : nullptr, SDL_free);
    odh_host::desktop_writable_defaults(o.saves, o.cache,
        prefs ? fs::u8path(prefs.get()) : fs::path{}, user, false);
#endif
#endif
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
#ifdef ODH_PLATFORM_VITA
    const std::string mount = "ux0";
#else
    const std::string mount = "sdmc";
#endif
    o.data = odh::console::DevicePath(o.data.generic_string(), mount);
    o.saves = odh::console::DevicePath(o.saves.generic_string(), mount);
    o.cache = odh::console::DevicePath(o.cache.generic_string(), mount);
    if (!o.patches.empty()) o.patches = odh::console::DevicePath(o.patches.generic_string(), mount);
#else
    o.data = fs::canonical(o.data);
    o.saves = fs::weakly_canonical(fs::absolute(o.saves));
    o.cache = fs::weakly_canonical(fs::absolute(o.cache));
    if (!o.patches.empty()) o.patches = fs::canonical(o.patches);
#endif
    if (!o.patches.empty() && !fs::is_directory(o.patches))
        throw std::runtime_error("The resource patch directory is not available: " + o.patches.string());
    if (!fs::is_directory(o.data / "GameData"))
        throw std::runtime_error("The data directory does not contain GameData: " + o.data.string());
    const fs::path roots[] = {o.data, o.saves, o.cache};
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (Contains(roots[i], roots[j]) || Contains(roots[j], roots[i]))
                throw std::runtime_error("Data, save and cache directories must be separate and must not contain one another.");
    if (!o.screenshot.empty()) {
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
        o.screenshot = odh::console::DevicePath(o.screenshot.generic_string(), mount);
#else
        o.screenshot = fs::weakly_canonical(fs::absolute(o.screenshot));
#endif
        if (Contains(o.data, o.screenshot) || Contains(o.saves, o.screenshot))
            throw std::runtime_error("Diagnostic screenshots must be outside game data and saves.");
    }
    odh_host::validate_reference_patch_source(o.saves,o.importPatches);
    odh_host::validate_reference_patch_source(o.cache,o.importPatches);
    if (!o.screenshot.empty()) odh_host::validate_reference_patch_source(o.screenshot,o.importPatches);
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
    odh::console::EnsureDeviceDirectory(o.saves.generic_string());
    odh::console::EnsureDeviceDirectory(o.cache.generic_string());
#else
    fs::create_directories(o.saves);
    fs::create_directories(o.cache);
#endif
}

void OpenLog(const fs::path& path) {
    // Keep upstream printf diagnostics and platform errors together for crash reports.
#if defined(ODH_PLATFORM_VITA)
    // SDK consumers use both legacy and thread-local newlib FILE objects.
    odh::vita::CaptureStdio(path.string().c_str());
#else
    FILE* log = std::fopen(path.string().c_str(), "a");
    if (!log) throw std::runtime_error("Cannot open log: " + path.string());
    std::printf("Log: %s\n", path.string().c_str());
    std::fflush(stdout);
#ifdef _WIN32
    const int resultOut = _dup2(_fileno(log), _fileno(stdout));
    const int resultErr = _dup2(_fileno(log), _fileno(stderr));
#else
    const int resultOut = dup2(fileno(log), fileno(stdout));
    const int resultErr = dup2(fileno(log), fileno(stderr));
#endif
    std::fclose(log);
    if (resultOut < 0 || resultErr < 0) throw std::runtime_error("Cannot redirect application log.");
#endif
#if !defined(ODH_PLATFORM_VITA)
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#endif
}

double NowMs() {
    return SDL_GetPerformanceCounter() * 1000.0 / SDL_GetPerformanceFrequency();
}

uint32_t Button(SDL_Keycode key) {
    using namespace simapp;
    switch (key) {
    case SDLK_RETURN: return BTN_A;
    case SDLK_ESCAPE: return BTN_B;
    case SDLK_F5: return BTN_START;
    case SDLK_COMMA: return BTN_L;
    case SDLK_PERIOD: return BTN_R;
    case SDLK_PAGEUP: return BTN_ZR;
    case SDLK_PAGEDOWN: return BTN_ZL;
    default: return 0;
    }
}

#ifdef ODH_DESKTOP_LANGUAGE_SETUP
std::optional<uint8_t> ChooseLanguage(SDL_Window* window,SDL_Renderer* renderer,SDL_Texture* texture,
        int width,int height,uint8_t initial,bool invalidSettings) {
    simapp::original::InstallTextBackendIfNeeded();
    odh_host::LanguagePicker picker(initial);
#ifdef ODH_DESKTOP_PANGO
    constexpr bool ascii=false;
#else
    constexpr bool ascii=true;
#endif
    const auto windowID=SDL_GetWindowID(window);
    bool dirty=true;std::optional<std::pair<int,int>> pressed;
    for(;;){
        if(dirty){
            const auto frame=picker.draw(width,height,ascii,true,invalidSettings?
                "Unreadable settings: confirm to preserve a backup and create new settings.":"");
            SDL_SetWindowTitle(window,picker.title(ascii).c_str());
            if(SDL_UpdateTexture(texture,nullptr,frame.pixels.data(),width*4)!=0 ||
               SDL_RenderClear(renderer)!=0 || SDL_RenderCopy(renderer,texture,nullptr,nullptr)!=0)
                throw std::runtime_error(std::string("Language picker presentation failed: ")+SDL_GetError());
            SDL_RenderPresent(renderer);dirty=false;
        }
        SDL_Event event;
        if(!SDL_WaitEventTimeout(&event,100))continue;
        if(event.type==SDL_QUIT)return {};
        if(event.type==SDL_KEYDOWN && event.key.windowID==windowID && !event.key.repeat){
            const auto key=event.key.keysym.sym;
            if(key==SDLK_ESCAPE)return {};
            if(key==SDLK_RETURN||key==SDLK_KP_ENTER)return picker.language();
            if(key==SDLK_TAB||key==SDLK_DOWN||key==SDLK_UP){
                picker.move(key==SDLK_UP||(key==SDLK_TAB&&(event.key.keysym.mod&KMOD_SHIFT))?-1:1);dirty=true;
            }
        }
        if(event.type==SDL_WINDOWEVENT && event.window.windowID==windowID){
            dirty=true;if(event.window.event==SDL_WINDOWEVENT_FOCUS_LOST)pressed.reset();
        }
        if((event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP) &&
           event.button.windowID==windowID && event.button.button==SDL_BUTTON_LEFT){
            // SDL supplies logical coordinates in this existing window.
            const auto point=std::make_pair(event.button.x,event.button.y);
            if(event.type==SDL_MOUSEBUTTONDOWN)pressed=point;
            else {
                auto down=picker,up=picker;
                if(pressed && down.click(pressed->first,pressed->second,width) &&
                   up.click(point.first,point.second,width) && down.language()==up.language())return up.language();
                pressed.reset();
            }
        }
    }
}
#endif

struct SdlLifetime { ~SdlLifetime() { SDL_Quit(); } };
struct AudioLifetime { ~AudioLifetime() { simaudio::GameAudio::Get().Shutdown(); } };

#if defined(ODH_PORTABLE_DIAGNOSTICS)
#if defined(ODH_PLATFORM_VITA)
constexpr const char* ConsolePerfTag="vita-perf";
#elif defined(ODH_PLATFORM_SWITCH)
constexpr const char* ConsolePerfTag="switch-perf";
#else
constexpr const char* ConsolePerfTag="android-perf";
#endif
// Allocator free blocks are separate from the platform's process or kernel
// memory measurements; report those domains independently.
void ConsoleMemoryText(char* buffer, size_t size) {
#if defined(__ANDROID__)
    odh::android::MemoryText(buffer,size);
#else
    const auto heap = mallinfo();
#if defined(ODH_PLATFORM_VITA)
    SceKernelFreeMemorySizeInfo freeMemory{};
    freeMemory.size = sizeof(freeMemory);
    const int result = sceKernelGetFreeMemorySize(&freeMemory);
    std::snprintf(buffer, size,
        "heap_used=%llu heap_arena=%llu heap_free_blocks=%llu os_user_free=%d cdram_free=%d phycont_free=%d mem_status=%d",
        static_cast<unsigned long long>(heap.uordblks), static_cast<unsigned long long>(heap.arena),
        static_cast<unsigned long long>(heap.fordblks), result < 0 ? -1 : freeMemory.size_user,
        result < 0 ? -1 : freeMemory.size_cdram, result < 0 ? -1 : freeMemory.size_phycont, result);
#else
    u64 total=0,used=0;
    const Result totalResult=svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);
    const Result usedResult=svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
    std::snprintf(buffer,size,
        "heap_used=%llu heap_arena=%llu heap_free_blocks=%llu process_total=%llu process_used=%llu total_status=%u used_status=%u",
        static_cast<unsigned long long>(heap.uordblks),static_cast<unsigned long long>(heap.arena),
        static_cast<unsigned long long>(heap.fordblks),static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(used),unsigned(totalResult),unsigned(usedResult));
#endif
#endif
}
std::string ConsoleMemoryInfo() { char buffer[256]; ConsoleMemoryText(buffer, sizeof(buffer)); return buffer; }
void ConsoleStage(const char* stage) {
    char memory[256]; ConsoleMemoryText(memory, sizeof(memory));
    std::printf("[%s] ms=%.1f stage=%s %s\n", ConsolePerfTag, NowMs(), stage, memory);
    std::fflush(stdout);
}
#endif

int Run(Options options) {
    Prepare(options);
    OpenLog(options.cache / "opendollhouse.log");
#if defined(ODH_PORTABLE_DIAGNOSTICS)
    ConsoleStage("startup");
#endif
    const auto resourcePatches=odh_host::prepare_reference_patches(options.saves,options.importPatches,options.patches);
    options.patches=resourcePatches.root;
    std::printf("resource patches: %s\n",resourcePatches.diagnostic.c_str());
    std::printf("\nOpenDollhouse desktop: single canvas %dx%d\n", options.width, options.height);
#ifdef ODH_DESKTOP_LANGUAGE_SETUP
    odh_host::LinuxLanguageSetup languageSetup;
    if(!options.languageExplicit){
        languageSetup.load();
        options.language=odh_host::game_language_or_english(languageSetup.config.language_code);
        if(!languageSetup.readable)std::fprintf(stderr,"language settings: %s\n",languageSetup.diagnostic.c_str());
    }
#endif
    SDL_SetMainReady();
#if defined(ODH_PLATFORM_VITA)
    constexpr auto portableProfile=odh::desktop::PortableInputProfile::Vita;
#elif defined(ODH_PLATFORM_SWITCH)
    constexpr auto portableProfile=odh::desktop::PortableInputProfile::Switch;
#elif defined(__ANDROID__)
    constexpr auto portableProfile=odh::desktop::PortableInputProfile::Android;
#else
    constexpr auto portableProfile=odh::desktop::PortableInputProfile::Disabled;
#endif
    constexpr bool portableEnabled=portableProfile!=odh::desktop::PortableInputProfile::Disabled;
    odh::desktop::SdlPortableInput::Configure(portableProfile);
    const auto controllerFlags=portableEnabled?(SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER):0u;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS | controllerFlags) != 0)
        throw std::runtime_error(std::string("SDL initialization failed: ") + SDL_GetError());
    SdlLifetime sdlLifetime;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    const SDL_Point* fixedDisplay=nullptr;
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
    SDL_Point physicalSize{};
#endif
#if defined(ODH_PLATFORM_VITA)
    SDL_DisplayMode displayMode{};
    if(SDL_GetCurrentDisplayMode(0,&displayMode)!=0||displayMode.w<=0||displayMode.h<=0)
        throw std::runtime_error(std::string("Cannot query Vita display: ")+SDL_GetError());
    physicalSize={displayMode.w,displayMode.h};fixedDisplay=&physicalSize;
#elif defined(ODH_PLATFORM_SWITCH)
    // SDL's Switch mode list is static1920x1080, whereas the native layer
    // reports its initial buffer extent (normally1280x720). Query BEFORE
    // SDL_CreateWindow changes that extent to the requested window size.
    u32 nativeWidth=0,nativeHeight=0;
    const Result dimensionsResult=nwindowGetDimensions(nwindowGetDefault(),&nativeWidth,&nativeHeight);
    if(R_FAILED(dimensionsResult)||!nativeWidth||!nativeHeight||nativeWidth>16384||nativeHeight>16384)
        throw std::runtime_error("Cannot query Switch native window dimensions: "+std::to_string(dimensionsResult));
    physicalSize={int(nativeWidth),int(nativeHeight)};fixedDisplay=&physicalSize;
#endif
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        odh::desktop::CreateCanvasWindow(options.width,options.height,fixedDisplay),SDL_DestroyWindow);
    if (!window) throw std::runtime_error(std::string("Cannot create window: ") + SDL_GetError());
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC), SDL_DestroyRenderer);
    if (!renderer) renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE));
    if (!renderer) throw std::runtime_error(std::string("Cannot create renderer: ") + SDL_GetError());
    if (!portableEnabled && SDL_RenderSetLogicalSize(renderer.get(), options.width, options.height) != 0)
        throw std::runtime_error(std::string("Cannot set canvas size: ") + SDL_GetError());
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(
        SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,
            options.width, options.height), SDL_DestroyTexture);
    if (!texture) throw std::runtime_error(std::string("Cannot create screen texture: ") + SDL_GetError());

    odh::desktop::SdlPortableInput portable(window.get(),renderer.get(),options.width,options.height,portableProfile);
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
    std::printf("[presentation] requested_native=%dx%d engine_canvas=%dx%d fixed_display=1\n",
        physicalSize.x,physicalSize.y,options.width,options.height);
    portable.LogGeometry("startup");
#elif defined(__ANDROID__)
    portable.LogGeometry("startup");
#endif
    simapp::AppInput cursorInput;cursorInput.TouchX=cursorInput.TouchY=-1;
    const odh::CursorImage* softwareCursor=nullptr;
    std::string softwareCursorKey;
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
    if (!options.languageExplicit) {
        const auto saveRoot = options.saves.generic_string();
        const auto saved = simapp::ReadHandheldLanguage(saveRoot);
        if (!saved || options.chooseLanguage || portable.LanguageSelectionRequested()) {
            const auto chosen = odh::desktop::ChoosePortableLanguage(window.get(), renderer.get(), texture.get(),
                portable, options.width, options.height, saved.value_or(options.language), false);
            if (!chosen && (!saved || SDL_HasEvent(SDL_QUIT))) return 0;
            if (chosen && !simapp::WriteHandheldLanguage(saveRoot, *chosen))
                throw std::runtime_error("Could not save the language in " + saveRoot + "/language.txt");
            options.language = chosen.value_or(saved.value_or(options.language));
        } else {
            options.language = *saved;
        }
        std::printf("[language] selected=%s code=%u\n", odh_host::game_languages[options.language-1].tag,
                    unsigned(options.language));
        std::fflush(stdout);
    }
#endif
    AudioLifetime audioLifetime;
#ifdef ODH_DESKTOP_PANGO
    // Keep the preserved font-map cache for the complete UI session instead
    // of creating a separate map for every measurement and label.
    odh_host::LinuxTextRuntime textRuntime;
#endif
#ifdef ODH_DESKTOP_LANGUAGE_SETUP
    if(!options.languageExplicit && (options.chooseLanguage || languageSetup.needs_choice())){
        const auto chosen=ChooseLanguage(window.get(),renderer.get(),texture.get(),options.width,options.height,
            options.language,!languageSetup.readable);
        if(!chosen)return 0;
        fs::path backup;std::string error;
        if(!languageSetup.confirm(*chosen,backup,error)){
            const std::string message="Could not save the selected language. "+error+
                (backup.empty()?"":" Previous settings are preserved at "+backup.string());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"OpenDollhouse",message.c_str(),window.get());
            throw std::runtime_error(message);
        }
        if(!backup.empty())std::printf("language settings preserved at %s\n",backup.string().c_str());
        options.language=*chosen;
        SDL_SetWindowTitle(window.get(),"OpenDollhouse");
        // No picker key/release can activate the next screen.
        SDL_FlushEvents(SDL_KEYDOWN,SDL_KEYUP);
        SDL_FlushEvents(SDL_MOUSEBUTTONDOWN,SDL_MOUSEBUTTONUP);
    }
#endif
    simapp::App app;
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
    odh::desktop::SdlCanvasUpload canvasUpload;
#endif
#if defined(ODH_PORTABLE_DIAGNOSTICS)
    odh::desktop::ConsoleFrameWindow performance(NowMs());
    auto performanceIo = FSO::Files::FilePool::Stats;
#if defined(ODH_PLATFORM_VITA) || defined(__ANDROID__)
    struct {
        unsigned long long Polls=0, Events=0, Axes=0, RightStick=0, Touches=0, Front=0, Rear=0, Delivered=0;
        double LastPoll=0, MaxPollGap=0;
        int TouchX=-1, TouchY=-1;
        size_t Queued=0, QueuePeak=0;
    } inputDiagnostics;
#endif
    auto performanceReport = [&](const char* reason, bool force = false) {
        const double now = NowMs();
        if (!force && !performance.Due(now)) return;
        if (!performance.Frames && !performance.Presents && !force) return;
        const auto io = FSO::Files::FilePool::Stats;
        const auto delta = [](uint64_t current, uint64_t previous) { return current >= previous ? current - previous : current; };
        const double frames = performance.Frames ? double(performance.Frames) : 1.0;
        const double presents = performance.Presents ? double(performance.Presents) : 1.0;
        const char* phase = "unknown";
        switch (app.CurrentMode()) {
        case simapp::App::Mode::Boot: phase = "boot"; break;
        case simapp::App::Mode::Front: phase = "front"; break;
        case simapp::App::Mode::Loading: phase = "loading-lot"; break;
        case simapp::App::Mode::Live: phase = "lot"; break;
        case simapp::App::Mode::Error: phase = "error"; break;
        }
        char memory[256]; ConsoleMemoryText(memory, sizeof(memory));
        const auto audio = simaudio::GameAudio::Get().Backend();
        std::printf("[%s] reason=%s phase=%s window_ms=%.1f completed=%llu presents=%llu fps=%.2f "
            "work_avg_ms=%.2f work_p95_upper_ms=%.2f work_max_ms=%.2f app_avg_ms=%.2f vm_avg_ms=%.2f world_avg_ms=%.2f ui_avg_ms=%.2f "
            "upload_avg_ms=%.2f present_avg_ms=%.2f updates=%llu io_bytes=%llu io_reads=%llu io_opens=%llu io_seeks=%llu pool_open=%d "
            "audio=%s audio_latency_ms=%.2f %s\n", ConsolePerfTag, reason, phase, now - performance.Since,
            static_cast<unsigned long long>(performance.Frames), static_cast<unsigned long long>(performance.Presents), performance.Fps(now),
            performance.Work / frames, performance.P95Upper(), performance.WorkMax, performance.App / frames,
            performance.Vm / frames, performance.World / frames, performance.Ui / frames, performance.Upload / presents,
            performance.Present / presents, static_cast<unsigned long long>(performance.Updates),
            static_cast<unsigned long long>(delta(io.BytesRead, performanceIo.BytesRead)),
            static_cast<unsigned long long>(delta(io.Reads, performanceIo.Reads)),
            static_cast<unsigned long long>(delta(io.Opens, performanceIo.Opens)),
            static_cast<unsigned long long>(delta(io.Seeks, performanceIo.Seeks)), FSO::Files::FilePool::OpenCount(),
            audio ? audio->Name() : "off", audio ? audio->LatencyMs() : 0.0, memory);
#if defined(ODH_PLATFORM_VITA) || defined(__ANDROID__)
        // Read the already opened mapped controller without pumping SDL again.
        auto* diagnosticPad = portable.HasController()
            ? SDL_GameControllerFromInstanceID(portable.ControllerId()) : nullptr;
#if defined(__ANDROID__)
        constexpr const char* inputTag="android-input";
#else
        constexpr const char* inputTag="vita-input";
#endif
        std::printf("[%s] phase=%s polls=%llu poll_gap_max_ms=%.2f events=%llu axis_events=%llu rightstick_events=%llu touch_events=%llu "
#if defined(ODH_PLATFORM_VITA)
            "front_events=%llu rear_events=%llu "
#endif
            "delivered=%llu controller=%d focus=%d right_raw_x=%d right_raw_y=%d "
            "touch_x=%d touch_y=%d queued=%llu queue_peak=%llu\n", inputTag, phase, inputDiagnostics.Polls, inputDiagnostics.MaxPollGap,
            inputDiagnostics.Events, inputDiagnostics.Axes, inputDiagnostics.RightStick, inputDiagnostics.Touches,
#if defined(ODH_PLATFORM_VITA)
            inputDiagnostics.Front,inputDiagnostics.Rear,
#endif
            inputDiagnostics.Delivered, int(diagnosticPad!=nullptr), int(portable.Focused()),
            diagnosticPad ? int(SDL_GameControllerGetAxis(diagnosticPad,SDL_CONTROLLER_AXIS_RIGHTX)) : 0,
            diagnosticPad ? int(SDL_GameControllerGetAxis(diagnosticPad,SDL_CONTROLLER_AXIS_RIGHTY)) : 0,
            inputDiagnostics.TouchX, inputDiagnostics.TouchY, static_cast<unsigned long long>(inputDiagnostics.Queued),
            static_cast<unsigned long long>(inputDiagnostics.QueuePeak));
        inputDiagnostics.Polls=inputDiagnostics.Events=inputDiagnostics.Axes=inputDiagnostics.RightStick=0;
        inputDiagnostics.Touches=inputDiagnostics.Front=inputDiagnostics.Rear=inputDiagnostics.Delivered=0;
        inputDiagnostics.MaxPollGap=0;inputDiagnostics.QueuePeak=inputDiagnostics.Queued;
#endif
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
        std::printf("[portable-display] canvas_bytes=%llu presents=%llu\n",
            static_cast<unsigned long long>(canvasUpload.bytes),static_cast<unsigned long long>(performance.Presents));
        canvasUpload.bytes=0;
#endif
        std::fflush(stdout); // preserve the latest sample if the next step fails
        performance.Reset(now); performanceIo = io;
    };
    SDL_RendererInfo rendererInfo{};
    if (SDL_GetRendererInfo(renderer.get(), &rendererInfo) == 0)
        std::printf("[%s] canvas=%dx%d renderer=%s flags=%u interval_ms=5000 p95=bucket-upper-bound osd=off\n",
            ConsolePerfTag, options.width, options.height, rendererInfo.name ? rendererInfo.name : "unknown", unsigned(rendererInfo.flags));
    ConsoleStage("app-init-pending");
#endif
    bool renderFailed = false;
    auto present = [&]() {
        const auto& canvas = app.DesktopCanvas();
        if (canvas.Width != options.width || canvas.Height != options.height) {
            renderFailed = true;
            std::fprintf(stderr, "desktop: engine returned unexpected canvas size %dx%d\n", canvas.Width, canvas.Height);
            return;
        }
        const auto portableCanvas=portable.Enabled()?portable.Canvas():odh::desktop::PortableCanvas{};
#if defined(ODH_PORTABLE_DIAGNOSTICS)
        const double uploadStarted = NowMs();
#endif
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
        const bool uploaded=canvasUpload.Upload(texture.get(),canvas.Px,canvas.Width,canvas.Height);
        SDL_Rect* paintedCursor=&canvasUpload.paintedCursor;
#else
        const bool uploaded=SDL_UpdateTexture(texture.get(), nullptr, canvas.Px.data(), canvas.Width * sizeof(uint32_t))==0;
        SDL_Rect* paintedCursor=nullptr;
#endif
        if (!uploaded
            || (portable.Enabled() && portable.Focused() && !odh::desktop::SdlPortableInput::UploadCursor(
                texture.get(),canvas.Px,canvas.Width,canvas.Height,cursorInput,softwareCursor,false,paintedCursor))
            || SDL_RenderClear(renderer.get()) != 0
            || SDL_RenderCopy(renderer.get(), texture.get(), nullptr, portable.Enabled()?&portableCanvas.Box:nullptr) != 0) {
            renderFailed = true;
            std::fprintf(stderr, "desktop: presentation failed: %s\n", SDL_GetError());
            return;
        }
#if defined(ODH_PORTABLE_DIAGNOSTICS)
        const double uploadDone = NowMs();
#endif
        SDL_RenderPresent(renderer.get());
#if defined(ODH_PORTABLE_DIAGNOSTICS)
        performance.Presentation(uploadDone - uploadStarted, NowMs() - uploadDone);
#endif
        if (auto backend = simaudio::GameAudio::Get().Backend()) backend->Pump(0);
#if defined(ODH_PORTABLE_DIAGNOSTICS)
        // App::Present is also called by blocking loading progress callbacks.
        // Report presentations separately rather than inventing completed FPS.
        performanceReport("periodic");
#endif
    };
    app.Present = present;
    simapp::App::Config config;
    config.DataRoot = options.data.generic_string();
    config.ResourcePatchRoot = options.patches.generic_string();
    config.SaveRoot = options.saves.generic_string();
    config.CacheDir = options.cache.generic_string();
    config.LanguageCode = options.language;
    config.Desktop = true;
    config.DesktopWidth = options.width;
    config.DesktopHeight = options.height;
    config.InitAudio = [&](bool loading, int musicVolume) {
#if defined(ODH_PORTABLE_DIAGNOSTICS)
        ConsoleStage("audio-init-begin");
#endif
        if (!options.silent) {
            simaudio::GameAudio::Config audioConfig;
            audioConfig.DataRoot = options.data.generic_string();
            audioConfig.OutputRate = 48000;
            audioConfig.MusicVolume = musicVolume / 100.0f;
            simaudio::GameAudio::Get().Init(audioConfig, std::make_unique<odh::desktop::SdlAudio>(), loading);
        }
#if defined(ODH_PORTABLE_DIAGNOSTICS)
        ConsoleStage("audio-init-end");
#endif
    };
    config.SkipIntro = options.skipIntro;
    config.NowMs = NowMs;
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
    // Like the 3DS host, yield between complete VM updates. An overloaded
    // lot must service input and audio before trying to catch up again.
    config.VMBudgetMs = 14.0;
    std::printf("[portable-perf] VM budget %.0f ms per frame\n", config.VMBudgetMs);
#endif
#if defined(ODH_PLATFORM_VITA)
    config.HalfResolutionLot = true;
#endif
    config.LogSteps = true;
#if defined(ODH_PORTABLE_DIAGNOSTICS)
    config.MemInfo = ConsoleMemoryInfo; // existing flushed load-step/progress/heartbeat lines
    ConsoleStage("app-init-begin");
#endif
    if (!app.Init(config)) throw std::runtime_error("Game initialization failed; inspect preceding log entries.");
#if defined(ODH_PORTABLE_DIAGNOSTICS)
    ConsoleStage("app-init-end");
#endif
    // Mobile and console SDL backends open a software keyboard on StartTextInput.
    // Request it only while the engine has a focused text field.
    const bool softwareKeyboard = SDL_HasScreenKeyboardSupport() == SDL_TRUE;
    // Desktop text events stay enabled so typing queued just after a click is retained.
    if (softwareKeyboard) SDL_StopTextInput();
    else SDL_StartTextInput();
    bool textInputActive = !softwareKeyboard;

    double last = NowMs();
    using InputKind=odh::desktop::InputSequence::Kind;
    odh::desktop::InputSequence inputs;
    simapp::AppInput state;
    state.DesktopPointer=true;state.TouchX=state.TouchY=-1;portable.InitialState(state);
    std::array<bool,SDL_NUM_SCANCODES> keyboard{};
    bool suppressPrimary=false,suppressSecondary=false;
    // The neighbourhood precedes LotScreen, which used to initialize this
    // service only after entering a house. Native cursor misses are cached,
    // so bind the validated installation before the first pointer selection.
    simapp::UIGraphics::Get().Init(options.data.generic_string());
    odh::desktop::SdlOriginalCursor originalCursor;
    const bool traceInput=std::getenv("ODH_TRACE_INPUT")!=nullptr;
    unsigned traceLines=0;
    const Uint32 windowId=SDL_GetWindowID(window.get());
    auto resetCapture=[&](const char* reason) {
        if(traceInput && traceLines++<256)std::printf("[desktop-input] cancel=%s queued=%zu observedHeld=%d\n",reason,inputs.Pending(),int(state.Touching));
        suppressPrimary=suppressPrimary || state.Touching;
        suppressSecondary=suppressSecondary || state.MouseRightHeld;
        inputs.Cancel();keyboard.fill(false);portable.Reset(state);
        state=simapp::AppInput();state.DesktopPointer=true;state.TouchX=state.TouchY=-1;
    };
    auto keyboardState=[&](SDL_Keymod modifiers) {
        state.DesktopCtrl=(modifiers&KMOD_CTRL)!=0;
        state.DesktopShift=(modifiers&KMOD_SHIFT)!=0;
        state.CircleX=float(keyboard[SDL_SCANCODE_RIGHT])-float(keyboard[SDL_SCANCODE_LEFT]);
        state.CircleY=float(keyboard[SDL_SCANCODE_UP])-float(keyboard[SDL_SCANCODE_DOWN]);
    };
    int frame = 0;
    simapp::FramePresentation lastPresented;
    bool havePresented = false;
    while (!app.QuitRequested && !renderFailed) {
        const double begin = NowMs();
#if defined(ODH_PLATFORM_VITA) || defined(__ANDROID__)
        if(inputDiagnostics.LastPoll>0)
            inputDiagnostics.MaxPollGap=std::max(inputDiagnostics.MaxPollGap,begin-inputDiagnostics.LastPoll);
        inputDiagnostics.LastPoll=begin;++inputDiagnostics.Polls;
#endif
        inputs.SetFocused(portable.Enabled()?portable.Focused():SDL_GetKeyboardFocus()==window.get());
        inputs.BeginPoll();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
            if(event.type==SDL_RENDER_DEVICE_RESET || event.type==SDL_RENDER_TARGETS_RESET)
                canvasUpload.Invalidate();
#endif
#if defined(ODH_PLATFORM_VITA) || defined(__ANDROID__)
            inputDiagnostics.QueuePeak=std::max(inputDiagnostics.QueuePeak,inputs.Pending());
            ++inputDiagnostics.Events;
            if(event.type==SDL_CONTROLLERAXISMOTION){
                ++inputDiagnostics.Axes;
                if(event.caxis.which==portable.ControllerId() &&
                   (event.caxis.axis==SDL_CONTROLLER_AXIS_RIGHTX||event.caxis.axis==SDL_CONTROLLER_AXIS_RIGHTY))
                    ++inputDiagnostics.RightStick;
            }
            if(event.type==SDL_FINGERDOWN||event.type==SDL_FINGERMOTION||event.type==SDL_FINGERUP){
                ++inputDiagnostics.Touches;
                if(event.tfinger.touchId==0)++inputDiagnostics.Front;
                else if(event.tfinger.touchId==1)++inputDiagnostics.Rear;
            }
#endif
            if(traceInput && traceLines<256 && (event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP)) {
                ++traceLines;
                std::printf("[desktop-input] SDL %s button=%u window=%u expected=%u xy=%d,%d discard=%d suppress=%d queued=%zu\n",
                    event.type==SDL_MOUSEBUTTONDOWN?"down":"up",unsigned(event.button.button),event.button.windowID,windowId,
                    event.button.x,event.button.y,int(inputs.Discarding()),int(suppressPrimary),inputs.Pending());
            }
            // Device removal/background cancellation must be observed even if
            // the ordered input queue is discarding the rest of this poll.
#if defined(ODH_PORTABLE_DIAGNOSTICS)
            if(event.type==SDL_WINDOWEVENT&&(event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED||event.window.event==SDL_WINDOWEVENT_RESIZED))
                portable.LogGeometry("window-size");
#endif
            if(portable.Handle(event,state,inputs,portable.Focused(),app.WantsTextInput(),
                app.Lot()&&app.Lot()->Holder&&app.Lot()->Holder->Holding))continue;
            if(inputs.Discarding() && event.type!=SDL_QUIT)continue;
            switch (event.type) {
            case SDL_QUIT: inputs.Push(state,InputKind::Action,true);break;
            case SDL_KEYDOWN: {
                if(event.key.windowID!=windowId)break;
                const auto key=event.key.keysym.sym;
                const auto scan=event.key.keysym.scancode;
                if(scan>=0 && scan<SDL_NUM_SCANCODES)keyboard[scan]=true;
                const auto modifiers=static_cast<SDL_Keymod>(event.key.keysym.mod);
                const bool textOwner=app.WantsTextInput();
                const bool gameplay=odh::desktop::PlainGameplayKey(modifiers,textOwner);
                // Enter/Escape still belong to text fields and dialogs. Other
                // gameplay edges never leak out of modified typing shortcuts.
                const bool noChord=!(modifiers&(KMOD_CTRL|KMOD_ALT|KMOD_GUI));
                const bool punctuation=!textOwner && noChord &&
                    (key==SDLK_COMMA || key==SDLK_PERIOD || key==SDLK_BACKQUOTE);
                const bool dialogKey=noChord && (key==SDLK_RETURN || key==SDLK_KP_ENTER || key==SDLK_ESCAPE);
                const auto button=(gameplay || punctuation || dialogKey) ? Button(key) : 0u;
                state.Held|=button;
                keyboardState(modifiers);
                auto input=state;
                if(!event.key.repeat)input.Down=button;
                input.DesktopAction=odh::desktop::OriginalHotkey(key,modifiers,event.key.repeat,textOwner);
                using Type=simapp::AppInput::TextEvent::Type;
                auto edit=[&](Type type,bool& flag) { flag=true;input.TextEvents.push_back({type,{}}); };
                switch(key) {
                case SDLK_BACKSPACE: edit(Type::Backspace,input.TextBackspace);break;
                case SDLK_DELETE: edit(Type::Delete,input.TextDelete);input.DesktopDelete=gameplay&&!event.key.repeat;break;
                case SDLK_HOME: edit(Type::Home,input.TextHome);break;
                case SDLK_END: edit(Type::End,input.TextEnd);break;
                case SDLK_LEFT: edit(Type::Left,input.TextLeft);break;
                case SDLK_RIGHT: edit(Type::Right,input.TextRight);break;
                case SDLK_RETURN: case SDLK_KP_ENTER: edit(Type::Accept,input.TextAccept);break;
                case SDLK_ESCAPE: edit(Type::Cancel,input.TextCancel);break;
                case SDLK_COMMA: input.DesktopRotateLeft=(gameplay||punctuation)&&!event.key.repeat;break;
                case SDLK_PERIOD: input.DesktopRotateRight=(gameplay||punctuation)&&!event.key.repeat;break;
                default: break;
                }
                const bool action=input.Down || input.DesktopAction!=simapp::DesktopCommand::None || input.DesktopDelete || input.DesktopRotateLeft ||
                    input.DesktopRotateRight || input.TextAccept || input.TextCancel;
                const auto kind=action?InputKind::Action:input.TextEvents.empty()?InputKind::State:InputKind::Text;
                inputs.Push(std::move(input),kind);
                break;
            }
            case SDL_TEXTINPUT: {
                if(event.text.windowID!=windowId)break;
                auto input=state;
                input.TextUTF8=event.text.text;
                input.TextEvents.push_back({simapp::AppInput::TextEvent::Type::Insert,event.text.text});
                inputs.Push(std::move(input),InputKind::Text);
                break;
            }
            case SDL_KEYUP:
                if(event.key.windowID!=windowId)break;
                if(event.key.keysym.scancode>=0 && event.key.keysym.scancode<SDL_NUM_SCANCODES)keyboard[event.key.keysym.scancode]=false;
                state.Held&=~Button(event.key.keysym.sym);
                keyboardState(static_cast<SDL_Keymod>(event.key.keysym.mod));
                inputs.Push(state,InputKind::State);
                break;
            case SDL_MOUSEMOTION:
                if(event.motion.windowID!=windowId)break;
                // SDL_RenderSetLogicalSize already scales event coordinates.
                state.TouchX=event.motion.x;state.TouchY=event.motion.y;
                inputs.Push(state,InputKind::State);
                break;
            case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
                if(event.button.windowID!=windowId)break;
                state.TouchX=event.button.x;state.TouchY=event.button.y;
                const bool down=event.type==SDL_MOUSEBUTTONDOWN;
                auto input=state;
                if(event.button.button==SDL_BUTTON_LEFT) {
                    if(suppressPrimary) { if(!down)suppressPrimary=false;break; }
                    state.Touching=down;input.Touching=down;
                    input.TouchDown=down;input.TouchUp=!down;
                    inputs.Push(std::move(input),InputKind::Action);
                } else if(event.button.button==SDL_BUTTON_RIGHT) {
                    if(suppressSecondary) { if(!down)suppressSecondary=false;break; }
                    state.MouseRightHeld=down;input.MouseRightHeld=down;
                    input.MouseRightDown=down;input.MouseRightUp=!down;
                    inputs.Push(std::move(input),InputKind::Action);
                }
                break;
            }
            case SDL_MOUSEWHEEL: {
                if(event.wheel.windowID!=windowId)break;
                auto input=state;
                input.WheelY=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;
                inputs.Push(std::move(input),InputKind::Action);
                break;
            }
            case SDL_WINDOWEVENT:
                if(event.window.windowID==windowId &&
                   (event.window.event==SDL_WINDOWEVENT_FOCUS_LOST || event.window.event==SDL_WINDOWEVENT_LEAVE))
                    resetCapture("focus/pointer left window");
                break;
            default: break;
            }
        }
        inputs.SetFocused(portable.Enabled()?portable.Focused():SDL_GetKeyboardFocus()==window.get());
        if(inputs.TakeOverflow()) {
            std::fprintf(stderr,"desktop: input queue limit reached; stale input discarded and pointer capture cancelled\n");
            resetCapture("queue overflow");
        }
        if(!inputs.Discarding()) {
            // Sampling is only the final state packet, never a replacement for a
            // queued press/release position. Raw SDL_GetMouseState needs conversion.
            if(!portable.Enabled()){
            int mx=0,my=0;const auto mouse=SDL_GetMouseState(&mx,&my);
            if(!(mouse&SDL_BUTTON_LMASK))suppressPrimary=false;
            if(!(mouse&SDL_BUTTON_RMASK))suppressSecondary=false;
            if(SDL_GetMouseFocus()==window.get()) {
                float lx=0,ly=0;SDL_RenderWindowToLogical(renderer.get(),mx,my,&lx,&ly);
                state.TouchX=int(std::floor(lx));state.TouchY=int(std::floor(ly));
                if((state.Touching && !(mouse&SDL_BUTTON_LMASK)) ||
                   (state.MouseRightHeld && !(mouse&SDL_BUTTON_RMASK))) {
                    // A missing release (capture/focus change) cancels, never clicks.
                    resetCapture("sampled release before event");
                }
            } else {
                if(state.Touching || state.MouseRightHeld)resetCapture("pointer outside window");
                state.TouchX=state.TouchY=-1;
            }
            }
            if(SDL_GetKeyboardFocus()==window.get()) {
                const auto keys=SDL_GetKeyboardState(nullptr);
                for(size_t i=0;i<keyboard.size();++i)keyboard[i]=keys[i]!=0;
                keyboardState(SDL_GetModState());
            }
            if(portable.Enabled())portable.Frame(begin-last,state,inputs,portable.Focused(),app.WantsTextInput());
            else inputs.Push(state,InputKind::State);
        }
#if defined(ODH_PLATFORM_VITA) || defined(__ANDROID__)
        inputDiagnostics.QueuePeak=std::max(inputDiagnostics.QueuePeak,inputs.Pending());
#endif
        auto packet=inputs.Next();
#if defined(ODH_PLATFORM_VITA) || defined(__ANDROID__)
        ++inputDiagnostics.Delivered;
        inputDiagnostics.TouchX=packet.Input.TouchX;inputDiagnostics.TouchY=packet.Input.TouchY;
        inputDiagnostics.Queued=inputs.Pending();
#endif
        if(traceInput && traceLines<256 && (packet.Input.TouchDown || packet.Input.TouchUp || !packet.Input.TextEvents.empty())) {
            ++traceLines;
            std::printf("[desktop-input] deliver frame=%d down=%d up=%d held=%d xy=%d,%d text=%zu pending=%zu\n",
                frame,int(packet.Input.TouchDown),int(packet.Input.TouchUp),int(packet.Input.Touching),packet.Input.TouchX,packet.Input.TouchY,
                packet.Input.TextEvents.size(),inputs.Pending());
        }
        if(packet.Quit)app.RequestDesktopQuit();
        const double elapsed = std::clamp(begin - last, 0.0, 250.0);
        last = begin;
#if defined(ODH_PLATFORM_VITA) || defined(ODH_PLATFORM_SWITCH)
        // On a slow frame, show stick motion on the last completed scene before
        // simulation or drawing can block again. Clicks keep their ordered position.
        if(havePresented && elapsed>=32 && app.Stats.Total>=32 && portable.Focused() && softwareCursor &&
           packet.Type==InputKind::State && !packet.Input.Touching && !packet.Input.MouseRightHeld &&
           (cursorInput.TouchX!=packet.Input.TouchX || cursorInput.TouchY!=packet.Input.TouchY)){
            const auto& previous=app.DesktopCanvas();
            const auto viewport=portable.Canvas();
            using Cursor=odh::desktop::SdlPortableInput;
#if defined(ODH_PORTABLE_DIAGNOSTICS)
            const double uploadStarted=NowMs();
#endif
            if(!Cursor::UploadCursor(texture.get(),previous.Px,previous.Width,previous.Height,cursorInput,softwareCursor,true) ||
               !Cursor::UploadCursor(texture.get(),previous.Px,previous.Width,previous.Height,packet.Input,softwareCursor,false,&canvasUpload.paintedCursor) ||
               SDL_RenderClear(renderer.get())!=0 || SDL_RenderCopy(renderer.get(),texture.get(),nullptr,&viewport.Box)!=0){
                std::fprintf(stderr,"portable pointer presentation failed: %s\n",SDL_GetError());
                renderFailed=true;
                break;
            }
#if defined(ODH_PORTABLE_DIAGNOSTICS)
            const double uploadDone=NowMs();
#endif
            SDL_RenderPresent(renderer.get());
#if defined(ODH_PORTABLE_DIAGNOSTICS)
            performance.Presentation(uploadDone-uploadStarted,NowMs()-uploadDone);
#endif
        }
#endif
        cursorInput=packet.Input; // Cursor and engine consume the same ordered point.
        app.Frame(packet.Input, elapsed);
        // Frame has rendered the canvas. Export before presentation so an SDL
        // failure still leaves the last player command in the existing log.
        if (!app.Log.empty() && !std::ferror(stdout)) {
            const auto written = std::fwrite(app.Log.data(), 1, app.Log.size(), stdout);
            if (std::fflush(stdout) == 0) app.Log.erase(0, written);
            if (std::ferror(stdout)) std::fputs("Application log write failed; unexported messages retained.\n", stderr);
        }
        const bool cursorActive=portable.Enabled()?portable.Focused():SDL_GetMouseFocus()==window.get() && SDL_GetKeyboardFocus()==window.get();
        const auto cursorPath=!cursorActive?std::string{}:
            (!portable.Enabled() && app.CurrentMode()==simapp::App::Mode::Loading)?std::string("Shared/cursors/HOURGLASS.cur"):
            app.Lot()?app.Lot()->DesktopCameraCursorPath():
            app.Front()?app.Front()->DesktopCursorPath():std::string{};
        if(portable.Enabled()){
            const auto requested=cursorActive?(cursorPath.empty()?std::string("Shared/cursors/Arrow.cur"):cursorPath):std::string{};
            if(requested!=softwareCursorKey){
                softwareCursorKey=requested;
                try {
                    softwareCursor=requested.empty()?nullptr:simapp::UIGraphics::Get().NativeCursor(requested);
                    if(cursorActive&&!softwareCursor)softwareCursor=simapp::UIGraphics::Get().NativeCursor("Shared/cursors/Arrow.cur");
                }catch(const std::exception& error){softwareCursor=nullptr;std::fprintf(stderr,"portable cursor: %s\n",error.what());}
            }
        }else originalCursor.Select(cursorPath);
        const bool wantsTextInput = !softwareKeyboard || app.WantsTextInput();
        if (wantsTextInput != textInputActive) {
            if (wantsTextInput) SDL_StartTextInput();
            else SDL_StopTextInput();
            textInputActive = wantsTextInput;
        }
        present();
#if defined(ODH_PORTABLE_DIAGNOSTICS)
        const double completedWork = NowMs() - begin;
        performance.Frame(completedWork, app.Stats.Total, app.Stats.Vm, app.Stats.World, app.Stats.Ui,
            static_cast<uint64_t>(std::max(0, app.Stats.Updates)));
#if !defined(__ANDROID__)
        if (completedWork >= 1000) {
            char memory[256]; ConsoleMemoryText(memory, sizeof(memory));
            std::printf("[%s] slow_completed_work_ms=%.2f app_ms=%.2f vm_ms=%.2f world_ms=%.2f ui_ms=%.2f %s\n",
                ConsolePerfTag, completedWork, app.Stats.Total, app.Stats.Vm, app.Stats.World, app.Stats.Ui, memory);
            std::fflush(stdout);
        }
#endif
        performanceReport("periodic");
#endif
        // Do not report from app.Present: blocking load/save callbacks also use
        // it, before the ordinary frame and its input layer have completed.
        const auto& drawn = app.DrawnFrame();
        if (!renderFailed && (!havePresented || !(drawn == lastPresented))) {
            const char* scene = "other";
            switch (drawn.scene) {
                case simapp::FramePresentation::Scene::Loading: scene = "loading"; break;
                case simapp::FramePresentation::Scene::Map: scene = "map"; break;
                case simapp::FramePresentation::Scene::Lot: scene = "lot"; break;
                default: break;
            }
            std::printf("desktop: presented frame=%d scene=%s house=%d area=%d lot_mode=%d modal=%d family_picker=%d destination_picker=%d cheats=%d pie=%d\n",
                frame, scene, drawn.house, drawn.area, drawn.lot_mode, int(drawn.modal),
                int(drawn.family_picker), int(drawn.destination_picker), int(drawn.cheats), int(drawn.pie));
            std::fflush(stdout);
            lastPresented = drawn;
            havePresented = true;
        }
        ++frame;
        if (options.frames && frame >= options.frames) {
            std::printf("diagnostic: frame limit %d reached; exiting without an automatic save\n", frame);
            break;
        }
        const double remaining = 1000.0 / 60.0 - (NowMs() - begin);
        if (remaining >= 1.0) SDL_Delay(static_cast<Uint32>(remaining));
    }
#if defined(ODH_PORTABLE_DIAGNOSTICS)
    performanceReport("exit", true);
#endif
    SDL_StopTextInput();
    if (!options.screenshot.empty()) {
        const auto& canvas = app.DesktopCanvas();
        // Explicit byte conversion also works on big-endian desktop targets.
        std::vector<uint8_t> bytes(canvas.Px.size() * 4);
        for (size_t i = 0; i < canvas.Px.size(); ++i)
            for (int channel = 0; channel < 4; ++channel)
                bytes[4 * i + channel] = static_cast<uint8_t>(canvas.Px[i] >> (channel * 8));
        if (!st::writePNG(options.screenshot.string(), canvas.Width, canvas.Height, bytes.data()))
            throw std::runtime_error("Cannot write screenshot: " + options.screenshot.string());
        std::printf("diagnostic: screenshot %s\n", options.screenshot.string().c_str());
    }
    if (const auto* session = app.Session())
        std::printf("desktop: session VM updates=%lld, object resets=%d\n",
            static_cast<long long>(session->Updates), session->Resets);
    std::printf("desktop: exit after %d frames, mode=%d\n", frame, static_cast<int>(app.CurrentMode()));
    return renderFailed || app.CurrentMode() == simapp::App::Mode::Error ? 1 : 0;
}
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") { Usage(); return 0; }
    try { return Run(Parse(argc, argv)); }
    catch (const std::exception& error) {
        std::fprintf(stderr, "OpenDollhouse: %s\n", error.what());
#if defined(ODH_PORTABLE_DIAGNOSTICS)
        ConsoleStage("exception");
#endif
#if !defined(ODH_PLATFORM_VITA) && !defined(ODH_PLATFORM_SWITCH)
        // A GUI launch has no terminal on Windows. SDL supports this before Init.
        if (argc == 1) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenDollhouse", error.what(), nullptr);
#endif
        return 1;
    }
}
