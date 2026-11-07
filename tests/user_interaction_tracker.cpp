#include "odh/user_interaction_tracker.hpp"
#include "odh/entity_thread_controllers_snapshot_codec.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include <iostream>
#include <memory>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
odh::RoutineKey key(unsigned id){return odh::routine_key(static_cast<std::uint16_t>(id));}
odh::Bhav code(std::initializer_list<odh::Instruction> instructions){return {0x8002,instructions,{},odh::Bhav::Metadata{0,4,4,0}};}
odh::Instruction idle(unsigned next=1){return {17,static_cast<std::uint8_t>(next),255,{0,0,1,0,0,0,0,0}};}
odh::Instruction set(unsigned index,unsigned value,unsigned next=254){return {2,static_cast<std::uint8_t>(next),255,{static_cast<std::uint8_t>(index),0,static_cast<std::uint8_t>(value),0,0,5,8,7}};}
odh::RoutineSet program(){return {{key(4096),code({idle(),set(9,99)})}};}
odh::RoutineOwners owners(){return {{22,{
    {key(4097),code({set(0,11)})},{key(4098),code({set(1,22,255)})},
    {key(4099),code({idle(),set(3,77)})},{key(4100),code({set(4,88,255)})}}}};}
struct Fixture {
    odh::World world;odh::InteractionQueue queue;odh::Vm vm;
    explicit Fixture(bool initialize=true):queue(world,1,8),vm(program(),world.vm_memory(),{},queue.primitive(),owners()){
        if(!initialize)return;
        require(world.create(1,odh::EntityKind::avatar,{0,80,0,16})&&world.create(2,odh::EntityKind::object)&&
            world.create(3,odh::EntityKind::object)&&world.declare_object_word(1,8,16),"player fixture entities");
        vm.start(key(4096),{1,1,1,0},{0});require(queue.bind(vm),"bind player fixture");
        queue.set_queue_skipped_handler([](const auto&){return true;}); // Known synthetic empty EP4.
    }
    std::uint64_t enqueue(unsigned routine=4097,unsigned target=2,std::uint32_t flags=0){
        require(queue.enqueue_user(key(routine),{1,static_cast<std::int16_t>(target),static_cast<std::int16_t>(target),22},{},flags),"enqueue player action");
        return queue.last_enqueued_uid();
    }
    void run(){vm.run(32);require(queue.observe(),"observe executed player action");}
    void tick(std::uint64_t now){require(queue.tick(now),"tick player action");run();}
};
odh::InteractionQueueSnapshot codec(const odh::InteractionQueueSnapshot& queue){
    odh::EntityThreadControllersSnapshot state{};state.interactions=queue;
    return odh::decode_entity_thread_controllers_snapshot(odh::encode_entity_thread_controllers_snapshot(state)).interactions;
}
std::unique_ptr<Fixture> restore(const Fixture& source){
    auto result=std::make_unique<Fixture>(false);
    require(result->world.restore_snapshot(source.world.snapshot()),"restore player World");
    require(result->vm.restore_snapshot(odh::decode_vm_snapshot(odh::encode_vm_snapshot(source.vm.snapshot())))&&
        result->queue.bind(result->vm)&&result->queue.restore_snapshot(codec(source.queue.snapshot())),"restore player VM and native queue");
    result->queue.set_queue_skipped_handler([](const auto&){return true;});return result;
}
void immediate_player_request(){
    Fixture source;const auto uid=source.enqueue(4097,2,4);
    odh::UserInteractionTracker tracker;
    require(tracker.observe(source.queue.snapshot())&&tracker.current(1,uid)&&
        tracker.current(1,uid)->state==odh::UserInteractionState::pending,
        "immediate player request keeps ordinary tracking identity");
    auto wrong=source.queue.snapshot();wrong.actions[0].call.return_mode=odh::VmCallReturnMode::interaction_next_tick;
    require(!tracker.observe(wrong),"immediate request rejects mismatched call mode without changing records");
    require(source.queue.try_run_immediate({})&&tracker.observe(source.queue.snapshot())&&
        tracker.current(1,uid)->state==odh::UserInteractionState::active,
        "immediate request becomes active on its original native identity");
    require(source.vm.run(8)==odh::VmStatus::yielded&&source.queue.observe()&&
        tracker.observe(source.queue.snapshot())&&tracker.current(1,uid)->result==true&&
        tracker.current(1,uid)->state==odh::UserInteractionState::completed_true,
        "immediate return completes the player request while preserving parent execution");
}
void queued_and_reopened(){
    Fixture source;const auto first=source.enqueue(),second=source.enqueue(4098,3);
    odh::UserInteractionTracker tracker;
    require(tracker.observe(source.queue.snapshot())&&tracker.pending(1).size()==2&&
        tracker.pending(1)[0]->identity.uid==first&&tracker.pending(1)[1]->identity.uid==second,
        "multiple player requests retain native queue order");
    const auto first_identity=tracker.current(1,first)->identity;
    auto copy=restore(source);odh::UserInteractionTracker reopened;
    require(reopened.observe(copy->queue.snapshot())&&reopened.pending(1).size()==2&&
        reopened.current(1,first)->action.call.context.code_owner==22&&reopened.current(1,second)->action.call.key.id==4098,
        "fresh presentation tracker rehydrates pending requests from exact saved queue metadata");
    source.run();copy->run();require(tracker.observe(source.queue.snapshot())&&reopened.observe(copy->queue.snapshot()),"observe first completion in both continuations");
    require(tracker.current(1,first)->state==odh::UserInteractionState::completed_true&&
        tracker.current(1,first)->result==true&&reopened.current(1,first)->result==true&&
        tracker.current(1,second)->state==odh::UserInteractionState::pending&&!tracker.current(1,second)->result,
        "first completion cannot be attributed to a pending second request");
    require(source.world.remove(2)&&copy->world.remove(2)&&tracker.observe(source.queue.snapshot()),
        "successful completion remains valid after cleanup removes its target");
    source.tick(1);copy->tick(1);require(tracker.observe(source.queue.snapshot())&&reopened.observe(copy->queue.snapshot()),"observe second completion");
    require(tracker.current(1,second)->state==odh::UserInteractionState::completed_false&&
        tracker.current(1,second)->result==false&&reopened.current(1,second)->result==false&&
        source.vm.temps()==copy->vm.temps()&&tracker.pending(1).empty()&&
        tracker.find(first_identity)->state==odh::UserInteractionState::completed_true,
        "false result is attributed to its own native identity without changing prior success");
}
void nested_child_is_not_user_completion(){
    Fixture source;const auto parent=source.enqueue(4099),later=source.enqueue(4098,3);
    odh::UserInteractionTracker tracker;require(tracker.observe(source.queue.snapshot()),"track pending parent");
    require(source.vm.run(1)==odh::VmStatus::budget_exhausted&&source.queue.active(),"dispatch parent");
    require(source.queue.enqueue_ts1(key(4100),{1,3,3,22},{},100,odh::InteractionQueueMode::normal,0,false,false),"enqueue non-user child");
    require(source.vm.run(16)==odh::VmStatus::yielded&&tracker.observe(source.queue.snapshot())&&
        tracker.current(1,parent)->state==odh::UserInteractionState::active&&tracker.pending(1).size()==2,
        "suspended user ancestor remains active while its non-user child returns");
    require(source.queue.observe()&&source.queue.completion()->uid!=parent&&tracker.observe(source.queue.snapshot())&&
        !tracker.current(1,parent)->result&&tracker.current(1,parent)->state==odh::UserInteractionState::active,
        "observed child completion does not finish its user parent");
    auto copy=restore(source);odh::UserInteractionTracker reopened;
    require(reopened.observe(copy->queue.snapshot())&&reopened.current(1,parent)->state==odh::UserInteractionState::active&&
        reopened.current(1,later)->state==odh::UserInteractionState::pending,
        "restored active parent and pending next request rehydrate without inventing cancellation from flags");
    source.tick(1);copy->tick(1);require(tracker.observe(source.queue.snapshot())&&reopened.observe(copy->queue.snapshot())&&
        tracker.current(1,parent)->result==true&&reopened.current(1,parent)->result==true&&
        !tracker.current(1,later)->result,"parent completion matches after nested save/reopen");
    source.tick(2);copy->tick(2);require(tracker.observe(source.queue.snapshot())&&reopened.observe(copy->queue.snapshot())&&
        tracker.current(1,later)->result==false&&reopened.current(1,later)->result==false,
        "later request remains independently attributable after child and parent completions");
}
void cancellation_and_unknown_disappearance(){
    {
        Fixture source;const auto first=source.enqueue(),second=source.enqueue(4098,3);
        odh::UserInteractionTracker tracker;require(tracker.observe(source.queue.snapshot()),"observe before pending cancellation");
        const auto identity=tracker.current(1,first)->identity;
        require(source.queue.cancel(first)==odh::InteractionCancelResult::removed&&
            tracker.note_cancel(identity,odh::InteractionCancelResult::removed)&&tracker.observe(source.queue.snapshot())&&
            tracker.find(identity)->state==odh::UserInteractionState::cancelled&&!tracker.find(identity)->result&&
            tracker.current(1,second)->state==odh::UserInteractionState::pending,
            "accepted pending removal records cancellation without a fake false result");
    }
    {
        Fixture source;const auto uid=source.enqueue();odh::UserInteractionTracker tracker;
        require(source.vm.run(1)==odh::VmStatus::budget_exhausted&&tracker.observe(source.queue.snapshot()),"active cancellation starts");
        const auto identity=tracker.current(1,uid)->identity;
        const auto result=source.queue.cancel(uid);
        require(result==odh::InteractionCancelResult::notified&&tracker.note_cancel(identity,result)&&
            tracker.observe(source.queue.snapshot())&&tracker.current(1,uid)->state==odh::UserInteractionState::cancel_requested&&
            !tracker.current(1,uid)->result,"cooperative cancel request is not completion");
        source.run();require(tracker.observe(source.queue.snapshot())&&tracker.current(1,uid)->result==true&&
            tracker.current(1,uid)->cancellation_requested,"actual BHAV result wins even if cancellation was requested");
    }
    {
        Fixture source;const auto uid=source.enqueue();odh::UserInteractionTracker tracker;
        require(tracker.observe(source.queue.snapshot())&&source.queue.cancel(uid)==odh::InteractionCancelResult::removed&&
            tracker.observe(source.queue.snapshot())&&tracker.current(1,uid)->state==odh::UserInteractionState::dropped&&
            !tracker.current(1,uid)->result&&!tracker.current(1,uid)->cancellation_requested,
            "unexplained disappearance is dropped, never invented user cancellation or false completion");
    }
    {
        Fixture source;const auto uid=source.enqueue(4097,2,1U<<10);
        require(source.queue.cancel(uid)==odh::InteractionCancelResult::notified,"notify retained MustRun without human tracker event");
        odh::UserInteractionTracker reopened;
        require(reopened.observe(codec(source.queue.snapshot()))&&reopened.current(1,uid)->state==odh::UserInteractionState::pending&&
            !reopened.current(1,uid)->cancellation_requested,"restored NotifyIdle and zero current priority do not prove human cancellation intent");
    }
}
void generations_and_actor_isolation(){
    Fixture source;const auto uid=source.enqueue();odh::UserInteractionTracker tracker;
    require(tracker.observe(source.queue.snapshot()),"observe old actor epoch");const auto old=tracker.current(1,uid)->identity;
    source.vm.start(key(4096),{1,1,1,0},{0});const auto next=source.enqueue(4098,3);
    require(tracker.observe(source.queue.snapshot())&&tracker.find(old)->state==odh::UserInteractionState::invalidated&&
        !tracker.current(1,uid)&&tracker.current(1,next)->identity.run_generation!=old.run_generation,
        "Main restart invalidates outstanding old-run requests before matching identities");
    const auto before_reuse=tracker.current(1,next)->identity;
    require(source.world.remove(1)&&source.world.create(1,odh::EntityKind::avatar,{0,80,0,16})&&
        source.world.declare_object_word(1,8,16),"actor ID reused with new generation");
    odh::InteractionQueue replacement(source.world,1,8);
    odh::Vm replacement_vm(program(),source.world.vm_memory(),{},replacement.primitive(),owners());
    replacement_vm.start(key(4096),{1,1,1,0},{0});require(replacement.bind(replacement_vm)&&
        replacement.enqueue_user(key(4097),{1,2,2,22},{},0)&&tracker.observe(replacement.snapshot()),"new actor epoch observed");
    const auto reused=replacement.last_enqueued_uid();
    require(reused==uid&&tracker.find(before_reuse)->state==odh::UserInteractionState::invalidated&&
        tracker.current(1,reused)->identity.actor_generation!=old.actor_generation&&tracker.find(old)->state==odh::UserInteractionState::invalidated,
        "same actor/native UID cannot attach to another entity generation");
    require(source.world.create(4,odh::EntityKind::avatar,{0,80,0,16})&&source.world.declare_object_word(4,8,16),"second actor exists");
    odh::InteractionQueue other(source.world,4,8);
    odh::Vm other_vm(program(),source.world.vm_memory(),{},other.primitive(),owners());
    other_vm.start(key(4096),{4,4,4,0},{0});require(other.bind(other_vm)&&other.enqueue_user(key(4098),{4,3,3,22},{},0)&&
        tracker.observe(other.snapshot()),"observe another actor using the same native UID");
    other_vm.run(16);require(other.observe()&&tracker.observe(other.snapshot())&&tracker.current(4,reused)->result==false&&
        !tracker.current(1,reused)->result&&tracker.current(1,reused)->state==odh::UserInteractionState::pending,
        "completion for another actor cannot finish the selected actor request");
}
void terminal_transition_retention(){
    for(const unsigned transition:{0U,1U,2U,3U}){
        Fixture source;const auto old_uid=source.enqueue();
        odh::UserInteractionTracker tracker(4,2);
        require(tracker.observe(source.queue.snapshot()),"retain old pending request before newer history");
        const auto old_identity=tracker.current(1,old_uid)->identity;
        require(source.world.create(4,odh::EntityKind::avatar,{0,80,0,16})&&source.world.declare_object_word(4,8,16),
            "terminal retention second actor exists");
        odh::InteractionQueue other(source.world,4,8);
        odh::Vm other_vm(program(),source.world.vm_memory(),{},other.primitive(),owners());
        other_vm.start(key(4096),{4,4,4,0},{0});require(other.bind(other_vm),"bind terminal retention actor");
        std::vector<odh::UserInteractionIdentity> newer;
        for(unsigned n=0;n<2;++n){
            other_vm.start(key(4096),{4,4,4,0},{0});
            require(other.enqueue_user(key(4097),{4,3,3,22},{},0)&&tracker.observe(other.snapshot()),
                "newer request enters terminal history fixture");
            newer.push_back(tracker.current(4,other.last_enqueued_uid())->identity);
            other_vm.run(16);require(other.observe()&&tracker.observe(other.snapshot()),"newer request completes");
        }
        require(tracker.records().size()==3&&tracker.find(old_identity)&&tracker.find(newer[0])&&tracker.find(newer[1]),
            "full terminal history does not evict the older pending request");
        if(transition==0){source.run();require(tracker.observe(source.queue.snapshot()),"old pending request completes last");}
        else if(transition==1){require(source.queue.cancel(old_uid)==odh::InteractionCancelResult::removed&&
            tracker.note_cancel(old_identity,odh::InteractionCancelResult::removed)&&tracker.observe(source.queue.snapshot()),
            "old pending removal is a fresh terminal transition");}
        else if(transition==2){source.vm.start(key(4096),{1,1,1,0},{0});require(source.queue.tick(0)&&
            tracker.observe(source.queue.snapshot()),"old pending invalidation is a fresh terminal transition");}
        else require(source.queue.cancel(old_uid)==odh::InteractionCancelResult::removed&&tracker.observe(source.queue.snapshot()),
            "old pending unexplained drop is a fresh terminal transition");
        const auto* latest=tracker.find(old_identity);
        const auto expected=transition==0?odh::UserInteractionState::completed_true:transition==1?
            odh::UserInteractionState::cancelled:transition==2?odh::UserInteractionState::invalidated:odh::UserInteractionState::dropped;
        require(latest&&latest->state==expected&&!tracker.find(newer[0])&&tracker.find(newer[1])&&tracker.records().size()==2,
            "history evicts oldest terminal transition, never the newly finished request discovered earlier");
        if(transition==0)require(latest->result==true,"fresh successful completion remains available to Stand adapter");
    }
}
void bounds_and_reordering(){
    Fixture source;const auto first=source.enqueue(),second=source.enqueue(4098,3);
    odh::UserInteractionTracker too_small(1,1);
    require(!too_small.observe(source.queue.snapshot())&&too_small.records().empty(),"live capacity rejection never silently discards pending requests");
    odh::UserInteractionTracker bounded(2,1);require(bounded.observe(source.queue.snapshot()),"bounded observer accepts two live requests");
    const auto second_identity=bounded.current(1,second)->identity;
    auto reordered=source.queue.snapshot();std::swap(reordered.actions[0],reordered.actions[1]);
    require(bounded.observe(reordered)&&bounded.pending(1)[0]->identity.uid==second&&bounded.pending(1)[1]->identity.uid==first,
        "presentation order follows native queue positions rather than discovery order");
    require(bounded.observe(source.queue.snapshot()),"authoritative queue order can change again without losing identity");
    auto malformed=source.queue.snapshot();++malformed.actions[0].target_generation;
    const auto generation=bounded.current(1,first)->action.target_generation;
    require(!bounded.observe(malformed)&&bounded.pending(1).size()==2&&bounded.current(1,first)->action.target_generation==generation,
        "changed invocation metadata is rejected without publishing partial records");
    source.run();require(bounded.observe(source.queue.snapshot())&&bounded.records().size()==2&&bounded.current(1,second),"terminal history does not evict a pending request");
    source.tick(1);require(bounded.observe(source.queue.snapshot())&&bounded.records().size()==1&&
        bounded.find(second_identity)&&bounded.find(second_identity)->result==false,"terminal history prunes oldest completed records only");
    odh::UserInteractionTracker none(2,0);require(none.observe(source.queue.snapshot())&&none.records().empty(),"reopen never fabricates unknown terminal history from the last completion event");
}
}
int main(){try{immediate_player_request();queued_and_reopened();nested_child_is_not_user_completion();cancellation_and_unknown_disappearance();generations_and_actor_isolation();terminal_transition_retention();bounds_and_reordering();
    std::cout<<"user_interaction_tracker: "<<checks<<" checks passed (synthetic native execution/snapshot evidence)\n";
}catch(const std::exception& error){std::cerr<<"user_interaction_tracker: "<<error.what()<<'\n';return 1;}}
