#include "odh/world_snapshot_codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace odh {
namespace {
constexpr std::uint16_t codec_version = 12;
constexpr std::size_t minimum_header_size = 31;

class Writer {
public:
    explicit Writer(std::size_t limit) : limit_(limit) {}
    void u8(std::uint8_t value) { push(value); }
    void boolean(bool value) { u8(value ? 1 : 0); }
    void u16(std::uint16_t value) {
        push(static_cast<std::uint8_t>(value));
        push(static_cast<std::uint8_t>(value >> 8));
    }
    void i16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }
    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            push(static_cast<std::uint8_t>(value >> shift));
    }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8)
            push(static_cast<std::uint8_t>(value >> shift));
    }
    void f32(float value) {
        if (!std::numeric_limits<float>::is_iec559 || sizeof(float) != sizeof(std::uint32_t))
            throw FormatError("World snapshot float format is unsupported on this platform");
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }
    void f64(double value) {
        if (!std::numeric_limits<double>::is_iec559 || sizeof(double) != sizeof(std::uint64_t))
            throw FormatError("World snapshot double format is unsupported on this platform");
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u64(bits);
    }
    void count(std::size_t value) {
        if (value > std::numeric_limits<std::uint32_t>::max())
            throw FormatError("World snapshot count exceeds the file format");
        u32(static_cast<std::uint32_t>(value));
    }
    void text(const std::string& value) {
        count(value.size());
        for(const unsigned char byte:value)push(byte);
    }
    void raw(const std::vector<std::uint8_t>& bytes) {
        if (bytes.size() > limit_ - data_.size())
            throw FormatError("World snapshot exceeds byte limit");
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }
    const std::vector<std::uint8_t>& data() const { return data_; }
    std::vector<std::uint8_t> take() { return std::move(data_); }
private:
    void push(std::uint8_t value) {
        if (data_.size() >= limit_) throw FormatError("World snapshot exceeds byte limit");
        data_.push_back(value);
    }
    std::size_t limit_;
    std::vector<std::uint8_t> data_;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    std::uint8_t u8() {
        require(1);
        return bytes_[offset_++];
    }
    bool boolean() {
        const auto value = u8();
        if (value > 1) throw FormatError("invalid World snapshot boolean");
        return value != 0;
    }
    std::uint16_t u16() {
        const auto low = static_cast<std::uint16_t>(u8());
        const auto high = static_cast<std::uint16_t>(u8());
        return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8));
    }
    std::int16_t i16() {
        const auto bits = u16();
        const auto value = bits <= static_cast<std::uint16_t>(INT16_MAX)
            ? static_cast<std::int32_t>(bits)
            : static_cast<std::int32_t>(bits) - 65536;
        return static_cast<std::int16_t>(value);
    }
    std::uint32_t u32() {
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
            value |= static_cast<std::uint32_t>(u8()) << shift;
        return value;
    }
    std::int32_t i32() {
        const auto bits = u32();
        const auto value = bits <= static_cast<std::uint32_t>(INT32_MAX)
            ? static_cast<std::int64_t>(bits)
            : static_cast<std::int64_t>(bits) - INT64_C(4294967296);
        return static_cast<std::int32_t>(value);
    }
    std::uint64_t u64() {
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
            value |= static_cast<std::uint64_t>(u8()) << shift;
        return value;
    }
    std::size_t size(std::size_t maximum) {
        const auto value = u64();
        if (value > maximum || value > std::numeric_limits<std::size_t>::max())
            throw FormatError("World snapshot size exceeds limit");
        return static_cast<std::size_t>(value);
    }
    std::uint32_t count(std::size_t maximum, std::size_t minimum_bytes_per_item) {
        const auto value = u32();
        if (value > maximum || (minimum_bytes_per_item != 0 &&
            value > remaining() / minimum_bytes_per_item))
            throw FormatError("World snapshot count exceeds limit or remaining data");
        return value;
    }
    std::string text(std::size_t maximum) {
        const auto length=count(maximum,1);
        std::string result;result.reserve(length);
        for(std::uint32_t i=0;i<length;++i)result.push_back(static_cast<char>(u8()));
        return result;
    }
    float f32() {
        if (!std::numeric_limits<float>::is_iec559 || sizeof(float) != sizeof(std::uint32_t))
            throw FormatError("World snapshot float format is unsupported on this platform");
        const auto bits = u32();
        float value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    double f64() {
        if (!std::numeric_limits<double>::is_iec559 || sizeof(double) != sizeof(std::uint64_t))
            throw FormatError("World snapshot double format is unsupported on this platform");
        const auto bits=u64();
        double value=0;
        std::memcpy(&value,&bits,sizeof(value));
        return value;
    }
    std::size_t remaining() const { return bytes_.size() - offset_; }
    bool done() const { return offset_ == bytes_.size(); }
private:
    void require(std::size_t count) {
        if (count > remaining()) throw FormatError("World snapshot is truncated");
    }
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
};

