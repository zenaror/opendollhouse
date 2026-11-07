#include "odh/ts1_placement.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
odh::SlotRecord route(int destination=-1){odh::SlotRecord r{};r.type=3;r.standing=1;r.snap_target_slot=destination;r.facing=-2;r.resolution=16;r.proximity=std::array<std::int32_t,3>{0,0,0};return r;}
struct Fixture {
    odh::World world;odh::Ts1PlacementServices services;std::shared_ptr<odh::SlotTable> slots=std::make_shared<odh::SlotTable>();
    std::vector<odh::EntityPosition> attempts;std::vector<std::uint8_t> flags;std::vector<std::uint64_t> random_bounds;
    unsigned accept_at=1,lookups=0,scans=0,transfers=0,shoo=0;bool missing=false,unknown=false,empty=false,occupied=false;
    bool no_goals=true,global=false;int target_slot=-1;std::optional<odh::PrimitiveOutcome> outcome;
    Fixture(){
        require(world.create(1,odh::EntityKind::avatar,{0,88,3,16})&&world.create(2,odh::EntityKind::object,{0,0,2})&&
            world.create(3,odh::EntityKind::object)&&world.create(4,odh::EntityKind::object),"placement fixture identities");
        require(world.set_position(1,{104,104,1,2})&&world.set_position(2,{120,120,1,0})&&world.set_position(3,{144,104,1,0})&&
            world.set_position(4,{88,88,1,0})&&world.declare_object_word(1,52,0)&&world.declare_object_word(1,54,0),"placement fixture positions/result bank");
        slots->version=10;slots->records={route(),route(1)};
        services.slots=[&](const auto& source,auto,bool requested){require(source.source_still_matches(),"resource lookup uses authentic dispatch");++lookups;global=requested;
            return odh::Ts1SlotLookup{unknown?odh::Ts1SlotLookupStatus::unsupported:missing?odh::Ts1SlotLookupStatus::missing:odh::Ts1SlotLookupStatus::available,slots,{}};};
        services.locations=[&](const auto& source,const auto& record,auto,auto){require(source.source_still_matches(),"parser uses authentic dispatch");++scans;target_slot=record.snap_target_slot;
            odh::Ts1PlacementLocations result;result.supported=!unknown;result.no_valid_goals=no_goals;
            if(!empty)result.goals.push_back({{120,104,1,0},1.57079637f,false,10,1,{}});return result;};
        services.transfer=[&](const auto& source,auto container,auto slot,auto item){require(source.source_still_matches(),"transfer uses original primitive");++transfers;
            const auto ok=world.move_to_slot(container.id,slot,item.id);return odh::Ts1PlacementResult{ok?odh::Ts1PlacementStatus::success:odh::Ts1PlacementStatus::blocked,-1,{},{}};};
        services.place=[&](const auto& source,auto object,auto point,auto requested){require(source.source_still_matches(),"floor uses original primitive");flags.push_back(requested);
            if(point)attempts.push_back(*point);else attempts.push_back({-32768,-32768,1,0});
            if(unknown)return odh::Ts1PlacementResult{odh::Ts1PlacementStatus::unsupported,-1,{},"fixture unknown"};
            if(attempts.size()!=accept_at)return odh::Ts1PlacementResult{odh::Ts1PlacementStatus::blocked,0,{},{}};
            const bool oow=!point||(point->x==-32768&&point->y==-32768);
            const bool ok=oow?world.place_out_of_world(object.id,point?point->direction:0):world.place_on_floor(object.id,*point);
            return odh::Ts1PlacementResult{ok?odh::Ts1PlacementStatus::success:odh::Ts1PlacementStatus::blocked,-1,{},{}};};
        services.tile_occupied=[&](const auto& source,const auto&)->std::optional<bool>{require(source.source_still_matches(),"occupancy uses actual source");return occupied;};
        services.lot_size=[](const auto&)->std::optional<std::pair<std::int16_t,std::int16_t>>{return std::pair<std::int16_t,std::int16_t>{8,10};};
        services.random=[&](const auto& source,std::uint64_t bound)->std::optional<std::uint64_t>{require(source.source_still_matches(),"RNG uses actual source");random_bounds.push_back(bound);return 0;};
        services.shoo=[&](const auto&,auto){++shoo;return true;};services.moved_self=[](const auto&,auto){return true;};
    }
    odh::VmStatus run(odh::Instruction instruction,odh::VmContext context={1,2,2,0},std::vector<std::int16_t> args={0,0,0,0},bool local=false){
        odh::Ts1Placement placement(world,services);auto handler=placement.primitive();
        auto prefix=odh::Instruction{2,1,255,{0,0,3,0,0,5,25,7}}; // Local0=literal3.
        odh::Bhav bhav{0x8002,local?std::vector<odh::Instruction>{prefix,instruction}:std::vector<odh::Instruction>{instruction},{},odh::Bhav::Metadata{0,4,4,0}};
        odh::Vm vm({{odh::routine_key(4096),bhav}},world.vm_memory(),{},[&](const auto& f,const auto& i,const auto& t,const auto& d){outcome=handler(f,i,t,d);return *outcome;});
        require(vm.start(odh::routine_key(4096),context,args,true)==odh::VmStatus::running,"start real placement VM");return vm.run(3);
    }
};
odh::Instruction snap(unsigned mode,unsigned index=0,unsigned flags=0){return {46,254,255,{static_cast<std::uint8_t>(index),static_cast<std::uint8_t>(index>>8),static_cast<std::uint8_t>(mode),static_cast<std::uint8_t>(mode>>8),static_cast<std::uint8_t>(flags),255,254,253}};}
odh::Instruction find(unsigned mode,unsigned flags=2,unsigned local=0){return {16,254,255,{static_cast<std::uint8_t>(mode),static_cast<std::uint8_t>(local),static_cast<std::uint8_t>(flags),255,254,253,252,251}};}
void snap_modes(){
    for(unsigned mode=0;mode<5;++mode){Fixture f;
        const auto index=mode==0?2:0;std::vector<std::int16_t> args{0,0,1,0};
        require(f.run(snap(mode,index,5),{1,2,2,0},args)==odh::VmStatus::returned_true,"all five Snap modes and ignored flags/padding execute");
        if(mode==1)require(f.transfers==1&&!f.lookups&&!f.scans&&f.world.find(1)->attachment->container==2,"BeContained uses physical slot0 without requesting SLOT");
        else if(mode==2)require(!f.lookups&&f.scans==1&&f.attempts.size()==1,"InFront uses one synthetic source SLOT");
        else require(f.lookups==1&&f.scans==1&&f.global==(mode==4),"SLOT scopes resolve exact literal/argument/global table");
        if(mode==0)require(f.target_slot==1&&f.transfers==1,"StackVariable uses Args[index], not index itself");
    }
    Fixture contained;contained.empty=true;
    require(contained.run(snap(3,1))==odh::VmStatus::returned_true&&contained.transfers==1&&contained.world.find(1)->attachment,
        "source SnapTargetSlot may contain even with no candidate locations");
    Fixture no_goals;no_goals.empty=true;
    require(no_goals.run(snap(3))==odh::VmStatus::returned_false&&no_goals.world.find(1)->object_words.at(52)==2&&
        no_goals.world.find(1)->object_words.at(54)==0,"no valid goals publishes source failure words");
    Fixture missing;missing.missing=true;require(missing.run(snap(4))==odh::VmStatus::returned_false&&!missing.scans,"known missing global SLOT is source false");
    Fixture unknown;unknown.unknown=true;require(unknown.run(snap(3))==odh::VmStatus::fault&&!unknown.attempts.size(),"unknown resource is explicit unsupported");
    Fixture absent;require(absent.run(snap(0,20))==odh::VmStatus::fault,"StackVariable malformed frame does not invent Args");
    Fixture invalid;require(invalid.run(snap(5))==odh::VmStatus::returned_false,"unknown Snap enum follows source null-slot false");
    Fixture blocked;blocked.accept_at=99;require(blocked.run(snap(3))==odh::VmStatus::returned_false&&blocked.world.find(1)->object_words.at(52)==2,
        "final out-of-bounds placement publishes source error");
    Fixture facing;facing.slots->records[0].flags=4096;
    require(facing.run(snap(3))==odh::VmStatus::returned_true&&facing.world.find(1)->radian_direction==1.57079637f,"SnapToDirection preserves continuous selected direction");
}
void lifecycle_radial_search(){
    odh::Ts1RadialPlacementServices service;
    bool active=true;unsigned calls=0,anchors=0;std::vector<odh::EntityPosition> points;
    service.source_still_matches=[&]{return active;};
    service.reference_position=[&]()->std::optional<odh::EntityPosition>{++anchors;return odh::EntityPosition{72,88,1,7};};
    service.place=[&](odh::EntityPosition point){++calls;points.push_back(point);
        return odh::Ts1PlacementResult{calls==5?odh::Ts1PlacementStatus::success:odh::Ts1PlacementStatus::blocked,7,{},{}};};
    auto result=odh::ts1_find_location_radial(service);
    require(result.status==odh::Ts1PlacementStatus::success&&calls==5&&anchors==2&&
        points[0]==odh::EntityPosition{72,88,1,0}&&points[3].direction==6&&
        points[4]==odh::EntityPosition{56,72,1,0},"lifecycle uses source radial center/cardinal/radius order without a VM dispatch");
    calls=0;points.clear();service.place=[&](odh::EntityPosition){++calls;return odh::Ts1PlacementResult{odh::Ts1PlacementStatus::blocked,10,{},{}};};
    require(odh::ts1_find_location_radial(service).status==odh::Ts1PlacementStatus::blocked&&calls==1444,
        "all blocked candidates exhaust the bounded complete radial search as a normal failure");
    calls=0;service.place=[&](odh::EntityPosition){++calls;return odh::Ts1PlacementResult{odh::Ts1PlacementStatus::unsupported,-1,{},"real callback gap"};};
    require(odh::ts1_find_location_radial(service).status==odh::Ts1PlacementStatus::unsupported&&calls==1,
        "unsupported is never swallowed as blocked");
    service.place=[&](odh::EntityPosition){++calls;active=false;return odh::Ts1PlacementResult{odh::Ts1PlacementStatus::success,-1,{},{}};};
    require(odh::ts1_find_location_radial(service).status==odh::Ts1PlacementStatus::unsupported&&!active,
        "source expiry after a physical effect fails explicitly without rollback");
}
void location_modes(){
    for(unsigned mode=0;mode<6;++mode){Fixture f;require(f.run(find(mode))==odh::VmStatus::returned_true,"all six FindLocationFor source modes execute");
        if(mode==1)require(!f.world.find(2)->position&&f.attempts.front().x==-32768,"mode1 performs actual OOW publication");
        else if(mode==2)require(f.attempts[0].x==104&&f.attempts[0].y==104&&f.flags[0]==0,"smoke uses Caller+Callee midpoint minus8 and default flags");
        else if(mode==3||mode==4)require(f.attempts[0].direction==(mode==3?2:4),"vector rotation follows reference object, lateral adds two directions");
        else if(mode==5)require(f.random_bounds==std::vector<std::uint64_t>{6,8}&&f.attempts[0].x==24&&f.attempts[0].y==24,"random uses actual width/height interior bounds in X/Y order");
    }
    Fixture local;require(local.run(find(0,7),{1,2,2,0},{0,0,0,0},true)==odh::VmStatus::returned_true&&
        local.attempts[0].x==144&&local.flags[0]==3,"Local reference and UserBuildableLimit share real placement path");
    Fixture order;order.accept_at=3;require(order.run(find(3))==odh::VmStatus::returned_true&&order.attempts.size()==3&&
        order.attempts[0]==order.attempts[1]&&order.attempts[1]==order.attempts[2],"vector loop preserves source duplicate i/2 attempts");
    Fixture exhausted;exhausted.accept_at=99999;require(exhausted.run(find(5))==odh::VmStatus::returned_false&&exhausted.attempts.size()==100&&
        exhausted.random_bounds.size()==200,"random exhausts100 attempts and exactly200 draws");
    Fixture deferred;deferred.occupied=true;require(deferred.run(find(0,0))==odh::VmStatus::returned_true&&deferred.attempts.size()==1,
        "preferNonEmpty source quirk defers occupied candidates then retries them in authored order");
    Fixture unknown;unknown.unknown=true;const auto before=unknown.world.find(2)->position;
    require(unknown.run(find(0))==odh::VmStatus::fault&&unknown.world.find(2)->position==before,"unknown placement is not normal rejection or fabricated point");
    for(unsigned direction=0;direction<8;++direction){Fixture oow;
        require(oow.world.place_out_of_world(1,static_cast<std::uint8_t>(direction)),"OOW reference source heading");
        require(oow.run(find(3))==odh::VmStatus::returned_true&&oow.attempts.front().direction==direction,
            "OOW reference Direction GetValue is a notch0..7, not a bitmask");}
    Fixture invalid;require(invalid.run(find(9))==odh::VmStatus::returned_false,"unknown FindLocationFor mode follows source false");
}
void contained_position_publication(){
    odh::World world;
    require(world.create(10,odh::EntityKind::object,{0,0,1})&&world.create(11,odh::EntityKind::object,{0,0,1})&&
        world.create(12,odh::EntityKind::object)&&world.set_position(10,{200,200,1,0})&&world.set_position(11,{100,100,1,2})&&
        world.set_position(12,{80,80,1,4})&&world.attach(10,0,11)&&world.attach(11,0,12),"legacy raw containment fixture");
    require(world.set_room_resolver([](const auto& p)->std::optional<std::uint16_t>{return p.x>=160?4:2;}),"bind real synthetic room partition");
    require(world.refresh_contained_position(11)&&world.find(11)->position->x==200&&world.find(11)->position->direction==2&&
        world.find(11)->object_words.at(29)==3&&world.find(11)->attachment->positioned&&world.find(12)->position->x==80,
        "one recursive refresh publishes ONLY the child, not its later descendant");
    require(world.refresh_contained_position(12)&&world.find(12)->position->x==200&&world.find(12)->position->direction==4,
        "next source recursion step updates grandchild preserving own heading");
    require(world.place_individual_on_floor(10,odh::EntityPosition{220,220,1,0},6)&&world.find(10)->position->x==220&&
        world.find(10)->position->direction==6&&world.find(11)->position->x==200&&world.find(12)->position->x==200,
        "individual placement publishes only root until explicit source child recursion");
    require(world.refresh_contained_position(11)&&world.find(11)->position->x==220&&world.find(12)->position->x==200&&
        world.refresh_contained_position(12)&&world.find(12)->position->x==220,"source recursion controls each contained publication");
    require(world.place_out_of_world(10)&&!world.find(10)->position&&!world.find(11)->position&&!world.find(12)->position&&
        world.find(11)->object_words.at(1)==2&&world.find(12)->object_words.at(1)==4&&world.find(12)->object_words.at(29)==-1,
        "OOW publication preserves descendant orientation and source room shadow");
    const auto snapshot=world.snapshot();odh::World fresh;
    require(fresh.restore_snapshot(snapshot)&&fresh.set_room_resolver([](const auto&)->std::optional<std::uint16_t>{return 4;})&&fresh.place_on_floor(10,{240,240,1,0})&&fresh.find(12)->position->x==240&&
        fresh.find(12)->position->direction==4,"fresh World restores OOW physical containment before returning to floor");
    odh::WorldLimits limits;limits.words=1;odh::World bounded(limits);
    require(bounded.create(1,odh::EntityKind::object,{0,0,1})&&bounded.create(2,odh::EntityKind::object)&&
        bounded.set_position(1,{200,200,1,0})&&bounded.set_position(2,{100,100,1,2})&&bounded.attach(1,0,2)&&
        bounded.set_room_resolver([](const auto&)->std::optional<std::uint16_t>{return 3;}),"tight publication budget");
    const auto before=bounded.snapshot();
    require(!bounded.refresh_contained_position(2)&&!bounded.place_out_of_world(1)&&bounded.snapshot().word_count==before.word_count&&
        bounded.find(2)->position->x==100&&!bounded.find(2)->attachment->positioned&&bounded.find(1)->position->x==200,
        "missing direction/room budget rejects refresh and OOW before coordinates or containment mutate");
}
void drops_and_oow(){
    Fixture f;require(f.world.move_to_slot(1,0,4),"put actual item in Caller hand");f.accept_at=3;
    require(f.run({5,254,255,{255,254,253,252,251,250,249,248}})==odh::VmStatus::returned_true&&f.attempts.size()==3&&
        f.attempts[0].x==120&&f.attempts[0].y==104&&f.attempts[1].x==120&&f.attempts[1].y==88&&
        f.attempts[2].x==120&&f.attempts[2].y==120&&!f.world.find(4)->attachment,"Drop uses all8-direction source near-first order, not FindLocation order");
    Fixture empty;require(empty.run({5,254,255,{}})==odh::VmStatus::returned_false&&!empty.attempts.size(),"empty hand is ordinary false");
    Fixture oow;require(oow.world.move_to_slot(2,0,4)&&oow.world.place_out_of_world(2)&&!oow.world.find(4)->position&&
        oow.world.find(4)->attachment&&oow.world.find(4)->attachment->container==2,"OOW moves physical descendants and preserves containment");
    require(oow.world.place_on_floor(2,{200,200,1,4})&&oow.world.find(4)->position&&oow.world.find(4)->position->x==200,
        "OOW descendants retain enough direction state to return to physical space");
    odh::Ts1Placement placement(f.world,f.services);odh::VmFrame frame;odh::Ts1Temps temps{};
    require(placement.primitive()(frame,snap(1),temps,{}).disposition==odh::PrimitiveDisposition::unsupported,"forged placement dispatch has no mutation authority");
}
}
int main(){try{lifecycle_radial_search();snap_modes();location_modes();contained_position_publication();drops_and_oow();std::cout<<"TS1 shared placement mode matrix passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
