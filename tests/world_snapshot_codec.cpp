#include "odh/world_snapshot_codec.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F>
void rejects(F&& operation, const char* message) {
    try { operation(); }
    catch (const odh::FormatError&) { return; }
    require(false, message);
}
odh::WorldSnapshot fixture() {
    odh::World world({}, odh::AttributePolicy::ts1_bounded);
    std::array<std::int16_t,68> raw{};
    raw[1] = 7;
    raw[3] = 19;
    std::array<std::int16_t,5> extras{-1,2,-3,4,-5};
    require(world.create(9, odh::EntityKind::object) && world.remove(9),
            "fixture generation history");
    require(world.create_saved_entity(1, odh::EntityKind::avatar, 123, {1,-2}, {3,-4},
                {-300,200}, raw, extras, 1, 456) &&
            world.create(2, odh::EntityKind::object, {2,0,1}) &&
            world.create(3, odh::EntityKind::object, {1}),
            "fixture entity creation");
    require(world.define_type(2, 789) &&
            world.define_entry_points(1, {{12,65535},{0,4}}) &&
            world.define_entry_points(2, {}) &&
            world.declare_definition_word(1, 255, -7) &&
            world.declare_object_word(3, 78, -8) &&
            world.add_bound_appearance(1,"suit-toothbrush.apr") &&
            world.add_bound_appearance(1,"suit-toothpaste.apr"),
            "fixture definitions");
    odh::VmFrame frame{odh::routine_key(4096),0,{1,1,1},{},{}};
    require(world.write(frame,0,4,-55) && world.write(frame,3,0,999) &&
            world.set_person_data(1,0,91) && world.set_motive(1,0,-25) &&
            world.set_avatar_kill_timeout(1,72),
            "fixture mutable banks");
    require(world.set_position(1,{16,32,2,7}) &&
            world.set_position(2,{48,64,1,3}) &&
            world.place_in_slot(1,0,2) && world.attach(2,0,3) &&
            world.set_radian_direction(1,-0.3f) &&
            world.set_static_footprint(1,false) && world.set_ui_ghost(3,true),
            "fixture positions and containment");
    require(world.set_dynamic_sprite_flag(1,5,true),"fixture dynamic sprite flag");
    return world.snapshot();
}
void retained_body_roundtrip(){
    odh::World world;require(world.create(1,odh::EntityKind::object,{1,0,0}),"create dead codec body");
    const auto old=world.find(1)->generation;
    require(world.mark_dead({1,old})&&world.find(1)&&world.reference_is_dead({1,old}),"source marks Dead while numeric lookup still sees the dictionary body");
    const auto in_flight=odh::encode_world_snapshot(world.snapshot());const auto marked=odh::decode_world_snapshot(in_flight);
    require(in_flight[0]==7&&marked.retained_entities.empty()&&marked.entities.front().dead,
        "standalone World codec does not silently revive an in-flight Dead body");
    require(world.remove_retaining_body({1,old})&&world.create(1,odh::EntityKind::object,{1,0,0}),"reuse codec body number with new generation");
    const auto state=world.snapshot();const auto encoded=odh::encode_world_snapshot(state);
    require(encoded[0]==7&&encoded[1]==0,"retained body uses World v7 only when present");
    auto decoded=odh::decode_world_snapshot(encoded);odh::World reopened;
    require(!decoded.entities.front().suppress_referenced_reset&&
        !decoded.retained_entities.front().suppress_referenced_reset,
        "legacy version7 retains unknown policy for both indexed and retained bodies");
    require(reopened.restore_snapshot(decoded)&&reopened.reference_is_dead({1,old})&&reopened.find(1)&&
        reopened.find(1)->generation!=old&&odh::encode_world_snapshot(decoded)==encoded,"same numeric live and dead generations survive a fresh World exactly");
    auto bad=decoded;bad.retained_entities.push_back(bad.retained_entities.front());
    rejects([&]{odh::encode_world_snapshot(bad);},"duplicate retained body cannot be archived");
    bad=decoded;bad.retained_entities.front().generation=bad.entities.front().generation;
    rejects([&]{odh::encode_world_snapshot(bad);},"one generation cannot describe live and retained bodies");
    auto limits=odh::WorldSnapshotCodecLimits{};limits.world.entities=1;
    rejects([&]{odh::decode_world_snapshot(encoded,limits);},"retained plus live count is bounded together before copying");
    require(world.collect_retained_bodies({}),"collect codec body at safe point");
    const auto legacy=odh::encode_world_snapshot(world.snapshot());require(legacy[0]!=7&&odh::encode_world_snapshot(odh::decode_world_snapshot(legacy))==legacy,
        "no retained bodies preserves the historical World encoding");
}
void parent_alias_shadow_roundtrip() {
    odh::World world;
    std::array<std::int16_t,68> raw{};
    raw[3]=-1;raw[26]=31337;
    require(world.create_saved_entity(1,odh::EntityKind::object,123,{},{},{},raw,{},0) &&
            world.create(2,odh::EntityKind::object,{0,0,1}),"parent alias imported shadow fixture");
    const odh::VmFrame child{odh::routine_key(4096),0,{1,1,1},{},{}};
    require(world.find(1)->object_words.at(26)==31337 &&
            world.find(1)->saved_object_banks->object_data[26]==31337 &&
            world.read(child,3,26)==0 && world.read(child,4,26)==0,
            "uncontained imported ParentId shadow is preserved but reads null");
    require(world.attach(2,0,1),"attach imported shadow fixture");
    const auto before=odh::encode_world_snapshot(world.snapshot());
    const auto words=world.word_count();
    require(world.read(child,3,26)==2 && world.read(child,4,26)==2 &&
            world.write(child,3,26,-123) && world.read(child,3,26)==2 &&
            world.find(1)->object_words.at(26)==-123 &&
            world.write(child,4,26,22222) && world.read(child,4,26)==2,
            "both ObjectData scopes read actual parent while shadow writes remain permitted");
    require(world.word_count()==words && world.find(1)->object_words.at(26)==22222 &&
            world.find(1)->saved_object_banks->object_data[26]==31337,
            "ParentId live shadow write keeps word accounting and immutable imported bank");
    const auto encoded=odh::encode_world_snapshot(world.snapshot());
    require(encoded.size()==before.size() && encoded[0]==before[0] && encoded[1]==before[1],
            "ParentId read alias leaves codec version and serialized extent unchanged");
    const auto decoded=odh::decode_world_snapshot(encoded);
    require(odh::encode_world_snapshot(decoded)==encoded,"ParentId raw/source banks reencode byte-stably");
    odh::World restored;
    require(restored.restore_snapshot(decoded) && restored.read(child,3,26)==2 && restored.read(child,4,26)==2 &&
            restored.find(1)->object_words.at(26)==22222 &&
            restored.find(1)->saved_object_banks->object_data[26]==31337 && restored.word_count()==words &&
            odh::encode_world_snapshot(restored.snapshot())==encoded,
            "fresh restored World reads real parent and preserves live shadow plus source bank");
    require(restored.detach(2,0) && restored.read(child,3,26)==0 && restored.read(child,4,26)==0 &&
            restored.find(1)->object_words.at(26)==22222 &&
            restored.find(1)->saved_object_banks->object_data[26]==31337,
            "detach reads null parent without clearing either stored shadow");
    const auto detached=odh::encode_world_snapshot(restored.snapshot());
    odh::World detached_restored;
    require(detached_restored.restore_snapshot(odh::decode_world_snapshot(detached)) &&
            detached_restored.read(child,3,26)==0 && detached_restored.read(child,4,26)==0 &&
            detached_restored.find(1)->object_words.at(26)==22222 &&
            detached_restored.find(1)->saved_object_banks->object_data[26]==31337 &&
            odh::encode_world_snapshot(detached_restored.snapshot())==detached,
            "detached roundtrip preserves nonzero raw shadow while ParentId remains null");
}
}