void write_i16_vector(Writer& writer, const std::vector<std::int16_t>& values) {
    writer.count(values.size());
    for (const auto value : values) writer.i16(value);
}
std::vector<std::int16_t> read_i16_vector(Reader& reader, std::size_t maximum) {
    const auto count = reader.count(maximum, 2);
    std::vector<std::int16_t> values;
    values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) values.push_back(reader.i16());
    return values;
}
void write_outfit(Writer& writer,const AvatarOutfitReference& outfit){
    writer.u8(static_cast<std::uint8_t>(outfit.kind));writer.u64(outfit.identifier);writer.text(outfit.literal);
    for(const auto& hand:outfit.hands)writer.text(hand);
}
AvatarOutfitReference read_outfit(Reader& reader){
    AvatarOutfitReference outfit;const auto kind=reader.u8();
    if(kind>static_cast<std::uint8_t>(AvatarOutfitKind::none))throw FormatError("invalid avatar outfit reference kind");
    outfit.kind=static_cast<AvatarOutfitKind>(kind);outfit.identifier=reader.u64();outfit.literal=reader.text(4096);
    for(auto& hand:outfit.hands)hand=reader.text(1024);
    return outfit;
}
void write_entity(Writer& writer, const Entity& entity,std::uint16_t version) {
    writer.i16(entity.id);
    writer.u8(static_cast<std::uint8_t>(entity.kind));
    writer.u64(entity.generation);
    write_i16_vector(writer, entity.attributes);
    write_i16_vector(writer, entity.person_data);
    write_i16_vector(writer, entity.slots);
    writer.count(entity.object_words.size());
    for (const auto& item : entity.object_words) {
        writer.i16(item.first);
        writer.i16(item.second);
    }

    writer.boolean(entity.attachment.has_value());
    if (entity.attachment) {
        writer.i16(entity.attachment->container);
        writer.u64(static_cast<std::uint64_t>(entity.attachment->slot));
        writer.boolean(entity.attachment->positioned);
    }
    writer.boolean(entity.position.has_value());
    if (entity.position) {
        writer.i32(entity.position->x);
        writer.i32(entity.position->y);
        writer.i16(entity.position->level);
        writer.u8(entity.position->direction);
    }

    writer.boolean(entity.definition.has_value());
    if (entity.definition) {
        writer.u32(entity.definition->guid);
        writer.boolean(entity.definition->master_guid.has_value());
        if (entity.definition->master_guid) writer.u32(*entity.definition->master_guid);
    }
    write_i16_vector(writer, entity.motives);
    writer.count(entity.definition_words.size());
    for (const auto& item : entity.definition_words) {
        writer.i16(item.first);
        writer.i16(item.second);
    }

    writer.boolean(entity.entry_points.has_value());
    if (entity.entry_points) {
        writer.count(entity.entry_points->size());
        for (const auto& entry : *entity.entry_points) {
            writer.u16(entry.condition);
            writer.u16(entry.action);
        }
    }

    writer.boolean(entity.saved_object_banks.has_value());
    if (entity.saved_object_banks) {
        for (const auto value : entity.saved_object_banks->object_data) writer.i16(value);
        for (const auto value : entity.saved_object_banks->extras) writer.i16(value);
    }
    writer.i16(entity.unattached_slot);
    writer.boolean(entity.static_footprint);
    writer.boolean(entity.ui_ghost);
    writer.boolean(entity.kill_timeout.has_value());
    if (entity.kill_timeout) writer.i32(*entity.kill_timeout);
    writer.f32(entity.radian_direction);
    writer.boolean(entity.motive_changes.size()==16);
    if(entity.motive_changes.size()==16)for(const auto& change:entity.motive_changes){
        writer.i16(change.per_hour_change);
        writer.i16(change.maximum);
        writer.f64(change.fractional);
    }
    writer.count(entity.bound_appearances.size());
    for(const auto& name:entity.bound_appearances)writer.text(name);
    writer.u64(entity.dynamic_sprite_flags);
    if(version>=5){
        writer.boolean(entity.lockout_count.has_value());
        if(entity.lockout_count){
            writer.boolean(entity.lockout_count->thread_present);
            writer.boolean(entity.lockout_count->timestamp.has_value());
            if(entity.lockout_count->timestamp)writer.u32(*entity.lockout_count->timestamp);
        }
    }
    if(version>=6){
        writer.boolean(entity.avatar_appearance.has_value());
        if(entity.avatar_appearance){const auto& state=*entity.avatar_appearance;
            writer.boolean(state.body.has_value());if(state.body)write_outfit(writer,*state.body);
            writer.boolean(state.default_daywear.has_value());if(state.default_daywear)write_outfit(writer,*state.default_daywear);
            writer.boolean(state.accessory_assets.has_value());if(state.accessory_assets){
                writer.count(state.accessory_assets->size());for(const auto& asset:*state.accessory_assets)writer.text(asset);}
        }
    }

    if(version>=7)writer.boolean(entity.dead);
    if(version>=8){writer.boolean(entity.name_override.has_value());if(entity.name_override)writer.text(*entity.name_override);}
    if(version>=9)writer.u8(!entity.suppress_referenced_reset?0:*entity.suppress_referenced_reset?2:1);
    if(version>=10)writer.u8(!entity.skip_native_recovery_position?0:*entity.skip_native_recovery_position?2:1);
    if(version>=11){writer.boolean(entity.native_slot0.has_value());if(entity.native_slot0){writer.i16(entity.native_slot0->id);writer.u64(entity.native_slot0->generation);}}
    if(version>=12){writer.boolean(entity.native_out_of_world_level.has_value());if(entity.native_out_of_world_level)writer.i16(*entity.native_out_of_world_level);}
}

