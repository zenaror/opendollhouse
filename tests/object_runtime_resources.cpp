#include "odh/create_object_instance.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    try { action(); } catch (const odh::FormatError&) { require(true, message); return; }
    require(false, message);
}
void put16(std::string& bytes, std::size_t offset, std::uint16_t value, bool big = false) {
    for (unsigned n = 0; n < 2; ++n)
        bytes.at(offset + n) = static_cast<char>((value >> (8 * (big ? 1 - n : n))) & 255);
}
void put32(std::string& bytes, std::size_t offset, std::uint32_t value, bool big = false) {
    for (unsigned n = 0; n < 4; ++n)
        bytes.at(offset + n) = static_cast<char>((value >> (8 * (big ? 3 - n : n))) & 255);
}
std::string chunk(const char* type, std::uint16_t id, const std::string& payload) {
    std::string result(76, '\0');
    result.replace(0, 4, type);
    put32(result, 4, static_cast<std::uint32_t>(76 + payload.size()), true);
    put16(result, 8, id, true);
    return result + payload;
}
std::string objd() {
    std::string result(162, '\0');
    put32(result, 0, 138);
    put16(result, 14, 137);
    put16(result, 18, 4);
    put32(result, 28, 0xf301e375);
    put16(result, 40, 128);
    put16(result, 44, 1); // UsesFnTable
    put16(result, 116, 16);
    return result;
}
std::string objf() {
    std::string result(24, '\0');
    put32(result, 4, 1);
    result.replace(8, 4, "fJBO");
    put32(result, 12, 2);
    put16(result, 18, 4100);
    put16(result, 22, 4177);
    return result;
}
std::string slot() {
    constexpr std::size_t span = 70;
    std::string result(16 + span * 3, '\0');
    put32(result, 4, 10);
    result.replace(8, 4, "TOLS");
    put32(result, 12, 3);
    put16(result, 16, 0);
    put16(result, 16 + span, 3);
    put16(result, 16 + 2 * span, 0);
    return result;
}
std::string bhav() {
    std::string result(24, '\0');
    put16(result, 0, 0x8002);
    put16(result, 2, 1);
    result[14] = static_cast<char>(0xfd);
    result[15] = static_cast<char>(0xfd);
    return result;
}
std::string iff(bool with_objf = true,bool with_slots = true) {
    std::string result = "IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    result.resize(64, '\0');
    result += chunk("OBJD", 16808, objd());
    if (with_objf) result += chunk("OBJf", 16808, objf());
    if(with_slots)result += chunk("SLOT", 128, slot());
    result += chunk("GLOB", 0, std::string(1, static_cast<char>(11)) + "FoodGlobals");
    result += chunk("BHAV", 4100, bhav());
    return result;
}
odh::ObjectRuntimeResources read(const std::string& bytes,
    odh::ObjectRuntimeResourceLimits limits = {},const std::string& definition_bytes=objd(),bool selected_subpart=false) {
    std::istringstream stream(bytes, std::ios::binary);
    const odh::View resource(stream, bytes.size());
    std::istringstream object(definition_bytes, std::ios::binary);
    const auto definition = odh::read_object_definition(odh::View(object, definition_bytes.size()));
    return odh::read_object_runtime_resources(resource,
        {"Objects.far", "Food.iff", 16808, definition}, limits,selected_subpart);
}

