#include "odh/create_object_instance.hpp"

#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void placement_plan_matrix(){
    using Status=odh::CreateObjectPlacementStatus;
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,88,1})&&world.create(2,odh::EntityKind::object,{0,0,1})&&
        world.create(3,odh::EntityKind::object)&&world.create(4,odh::EntityKind::object),"create independent placement anchors");
    require(world.set_position(3,{192,208,1,6})&&world.set_position(4,{224,240,1,4}),"position parameter and local reference objects");
    odh::VmFrame frame;frame.context={1,1,2,0};frame.arguments={3};frame.locals={4,2};
    const auto offset=[](odh::EntityPosition point,int direction){
        if(direction==0)point.y-=16;if(direction==2)point.x+=16;
        if(direction==4)point.y+=16;if(direction==6)point.x-=16;return point;
    };
    for(std::uint8_t facing=0;facing<8;++facing){
        require(world.set_position(1,{64,80,1,facing})&&world.set_position(2,{112,128,1,facing})&&
            world.declare_object_word(1,1,facing),"declare source caller Direction word independently of position references");
        for(std::uint8_t mode=0;mode<=9;++mode){
            const odh::CreateObjectInstanceRequest request{0x7c4,mode,0,static_cast<std::uint8_t>(mode==9?1:0),0};
            const auto before=world.snapshot();const auto plan=odh::capture_create_object_placement(world,frame,request);
            require(plan.status==Status::resolved&&plan.position==request.position()&&
                plan.caller==odh::CreateObjectPlacementIdentity{1,world.find(1)->generation},"all ten placement modes resolve exact caller identity");
            std::optional<odh::EntityPosition> expected;
            if(mode==0||mode==1||mode==5||mode==9)expected=*world.find(1)->position;
            if(mode==3)expected=*world.find(2)->position;
            if(mode==7)expected=*world.find(3)->position;
            if(mode==8)expected=*world.find(4)->position;
            std::uint8_t direction=mode==6?0:mode==7?6:mode==8?4:facing;
            if(mode==0||mode==3)expected=offset(*expected,facing);
            if(mode==9){const auto relative=(2+(facing/2)*2)%8;expected=offset(*expected,relative);direction=relative;}
            if(expected)expected->direction=direction;
            require(plan.destination==expected&&plan.direction==direction&&plan.retry_underneath==(mode==5),
                "source mode matrix preserves cardinal offsets diagonal no-offset and exact captured direction");
            const auto reference=mode==6?0:mode==3||mode==4?2:mode==7?3:mode==8?4:1;
            require((reference==0?!plan.reference:plan.reference==std::optional<odh::CreateObjectPlacementIdentity>{{
                static_cast<std::int16_t>(reference),world.find(static_cast<std::int16_t>(reference))->generation}})&&
                ((mode==2||mode==4)?plan.container==plan.reference&&plan.container_slot==0:!plan.container&&!plan.container_slot),
                "reference and container identities remain independent of initial OUT_OF_WORLD construction");
            auto flags=request;flags.flags=255;
            const auto with_flags=odh::capture_create_object_placement(world,frame,flags);
            require(with_flags.status==Status::resolved&&with_flags.destination==plan.destination&&with_flags.direction==plan.direction,
                "placement capture does not invent semantics for FaceStackObjectDirection or other independent creation options");
            const auto after=world.snapshot();
            require(before.generation_sequence==after.generation_sequence&&before.spatial_revision==after.spatial_revision&&
                before.word_count==after.word_count&&frame.arguments==std::vector<std::int16_t>{3}&&frame.locals==std::vector<std::int16_t>{4,2},
                "shared placement capture never consumes IDs changes World or rewrites frame references");
        }
    }
    odh::CreateObjectInstanceRequest request{0x7c4,9,1,1,0};
    for(const auto base:{0,1,2,3,4,5,6,7})for(const auto local:{-32768,-9,-8,-7,-2,-1,0,1,2,3,4,5,6,7,8,32767}){
        require(world.set_position(1,{64,80,1,static_cast<std::uint8_t>(base)})&&world.declare_object_word(1,1,base),
            "prepare signed relative-direction source word");
        frame.locals[1]=static_cast<std::int16_t>(local);
        const auto plan=odh::capture_create_object_placement(world,frame,request);
        const auto raw=static_cast<std::uint16_t>(local+(base/2)*2);
        const auto signed_sum=raw<32768?static_cast<int>(raw):static_cast<int>(raw)-65536;
        const auto relative=signed_sum%8;auto expected=offset(*world.find(1)->position,relative);
        expected.direction=relative==0||relative==2||relative==4||relative==6?static_cast<std::uint8_t>(relative):0;
        require(plan.status==Status::resolved&&plan.destination==expected&&plan.direction==expected.direction,
            "mode9 wraps short addition and preserves C# negative remainder and odd-direction NORTH fallback");
    }
    request.raw_position=0;
    require(world.set_position(1,{32767,80,1,2}),"source short-edge anchor");
    auto plan=odh::capture_create_object_placement(world,frame,request);
    require(plan.status==Status::resolved&&plan.destination&&plan.destination->x==-32753,
        "front offset wraps source short coordinates instead of inventing a clamped valid point");
    require(world.set_position(1,{0,80,1,6}),"source west-edge anchor");
    plan=odh::capture_create_object_placement(world,frame,request);
    require(plan.destination&&plan.destination->x==-16,"known outside-lot candidate remains a placement rejection for the host");
    auto broken=frame;broken.context.caller=99;
    require(odh::capture_create_object_placement(world,broken,request).status==Status::invalid_caller,"missing Caller is unknown execution identity");
    broken=frame;broken.context.stack_object=99;request.raw_position=3;
    require(odh::capture_create_object_placement(world,broken,request).status==Status::invalid_reference,"missing stack reference never becomes OUT_OF_WORLD");
    broken=frame;broken.arguments.clear();request.raw_position=7;
    require(odh::capture_create_object_placement(world,broken,request).status==Status::invalid_frame,"mode7 requires actual Param0");
    broken=frame;broken.arguments[0]=-1;
    require(odh::capture_create_object_placement(world,broken,request).status==Status::invalid_reference,"negative Param0 object ID is not an absent lot position");
    request.raw_position=8;request.local_to_use=255;
    require(odh::capture_create_object_placement(world,frame,request).status==Status::invalid_frame,"mode8 rejects missing local rather than selecting local0");
    request.raw_position=9;
    require(odh::capture_create_object_placement(world,frame,request).status==Status::invalid_frame,"mode9 rejects missing relative local");
    request.local_to_use=1;world.declare_object_word(1,1,9);
    require(odh::capture_create_object_placement(world,frame,request).status==Status::unknown_direction,"mode9 uses validated World Direction variable rather than guessing from unrelated coordinates");
    request.raw_position=10;
    require(odh::capture_create_object_placement(world,frame,request).status==Status::unsupported_position,"unknown placement mode remains explicit");
    request.raw_position=1;
    require(!world.set_position(1,{32768,80,1,0}),
        "World rejects coordinates outside source signed-short domain before placement capture");
    require(world.set_position(1,{64,80,128,0})&&
        odh::capture_create_object_placement(world,frame,request).status==Status::invalid_position,
        "native story outside source signed-byte domain cannot silently truncate during placement capture");
    odh::World absent;
    require(absent.create(1,odh::EntityKind::object)&&absent.declare_object_word(1,1,0),"known OUT_OF_WORLD anchor retains direction property");
    frame.context={1,1,1,0};request={0x7c4,1,1,0,0};
    plan=odh::capture_create_object_placement(absent,frame,request);
    require(plan.status==Status::resolved&&!plan.destination&&plan.direction==0&&
        odh::create_object_instance_has_duplicate(absent,request,plan)==false,
        "known copied OUT_OF_WORLD position differs from an unresolved reference and has no duplicate bucket");
    request.raw_position=0;plan=odh::capture_create_object_placement(absent,frame,request);
    require(plan.status==Status::resolved&&plan.destination&&plan.destination->x==-32768&&plan.destination->y==32752,
        "front of OUT_OF_WORLD follows source sentinel arithmetic rather than assuming destination absence");
    odh::World duplicate;
    require(duplicate.create_runtime_object(1,0x7c4,{}, {})&&duplicate.set_position(1,{4,4,1,0}),"ordinary duplicate integration source");
    frame.context={1,1,1,0};request={0x7c4,1,1,0,0};
    plan=odh::capture_create_object_placement(duplicate,frame,request);
    require(odh::create_object_instance_has_duplicate(duplicate,request,plan)==true,"NoDuplicate consumes the captured plan destination before construction");
    request.raw_position=0;duplicate.set_position(1,{4,4,1,0});plan=odh::capture_create_object_placement(duplicate,frame,request);
    require(plan.destination&&plan.destination->y==-12&&odh::create_object_instance_has_duplicate(duplicate,request,plan)==false,
        "negative source candidate uses signed source tile shift rather than truncating into positive tile0");
    plan.status=Status::invalid_reference;
    require(!odh::create_object_instance_has_duplicate(duplicate,request,plan),"unresolved placement cannot prove NoDuplicate absence");
}

}