Entity read_entity(Reader& reader, const WorldSnapshotCodecLimits& limits,std::uint16_t version) {
    Entity entity{};
    entity.id = reader.i16();
    const auto kind = reader.u8();
    if (kind > static_cast<std::uint8_t>(EntityKind::avatar))
        throw FormatError("invalid World snapshot entity kind");
    entity.kind = static_cast<EntityKind>(kind);
    entity.generation = reader.u64();
    entity.attributes = read_i16_vector(reader, limits.world.bank_words);
    entity.person_data = read_i16_vector(reader, limits.world.bank_words);
    entity.slots = read_i16_vector(reader, limits.world.bank_words);
    auto count = reader.count(limits.world.bank_words, 4);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto key = reader.i16();
        const auto value = reader.i16();
        if (!entity.object_words.emplace(key, value).second)
            throw FormatError("duplicate World snapshot object word");
    }

    if (reader.boolean()) {
        const auto container = reader.i16();
        const auto slot = reader.size(limits.world.bank_words);
        const auto positioned = reader.boolean();
        entity.attachment = Attachment{container, slot, positioned};
    }
    if (reader.boolean()) {
        entity.position = EntityPosition{reader.i32(), reader.i32(), reader.i16(), reader.u8()};
    }

    if (reader.boolean()) {
        EntityType type{};
        type.guid = reader.u32();
        if (reader.boolean()) type.master_guid = reader.u32();
        entity.definition = type;
    }
    entity.motives = read_i16_vector(reader, limits.world.bank_words);
    count = reader.count(limits.world.bank_words, 4);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto key = reader.i16();
        const auto value = reader.i16();
        if (!entity.definition_words.emplace(key, value).second)
            throw FormatError("duplicate World snapshot definition word");
    }

    if (reader.boolean()) {
        const auto entry_count = reader.count(256, 4);
        std::vector<EntryPoint> entries;
        entries.reserve(entry_count);
        for (std::uint32_t i = 0; i < entry_count; ++i)
            entries.push_back({reader.u16(), reader.u16()});
        entity.entry_points = std::move(entries);
    }

    if (reader.boolean()) {
        SavedObjectBanks banks{};
        for (auto& value : banks.object_data) value = reader.i16();
        for (auto& value : banks.extras) value = reader.i16();
        entity.saved_object_banks = banks;
    }
    entity.unattached_slot = reader.i16();
    entity.static_footprint = reader.boolean();
    entity.ui_ghost = reader.boolean();
    if (reader.boolean()) entity.kill_timeout = reader.i32();
    entity.radian_direction = reader.f32();
    if(version>=2&&reader.boolean()){
        entity.motive_changes.resize(16);
        for(auto& change:entity.motive_changes){
            change.per_hour_change=reader.i16();
            change.maximum=reader.i16();
            change.fractional=reader.f64();
            // Simitone does not serialize VMMotiveChange.Ticked.
            change.ticked=false;
        }
    }
    if(version>=3){
        const auto appearances=reader.count(limits.world.bound_appearance_names,4);
        entity.bound_appearances.reserve(appearances);
        for(std::uint32_t i=0;i<appearances;++i)
            entity.bound_appearances.push_back(reader.text(256));
    }
    if(version>=4)entity.dynamic_sprite_flags=reader.u64();
    if(version>=5&&reader.boolean()){
        EntityLockoutCountState state;state.thread_present=reader.boolean();
        if(reader.boolean())state.timestamp=reader.u32();
        entity.lockout_count=state;
    }
    if(version>=6&&reader.boolean()){
        AvatarAppearanceState state;
        if(reader.boolean())state.body=read_outfit(reader);
        if(reader.boolean())state.default_daywear=read_outfit(reader);
        if(reader.boolean()){const auto count=reader.count(limits.world.bound_appearance_names,4);
            state.accessory_assets=std::vector<std::string>{};state.accessory_assets->reserve(count);
            for(std::uint32_t n=0;n<count;++n)state.accessory_assets->push_back(reader.text(256));}
        entity.avatar_appearance=std::move(state);
    }
    if(version>=7)entity.dead=reader.boolean();
    if(version>=8&&reader.boolean())entity.name_override=reader.text(128);
    if(version>=9){
        const auto policy=reader.u8();
        if(policy>2)throw FormatError("invalid World snapshot referenced-reset policy tag");
        if(policy)entity.suppress_referenced_reset=policy==2;
    }
    if(version>=10){
        const auto policy=reader.u8();
        if(policy>2)throw FormatError("invalid World snapshot native-recovery-position policy tag");
        if(policy)entity.skip_native_recovery_position=policy==2;
    }
    if(version>=11&&reader.boolean())entity.native_slot0=WorldEntityReference{reader.i16(),reader.u64()};
    if(version>=12&&reader.boolean())entity.native_out_of_world_level=reader.i16();
    return entity;
}

