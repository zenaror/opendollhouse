#include "odh/entry_point5_readonly.hpp"
#include "odh/world_snapshot_codec.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

odh::Bhav flamingo_like_bhav() {
    odh::Instruction type_test{};
    type_test.opcode = 32;
    type_test.on_true = 1;
    type_test.on_false = 0xff;
    type_test.operands = {0x56, 0x6b, 0x71, 0x43, 0, 0, 10, 0};

    odh::Instruction attribute_test{};
    attribute_test.opcode = 2;
    attribute_test.on_true = 0xfe;
    attribute_test.on_false = 0xff;
    attribute_test.operands = {16, 0, 1, 0, 0, 2, 1, 7};

    odh::Bhav bhav;
    bhav.version = 0x8002;
    bhav.metadata = odh::Bhav::Metadata{0, 0, 0, 2};
    bhav.instructions = {type_test, attribute_test};
    return bhav;
}

odh::Bhav urnstone_bhav() {
    odh::Instruction person_type{};
    person_type.opcode = 2; person_type.on_true = 1; person_type.on_false = 0xff;
    person_type.operands = {9, 0, 2, 0, 0, 2, 21, 7};
    odh::Instruction same_neighbor{};
    same_neighbor.opcode = 2; same_neighbor.on_true = 0xfe; same_neighbor.on_false = 0xff;
    same_neighbor.operands = {31, 0, 1, 0, 0, 2, 19, 0};
    odh::Bhav bhav;
    bhav.version = 0x8002;
    bhav.metadata = odh::Bhav::Metadata{0, 1, 0, 3};
    bhav.instructions = {person_type, same_neighbor};
    return bhav;
}

odh::Bhav dining_table_bhav() {
    odh::Instruction verify_person{};
    verify_person.opcode = 355; verify_person.on_true = 1; verify_person.on_false = 0xff;
    verify_person.operands.fill(0xff);
    odh::Instruction flag_two{};
    flag_two.opcode = 2; flag_two.on_true = 0xfe; flag_two.on_false = 2;
    flag_two.operands = {65, 0, 2, 0, 0, 8, 19, 7};
    odh::Instruction flag_three{};
    flag_three.opcode = 2; flag_three.on_true = 0xfe; flag_three.on_false = 0xff;
    flag_three.operands = {65, 0, 3, 0, 0, 8, 19, 7};
    odh::Bhav bhav;
    bhav.version = 0x8002;
    bhav.metadata = odh::Bhav::Metadata{0, 0, 0, 1};
    bhav.instructions = {verify_person, flag_two, flag_three};
    return bhav;
}

odh::Bhav global355_person_lookup() {
    odh::Instruction reject_zero_stack_object{};
    reject_zero_stack_object.opcode = 2;
    reject_zero_stack_object.on_true = 0xff;
    reject_zero_stack_object.on_false = 1;
    reject_zero_stack_object.operands = {0, 0, 0, 0, 0, 15, 10, 7};
    odh::Instruction save_original_stack_object{};
    save_original_stack_object.opcode = 2;
    save_original_stack_object.on_true = 2;
    save_original_stack_object.on_false = 0xfd;
    save_original_stack_object.operands = {0, 0, 0, 0, 0, 5, 25, 10};
    odh::Instruction reset_search_cursor{};
    reset_search_cursor.opcode = 2;
    reset_search_cursor.on_true = 3;
    reset_search_cursor.on_false = 0xfd;
    reset_search_cursor.operands = {0, 0, 0, 0, 0, 5, 10, 7};
    odh::Instruction next_person{};
    next_person.opcode = 31;
    next_person.on_true = 4;
    next_person.on_false = 0xff;
    next_person.operands = {0, 0, 0, 0, 129, 10, 0, 0};
    odh::Instruction compare_person_id{};
    compare_person_id.opcode = 2;
    compare_person_id.on_true = 0xfe;
    compare_person_id.on_false = 3;
    compare_person_id.operands = {0, 0, 0, 0, 0, 2, 25, 10};
    return {0x8002,
        {reject_zero_stack_object, save_original_stack_object, reset_search_cursor,
         next_person, compare_person_id}, {}, odh::Bhav::Metadata{0, 0, 1, 2}};
}
}