struct GroupDefinition {std::uint16_t id;std::string bytes;};
std::string group_iff(const std::vector<GroupDefinition>& definitions) {
    std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";bytes.resize(64,'\0');
    for(const auto& definition:definitions)bytes+=chunk("OBJD",definition.id,definition.bytes);
    return bytes;
}
odh::DefinitionCandidate group_candidate(const GroupDefinition& source) {
    std::istringstream stream(source.bytes,std::ios::binary);
    return {"Objects.far","Group.iff",source.id,odh::read_object_definition(odh::View(stream,source.bytes.size()))};
}
std::vector<odh::DefinitionCandidate> group_candidates(const std::vector<GroupDefinition>& definitions) {
    std::vector<odh::DefinitionCandidate> result;for(const auto& source:definitions)result.push_back(group_candidate(source));return result;
}
odh::ObjectGroupConstructionPlan group_plan(const std::string& bytes,const odh::DefinitionIndex& index,
        std::uint32_t requested=0x10000001U,std::size_t limit=64) {
    const auto* candidate=index.unique(requested);require(candidate!=nullptr,"requested synthetic definition is unique");
    std::istringstream stream(bytes,std::ios::binary);
    return odh::plan_object_group_construction(odh::View(stream,bytes.size()),*candidate,index,limit);
}
void group_construction() {
    const auto definition=[](std::uint32_t guid,std::uint16_t sub,std::uint16_t lead=0,std::uint16_t level=0,std::uint16_t master=42){
        auto bytes=objd();put32(bytes,28,guid);put16(bytes,20,master);put16(bytes,22,sub);
        put16(bytes,126,lead);put16(bytes,112,level);return bytes;};
    // Deliberately interleave the master; neither chunk ID sorting nor first
    // part/minimum allocated runtime ID identifies the source BaseObject.
    const std::vector<GroupDefinition> definitions{{900,definition(0x10000002,0xff02,0,0x01ff)},
        {500,definition(0x10000001,0xffff)},{100,definition(0x10000003,0)},
        {700,definition(0x10000004,0x0280,7,0x7f)},
        {200,definition(0x10000005,0x0100,0,0,77)}};
    const auto candidates=group_candidates(definitions);const odh::DefinitionIndex index(candidates);
    const auto bytes=group_iff(definitions);const auto plan=group_plan(bytes,index);
    require(plan.multitile&&plan.definition_guid==0x10000001&&plan.parts.size()==3&&plan.base_index==2&&
        plan.parts[0].candidate->chunk_id==900&&plan.parts[1].candidate->chunk_id==100&&plan.parts[2].candidate->chunk_id==700,
        "master expands only its exact family and preserves IFF part order; positive lead marker wins over zero-offset/first part");
    require(plan.parts[0].offset_x==-16&&plan.parts[0].offset_y==32&&plan.parts[0].offset_level==-1&&
        plan.parts[2].offset_x==32&&plan.parts[2].offset_y==-2048&&plan.parts[2].offset_level==127&&plan.parts[2].lead,
        "SubIndex signed byte halves, low signed LevelOffset byte and lead field126 are read without clamping");
    const auto direct=group_plan(bytes,index,0x10000002);
    require(!direct.multitile&&direct.definition_guid==0x10000002&&direct.parts.size()==1&&direct.base_index==0&&
        direct.parts[0].candidate==index.unique(0x10000002)&&direct.parts[0].offset_x==-16&&
        direct.parts[0].offset_y==32&&direct.parts[0].offset_level==-1,
        "direct subpart remains one member while retaining raw AddObject offsets for later group repositioning");
    const auto direct_lead=group_plan(bytes,index,0x10000004);
    require(!direct_lead.multitile&&direct_lead.parts.size()==1&&direct_lead.base_index==0&&
        direct_lead.parts[0].offset_x==32&&direct_lead.parts[0].offset_y==-2048&&
        direct_lead.parts[0].offset_level==127&&direct_lead.parts[0].lead,
        "directly selected lead subpart retains its own raw metadata without creating siblings");
    auto no_lead=definitions;put16(no_lead[3].bytes,126,0);const odh::DefinitionIndex no_lead_index(group_candidates(no_lead));
    require(group_plan(group_iff(no_lead),no_lead_index).base_index==1,"zero-offset part is selected only when no lead marker exists");
    put16(no_lead[2].bytes,22,0x0101);const odh::DefinitionIndex no_zero_index(group_candidates(no_lead));
    require(group_plan(group_iff(no_lead),no_zero_index).base_index==0,"first source part is the fallback when lead and zero-offset parts are absent");
    for(const auto limit:{std::size_t{0},std::size_t{2},std::size_t{4097}})
        rejects([&]{group_plan(bytes,index,0x10000001,limit);},"invalid or insufficient group member limit rejects before materialization");
    require(group_plan(bytes,index,0x10000001,3).parts.size()==3,"exact member budget admits complete group");
    auto missing=candidates;missing.erase(missing.begin());const odh::DefinitionIndex missing_index(missing);
    rejects([&]{group_plan(bytes,missing_index);},"master cannot omit a real IFF part absent from the immutable index");
    auto ambiguous=candidates;ambiguous.push_back(candidates.front());ambiguous.back().source="Other.far";
    const odh::DefinitionIndex ambiguous_index(ambiguous);
    rejects([&]{group_plan(bytes,ambiguous_index);},"ambiguous subpart GUID cannot choose an arbitrary resource");
    auto foreign=candidates;foreign.front().asset="Other.iff";const odh::DefinitionIndex foreign_index(foreign);
    rejects([&]{group_plan(bytes,foreign_index);},"cross-IFF subpart cannot join the requested master's group");
    auto absent_master=definitions;absent_master.erase(absent_master.begin()+1);
    rejects([&]{group_plan(group_iff(absent_master),index);},"requested master chunk must actually exist in supplied IFF bytes");
    rejects([&]{group_plan(group_iff({definitions[1]}),index);},"abstract master without represented subparts cannot fabricate a body");
    auto duplicate=bytes+chunk("OBJD",definitions[0].id,definitions[0].bytes);
    rejects([&]{group_plan(duplicate,index);},"duplicate serialized subpart cannot create the same definition twice");
    duplicate=bytes+chunk("OBJD",definitions[1].id,definitions[1].bytes);
    rejects([&]{group_plan(duplicate,index);},"duplicate selected master chunk rejected");
    auto short_part=definitions;short_part[0].bytes.resize(127);const odh::DefinitionIndex short_index(group_candidates(short_part));
    rejects([&]{group_plan(group_iff(short_part),short_index);},"incomplete lead/offset metadata is not inferred");
    auto stale=definitions;put16(stale[0].bytes,22,0x0101);
    rejects([&]{group_plan(group_iff(stale),index);},"subpart identity must match actual supplied IFF metadata");
    const auto copied=*index.unique(0x10000001);std::istringstream stream(bytes,std::ios::binary);
    rejects([&]{odh::plan_object_group_construction(odh::View(stream,bytes.size()),copied,index);},
        "structurally identical external candidate does not replace the exact indexed resource capability");
}
}

