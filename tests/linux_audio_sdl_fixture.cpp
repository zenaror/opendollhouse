// Test-only ABI double: loaded by one isolated CTest via LD_LIBRARY_PATH.
// No real SDL, hardware, mixer, simulation or assets are used here.
#include "linux_audio_sdl_fixture.hpp"
#include <array>
#include <cstdint>
#include <cstring>
namespace {
struct AudioSpec {int freq;std::uint16_t format;std::uint8_t channels,silence;
    std::uint16_t samples,padding;std::uint32_t size;
    void (*callback)(void*,std::uint8_t*,int);void* userdata;};
struct Version {std::uint8_t major,minor,patch;};
static_assert(offsetof(AudioSpec,callback)==16&&sizeof(Version)==3,"isolated SDL2 ABI fixture");
std::array<int,audio_fixture::count> counters{};
struct Device {bool live=false;std::uint32_t queued=0;};
std::array<Device,64> devices{};
int failure=audio_fixture::none;
void (*quit_hook)(void*)=nullptr;void* quit_userdata=nullptr;
}
extern "C" {
int SDLTestReset(){
    if(counters[audio_fixture::live_references]||counters[audio_fixture::live_devices])return 0;
    counters.fill(0);failure=audio_fixture::none;quit_hook=nullptr;quit_userdata=nullptr;return 1;
}
void SDLTestFailure(int value){failure=value;}
void SDLTestQuitHook(void (*hook)(void*),void* userdata){quit_hook=hook;quit_userdata=userdata;}
void SDLTestCounters(int* out,std::size_t size){if(out&&size==counters.size())std::memcpy(out,counters.data(),sizeof(int)*counters.size());}
int SDL_InitSubSystem(std::uint32_t flags){
    ++counters[audio_fixture::init_calls];
    if(flags!=0x10||failure==audio_fixture::init_error)return -1;
    ++counters[audio_fixture::init_success];++counters[audio_fixture::live_references];return 0;
}
void SDL_QuitSubSystem(std::uint32_t flags){
    ++counters[audio_fixture::quit_subsystem];
    if(flags!=0x10||counters[audio_fixture::live_references]<=0){++counters[audio_fixture::bad_device];return;}
    --counters[audio_fixture::live_references];
}
void SDL_Quit(){
    if(quit_hook)quit_hook(quit_userdata);
    ++counters[audio_fixture::global_quit];
    if(counters[audio_fixture::live_devices]||counters[audio_fixture::live_references])++counters[audio_fixture::premature_quit];
    counters[audio_fixture::live_devices]=0;counters[audio_fixture::live_references]=0;
    for(auto& device:devices)device={};
}
const char* SDL_GetError(){return "injected SDL lifecycle failure";}
const char* SDL_GetCurrentAudioDriver(){return "odh-lifetime-fixture";}
void SDL_GetVersion(Version* value){++counters[audio_fixture::version_calls];*value={2,0,static_cast<std::uint8_t>(failure==audio_fixture::old_version?3:4)};}
std::uint32_t SDL_OpenAudioDevice(const char*,int,const AudioSpec* desired,AudioSpec* obtained,int){
    ++counters[audio_fixture::open_calls];if(failure==audio_fixture::open_error)return 0;
    for(std::size_t id=1;id<devices.size();++id)if(!devices[id].live){
        devices[id]={true,0};++counters[audio_fixture::live_devices];*obtained=*desired;
        if(failure==audio_fixture::changed_format)++obtained->channels;
        return static_cast<std::uint32_t>(id);
    }return 0;
}
void SDL_CloseAudioDevice(std::uint32_t id){
    ++counters[audio_fixture::close_calls];
    if(id>=devices.size()||!devices[id].live){++counters[audio_fixture::bad_device];return;}
    devices[id]={};--counters[audio_fixture::live_devices];
}
int SDL_QueueAudio(std::uint32_t id,const void*,std::uint32_t bytes){
    if(id>=devices.size()||!devices[id].live){++counters[audio_fixture::bad_device];return -1;}
    devices[id].queued+=bytes;return 0;
}
std::uint32_t SDL_GetQueuedAudioSize(std::uint32_t id){
    if(id>=devices.size()||!devices[id].live){++counters[audio_fixture::bad_device];return 0;}
    return devices[id].queued;
}
void SDL_PauseAudioDevice(std::uint32_t,int){}
void SDL_ClearQueuedAudio(std::uint32_t id){if(id<devices.size()&&devices[id].live)devices[id].queued=0;else ++counters[audio_fixture::bad_device];}
}
