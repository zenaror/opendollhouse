#include "odh/routing.hpp"
#include "odh/playback.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
std::uint32_t bits(float value){std::uint32_t result=0;std::memcpy(&result,&value,sizeof(value));return result;}
constexpr odh::CodeOwner owner=0x80000071U;
constexpr float pi=3.14159265358979323846f;
odh::RoutineSet program(std::uint8_t mode){
    return {{odh::routine_key(4100),{0x8002,{{22,1,255,{mode,93,8,7,6,5,4,3}},
        {2,254,255,{0,0,91,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,4,0}}},
        {odh::routine_key(4101),{0x8002,{{22,254,255,{mode,0,0,0,0,0,0,0}}},{},
            odh::Bhav::Metadata{0,4,4,0}}},
        {odh::routine_key(4102),{0x8002,{{27,254,255,{0,0,254,255,0,0,2,0}}},{},
            odh::Bhav::Metadata{0,4,4,0}}}};
}
struct Animation {
    bool ended=false,pool=false,unknown=false;
    std::uint8_t index=0;
    float speed=1;
    std::uint32_t frames=8;
    unsigned starts=0,resets=0;
};
struct Fixture {
    odh::World world;
    // Deliberately blocked: a turn does not ask for path clearance.
    odh::RouteGrid grid{1,1};odh::SlotTable slots;
    odh::Routing route{world,grid,slots};odh::Vm vm;
    Animation animation;
    odh::SourceLookTowardsProfile profile{};
    std::optional<std::vector<odh::SourceLookTowardsGroupMember>> group;
    unsigned geometry_calls=0;
    bool reject_start=false,reject_reset=false,mutate_actor=false;
    explicit Fixture(std::uint8_t mode=2,bool populate=true):
        vm({},world.vm_memory(),{},route.primitive(),{{owner,program(mode)}}){
        if(populate)require(world.create(7,odh::EntityKind::avatar,{0,88,3,16})&&
            world.create(9,odh::EntityKind::object)&&world.set_position(7,{32,32,1,0})&&
            world.set_position(9,{48,32,1,0}),"create caller and independently moving StackObject");
        grid.block({0,0});
        route.set_route_goal_validator([this](const auto&,auto){++geometry_calls;return false;});
        route.set_room_pair_provider([this](const auto&,const auto&)
            ->std::optional<std::pair<std::uint16_t,std::uint16_t>>{++geometry_calls;return {};});
        route.set_look_towards_group_resolver([this](const auto&){return group;});
        route.set_look_towards_services({
            [this](const auto&)->std::optional<odh::SourceLookTowardsProfile>{return profile;},
            [this](const auto&,auto,std::uint8_t index,bool pool,float speed)->std::optional<std::uint32_t>{
                if(reject_start)return {};
                ++animation.starts;animation.index=index;animation.pool=pool;animation.speed=speed;animation.ended=false;
                if(mutate_actor)world.set_radian_direction(7,0.31f);
                return animation.frames;
            },
            [this](const auto&,auto)->std::optional<bool>{if(animation.unknown)return {};return animation.ended;},
            [this](const auto&,auto,bool pool){if(reject_reset)return false;
                ++animation.resets;animation.index=3;animation.pool=pool;return true;}
        });
        require(route.bind(vm),"bind caller-specific routing VM");
    }
    void start(std::int16_t target=9){
        require(vm.start(odh::routine_key(4100),{7,7,target,owner})==odh::VmStatus::running&&
            vm.run(8)==odh::VmStatus::waiting,"LookTowards suspends the actual source instruction");
    }
    void finish(std::uint64_t tick){
        animation.ended=true;
        require(route.tick(tick)&&!route.active()&&vm.run(8)==odh::VmStatus::returned_true&&vm.temps()[0]==91,
            "source true branch runs only after turn clip ends");
    }
};
void check_and_geometry(){
    Fixture check;
    const auto source=check.world.snapshot();
    odh::VmFrame frame{odh::routine_key(4100),0,{7,7,9,owner},{},{}};
    const auto primitive=check.route.primitive();
    for(std::uint8_t mode=2;mode<=5;++mode){
        const odh::Instruction instruction{22,254,255,{mode,255,255,255,255,255,255,255}};
        require(primitive(frame,instruction,{},odh::VmDispatch{nullptr,1,true}).disposition==
            odh::PrimitiveDisposition::goto_false&&!check.route.active()&&!check.animation.starts,
            "body check refuses without bound-origin admission or host effects");
    }
    require(check.world.snapshot().entities.size()==source.entities.size()&&check.world.find(7)->radian_direction==0,
        "body check leaves World and animation untouched");
    const odh::Instruction camera{22,254,255,{1,0,0,0,0,0,0,0}};
    require(primitive(frame,camera,{},odh::VmDispatch{nullptr,1,true}).disposition==odh::PrimitiveDisposition::goto_true,
        "camera mode remains a source no-op even in checks");
    Fixture small;require(small.world.set_position(9,{33,16,1,0})&&small.world.set_static_footprint(7,false),
        "small angle starts with a dynamic caller");
    small.start();require(!small.world.find(7)->static_footprint,"request preserves original dynamic footprint");
    require(small.route.tick(1)&&!small.route.active()&&small.animation.starts==0&&small.animation.resets==0&&
        small.world.find(7)->static_footprint&&small.world.find(7)->person_data[37]==1&&small.geometry_calls==0,
        "small angle sets final facing and NORTH without path clearance or animation, then PreExit makes caller static");
    const auto expected=static_cast<float>(std::atan2(1.0,16.0));
    require(bits(small.world.find(7)->radian_direction)==bits(expected),"small angle preserves continuous atan2");
    Fixture self;self.start(7);require(self.route.tick(1)&&!self.route.active()&&
        bits(self.world.find(7)->radian_direction)==bits(0.0f),"self delta integer zero faces north");
    Fixture away(3);away.start(7);require(away.route.tick(1)&&away.animation.index==5&&away.route.active(),
        "away from self retains source float-pi normalization and signed turn choice");
    Fixture oow;require(oow.world.remove(9)&&oow.world.create(9,odh::EntityKind::object),"create live OOW target");
    oow.start();const auto frozen=oow.route.capture_local_session_route(owner);
    require(frozen&&frozen->look_towards&&bits(frozen->look_towards->target_radian_direction)==
        bits(static_cast<float>(std::atan2(-32800.0,32800.0))),"OOW target uses exact LotTilePos sentinel, not missing identity");
}
void angle_and_shared_animation(){
    struct Case {odh::EntityPosition target;std::uint8_t index;};
    for(const auto& test:std::vector<Case>{{{32,48,1,0},5},{{48,32,1,0},6},{{16,32,1,0},7},
            {{48,16,1,0},8},{{16,16,1,0},9}}){
        Fixture turn;require(turn.world.set_position(9,test.target),"set authored turn direction");
        turn.start();require(turn.route.tick(1)&&turn.animation.index==test.index&&turn.route.active()&&
            turn.world.find(7)->static_footprint&&turn.world.find(7)->person_data[37]==1&&turn.vm.temps()[0]==0,
            "source signed 180/90/45 selection waits on real animation and leaves footprint static");
        const auto saved=turn.route.capture_local_session_route(owner);
        require(saved&&saved->frame.state==odh::RouteFrameState::end_turn&&saved->look_towards&&
            saved->look_towards->animation_num_frames==8,"animated turn captures exact duration and END_TURN");
        const auto target=saved->look_towards->target_radian_direction;
        turn.profile.in_pool=true; // Source reset consults CURRENT pool family.
        turn.finish(2);require(turn.animation.index==3&&turn.animation.pool&&turn.animation.resets==1&&
            bits(turn.world.find(7)->radian_direction)==bits(target),"clip end installs current-pool standing loop and exact final facing");
    }
    Fixture tweak;require(tweak.world.set_position(9,{48,28,1,0}),"set non-notch residual turn");
    tweak.profile.walk_style=1;tweak.start();require(tweak.route.tick(1)&&tweak.animation.speed==2,
        "WalkStyle1 doubles native turn clip speed");
    auto saved=tweak.route.capture_local_session_route(owner);
    require(saved&&saved->look_towards->turn_tweak!=0&&bits(tweak.world.find(7)->radian_direction)==
        bits(saved->look_towards->turn_tweak),"first END_TURN tick applies source residual tweak exactly once");
    const auto prior=tweak.world.find(7)->radian_direction;
    require(tweak.route.tick(2)&&bits(tweak.world.find(7)->radian_direction)==
        bits(prior+saved->look_towards->turn_tweak),"each non-ended tick advances one tweak");
    // A child may replace the shared primary animation. EndReached of the new
    // state is authoritative; routing must not insist the original clip exists.
    tweak.animation.index=42;tweak.animation.ended=true;tweak.finish(3);
    Fixture pool;pool.profile={1,true};pool.start();require(pool.route.tick(1)&&pool.animation.pool&&pool.animation.speed==1,
        "pool ignores WalkStyle and uses swim family at normal speed");
}
void frozen_restore_and_tamper(){
    Fixture live;require(live.world.set_position(9,{48,28,1,0}),"prepare continuous source body target");
    live.start();require(live.route.tick(1),"begin real end-turn clip");
    require(live.world.remove(9),"source StackObject can leave World after its frozen turn result");
    const auto route=live.route.capture_local_session_route(owner);
    require(route&&route->target==7&&!route->arrival_radian_direction&&!route->target_anchor_radian_direction&&
        !route->source_circle&&route->look_towards,"body turn stores independent frozen result with actor semantic target");
    Fixture copy(2,false);require(copy.world.restore_snapshot(live.world.snapshot())&&copy.vm.restore_snapshot(live.vm.snapshot()),
        "restore independent World and VM before route publication");
    copy.animation=live.animation;
    require(copy.route.restore_local_session_route(*route,owner),"restore turn without rereading deleted StackObject or reloading old clip");
    Fixture invalid(2,false);require(invalid.world.restore_snapshot(live.world.snapshot())&&invalid.vm.restore_snapshot(live.vm.snapshot()),
        "prepare isolated invalid-restore graph");
    const auto rejected=[&](auto alter,const char* why){auto bad=*route;alter(bad);
        require(!invalid.route.restore_local_session_route(bad,owner)&&!invalid.route.active(),why);};
    rejected([](auto& value){value.ticket++;},"stale ticket cannot restore a body turn");
    rejected([](auto& value){value.actor_generation++;},"stale generation cannot restore a body turn");
    rejected([](auto& value){value.look_towards->mode=4;},"frozen mode must match pending opcode22 operands");
    rejected([](auto& value){value.look_towards->animation_index=4;},"clip index must match source signed turn choice");
    rejected([](auto& value){value.look_towards->turn_tweak+=0.1f;},"saved tweak must match duration and source residual");
    rejected([](auto& value){value.look_towards->target_radian_direction=std::numeric_limits<float>::quiet_NaN();},
        "non-finite frozen body angle is rejected");
    rejected([](auto& value){value.path.push_back({33,32});},"body turn cannot smuggle a walking path");
    rejected([](auto& value){value.frame.portal_turns=1;},"simple body snapshot cannot hide a portal allowance");
    require(!invalid.route.restore_local_session_route(*route),"dynamic owner still requires exact host resource authority");
    require(live.route.tick(2)&&copy.route.tick(2),"original and restored turn continue without source target");
    live.finish(3);copy.finish(3);
    require(bits(live.world.find(7)->radian_direction)==bits(copy.world.find(7)->radian_direction)&&
        live.world.find(7)->position==copy.world.find(7)->position&&live.vm.steps()==copy.vm.steps(),
        "fresh graph preserves continuous direction, final pose and authored VM work");
    Fixture unknown;unknown.start();unknown.animation.unknown=true;
    require(!unknown.route.tick(1)&&unknown.route.active()&&unknown.vm.status()==odh::VmStatus::waiting,
        "unknown shared animation state is explicit failure, never a fake authored completion");
    Fixture mutate;mutate.mutate_actor=true;mutate.start();
    require(!mutate.route.tick(1)&&mutate.route.active(),"callback actor mutation fails capability validation");
}
void complete_group_average(){
    for(const std::uint8_t mode:{std::uint8_t{4},std::uint8_t{5}}){
        Fixture average(mode);
        require(average.world.set_position(9,{30000,5,1,0})&&average.world.create(10,odh::EntityKind::object)&&
            average.world.set_position(10,{30001,4,1,0}),"prepare authoritative group whose short sum wraps");
        average.group=std::vector<odh::SourceLookTowardsGroupMember>{
            {9,average.world.find(9)->generation,*average.world.find(9)->position},
            {10,average.world.find(10)->generation,*average.world.find(10)->position}};
        average.start();const auto saved=average.route.capture_local_session_route(owner);
        // (short)(30000+30001) == -5535; -5535/2 truncates to -2767.
        auto angle=static_cast<float>(std::atan2(-2767.0-32,28.0));
        if(mode==5)angle=static_cast<float>(std::fmod(static_cast<double>(angle)+3.14159265358979323846,
            6.28318530717958647692));
        require(saved&&bits(saved->look_towards->target_radian_direction)==bits(angle),
            "complete group uses source short wrapping, signed truncation and away modulo");
    }
    Fixture missing(4);require(missing.vm.start(odh::routine_key(4100),{7,7,9,owner})==odh::VmStatus::running&&
        missing.vm.run(8)==odh::VmStatus::fault&&!missing.route.active(),"average cannot guess a single-part group");
    Fixture duplicate(4);duplicate.group=std::vector<odh::SourceLookTowardsGroupMember>(2,
        {9,duplicate.world.find(9)->generation,*duplicate.world.find(9)->position});
    require(duplicate.vm.start(odh::routine_key(4100),{7,7,9,owner})==odh::VmStatus::running&&
        duplicate.vm.run(8)==odh::VmStatus::fault&&!duplicate.route.active(),"duplicate group members are rejected atomically");
}
void nested_portal_allowance(){
    for(const bool consumed:{false,true}){
        Fixture nested;
        nested.grid=odh::RouteGrid{64,64}; // Parent27 needs a real walkable grid; child22 does not.
        nested.route.set_route_goal_validator([](const auto&,auto){return true;});
        nested.route.set_room_pair_provider([](const auto&,const auto&)
            ->std::optional<std::pair<std::uint16_t,std::uint16_t>>{return std::pair<std::uint16_t,std::uint16_t>{1,1};});
        require(nested.vm.start(odh::routine_key(4102),{7,7,7,owner})==odh::VmStatus::running&&
            nested.vm.run(8)==odh::VmStatus::waiting,"prepare route parent before portal child");
        const odh::VmCall child{odh::routine_key(4101),{7,7,9,owner},{}};
        require(nested.route.call_external_frame(child),"portal parent owns actual external child frame");
        if(consumed)require(nested.route.consume_portal_turn(),"consume portal parent's first turn allowance");
        require(nested.vm.run(8)==odh::VmStatus::waiting&&nested.route.tick(1),
            "portal child may request a real nested body-turn service");
        require(!nested.route.capture_local_session_route(owner),"suspended parent route remains explicit snapshot boundary");
        if(consumed){
            require(nested.animation.starts==0&&nested.vm.external_frame_return()&&
                bits(nested.world.find(7)->radian_direction)==bits(pi/2),
                "exhausted portal turn allowance applies facing without inventing another animation");
        }else{
            require(nested.animation.starts==1&&nested.route.active()&&nested.vm.status()==odh::VmStatus::waiting,
                "first nested portal turn genuinely waits on native animation");
            nested.animation.ended=true;require(nested.route.tick(2)&&nested.vm.external_frame_return(),
                "animated child return resumes portal ownership without branching the parent route");
        }
        const auto parent=nested.route.active_frame_context();
        require(parent&&parent->state==odh::RouteFrameState::room_portal&&parent->portal_turns==(consumed?2U:1U),
            "EndWalk consumes source allowance on parent context, including the denied attempt");
    }
}
void real_playback_integration();
}
int main(){try{check_and_geometry();angle_and_shared_animation();frozen_restore_and_tamper();complete_group_average();
    nested_portal_allowance();real_playback_integration();
    std::cout<<checks<<" LookTowards routing checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"LookTowards routing: "<<error.what()<<'\n';return 1;}}

namespace {
// Exercise the actual shared avatar animation controller rather than a timer
// substitute: Routing observes EndReached before VM dispatch, and Playback owns
// the only frame/event advance after that actor's VM slice.
void real_playback_integration(){
    struct Runtime {
        odh::World world;odh::RouteGrid grid{1,1};odh::SlotTable slots;
        std::shared_ptr<odh::AnimationHeader> ordinary=std::make_shared<odh::AnimationHeader>();
        std::shared_ptr<odh::AnimationHeader> turn=std::make_shared<odh::AnimationHeader>();
        std::shared_ptr<odh::AnimationHeader> standing=std::make_shared<odh::AnimationHeader>();
        odh::Routing route{world,grid,slots};odh::Playback playback;
        odh::Vm vm;bool swimming=false;std::int16_t style=0;
        unsigned route_resolutions=0,events=0;std::uint16_t selected=0;
        explicit Runtime(bool populate=true):
            playback(world,[this](const auto&,auto,auto){return ordinary;},{},{},
                [this](const auto&,std::uint16_t index,bool pool)->std::shared_ptr<const odh::AnimationHeader>{
                    ++route_resolutions;selected=index;
                    require(pool==swimming,"real route resolver receives the actor swim/walk family");
                    return index==3?standing:turn;
                }),
            vm({},world.vm_memory(),{},[this](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
                if(instruction.opcode==44)return playback.primitive()(frame,instruction,temps,dispatch);
                return route.primitive()(frame,instruction,temps,dispatch);
            },{{owner,program(2)}}){
            for(const auto& clip:{ordinary,turn,standing}){
                odh::BoneMotion motion{};motion.frames=7;clip->motions.push_back(motion);
            }
            ordinary->name="ordinary-and-carry";turn->name="real-route-turn";standing->name="real-route-reset";
            ordinary->motions[0].time_properties={{{0,{{"lefthand","2"},{"xevt","7"}}}}};
            turn->motions[0].time_properties={{{0,{{"righthand","1"},{"xevt","9"}}},
                                             {90,{{"lefthand","3"}}}}};
            if(populate)require(world.create(7,odh::EntityKind::avatar,{0,88,1})&&
                world.create(9,odh::EntityKind::object)&&world.set_position(7,{32,32,1,0})&&
                world.set_position(9,{48,28,1,0}),"real route World contains the exact actor and source target");
            require(route.bind(vm)&&playback.bind(vm),"real Routing and Playback bind one execution graph");
            route.set_look_towards_services({
                [this](const auto&)->std::optional<odh::SourceLookTowardsProfile>{return odh::SourceLookTowardsProfile{style,swimming};},
                [this](const auto& frame,auto ticket,std::uint8_t index,bool pool,float speed)->std::optional<std::uint32_t>{
                    if(!vm.pending()||vm.pending()->ticket!=ticket||
                       !playback.start_route_animation(frame,vm,world.find(7)->generation,
                           vm.run_generation(),index,pool,speed,false))return {};
                    const auto sample=playback.sample();return sample?std::optional<std::uint32_t>{sample->frames}:std::nullopt;
                },
                [this](const auto&,auto ticket)->std::optional<bool>{
                    const auto sample=playback.sample();
                    if(!vm.pending()||vm.pending()->ticket!=ticket||!sample)return {};
                    return sample->end;
                },
                [this](const auto& frame,auto ticket,bool pool){
                    return vm.pending()&&vm.pending()->ticket==ticket&&
                        playback.start_route_animation(frame,vm,world.find(7)->generation,
                            vm.run_generation(),3,pool,1,true);
                }});
        }
        void step(std::uint64_t tick){
            if(route.active())require(route.tick(tick),"real route service runs at the actor safe point");
            vm.run(8);
            require(playback.advance(tick,[this](const auto&){++events;}),
                "real animation advances once after actor VM dispatch");
        }
        odh::PlaybackSnapshot animation(){odh::PlaybackSnapshot result;std::string error;
            require(playback.capture_snapshot(result,&error),"capture real route animation continuation");return result;}
    };
    for(bool swimming:{false,true})for(std::int16_t style:{std::int16_t{0},std::int16_t{1}}){
        Runtime live;live.swimming=swimming;live.style=style;
        require(live.vm.start(odh::routine_key(4100),{7,7,9,owner})==odh::VmStatus::running,
            "real integration starts the actor VM");
        const auto frame=live.vm.stack().back();
        require(live.playback.primitive()(frame,{44,254,255,{1,0,0,0,1,1,0,0}}, {},{&live.vm,0}).disposition==
            odh::PrimitiveDisposition::goto_true&&live.playback.advance(0),"real primary commits its initial hand gesture");
        require(live.playback.primitive()(frame,{44,254,255,{2,0,0,0,1,16,0,0}}, {},{&live.vm,0}).disposition==
            odh::PrimitiveDisposition::goto_true,"real integration retains a custom carry clip");
        require(live.vm.run(8)==odh::VmStatus::waiting,"actual opcode22 owns the actor continuation");
        const auto steps=live.vm.steps();live.step(1);
        const auto source_animation=live.animation();
        const auto speed=!swimming&&style==1?2.0f:1.0f;
        require(live.vm.steps()==steps&&live.selected==8&&source_animation.state&&
            source_animation.state->resource.kind==odh::PlaybackResourceKey::Kind::route_walk&&
            source_animation.state->speed==speed&&source_animation.state->frame==speed&&
            source_animation.state->queue==std::deque<std::int16_t>{9}&&
            source_animation.hands->value.left==odh::HandGesture::fist&&
            source_animation.hands->value.right==odh::HandGesture::pointing&&live.playback.carry_clip()==live.ordinary,
            "real body turn replaces only primary state, uses source speed and preserves carry/left hand");
        require(live.world.remove(9),"real route freezes source target before it leaves World");
        const auto source_route=live.route.capture_local_session_route(owner);
        require(source_route&&source_route->look_towards&&source_route->frame.state==odh::RouteFrameState::end_turn&&
            source_route->look_towards->animation_num_frames==7,"real END_TURN stores resolved clip duration");
        Runtime copy(false);copy.swimming=swimming;copy.style=style;
        require(copy.world.restore_snapshot(live.world.snapshot())&&copy.vm.restore_snapshot(live.vm.snapshot()),
            "real integration restores World and VM in a fresh graph");
        require(copy.playback.restore_snapshot(source_animation,copy.vm,[&](const auto& key)->std::shared_ptr<const odh::AnimationHeader>{
            if(key.kind==odh::PlaybackResourceKey::Kind::route_walk)return key.id==3?copy.standing:copy.turn;
            return key.kind==odh::PlaybackResourceKey::Kind::animation?copy.ordinary:nullptr;
        })&&copy.route.restore_local_session_route(*source_route,owner),
            "real route and animation restore without dispatch, target lookup or restarting the clip");
        const auto initial_events=live.events;
        for(std::uint64_t tick=2;tick<=10;++tick){
            live.step(tick);copy.step(tick);
            const auto a=live.animation(),b=copy.animation();
            require(a.state&&b.state&&a.state->resource==b.state->resource&&a.state->frame==b.state->frame&&
                a.state->speed==b.state->speed&&a.state->end==b.state->end&&a.state->loop==b.state->loop&&
                a.state->low==b.state->low&&a.state->high==b.state->high&&a.state->queue==b.state->queue&&
                a.state->events_run==b.state->events_run&&a.state->ticks==b.state->ticks&&a.last_tick==b.last_tick&&
                a.hands->value.left==b.hands->value.left&&a.hands->value.right==b.hands->value.right&&
                bits(live.world.find(7)->radian_direction)==bits(copy.world.find(7)->radian_direction)&&
                live.world.find(7)->position==copy.world.find(7)->position&&live.vm.steps()==copy.vm.steps()&&
                live.vm.status()==copy.vm.status()&&live.route.active()==copy.route.active()&&
                live.events-initial_events==copy.events,"real resumed turn matches continuous frames/events/hands/angles/VM");
        }
        require(!live.route.active()&&live.vm.status()==odh::VmStatus::returned_true&&live.vm.temps()[0]==91&&
            live.selected==3&&live.playback.sample()->loop&&live.animation().state->speed==1&&
            live.world.find(7)->static_footprint&&live.playback.carry_clip()==live.ordinary&&
            live.events==2&&copy.events==1,"real clip completion resumes authored code, resets loop and never replays saved events");
    }
}
}
