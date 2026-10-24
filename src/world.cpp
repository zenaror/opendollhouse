#include "odh/world.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace odh {
namespace {
bool valid_index(std::int16_t index, std::size_t size) {
    return index >= 0 && static_cast<std::size_t>(index) < size;
}
bool derived(std::int16_t index) { return index == 2 || index == 3 || index == 11 || index == 67; }
bool valid_position(EntityPosition position) {
    return position.x>=0 && position.y>=0 && position.x<=32767 && position.y<=32767 &&
           position.level>=1 && position.direction<=7;
}
bool valid_appearance_name(const std::string& name) {
    if(name.size()<5||name.size()>256||name.substr(name.size()-4)!=".apr")return false;
    for(const unsigned char c:name){
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;
    }
    return true;
}
std::uint8_t direction_notch(float radians) {
    constexpr double pi = 3.141592653589793238462643383279502884;
    const auto notch = static_cast<double>(radians) / (pi / 4.0);
    const auto lower = std::floor(notch);
    const auto fraction = notch - lower;
    // Match Math.Round's midpoint-to-even rule without inheriting the process
    // floating-point rounding mode as nearbyint would.
    const auto rounded = fraction < 0.5 ? lower
                       : fraction > 0.5 ? lower + 1.0
                       : std::fmod(std::abs(lower), 2.0) == 0.0 ? lower : lower + 1.0;
    auto modulo = std::fmod(rounded, 8.0);
    if (modulo < 0.0) modulo += 8.0;
    return static_cast<std::uint8_t>(modulo);
}
float radians_for_notch(std::uint8_t direction) {
    constexpr float pi_over_four = 0.78539816339744830962f;
    return static_cast<float>(direction) * pi_over_four;
}
std::int16_t scope_entity(const VmFrame& frame, std::uint8_t scope) {
    if (scope == 35) return frame.context.stack_object;
    return scope == 0 || scope == 3 || scope == 14 || scope == 18 || scope == 20
        ? frame.context.caller : frame.context.stack_object;
}
}
WorldSnapshot World::snapshot() const {
    WorldSnapshot result;
    result.attribute_policy=attribute_policy_;
    result.word_count=words_;
    result.generation_sequence=generation_sequence_;
    result.spatial_revision=spatial_revision_;
    result.entities.reserve(entities_.size());
    for(const auto& [id,entity]:entities_) result.entities.push_back(entity);
    return result;
}
bool World::restore_snapshot(const WorldSnapshot& snapshot) {
    if(!entities_.empty() || words_!=0 || generation_sequence_!=0 || spatial_revision_!=0)
        return false;
    return publish_snapshot(snapshot);
}
bool World::publish_snapshot(const WorldSnapshot& snapshot) {
    if(snapshot.generation_sequence<generation_sequence_ ||
       snapshot.spatial_revision<spatial_revision_) return false;
    if(snapshot.entities.size()>limits_.entities ||
       (snapshot.attribute_policy!=AttributePolicy::fixed &&
        snapshot.attribute_policy!=AttributePolicy::ts1_bounded)) return false;
    auto available=limits_.words;
    std::size_t appearance_name_bytes=0;
    // Bound all banks before copying. ObjectData follows its fixed index range,
    // as declare_object_word does, rather than the variable bank_words limit.
    for(const auto& entity:snapshot.entities) {
        if(entity.id<=0 || !entity.generation || entity.generation>snapshot.generation_sequence ||
           (entity.kind!=EntityKind::object && entity.kind!=EntityKind::avatar) ||
           entity.attributes.size()>limits_.bank_words || entity.attributes.size()>32768 ||
           entity.person_data.size()>limits_.bank_words || entity.person_data.size()>32768 ||
           entity.slots.size()>limits_.bank_words || entity.slots.size()>32767 ||
           entity.motives.size()>limits_.bank_words || entity.motives.size()>16 ||
           entity.bound_appearances.size()>limits_.bound_appearance_names ||
           (!entity.bound_appearances.empty()&&entity.kind!=EntityKind::avatar) ||
           (!entity.motive_changes.empty() &&
            (entity.kind!=EntityKind::avatar || entity.motives.size()<16 || entity.motive_changes.size()!=16)) ||
           entity.definition_words.size()>limits_.bank_words ||
           (entity.kind!=EntityKind::avatar &&
            (!entity.person_data.empty() || !entity.motives.empty() || entity.kill_timeout)) ||
           (entity.kill_timeout && *entity.kill_timeout < -1) ||
           !std::isfinite(entity.radian_direction) ||
           (entity.position && !valid_position(*entity.position)) ||
           (entity.entry_points && (!entity.definition || entity.entry_points->size()>256 ||
                                    entity.entry_points->size()>limits_.bank_words/2)) ||
           (entity.saved_object_banks &&
            (limits_.bank_words<68 || entity.saved_object_banks->object_data[1]<0 ||
             entity.saved_object_banks->object_data[1]>7))) return false;
        const auto current=entities_.find(entity.id);
        if((current!=entities_.end() && current->second.generation!=entity.generation) ||
           (current==entities_.end() && entity.generation<=generation_sequence_)) return false;
        for(const auto& [index,value]:entity.object_words)
            if(index<0 || index>=79 || derived(index)) return false;
        for(const auto& [index,value]:entity.definition_words)
            if(index<0 || index>255) return false;
        for(const auto& change:entity.motive_changes)
            if(!std::isfinite(change.fractional))return false;
        if(!std::is_sorted(entity.bound_appearances.begin(),entity.bound_appearances.end())||
           std::adjacent_find(entity.bound_appearances.begin(),entity.bound_appearances.end())!=entity.bound_appearances.end())return false;
        for(const auto& name:entity.bound_appearances){
            if(!valid_appearance_name(name)||name.size()>limits_.bound_appearance_name_bytes-appearance_name_bytes)return false;
            appearance_name_bytes+=name.size();
        }
        if(entity.saved_object_banks) {
            if(!entity.definition) return false;
            for(std::int16_t index=0;index<68;++index)
                if(!derived(index) && !entity.object_words.count(index)) return false;
        } else if(entity.unattached_slot!=-1) return false;
        for(const auto count:{entity.attributes.size(),entity.person_data.size(),entity.slots.size(),
                              entity.motives.size(),entity.object_words.size(),entity.definition_words.size(),
                              entity.entry_points?2*entity.entry_points->size():std::size_t{0},
                              entity.saved_object_banks?std::size_t{73}:std::size_t{0}}) {
            if(count>available) return false;
            available-=count;
        }
    }
    if(snapshot.word_count!=limits_.words-available) return false;
    std::map<std::int16_t,Entity> candidate;
    std::map<std::uint64_t,std::int16_t> generations;
    for(const auto& entity:snapshot.entities) {
        if(!candidate.emplace(entity.id,entity).second ||
           !generations.emplace(entity.generation,entity.id).second) return false;
    }
    for(const auto& [id,entity]:candidate) {
        for(std::size_t slot=0;slot<entity.slots.size();++slot) {
            const auto occupant=entity.slots[slot];
            if(!occupant) continue;
            const auto child=candidate.find(occupant);
            if(child==candidate.end() || !child->second.attachment ||
               child->second.attachment->container!=id || child->second.attachment->slot!=slot) return false;
        }
        if(entity.attachment) {
            const auto& attachment=*entity.attachment;
            const auto parent=candidate.find(attachment.container);
            if(parent==candidate.end() || attachment.slot>=parent->second.slots.size() ||
               parent->second.slots[attachment.slot]!=id) return false;
            if(attachment.positioned && (!entity.position || !parent->second.position ||
               entity.position->x!=parent->second.position->x ||
               entity.position->y!=parent->second.position->y ||
               entity.position->level!=parent->second.position->level)) return false;
        }
        // Every ancestry chain must terminate, including unpositioned links.
        auto ancestor=&entity;
        std::size_t depth=0;
        while(ancestor->attachment) {
            if(++depth>candidate.size()) return false;
            const auto parent=candidate.find(ancestor->attachment->container);
            if(parent==candidate.end()) return false;
            ancestor=&parent->second;
        }
    }
    entities_.swap(candidate);
    words_=snapshot.word_count;
    generation_sequence_=snapshot.generation_sequence;
    spatial_revision_=snapshot.spatial_revision;
    attribute_policy_=snapshot.attribute_policy;
    return true;
}
void World::mark_spatial_change() noexcept {
    if(spatial_revision_!=std::numeric_limits<std::uint64_t>::max())++spatial_revision_;
}
bool World::create(std::int16_t id, EntityKind kind, EntityLayout layout) {
    if (id <= 0 || generation_sequence_==std::numeric_limits<std::uint64_t>::max() ||
        (kind != EntityKind::object && kind != EntityKind::avatar) ||
        entities_.count(id) || entities_.size() >= limits_.entities ||
        layout.attributes > limits_.bank_words || layout.person_data > limits_.bank_words ||
        layout.slots > limits_.bank_words || layout.motives > limits_.bank_words ||
        layout.motives > 16 || layout.attributes > 32768 ||
        layout.person_data > 32768 || layout.slots > 32767 ||
        (kind != EntityKind::avatar && (layout.person_data != 0 || layout.motives != 0))) return false;
    // Subtract before summing, so even caller-provided limits cannot overflow.
    auto available = limits_.words - words_;
    for (const auto count : {layout.attributes, layout.person_data, layout.slots, layout.motives}) {
        if (count > available) return false;
        available -= count;
    }
    const auto count = layout.attributes + layout.person_data + layout.slots + layout.motives;
    entities_.emplace(id, Entity{id,kind,std::vector<std::int16_t>(layout.attributes),
        std::vector<std::int16_t>(layout.person_data),std::vector<std::int16_t>(layout.slots),{},
        std::nullopt,std::nullopt,++generation_sequence_,{},std::vector<std::int16_t>(layout.motives),
        {},{},{},std::int16_t{-1},true,false,std::nullopt,0.0f,{}, {}});
    if (kind == EntityKind::avatar) entities_.at(id).kill_timeout = -1;
    words_ += count;
    mark_spatial_change();
    return true;
}
bool World::create_runtime_object(std::int16_t id, std::uint32_t guid,
    EntityLayout layout, const std::vector<EntryPoint>& entry_points) {
    constexpr std::size_t runtime_object_words = 75; // 0..78 except derived 2,3,11,67.
    if (id <= 0 || guid == 0 || entities_.count(id) ||
        generation_sequence_ == std::numeric_limits<std::uint64_t>::max() ||
        layout.person_data != 0 || layout.motives != 0 ||
        layout.attributes > limits_.bank_words || layout.attributes > 32768 ||
        layout.slots > limits_.bank_words || layout.slots > 32767 ||
        runtime_object_words > limits_.bank_words || entry_points.size() > 256 ||
        entry_points.size() > limits_.bank_words / 2) return false;

    auto available = limits_.words - words_;
    for (const auto count : {layout.attributes, layout.slots, runtime_object_words,
                             2 * entry_points.size()}) {
        if (count > available) return false;
        available -= count;
    }

    auto candidate = snapshot();
    Entity entity{};
    entity.id = id;
    entity.kind = EntityKind::object;
    entity.attributes.resize(layout.attributes, 0);
    entity.slots.resize(layout.slots, 0);
    entity.generation = generation_sequence_ + 1;
    entity.definition = EntityType{guid, std::nullopt};
    entity.entry_points = entry_points;
    for (std::int16_t word = 0; word <= 78; ++word) {
        if (!derived(word)) entity.object_words.emplace(word, 0);
    }
    candidate.entities.push_back(std::move(entity));
    candidate.word_count = words_ + (limits_.words - words_ - available);
    candidate.generation_sequence = generation_sequence_ + 1;
    if (candidate.spatial_revision != std::numeric_limits<std::uint64_t>::max())
        ++candidate.spatial_revision;
    return publish_snapshot(candidate);
}
bool World::create_saved_entity(std::int16_t id, EntityKind kind, std::uint32_t guid,
    const std::vector<std::int16_t>& attributes,
    const std::vector<std::int16_t>& person_data,
    const std::vector<std::int16_t>& motives,
    const std::array<std::int16_t,68>& object_data,
    const std::array<std::int16_t,5>& extras, std::size_t slots,
    std::optional<std::uint32_t> master_guid) {
    if (id<=0 || !guid || (master_guid && !*master_guid) ||
        generation_sequence_==std::numeric_limits<std::uint64_t>::max() ||
        (kind!=EntityKind::object && kind!=EntityKind::avatar) ||
        entities_.count(id) || entities_.size()>=limits_.entities ||
        attributes.size()>limits_.bank_words || person_data.size()>limits_.bank_words ||
        motives.size()>limits_.bank_words || slots>limits_.bank_words ||
        object_data.size()>limits_.bank_words || extras.size()>limits_.bank_words ||
        attributes.size()>32768 || person_data.size()>32768 || slots>32767 ||
        motives.size()>16 || object_data[1]<0 || object_data[1]>7 ||
        (kind!=EntityKind::avatar && (!person_data.empty() || !motives.empty()))) return false;
    auto available=limits_.words-words_;
    for (const auto count : {attributes.size(),person_data.size(),motives.size(),slots,
                             object_data.size(),extras.size(),std::size_t{64}}) {
        if (count>available) return false;
        available-=count;
    }
    const auto count=limits_.words-words_-available;
    const auto generation=generation_sequence_+1;
    Entity candidate{id,kind,attributes,person_data,std::vector<std::int16_t>(slots),{},
        {},{},generation,EntityType{guid,master_guid},motives,{},{},
        SavedObjectBanks{object_data,extras},object_data[2]==0 ? object_data[3] : std::int16_t{-1},
        true,false,std::nullopt,0.0f,{}, {}};
    for (std::int16_t index=0;index<68;++index)
        if (!derived(index)) candidate.object_words.emplace(index,object_data[static_cast<std::size_t>(index)]);
    entities_.emplace(id,std::move(candidate));
    if (kind == EntityKind::avatar) entities_.at(id).kill_timeout = -1;
    generation_sequence_=generation;
    words_+=count;
    mark_spatial_change();
    return true;
}
const Entity* World::find(std::int16_t id) const {
    const auto found = entities_.find(id);
    return found == entities_.end() ? nullptr : &found->second;
}
std::optional<std::int16_t> World::next_entity_of_guid(std::int16_t current,std::uint32_t guid) const {
    for(auto it=entities_.upper_bound(current);it!=entities_.end();++it)
        if(!it->second.ui_ghost&&it->second.definition&&it->second.definition->guid==guid)return it->first;
    return {};
}
std::optional<std::int16_t> World::next_entity(std::int16_t current,std::optional<EntityKind> kind) const {
    for(auto it=entities_.upper_bound(current);it!=entities_.end();++it)
        if(!it->second.ui_ghost&&(!kind || it->second.kind==*kind))return it->first;
    return {};
}
std::optional<std::int16_t> World::next_family_member(std::int16_t current) const {
    constexpr std::size_t family_word=61; // VMPersonDataVariable.TS1FamilyNumber
    const auto cursor=entities_.find(current);
    if(cursor==entities_.end()||cursor->second.ui_ghost||cursor->second.kind!=EntityKind::avatar||
       cursor->second.person_data.size()<=family_word)return {};
    const auto family=cursor->second.person_data[family_word];
    const auto matches=[&](const Entity& entity){
        return !entity.ui_ghost&&entity.kind==EntityKind::avatar&&
               entity.person_data.size()>family_word&&entity.person_data[family_word]==family;
    };
    for(auto it=entities_.upper_bound(current);it!=entities_.end();++it)
        if(matches(it->second))return it->first;
    for(const auto& [id,entity]:entities_)
        if(matches(entity))return id;
    return {};
}
std::optional<std::int16_t> World::next_entity_on_same_tile(std::int16_t current) const {
    const auto* anchor=find(current);
    if(!anchor||anchor->ui_ghost||!anchor->position||anchor->position->x<0||anchor->position->y<0||
       anchor->position->level<1||anchor->position->level>2)return {};
    const auto tile=[](std::int32_t coordinate){
        return coordinate>=0?coordinate/16:-static_cast<std::int32_t>((-static_cast<std::int64_t>(coordinate)+15)/16);
    };
    const auto same_tile=[&](const Entity& entity){
        if(!entity.position||entity.position->level!=anchor->position->level)return false;
        const auto& a=*anchor->position;const auto& b=*entity.position;
        return tile(a.x)==tile(b.x)&&tile(a.y)==tile(b.y);
    };
    std::optional<std::int16_t> first,after;
    bool anchor_present=false;
    for(const auto& [id,entity]:entities_){
        if(entity.ui_ghost)continue;
        if(!same_tile(entity))continue;
        if(!first)first=id;
        if(id==current)anchor_present=true;
        if(id>current&&!after)after=id;
    }
    if(after)return after;
    return anchor_present?first:std::nullopt;
}
std::optional<std::int16_t> World::next_entity_adjacent_to_local(std::int16_t anchor_id,
                                                                  std::int16_t current) const {
    const auto* anchor=find(anchor_id);
    if(!anchor||anchor->ui_ghost||!anchor->position||anchor->position->level<1||
       anchor->position->level>2)return {};
    const auto tile=[](std::int32_t coordinate){
        return coordinate>=0?coordinate/16:-static_cast<std::int32_t>(
            (-static_cast<std::int64_t>(coordinate)+15)/16);
    };
    const auto anchor_x=tile(anchor->position->x),anchor_y=tile(anchor->position->y);
    int previous=-1;
    if(const auto* pointer=find(current);pointer&&!pointer->ui_ghost&&pointer->position){
        const auto dx=tile(pointer->position->x)-anchor_x;
        const auto dy=tile(pointer->position->y)-anchor_y;
        previous=dx==0?(dy<0?0:2):(dx<0?3:1);
    }
    if(previous==3)return {};
    constexpr std::array<std::array<std::int32_t,2>,4> steps{{
        {{0,-1}},{{1,0}},{{0,1}},{{-1,0}}
    }};
    for(int direction=previous+1;direction<4;++direction){
        const auto x=anchor_x+steps[static_cast<std::size_t>(direction)][0];
        const auto y=anchor_y+steps[static_cast<std::size_t>(direction)][1];
        for(const auto& [id,entity]:entities_){
            if(entity.ui_ghost||!entity.position||entity.position->level!=anchor->position->level)continue;
            if(tile(entity.position->x)==x&&tile(entity.position->y)==y)return id;
        }
    }
    return {};
}
bool World::define_type(std::int16_t id,std::uint32_t guid,std::optional<std::uint32_t> master_guid) {
    const auto found=entities_.find(id);if(found==entities_.end())return false;
    auto& type=found->second.definition;
    if(type)return type->guid==guid&&type->master_guid==master_guid;
    type=EntityType{guid,master_guid};mark_spatial_change();return true;
}
bool World::define_entry_points(std::int16_t id,const std::vector<EntryPoint>& values) {
    const auto found=entities_.find(id);
    if(found==entities_.end() || !found->second.definition || values.size()>256) return false;
    auto& table=found->second.entry_points;
    if(table) return table->size()==values.size() &&
        std::equal(table->begin(),table->end(),values.begin(),[](const auto& a,const auto& b) {
            return a.condition==b.condition && a.action==b.action;
        });
    // Divide limits before multiplying the bounded count, avoiding overflow.
    if(values.size()>limits_.bank_words/2 || values.size()>(limits_.words-words_)/2) return false;
    auto owned=values;
    table.emplace(std::move(owned));
    words_+=2*values.size();
    mark_spatial_change();
    return true;
}
bool World::declare_object_word(std::int16_t id, std::int16_t index, std::int16_t value) {
    const auto found = entities_.find(id);
    // Current TS1 reference bank; FSO's extension query 79 is outside this profile.
    if (found == entities_.end() || index < 0 || index >= 79 || derived(index)) return false;
    auto& bank = found->second.object_words;
    const auto field = bank.find(index);
    if (field != bank.end()) { if(field->second!=value){field->second = value;mark_spatial_change();} return true; }
    if (words_ >= limits_.words) return false;
    bank.emplace(index,value); ++words_;mark_spatial_change();return true;
}
bool World::set_person_data(std::int16_t id, std::int16_t index, std::int16_t value) {
    const auto found = entities_.find(id);
    if (found == entities_.end() || found->second.kind != EntityKind::avatar ||
        !valid_index(index,found->second.person_data.size())) return false;
    auto& old=found->second.person_data[static_cast<std::size_t>(index)];
    if(old!=value){old=value;mark_spatial_change();}return true;
}
bool World::replace_person_data(std::int16_t id, const std::vector<std::int16_t>& values) {
    const auto found = entities_.find(id);
    if (found == entities_.end() || found->second.kind != EntityKind::avatar ||
        values.size() > limits_.bank_words || values.size() > 32768) return false;
    auto& bank = found->second.person_data;
    const auto retained_words = words_ - bank.size();
    if (values.size() > limits_.words - retained_words) return false;
    const auto replacement_size = values.size();
    auto replacement = values; // Copy before publication, including self-aliasing input.
    bank.swap(replacement);
    words_ = retained_words + replacement_size;
    mark_spatial_change();
    return true;
}
bool World::add_bound_appearance(std::int16_t avatar,const std::string& name) {
    const auto found=entities_.find(avatar);
    if(found==entities_.end()||found->second.kind!=EntityKind::avatar||!valid_appearance_name(name))return false;
    auto& names=found->second.bound_appearances;
    const auto at=std::lower_bound(names.begin(),names.end(),name);
    if(at!=names.end()&&*at==name)return true;
    std::size_t bytes=name.size();
    for(const auto& existing:names)bytes+=existing.size();
    if(names.size()>=limits_.bound_appearance_names||bytes>limits_.bound_appearance_name_bytes)return false;
    names.insert(at,name);return true;
}
bool World::remove_bound_appearance(std::int16_t avatar,const std::string& name) {
    const auto found=entities_.find(avatar);
    if(found==entities_.end()||found->second.kind!=EntityKind::avatar||!valid_appearance_name(name))return false;
    auto& names=found->second.bound_appearances;
    const auto at=std::lower_bound(names.begin(),names.end(),name);
    if(at!=names.end()&&*at==name)names.erase(at);
    return true;
}
bool World::set_motive(std::int16_t id, std::int16_t index, std::int16_t value) {
    const auto found = entities_.find(id);
    if (found == entities_.end() || found->second.kind != EntityKind::avatar ||
        !valid_index(index,found->second.motives.size())) return false;
    auto& old = found->second.motives[static_cast<std::size_t>(index)];
    const auto updated=std::clamp(value,static_cast<std::int16_t>(-100),
                     std::max(old,static_cast<std::int16_t>(100)));
    // Motives do not change room geometry or route obstacles.
    if(old!=updated)old=updated;
    return true;
}
namespace {
std::int16_t wrap_motive_word(std::int32_t value) {
    const auto bits=static_cast<std::uint16_t>(static_cast<std::uint32_t>(value));
    const auto signed_value=bits<0x8000U?static_cast<std::int32_t>(bits):
        static_cast<std::int32_t>(bits)-0x10000;
    return static_cast<std::int16_t>(signed_value);
}
std::int16_t wrap_motive_double(double value) {
    const auto integral=std::fmod(std::trunc(value),65536.0);
    const auto normalized=integral<0.0?integral+65536.0:integral;
    return wrap_motive_word(static_cast<std::int32_t>(normalized));
}
}
bool World::set_motive_change(std::int16_t id,std::int16_t motive,
                              std::int16_t per_hour_change,std::int16_t maximum) {
    const auto found=entities_.find(id);
    if(found==entities_.end()||found->second.kind!=EntityKind::avatar||
       !valid_index(motive,16)||found->second.motives.size()<16)return false;
    auto& changes=found->second.motive_changes;
    if(changes.empty())changes.resize(16);
    if(changes.size()!=16)return false;
    auto& change=changes[static_cast<std::size_t>(motive)];
    if(change.ticked){
        change.per_hour_change=per_hour_change;
        change.maximum=maximum;
        change.ticked=false;
    }
    return true;
}
bool World::clear_motive_changes(std::int16_t id) {
    const auto found=entities_.find(id);
    if(found==entities_.end()||found->second.kind!=EntityKind::avatar)return false;
    for(auto& change:found->second.motive_changes){
        change.per_hour_change=0;
        change.maximum=INT16_MAX;
    }
    return true;
}
bool World::tick_motive_changes() {
    for(auto& [id,entity]:entities_){
        if(entity.kind!=EntityKind::avatar||entity.motives.size()<16)continue;
        if(entity.motive_changes.empty())entity.motive_changes.resize(16);
        if(entity.motive_changes.size()!=16)return false;
        for(std::size_t index=0;index<entity.motive_changes.size();++index){
            auto& change=entity.motive_changes[index];
            change.ticked=true;
            if(change.per_hour_change==0)continue;
            const auto rate=(static_cast<double>(change.per_hour_change)/60.0)/30.0;
            change.fractional+=rate;
            if(std::abs(change.fractional)<1.0)continue;
            auto& current=entity.motives[index];
            if((rate>0.0&&current>change.maximum)||(rate<0.0&&current<change.maximum))continue;
            const auto delta=wrap_motive_double(change.fractional);
            change.fractional=std::fmod(change.fractional,1.0);
            auto next=wrap_motive_word(static_cast<std::int32_t>(current)+delta);
            if((rate>0.0&&next>change.maximum)||(rate<0.0&&next<change.maximum))
                next=change.maximum;
            if(!set_motive(id,static_cast<std::int16_t>(index),next))return false;
        }
    }
    return true;
}
bool World::has_motive_change(std::int16_t id,std::int16_t motive) const {
    const auto found=entities_.find(id);
    return found!=entities_.end()&&found->second.kind==EntityKind::avatar&&
        valid_index(motive,found->second.motive_changes.size())&&
        found->second.motive_changes[static_cast<std::size_t>(motive)].per_hour_change!=0;
}
bool World::declare_definition_word(std::int16_t id, std::int16_t index, std::int16_t value) {
    const auto found = entities_.find(id);
    if (found == entities_.end() || index < 0 || index > 255) return false;
    auto& bank = found->second.definition_words;
    const auto field = bank.find(index);
    if (field != bank.end()) { if(field->second!=value)return false; return true; }
    if (bank.size() >= limits_.bank_words || words_ >= limits_.words) return false;
    bank.emplace(index,value); ++words_;mark_spatial_change();return true;
}
bool World::set_static_footprint(std::int16_t id, bool is_static) {
    const auto found=entities_.find(id);
    if(found==entities_.end()) return false;
    if(found->second.static_footprint!=is_static){found->second.static_footprint=is_static;mark_spatial_change();}
    return true;
}
bool World::set_ui_ghost(std::int16_t id,bool is_ghost) {
    const auto found=entities_.find(id);
    if(found==entities_.end())return false;
    if(found->second.ui_ghost!=is_ghost){found->second.ui_ghost=is_ghost;mark_spatial_change();}
    return true;
}
std::optional<bool> World::dynamic_sprite_flag(std::int16_t id,std::uint16_t flag) const {
    const auto found=entities_.find(id);
    if(found==entities_.end())return std::nullopt;
    const auto bit=static_cast<unsigned>(flag&63U);
    return (found->second.dynamic_sprite_flags&(std::uint64_t{1}<<bit))!=0;
}
bool World::set_dynamic_sprite_flag(std::int16_t id,std::uint16_t flag,bool enabled) {
    const auto found=entities_.find(id);
    if(found==entities_.end())return false;
    const auto bit=static_cast<unsigned>(flag&63U);
    const auto mask=std::uint64_t{1}<<bit;
    if(enabled)found->second.dynamic_sprite_flags|=mask;
    else found->second.dynamic_sprite_flags&=~mask;
    return true;
}
bool World::set_avatar_kill_timeout(std::int16_t id, std::int32_t value) {
    const auto found=entities_.find(id);
    if(found==entities_.end() || found->second.kind!=EntityKind::avatar || value < -1) return false;
    found->second.kill_timeout=value;
    return true;
}
std::vector<std::int16_t> World::position_tree(std::int16_t id) const {
    std::vector<std::int16_t> descendants{id};
    for(std::size_t n=0;n<descendants.size();++n)
        for(const auto occupant:entities_.at(descendants[n]).slots)
            if(occupant && entities_.at(occupant).attachment->positioned) descendants.push_back(occupant);
    return descendants;
}
void World::apply_position(const std::vector<std::int16_t>& descendants,EntityPosition position) {
    auto& root=entities_.at(descendants[0]);
    root.position=position;
    if(root.kind==EntityKind::avatar && direction_notch(root.radian_direction)!=position.direction)
        root.radian_direction=radians_for_notch(position.direction);
    if (auto direction=root.object_words.find(1); direction!=root.object_words.end())
        direction->second=position.direction;
    for(std::size_t n=1;n<descendants.size();++n) {
        auto& child=entities_.at(descendants[n]);
        auto p=position;p.direction=child.position->direction;child.position=p;
        if(child.kind==EntityKind::avatar && direction_notch(child.radian_direction)!=p.direction)
            child.radian_direction=radians_for_notch(p.direction);
        if (auto direction=child.object_words.find(1); direction!=child.object_words.end())
            direction->second=p.direction;
    }
    mark_spatial_change();
}
bool World::set_position(std::int16_t id,EntityPosition position) {
    const auto found=entities_.find(id);
    if(found==entities_.end() || found->second.attachment || !valid_position(position)) return false;
    const auto descendants=position_tree(id); // Allocation happens before any mutation.
    apply_position(descendants,position);return true;
}
bool World::set_radian_direction(std::int16_t id,float radians) {
    const auto found=entities_.find(id);
    if(found==entities_.end() || found->second.kind!=EntityKind::avatar || !std::isfinite(radians))
        return false;
    auto& avatar=found->second;
    const auto notch=direction_notch(radians);
    avatar.radian_direction=radians;
    if(avatar.position) avatar.position->direction=notch;
    if(auto direction=avatar.object_words.find(1);direction!=avatar.object_words.end())
        direction->second=notch;
    mark_spatial_change();
    return true;
}
bool World::place_on_floor(std::int16_t id,EntityPosition position) {
    const auto found=entities_.find(id);
    if(found==entities_.end() || !valid_position(position)) return false;
    const auto old=found->second.attachment;
    if(old) {
        const auto parent=entities_.find(old->container);
        if(parent==entities_.end() || old->slot>=parent->second.slots.size() ||
           parent->second.slots[old->slot]!=id) return false;
    }
    const auto descendants=position_tree(id); // No allocation after changing either relation end.
    if(old) {
        entities_.at(old->container).slots[old->slot]=0;
        found->second.attachment.reset();
        if (found->second.saved_object_banks) found->second.unattached_slot=-1;
    }
    apply_position(descendants,position);return true;
}
bool World::place_in_slot(std::int16_t container,std::size_t slot,std::int16_t occupant,
                          std::optional<std::uint8_t> direction) {
    const auto c=entities_.find(container),o=entities_.find(occupant);
    if(c==entities_.end() || o==entities_.end() || !c->second.position || !o->second.position ||
       slot>=c->second.slots.size() || (direction&&*direction>7)) return false;
    const bool same=c->second.slots[slot]==occupant && o->second.attachment &&
                    o->second.attachment->container==container && o->second.attachment->slot==slot;
    const auto descendants=position_tree(occupant);
    if(!same && !attach(container,slot,occupant)) return false;
    auto p=*c->second.position;p.direction=direction.value_or(o->second.position->direction);
    apply_position(descendants,p);o->second.attachment->positioned=true;return true;
}
bool World::move_to_slot(std::int16_t container,std::size_t slot,std::int16_t occupant,
                         std::optional<std::uint8_t> direction) {
    auto c=entities_.find(container),o=entities_.find(occupant);
    if(c==entities_.end()||o==entities_.end()||container==occupant||!c->second.position||
       !o->second.position||slot>=c->second.slots.size()||(direction&&*direction>7))return false;
    if(c->second.slots[slot]==occupant&&o->second.attachment&&
       o->second.attachment->container==container&&o->second.attachment->slot==slot)return true;
    if(c->second.slots[slot]!=0)return false;
    const auto descendants=position_tree(occupant);
    const auto previous=o->second.attachment;
    const auto old_unattached_slot=o->second.unattached_slot;
    if(previous){
        const auto old_container=entities_.find(previous->container);
        if(old_container==entities_.end()||previous->slot>=old_container->second.slots.size()||
           old_container->second.slots[previous->slot]!=occupant)return false;
        const Entity* ancestor=&c->second;
        while(ancestor->attachment){
            const auto parent=ancestor->attachment->container;
            if(parent==occupant)return false;
            ancestor=find(parent);
            if(!ancestor)return false;
        }
        if(!detach(previous->container,previous->slot))return false;
    }
    if(!attach(container,slot,occupant)){
        if(previous)(void)attach(previous->container,previous->slot,occupant);
        if(previous){auto found=entities_.find(occupant);if(found!=entities_.end()){
            found->second.unattached_slot=old_unattached_slot;
            if(found->second.attachment)found->second.attachment->positioned=previous->positioned;
        }}
        return false;
    }
    auto position=*c->second.position;
    position.direction=direction.value_or(o->second.position->direction);
    apply_position(descendants,position);
    o->second.attachment->positioned=true;
    return true;
}
bool World::attach(std::int16_t container, std::size_t slot, std::int16_t occupant) {
    const auto c = entities_.find(container), o = entities_.find(occupant);
    if (c == entities_.end() || o == entities_.end() || container == occupant ||
        slot >= c->second.slots.size() || c->second.slots[slot] != 0 || o->second.attachment) return false;
    // Reject containment cycles before changing either side of the relation.
    const Entity* ancestor = &c->second;
    while (ancestor->attachment) {
        const auto parent = ancestor->attachment->container;
        if (parent == occupant) return false;
        ancestor = find(parent);
        if (!ancestor) return false;
    }
    c->second.slots[slot] = occupant;
    o->second.attachment = Attachment{container,slot};
    mark_spatial_change();
    return true;
}
bool World::detach(std::int16_t container, std::size_t slot) {
    const auto c = entities_.find(container);
    if (c == entities_.end() || slot >= c->second.slots.size()) return false;
    const auto occupant = c->second.slots[slot];
    if (occupant) {
        auto& entity=entities_.at(occupant);
        entity.attachment.reset();
        if (entity.saved_object_banks) entity.unattached_slot=-1;
    }
    c->second.slots[slot] = 0;
    if(occupant)mark_spatial_change();
    return true;
}
bool World::remove(std::int16_t id) {
    const auto found = entities_.find(id);
    if (found == entities_.end()) return false;
    auto& entity = found->second;
    if (entity.attachment) detach(entity.attachment->container,entity.attachment->slot);
    for (std::size_t slot = 0; slot < entity.slots.size(); ++slot) detach(id,slot);
    words_ -= entity.attributes.size() + entity.person_data.size() + entity.slots.size() +
              entity.object_words.size() + entity.motives.size() + entity.definition_words.size() +
              (entity.entry_points ? 2*entity.entry_points->size() : 0) +
              (entity.saved_object_banks ? 73 : 0);
    entities_.erase(found);mark_spatial_change();return true;
}
std::optional<std::int16_t> World::read(const VmFrame& frame, std::uint8_t scope, std::int16_t index) const {
    if (scope != 0 && scope != 1 && scope != 3 && scope != 4 && scope != 14 && scope != 15 &&
        scope != 16 && scope != 18 && scope != 19 && scope != 20 && scope != 21 && scope != 35) return std::nullopt;
    const auto* entity = find(scope_entity(frame,scope));
    if (!entity) return std::nullopt;
    if (scope == 35) {
        if (!entity->entry_points || !valid_index(index,entity->entry_points->size())) return std::nullopt;
        const auto action=(*entity->entry_points)[static_cast<std::size_t>(index)].action;
        // Preserve the low 16 bits with a defined signed conversion.
        return static_cast<std::int16_t>(action<=32767 ? static_cast<std::int32_t>(action)
                                                     : static_cast<std::int32_t>(action)-65536);
    }
    if (scope == 21) {
        const auto field = entity->definition_words.find(index);
        return field == entity->definition_words.end() ? std::nullopt : std::optional<std::int16_t>(field->second);
    }
    if (scope == 14 || scope == 15) {
        if (entity->kind != EntityKind::avatar || !valid_index(index,entity->motives.size())) return std::nullopt;
        return entity->motives[static_cast<std::size_t>(index)];
    }
    if (scope == 0 || scope == 1) {
        if (!valid_index(index,entity->attributes.size())) {
            if (attribute_policy_ == AttributePolicy::ts1_bounded &&
                valid_index(index,limits_.bank_words)) return std::int16_t{0};
            return std::nullopt;
        }
        return entity->attributes[static_cast<std::size_t>(index)];
    }
    if (scope == 18 || scope == 19) {
        if (entity->kind != EntityKind::avatar || !valid_index(index,entity->person_data.size())) return std::nullopt;
        return entity->person_data[static_cast<std::size_t>(index)];
    }
    if (scope == 16 || scope == 20) {
        if (!valid_index(index,entity->slots.size())) return std::nullopt;
        return entity->slots[static_cast<std::size_t>(index)];
    }
    if (index == 11) return entity->id;
    if (index == 67) return static_cast<std::int16_t>(entity->slots.size());
    if (index == 2) return entity->attachment ? entity->attachment->container : static_cast<std::int16_t>(0);
    if (index == 3) return entity->attachment ? static_cast<std::int16_t>(entity->attachment->slot) : entity->unattached_slot;
    // Simitone's VMEntity.GetValue(Room) reports OUT_OF_WORLD (-5) until the
    // entity has a position, regardless of the stored ObjectData[29] word.
    if (index == 29 && !entity->position) return std::int16_t{-5};
    const auto field = entity->object_words.find(index);
    return field == entity->object_words.end() ? std::nullopt : std::optional<std::int16_t>(field->second);
}
bool World::write(const VmFrame& frame, std::uint8_t scope, std::int16_t index, std::int16_t value) {
    const auto found = entities_.find(scope_entity(frame,scope));
    if (found == entities_.end()) return false;
    auto& entity = found->second;
    if (scope == 0 || scope == 1) {
        if (!valid_index(index,entity.attributes.size())) {
            if (attribute_policy_ != AttributePolicy::ts1_bounded ||
                !valid_index(index,limits_.bank_words)) return false;
            const auto replacement_size = static_cast<std::size_t>(index) + 1;
            const auto retained_words = words_ - entity.attributes.size();
            if (replacement_size > limits_.words - retained_words) return false;
            auto replacement = entity.attributes;
            replacement.resize(replacement_size,0);
            replacement[static_cast<std::size_t>(index)] = value;
            entity.attributes.swap(replacement);
            words_ = retained_words + replacement_size;
            mark_spatial_change();
            return true;
        }
        auto& old=entity.attributes[static_cast<std::size_t>(index)];
        if(old!=value){old=value;mark_spatial_change();}
        return true;
    }
    if (scope == 18 || scope == 19) return set_person_data(entity.id,index,value);
    if (scope == 14 || scope == 15) return set_motive(entity.id,index,value);
    if ((scope == 3 || scope == 4) && !derived(index)) {
        const auto field = entity.object_words.find(index);
        if (field == entity.object_words.end()) return false;
        if (index == 1) {
            if (value < 0 || value > 7) return false;
            if (entity.position) entity.position->direction = static_cast<std::uint8_t>(value);
            if (entity.kind==EntityKind::avatar)
                entity.radian_direction=radians_for_notch(static_cast<std::uint8_t>(value));
        }
        if(field->second!=value){field->second = value;mark_spatial_change();}
        return true;
    }
    return false;
}
VmMemory World::vm_memory() {
    VmMemory memory;
    memory.read=[this](const VmFrame& frame,std::uint8_t scope,std::int16_t index) {
        return read(frame,scope,index);
    };
    memory.write=[this](const VmFrame& frame,std::uint8_t scope,std::int16_t index,std::int16_t value) {
        return write(frame,scope,index,value);
    };
    memory.dynamic_sprite_flag=[this](const VmFrame& frame,std::uint16_t flag) {
        return dynamic_sprite_flag(frame.context.stack_object,flag);
    };
    memory.set_dynamic_sprite_flag=[this](const VmFrame& frame,std::uint16_t flag,bool enabled) {
        return set_dynamic_sprite_flag(frame.context.stack_object,flag,enabled);
    };
    return memory;
}
} // namespace odh
