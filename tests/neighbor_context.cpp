#include "odh/neighbor_context.hpp"
#include "odh/world.hpp"
#include <iostream>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
odh::NeighborEntry entry(std::int16_t id, int version = 4, bool person = true) {
    odh::NeighborRecord record{};
    record.version = version; record.name = "Native shaped synthetic neighbor";
    record.person_mode = person ? 1 : 0;
    if (person) record.person_data = std::vector<std::int16_t>(version == 4 ? 80 : 256);
    record.neighbor_id = id; record.guid = 0xA0123456; record.unknown_neg_one = -1;
    return {1, record};
}
odh::Nbrs catalog(std::initializer_list<odh::NeighborEntry> entries) {
    return {0, 62, entries, 0};
}
void rejects(odh::Nbrs source) {
    bool rejected = false;
    try { odh::NeighborContext context(std::move(source)); }
    catch (const odh::FormatError&) { rejected = true; }
    require(rejected, "invalid runtime record/profile/budget rejects");
}
odh::Instruction next(unsigned scope = 10, unsigned index = 0, unsigned flags = 133) {
    // Arbitrary GUID/mode bytes must not affect NeighborId enumeration.
    return {31, 1, 1, {0xBE,0xEF,0xA1,0x5C,static_cast<std::uint8_t>(flags),
            static_cast<std::uint8_t>(scope),0xF9,static_cast<std::uint8_t>(index)}};
}
odh::Instruction next_guid(std::uint32_t guid,unsigned scope=10,unsigned index=0) {
    return {31,1,1,{static_cast<std::uint8_t>(guid),static_cast<std::uint8_t>(guid>>8),
        static_cast<std::uint8_t>(guid>>16),static_cast<std::uint8_t>(guid>>24),
        static_cast<std::uint8_t>(7|(scope==10?0:128)),static_cast<std::uint8_t>(scope),0,
        static_cast<std::uint8_t>(index)}};
}
odh::VmFrame frame(std::int16_t current = 11) {
    return {odh::routine_key(4096), 0, {101,202,current,0}, {0,current,0,0}, {0,current,0,0}};
}
odh::PrimitiveOutcome dispatch(const odh::VmPrimitive& primitive, const odh::VmFrame& f,
                              const odh::Instruction& i, const std::array<std::int16_t,20>& temps = {}) {
    return primitive(f,i,temps,{nullptr,17});
}
bool no_output(const odh::PrimitiveOutcome& result) {
    return !result.write && !result.temp_write && !result.stack_object_write && !result.call;
}
void ordering() {
    odh::NeighborContext context(catalog({entry(19),entry(11),entry(15)}));
    require(context.active_count() == 3 && context.source().entries[0].record->neighbor_id == 19,
            "runtime ordering retains immutable serialized order");
    for (const auto& pair : {std::pair<std::int16_t,std::int16_t>{0,11}, {11,15}, {12,15},
                           {15,19}, {18,19}, {19,-1}, {32767,-1}})
        require(context.next(pair.first) == pair.second, "sorted active and absent cursor continuation");
    odh::NeighborContext extremes(catalog({entry(32767),entry(-7),entry(-32768),entry(11)}));
    require(extremes.next(-32768) == -7 && extremes.next(-8) == -7 &&
            extremes.next(-7) == 11 && extremes.next(11) == 32767 && extremes.next(32767) == -1,
            "signed ordering preserves negative provider results and extrema");
    odh::NeighborContext placeholders(catalog({entry(11),{0,{}},entry(-7),{0,{}}}));
    require(placeholders.active_count() == 2 && placeholders.next(-7) == 0 &&
            placeholders.next(-1) == 0 && placeholders.next(0) == 11,
            "placeholder effective zero participates in order but is absent from active lookup");
    odh::NeighborContext after(catalog({entry(11),entry(0),{0,{}},{0,{}}}));
    odh::NeighborContext before(catalog({{0,{}},{0,{}},entry(0),entry(11)}));
    require(after.next(0) == 0 && before.next(0) == 11,
            "stable ordering preserves active zero position relative to placeholder zeros");
    const auto zero = dispatch(after.primitive(),frame(0),next());
    require(zero.disposition == odh::PrimitiveDisposition::goto_true && zero.stack_object_write &&
            zero.stack_object_write->value == 0, "placeholder zero provider result succeeds even without active placeholder entity");
    rejects(catalog({entry(11),entry(11)}));
    odh::NeighborContext empty(catalog({}));
    require(empty.next(-32768) == -1 && empty.active_count() == 0, "empty provider ends");
    auto match_a=entry(19);match_a.record->guid=0xC61931DC;
    auto match_b=entry(15);match_b.record->guid=0xC61931DC;
    auto other=entry(11);other.record->guid=0x11223344;
    odh::NeighborContext typed(catalog({match_a,other,{0,{}},match_b}));
    require(typed.next_of_guid(0,0xC61931DC)==15&&typed.next_of_guid(15,0xC61931DC)==19&&
            typed.next_of_guid(19,0xC61931DC)==-1&&typed.next_of_guid(0,0x11223344)==11,
            "typed neighbor scan skips other GUIDs and continues in signed ID order");
    unsigned typed_observations=0;
    const auto typed_primitive=typed.primitive({},[&](const auto& result){
        require(result.current==0&&result.result==15&&result.success,"typed scan observation records matching neighbor");
        ++typed_observations;
    });
    const auto typed_result=dispatch(typed_primitive,frame(0),next_guid(0xC61931DC));
    require(typed_result.disposition==odh::PrimitiveDisposition::goto_true&&typed_result.stack_object_write&&
            typed_result.stack_object_write->value==15&&typed_observations==1,
            "opcode31 mode7 writes the next neighbor matching its GUID");
    const auto missing_type=dispatch(typed.primitive(),frame(19),next_guid(0xC61931DC));
    require(missing_type.disposition==odh::PrimitiveDisposition::goto_false&&no_output(missing_type),
            "typed neighbor scan with no later match takes the false branch without writing");
}
void person_data() {
    auto old = entry(11); auto raw = entry(15,10); auto absent = entry(19,4,false);
    (*old.record->person_data)[0] = -32768; (*old.record->person_data)[79] = 32767;
    (*raw.record->person_data)[0] = 901; (*raw.record->person_data)[87] = -123;
    (*raw.record->person_data)[88] = 777; (*raw.record->person_data)[255] = 999;
    odh::NeighborContext context(catalog({old,raw,absent,{0,{}}}));
    for (const auto id : {11,15}) {
        const auto first = context.person_data(static_cast<std::int16_t>(id),0);
        require(first && first->neighbor == id && first->index == 0 &&
                first->value == (id == 11 ? -32768 : 901) &&
                first->source == odh::NeighborDataSource::serialized, "serialized person words retain signed values and provenance");
        require(!context.person_data(static_cast<std::int16_t>(id),-1) &&
                !context.person_data(static_cast<std::int16_t>(id),88), "present runtime bank bounds are exactly88");
    }
    require(context.person_data(11,79)->value == 32767 && context.person_data(15,87)->value == -123,
            "legacy serialized last word and raw runtime last word remain accessible");
    for (std::int16_t index = 80; index < 88; ++index) {
        const auto value = context.person_data(11,index);
        require(value && value->value == 0 && value->source == odh::NeighborDataSource::legacy_zero_extension,
                "legacy bank zero extends words80..87");
    }
    require(context.source().entries[1].record->person_data->size() == 256 &&
            (*context.source().entries[1].record->person_data)[255] == 999 && !context.person_data(15,255),
            "raw256 source retained while runtime exposes only first88");
    for (const std::int16_t index : {std::int16_t{-32768},std::int16_t{-1},std::int16_t{0},std::int16_t{88},std::int16_t{32767}}) {
        for (const std::int16_t id : {std::int16_t{0},std::int16_t{77},std::int16_t{19}}) {
            const auto value = context.person_data(id,index);
            require(value && value->neighbor == id && value->index == index && value->value == 0 &&
                    value->source == (id == 19 ? odh::NeighborDataSource::absent_person_data_zero :
                                     odh::NeighborDataSource::missing_neighbor_zero),
                    "null conditional missing neighbor/person bank yields zero before index validation");
        }
    }
}
void primitive_targets() {
    odh::NeighborContext context(catalog({entry(19),entry(11),entry(15)}));
    auto f = frame(); std::array<std::int16_t,20> temps{}; temps[1] = 11;
    unsigned observations = 0;
    auto primitive = context.primitive({}, [&](const auto& observation) {
        require(observation.current == 11 && observation.result == 15 && observation.success,
                "observation records successful neighbor continuation"); ++observations;
    });
    for (unsigned scope : {8U,9U,10U,25U}) {
        const auto outcome = dispatch(primitive,f,next(scope,1),temps);
        require(outcome.disposition == odh::PrimitiveDisposition::goto_true, "every native writable bank branches true");
        if (scope == 8) require(outcome.temp_write && outcome.temp_write->index == 1 && outcome.temp_write->value == 15 && !outcome.write && !outcome.stack_object_write, "temp target selects one output");
        if (scope == 9 || scope == 25) require(outcome.write && outcome.write->bank == (scope == 9 ? odh::VmFrameBank::arguments : odh::VmFrameBank::locals) && outcome.write->index == 1 && outcome.write->value == 15 && !outcome.temp_write && !outcome.stack_object_write, "frame target selects bank and index");
        if (scope == 10) require(outcome.stack_object_write && outcome.stack_object_write->value == 15 && !outcome.temp_write && !outcome.write, "stack target publishes independent identity");
    }
    require(observations == 4, "observation emitted once per successful dispatch");
    const auto legacy = dispatch(context.primitive(),f,next(8,255,5),temps);
    require(legacy.stack_object_write && legacy.stack_object_write->value == 15 && !legacy.temp_write,
            "legacy flags force scope10/index0 despite invalid encoded target");
    auto local_temps=temps;local_temps[0]=1;
    const auto local_output=dispatch(context.primitive(),f,next(40,0),local_temps);
    require(local_output.disposition==odh::PrimitiveDisposition::goto_true&&local_output.write&&
        local_output.write->bank==odh::VmFrameBank::locals&&local_output.write->index==1&&local_output.write->value==15,
        "neighbor LocalByTemp stages a frame write rather than an external scope25 write");
    for (unsigned scope : {8U,9U,25U,30U}) {
        const auto outcome = dispatch(context.primitive(),f,next(scope,255),temps);
        require(outcome.disposition == odh::PrimitiveDisposition::unsupported && no_output(outcome),
                "missing target rejects all VM outputs");
    }
    const auto literal = dispatch(context.primitive(),f,next(7,11),temps);
    require(literal.disposition == odh::PrimitiveDisposition::unsupported && no_output(literal), "successful literal target rejects write");
    auto wrong_opcode = next(); wrong_opcode.opcode = 30;
    require(dispatch(context.primitive(),f,wrong_opcode).disposition == odh::PrimitiveDisposition::unsupported &&
            dispatch(context.primitive(),f,next(10,0,132)).disposition == odh::PrimitiveDisposition::unsupported,
            "unhandled opcode and search type remain unsupported");
    unsigned calls = 0; std::int16_t external = 11;
    odh::VmMemory memory;
    memory.read = [&](const auto&, auto scope, auto index)->std::optional<std::int16_t> {
        require(calls++ == 0 && scope == 18 && index == 11 && external == 11, "indirect read resolves selector before write"); return external;
    };
    memory.write = [&](const auto&, auto scope, auto index, auto value) {
        require(calls++ == 1 && scope == 18 && index == 11 && value == 15 && external == 11, "aliased external write follows original cursor read"); external = value; return true;
    };
    const auto external_outcome = dispatch(context.primitive(memory),f,next(30,1),temps);
    require(external_outcome.disposition == odh::PrimitiveDisposition::goto_true && no_output(external_outcome) && external == 15 && calls == 2, "scope30 remaps external read/write without VM bank output");
    for (unsigned target_scope : {19U,31U}) {
        calls = 0; external = 11;
        const auto expected_index = target_scope == 31 ? 11 : 1;
        memory.read = [&](const auto&,auto scope,auto index)->std::optional<std::int16_t> {
            require(calls++ == 0 && scope == 19 && index == expected_index, "direct or stack indirect backend target read");return external;
        };
        memory.write = [&](const auto&,auto scope,auto index,auto value) {
            require(calls++ == 1 && scope == 19 && index == expected_index && value == 15, "direct or stack indirect backend target write");external = value;return true;
        };
        const auto result = dispatch(context.primitive(memory),f,next(target_scope,1),temps);
        require(result.disposition == odh::PrimitiveDisposition::goto_true && no_output(result) && calls == 2 && external == 15,
                "external target publishes through explicit memory backend");
    }
    memory.read = [](const auto&,auto,auto)->std::optional<std::int16_t>{return 11;};
    memory.write = [](const auto&,auto,auto,auto){return false;};
    const auto denied = dispatch(context.primitive(memory),f,next(19,1));
    require(denied.disposition == odh::PrimitiveDisposition::unsupported && no_output(denied), "backend rejected write rejects outputs");
    memory.write = {};
    require(dispatch(context.primitive(memory),f,next(19,1)).disposition == odh::PrimitiveDisposition::unsupported,
            "missing backend write rejects successful continuation");
    memory.read = {};
    require(dispatch(context.primitive(memory),f,next(19,1)).disposition == odh::PrimitiveDisposition::unsupported,
            "missing backend read rejects target");
}
void false_results() {
    for (const auto current : {std::int16_t{-32768},std::int16_t{-8},std::int16_t{32767}}) {
        odh::NeighborContext context(catalog({entry(-32768),entry(-7),entry(32767)}));
        const auto f = frame(current); std::array<std::int16_t,20> temps{}; temps[1] = current;
        unsigned reads = 0, writes = 0, observations = 0;
        odh::VmMemory memory;
        memory.read = [&](const auto&,auto,auto)->std::optional<std::int16_t>{++reads;return current;};
        memory.write = [&](const auto&,auto,auto,auto){++writes;return true;};
        auto primitive = context.primitive(memory,[&](const auto& result) { require(!result.success && result.result < 0 && result.current == current, "negative provider observation remains false");++observations; });
        for (unsigned scope : {8U,9U,10U,25U,19U}) {
            const auto result = dispatch(primitive,f,next(scope,1),temps);
            require(result.disposition == odh::PrimitiveDisposition::goto_false && no_output(result), "negative provider IDs and terminal minus1 never publish output");
        }
        require(reads == 1 && writes == 0 && observations == 5 && f.context.stack_object == current && temps[1] == current,
                "false branch reads external target but preserves all targets");
    }
}
void actual_vm() {
    auto chosen = entry(15,10); (*chosen.record->person_data)[3] = -812;
    odh::NeighborContext context(catalog({entry(19),entry(11),chosen}));
    unsigned reads = 0;
    odh::VmMemory memory;
    memory.read = [&](const auto& f,auto scope,auto index)->std::optional<std::int16_t> {
        require(scope == 32 && index == 3 && f.context.stack_object == 15 && f.context.caller == 101 && f.context.callee == 202,
                "VM neighbor memory sees chosen independent ID and preserves caller/callee");
        ++reads; const auto value = context.person_data(f.context.stack_object,index);
        return value ? std::optional<std::int16_t>{value->value} : std::nullopt;
    };
    const auto key = odh::routine_key(4096);
    odh::Bhav routine{0x8002,{next(),{2,254,255,{0,0,3,0,0,5,8,32}}},{},odh::Bhav::Metadata{0,4,0,0}};
    odh::Vm vm({{key,routine}},memory,{},context.primitive(memory));
    require(vm.start(key,{101,202,11,0}) == odh::VmStatus::running && vm.run(1) == odh::VmStatus::budget_exhausted &&
            vm.stack()[0].context.stack_object == 15 && vm.stack()[0].context.code_owner == 0,
            "actual VM publishes neighbor ID without requiring World entity");
    require(vm.run(1) == odh::VmStatus::returned_true && reads == 1 && vm.temps()[0] == -812,
            "subsequent actual Expression reads scope32 chosen person word");
}
void instance_ids() {
    auto neighbor=entry(40);neighbor.record->guid=0xfc72c0c0;
    auto alias=entry(41);alias.record->guid=neighbor.record->guid;
    odh::NeighborContext context(catalog({neighbor,alias}));
    odh::World world;
    require(context.instance_id(world,40)==0&&context.instance_id(world,99)==0,
        "scope24 instance query distinguishes existing neighbor without avatar and missing neighbor as zero");
    require(world.create(8,odh::EntityKind::object,{})&&world.define_type(8,neighbor.record->guid)&&
        context.instance_id(world,40)==0,"matching typed ordinary object cannot satisfy avatar query");
    // Insert larger ID first to distinguish source ObjectID order from creation
    // or World generation order, including duplicate GUID neighbor aliases.
    require(world.create(20,odh::EntityKind::avatar,{})&&world.define_type(20,neighbor.record->guid)&&
        world.create(10,odh::EntityKind::avatar,{})&&world.define_type(10,neighbor.record->guid)&&
        context.instance_id(world,40)==10&&context.instance_id(world,41)==10,
        "duplicate-GUID avatars return first ObjectID, independently of insertion and neighbor alias");
    require(world.set_ui_ghost(10,true)&&context.instance_id(world,40)==20,
        "UI ghost avatar is absent from source ObjectQueries registration");
    require(world.set_ui_ghost(10,false)&&world.retire(10)&&context.instance_id(world,40)==20,
        "removed avatar cannot remain an instance through a stale neighbor cache");
    require(world.retire(20)&&context.instance_id(world,40)==0,
        "last live matching avatar retirement returns zero immediately");
    require(world.create(30,odh::EntityKind::avatar,{})&&world.define_type(30,neighbor.record->guid)&&
        context.instance_id(world,40)==30,"new live instance is observed without rebuilding NeighborContext");
    require(world.create(15,odh::EntityKind::avatar,{})&&!context.instance_id(world,40)&&
        context.instance_id(world,99)==0,
        "unknown earlier avatar GUID prevents guessing first match while missing NBRS still returns zero");
    require(world.define_type(15,0x12345678)&&context.instance_id(world,40)==30,
        "proven nonmatching earlier avatar allows exact later match");
    require(world.create(31,odh::EntityKind::avatar,{})&&context.instance_id(world,40)==30,
        "unknown avatar after first proven match cannot change source FirstOrDefault result");
    odh::VmMemory memory;
    memory.read=[&](const auto& frame,auto scope,auto index)->std::optional<std::int16_t>{
        if(scope!=24||index!=0)return {};
        return context.instance_id(world,frame.context.stack_object);
    };
    const auto key=odh::routine_key(4096);
    const odh::Bhav read{0x8002,{{2,254,255,{0,0,0,0,0,5,8,24}}},{},odh::Bhav::Metadata{0,4,0,0}};
    odh::Vm vm({{key,read}},memory);
    vm.start(key,{101,202,40});
    require(vm.run(10)==odh::VmStatus::returned_true&&vm.temps()[0]==30,
        "actual VM Expression reads scope24 field0 with StackObject interpreted as NeighborID");
    require(world.retire(30)&&world.retire(31),"remove known match and unknown later avatar for no-instance VM case");
    vm.start(key,{101,202,40});
    require(vm.run(10)==odh::VmStatus::returned_true&&vm.temps()[0]==0,
        "actual VM sees zero for a valid neighbor lacking a live avatar");
    require(world.create(12,odh::EntityKind::avatar,{}),"unknown avatar fixture");
    vm.start(key,{101,202,40});
    require(vm.run(10)==odh::VmStatus::fault,"unprovable GUID lookup remains unsupported instead of fabricating zero");
}
void invalid_profiles() {
    auto invalid = entry(11); invalid.marker = 2; rejects(catalog({invalid}));
    invalid = entry(11); invalid.marker = 0; rejects(catalog({invalid}));
    rejects(catalog({{1,{}}}));
    invalid = entry(11); invalid.record->version = 7; rejects(catalog({invalid}));
    invalid = entry(11); invalid.record->person_mode = 0; rejects(catalog({invalid}));
    invalid = entry(11); invalid.record->person_data.reset(); rejects(catalog({invalid}));
    invalid = entry(11); invalid.record->person_data->resize(88); rejects(catalog({invalid}));
    invalid = entry(11,10); invalid.record->person_data->resize(80); rejects(catalog({invalid}));
    invalid = entry(11); invalid.record->name.assign(257,'x'); rejects(catalog({invalid}));
    invalid = entry(11); invalid.record->relationships.resize(16385); rejects(catalog({invalid}));
    invalid = entry(11); invalid.record->relationships.push_back({1,7,std::vector<std::int32_t>(65537)}); rejects(catalog({invalid}));
    odh::Nbrs many{0,62,std::vector<odh::NeighborEntry>(4097,odh::NeighborEntry{0,{}}),0}; rejects(many);
    many.entries.clear();
    for (int id = 0; id < 257; ++id) {auto item = entry(static_cast<std::int16_t>(id));item.record->name.assign(256,'x');many.entries.push_back(item);}
    rejects(many);
    auto one = entry(1); auto two = entry(2);
    one.record->relationships.resize(8192); two.record->relationships.resize(8193); rejects(catalog({one,two}));
    one.record->relationships = {{1,1,std::vector<std::int32_t>(32768)}};
    two.record->relationships = {{1,1,std::vector<std::int32_t>(32769)}}; rejects(catalog({one,two}));
    // Exact budget boundaries remain valid; cumulative limits reject only excess.
    many.entries.assign(4096,odh::NeighborEntry{0,{}});
    require(odh::NeighborContext(many).source().entries.size() == 4096, "entry budget accepts exact boundary");
    many.entries.clear();
    for (int id = 0; id < 256; ++id) {auto item = entry(static_cast<std::int16_t>(id),4,false);item.record->name.assign(256,'x');many.entries.push_back(item);}
    require(odh::NeighborContext(many).active_count() == 256, "name budgets accept256 bytes and65536 cumulative bytes");
    one.record->relationships.resize(8192); two.record->relationships.resize(8192);
    one.record->relationships[0].values.assign(32768,0); two.record->relationships[0].values.assign(32768,0);
    require(odh::NeighborContext(catalog({one,two})).active_count() == 2, "relationship and value budgets accept exact cumulative boundaries");
}
}
int main() {
    try {
        ordering(); person_data(); primitive_targets(); false_results(); actual_vm(); instance_ids(); invalid_profiles();
        auto a=entry(19),b=entry(11),c=entry(15);c.record->guid=0x12345678;
        odh::NeighborContext guids(catalog({a,b,{0,{}},c}));
        require(guids.id_for_guid(0xA0123456)==11,"GUID map last serialized wins rather than highest sorted ID");
        require(guids.id_for_guid(0x12345678)==15&&!guids.id_for_guid(0),"distinct GUID and missing mapping remain explicit");
        require(guids.by_id(19)&&guids.by_id(19)->neighbor_id==19&&guids.by_id(11)&&!guids.by_id(0),"record lookup retains actual active identity excluding placeholders");
        std::cout << checks << " checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
