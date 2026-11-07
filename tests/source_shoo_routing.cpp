// SPDX-License-Identifier: MPL-2.0
// Source SHOOED continuation: FreeSO VMRoutingFrame, pinned 7e76c196.
#include "odh/routing.hpp"
#include "odh/source_walk_plan.hpp"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
constexpr odh::CodeOwner owner=0x80000079U;
odh::SlotTable slots(){odh::SlotRecord row{};row.type=3;row.offset_bits={0x41800000,0,0};
    row.snap_target_slot=-1;row.proximity=std::array<std::int32_t,3>{16,16,16};row.facing=-3;row.resolution=16;
    return {0,10,{row},0};}
odh::RoutineSet routines(bool nested,bool child_true=false,bool go_here=false){
    odh::Bhav main{0x8002,{{45,254,255,{0,0,1,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}};
    if(go_here)main.instructions.front()={27,254,255,{23,1,254,255,45,2,5,0}};
    odh::Bhav child{0x8002,{{2,255,255,{0,0,0,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,6,6,0}};
    if(child_true)child.instructions.back().on_true=child.instructions.back().on_false=254;
    if(nested)child.instructions.insert(child.instructions.begin(),{45,1,1,{0,0,1,0,0,0,0,0}});
    return {{odh::routine_key(4116),main},{odh::routine_key(4107),child}};
}
struct Fixture {
    odh::World world;odh::RouteGrid grid{128,128};odh::SlotTable table=slots();odh::Routing route{world,grid,table};odh::Vm vm;
    unsigned ticks=0,profiles=0;
    explicit Fixture(bool nested=false,bool populate=true,bool child_true=false,bool go_here=false):vm({},world.vm_memory(),{},route.primitive(),{{owner,routines(nested,child_true,go_here)}}){
        if(populate){require(world.create(7,odh::EntityKind::avatar,{0,88,0,16})&&world.create(8,odh::EntityKind::avatar,{0,88,0,16})&&
            world.create(9,odh::EntityKind::object)&&world.set_position(7,{32,32,1,0})&&world.set_position(8,{48,48,1,0})&&
            world.set_position(9,{96,32,1,0}),"create real source route participants");
            for(auto id:{7,8,9})require(world.declare_object_word(id,8,0),"declare source intersection flags");}
        route.set_stack_object_slot_table_provider([this](const auto&){return &table;});
        route.set_room_pair_provider([](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{2,2}};});
        odh::SourceSparseServices services;
        services.capture_avatars_to_consider=[](const auto& c,const auto& inherited)->std::optional<std::vector<odh::LiveAttemptWalkIdentity>>{
            if(!c.proof.matches(c.frame,*c.vm,c.instruction,c.dispatch,c.phase))return {};
            return inherited.value_or(std::vector<odh::LiveAttemptWalkIdentity>{});};
        services.plan_input=[this](const auto& c,odh::RoutePoint goal,std::optional<float> outgoing,auto inherited)->std::optional<odh::SourceSparsePlanInput>{
            if(!c.proof.matches(c.frame,*c.vm,c.instruction,c.dispatch,c.phase))return {};
            odh::SourceSparsePlanInput input;input.profile={false,88,false,0};input.avatars_to_consider=inherited.value_or(std::vector<odh::LiveAttemptWalkIdentity>{});
            input.request.actor={7,world.find(7)->generation};input.request.destination={goal.x,goal.y};input.request.bezier=false;input.request.outgoing_direction=outgoing;
            input.services.context_still_matches=[c]{return c.proof.matches_after_restore(c.frame,*c.vm,c.instruction,c.dispatch,c.phase);};
            input.services.room_at=[](const auto&){return std::optional<std::uint16_t>{2};};
            input.services.domain=[this](auto){odh::SourceWalkDomain d;d.state=odh::LiveAttemptWalkState::ready;d.request.actor={7,world.find(7)->generation};
                d.request.room=2;d.request.level=1;d.request.room_bounds={0,0,16,16};d.request.architecture_complete=true;d.request.entities_complete=true;d.request.avatars_to_consider_complete=true;
                for(const auto& e:world.snapshot().entities)d.request.entities.push_back({e.id,e.generation});
                d.request.architectural_and_bounds={{-32,-32,288,-16},{-32,272,288,288},{-32,-32,-16,288},{272,-32,288,288}};
                d.services.inventory_still_matches=[](const auto&){return true;};
                d.services.footprint=[](auto){return odh::LiveAttemptWalkFootprint{odh::LiveAttemptWalkFootprintState::absent,{}};};return d;};return input;};
        services.place=[](const auto&,const auto&)->std::optional<odh::SourceSparsePlacementOutcome>{return odh::SourceSparsePlacementOutcome{odh::SourceSparsePlacementStatus::success,0,0,{}};};
        services.begin_locomotion=[](const auto&,const auto&,auto*){return true;};services.end_locomotion=services.begin_locomotion;
        services.publish_motion=[](const auto&,const auto&,const auto&,auto*){return true;};
        services.publish_velocity=[](const auto&,const auto&,auto*){return true;};services.source_tick=[this](const auto&){++ticks;return true;};
        services.locomotion_profile=[this](const auto&)->std::optional<odh::WalkMotionProfile>{++profiles;return odh::WalkMotionProfile{false,88,false,0};};
        route.set_source_sparse_services(std::move(services));
        odh::SourceLookTowardsServices turn;turn.profile=[](const auto&){return std::optional<odh::SourceLookTowardsProfile>{{0,false}};};
        turn.start=[](const auto&,auto,std::uint8_t,bool,float){return std::optional<std::uint32_t>{1};};
        turn.ended=[](const auto&,auto){return std::optional<bool>{true};};turn.reset=[](const auto&,auto,bool){return true;};route.set_look_towards_services(std::move(turn));
        require(route.bind(vm),"bind real sparse Route45 kernel");
    }
    void start(){require(vm.start(odh::routine_key(4116),{7,7,9,owner})==odh::VmStatus::running&&vm.run(8)==odh::VmStatus::waiting,"source route enters actual pending45");}
    odh::LiveAttemptWalkIdentity shooer()const{return {8,world.find(8)->generation};}
    odh::VmCall destination(bool placed=true){require(world.create(20,odh::EntityKind::object)&&world.define_type(20,0x7c4)&&world.declare_object_word(20,8,0),"create fresh source Destination");
        if(placed)require(world.set_position(20,*world.find(7)->position),"publish accepted Destination placement");
        return {odh::routine_key(4107),{7,20,20,owner},{0,0,0,0,-1,-1},odh::VmCallReturnMode::ordinary,true};}
    odh::RouteStackSnapshot capture(){std::string why;auto saved=route.capture_session_route_stack(owner,&why);if(!saved)throw std::runtime_error("capture shoo stack: "+why);return *saved;}
    void tick(std::uint64_t now){require(route.tick(now),"advance source continuation");if(vm.status()==odh::VmStatus::running)vm.run(64);}
};
void token_contract(){
    Fixture f;f.start();const auto old_motion=f.capture().jobs.front().source_sparse->motion;auto token=f.route.prepare_source_shoo(f.shooer());require(bool(token),"prepare healthy top sparse route before Destination creation");auto call=f.destination();
    auto bad=call;bad.arguments[4]=0;require(!f.route.can_commit_source_shoo(*token,bad),"reject default argument beyond four source zeros");
    bad=call;bad.exact_arguments=false;require(!f.route.can_commit_source_shoo(*token,bad),"reject padded ordinary call");
    bad=call;bad.context.stack_object=9;require(!f.route.can_commit_source_shoo(*token,bad),"reject changed Destination context");
    auto before=f.vm.snapshot();f.vm.temps()[2]=123;f.vm.temp_xl()[1]=456;
    require(f.route.can_commit_source_shoo(*token,call)&&f.route.commit_source_shoo(*token,call),"legitimate shared TEMP/XL effects do not stale routing authority");
    require(f.vm.temps()[2]==123&&f.vm.temp_xl()[1]==456&&f.vm.stack().back().arguments==call.arguments,"commit retains live registers and exact six source arguments");
    auto saved=f.capture();const auto& root=saved.jobs.front();
    require(root.frame.state==odh::RouteFrameState::shooed&&root.external_frame_purpose==odh::RouteExternalFramePurpose::shoo&&
        root.source_sparse->walk_to_purpose==odh::SourceWalkToPurpose::shoo&&root.source_sparse->walk_to_pending&&root.source_sparse->avatars_to_consider.size()==1&&
        root.source_sparse->avatars_to_consider[0].id==8&&root.external_frame_callee_generation==f.world.find(20)->generation,"typed SHOOED continuation freezes Destination and consideration");
    require(root.source_sparse->motion.move_frames==old_motion.move_frames&&root.source_sparse->motion.move_total_frames==old_motion.move_total_frames&&
        root.source_sparse->motion.velocity==old_motion.velocity&&root.source_sparse->motion.remaining.size()==old_motion.remaining.size(),"SHOOED preserves old CurrentPath and motion counters until next AttemptWalk");
    require(!f.route.can_commit_source_shoo(*token,call),"consumed shoo token cannot replay");
    const auto prefix=f.ticks;require(f.route.tick(0)&&f.ticks==prefix,"parked external child remains healthy without drawing source routing prefix");
    require(f.vm.run(64)==odh::VmStatus::yielded&&f.vm.external_frame_return()&&!f.vm.external_frame_return()->result,"source child returns false");
    require(f.world.remove(20),"source may delete Destination before child return is consumed");
    f.tick(1);require(f.route.active()&&f.profiles>0&&!f.route.external_frame_active(),"false shoo child and removed Destination resume original route rather than portal retry");
    require(f.vm.temps()[2]==123&&f.vm.temp_xl()[1]==456&&before.run_generation==f.vm.run_generation(),"resume preserves child register effects and run");
}
void stale_contract(){
    Fixture f;f.start();auto old=f.destination();auto token=f.route.prepare_source_shoo(f.shooer());require(bool(token)&&!f.route.can_commit_source_shoo(*token,old),"Destination must be born after preparation");
    require(f.world.remove(20),"remove old Destination");auto fresh=f.destination(false);
    require(f.route.can_commit_source_shoo(*token,fresh),"legitimate source placement rejection may leave fresh Destination OOW");
    Fixture foreign;foreign.start();require(!foreign.route.can_commit_source_shoo(*token,fresh),"token cannot transfer to another routing owner");
    require(f.vm.update_pending(f.vm.pending()->ticket,{odh::VmFrameBank::arguments,0,17}),"mutate frame through authenticated pending service");
    require(!f.route.can_commit_source_shoo(*token,fresh)&&!f.route.external_frame_active(),"changed source frame refuses atomically");
    Fixture moved;moved.start();auto t=moved.route.prepare_source_shoo(moved.shooer());auto c=moved.destination();
    require(moved.world.set_position(8,{49,48,1,0})&&!moved.route.commit_source_shoo(*t,c)&&moved.vm.stack().size()==1,"changed shooer pose cannot publish child");
    Fixture newrun;newrun.start();auto run_token=newrun.route.prepare_source_shoo(newrun.shooer());auto run_call=newrun.destination();
    require(newrun.vm.start(odh::routine_key(4116),{7,7,9,owner})==odh::VmStatus::running&&
        !newrun.route.can_commit_source_shoo(*run_token,run_call),"new VM run rejects prepared old route authority");
    Fixture oow;oow.start();auto oow_token=oow.route.prepare_source_shoo(oow.shooer());auto oow_call=oow.destination(false);
    require(oow.route.commit_source_shoo(*oow_token,oow_call)&&!oow.world.find(20)->position&&oow.vm.run(64)==odh::VmStatus::yielded,
        "source placement failure keeps Destination OOW while its shoo routine executes");
    auto oow_stack=oow.capture();require(oow_stack.jobs.front().external_frame_callee_generation==oow.world.find(20)->generation,"OOW returned child retains exact Destination epoch");
    require(oow.world.remove(20)&&oow.world.create(20,odh::EntityKind::object)&&oow.world.define_type(20,0x7c4)&&
        !oow.route.capture_session_route_stack(owner),"same-id Destination reuse cannot authenticate child return");
    Fixture recycled;recycled.start();auto r=recycled.route.prepare_source_shoo(recycled.shooer());auto rc=recycled.destination();
    require(recycled.world.remove(8)&&recycled.world.create(8,odh::EntityKind::avatar)&&recycled.world.set_position(8,{48,48,1,0})&&
        !recycled.route.commit_source_shoo(*r,rc),"recycled shooer generation rejects old authority");
}

void wait_and_timeout_contract(){
    for(bool child_true:{false,true})for(bool timeout:{false,true}){
        Fixture seed(false,true,child_true);seed.start();seed.tick(1);
        auto stack=seed.capture();auto& job=stack.jobs.front();
        require(job.source_sparse->motion.current.has_value(),"wait fixture retains a real sampled CurrentPath");
        job.source_sparse->motion.phase=odh::WalkMotionPhase::collision_required;
        odh::SourceRouteCollisionSnapshot collision;collision.wait_time=3;collision.timeout=timeout?1:10;
        collision.phase=odh::SourceRouteCollisionPhase::waiting;job.source_sparse->collision=collision;job.frame.wait_time=3;
        const auto old=job.source_sparse->motion;
        Fixture f(false,false,child_true);require(f.world.restore_snapshot(seed.world.snapshot())&&f.vm.restore_snapshot(seed.vm.snapshot()),"restore live source wait participants");
        std::string why;require(f.route.restore_session_route_stack(stack,owner,&why),why.c_str());
        auto token=f.route.prepare_source_shoo(f.shooer());auto call=f.destination();
        require(token&&f.route.commit_source_shoo(*token,call)&&f.vm.run(64)==odh::VmStatus::yielded&&
            f.vm.external_frame_return()&&f.vm.external_frame_return()->result==child_true,"true and false shoo child returns preserve exact external result until consumed");
        auto committed=f.capture();require(committed.jobs.front().source_sparse->collision->wait_time==3&&
            committed.jobs.front().source_sparse->collision->timeout==(timeout?1:10)&&
            committed.jobs.front().source_sparse->motion.move_frames==old.move_frames,"shoo commit retains existing wait timeout and movement counters");
        f.tick(2);
        if(timeout){require(!f.route.active()&&f.vm.status()==odh::VmStatus::returned_false&&f.world.find(7)->person_data[62]==3&&f.profiles==0,
            "wait timeout falls through source SoftFail without restarting SHOOED or consulting child truth");continue;}
        auto waiting=f.capture();require(waiting.jobs.front().frame.state==odh::RouteFrameState::shooed&&
            waiting.jobs.front().source_sparse->collision->wait_time==2&&waiting.jobs.front().source_sparse->collision->timeout==9&&
            waiting.jobs.front().source_sparse->motion.move_frames==old.move_frames&&f.profiles==0,
            "source wait prefix precedes SHOOED restart and preserves rejected CurrentPath");
        require(!f.route.prepare_source_shoo(f.shooer()),"oldest SHOOED routing frame cannot be shooed again");
        f.tick(3);f.tick(4);require(f.profiles==0&&f.capture().jobs.front().source_sparse->collision->wait_time==0,
            "wait1 to0 consumes NEXT_TICK before locomotion restart");
        f.tick(5);require(f.profiles==1&&f.route.active(),"expired wait restarts source locomotion exactly once for either child return");
    }
}
void ordinary_go_here_direct_shoo(){
    Fixture f(false,true,false,true);f.start();auto before=f.capture();
    require(before.jobs.front().source_sparse&&before.jobs.front().face_anywhere&&before.jobs.front().call_failure_trees&&
        before.jobs.front().pending.instruction.instruction.opcode==27,"ordinary GoHere owns actual source27 motion suitable for CanShoo");
    auto token=f.route.prepare_source_shoo(f.shooer());auto call=f.destination();
    require(token&&f.route.commit_source_shoo(*token,call)&&f.vm.run(64)==odh::VmStatus::yielded,"ordinary GoHere accepts source direct shoo child");
    const auto stack=f.capture();const auto world=f.world.snapshot();const auto vm=f.vm.snapshot();Fixture restored(false,false,false,true);
    require(restored.world.restore_snapshot(world)&&restored.vm.restore_snapshot(vm)&&restored.route.restore_session_route_stack(stack,owner),"ordinary GoHere shoo return restores via existing typed stack state");
    for(std::uint64_t tick=1;tick<180&&(f.route.active()||restored.route.active());++tick){if(f.route.active())f.tick(tick);if(restored.route.active())restored.tick(tick);
        require(f.world.find(7)->position==restored.world.find(7)->position&&f.vm.status()==restored.vm.status(),"ordinary GoHere direct shoo return resumes same smooth motion in restored graph");}
    require(!f.route.active()&&!restored.route.active()&&f.vm.status()==odh::VmStatus::returned_true&&f.world.find(7)->position->x==96&&
        f.profiles>0&&restored.profiles>0,"ordinary GoHere ignores false shoo return then reaches original exact goal");
}

void nested_restore(){
    Fixture f(true);f.start();auto token=f.route.prepare_source_shoo(f.shooer());auto call=f.destination();
    require(token&&f.route.commit_source_shoo(*token,call)&&f.vm.run(8)==odh::VmStatus::waiting,"shoo child enters actual nested Route45");
    auto stack=f.capture();require(stack.jobs.size()==2&&stack.jobs.front().external_frame_purpose==odh::RouteExternalFramePurpose::shoo,"capture nested typed shoo and child route");
    const auto world=f.world.snapshot();const auto vm=f.vm.snapshot();Fixture restored(true,false);
    require(restored.world.restore_snapshot(world)&&restored.vm.restore_snapshot(vm),"restore independent World and VM graphs");
    std::string why;require(restored.route.restore_session_route_stack(stack,owner,&why),why.c_str());
    auto re=restored.capture();require(re.jobs.size()==2&&re.jobs.front().external_frame_callee_generation==stack.jobs.front().external_frame_callee_generation,"restored typed Destination generation survives");
    for(std::uint64_t tick=1;tick<180&&(f.route.active()||restored.route.active());++tick){if(f.route.active())f.tick(tick);if(restored.route.active())restored.tick(tick);
        require(f.world.find(7)->position==restored.world.find(7)->position&&f.vm.status()==restored.vm.status()&&f.vm.temps()==restored.vm.temps(),"fresh graph follows same nested shoo movement and authored branches");}
    require(!f.route.active()&&!restored.route.active()&&f.vm.status()==odh::VmStatus::returned_true&&restored.vm.status()==odh::VmStatus::returned_true,"nested shoo false return is ignored and original destination completes");
}
}
int main(){try{token_contract();stale_contract();wait_and_timeout_contract();ordinary_go_here_direct_shoo();nested_restore();std::cout<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
