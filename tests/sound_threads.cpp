#include "odh/sound_threads.hpp"
#include <iostream>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
struct Counts {unsigned factories=0,starts=0,queries=0,stops=0;bool active=true,fail=false;};
struct Lifetime:odh::SoundThreadLifetime {
    Counts& counts;bool stopped=false;
    explicit Lifetime(Counts& value):counts(value){}
    void stop() noexcept override {if(!stopped){stopped=true;++counts.stops;}}
    ~Lifetime() override {stop();}
};
}
int main(){try{
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,66,0,0}),"source created");
    odh::SoundRequest request{355,0,0,0,1,"fixture",false,false,false,{}};
    request.source_variables=odh::sound_source_variables(*world.find(1));
    odh::HitFile hit;hit.major_version=1;hit.bytes.resize(16);
    hit.bytes.insert(hit.bytes.end(),{5,50,100,0,0,0,2,5,11,12});
    odh::Hot hot;hot.patches[100]={"fixture","fixture.xa",false,false};
    Counts counts;
    auto factory=[&](const odh::SoundRequest& submitted,const odh::SoundSourceBinding& binding){
        ++counts.factories;
        require(submitted.name=="fixture" && binding.valid(),"factory receives metadata and bound owner");
        auto lifetime=std::make_unique<Lifetime>(counts);
        odh::HitNoteBackend backend;
        backend.start=[&](auto)->std::optional<std::uint64_t>{++counts.starts;return 1;};
        backend.status=[&](auto){++counts.queries;return counts.fail?odh::HitNoteStatus::failed:
            counts.active?odh::HitNoteStatus::active:odh::HitNoteStatus::complete;};
        odh::HitSourceFieldReader reader=[binding](auto source,auto field){return binding.read(source,field);};
        std::array<std::int32_t,29> vars{};
        auto thread=std::make_unique<odh::HitThread>(hit,hot,16,vars,odh::HitChooser{},backend,
            odh::HitThreadLimits{},nullptr,odh::HitJumpOperand::FreeSoHeuristic,reader,odh::HitArguments{{submitted.source,0,0}});
        return std::make_unique<odh::SoundThreadInstance>(std::move(thread),std::move(lifetime));
    };
    {
        odh::SoundThreadCollection collection(world,factory,{2,256});
        require(collection.submit(request) && collection.size()==1 && counts.starts==0,"submission doesn't tick HIT");
        require(collection.submit(request) && !collection.submit(request) && counts.factories==2,"cap enforced before factory");
        auto report=collection.tick();
        require(report.active==2 && !report.completed && counts.starts==2,"one tick per thread, no screen filter");
        const auto queries=counts.queries;
        require(world.remove(1) && world.create(1,odh::EntityKind::avatar,{0,66,0,0}),"source ID reused");
        report=collection.tick();
        require(report.owner_invalid==2 && !report.completed && !report.faulted && !report.active,
                "stale owner cancels pending WaitSamp, distinct from completion");
        require(counts.queries==queries && counts.stops==2,"stop before querying/reexecuting stale thread");
        require(!collection.submit(request) && counts.factories==2,"stale submission rejected before factory");
        request.source_variables=odh::sound_source_variables(*world.find(1));
        auto loop=request;loop.loop=true;
        require(!collection.submit(loop),"loop rejected by explicit collection profile");
        require(collection.submit(request),"new incarnation accepted from new receipt");
        collection.tick();counts.active=false;
        report=collection.tick();require(report.active==1 && !report.completed,"WaitSamp completion yields");
        report=collection.tick();require(report.active==1 && !report.completed,"End yields");
        report=collection.tick();require(report.completed==1 && !report.active && counts.stops==3,"completion releases backend");
        counts.active=true;counts.fail=true;
        require(collection.submit(request),"failing thread submitted");
        report=collection.tick();
        require(report.faulted==1 && !report.completed && !report.active && counts.stops==4,"backend failure distinct and cleaned");
        counts.fail=false;
        require(collection.submit(request),"destructor fixture submitted");
    }
    require(counts.stops==5,"collection destruction releases remaining backend even before NoteOn");
    // Source disappearing during factory cannot publish an acquired instance.
    {
        odh::SoundThreadCollection collection(world,[&](const auto& value,const auto& binding){
            auto instance=factory(value,binding);world.remove(1);return instance;
        });
        const auto stopped=counts.stops;
        require(!collection.submit(request) && !collection.size() && counts.stops==stopped+1,
                "factory owner race discards instance with cleanup");
    }
    require(world.create(1,odh::EntityKind::avatar,{0,66,0,0}),"source restored");
    request.source_variables=odh::sound_source_variables(*world.find(1));
    {
        odh::SoundThreadCollection collection(world,factory);
        auto primitive=odh::sound_primitive(world,[](const auto&,auto)->std::optional<odh::SoundEvent>{return odh::SoundEvent{"fixture",0};},
            [&](const auto& value){return collection.submit(value);});
        const auto key=odh::routine_key(4096);
        const odh::Instruction sound{23,1,255,{99,1,0,0,0,0,0,0}};
        odh::RoutineSet routines{{key,{0x8002,{sound,{2,254,255,{0,0,42,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
        const auto starts=counts.starts;
        odh::Vm vm(routines,{}, {},primitive);vm.start(key,{1,1,1});
        require(vm.run(1)==odh::VmStatus::budget_exhausted && !vm.pending() && collection.size()==1 && counts.starts==starts,
                "PlaySound sink submits without blocking or hidden sound tick");
        collection.tick();
        require(vm.run(1)==odh::VmStatus::returned_true && vm.temps()[0]==42,"BHAV continues separately from sound wait");
    }
    {
        odh::SoundThreadCollection rejecting(world,[](const auto&,const auto&)->std::unique_ptr<odh::SoundThreadInstance>{return {};});
        require(!rejecting.submit(request) && !rejecting.size(),"factory rejection not published");
        const auto stops=counts.stops;
        odh::SoundThreadCollection failing(world,[&](const auto&,const auto&)->std::unique_ptr<odh::SoundThreadInstance>{
            auto owner=std::make_unique<Lifetime>(counts);
            throw odh::FormatError("factory failure");
        });
        bool thrown=false;try{failing.submit(request);}catch(const odh::FormatError&){thrown=true;}
        require(thrown && !failing.size() && counts.stops==stops+1,"factory exception preserves collection and RAII cleanup");
    }
    {
        odh::SoundThreadCollection* self=nullptr;
        odh::SoundThreadCollection recursive(world,[&](const auto&,const auto&)->std::unique_ptr<odh::SoundThreadInstance>{self->tick();return {};});
        self=&recursive;
        bool thrown=false;try{recursive.submit(request);}catch(const odh::FormatError&){thrown=true;}
        require(thrown && !recursive.size(),"callback cannot reenter collection operations");
    }
    {
        odh::SoundThreadCollection removing(world,[&](const auto&,const auto&){
            auto owner=std::make_unique<Lifetime>(counts);
            odh::HitNoteBackend backend;
            backend.start=[](auto)->std::optional<std::uint64_t>{return 1;};
            backend.status=[&](auto){world.remove(1);return odh::HitNoteStatus::complete;};
            std::array<std::int32_t,29> vars{};
            auto thread=std::make_unique<odh::HitThread>(hit,hot,16,vars,odh::HitChooser{},backend);
            return std::make_unique<odh::SoundThreadInstance>(std::move(thread),std::move(owner));
        });
        require(removing.submit(request),"callback removal fixture submitted");
        const auto stops=counts.stops;const auto report=removing.tick();
        require(report.owner_invalid==1 && !report.completed && !report.faulted && counts.stops==stops+1,
                "owner removed during backend callback cancelled before next tick");
    }
    std::cout<<checks<<" sound thread collection checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