int main() {
    try {
        placement_plan_matrix();
        odh::Instruction fridge{};
        fridge.opcode = 42;
        fridge.operands = {117, 227, 1, 243, 4, 1, 0, 0};
        const auto food = odh::decode_create_object_instance(fridge);
        require(food.guid == 0xf301e375 && food.has_known_position() &&
                food.position() == odh::CreateObjectPosition::in_slot0_of_stack_object,
                "Have A Snack decodes the Food GUID and slot-0 position");
        require(food.flags == 1 && food.no_duplicate() && !food.pass_object_ids() &&
                !food.use_neighbor() && !food.fail_if_non_empty() && !food.pass_temp0() &&
                !food.face_stack_object_direction() && !food.return_immediately() &&
                !food.persist_in_database() && food.local_to_use == 0 &&
                food.interaction_callback == 0,
                "Have A Snack decodes only the NoDuplicate option");

        fridge.operands[5] = 0xff;
        const auto all_flags = odh::decode_create_object_instance(fridge);
        require(all_flags.no_duplicate() && all_flags.pass_object_ids() &&
                all_flags.use_neighbor() && all_flags.fail_if_non_empty() &&
                all_flags.pass_temp0() && all_flags.face_stack_object_direction() &&
                all_flags.return_immediately() && all_flags.persist_in_database(),
                "all eight option bits have distinct meanings");

        fridge.operands[4] = 255;
        const auto unknown_position = odh::decode_create_object_instance(fridge);
        require(!unknown_position.has_known_position() && unknown_position.raw_position == 255,
                "unknown position values are retained for an explicit unsupported result");

        fridge.opcode = 41;
        bool rejected = false;
        try { (void)odh::decode_create_object_instance(fridge); }
        catch (const odh::FormatError&) { rejected = true; }
        require(rejected, "other opcodes cannot be decoded as CreateObjectInstance");
        {
            odh::World world;
            require(world.create(1,odh::EntityKind::avatar,{0,88,3,16})&&
                world.set_position(1,{40,56,1,6})&&world.create(2,odh::EntityKind::object,{0,0,1})&&
                world.set_position(2,{80,80,1,2})&&world.create_runtime_object(3,food.guid,{},{}),
                "initialized hand object with distinct Caller and StackObject fixture");
            auto hand=food;hand.raw_position=2;
            const auto generation=world.snapshot().generation_sequence;
            require(odh::create_object_instance_has_duplicate(world,hand,odh::EntityPosition{40,56,1,0})==false,
                "NoDuplicate is explicitly bypassed for InMyHand");
            const auto placed=odh::place_created_object_in_hand(world,3,1,hand,2);
            require(placed.status==odh::CreateObjectInstanceStatus::created&&placed.object_id==3&&
                world.find(1)->slots[0]==3&&world.find(2)->slots[0]==0&&
                world.find(3)->attachment->container==1&&world.find(3)->attachment->slot==0&&
                world.find(3)->position==std::optional<odh::EntityPosition>{{40,56,1,2}}&&
                world.snapshot().generation_sequence==generation,
                "post-Init hand placement attaches Caller slot0 and preserves captured direction without new allocation");
            require(world.create_runtime_object(4,food.guid,{},{}),"second initialized object");
            const auto before=world.snapshot();
            require(odh::place_created_object_in_hand(world,4,1,hand,0).status==odh::CreateObjectInstanceStatus::slot_occupied&&
                !world.find(4)->attachment&&!world.find(4)->position&&world.snapshot().spatial_revision==before.spatial_revision,
                "occupied hand returns false placement before mutating initialized object or container");
            require(world.create(5,odh::EntityKind::avatar,{0,88,3,16}),"unpositioned avatar");
            require(odh::place_created_object_in_hand(world,4,5,hand,7).status==odh::CreateObjectInstanceStatus::created&&
                world.find(4)->attachment->container==5&&!world.find(4)->position&&world.find(4)->object_words.at(1)==7,
                "out-of-world Caller can own an out-of-world hand item with captured direction");
            require(world.create_runtime_object(6,food.guid,{},{}),"unsupported options fixture");
            for(unsigned flags=2;flags<=128;flags*=2){hand.flags=static_cast<std::uint8_t>(flags);
                require(odh::place_created_object_in_hand(world,6,1,hand,0).status==odh::CreateObjectInstanceStatus::unsupported_flags&&
                    !world.find(6)->attachment&&!world.find(6)->position,
                    "unported creation flag effects reject before hand placement");}
        }
        {
            odh::World world;auto request=food;request.raw_position=0;
            require(world.create_runtime_object(1,food.guid,{},{})&&world.set_position(1,{34,49,1,3}),
                "same-tile duplicate fixture");
            const auto before=world.snapshot();
            require(!odh::create_object_instance_has_duplicate(world,request,std::optional<odh::EntityPosition>{}).has_value(),
                "same-tile duplicate query needs a known destination");
            require(odh::create_object_instance_has_duplicate(world,request,odh::EntityPosition{47,63,0,7})==false,
                "out-of-world destination never has registered RoomInfo duplicates");
            require(odh::create_object_instance_has_duplicate(world,request,odh::EntityPosition{47,63,1,7})==true&&
                odh::create_object_instance_has_duplicate(world,request,odh::EntityPosition{48,63,1,7})==false&&
                odh::create_object_instance_has_duplicate(world,request,odh::EntityPosition{47,63,2,7})==false,
                "NoDuplicate compares exact GUID within tile and floor, ignoring subtiles and direction");
            require(world.set_ui_ghost(1,true)&&
                odh::create_object_instance_has_duplicate(world,request,odh::EntityPosition{47,63,1,0})==false,
                "UI ghost is absent from source position query");
            require(world.create_runtime_object(2,food.guid,{},{})&&
                odh::create_object_instance_has_duplicate(world,request,odh::EntityPosition{47,63,1,0})==false,
                "out-of-world objects are not registered in source tile query");
            request.raw_position=4;
            require(odh::create_object_instance_has_duplicate(world,request,odh::EntityPosition{34,49,1,0})==false,
                "NoDuplicate also bypasses StackObject slot placement");
            request.raw_position=0;
            require(world.create(3,odh::EntityKind::object)&&world.set_position(3,{36,50,1,0})&&
                !odh::create_object_instance_has_duplicate(world,request,odh::EntityPosition{34,49,1,0}),
                "unknown same-tile resource identity cannot silently prove duplicate absence");
            const auto end=world.snapshot();
            const auto cursor=end.generation_sequence;const auto revision=end.spatial_revision;
            (void)odh::create_object_instance_has_duplicate(world,request,odh::EntityPosition{34,49,1,0});
            require(world.snapshot().generation_sequence==cursor&&world.snapshot().spatial_revision==revision&&
                world.snapshot().word_count==end.word_count&&before.generation_sequence==1,
                "duplicate admission consumes no entity IDs or World effects");
        }
        std::cout << checks << " CreateObjectInstance operand checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CreateObjectInstance test failed: " << error.what() << '\n';
        return 1;
    }
}
