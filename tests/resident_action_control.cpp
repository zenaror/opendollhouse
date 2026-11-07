#include "odh/resident_action_control.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
odh::Bhav tree(std::initializer_list<odh::Instruction> instructions){return {0x8002,instructions,{},odh::Bhav::Metadata{0,4,4,0}};}
struct Fixture {
    odh::World world;odh::InteractionQueue queue{world,1,8};
    odh::Vm vm;
    static odh::RoutineSet main(){return {{odh::routine_key(4096),tree({{17,1,255,{0,0,1,0,0,0,0,0}},{2,254,255,{}}})}};}
    static odh::RoutineSet actions(){return {
        {odh::routine_key(4101),tree({{2,254,255,{0,0,7,0,0,5,8,7}}})},
        {odh::routine_key(4102),tree({{2,254,255,{1,0,8,0,0,5,8,7}}})}};}
    explicit Fixture(bool start=true):vm(main(),world.vm_memory(),{},queue.primitive(),{{22,actions()}}){
        if(!start)return;
        require(world.create(1,odh::EntityKind::avatar,{0,88,3,16})&&world.create(2,odh::EntityKind::object)&&
        world.declare_object_word(1,8,16),"resident control identities");
        if(start)require(vm.start(odh::routine_key(4096),{1,1,1,0},{4})==odh::VmStatus::running,"resident control real Main");
        require(queue.bind(vm),"resident control actual queue binding");}
};
}
int main(){try{
    Fixture f;require(f.queue.enqueue_source_ts1(odh::routine_key(4101),{1,2,2,22},std::vector<std::int16_t>(4),2,
        odh::InteractionQueueMode::normal,0,false,false)&&f.vm.run(1)==odh::VmStatus::budget_exhausted&&f.queue.active(),
        "real autonomous-priority action becomes protected active prefix");
    const auto current=odh::resident_control_identity(f.world,f.vm,f.queue);require(bool(current),"capture actual resident authority");
    const auto original=f.queue.snapshot().active->uid;const auto generation=f.world.find(2)->generation;
    odh::VmCall user{odh::routine_key(4102),{1,2,2,22},std::vector<std::int16_t>(4)};
    std::string why;
    require(odh::enqueue_resident_user_action(f.world,f.vm,f.queue,*current,user,generation,0,{}, {},&why)&&
        f.queue.size()==2&&f.queue.snapshot().active->uid==original&&!f.queue.completion(),
        "busy resident accepts source user action without clearing active action or inventing completion");
    require(f.queue.snapshot().actions.back().priority==50&&f.queue.snapshot().actions.back().call.arguments==user.arguments,
        "resident bridge preserves source user priority and exact arguments");
    Fixture other;
    require(other.vm.run_generation()==f.vm.run_generation()&&other.world.find(1)->generation==f.world.find(1)->generation&&
        !odh::resident_control_identity(f.world,other.vm,f.queue)&&
        !odh::enqueue_resident_user_action(f.world,other.vm,f.queue,*current,user,generation,0,{}, {},&why)&&
        odh::cancel_resident_action(f.world,other.vm,f.queue,*current,original,&why)==odh::InteractionCancelResult::unsupported&&f.queue.size()==2,
        "different VM with same actor/run generations cannot borrow actual resident queue authority");
    auto stale=*current;++stale.generation;
    require(odh::cancel_resident_action(f.world,f.vm,f.queue,stale,original,&why)==odh::InteractionCancelResult::unsupported&&f.queue.size()==2,
        "stale resident cancellation cannot change another actor generation");
    require(odh::cancel_resident_action(f.world,f.vm,f.queue,*current,original,&why)==odh::InteractionCancelResult::notified&&
        f.queue.size()==2&&f.queue.snapshot().active->uid==original&&f.queue.snapshot().actions.front().notify_idle&&
        (static_cast<std::uint16_t>(f.world.find(1)->object_words.at(8))&128U)&&!f.queue.completion(),
        "active cancellation preserves source action and requests cooperative exit");
    const auto saved=f.queue.snapshot();Fixture reopened(false);
    require(reopened.world.restore_snapshot(f.world.snapshot()),"fresh resident control World restore");
    require(reopened.vm.restore_snapshot(f.vm.snapshot()),"fresh never-started resident VM restores exact pending action ancestry");
    require(reopened.queue.bind(reopened.vm)&&reopened.queue.restore_snapshot(saved),"fresh resident queue restores protected action and pending user intent");
    require(reopened.queue.snapshot().active->uid==original&&reopened.queue.snapshot().actions.front().notify_idle&&reopened.queue.size()==2,
        "fresh resume does not erase source cancellation or queued user action");
    require(!odh::enqueue_resident_user_action(f.world,f.vm,f.queue,*current,user,generation+1,0,{}, {},&why)&&f.queue.size()==2,
        "stale target generation rejects user intent before mutating queue");
    require(odh::cancel_resident_action(f.world,f.vm,f.queue,*current,9999,&why)==odh::InteractionCancelResult::missing,
        "missing source action identity does not become fake completed cancellation");
    {Fixture g;const auto owner=odh::resident_control_identity(g.world,g.vm,g.queue);require(bool(owner),"reuse resident authority");
     odh::VmCall stale{odh::routine_key(4102),{1,2,2,22},std::vector<std::int16_t>(4)};
     require(g.world.bind_context_live(stale.context),"capture exact user request target body");
     const auto old=g.world.find(2)->generation;
     require(g.world.remove_retaining_body({2,old})&&g.world.create(2,odh::EntityKind::object),"reuse user target numeric ID");
     const auto replacement=g.world.find(2)->generation;
     require(!odh::enqueue_resident_user_action(g.world,g.vm,g.queue,*owner,stale,replacement,0,{}, {},&why),
         "stale bound UI request cannot bind replacement target despite equal numeric ID");
     odh::VmCall fresh{odh::routine_key(4102),{1,2,2,22},std::vector<std::int16_t>(4)};
     require(odh::enqueue_resident_user_action(g.world,g.vm,g.queue,*owner,fresh,replacement,0,{}, {},&why),
         "fresh numeric UI request binds actual live replacement generation");
    }
    std::cout<<"resident source user/cancellation bridge passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
