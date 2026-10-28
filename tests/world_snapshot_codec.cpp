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

int main() {
    try {
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

        std::cout << "World snapshot codec checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