void validate_snapshot(const WorldSnapshot& snapshot, WorldLimits limits) {
    for (std::size_t i = 1; i < snapshot.entities.size(); ++i)
        if (snapshot.entities[i - 1].id >= snapshot.entities[i].id)
            throw FormatError("World snapshot entities are not in canonical ID order");
    World candidate(limits);
    if (!candidate.restore_snapshot(snapshot))
        throw FormatError("invalid World snapshot state");
}
}

std::vector<std::uint8_t> encode_world_snapshot(
        const WorldSnapshot& snapshot, WorldSnapshotCodecLimits limits) {
    if (limits.max_bytes < minimum_header_size)
        throw FormatError("World snapshot byte limit is too small");
    validate_snapshot(snapshot, limits.world);
    Writer writer(limits.max_bytes);
    const bool appearance=std::any_of(snapshot.entities.begin(),snapshot.entities.end(),[](const auto& entity){return entity.avatar_appearance.has_value();});
    const bool marked=std::any_of(snapshot.entities.begin(),snapshot.entities.end(),[](const auto& entity){return entity.dead;});
    const auto named=[](const auto& e){return e.name_override.has_value();};
    const bool names=std::any_of(snapshot.entities.begin(),snapshot.entities.end(),named)||std::any_of(snapshot.retained_entities.begin(),snapshot.retained_entities.end(),named);
    const auto has_reset_policy=[](const auto& entity){return entity.suppress_referenced_reset.has_value();};
    const bool reset_policy=std::any_of(snapshot.entities.begin(),snapshot.entities.end(),has_reset_policy)||
        std::any_of(snapshot.retained_entities.begin(),snapshot.retained_entities.end(),has_reset_policy);
    const auto has_position_policy=[](const auto& entity){return entity.skip_native_recovery_position.has_value();};
    const bool position_policy=std::any_of(snapshot.entities.begin(),snapshot.entities.end(),has_position_policy)||
        std::any_of(snapshot.retained_entities.begin(),snapshot.retained_entities.end(),has_position_policy);
    const auto has_native_slot=[](const auto& entity){return entity.native_slot0.has_value();};
    const bool native_slots=std::any_of(snapshot.entities.begin(),snapshot.entities.end(),has_native_slot)||std::any_of(snapshot.retained_entities.begin(),snapshot.retained_entities.end(),has_native_slot);
    const auto has_native_oow=[](const auto& entity){return entity.native_out_of_world_level.has_value();};
    const bool native_oow=std::any_of(snapshot.entities.begin(),snapshot.entities.end(),has_native_oow)||std::any_of(snapshot.retained_entities.begin(),snapshot.retained_entities.end(),has_native_oow);
    const auto version=static_cast<std::uint16_t>(native_oow?12:native_slots?11:position_policy?10:reset_policy?9:names?8:!snapshot.retained_entities.empty()||marked?7:appearance?6:snapshot.lockout_count_clock?5:4);
    writer.u16(version);
    writer.u8(static_cast<std::uint8_t>(snapshot.attribute_policy));
    writer.u64(static_cast<std::uint64_t>(snapshot.word_count));
    writer.u64(snapshot.generation_sequence);
    writer.u64(snapshot.spatial_revision);
    if(version>=6)writer.boolean(snapshot.lockout_count_clock.has_value());
    if(snapshot.lockout_count_clock)writer.u32(*snapshot.lockout_count_clock);
    writer.count(snapshot.entities.size());
    for (const auto& entity : snapshot.entities) write_entity(writer, entity,version);
    if(version>=7){writer.count(snapshot.retained_entities.size());for(const auto& body:snapshot.retained_entities)write_entity(writer,body,version);}
    return writer.take();
}

