#include "odh/sound_factory.hpp"
#include <iostream>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const odh::FormatError&){failed=true;}require(failed,"expected explicit factory/resource failure");}
struct Counts {unsigned starts=0,stops=0,choices=0;bool active=true;std::uint32_t patch=0;};
struct Owner:odh::SoundThreadLifetime {
    Counts& c;bool stopped=false;
    explicit Owner(Counts& counts):c(counts){}
    void stop() noexcept override{if(!stopped){stopped=true;++c.stops;}}
    ~Owner() override{stop();}
};
struct Fixture {
    odh::World world;
    odh::HitFile hit;
    odh::Hot hot;
    odh::SoundRequest request{355,0,0,0,1,"FIXTURE",false,false,false,{}};
    Counts c;
    Fixture(){
        world.create(1,odh::EntityKind::avatar,{0,66,0,0});world.set_person_data(1,7,300);
        request.source_variables=odh::sound_source_variables(*world.find(1));
        hit.major_version=1;hit.bytes.resize(16);hit.bytes.insert(hit.bytes.end(),{2,5,11,12});
        hot.events["fixture"]={1,7,{}};
        odh::HotTrack t;t.subroutine_offset=16;t.hitlist_id=0;t.patch_id=100;hot.tracks[7]=t;
        hot.patches[100]={"fixture","fixture.xa",false,false};
        hot.patches[200]={"other","other.xa",false,false};
    }
    odh::HitNoteBackend backend(){
        odh::HitNoteBackend b;
        b.start=[this](std::uint32_t patch)->std::optional<std::uint64_t>{++c.starts;c.patch=patch;return 1;};
        b.status=[this](auto){return c.active?odh::HitNoteStatus::active:odh::HitNoteStatus::complete;};
        return b;
    }
    auto binding(){return *odh::SoundSourceBinding::bind(world,request);}
    auto make(odh::HitChooser chooser={},odh::HitThreadLimits limits={}){
        return odh::make_hit_sound(hit,hot,request,binding(),std::move(chooser),backend(),
            std::make_unique<Owner>(c),limits,odh::HitJumpOperand::FreeSoHeuristic,odh::HitArguments{{1,20,0}});
    }
};
template<class F>void invalid(F mutate){Fixture f;mutate(f);rejects([&]{odh::resolve_hit_event(f.hit,f.hot,f.request);});}
}
int main(){try{
    {
        Fixture f;const auto resolved=odh::resolve_hit_event(f.hit,f.hot,f.request);
        require(resolved.track_id==7 && resolved.start_pc==16 && resolved.initial_track==&f.hot.tracks.at(7),"case folded lookup returns borrowed track");
        require(!f.c.starts && !f.c.choices && f.request.name=="FIXTURE","resolution pure, request unchanged");
        f.hit.minor_version=8;require(odh::resolve_hit_event(f.hit,f.hot,f.request).start_pc==16,"HIT1.8 accepted");
        rejects([&]{odh::resolve_hit_event(f.hit,f.hot,f.request,{0});});
        rejects([&]{odh::resolve_hit_event(f.hit,f.hot,f.request,{65537});});
        rejects([&]{odh::resolve_hit_event(f.hit,f.hot,f.request,{6});});
        require(odh::resolve_hit_event(f.hit,f.hot,f.request,{7}).start_pc==16,"name exact budget accepted");
    }
    invalid([](auto& f){f.request.name="";});
    invalid([](auto& f){f.request.name=std::string("fi\0xture",8);});
    invalid([](auto& f){f.request.name="missing";});
    invalid([](auto& f){f.request.loop=true;});
    invalid([](auto& f){f.hot.events["fixture"].event_type.reset();});
    invalid([](auto& f){f.hot.events["fixture"].event_type=2;});
    invalid([](auto& f){f.hot.events["fixture"].track_id.reset();});
    invalid([](auto& f){f.hot.events["fixture"].unsupported_reason="fixture";});
    invalid([](auto& f){f.hot.tracks.clear();});
    invalid([](auto& f){f.hot.tracks[7].unsupported_reason="fixture";});
    invalid([](auto& f){f.hot.tracks[7].patch_id.reset();});
    invalid([](auto& f){f.hot.tracks[7].hitlist_id.reset();});
    invalid([](auto& f){f.hot.tracks[7].subroutine_offset.reset();});
    invalid([](auto& f){f.hot.tracks[7].subroutine_offset=15;});
    invalid([](auto& f){f.hot.tracks[7].subroutine_offset=20;});
    invalid([](auto& f){f.hit.entry_table_offset=16;});
    invalid([](auto& f){f.hit.entry_table_offset=21;});
    invalid([](auto& f){f.hit.entry_table_offset=17;f.hot.tracks[7].subroutine_offset=17;});
    invalid([](auto& f){f.hit.major_version=2;});
    invalid([](auto& f){f.hit.minor_version=9;});
    invalid([](auto& f){f.hot.patches.erase(100);});
    invalid([](auto& f){f.hot.patches[100].looped=true;});
    invalid([](auto& f){f.hot.tracks[7].patch_id=0;});
    invalid([](auto& f){f.hot.tracks[7].hitlist_id=9;});
    invalid([](auto& f){f.hot.tracks[7].hitlist_id=9;f.hot.hitlists[9]={};});
    invalid([](auto& f){f.hot.tracks[7].hitlist_id=9;f.hot.hitlists[9]={{100},"","unsupported"};});
    {
        Fixture f;f.hot.tracks[7].patch_id=0;f.hot.tracks[7].hitlist_id=9;f.hot.hitlists[9]={{100,200},{},{}};
        require(odh::resolve_hit_event(f.hit,f.hot,f.request).track_id==7 && !f.c.choices,"zero patch with list is deferred selection");
        auto instance=f.make([&](auto id,const auto& ids){require(id==9 && ids.size()==2,"explicit initial chooser inputs");++f.c.choices;return std::size_t{1};});
        require(f.c.choices==1 && !f.c.starts,"initial SetTrack chooser once, no playback");
        require(instance->tick()==odh::HitThreadTick::yielded && f.c.patch==200 && f.c.starts==1,"selected patch starts on later tick");
        instance.reset();require(f.c.stops==1,"success backend released once");
        rejects([&]{f.make();});require(f.c.stops==2,"missing chooser cleanup");
        rejects([&]{f.make([](auto,const auto&)->std::size_t{throw odh::FormatError("chooser failed");});});
        require(f.c.stops==3,"throwing chooser cleanup");
        rejects([&]{f.make([](auto,const auto&){return std::size_t{2};});});
        require(f.c.stops==4,"invalid index cleanup");
        odh::HitThreadLimits limits;limits.execution.choice_ids=1;
        rejects([&]{f.make([](auto,const auto&){return std::size_t{0};},limits);});
        require(f.c.stops==5,"choice budget cleanup");
    }
    {
        Fixture f;const auto binding=f.binding();
        f.request.name="missing";rejects([&]{f.make();});require(f.c.stops==1,"resolution error stops acquired backend");
        f.request.name="fixture";
        auto bad=f.request;bad.source=2;
        rejects([&]{odh::make_hit_sound(f.hit,f.hot,bad,binding,{},f.backend(),std::make_unique<Owner>(f.c),{},odh::HitJumpOperand::FreeSoHeuristic,{});});
        require(f.c.stops==2,"wrong source binding cleanup");
        bad=f.request;bad.source_variables->generation++;
        rejects([&]{odh::make_hit_sound(f.hit,f.hot,bad,binding,{},f.backend(),std::make_unique<Owner>(f.c),{},odh::HitJumpOperand::FreeSoHeuristic,{});});
        require(f.c.stops==3,"wrong receipt generation cleanup");
        rejects([&]{odh::make_hit_sound(f.hit,f.hot,f.request,binding,{},f.backend(),{}, {},odh::HitJumpOperand::FreeSoHeuristic,{});});
        f.world.remove(1);f.world.create(1,odh::EntityKind::avatar,{0,66,0,0});
        rejects([&]{odh::make_hit_sound(f.hit,f.hot,f.request,binding,{},f.backend(),std::make_unique<Owner>(f.c),{},odh::HitJumpOperand::FreeSoHeuristic,{});});
        require(f.c.stops==4,"ID reuse cannot be adopted");
    }
    {
        Fixture f;f.hot.tracks[7].patch_id=0;f.hot.tracks[7].hitlist_id=9;f.hot.hitlists[9]={{100},{},{}};
        rejects([&]{f.make([&](auto,const auto&){f.world.remove(1);return std::size_t{0};});});
        require(!f.c.starts && f.c.stops==1,"owner removal during initial chooser rejected before playback");
    }
    {
        Fixture f; // The track's initial patch is not necessarily the final one.
        f.hit.bytes.resize(16);f.hit.bytes.insert(f.hit.bytes.end(),{5,50,200,0,0,0,2,5,11,72,6,1,2,12});
        odh::SoundThreadCollection collection(f.world,[&](const auto& request,const auto& binding){
            return odh::make_hit_sound(f.hit,f.hot,request,binding,{},f.backend(),std::make_unique<Owner>(f.c),{},
                odh::HitJumpOperand::FreeSoHeuristic,odh::HitArguments{{1,20,0}});
        });
        require(collection.submit(f.request) && !f.c.starts,"factory fits collection without hidden tick");
        require(collection.tick().active==1 && f.c.patch==200,"bytecode replaces initial patch");
        f.c.active=false;collection.tick();collection.tick();
        require(collection.tick().completed==1 && f.c.stops==1,"full factory/collection completion");
        require(collection.submit(f.request),"second submission");collection.tick();
        f.world.remove(1);require(collection.tick().owner_invalid==1 && f.c.stops==2,"factory-created instance cancellation");
    }
    {
        Fixture f;f.world.set_person_data(1,7,1000);
        f.request.source_variables=odh::sound_source_variables(*f.world.find(1));
        f.hit.bytes.resize(16);f.hit.bytes.insert(f.hit.bytes.end(),{72,50,1,2,2,5,11,12});
        auto instance=f.make();
        f.world.set_person_data(1,7,2000);
        instance->tick();require(f.c.patch==200,"reader captured by value reads live bank rather than submitted snapshot");
    }
    std::cout<<checks<<" sound event factory checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
