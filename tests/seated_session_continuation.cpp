#include "odh/seated_session_continuation.hpp"
#include "odh/single_resident_session_restore.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation,const char* message){
    try{operation();}catch(const odh::FormatError&){return;}
    throw std::runtime_error(message);
}
odh::RoutineSet action(std::uint16_t id){
    return {{odh::routine_key(id),odh::Bhav{0x8002,{{2,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
}
odh::RoutineSet main_program(){
    return {{odh::routine_key(4096),odh::Bhav{0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
}
odh::RoutineSet animation_main_program(){
    return {{odh::routine_key(4096),odh::Bhav{0x8002,
        {{44,254,255,{7,0,0,0,1,0,0,0}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
}
odh::RoutineSet named_animation_main_program(){
    return {{odh::routine_key(4096),odh::Bhav{0x8002,
        {{28,254,255,{0,0,0,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
}
constexpr odh::CodeOwner sit_owner=0x80000000U;
odh::RoutineSet sit_owner_bank(std::uint16_t sit_action){
    auto bank=action(sit_action);
    bank.emplace(odh::routine_key(8228),odh::Bhav{0x8002,
        {{44,254,255,{7,0,0,0,1,0,0,0}}},{},odh::Bhav::Metadata{0,4,4,0}});
    return bank;
}
struct Fixture {
    odh::World world;
    odh::EntityThreadControllers controllers{world,1};
    odh::Vm vm{odh::RoutineSet{},world.vm_memory(),{},controllers.sleep_primitive(),{{1,main_program()}}};
    odh::EntityVmScheduler scheduler{4};
    odh::LotContext lot{7,std::vector<std::int16_t>(32)};
    odh::Ts1BootstrapGlobals bootstrap;
    odh::DefinitionCandidate definition{};
    odh::SessionBundle bundle;
    std::optional<odh::SessionLinkedSemiglobal> linked;
    std::uint16_t sit_action=4097,stand_action=4102;
    odh::RoutineSet linked_owner;
    Fixture(std::optional<odh::SessionLinkedSemiglobal> link={},std::uint16_t sit=4097,
            std::uint16_t stand=4102):linked(std::move(link)),sit_action(sit),stand_action(stand){
        require(world.create(1,odh::EntityKind::avatar,{0,88,0})&&world.define_type(1,123),"actor");
        require(world.create(2,odh::EntityKind::object,{0,0,1})&&world.define_type(2,456),"chair");
        std::vector<odh::EntryPoint> eps(28);eps[27]={0,stand_action};
        require(world.define_entry_points(2,eps)&&world.set_position(1,{16,16,1,0})&&world.set_position(2,{32,48,1,0})&&
            world.attach(2,0,1)&&world.place_in_slot(2,0,1)&&world.set_person_data(1,0,1),"seated chair state");
        require(vm.start(odh::routine_key(4096),{1,1,1,1},{6})==odh::VmStatus::running&&controllers.bind(vm),"start Main");
        require(scheduler.add(1,vm,[&](std::uint64_t now){return controllers.advance(now);},
            [&]{return controllers.observe();},[&]{return controllers.notify();}),"scheduler");
        require(scheduler.tick(1,8,8)&&scheduler.tick(2,8,8),"Main stable wait");
        bundle.host.neighborhood_mode=1;bundle.host.house_number=7;bundle.host.family_chunk_id=21;
        bundle.host.family_number=12;bundle.host.resident_guid=123;bundle.host.runtime_avatar_id=1;bundle.host.tick=2;
        for(std::int16_t index=0;index<38;++index){
            auto value=index==9?std::optional<std::int16_t>{21}:bootstrap.read(index);
            if(!value)value=lot.read(index);
            if(value)bundle.host.global_values.push_back({static_cast<std::uint16_t>(index),*value});
        }
        bundle.resources.resource_fingerprint.fill(1);bundle.world=world.snapshot();bundle.scheduler=scheduler.snapshot();
        bundle.runtime_entities={{1,vm.snapshot(),controllers.snapshot(),{}}};
        auto& queue=bundle.runtime_entities[0].controllers.interactions;queue.sequence=1;queue.completion=odh::InteractionCompletion{1,1,1,true};
        odh::PlaybackResourceKey carry{};carry.source=1;carry.id=7;carry.caller=1;carry.callee=2;carry.stack_object=2;carry.code_owner=0x80000000U;
        bundle.runtime_entities[0].playback.carry=carry;
        odh::SessionSeatedContinuation c;c.actor_id=1;c.target_id=2;c.actor_generation=world.find(1)->generation;
        c.target_generation=world.find(2)->generation;c.target_guid=456;c.completion=*queue.completion;
        c.source="synthetic/exact.far";c.asset="chair.iff";c.definition_chunk_id=5;c.tree_table_id=8;
        c.action=sit_action;c.stand_action=stand_action;c.linked_semiglobal=linked;
        if(linked){linked_owner=action(sit_action);const auto stand=action(stand_action);
            linked_owner.insert(stand.begin(),stand.end());
            c.sit_routine_fingerprint=odh::session_action_routine_fingerprint(linked_owner);
            c.stand_routine_fingerprint=c.sit_routine_fingerprint;
        }else{
            c.sit_routine_fingerprint=odh::session_action_routine_fingerprint(action(sit_action));
            c.stand_routine_fingerprint=odh::session_action_routine_fingerprint(action(stand_action));
        }
        c.code_owners={{0x80000000U,c.source,c.asset,sit_action,c.sit_routine_fingerprint}};
        bundle.seated_continuation=c;
        definition.source=c.source;definition.asset=c.asset;definition.chunk_id=5;
        definition.definition.guid=456;definition.definition.tree_table_id=8;
    }
    auto resolve(const odh::SessionBundle& value){return odh::resolve_seated_session_owners(value,definition,
        *world.find(2)->entry_points,[&](const auto&,auto id){
            if(linked)return odh::SessionActionRoutineSource{linked_owner,linked};
            return odh::SessionActionRoutineSource{action(id),{}};});}
};
odh::SessionBundle animation_main_fixture(Fixture& source) {
    odh::World world;require(world.restore_snapshot(source.bundle.world),"animation fixture World");
    odh::EntityThreadControllers controllers(world,1);
    auto clip=std::make_shared<odh::AnimationHeader>();clip->name="synthetic-seated-Main-idle";
    odh::BoneMotion motion{};motion.frames=3;motion.time_properties={{{0,{{"xevt","100"}}}}};
    clip->motions.push_back(motion);
    odh::Playback playback(world,[&](const auto&,auto,auto){return clip;});
    odh::Vm vm(odh::RoutineSet{},world.vm_memory(),{},playback.primitive(),{{1,animation_main_program()}});
    require(vm.start(odh::routine_key(4096),{1,1,1,1})==odh::VmStatus::running&&
        controllers.bind(vm)&&playback.bind(vm),"bind seated Main animation");
    require(vm.run(8)==odh::VmStatus::waiting,"Main animation reaches pending opcode 44");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1,vm,[&](std::uint64_t tick){return controllers.advance(tick);},
        [&]{return controllers.observe();},[&]{return controllers.notify();})&&
        scheduler.tick(1,8,8)&&scheduler.tick(2,8,8),"animation Main scheduler boundary");
    auto bundle=source.bundle;odh::PlaybackSnapshot snapshot;
    require(playback.capture_snapshot(snapshot)&&snapshot.state&&snapshot.state->wait,"capture Main-owned animation wait");
    bundle.runtime_entities={{1,vm.snapshot(),controllers.snapshot(),snapshot}};
    auto& queue=bundle.runtime_entities[0].controllers.interactions;
    queue.sequence=1;queue.completion=odh::InteractionCompletion{1,1,1,true};
    bundle.scheduler=scheduler.snapshot();bundle.seated_continuation->code_owners.clear();
    return bundle;
}
odh::SessionBundle named_owner_animation_fixture(Fixture& source){
    odh::World world;require(world.restore_snapshot(source.bundle.world),"named-owner fixture World");
    odh::EntityThreadControllers controllers(world,1);
    auto clip=std::make_shared<odh::AnimationHeader>();clip->name="synthetic-seated-object-Main";
    odh::BoneMotion motion{};motion.frames=3;motion.time_properties={{{0,{{"xevt","100"}}}}};
    clip->motions.push_back(motion);
    odh::Playback playback(world,[&](const auto&,auto,auto){return clip;});
    const auto bank=sit_owner_bank(source.sit_action);
    const odh::VmPrimitive playback_primitive=playback.primitive();
    const odh::VmPrimitive dispatch=[&](const odh::VmFrame& frame,const odh::Instruction& instruction,
        const odh::Ts1Temps& temps,const odh::VmDispatch& vm_dispatch){
        if(instruction.opcode==28){
            const odh::VmContext context{1,1,2,sit_owner};
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::call,{}, {},
                odh::VmCall{odh::routine_key(8228),context,{}}};
        }
        return playback_primitive(frame,instruction,temps,vm_dispatch);
    };
    odh::Vm vm(odh::RoutineSet{},world.vm_memory(),{},dispatch,
        {{1,named_animation_main_program()},{sit_owner,bank}});
    require(vm.start(odh::routine_key(4096),{1,1,1,1})==odh::VmStatus::running&&
        controllers.bind(vm)&&playback.bind(vm),"bind Main to exact Sit owner bank");
    require(vm.run(8)==odh::VmStatus::waiting&&vm.stack().size()==2&&
        vm.stack().back().key.id==8228&&vm.stack().back().key.scope==odh::RoutineScope::semiglobal&&vm.stack().back().context.code_owner==sit_owner,
        "Main enters the saved object's named BHAV and waits at opcode 44");
    odh::EntityVmScheduler scheduler(4);
    require(scheduler.add(1,vm,[&](std::uint64_t tick){return controllers.advance(tick);},
        [&]{return controllers.observe();},[&]{return controllers.notify();})&&
        scheduler.tick(1,8,8)&&scheduler.tick(2,8,8),"named-owner Main reaches scheduler save point");
    odh::PlaybackSnapshot playback_snapshot;
    require(playback.capture_snapshot(playback_snapshot)&&playback_snapshot.state&&playback_snapshot.state->wait,
        "capture object-owned Main animation wait");
    auto bundle=source.bundle;
    bundle.runtime_entities={{1,vm.snapshot(),controllers.snapshot(),playback_snapshot}};
    auto& queue=bundle.runtime_entities[0].controllers.interactions;
    queue.sequence=1;queue.completion=odh::InteractionCompletion{1,1,1,true};
    bundle.scheduler=scheduler.snapshot();
    const auto fingerprint=odh::session_action_routine_fingerprint(bank);
    bundle.seated_continuation->sit_routine_fingerprint=fingerprint;
    bundle.seated_continuation->code_owners={{sit_owner,source.definition.source,source.definition.asset,
        source.sit_action,fingerprint}};
    return bundle;
}
}
void loaded_bank_fingerprint_cases() {
    const std::array<std::uint8_t,32> legacy_golden{{
        0x9d,0x70,0x25,0xa0,0x63,0xa6,0x41,0x78,0xcf,0x7b,0x18,0xcc,0xc5,0xba,0xbe,0xae,0xd0,0x13,0xb1,0xd7,0xe7,0xd8,0xf4,0x84,0xa5,0x7c,0x9a,0xd0,0x1b,0x0d,0x11,0x2a}};
    require(odh::session_action_routine_fingerprint(action(4097))==legacy_golden,
        "legacy strict closure fingerprint bytes changed");
    auto bank=action(4097);bank.begin()->second.instructions[0].opcode=8217;
    rejects([&]{odh::session_action_routine_fingerprint(bank);},
        "legacy closure fingerprint accepted unresolved call after generic API addition");
    const auto exact=odh::session_loaded_bank_fingerprint(bank);
    require(exact==odh::session_loaded_bank_fingerprint(bank),"loaded bank digest is not deterministic");
    auto changed=bank;changed.begin()->second.instructions[0].operands[7]=1;
    require(odh::session_loaded_bank_fingerprint(changed)!=exact,"loaded bank digest missed operand change");
    changed=bank;changed.begin()->second.instructions[0].opcode=8218;
    require(odh::session_loaded_bank_fingerprint(changed)!=exact,"loaded bank digest missed unresolved target change");
    changed=bank;changed.begin()->second.header[7]=1;
    require(odh::session_loaded_bank_fingerprint(changed)!=exact,"loaded bank digest missed raw header change");
    changed=bank;changed.begin()->second.metadata->locals++;
    require(odh::session_loaded_bank_fingerprint(changed)!=exact,"loaded bank digest missed interpreted metadata change");
    changed=bank;changed.emplace(odh::routine_key(8217),action(8217).begin()->second);
    require(odh::session_loaded_bank_fingerprint(changed)!=exact,"loaded bank digest missed added target routine");
    require(odh::session_loaded_bank_fingerprint(action(4097))!=legacy_golden,
        "loaded bank and strict closure digest domains collide");
    for(const auto version:{0x8000,0x8001}){
        changed=bank;changed.begin()->second.version=static_cast<std::uint16_t>(version);
        changed.begin()->second.metadata.reset();
        require(odh::session_loaded_bank_fingerprint(changed)!=exact,
            "loaded bank legacy BHAV format is not identified");
    }
    require(odh::session_loaded_bank_fingerprint({})!=exact,"empty installed bank identity is not distinct");
    rejects([&]{odh::session_loaded_bank_fingerprint({{{odh::RoutineScope::private_object,8217},bank.begin()->second}});},
        "loaded bank scope and routine ID mismatch accepted");
    rejects([&]{odh::session_loaded_bank_fingerprint({{odh::routine_key(1),bank.begin()->second}});},
        "global routine accepted into resource owner bank");
    changed=bank;changed.begin()->second.metadata.reset();
    require(odh::session_loaded_bank_fingerprint(changed)!=exact,"raw optional metadata presence omitted from identity");
    changed=bank;changed.begin()->second.version=0x8003;
    require(odh::session_loaded_bank_fingerprint(changed)!=exact,"unused raw BHAV version omitted from identity");
    changed=bank;changed.begin()->second.instructions[0].on_true=1;
    require(odh::session_loaded_bank_fingerprint(changed)!=exact,"unused branch bytes omitted from identity");
    changed=bank;changed.begin()->second.instructions.resize(32769,bank.begin()->second.instructions[0]);
    require(odh::session_loaded_bank_fingerprint(changed)!=exact,"native loaded bank inherited seated instruction limit");
    changed.clear();for(std::uint16_t id=4096;id<4096+769;++id)changed.emplace(odh::routine_key(id),bank.begin()->second);
    require(odh::session_loaded_bank_fingerprint(changed)!=exact,"native loaded bank inherited seated routine limit");
    for(const auto version:{0x8000,0x8001}){
        const odh::RoutineSet legacy{{odh::routine_key(4097),odh::Bhav{static_cast<std::uint16_t>(version),
            {{2,254,255,{0,0,5,0,0,5,8,7}}},{},std::nullopt}}};
        odh::Vm native(odh::RoutineSet{},{},{},{},{{0x80000000U,legacy}});
        require(native.start(odh::routine_key(4097),{1,1,1,0x80000000U})==odh::VmStatus::running&&
            native.run(1)==odh::VmStatus::returned_true&&native.temps()[0]==5,
            "native VM does not execute metadata-absent legacy BHAV fixture");
        require(odh::session_loaded_bank_fingerprint(legacy)!=exact,
            "native metadata-absent executable bank fingerprint rejected");
    }
}

int main(){try{
    loaded_bank_fingerprint_cases();
    Fixture f;const auto bytes=odh::encode_session_bundle(f.bundle);const auto loaded=odh::decode_session_bundle(bytes,f.bundle.resources);
    require(odh::encode_session_bundle(loaded)==bytes&&f.resolve(loaded).count(0x80000000U),"canonical seated owner roundtrip");
    auto unverified_carry=f.bundle;
    unverified_carry.runtime_entities[0].playback.carry->code_owner++;
    rejects([&]{odh::encode_session_bundle(unverified_carry);},
        "SEAT dispatch exempted an undeclared carry owner from strict provenance validation");
    auto mixed_dyno=f.bundle;
    odh::SessionDynamicActionOwner unrelated;unrelated.owner=0x80000001U;
    unrelated.target_id=2;unrelated.target_generation=f.world.find(2)->generation;
    unrelated.target_guid=456;unrelated.source=f.definition.source;unrelated.asset=f.definition.asset;
    unrelated.definition_chunk_id=f.definition.chunk_id;unrelated.tree_table_id=8;unrelated.root_routine=4097;
    unrelated.routine_fingerprint=f.bundle.seated_continuation->sit_routine_fingerprint;
    mixed_dyno.runtime_entities[0].dynamic_action_owners.push_back(unrelated);
    rejects([&]{odh::encode_session_bundle(mixed_dyno);},
        "SEAT dispatch bypassed unrelated DYNO owner validation");

    auto legacy=odh::decode_session_archive(bytes);
    auto legacy_seat=std::find_if(legacy.sections.begin(),legacy.sections.end(),[](const auto& s){
        return s.id==std::array<char,4>{{'S','E','A','T'}};});
    require(legacy_seat!=legacy.sections.end(),"legacy SEAT section exists");
    const auto link_flag=std::size_t{2+20+10+25+2+f.bundle.seated_continuation->source.size()+
        2+f.bundle.seated_continuation->asset.size()+10};
    require(link_flag<legacy_seat->data.size()&&legacy_seat->data[link_flag]==0,
        "private v2 profile has an empty link flag");
    legacy_seat->data.erase(legacy_seat->data.begin()+static_cast<std::ptrdiff_t>(link_flag));
    legacy_seat->data[0]=1;legacy_seat->data[1]=0;
    const auto legacy_loaded=odh::decode_session_bundle(odh::encode_session_archive(legacy),f.bundle.resources);
    require(legacy_loaded.seated_continuation&&!legacy_loaded.seated_continuation->linked_semiglobal&&
        f.resolve(legacy_loaded).count(0x80000000U),"version 1 private seated profile remains readable");
    Fixture linked(odh::SessionLinkedSemiglobal{"SofaGlobals","GameData/Global/Global.far","SofaGlobals.iff"},8213,8216);
    const auto linked_bytes=odh::encode_session_bundle(linked.bundle);
    const auto linked_loaded=odh::decode_session_bundle(linked_bytes,linked.bundle.resources);
    require(linked_loaded.seated_continuation->linked_semiglobal==linked.linked&&
        linked.resolve(linked_loaded).count(0x80000000U),"linked semiglobal provenance roundtrip and owner resolution");
    std::size_t rejections=0;
    const auto bad_link=[&](auto mutate,const char* message){auto changed=linked.bundle;mutate(changed);
        rejects([&]{odh::validate_seated_session_continuation(changed);},message);++rejections;};
    const auto bad=[&](auto mutate){auto changed=f.bundle;mutate(changed);rejects([&]{
        odh::validate_seated_session_continuation(changed);},"invalid seated continuation accepted");++rejections;};
    bad([](auto& b){++b.seated_continuation->actor_generation;});
    bad([](auto& b){++b.seated_continuation->target_generation;});
    bad([](auto& b){++b.seated_continuation->target_guid;});
    bad([](auto& b){++b.seated_continuation->target_slot;});
    bad([](auto& b){b.world.entities[1].slots[0]=0;});
    bad([](auto& b){b.world.entities[0].attachment->positioned=false;});
    bad([](auto& b){b.world.entities[0].person_data[0]=0;});
    bad([](auto& b){b.seated_continuation->completion.result=false;});
    bad([](auto& b){++b.seated_continuation->completion.uid;});
    bad([](auto& b){b.seated_continuation->completion.tick=b.host.tick;});
    bad([](auto& b){b.seated_continuation->action=4098;});
    bad([](auto& b){b.seated_continuation->stand_action=4103;});
    bad([](auto& b){b.world.entities[1].entry_points->at(27).action=4103;});
    bad([](auto& b){b.seated_continuation->code_owners.clear();});
    bad([](auto& b){b.seated_continuation->code_owners[0].owner=1;});
    bad([](auto& b){b.seated_continuation->code_owners[0].asset="other.iff";});
    bad([](auto& b){b.seated_continuation->code_owners[0].root_routine=4102;});
    bad([](auto& b){b.seated_continuation->source=std::string(4097,'x');});
    bad([](auto& b){b.seated_continuation->source.push_back('\0');});
    bad_link([](auto& b){b.seated_continuation->linked_semiglobal->name="Other";},"changed semiglobal link accepted");
    bad_link([](auto& b){b.seated_continuation->linked_semiglobal->archive="../Global.far";},"escaping semiglobal archive accepted");
    bad_link([](auto& b){b.seated_continuation->linked_semiglobal->archive="C:/outside/Global.far";},"drive-qualified semiglobal archive accepted");
    bad_link([](auto& b){b.seated_continuation->linked_semiglobal->archive="GameData/Global/";},"trailing-slash semiglobal archive accepted");
    bad_link([](auto& b){b.seated_continuation->linked_semiglobal->entry="other.iff";},"mismatched semiglobal entry accepted");
    bad([](auto& b){b.runtime_entities[0].controllers.interactions.active=odh::InteractionActiveSnapshot{1,1};});
    bad([](auto& b){b.runtime_entities[0].controllers.interactions.resume_tick=3;});
    bad([](auto& b){b.runtime_entities[0].vm.stack[0].context.code_owner=0x80000000U;});
    bad([](auto& b){b.runtime_entities[0].vm.pending->instruction.instruction.opcode=44;});
    auto wrong=f.definition;wrong.asset="other.iff";
    rejects([&]{odh::resolve_seated_session_owners(f.bundle,wrong,*f.world.find(2)->entry_points,
        [](const auto&,auto id){return odh::SessionActionRoutineSource{action(id),{}};});},"wrong exact asset accepted");
    wrong=f.definition;++wrong.chunk_id;
    rejects([&]{odh::resolve_seated_session_owners(f.bundle,wrong,*f.world.find(2)->entry_points,
        [](const auto&,auto id){return odh::SessionActionRoutineSource{action(id),{}};});},"wrong definition chunk accepted");
    rejects([&]{odh::resolve_seated_session_owners(f.bundle,f.definition,*f.world.find(2)->entry_points,
        [](const auto&,auto id){auto source=odh::SessionActionRoutineSource{action(id),{}};source.routines.begin()->second.instructions[0].operands[0]=1;return source;});},
        "changed private closure accepted with same source/asset/owner and overall manifest");
    rejects([&]{odh::resolve_seated_session_owners(f.bundle,f.definition,*f.world.find(2)->entry_points,
        [](const auto&,auto id){auto source=odh::SessionActionRoutineSource{action(id),{}};if(id==4102)source.routines.begin()->second.header[0]=1;return source;});},
        "changed Stand closure accepted with unchanged Sit closure");
    rejects([&]{odh::resolve_seated_session_owners(linked.bundle,linked.definition,*linked.world.find(2)->entry_points,
        [&](const auto&,auto){return odh::SessionActionRoutineSource{linked.linked_owner,
            odh::SessionLinkedSemiglobal{"Other","GameData/Global/Global.far","Other.iff"}};});},
        "changed exact GLOB link accepted by the restore loader");
    ++rejections;
    rejects([&]{odh::resolve_seated_session_owners(linked.bundle,linked.definition,*linked.world.find(2)->entry_points,
        [&](const auto&,auto){return odh::SessionActionRoutineSource{linked.linked_owner,
            odh::SessionLinkedSemiglobal{"SofaGlobals","GameData/Objects/Objects.far","SofaGlobals.iff"}};});},
        "changed linked archive accepted by the restore loader");
    ++rejections;
    rejects([&]{odh::resolve_seated_session_owners(linked.bundle,linked.definition,*linked.world.find(2)->entry_points,
        [&](const auto&,auto){return odh::SessionActionRoutineSource{linked.linked_owner,
            odh::SessionLinkedSemiglobal{"SofaGlobals","GameData/Global/Global.far","Other.iff"}};});},
        "changed linked archive entry accepted by the restore loader");
    ++rejections;
    rejects([&]{auto changed=linked.linked_owner;changed.begin()->second.instructions[0].operands[0]=1;
        odh::resolve_seated_session_owners(linked.bundle,linked.definition,*linked.world.find(2)->entry_points,
            [&](const auto&,auto){return odh::SessionActionRoutineSource{changed,linked.linked};});},
        "changed semiglobal owner code accepted with unchanged archive provenance");
    ++rejections;
    rejects([&]{odh::session_action_routine_fingerprint({{odh::routine_key(8213),
        odh::Bhav{0x8002,{{8217,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}});},
        "unresolved semiglobal call accepted in seated owner bank");
    ++rejections;
    auto archive=odh::decode_session_archive(bytes);
    auto seat=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& s){return s.id==std::array<char,4>{{'S','E','A','T'}};});
    require(seat!=archive.sections.end(),"SEAT section exists");seat->data[0]=3;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"unknown SEAT payload version accepted");
    archive=odh::decode_session_archive(bytes);archive.sections.back().required=false;
    rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));},"optional SEAT section accepted");
    // The generic candidate can own seated World values, but its execution is
    // gated until exact closures have been verified and installed in this VM.
    auto baseline=f.bundle.world;baseline.entities[0].attachment.reset();baseline.entities[0].person_data[0]=0;
    baseline.entities[1].slots[0]=0;
    const auto restore_case=[&](bool verify,bool wrong_vm){
        auto candidate=odh::SingleResidentSessionRestore::prepare(bytes,f.bundle.resources,f.bundle.host,
            baseline,f.lot,f.bootstrap,21);
        odh::EntityThreadControllers controllers(candidate->world(),1);
        odh::Playback playback(candidate->world(),{});
        odh::Vm vm(odh::RoutineSet{},candidate->world().vm_memory(),{},controllers.sleep_primitive(),{{1,main_program()}});
        require(controllers.bind(vm)&&playback.bind(vm),"bind fresh seated execution");
        odh::EntityVmScheduler scheduler(4);
        require(scheduler.add(1,vm,[&](std::uint64_t now){return controllers.advance(now);},
            [&]{return controllers.observe();},[&]{return controllers.notify();}),"register fresh seated execution");
        if(verify)candidate->install_seated_continuation_owners(vm,f.definition,*f.world.find(2)->entry_points,
            [&](const auto&,auto id){return odh::SessionActionRoutineSource{action(id),{}};});
        odh::RandomState random{999};std::uint64_t tick=999;
        auto clip=std::make_shared<odh::AnimationHeader>();clip->name="synthetic-seated-carry";
        if(!verify||wrong_vm){
            odh::Vm other{odh::RoutineSet{}};
            rejects([&]{candidate->restore_execution(wrong_vm?other:vm,controllers,playback,scheduler,
                [&](const auto&){return clip;},random,tick);},"unverified or different VM accepted seated owners");
            require(tick==999&&random.seed()==999,"failed seated restore published host state");
        }else{
            candidate->restore_execution(vm,controllers,playback,scheduler,[&](const auto&){return clip;},random,tick);
            require(tick==2&&vm.status()==odh::VmStatus::waiting&&playback.carry_clip()==clip&&
                candidate->world().find(1)->attachment->container==2,"verified seated execution restored");
            require(scheduler.tick(3,8,8),"seated Main continued at following logical tick");
        }
    };
    restore_case(false,false);restore_case(true,true);restore_case(true,false);
    const auto animation=animation_main_fixture(f);
    const auto animation_bytes=odh::encode_session_bundle(animation);
    const auto animation_loaded=odh::decode_session_bundle(animation_bytes,f.bundle.resources);
    require(odh::encode_session_bundle(animation_loaded)==animation_bytes&&
        animation_loaded.runtime_entities[0].playback.state->wait&&f.resolve(animation_loaded).empty(),
        "completed seated Sit accepts a verifiable Main-owned animation wait");
    const auto bad_animation=[&](auto mutate){auto changed=animation;mutate(changed);
        rejects([&]{odh::validate_seated_session_continuation(changed);},"invalid Main animation wait accepted");++rejections;};
    bad_animation([](auto& b){b.runtime_entities[0].playback.state->resource.code_owner=0x80000000U;});
    bad_animation([](auto& b){b.runtime_entities[0].vm.pending->context.code_owner=0x80000000U;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->wait->ticket;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->wait->expected;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->wait->index;});
    bad_animation([](auto& b){b.runtime_entities[0].playback.state->wait->bank=odh::VmFrameBank::locals;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->resource.id;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->resource.callee;});
    bad_animation([](auto& b){++b.runtime_entities[0].playback.state->vm_generation;});
    bad_animation([](auto& b){b.runtime_entities[0].vm.pending->instruction.instruction.operands[5]=1;});
    bad_animation([](auto& b){b.runtime_entities[0].vm.pending.reset();});
    bad_animation([](auto& b){b.runtime_entities[0].playback.state->wait.reset();});
    bad_animation([](auto& b){b.runtime_entities[0].vm.suspended_pending.push_back(*b.runtime_entities[0].vm.pending);});
    auto animation_candidate=odh::SingleResidentSessionRestore::prepare(animation_bytes,f.bundle.resources,f.bundle.host,
        baseline,f.lot,f.bootstrap,21);
    odh::EntityThreadControllers animation_controllers(animation_candidate->world(),1);
    odh::Playback animation_playback(animation_candidate->world(),{});
    odh::Vm animation_vm(odh::RoutineSet{},animation_candidate->world().vm_memory(),{},animation_playback.primitive(),
        {{1,animation_main_program()}});
    require(animation_controllers.bind(animation_vm)&&animation_playback.bind(animation_vm),"bind fresh Main animation restore");
    odh::EntityVmScheduler animation_scheduler(4);
    require(animation_scheduler.add(1,animation_vm,[&](std::uint64_t now){return animation_controllers.advance(now);},
        [&]{return animation_controllers.observe();},[&]{return animation_controllers.notify();}),"register Main animation restore");
    animation_candidate->install_seated_continuation_owners(animation_vm,f.definition,*f.world.find(2)->entry_points,
        [&](const auto&,auto id){return odh::SessionActionRoutineSource{action(id),{}};});
    auto animation_clip=std::make_shared<odh::AnimationHeader>();animation_clip->name="synthetic-seated-Main-idle";
    odh::BoneMotion animation_motion{};animation_motion.frames=3;
    animation_motion.time_properties={{{0,{{"xevt","100"}}}}};animation_clip->motions.push_back(animation_motion);
    odh::RandomState animation_random{999};std::uint64_t animation_tick=999;
    animation_candidate->restore_execution(animation_vm,animation_controllers,animation_playback,animation_scheduler,
        [&](const auto&){return animation_clip;},animation_random,animation_tick);
    require(animation_tick==2&&animation_vm.status()==odh::VmStatus::waiting&&animation_playback.progress(),
        "Main-owned seated animation pending wait restores before execution");
    auto named=named_owner_animation_fixture(f);
    const auto named_bytes=odh::encode_session_bundle(named);
    const auto named_loaded=odh::decode_session_bundle(named_bytes,f.bundle.resources);
    require(odh::encode_session_bundle(named_loaded)==named_bytes&&
        named_loaded.runtime_entities[0].vm.stack.size()==2&&
        named_loaded.runtime_entities[0].vm.stack.back().key.id==8228&&
        named_loaded.runtime_entities[0].vm.stack.back().key.scope==odh::RoutineScope::semiglobal&&
        named_loaded.runtime_entities[0].playback.state->wait&&
        named_loaded.runtime_entities[0].vm.pending->context.code_owner==0x80000000U,
        "save and reopen preserves Main inside the exact object's animation BHAV");
    const auto named_bank=sit_owner_bank(f.sit_action);
    const auto load_named=[&](const auto&,auto id){
        if(id==f.sit_action)return odh::SessionActionRoutineSource{named_bank,{}};
        return odh::SessionActionRoutineSource{action(id),{}};
    };
    const auto named_owners=odh::resolve_seated_session_owners(named_loaded,f.definition,
        *f.world.find(2)->entry_points,load_named);
    require(named_owners.count(0x80000000U)&&
        named_owners.at(0x80000000U).count(odh::routine_key(8228)),
        "restore resolver installs the recorded Sit bank containing the named wait routine");
    const auto bad_named=[&](auto mutate,const char* message){auto changed=named_loaded;mutate(changed);
        rejects([&]{odh::validate_seated_session_continuation(changed);},message);++rejections;};
    bad_named([](auto& b){b.runtime_entities[0].vm.stack.back().context.code_owner=0x80000001U;},
        "unregistered owner frame accepted in seated Main");
    bad_named([](auto& b){b.runtime_entities[0].vm.stack.back().context.callee=3;},
        "foreign callee context accepted in seated Main");
    bad_named([](auto& b){b.runtime_entities[0].vm.stack.front().context.code_owner=0x80000000U;},
        "object owner accepted as the root Main frame");
    bad_named([](auto& b){b.runtime_entities[0].playback.state->resource.code_owner=1;},
        "animation resource from another code owner accepted");
    bad_named([](auto& b){b.runtime_entities[0].vm.pending->instruction.instruction.opcode=17;},
        "non-animation wait inside the Sit owner accepted");
    auto named_candidate=odh::SingleResidentSessionRestore::prepare(named_bytes,f.bundle.resources,f.bundle.host,
        baseline,f.lot,f.bootstrap,21);
    odh::EntityThreadControllers named_controllers(named_candidate->world(),1);
    auto named_clip=std::make_shared<odh::AnimationHeader>();named_clip->name="synthetic-seated-object-Main";
    odh::BoneMotion named_motion{};named_motion.frames=3;
    named_motion.time_properties={{{0,{{"xevt","100"}}}}};named_clip->motions.push_back(named_motion);
    odh::Playback named_playback(named_candidate->world(),[&](const auto&,auto,auto){return named_clip;});
    const odh::VmPrimitive named_playback_primitive=named_playback.primitive();
    const odh::VmPrimitive named_dispatch=[&](const odh::VmFrame& frame,const odh::Instruction& instruction,
        const odh::Ts1Temps& temps,const odh::VmDispatch& vm_dispatch){
        if(instruction.opcode==28)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::call,{}, {},
            odh::VmCall{odh::routine_key(8228),{1,1,2,0x80000000U},{}}};
        return named_playback_primitive(frame,instruction,temps,vm_dispatch);
    };
    odh::Vm named_vm(odh::RoutineSet{},named_candidate->world().vm_memory(),{},named_dispatch,
        {{1,named_animation_main_program()}});
    require(named_controllers.bind(named_vm)&&named_playback.bind(named_vm),"bind named-owner restore controllers");
    named_candidate->install_seated_continuation_owners(named_vm,f.definition,*f.world.find(2)->entry_points,load_named);
    odh::EntityVmScheduler named_scheduler(4);
    require(named_scheduler.add(1,named_vm,[&](std::uint64_t now){return named_controllers.advance(now);},
        [&]{return named_controllers.observe();},[&]{return named_controllers.notify();}),
        "register named-owner restored resident");
    odh::RandomState named_random{999};std::uint64_t named_tick=999;
    named_candidate->restore_execution(named_vm,named_controllers,named_playback,named_scheduler,
        [&](const auto&){return named_clip;},named_random,named_tick);
    require(named_tick==2&&named_vm.status()==odh::VmStatus::waiting&&
        named_vm.stack().back().context.code_owner==0x80000000U&&named_playback.progress(),
        "restore resumes the waiting BHAV from the exact Sit owner bank");
    std::cout<<"seated continuation: roundtrip, exact resource closure and "<<rejections<<" adversarial value cases passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