WorldSnapshot decode_world_snapshot(
        const std::vector<std::uint8_t>& bytes, WorldSnapshotCodecLimits limits) {
    if (limits.max_bytes < minimum_header_size || bytes.size() > limits.max_bytes)
        throw FormatError("World snapshot exceeds byte limit");
    if (bytes.size() < minimum_header_size)
        throw FormatError("World snapshot header is truncated");
    Reader reader(bytes);
    const auto version=reader.u16();
    if(version<1||version>codec_version)throw FormatError("unsupported World snapshot version");
    WorldSnapshot snapshot;
    const auto policy = reader.u8();
    if (policy > static_cast<std::uint8_t>(AttributePolicy::ts1_bounded))
        throw FormatError("invalid World snapshot attribute policy");
    snapshot.attribute_policy = static_cast<AttributePolicy>(policy);
    snapshot.word_count = reader.size(limits.world.words);
    snapshot.generation_sequence = reader.u64();
    snapshot.spatial_revision = reader.u64();
    if(version>=6){if(reader.boolean())snapshot.lockout_count_clock=reader.u32();}
    else if(version>=5)snapshot.lockout_count_clock=reader.u32();
    const auto count = reader.count(limits.world.entities, version>=5?50:49);
    snapshot.entities.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        snapshot.entities.push_back(read_entity(reader, limits,version));
    if(version>=7){
        const auto retained=reader.count(limits.world.entities-count,50);
        if(version==7&&!retained&&!std::any_of(snapshot.entities.begin(),snapshot.entities.end(),[](const auto& entity){return entity.dead;}))
            throw FormatError("empty retained-body extension is noncanonical");
        snapshot.retained_entities.reserve(retained);
        for(std::uint32_t n=0;n<retained;++n)snapshot.retained_entities.push_back(read_entity(reader,limits,version));
    }
    if(version==8&&!std::any_of(snapshot.entities.begin(),snapshot.entities.end(),[](const auto& e){return e.name_override.has_value();})&&!std::any_of(snapshot.retained_entities.begin(),snapshot.retained_entities.end(),[](const auto& e){return e.name_override.has_value();}))throw FormatError("empty name extension is noncanonical");
    if(version==9){
        const auto known_policy=[](const auto& e){return e.suppress_referenced_reset.has_value();};
        if(!std::any_of(snapshot.entities.begin(),snapshot.entities.end(),known_policy)&&
           !std::any_of(snapshot.retained_entities.begin(),snapshot.retained_entities.end(),known_policy))
            throw FormatError("empty referenced-reset policy extension is noncanonical");
    }
    if(version==10){
        const auto known_policy=[](const auto& e){return e.skip_native_recovery_position.has_value();};
        if(!std::any_of(snapshot.entities.begin(),snapshot.entities.end(),known_policy)&&
           !std::any_of(snapshot.retained_entities.begin(),snapshot.retained_entities.end(),known_policy))
            throw FormatError("empty native-recovery-position policy extension is noncanonical");
    }
    if(version==11){
        const auto known=[](const auto& e){return e.native_slot0.has_value();};
        if(!std::any_of(snapshot.entities.begin(),snapshot.entities.end(),known)&&!std::any_of(snapshot.retained_entities.begin(),snapshot.retained_entities.end(),known))
            throw FormatError("empty native-slot-zero extension is noncanonical");
    }
    if(version==12){
        const auto known=[](const auto& e){return e.native_out_of_world_level.has_value();};
        if(!std::any_of(snapshot.entities.begin(),snapshot.entities.end(),known)&&!std::any_of(snapshot.retained_entities.begin(),snapshot.retained_entities.end(),known))
            throw FormatError("empty native-out-of-world extension is noncanonical");
    }
    if (!reader.done()) throw FormatError("World snapshot has unclaimed bytes");
    validate_snapshot(snapshot, limits.world);
    return snapshot;
}

} // namespace odh
