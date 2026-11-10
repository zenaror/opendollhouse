#include "linux_audio.hpp"
#include "linux_audio_sdl_fixture.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
using namespace audio_fixture;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,"expected lifecycle refusal");}
struct Fixture {
    void* library=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_LOCAL);
    int (*reset)()=nullptr;void (*fail)(int)=nullptr;void (*read)(int*,std::size_t)=nullptr;
    void (*quit_hook)(void (*)(void*),void*)=nullptr;
    int (*init)(std::uint32_t)=nullptr;void (*quit_sub)(std::uint32_t)=nullptr;void (*quit)()=nullptr;
    template<class T>void bind(T& target,const char* name){const auto address=dlsym(library,name);require(address,"isolated lifecycle SDL fixture missing");static_assert(sizeof(T)==sizeof(address));std::memcpy(&target,&address,sizeof(target));}
    Fixture(){require(library,"load isolated lifecycle fixture");bind(reset,"SDLTestReset");bind(fail,"SDLTestFailure");bind(read,"SDLTestCounters");bind(init,"SDL_InitSubSystem");bind(quit_sub,"SDL_QuitSubSystem");bind(quit,"SDL_Quit");bind(quit_hook,"SDLTestQuitHook");}
    ~Fixture(){if(library)dlclose(library);}
    auto values()const{std::array<int,count> result{};read(result.data(),result.size());return result;}
    void fresh(){require(reset()==1,"previous lifecycle left owned refs/devices");}
    void balanced(int globals)const{const auto v=values();require(v[global_quit]==globals&&v[live_references]==0&&v[live_devices]==0&&v[bad_device]==0&&v[premature_quit]==0,"SDL shutdown did not balance exact leases before global cleanup");require(v[init_success]==v[quit_subsystem],"successful Init requires one QuitSubSystem even on device failure");}
};
using Runtime=odh_host::LinuxAudioRuntime;using Audio=odh_host::LinuxAudio;
struct CleanupProbe {
    std::thread::id owner=std::this_thread::get_id();
    bool wait=false;
    std::atomic<bool> entered{false},attempted{false},refused{false},wrong_thread{false},timed_out{false};
    static void on_quit(void* data)noexcept{
        auto& probe=*static_cast<CleanupProbe*>(data);
        if(std::this_thread::get_id()!=probe.owner)probe.wrong_thread.store(true);
        probe.entered.store(true);
        if(probe.wait){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(!probe.attempted.load()){
                if(std::chrono::steady_clock::now()>=deadline){probe.timed_out.store(true);break;}
                std::this_thread::yield();
            }
        }
    }
};
}
int main(){try{
    Fixture f;
    f.fresh();{Runtime lazy(Runtime::Ownership::application);
        rejects([&]{Audio invalid(lazy,{7999,1});});
        require(f.values()[version_calls]==0&&f.values()[init_calls]==0,"invalid format must not load or initialize SDL");
    }f.balanced(0);
    rejects([]{Runtime invalid(static_cast<Runtime::Ownership>(99));});
    f.fresh();{
        Runtime root(Runtime::Ownership::application);
        for(unsigned session=0;session<4;++session){Runtime nested(Runtime::Ownership::application);Audio a(nested,{44100,2});
            require(a.driver()=="odh-lifetime-fixture","test may not open a physical backend");std::int16_t pcm[4]{};
            require(a.queue(pcm,2)==2,"queue before session teardown");}
        auto v=f.values();require(v[init_success]==4&&v[close_calls]==4&&v[global_quit]==0&&v[version_calls]==1,"four sessions must share one loaded runtime without global shutdown");
        rejects([]{Runtime wrong(Runtime::Ownership::external);});
        bool refused=false;std::thread foreign([&]{try{Runtime wrong(Runtime::Ownership::application);}catch(const std::exception&){refused=true;}});foreign.join();require(refused,"cross-thread runtime adoption must reject before SDL");
    }f.balanced(1);
    // Attempt acquisition while the final owner is inside global cleanup.
    // Metadata must reject without retaining State or moving cleanup to foreign.
    f.fresh();{
        CleanupProbe probe;probe.wait=true;
        auto root=std::make_unique<Runtime>(Runtime::Ownership::application);
        {Audio a(*root,{44100,2});}
        f.quit_hook(&CleanupProbe::on_quit,&probe);
        std::thread contender([&]{
            while(!probe.entered.load())std::this_thread::yield();
            try{Runtime other(Runtime::Ownership::application);}catch(const std::exception&){probe.refused.store(true);}
            probe.attempted.store(true);
        });
        root.reset();contender.join();f.quit_hook(nullptr,nullptr);
        require(probe.refused.load()&&!probe.timed_out.load()&&!probe.wrong_thread.load(),
            "finalizing host must refuse foreign adoption without deadlock or foreign SDL cleanup");
    }f.balanced(1);
    // Release/adoption race: after complete finalization a new lazy runtime is
    // valid on the contender, so either refusal or fresh acquisition is legal.
    f.fresh();{
        CleanupProbe probe;f.quit_hook(&CleanupProbe::on_quit,&probe);
        for(unsigned attempt=0;attempt<32;++attempt){
            auto root=std::make_unique<Runtime>(Runtime::Ownership::application);
            {Audio a(*root,{44100,2});}
            std::atomic<bool> go{false};
            std::thread contender([&]{while(!go.load())std::this_thread::yield();
                try{Runtime other(Runtime::Ownership::application);}catch(const std::exception&){} });
            go.store(true);root.reset();contender.join();
        }
        f.quit_hook(nullptr,nullptr);
        require(!probe.wrong_thread.load()&&f.values()[init_success]==32&&f.values()[close_calls]==32,
            "refused contender must never acquire the old SDL cleanup lifetime");
    }f.balanced(32);
    f.fresh();{
        Runtime root(Runtime::Ownership::application);auto first=std::make_unique<Audio>(root,odh_host::AudioFormat{44100,2});
        Audio second(root,{44100,2});first.reset();require(f.values()[live_references]==1&&f.values()[global_quit]==0,"one device must survive another closing");
        std::int16_t samples[2]{};require(second.queue(samples,1)==1,"remaining device remains usable");
    }f.balanced(1);
    f.fresh();{
        std::unique_ptr<Audio> retained;
        {Runtime root(Runtime::Ownership::application);retained=std::make_unique<Audio>(root,odh_host::AudioFormat{8000,1});}
        require(f.values()[global_quit]==0&&f.values()[live_devices]==1,"device lease must outlive early host scope release");
        retained.reset();
    }f.balanced(1);
    for(const int failure:{init_error,open_error,changed_format}){
        f.fresh();{Runtime root(Runtime::Ownership::application);f.fail(failure);rejects([&]{Audio bad(root,{44100,2});});
            require(f.values()[global_quit]==0&&f.values()[live_devices]==0&&f.values()[live_references]==0,"construction failure must balance lease without global shutdown");
            f.fail(none);{Audio retry(root,{44100,2});}require(f.values()[global_quit]==0,"successful later session shares original host");
        }f.balanced(1);
    }
    f.fresh();{
        Runtime root(Runtime::Ownership::application);f.fail(old_version);rejects([&]{Audio bad(root,{44100,2});});
        require(f.values()[global_quit]==0,"partial API failure cannot globally shutdown while owner remains");
        f.fail(none);rejects([&]{Audio bad(root,{44100,2});});
        require(f.values()[version_calls]==1&&f.values()[init_calls]==0,"poisoned API bind must not partially retry");
    }f.balanced(1);
    f.fresh();require(f.init(0x10)==0,"external owner begins");{
        Runtime borrowed(Runtime::Ownership::external);{Audio a(borrowed,{44100,2});}
        rejects([]{Runtime wrong(Runtime::Ownership::application);});
        require(f.values()[live_references]==1&&f.values()[global_quit]==0,"borrowed device cannot shutdown its external owner");
    }
    require(f.values()[live_references]==1&&f.values()[global_quit]==0,"borrowed runtime never calls SDL_Quit");
    f.quit_sub(0x10);f.quit();f.balanced(1);
    f.fresh();{Runtime fresh(Runtime::Ownership::application);Audio a(fresh,{192000,2});}f.balanced(1);
    std::cout<<"SDL runtime ownership, session leases and failure teardown passed (synthetic ABI only)\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