int main() {
    using namespace odh;
    constexpr std::uint32_t tested_guid = 0x43716b56;
    constexpr std::uint32_t other_guid = 0x36500000;
    auto bhav = flamingo_like_bhav();

    auto result = evaluate_read_only_entry_point5(4108, bhav, tested_guid, {}, 1);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_true && result.steps == 2,
            "matching object type and attribute 16 should return true");

    result = evaluate_read_only_entry_point5(4108, bhav, other_guid, {}, {});
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 1,
            "nonmatching type should short-circuit false without needing attribute 16");

    result = evaluate_read_only_entry_point5(4108, bhav, tested_guid, {}, 0);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 2,
            "matching type with attribute 16 clear should return false");

    result = evaluate_read_only_entry_point5(4108, bhav, tested_guid, {}, {});
    require(result.state == ReadOnlyEntryPoint5State::unsupported && result.steps == 0,
            "missing required attribute must not be inferred as zero");

    auto mutating = bhav;
    mutating.instructions[1].operands[5] = 5;
    result = evaluate_read_only_entry_point5(4108, mutating, tested_guid, {}, 1);
    require(result.state == ReadOnlyEntryPoint5State::unsupported && result.steps == 0,
            "expression assignment must fail the read-only allowlist");

    auto extended = bhav;
    extended.instructions.push_back(extended.instructions.back());
    result = evaluate_read_only_entry_point5(4108, extended, tested_guid, {}, 1);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "unexpected instructions must fail the fixed two-instruction shape");

    result = evaluate_read_only_entry_point5(4000, bhav, tested_guid, {}, 1);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "non-private routine IDs must fail the entry-point allowlist");

    const auto urn = urnstone_bhav();
    result = evaluate_read_only_entry_point5(4105, urn, 0, {}, {}, 2, 32, 30);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 2,
            "Legacy UrnStone allows only a Sim whose PersonData[31] matches its Attribute[1]");
    result = evaluate_read_only_entry_point5(4105, urn, 0, {}, {}, 2, 30, 30);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_true && result.steps == 2,
            "UrnStone rule returns true when the two saved neighbor values match");
    result = evaluate_read_only_entry_point5(4105, urn, 0, {}, {}, 3, {}, {});
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 1,
            "non-person objects short-circuit UrnStone's second read");
    result = evaluate_read_only_entry_point5(4105, urn, 0, {}, {}, 2, {}, 30);
    require(result.state == ReadOnlyEntryPoint5State::unsupported && result.steps == 1,
            "missing person data remains unknown rather than being guessed");
    result = evaluate_read_only_entry_point5(4105, urn, 0, {}, {}, {}, 32, 30);
    require(result.state == ReadOnlyEntryPoint5State::unsupported && result.steps == 0,
            "missing object definition remains unknown rather than being guessed");
    auto altered_urn = urn;
    altered_urn.instructions[1].operands[2] = 2;
    result = evaluate_read_only_entry_point5(4105, altered_urn, 0, {}, {}, 2, 32, 30);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "altered UrnStone behavior remains outside the exact allowlist");
    altered_urn = urn;
    altered_urn.metadata->tree_version++;
    result = evaluate_read_only_entry_point5(4105, altered_urn, 0, {}, {}, 2, 32, 30);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "unverified UrnStone tree versions remain outside the exact allowlist");

    const auto dining = dining_table_bhav();
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, true, 1);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 3,
            "DiningTables returns false for a live Sim without either tested flag");
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, true, 2);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_true,
            "DiningTables recognizes flag number 2 in PersonData[65]");
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, true, 4);
    require(result.state == ReadOnlyEntryPoint5State::evaluated_true,
            "DiningTables recognizes flag number 3 in PersonData[65]");
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, false, {});
    require(result.state == ReadOnlyEntryPoint5State::evaluated_false && result.steps == 1,
            "Global355 short-circuits when the stack object is not a live avatar");
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, true, {});
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "missing PersonData[65] remains unknown");
    result = evaluate_read_only_entry_point5(4122, dining, 0, {}, {}, {}, {}, {}, {}, 1);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "missing live-avatar enumeration remains unknown");
    auto changed_dining = dining;
    changed_dining.instructions[1].operands[2] = 4;
    result = evaluate_read_only_entry_point5(4122, changed_dining, 0, {}, {}, {}, {}, {}, true, 2);
    require(result.state == ReadOnlyEntryPoint5State::unsupported,
            "changed DiningTables logic remains outside the exact allowlist");

    World ep5_world;
    require(ep5_world.create(10, EntityKind::avatar, {2}) &&
            ep5_world.create(20, EntityKind::object, {2}) &&
            ep5_world.define_type(20, tested_guid), "generic EP5 caller and stack object");
    const VmFrame ep5_frame{routine_key(4096), 0, {10, 10, 20, 0}, {}, {}};
    require(ep5_world.write(ep5_frame, 0, 0, 44) && ep5_world.write(ep5_frame, 1, 0, 77),
            "generic EP5 caller and stack-object attributes");
    std::vector<EntryPoint> caller_ep5(27);caller_ep5[5].action=4108;
    require(ep5_world.define_type(10,0x1000000a)&&ep5_world.define_entry_points(10,caller_ep5),"bind actual private EP5 caller definition/table");
    const auto ep5_snapshot = ep5_world.snapshot();
    const auto ep5_before = encode_world_snapshot(ep5_snapshot);
    Instruction caller_attribute_test{};
    caller_attribute_test.opcode = 2; caller_attribute_test.on_true = 1; caller_attribute_test.on_false = 0xff;
    caller_attribute_test.operands = {0, 0, 44, 0, 0, 2, 0, 7};
    Instruction stack_attribute_test{};
    stack_attribute_test.opcode = 2; stack_attribute_test.on_true = 2; stack_attribute_test.on_false = 0xff;
    stack_attribute_test.operands = {0, 0, 77, 0, 0, 2, 1, 7};
    Instruction stack_id_argument_test{};
    stack_id_argument_test.opcode = 2; stack_id_argument_test.on_true = 0xfe; stack_id_argument_test.on_false = 0xff;
    stack_id_argument_test.operands = {0, 0, 20, 0, 0, 2, 9, 7};
    const auto stack_attribute = Bhav{0x8002,
        {caller_attribute_test, stack_attribute_test, stack_id_argument_test}, {}, Bhav::Metadata{0, 4, 0, 4}};
    RoutineSet ep5_owner{{routine_key(4108), stack_attribute}};
    auto generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 20, {}, 8);
    if(generic.state!=ReadOnlyBhavState::evaluated_true)
        std::cerr<<"generic EP5 state="<<static_cast<int>(generic.state)<<" reason="
                 <<static_cast<int>(generic.reason)<<" steps="<<generic.steps<<" diagnostic="
                 <<generic.diagnostic<<'\n';
    require(generic.state == ReadOnlyBhavState::evaluated_true && generic.steps == 6,
            "generic EP5 receives Caller, StackObject and the stack-object ID argument");
    require(encode_world_snapshot(ep5_snapshot) == ep5_before &&
            encode_world_snapshot(ep5_world.snapshot()) == ep5_before,
            "generic EP5 sandbox leaves saved and live World unchanged");

    World urn_world;
    require(urn_world.create(11, EntityKind::object, {2}) &&
            urn_world.create(16, EntityKind::avatar, {0, 32}) &&
            urn_world.declare_definition_word(16, 9, 2) &&
            urn_world.set_person_data(16, 31, 30),
            "generic UrnStone caller and avatar fixture");
    const VmFrame urn_frame{routine_key(4105),0,{11,11,16,1},{},{}};
    require(urn_world.write(urn_frame,0,1,30),"set UrnStone caller Attribute[1]");
    auto urn_ep5=caller_ep5;urn_ep5[5].action=4105;
    require(urn_world.define_type(11,0x1000000b)&&urn_world.define_entry_points(11,urn_ep5),"bind actual UrnStone EP5 definition/table");
    const auto urn_snapshot=urn_world.snapshot();
    const auto urn_before=encode_world_snapshot(urn_snapshot);
    const RoutineSet urn_owner{{routine_key(4105), urnstone_bhav()}};
    auto urn_generic=evaluate_read_only_entry_point5_bhav(urn_snapshot, {}, urn_owner,
        4105, 11, 16, {}, 8);
    require(urn_generic.state==ReadOnlyBhavState::evaluated_true && urn_generic.steps==4,
        "generic EP5 executes Legacy UrnStone using saved OBJD Type and avatar PersonData");
    require(encode_world_snapshot(urn_snapshot)==urn_before &&
            encode_world_snapshot(urn_world.snapshot())==urn_before,
        "generic UrnStone evaluation leaves saved and live World unchanged");

    Instruction ghost_test{};
    ghost_test.opcode = 2; ghost_test.on_true = 0xfe; ghost_test.on_false = 0xff;
    ghost_test.operands = {1, 0, 1, 0, 0, 2, 9, 7};
    auto ghost_bhav = Bhav{0x8002, {ghost_test}, {}, Bhav::Metadata{0, 4, 0, 4}};
    ep5_owner[routine_key(4108)] = ghost_bhav;
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 20, 1, 8);
    require(generic.state == ReadOnlyBhavState::evaluated_true,
            "generic EP5 receives the known ghost argument");
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 20, {}, 8);
    require(generic.state == ReadOnlyBhavState::unsupported &&
            generic.diagnostic.find("unknown ghost argument") != std::string::npos,
            "unknown ghost state cannot be guessed when the BHAV depends on it");
    ghost_test.operands[0] = 0;
    ghost_test.operands[2] = 1;
    ep5_owner[routine_key(4108)] = Bhav{0x8002, {ghost_test}, {}, Bhav::Metadata{0, 4, 0, 4}};
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 20, {}, 8);
    require(generic.state == ReadOnlyBhavState::evaluated_false,
            "unknown ghost state is accepted only when both possibilities agree");
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 99, {}, 8);
    require(generic.state == ReadOnlyBhavState::unsupported &&
            generic.reason == ReadOnlyBhavReason::invalid_snapshot,
            "missing EP5 stack object remains unsupported");
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4000, 10, 20, {}, 8);
    require(generic.state == ReadOnlyBhavState::unsupported &&
            generic.reason == ReadOnlyBhavReason::invalid_resources,
            "generic EP5 rejects a root that mismatches the Caller entry point");
    generic = evaluate_read_only_entry_point5_bhav(ep5_snapshot, {}, ep5_owner,
        4108, 10, 20, 2, 8);
    require(generic.state == ReadOnlyBhavState::unsupported &&
            generic.reason == ReadOnlyBhavReason::invalid_resources,
            "generic EP5 rejects invalid ghost flags");
    require(encode_world_snapshot(ep5_snapshot) == ep5_before &&
            encode_world_snapshot(ep5_world.snapshot()) == ep5_before,
            "all generic EP5 variants leave the live World unchanged");

    // Root ownership is a three-scope contract, independently of the numeric
    // private owner label used by the disposable VM. The selected Caller EP5
    // must name this exact root; a same-number routine in another bank is not
    // an implicit resource fallback.
    const auto with_ep5=[&](std::uint16_t action){auto snapshot=ep5_snapshot;
        for(auto& entity:snapshot.entities)if(entity.id==10)entity.entry_points->at(5).action=action;
        return snapshot;};
    Instruction type_only{32,0xfe,0xff,{0x56,0x6b,0x71,0x43,0,0,10,0}};
    const Bhav type_bhav{0x8002,{type_only},{},Bhav::Metadata{0,4,0,0}};
    auto semiglobal_snapshot=with_ep5(8206);
    const auto semi_before=encode_world_snapshot(semiglobal_snapshot);
    RoutineSet semiglobal_owner{{routine_key(8206),type_bhav}};
    for(const auto ghost:std::vector<std::optional<std::int16_t>>{std::nullopt,0,1}){
        const auto answer=evaluate_read_only_entry_point5_bhav(semiglobal_snapshot,{},semiglobal_owner,8206,10,20,ghost,16);
        require(answer.state==ReadOnlyBhavState::evaluated_true&&answer.steps==(ghost?1U:2U),
            "semiglobal-rooted8206 TestObjectType executes with exact Caller EP5 and isolated ghost variants");
    }
    auto negative=semiglobal_snapshot;
    for(auto& entity:negative.entities)if(entity.id==20)entity.definition->guid=other_guid;
    require(evaluate_read_only_entry_point5_bhav(negative,{},semiglobal_owner,8206,10,20,{},16).state==ReadOnlyBhavState::evaluated_false,
        "semiglobal TestObjectType false is a known answer rather than unsupported");
    auto missing_link=evaluate_read_only_entry_point5_bhav(semiglobal_snapshot,{}, {},8206,10,20,{},16);
    require(missing_link.state==ReadOnlyBhavState::unsupported&&missing_link.reason==ReadOnlyBhavReason::invalid_resources&&missing_link.steps==0,
        "missing linked bank/root is rejected before any execution");
    require(evaluate_read_only_entry_point5_bhav(ep5_snapshot,{},semiglobal_owner,8206,10,20,{},16).state==ReadOnlyBhavState::unsupported,
        "a supplied linked bank cannot override another Caller EP5 action");
    auto no_table=semiglobal_snapshot;
    for(auto& entity:no_table.entities)if(entity.id==10)entity.entry_points.reset();
    require(evaluate_read_only_entry_point5_bhav(no_table,{},semiglobal_owner,8206,10,20,{},16).state==ReadOnlyBhavState::unsupported,
        "missing Caller entry-point table stays unknown");
    // Semi -> private -> semi calls retain the same exact resource owner.
    const auto call=[](std::uint16_t action){return Bhav{0x8002,{{action,0xfe,0xff,{}}},{},Bhav::Metadata{0,4,0,0}};};
    RoutineSet linked_chain{{routine_key(8206),call(4109)},{routine_key(4109),call(8210)},{routine_key(8210),type_bhav}};
    require(evaluate_read_only_entry_point5_bhav(semiglobal_snapshot,{},linked_chain,8206,10,20,{},16).state==ReadOnlyBhavState::evaluated_true,
        "semiglobal EP5 can call its exact private and semiglobal helpers");
    const RoutineSet private_linked{{routine_key(4108),call(8206)},{routine_key(8206),type_bhav}};
    require(evaluate_read_only_entry_point5_bhav(ep5_snapshot,{},private_linked,4108,10,20,{},16).state==ReadOnlyBhavState::evaluated_true,
        "private-rooted EP5 retains its explicit linked semiglobal calls");
    linked_chain.erase(routine_key(8210));
    require(evaluate_read_only_entry_point5_bhav(semiglobal_snapshot,{},linked_chain,8206,10,20,{},16).state==ReadOnlyBhavState::unsupported,
        "missing nested linked helper cannot fall back to another owner");
    // Global EP5 roots are valid in VMEntity.GetRoutineWithOwner. Their root
    // belongs exclusively to the host's selected explicit Global bank.
    const auto global_snapshot=with_ep5(300);
    const RoutineSet global_root{{routine_key(300),type_bhav}};
    require(evaluate_read_only_entry_point5_bhav(global_snapshot,global_root,{},300,10,20,{},16).state==ReadOnlyBhavState::evaluated_true,
        "exact global EP5 root executes from the selected Global bank");
    const RoutineSet global_chain{{routine_key(300),call(4109)}};
    const RoutineSet global_descendants{{routine_key(4109),call(8210)},{routine_key(8210),type_bhav}};
    require(evaluate_read_only_entry_point5_bhav(global_snapshot,global_chain,global_descendants,300,10,20,{},16).state==ReadOnlyBhavState::evaluated_true,
        "global-rooted EP5 keeps the exact Caller's private/linked descendant owner");
    require(evaluate_read_only_entry_point5_bhav(global_snapshot,{},global_root,300,10,20,{},16).state==ReadOnlyBhavState::unsupported,
        "global EP5 root cannot be supplied in a private/linked owner bank");
    require(evaluate_read_only_entry_point5_bhav(with_ep5(255),{}, {},255,10,20,{},16).state==ReadOnlyBhavState::unsupported&&
        evaluate_read_only_entry_point5_bhav(with_ep5(0),{}, {},0,10,20,{},16).state==ReadOnlyBhavState::unsupported,
        "entry action0..255 is not an executable source BHAV");
    // Unknown ghost remains a real uncertainty when the linked tree reads it.
    RoutineSet ghost_owner{{routine_key(8206),ghost_bhav}};
    require(evaluate_read_only_entry_point5_bhav(semiglobal_snapshot,{},ghost_owner,8206,10,20,1,16).state==ReadOnlyBhavState::evaluated_true&&
        evaluate_read_only_entry_point5_bhav(semiglobal_snapshot,{},ghost_owner,8206,10,20,0,16).state==ReadOnlyBhavState::evaluated_false&&
        evaluate_read_only_entry_point5_bhav(semiglobal_snapshot,{},ghost_owner,8206,10,20,{},16).state==ReadOnlyBhavState::unsupported,
        "semiglobal ghost arguments are isolated and disagreeing results stay unsupported");
    Instruction increment_temp{2,1,0xff,{0,0,1,0,0,3,8,7}}; // PlusEquals=3; operator0 is GreaterThan.
    Instruction temp_one{2,0xfe,0xff,{0,0,1,0,0,2,8,7}};
    RoutineSet isolated{{routine_key(8206),{0x8002,{increment_temp,temp_one},{},Bhav::Metadata{0,4,0,0}}}};
    const auto isolated_result=evaluate_read_only_entry_point5_bhav(semiglobal_snapshot,{},isolated,8206,10,20,{},16);
    if(isolated_result.state!=ReadOnlyBhavState::evaluated_true||isolated_result.steps!=4)
        std::cerr<<"isolated ghost Temp EP5 state="<<static_cast<int>(isolated_result.state)
            <<" reason="<<static_cast<int>(isolated_result.reason)<<" steps="<<isolated_result.steps
            <<" diagnostic="<<isolated_result.diagnostic<<'\n';
    require(isolated_result.state==ReadOnlyBhavState::evaluated_true&&isolated_result.steps==4,
        "normal and ghost evaluations each begin with a fresh disposable Temp bank");
    require(encode_world_snapshot(semiglobal_snapshot)==semi_before&&encode_world_snapshot(ep5_world.snapshot())==ep5_before,
        "three-scope EP5 evaluation never mutates input or live World");

    // Reproduce the real DiningTables.iff#4122 -> Global.iff#355 path. Global
    // 355 searches the avatar list with SetToNext(Person), then compares each
    // ID with the original StackObjectID. This verifies nested global lookup
    // and read-only avatar enumeration in the same sandbox used by the resolver.
    World dining_world;
    require(dining_world.create(10, EntityKind::object, {2}) &&
            dining_world.create(5, EntityKind::avatar, {2, 66}) &&
            dining_world.create(20, EntityKind::avatar, {2, 66}) &&
            dining_world.set_person_data(20, 65, 0),
            "DiningTables caller and live avatar fixture");
    auto dining_ep5=caller_ep5;dining_ep5[5].action=4122;
    require(dining_world.define_type(10,0x2000000a)&&dining_world.define_entry_points(10,dining_ep5),"bind actual DiningTables EP5 definition/table");
    const auto dining_snapshot = dining_world.snapshot();
    const auto dining_before = encode_world_snapshot(dining_snapshot);
    const auto dining_root = dining_table_bhav();
    RoutineSet dining_owner{{routine_key(4122), dining_root}};
    const RoutineSet dining_globals{{routine_key(355), global355_person_lookup()}};
    auto dining_run = evaluate_read_only_entry_point5_bhav(dining_snapshot,
        dining_globals, dining_owner, 4122, 10, 20, {}, 16);
    require(dining_run.state == ReadOnlyBhavState::evaluated_false && dining_run.steps == 20,
            "DiningTables global 355 and flags return false for a live avatar with no flags");
    require(encode_world_snapshot(dining_snapshot) == dining_before &&
            encode_world_snapshot(dining_world.snapshot()) == dining_before,
            "DiningTables global enumeration does not change the snapshot or live World");
    require(dining_world.set_person_data(20, 65, 2), "set DiningTables flag two");
    auto flagged_snapshot = dining_world.snapshot();
    const auto flagged_before = encode_world_snapshot(flagged_snapshot);
    dining_run = evaluate_read_only_entry_point5_bhav(flagged_snapshot,
        dining_globals, dining_owner, 4122, 10, 20, {}, 16);
    require(dining_run.state == ReadOnlyBhavState::evaluated_true && dining_run.steps == 18,
            "DiningTables resolves flag two through nested Global 355");
    require(encode_world_snapshot(flagged_snapshot) == flagged_before &&
            encode_world_snapshot(dining_world.snapshot()) == flagged_before,
            "flag-two evaluation leaves the live World unchanged");
    require(dining_world.set_person_data(20, 65, 4), "set DiningTables flag three");
    flagged_snapshot = dining_world.snapshot();
    const auto flag_three_before = encode_world_snapshot(flagged_snapshot);
    dining_run = evaluate_read_only_entry_point5_bhav(flagged_snapshot,
        dining_globals, dining_owner, 4122, 10, 20, {}, 16);
    require(dining_run.state == ReadOnlyBhavState::evaluated_true && dining_run.steps == 20,
            "DiningTables resolves flag three through nested Global 355");
    require(encode_world_snapshot(flagged_snapshot) == flag_three_before &&
            encode_world_snapshot(dining_world.snapshot()) == flag_three_before,
            "flag-three evaluation leaves the live World unchanged");

    World no_person_world;
    require(no_person_world.create(10, EntityKind::object, {2}) &&
            no_person_world.create(20, EntityKind::object, {2}),
            "non-avatar StackObject fixture");
    require(no_person_world.define_type(10,0x2000000a)&&no_person_world.define_entry_points(10,dining_ep5),"bind non-avatar query caller EP5 definition/table");
    dining_run = evaluate_read_only_entry_point5_bhav(no_person_world.snapshot(),
        dining_globals, dining_owner, 4122, 10, 20, {}, 16);
    require(dining_run.state == ReadOnlyBhavState::evaluated_false,
            "Global 355 returns false when the World has no active avatar to match");

    auto unsupported_person_lookup = global355_person_lookup();
    unsupported_person_lookup.instructions[3].operands[4] = 130;
    const RoutineSet unsupported_globals{{routine_key(355), unsupported_person_lookup}};
    dining_run = evaluate_read_only_entry_point5_bhav(dining_snapshot,
        unsupported_globals, dining_owner, 4122, 10, 20, {}, 16);
    require(dining_run.state == ReadOnlyBhavState::unsupported,
            "unverified SetToNext search modes remain unsupported");

    std::cout << "Read-only EntryPoint 5 BHAV checks passed\n";
}