void lockout_clock_roundtrip(){
    odh::World legacy;
    require(legacy.create(1,odh::EntityKind::object)&&legacy.declare_object_word(1,25,30),"legacy lockout raw fixture");
    const auto old=odh::encode_world_snapshot(legacy.snapshot());const auto old_state=odh::decode_world_snapshot(old);
    require(old[0]==4&&!old_state.lockout_count_clock&&!old_state.entities[0].lockout_count&&
        odh::encode_world_snapshot(old_state)==old,"model absence retains byte-identical World v4 encoding");
    odh::World promoted;require(promoted.restore_snapshot(old_state)&&promoted.set_lockout_count_clock(5)&&
        !promoted.read({odh::routine_key(4096),0,{1,1,1},{},{}},3,25)&&
        promoted.admit_lockout_count_state(1,promoted.find(1)->generation,true),
        "enabling a legacy checkpoint does not invent positive-count history");
    require(promoted.create(2,odh::EntityKind::object)&&promoted.declare_object_word(2,25,-5)&&
        promoted.admit_lockout_count_state(2,promoted.find(2)->generation,true,40)&&
        promoted.create(3,odh::EntityKind::object)&&promoted.declare_object_word(3,25,8)&&
        promoted.admit_lockout_count_state(3,promoted.find(3)->generation,false)&&
        promoted.create(4,odh::EntityKind::object)&&promoted.declare_object_word(4,25,0),"v5 known, future, absent Thread and unknown entity states");
    const auto snapshot=promoted.snapshot();const auto encoded=odh::encode_world_snapshot(snapshot);
    const auto reopened=odh::decode_world_snapshot(encoded);
    require(encoded[0]==5&&encoded[1]==0&&reopened.lockout_count_clock==std::optional<std::uint32_t>{5}&&
        reopened.entities[0].lockout_count->thread_present&&!reopened.entities[0].lockout_count->timestamp&&
        reopened.entities[1].lockout_count->timestamp==std::optional<std::uint32_t>{40}&&
        !reopened.entities[2].lockout_count->thread_present&&!reopened.entities[3].lockout_count&&
        odh::encode_world_snapshot(reopened)==encoded,"World v5 preserves clock, unknown stamp, future stamp and explicit Thread proof exactly");
    odh::World restored;require(restored.restore_snapshot(reopened),"restore v5 World into independent runtime");
    const odh::VmFrame first{odh::routine_key(4096),0,{1,1,1},{},{}};
    const odh::VmFrame second{odh::routine_key(4096),0,{2,2,2},{},{}};
    const auto spatial=restored.spatial_revision();const auto words=restored.word_count();
    require(!restored.read(first,3,25)&&restored.read(second,3,25)==0&&
        restored.find(2)->lockout_count->timestamp==std::optional<std::uint32_t>{0}&&
        restored.find(2)->object_words.at(25)==-5&&restored.spatial_revision()==spatial&&restored.word_count()==words,
        "restored future read resets only timestamp, preserving raw banks and spatial/word counters");
    require(restored.write(first,3,25,30)&&restored.set_lockout_count_clock(8)&&restored.read(first,4,25)==27,
        "actual post-restore same-value write establishes source timestamp");
    const auto known=odh::encode_world_snapshot(restored.snapshot());
    odh::World continued;require(continued.restore_snapshot(odh::decode_world_snapshot(known))&&
        continued.set_lockout_count_clock(10)&&continued.read(first,3,25)==25&&
        continued.find(1)->object_words.at(25)==30,"known write history continues identically after another codec roundtrip");
    auto malformed=snapshot;malformed.lockout_count_clock.reset();
    rejects([&]{odh::encode_world_snapshot(malformed);},"entity timestamp evidence without its clock cannot enter legacy wire");
    for(std::size_t length=0;length<encoded.size();++length){const auto prefix=std::vector<std::uint8_t>(encoded.begin(),encoded.begin()+length);
        rejects([&]{odh::decode_world_snapshot(prefix);},"truncated v5 clock/state payload rejected");}
    auto bad_marker=encoded;bad_marker.back()=2;
    rejects([&]{odh::decode_world_snapshot(bad_marker);},"v5 entity-state marker must be a canonical boolean");
}
void referenced_reset_policy_roundtrip(){
    odh::World world;
    for(std::int16_t id=1;id<=6;++id)require(world.create(id,odh::EntityKind::object),"policy codec body");
    const auto reference=[&](std::int16_t id){return odh::WorldEntityReference{id,world.find(id)->generation};};
    require(world.set_suppress_referenced_reset(reference(1),false)&&
        world.set_suppress_referenced_reset(reference(2),true)&&
        world.set_suppress_referenced_reset(reference(4),false)&&
        world.set_suppress_referenced_reset(reference(5),true),"publish known live and soon-retained policies");
    for(std::int16_t id=4;id<=6;++id)require(world.remove_retaining_body(reference(id)),"retain policy codec body");
    const auto bytes=odh::encode_world_snapshot(world.snapshot());const auto decoded=odh::decode_world_snapshot(bytes);
    require(bytes[0]==9&&bytes[1]==0&&decoded.entities.size()==3&&decoded.retained_entities.size()==3,
        "known false or true policy selects World version9");
    for(const auto* bodies:{&decoded.entities,&decoded.retained_entities}){
        require(bodies->at(0).suppress_referenced_reset==std::optional<bool>{false}&&
            bodies->at(1).suppress_referenced_reset==std::optional<bool>{true}&&
            !bodies->at(2).suppress_referenced_reset.has_value(),
            "version9 preserves false true and unknown independently for live and retained bodies");
    }
    odh::World reopened;require(reopened.restore_snapshot(decoded)&&
        odh::encode_world_snapshot(reopened.snapshot())==bytes,"restored policy state roundtrips canonically");
    odh::World minimal;require(minimal.create(1,odh::EntityKind::object),"minimal policy body");
    const auto legacy=odh::encode_world_snapshot(minimal.snapshot());
    require(legacy[0]==4&&!odh::decode_world_snapshot(legacy).entities.front().suppress_referenced_reset&&
        odh::encode_world_snapshot(odh::decode_world_snapshot(legacy))==legacy,
        "legacy policy remains unknown with identical old bytes");
    require(minimal.set_suppress_referenced_reset({1,minimal.find(1)->generation},false),"publish known false policy alone");
    const auto known_false=odh::encode_world_snapshot(minimal.snapshot());
    require(known_false[0]==9&&odh::decode_world_snapshot(known_false).entities.front().suppress_referenced_reset==std::optional<bool>{false},
        "known false alone promotes version rather than being confused with unknown");
    // The final entity policy tag precedes version9's retained-count DWORD.
    for(const auto bad:{std::uint8_t{3},std::uint8_t{255}}){
        auto corrupted=known_false;corrupted[corrupted.size()-5]=bad;
        rejects([&]{odh::decode_world_snapshot(corrupted);},"noncanonical referenced-reset policy tag rejected");
    }
    auto empty_extension=known_false;empty_extension[empty_extension.size()-5]=0;
    rejects([&]{odh::decode_world_snapshot(empty_extension);},"policy-free version9 extension is noncanonical");
    require(minimal.set_name({1,minimal.find(1)->generation},"Named legacy body"),"legacy named policy fixture");
    auto old_named_state=minimal.snapshot();old_named_state.entities.front().suppress_referenced_reset.reset();
    const auto old_named=odh::encode_world_snapshot(old_named_state);const auto named_decoded=odh::decode_world_snapshot(old_named);
    require(old_named[0]==8&&!named_decoded.entities.front().suppress_referenced_reset&&
        odh::encode_world_snapshot(named_decoded)==old_named,"legacy version8 names do not imply reset-policy knowledge");
    auto tight=odh::WorldSnapshotCodecLimits{};tight.max_bytes=bytes.size()-1;
    rejects([&]{odh::encode_world_snapshot(world.snapshot(),tight);},"version9 encoder retains byte limit");
    rejects([&]{odh::decode_world_snapshot(bytes,tight);},"version9 decoder retains byte limit");
    for(std::size_t length=0;length<bytes.size();++length){
        const auto prefix=std::vector<std::uint8_t>(bytes.begin(),bytes.begin()+length);
        rejects([&]{odh::decode_world_snapshot(prefix);},"truncated version9 payload rejected");
    }
}
void native_recovery_position_roundtrip(){
    odh::World world;
    const std::array<std::optional<bool>,3> values{std::nullopt,false,true};
    for(std::int16_t id=1;id<=18;++id){
        require(world.create(id,odh::EntityKind::object),"native-position codec matrix body");
        const odh::WorldEntityReference ref{id,world.find(id)->generation};
        const auto n=(id-1)%9;
        if(values[n/3])require(world.set_skip_native_recovery_position(ref,*values[n/3]),"matrix native bit6 publication");
        if(values[n%3])require(world.set_suppress_referenced_reset(ref,*values[n%3]),"matrix native bit12 publication");
        if(id>9)require(world.remove_retaining_body(ref),"matrix retained body publication");
    }
    const auto bytes=odh::encode_world_snapshot(world.snapshot());
    const auto decoded=odh::decode_world_snapshot(bytes);
    require(bytes[0]==10&&decoded.entities.size()==9&&decoded.retained_entities.size()==9,"known bit6 selects codec10 for live and retained bodies");
    for(const auto* bodies:{&decoded.entities,&decoded.retained_entities})for(std::size_t n=0;n<9;++n)
        require(bodies->at(n).skip_native_recovery_position==values[n/3]&&bodies->at(n).suppress_referenced_reset==values[n%3],
            "all nine independent unknown/false/true bit6 and bit12 combinations survive disk encoding");
    odh::World restored;require(restored.restore_snapshot(decoded)&&odh::encode_world_snapshot(restored.snapshot())==bytes,
        "native-position codec roundtrip retains exact canonical bytes");
    odh::World minimal;require(minimal.create(1,odh::EntityKind::object),"native-position minimal body");
    const odh::WorldEntityReference ref{1,minimal.find(1)->generation};
    const auto legacy=odh::encode_world_snapshot(minimal.snapshot());
    require(!odh::decode_world_snapshot(legacy).entities.front().skip_native_recovery_position&&
        odh::encode_world_snapshot(odh::decode_world_snapshot(legacy))==legacy,"pre-policy bytes retain unknown native bit6");
    require(minimal.set_skip_native_recovery_position(ref,false),"known false native bit6 without any bit12 knowledge");
    const auto bit6_only=odh::encode_world_snapshot(minimal.snapshot());
    require(bit6_only[0]==10&&odh::decode_world_snapshot(bit6_only).entities.front().skip_native_recovery_position==std::optional<bool>{false}&&
        !odh::decode_world_snapshot(bit6_only).entities.front().suppress_referenced_reset,
        "codec10 accepts bit6 false with bit12 unknown rather than requiring unrelated proof");
    for(const auto tag:{std::uint8_t{3},std::uint8_t{255}}){auto invalid=bit6_only;invalid[invalid.size()-5]=tag;
        rejects([&]{odh::decode_world_snapshot(invalid);},"invalid native bit6 policy tag rejected");}
    auto empty=bit6_only;empty[empty.size()-5]=0;
    rejects([&]{odh::decode_world_snapshot(empty);},"version10 without known bit6 is noncanonical");
    require(minimal.set_suppress_referenced_reset(ref,false),"known referenced reset baseline");
    auto v9_state=minimal.snapshot();v9_state.entities.front().skip_native_recovery_position.reset();
    const auto v9=odh::encode_world_snapshot(v9_state);
    require(v9[0]==9&&!odh::decode_world_snapshot(v9).entities.front().skip_native_recovery_position&&
        odh::encode_world_snapshot(odh::decode_world_snapshot(v9))==v9,"version9 bytes unchanged when new bit6 remains unknown");
    auto both=odh::decode_world_snapshot(odh::encode_world_snapshot(minimal.snapshot()));both.entities.front().skip_native_recovery_position.reset();
    require(odh::encode_world_snapshot(both)==v9,"removing new knowledge reproduces exact previous version9 byte stream");
    auto tight=odh::WorldSnapshotCodecLimits{};tight.max_bytes=bytes.size()-1;
    rejects([&]{odh::encode_world_snapshot(world.snapshot(),tight);},"version10 encoder enforces byte limit");
    rejects([&]{odh::decode_world_snapshot(bytes,tight);},"version10 decoder enforces byte limit");
    for(std::size_t length=0;length<bit6_only.size();++length){const auto prefix=std::vector<std::uint8_t>(bit6_only.begin(),bit6_only.begin()+length);
        rejects([&]{odh::decode_world_snapshot(prefix);},"truncated version10 payload rejected");}
}
int main() {
    try {
        retained_body_roundtrip();
        lockout_clock_roundtrip();
        referenced_reset_policy_roundtrip();
        native_recovery_position_roundtrip();
        parent_alias_shadow_roundtrip();
        const auto snapshot = fixture();
        const auto encoded = odh::encode_world_snapshot(snapshot);
        const auto decoded = odh::decode_world_snapshot(encoded);
        require(odh::encode_world_snapshot(decoded) == encoded,
                "every World snapshot field round-trips byte-for-byte");
        odh::World restored;
        require(decoded.entities[0].bound_appearances==std::vector<std::string>{"suit-toothbrush.apr","suit-toothpaste.apr"},
                "version 4 stores sorted avatar accessory names and dynamic sprite flags");
        require(decoded.entities[0].dynamic_sprite_flags==(std::uint64_t{1}<<5),
                "dynamic sprite flags survive snapshot round-trip");
        require(restored.restore_snapshot(decoded), "decoded snapshot restores into a fresh World");
        require(odh::encode_world_snapshot(restored.snapshot()) == encoded,
                "restored World retains the complete encoded state");

        auto invalid_words = snapshot;
        ++invalid_words.word_count;
        rejects([&] { (void)odh::encode_world_snapshot(invalid_words); },
                "inconsistent word accounting is rejected");
        auto invalid_attachment = snapshot;
        invalid_attachment.entities[1].attachment->container = 300;
        rejects([&] { (void)odh::encode_world_snapshot(invalid_attachment); },
                "missing containment parent is rejected");
        rejects([&] { (void)odh::encode_world_snapshot(snapshot, {{}, 30}); },
                "encoder enforces byte budget");

        auto invalid_policy = encoded;
        invalid_policy[2] = 255;
        rejects([&] { (void)odh::decode_world_snapshot(invalid_policy); },
                "unknown attribute policy is rejected");
        auto unsupported_version = encoded;
        unsupported_version[0] = 99;
        rejects([&] { (void)odh::decode_world_snapshot(unsupported_version); },
                "unsupported World section version is rejected");
        for (std::size_t size = 0; size < encoded.size(); ++size) {
            const std::vector<std::uint8_t> prefix(encoded.begin(),
                encoded.begin() + static_cast<std::ptrdiff_t>(size));
            rejects([&] { (void)odh::decode_world_snapshot(prefix); },
                    "every truncated World snapshot is rejected");
        }
        auto tight_limits = odh::WorldSnapshotCodecLimits{};
        tight_limits.world.entities = 2;
        rejects([&] { (void)odh::decode_world_snapshot(encoded, tight_limits); },
                "entity count is checked against configured limits");

        const odh::World empty;
        const auto empty_encoded = odh::encode_world_snapshot(empty.snapshot());
        require(odh::encode_world_snapshot(odh::decode_world_snapshot(empty_encoded)) == empty_encoded,
                "empty World snapshot round-trips");
        odh::World one_entity;
        require(one_entity.create(1, odh::EntityKind::object), "minimal entity fixture");
        const auto minimal = odh::encode_world_snapshot(one_entity.snapshot());
        require(odh::encode_world_snapshot(odh::decode_world_snapshot(minimal)) == minimal,
                "minimum-size entity snapshot is accepted");
        auto legacy_v2=minimal;
        legacy_v2[0]=2;legacy_v2.resize(legacy_v2.size()-12); // v2 has no accessory vector or sprite flags.
        const auto legacy_v2_world=odh::decode_world_snapshot(legacy_v2);
        require(legacy_v2_world.entities.size()==1&&legacy_v2_world.entities[0].bound_appearances.empty(),
                "version 2 snapshots remain readable without avatar accessories");
        auto legacy_v1=legacy_v2;
        legacy_v1[0]=1;legacy_v1.pop_back(); // v1 has no motive-controller presence byte.
        const auto legacy_world=odh::decode_world_snapshot(legacy_v1);
        require(legacy_world.entities.size()==1&&legacy_world.entities[0].motive_changes.empty(),
                "version 1 snapshots remain readable without continuous motive state");

        odh::World motive_world;
        require(motive_world.create(1,odh::EntityKind::avatar,{0,0,0,16})&&
                motive_world.set_motive(1,7,40)&&motive_world.tick_motive_changes()&&
                motive_world.set_motive_change(1,7,900,100)&&motive_world.tick_motive_changes(),
                "continuous motive controller fixture");
        const auto motive_bytes=odh::encode_world_snapshot(motive_world.snapshot());
        const auto motive_snapshot=odh::decode_world_snapshot(motive_bytes);
        require(motive_snapshot.entities[0].motives[7]==40&&
                motive_snapshot.entities[0].motive_changes.size()==16&&
                motive_snapshot.entities[0].motive_changes[7].per_hour_change==900&&
                motive_snapshot.entities[0].motive_changes[7].fractional==0.5&&
                !motive_snapshot.entities[0].motive_changes[7].ticked&&
                odh::encode_world_snapshot(motive_snapshot)==motive_bytes,
                "version 2 stores fractional carry and resets transient Ticked on restore");
        auto unsorted = snapshot;
        std::swap(unsorted.entities[0], unsorted.entities[1]);
        rejects([&] { (void)odh::encode_world_snapshot(unsorted); },
                "non-canonical entity order is rejected");

        odh::World appearance;
        require(appearance.create(1,odh::EntityKind::avatar,{0,101,0}),"appearance avatar");
        const auto legacy_appearance=odh::encode_world_snapshot(appearance.snapshot());
        require(legacy_appearance[0]==4,"legacy worlds keep exact old format");
        require(appearance.set_avatar_outfit_resolver([](const auto&,auto,auto)->std::optional<odh::AvatarOutfitReference>{return std::nullopt;}),"bind runtime resolver");
        require(odh::encode_world_snapshot(appearance.snapshot())==legacy_appearance,"binding resolver does not normalize old save or alter bytes");
        odh::AvatarOutfitReference outfit;outfit.literal="nfit_01,BODY=nfitlgt_01";outfit.hands.fill("hand");
        require(appearance.set_avatar_outfit(1,outfit,true,1)&&appearance.add_bound_appearance(1,"CaseSensitive","CaseSensitive.apr"),"publish literal/default/raw accessory state");
        auto bytes=odh::encode_world_snapshot(appearance.snapshot());require(bytes[0]==6,"appearance requires version6");
        auto appearance_snapshot=odh::decode_world_snapshot(bytes);odh::World resumed;
        require(resumed.restore_snapshot(appearance_snapshot)&&odh::encode_world_snapshot(resumed.snapshot())==bytes&&resumed.find(1)->avatar_appearance==appearance.find(1)->avatar_appearance,"fresh World retains exact actual/default body and visual accessories");
        outfit.kind=odh::AvatarOutfitKind::identifier;outfit.literal.clear();outfit.hands.fill("");outfit.identifier=0x123456789abcdef0ULL;
        require(resumed.set_avatar_outfit(1,outfit,true),"numeric source outfit state");
        bytes=odh::encode_world_snapshot(resumed.snapshot());appearance_snapshot=odh::decode_world_snapshot(bytes);
        require(appearance_snapshot.entities[0].avatar_appearance->body->identifier==outfit.identifier&&odh::encode_world_snapshot(appearance_snapshot)==bytes,"numeric Update reference stays typed and lossless");
        appearance_snapshot.entities[0].avatar_appearance->body->literal="invented";
        rejects([&]{odh::encode_world_snapshot(appearance_snapshot);},"malformed typed outfit rejected");
        odh::WorldLimits tiny;tiny.bound_appearance_names=1;odh::World bounded(tiny);
        require(bounded.create(1,odh::EntityKind::avatar)&&bounded.add_bound_appearance(1,"one","one.apr"),"appearance budget fixture");
        const auto bounded_before=odh::encode_world_snapshot(bounded.snapshot());
        require(!bounded.add_bound_appearance(1,"two","two.apr")&&odh::encode_world_snapshot(bounded.snapshot())==bounded_before,"combined key/visual asset budget rejection atomic");

        {
            odh::World names;require(names.create(1,odh::EntityKind::object,{}),"name fixture");
            const auto old=names.find(1)->generation;
            require(names.set_name({1,old},"Açúcar"),"exact name setter");
            auto named=odh::encode_world_snapshot(names.snapshot());
            require(named[0]==8&&odh::decode_world_snapshot(named).entities[0].name_override=="Açúcar","nameoverride canonical v8 roundtrip");
            std::string astral;for(int i=0;i<16;++i)astral+="😀";
            require(names.set_name({1,old},astral)&&!names.set_name({1,old},astral+"😀"),"name limit counts UTF16 units rather than bytes/codepoints");
            require(!names.set_name({1,old},std::string(33,'a'))&&!names.set_name({1,old},std::string("bad\0name",8)),"overlong/NUL names rejected");
            require(names.remove_retaining_body({1,old})&&names.create(1,odh::EntityKind::object,{}),"oldname retained plus IDreuse");
            require(names.set_name({1,old},"Retained")&&!names.find(1)->name_override&&names.resolve_reference({1,old})->name_override=="Retained","old reference never renames replacement");
            named=odh::encode_world_snapshot(names.snapshot());const auto restored=odh::decode_world_snapshot(named);
            require(!restored.entities[0].name_override&&restored.retained_entities[0].name_override=="Retained"&&odh::encode_world_snapshot(restored)==named,"retainedname and replacement persist separately");
        }

        {
            odh::World native;
            require(native.create(1,odh::EntityKind::object)&&native.create(2,odh::EntityKind::object)&&native.create(3,odh::EntityKind::object),"native linked backing fixture");
            const odh::WorldEntityReference a{1,native.find(1)->generation},b{2,native.find(2)->generation},c{3,native.find(3)->generation};
            const auto legacy=odh::encode_world_snapshot(native.snapshot());
            require(!odh::decode_world_snapshot(legacy).entities[0].native_slot0,"older bytes keep native backing unknown");
            require(native.declare_object_word(2,14,0)&&native.declare_object_word(2,26,1)&&native.declare_object_word(3,14,0)&&native.declare_object_word(3,26,2),"raw zero hierarchy fixture");
            require(native.publish_native_slot0(a,b)&&native.publish_native_slot0(b,c)&&native.publish_native_slot0(c,{}),"exact native chain publication");
            require(!native.find(2)->attachment&&native.find(1)->slots.empty(),"native slot zero does not create physical containment");
            require(!native.remove(1)&&!native.remove(2)&&!native.remove_retaining_body(c),"linked owners require explicit native unlink before removal");
            auto bytes=odh::encode_world_snapshot(native.snapshot());
            const auto roundtrip=odh::decode_world_snapshot(bytes);
            require(bytes[0]==11&&roundtrip.entities[0].native_slot0==b&&roundtrip.entities[2].native_slot0==odh::WorldEntityReference{}&&odh::encode_world_snapshot(roundtrip)==bytes,"World11 exact chain and knownempty roundtrip");
            auto stale=roundtrip;stale.entities[0].native_slot0->generation++;
            rejects([&]{odh::encode_world_snapshot(stale);},"stale native child cannot alias numeric replacement");
            auto mismatch=roundtrip;mismatch.entities[1].object_words[26]=3;
            rejects([&]{odh::encode_world_snapshot(mismatch);},"nonreciprocal native owner rejected");
            auto cycle=roundtrip;cycle.entities[2].native_slot0=a;cycle.entities[0].object_words[14]=0;cycle.entities[0].object_words[26]=3;
            rejects([&]{odh::encode_world_snapshot(cycle);},"native slot zero cycle rejected");
            auto mixed=roundtrip;mixed.entities[1].native_slot0=odh::WorldEntityReference{};
            mixed.entities[1].slots.push_back(1);mixed.word_count++;
            mixed.entities[0].attachment=odh::Attachment{2,0};
            rejects([&]{odh::encode_world_snapshot(mixed);},"mixed physical and native slot-zero cycle rejected");
            require(!native.publish_native_slot0(a,c)&&!native.publish_native_slot0(a,{b.id,b.generation+1}),"wrong parent and expired child publication rejected");
            require(native.publish_native_slot0(b,{})&&native.publish_native_slot0(a,{})&&native.remove(2)&&native.create(2,odh::EntityKind::object),"explicit storage unlink permits later removal and reuse");
            require(!native.native_slot0(b)&&!native.find(2)->native_slot0,"old reference never reads replacement backing");
        }

        {
            odh::World native;require(native.create_runtime_object(1,100,{0,0,0},{}),"real constructor native OOW fixture");
            const odh::WorldEntityReference ref{1,native.find(1)->generation};
            require(native.native_position(ref)==std::optional<odh::EntityPosition>{{-16,-16,1,0}},"native constructor sentinel exact source level");
            require(native.set_native_out_of_world_level(ref,0)&&native.native_position(ref)->level==0,"known zero level differs from absent native data");
            const auto bytes=odh::encode_world_snapshot(native.snapshot());const auto decoded=odh::decode_world_snapshot(bytes);
            require(bytes[0]==12&&decoded.entities[0].native_out_of_world_level==0&&odh::encode_world_snapshot(decoded)==bytes,"World12 native OOW level byte roundtrip");
            auto invalid=decoded;invalid.entities[0].position=odh::EntityPosition{24,24,1,0};
            rejects([&]{odh::encode_world_snapshot(invalid);},"inworld pose cannot carry contradictory OOW level");
            require(native.set_position(1,{24,24,2,0})&&!native.find(1)->native_out_of_world_level&&native.native_position(ref)==native.find(1)->position,"inworld placement uses actual scalar and clears sentinel");
            require(native.place_individual_on_floor(1,std::nullopt,0)&&!native.native_position(ref),"generic removal does not manufacture native level");
            require(native.publish_native_recovery_position(ref,std::nullopt,true)&&native.native_position(ref)==std::optional<odh::EntityPosition>{{-16,-16,1,0}},"native scalar removal publishes source sentinel");
            require(native.declare_object_word(1,1,6)&&native.native_position(ref)->direction==6,"native sentinel uses actual current heading without a stale stored copy");
            require(native.remove(1)&&native.create(1,odh::EntityKind::object)&&!native.native_position(ref),"native scalar cannot bind replacement generation");
        }
        {
            odh::World native;require(native.create_runtime_object(1,100,{0,0,1},{})&&native.create_runtime_object(2,200,{0,0,0},{}),"contained native OOW fixture");
            const odh::WorldEntityReference parent{1,native.find(1)->generation},child{2,native.find(2)->generation};
            require(native.set_native_out_of_world_level(parent,2)&&native.attach(1,0,2)&&native.refresh_contained_position(2)&&native.native_position(child)->level==2,
                "contained OOW refresh copies authenticated parent scalar level");
            require(native.place_out_of_world(1,0)&&native.native_position(parent)->level==2&&native.native_position(child)->level==2,
                "generic repeated OOW keeps existing proven levels without inventing new data");
        }
        std::cout << "World snapshot codec checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
