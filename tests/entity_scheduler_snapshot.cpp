#include "odh/entity_scheduler.hpp"
#include "odh/entity_scheduler_snapshot_codec.hpp"
#include "odh/entity_thread_controllers.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <sstream>

namespace {int allocation_countdown=-1;}
void* operator new(std::size_t size) {
    if(allocation_countdown==0){allocation_countdown=-1;throw std::bad_alloc();}
    if(allocation_countdown>0)--allocation_countdown;
    if(auto* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new(std::size_t size,const std::nothrow_t&) noexcept {
    try{return ::operator new(size);}catch(...){return nullptr;}
}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);}
namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
odh::RoutineKey key(){return odh::routine_key(4096);}
odh::RoutineSet program(bool sleeping=false){
    odh::Bhav bhav{0x8002,{}, {},odh::Bhav::Metadata{0,4,4,0}};
    if(sleeping)bhav.instructions.push_back({0,1,255,{}});
    for(unsigned value=1;value<=3;++value)
        bhav.instructions.push_back({2,static_cast<std::uint8_t>(value==3?0:value+(sleeping?1:0)),255,
            {25,0,static_cast<std::uint8_t>(value),0,0,5,4,7}});
    return {{key(),bhav}};
}
std::string values(const odh::EntityVmSchedulerSnapshot& state){
    std::ostringstream out;
    out<<state.entity_limit<<','<<state.instructions_last_tick<<','<<state.faulted<<';'
       <<state.diagnostic.size()<<':'<<state.diagnostic<<';'<<bool(state.last_tick)<<':'
       <<state.last_tick.value_or(0)<<';'<<bool(state.budget_cursor)<<':'<<state.budget_cursor.value_or(0)<<';';
    for(const auto& e:state.entities)out<<e.id<<','<<e.run_generation<<','<<e.steps<<','<<int(e.status)<<','
        <<e.before<<','<<e.after<<','<<e.interrupt<<';';
    return out.str();
}
odh::EntityVmSchedulerSnapshot roundtrip(const odh::EntityVmSchedulerSnapshot& state){
    return odh::decode_entity_vm_scheduler_snapshot(odh::encode_entity_vm_scheduler_snapshot(state));
}
struct Fixture {
    odh::World world;
    std::vector<std::unique_ptr<odh::EntityThreadControllers>> controllers;
    std::vector<std::unique_ptr<odh::Vm>> vms;
    odh::EntityVmScheduler scheduler{4};
    std::vector<int> events;
    bool sleeping;
    explicit Fixture(bool initialize=true,bool sleep=false):sleeping(sleep){
        for(std::int16_t id=1;id<=3;++id){
            if(initialize)require(world.create(id,odh::EntityKind::avatar)&&world.declare_object_word(id,25,0),"World fixture");
            controllers.push_back(std::make_unique<odh::EntityThreadControllers>(world,id));
            auto* controller=controllers.back().get();
            vms.push_back(std::make_unique<odh::Vm>(program(sleeping),world.vm_memory(),odh::VmLimits{},controller->sleep_primitive()));
            auto& vm=*vms.back();
            if(initialize)vm.start(key(),{id,id,id,0},{3});
            if(initialize)require(controller->bind(vm),"bind equivalent controller");
            require(scheduler.add(id,vm,[this,id,controller](auto tick){events.push_back(100+id);return controller->advance(tick);},
                [this,id,controller]{events.push_back(200+id);return controller->observe();},
                [controller]{return controller->notify();}),"register equivalent VM/hooks");
        }
    }
    void tick(unsigned tick,std::uint64_t budget=1){
        events.clear();
        require(scheduler.tick(tick,budget,2,[this](auto id,const auto& trace){
            events.push_back(id);events.push_back(static_cast<int>(trace.pc));
        }),"valid next tick accepted");
    }
};
std::unique_ptr<Fixture> destination(const Fixture& source){
    auto target=std::make_unique<Fixture>(false,source.sleeping);
    require(target->world.restore_snapshot(source.world.snapshot()),"restore surrounding World");
    for(std::size_t i=0;i<source.vms.size();++i){
        require(target->vms[i]->restore_snapshot(source.vms[i]->snapshot()),"restore each VM");
        require(target->controllers[i]->bind(*target->vms[i]),"bind restored World/VM");
        require(target->controllers[i]->restore_snapshot(source.controllers[i]->snapshot()),"restore each controller");
    }
    return target;
}
void same(const Fixture& left,const Fixture& right){
    require(values(left.scheduler.snapshot())==values(right.scheduler.snapshot()),"complete scheduler snapshot matches");
    require(left.events==right.events,"trace and before/after hook order matches");
    for(std::int16_t id=1;id<=3;++id){
        const auto i=static_cast<std::size_t>(id-1);
        require(left.world.find(id)->object_words==right.world.find(id)->object_words,"World writes match");
        const auto a=left.vms[i]->snapshot(),b=right.vms[i]->snapshot();
        require(a.steps==b.steps&&a.status==b.status&&a.run_generation==b.run_generation&&a.temps==b.temps&&
            a.stack.size()==b.stack.size()&&a.stack.front().pc==b.stack.front().pc,"VM continuation matches");
    }
}
void continuation(){
    Fixture source;
    source.tick(1);source.tick(2);
    require(source.scheduler.snapshot().budget_cursor==3,"checkpoint keeps noninitial scarce-budget cursor");
    auto target=destination(source);const auto checkpoint=roundtrip(source.scheduler.snapshot());
    require(target->scheduler.restore_snapshot(checkpoint),"restore tick-boundary scheduler");
    require(!target->scheduler.restore_snapshot(checkpoint),"successful restore consumes freshness");
    source.events.clear();target->events.clear();same(source,*target);
    require(!target->scheduler.tick(2,1,1)&&!target->scheduler.tick(4,1,1),"restored clock rejects duplicate and skipped tick");
    for(unsigned tick=3;tick<12;++tick){source.tick(tick,tick%2?1:5);target->tick(tick,tick%2?1:5);same(source,*target);}
    require(source.scheduler.remove(2)&&target->scheduler.remove(2),"registered removal preserves restored fairness");
    source.tick(12);target->tick(12);same(source,*target);
    Fixture sleeping(true,true);sleeping.tick(1,6);
    auto awakened=destination(sleeping);
    require(awakened->scheduler.restore_snapshot(roundtrip(sleeping.scheduler.snapshot())),"restore waiting VMs with matching controllers");
    require(sleeping.scheduler.interrupt(3)&&awakened->scheduler.interrupt(3),"restored interrupt dispatches rebuilt handler");
    for(unsigned tick=2;tick<8;++tick){sleeping.tick(tick,6);awakened->tick(tick,6);same(sleeping,*awakened);}
    odh::EntityVmScheduler empty(0),copy(1);
    require(empty.tick(1,1,1)&&copy.restore_snapshot(roundtrip(empty.snapshot()))&&copy.tick(2,1,1),"empty scheduler preserves next tick and zero capacity");
}
void rejections(){
    Fixture source;source.tick(1);const auto good=roundtrip(source.scheduler.snapshot());
    const auto reject=[&](const auto& bad){
        auto target=destination(source);const auto before=values(target->scheduler.snapshot());
        require(!target->scheduler.restore_snapshot(bad)&&values(target->scheduler.snapshot())==before,"invalid snapshot rejects atomically");
        require(target->scheduler.restore_snapshot(good),"rejection preserves freshness");
    };
    auto bad=good;bad.entity_limit=5;reject(bad);
    bad=good;bad.entity_limit=2;reject(bad);
    bad=good;bad.entities.pop_back();reject(bad);
    bad=good;std::swap(bad.entities[0],bad.entities[1]);reject(bad);
    bad=good;bad.entities[1].id=1;reject(bad);
    bad=good;++bad.entities[0].run_generation;reject(bad);
    bad=good;++bad.entities[0].steps;reject(bad);
    bad=good;bad.entities[0].status=odh::VmStatus::fault;reject(bad);
    bad=good;bad.entities[0].before=false;reject(bad);
    bad=good;bad.entities[0].after=false;reject(bad);
    bad=good;bad.entities[0].interrupt=false;reject(bad);
    bad=good;bad.budget_cursor=99;reject(bad);
    bad=good;bad.last_tick=0;reject(bad);
    bad=good;bad.last_tick=std::uint64_t{std::numeric_limits<std::uint32_t>::max()}+1;reject(bad);
    bad=good;bad.last_tick.reset();reject(bad);
    bad=good;bad.faulted=true;reject(bad);
    require(!source.scheduler.restore_snapshot(good),"used scheduler rejects restore");
    auto removed=destination(source);require(removed->scheduler.remove(3),"remove prior to restore");
    auto smaller=good;smaller.entities.pop_back();
    require(!removed->scheduler.restore_snapshot(smaller),"remove consumes freshness");
    auto signaled=destination(source);require(signaled->scheduler.interrupt(1),"notify prior to restore");
    require(!signaled->scheduler.restore_snapshot(good),"interrupt consumes freshness");
    auto invalid_tick=destination(source);require(!invalid_tick->scheduler.tick(0,1,1)&&!invalid_tick->scheduler.restore_snapshot(good),"invalid runtime tick consumes freshness");
}
void fault_and_atomicity(){
    Fixture source;
    // A rejecting callback can leave partially applied external state. The
    // scheduler restores the terminal fault, never reruns that partial tick.
    odh::EntityVmScheduler failed(1),restored(1);int callback_effects=0;
    require(failed.add(1,*source.vms[0],[&](auto){++callback_effects;throw std::runtime_error(std::string(128,'f'));return false;})&&
        restored.add(1,*source.vms[0],[](auto){return true;}),"fault fixture registers equivalent hook shapes");
    require(!failed.tick(1,1,1)&&callback_effects==1,"partially applied first tick faults");
    const auto payload=roundtrip(failed.snapshot());require(payload.faulted&&!payload.last_tick,"fault snapshot retains attempted first-tick state");
    const auto before=values(restored.snapshot());
    bool published=false;unsigned failures=0;
    for(int fail=0;fail<8&&!published;++fail){
        bool threw=false;allocation_countdown=fail;
        try{published=restored.restore_snapshot(payload);}catch(const std::bad_alloc&){threw=true;}
        allocation_countdown=-1;
        if(!published){++failures;require(threw&&values(restored.snapshot())==before,"allocation failure is atomic and keeps freshness");}
    }
    require(failures>=1&&published&&values(restored.snapshot())==values(payload),"staged diagnostic publishes after allocation succeeds");
    require(!restored.tick(1,1,1)&&callback_effects==1&&!restored.remove(1),"restored terminal fault cannot repeat partial effects");
    odh::EntityVmScheduler guarded(1);bool capture_guard=false,restore_guard=false;
    const auto safe=guarded.snapshot();
    require(guarded.add(1,*source.vms[0],[&](auto){
        try{guarded.snapshot();}catch(const std::logic_error&){capture_guard=true;}
        try{guarded.restore_snapshot(safe);}catch(const std::logic_error&){restore_guard=true;}
        return true;
    })&&guarded.tick(1,1,1)&&capture_guard&&restore_guard,"in-flight scheduler callbacks cannot capture or restore");
}
}
int main(){try{continuation();rejections();fault_and_atomicity();std::cout<<checks<<" scheduler snapshot checks passed (synthetic in-memory continuation only)\n";}
    catch(const std::exception& error){allocation_countdown=-1;std::cerr<<error.what()<<'\n';return 1;}}