int main() {
    try {
        group_construction();
        const auto resources = read(iff());
        require(resources.slots&&resources.slots->records.size()==3,
            "present selected SLOT remains an owned parsed resource");
        for(const auto slot_id:{0U,333U}) {
            auto definition=objd();put16(definition,40,static_cast<std::uint16_t>(slot_id));
            auto bytes=iff(true,false);bytes.replace(64+76,definition.size(),definition);
            const auto missing=read(bytes,{},definition);
            require(!missing.slots&&missing.definition.slot_table_id==slot_id&&
                missing.construction.status==odh::ObjectConstructionStatus::ready&&
                missing.construction.layout.containment_slots==0,
                "absent SLOT for zero or nonzero complete SlotID has zero containment without fabricated table");
            odh::World materialized;
            require(odh::materialize_runtime_object(materialized,1,missing)==odh::ObjectRuntimeCreateStatus::created&&
                materialized.find(1)->slots.empty()&&materialized.find(1)->attributes.size()==16,
                "missing SLOT materializes zero slots while preserving independent attribute layout");
            odh::World hidden;
            const odh::CreateObjectInstanceRequest request{missing.definition.guid,6,0,0,0};
            const auto created=odh::create_object_instance_out_of_world(hidden,2,request,missing);
            require(created.status==odh::CreateObjectInstanceStatus::created&&hidden.find(2)&&
                hidden.find(2)->slots.empty()&&!hidden.find(2)->position&&!hidden.find(2)->attachment,
                "no-SLOT object supports real OutOfWorld creation without containment");
            // A differently numbered malformed table is not the selected one.
            auto unrelated=bytes+chunk("SLOT",static_cast<std::uint16_t>(slot_id+1),"broken");
            require(!read(unrelated,{},definition).slots,"unrelated SLOT cannot fill absent selected ID");
        }
        {
            auto definition=objd();definition.resize(41);
            std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
            bytes.resize(64,'\0');bytes+=chunk("OBJD",16808,definition);
            rejects([&]{read(bytes,{},definition);},"partial OBJD SlotID cannot be mistaken for absent SLOT resource");
            auto malformed=iff(true,false)+chunk("SLOT",128,"broken");
            rejects([&]{read(malformed);},"present malformed selected SLOT still fails closed");
            auto limited=odh::ObjectRuntimeResourceLimits{};limited.formats.slot_records=0;
            rejects([&]{read(iff(),limited);},"present SLOT record budget remains enforced");
            require(!read(iff(true,false),limited).slots,"missing SLOT does not consume a fabricated record budget");
        }
        {
            const auto bytes=iff();std::istringstream stream(bytes,std::ios::binary);
            auto changed=resources.definition;changed.build_mode_type=7;
            rejects([&]{odh::read_object_runtime_resources(odh::View(stream,bytes.size()),
                {"Objects.far","Food.iff",16808,changed});},"changed BuildModeType rejects stale definition identity");
        }
        require(resources.definition.guid == 0xf301e375 &&
                    resources.definition.slot_table_id == 128,
                "definition identity and SlotID come from the selected OBJD");
        require(resources.construction.status == odh::ObjectConstructionStatus::ready &&
                    resources.construction.layout.attributes == 16 &&
                    resources.construction.layout.containment_slots == 2,
                "construction layout uses only the exact selected SLOT resource");
        require(resources.entry_points.source == odh::EntryPointSource::objf &&
                    resources.entry_points.entries.size() == 2 &&
                    resources.entry_points.entries[0].action == 4100 &&
                    resources.entry_points.entries[1].action == 4177,
                "OBJf entry points are resolved by the selected OBJD chunk ID");
        require(resources.private_routines.size() == 1 &&
                    resources.private_routines.count(odh::routine_key(4100)) == 1 &&
                    resources.semiglobal_name == std::optional<std::string>{"FoodGlobals"},
                "private BHAV and raw semiglobal link stay tied to one resource");
        odh::World world;
        require(world.create(1, odh::EntityKind::avatar) &&
                    odh::materialize_runtime_object(world, 4, resources) ==
                        odh::ObjectRuntimeCreateStatus::created,
                "resource-derived object is atomically materialized through World");
        const auto* entity = world.find(4);
        require(entity && entity->definition && entity->definition->guid == 0xf301e375 &&
                    entity->attributes.size() == 16 && entity->slots.size() == 2 &&
                    entity->entry_points && entity->entry_points->size() == 2,
                "materialized banks and entry points match the exact IFF resources");
        require(odh::materialize_runtime_object(world, 4, resources) ==
                    odh::ObjectRuntimeCreateStatus::world_rejected &&
                    world.snapshot().entities.size() == 2,
                "failed second materialization preserves the existing World");
        {
            auto part_definition=objd();put16(part_definition,20,42);put16(part_definition,22,0x0102);
            auto part_bytes=iff();part_bytes.replace(64+76,part_definition.size(),part_definition);
            const auto rejected=read(part_bytes,{},part_definition);
            require(rejected.construction.status==odh::ObjectConstructionStatus::unsupported_multitile,
                "ordinary runtime-resource reader keeps its default rejection of unresolved multipart definitions");
            auto admitted=read(part_bytes,{},part_definition,true);
            require(admitted.construction.status==odh::ObjectConstructionStatus::ready&&!admitted.master_guid&&
                admitted.construction.layout.attributes==16&&admitted.construction.layout.containment_slots==2,
                "selected subpart opt-in reads exact own banks without inventing a master override");
            odh::World direct;
            require(odh::materialize_runtime_object(direct,3,admitted)==odh::ObjectRuntimeCreateStatus::created&&
                direct.find(3)->definition->guid==admitted.definition.guid&&!direct.find(3)->definition->master_guid,
                "directly requested subpart materializes as a source singleton without implicit master expansion");
            admitted.master_guid=0x81234567U;odh::World grouped;
            require(odh::materialize_runtime_object(grouped,3,admitted)==odh::ObjectRuntimeCreateStatus::created&&
                grouped.find(3)->definition->guid==admitted.definition.guid&&grouped.find(3)->definition->master_guid==0x81234567U&&
                grouped.find(3)->entry_points->at(0).action==4100,
                "group-plan override reaches World while per-piece identity and own entry points remain unchanged");
            for(const auto bad_master:{0U,admitted.definition.guid}){
                auto invalid=admitted;invalid.master_guid=bad_master;odh::World untouched;const auto before=untouched.snapshot();
                require(odh::materialize_runtime_object(untouched,3,invalid)==odh::ObjectRuntimeCreateStatus::world_rejected&&
                    untouched.snapshot().entities.empty()&&untouched.snapshot().generation_sequence==before.generation_sequence&&
                    untouched.word_count()==before.word_count&&untouched.spatial_revision()==before.spatial_revision,
                    "invalid zero/self group master fails without partial World materialization");
            }
            auto master_definition=part_definition;put16(master_definition,22,0xffff);
            auto master_bytes=iff();master_bytes.replace(64+76,master_definition.size(),master_definition);
            const auto abstract=read(master_bytes,{},master_definition,true);odh::World absent;
            require(abstract.construction.status==odh::ObjectConstructionStatus::unsupported_multitile&&
                odh::materialize_runtime_object(absent,3,abstract)==odh::ObjectRuntimeCreateStatus::unsupported_profile&&absent.snapshot().entities.empty(),
                "abstract master can never materialize as a physical piece even with opt-in");
        }

        odh::World snack_world;
        require(snack_world.create(1, odh::EntityKind::object, {0, 0, 1, 0}) &&
                    snack_world.set_position(1, {80, 96, 1, 2}),
                "positioned fridge fixture is ready");
        const odh::CreateObjectInstanceRequest snack_request{
            0xf301e375, 4, 1, 0, 0};
        const auto snack = odh::create_object_instance_in_slot0(
            snack_world, 2, 1, snack_request, resources);
        const auto* fridge = snack_world.find(1);
        const auto* food = snack_world.find(2);
        require(snack.status == odh::CreateObjectInstanceStatus::created &&
                    snack.object_id == 2 && fridge && fridge->slots[0] == 2 && food &&
                    food->attachment && food->attachment->container == 1 &&
                    food->attachment->slot == 0 && food->attachment->positioned &&
                    food->position == std::optional<odh::EntityPosition>{{80, 96, 1, 2}},
                "Have A Snack creates Food and places it in fridge slot 0");
        odh::World out_of_world;
        const odh::CreateObjectInstanceRequest hidden_request{0xf301e375,6,0,0,0};
        const auto hidden=odh::create_object_instance_out_of_world(
            out_of_world,7,hidden_request,resources);
        const auto* hidden_food=out_of_world.find(7);
        require(hidden.status==odh::CreateObjectInstanceStatus::created&&hidden.object_id==7&&
                hidden_food&&!hidden_food->position&&!hidden_food->attachment,
                "OutOfWorld creation makes an unpositioned unattached runtime object");
        const auto occupied = odh::create_object_instance_in_slot0(
            snack_world, 3, 1, snack_request, resources);
        require(occupied.status == odh::CreateObjectInstanceStatus::slot_occupied &&
                    !occupied.object_id && snack_world.find(3) == nullptr,
                "occupied slot rejects a second Food without changing World");
        auto unsupported_request = snack_request;
        unsupported_request.raw_position = 0;
        require(odh::create_object_instance_in_slot0(
                    snack_world, 3, 1, unsupported_request, resources).status ==
                    odh::CreateObjectInstanceStatus::unsupported_position &&
                    snack_world.find(3) == nullptr,
                "other CreateObjectInstance positions fail without partial creation");

        auto bad = odh::ObjectRuntimeResourceLimits{};
        bad.private_routines = 0;
        rejects([&] { read(iff(), bad); }, "private BHAV count budget is enforced");
        bad = {};
        bad.private_instructions = 0;
        rejects([&] { read(iff(), bad); }, "private instruction budget is enforced");
        rejects([&] { read(iff(false)); }, "required OBJf table cannot be guessed");
        auto mismatch = iff();
        auto changed = objd();
        put32(changed, 28, 7);
        mismatch.replace(64 + 76, changed.size(), changed);
        rejects([&] { read(mismatch); }, "indexed GUID must match selected OBJD bytes");
        auto duplicate = iff();
        duplicate += chunk("SLOT", 128, slot());
        rejects([&] { read(duplicate); }, "duplicate selected SLOT fails closed");
        duplicate = iff();
        duplicate += chunk("BHAV", 4100, bhav());
        rejects([&] { read(duplicate); }, "duplicate private BHAV ID fails closed");
        duplicate = iff();
        duplicate += chunk("GLOB", 0, std::string(1, static_cast<char>(11)) + "FoodGlobals");
        rejects([&] { read(duplicate); }, "ambiguous semiglobal links fail closed");

        std::cout << checks << " object runtime resource checks passed (synthetic IFF fixtures only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "object runtime resource test failed: " << error.what() << '\n';
        return 1;
    }
}
