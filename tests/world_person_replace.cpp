#include "odh/world.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
odh::VmFrame frame(std::int16_t id) {
    return {odh::routine_key(4096),0,{id,id,id},{},{}};
}
bool same_unrelated(const odh::Entity& a, const odh::Entity& b) {
    const bool attachment = a.attachment.has_value() == b.attachment.has_value() &&
        (!a.attachment || (a.attachment->container == b.attachment->container &&
         a.attachment->slot == b.attachment->slot &&
         a.attachment->positioned == b.attachment->positioned));
    const bool definition = a.definition.has_value() == b.definition.has_value() &&
        (!a.definition || (a.definition->guid == b.definition->guid &&
         a.definition->master_guid == b.definition->master_guid));
    return a.id == b.id && a.kind == b.kind && a.generation == b.generation &&
        a.attributes == b.attributes && a.slots == b.slots &&
        a.object_words == b.object_words && a.motives == b.motives &&
        a.definition_words == b.definition_words && a.position == b.position &&
        attachment && definition;
}
void unchanged(const odh::World& world, std::int16_t id, const odh::Entity& before,
               std::size_t words) {
    const auto* after = world.find(id);
    require(after && same_unrelated(*after,before) &&
            after->person_data == before.person_data && world.word_count() == words,
            "rejected replacement leaves all state and accounting unchanged");
}
}
int main() {
    try {
        odh::World world;
        require(world.create(1,odh::EntityKind::avatar,{2,101,2,7}) &&
                world.create(2,odh::EntityKind::object,{1,0,2}) &&
                world.create(3,odh::EntityKind::object),"identity fixture creation");
        require(world.define_type(1,0x12345678U,0x87654321U) &&
                world.declare_object_word(1,0,-42) &&
                world.declare_definition_word(1,9,2) &&
                world.write(frame(1),0,0,-32768) &&
                world.write(frame(1),0,1,32767) &&
                world.set_motive(1,0,-75) && world.set_motive(1,6,55),
                "nontrivial fields initialized");
        require(world.set_position(1,{40,60,1,3}) &&
                world.set_position(2,{100,120,2,5}) &&
                world.set_position(3,{10,20,1,7}) &&
                world.place_in_slot(1,1,3) && world.place_in_slot(2,1,1),
                "avatar has positioned parent and child");
        for (std::int16_t i=0; i<101; ++i)
            require(world.set_person_data(1,i,123),"old person words initialized");
        const auto* identity = world.find(1);
        const auto old = *identity;
        const auto parent = *world.find(2);
        const auto child = *world.find(3);
        const auto words = world.word_count();
        std::vector<std::int16_t> incoming(88,0);
        incoming[0] = std::numeric_limits<std::int16_t>::min();
        incoming[1] = std::numeric_limits<std::int16_t>::max();
        incoming[87] = -1;
        const auto expected = incoming;
        require(world.replace_person_data(1,incoming),"shrink 101 to 88");
        require(world.find(1) == identity && same_unrelated(*identity,old) &&
                identity->person_data == expected && world.word_count() == words-13,
                "shrink preserves identity, generation, metadata and signed zero data");
        require(same_unrelated(*world.find(2),parent) &&
                same_unrelated(*world.find(3),child),"relations and positioned child unchanged");
        require(world.read(frame(1),18,87) == -1 && !world.read(frame(1),19,88) &&
                !world.set_person_data(1,88,1),"new runtime bounds published");
        incoming[0] = 99;
        incoming.clear();
        require(identity->person_data == expected,"replacement owns its input copy");
        const std::vector<std::int16_t> grown(101,-12);
        require(world.replace_person_data(1,grown) && identity->person_data == grown &&
                same_unrelated(*identity,old) && world.word_count() == words,
                "grow 88 to 101 with exact replacement values");
        require(world.replace_person_data(1,identity->person_data) &&
                identity->person_data == grown && same_unrelated(*identity,old) &&
                world.word_count() == words,"self-aliasing bank input survives swap");

        const auto before_reject = *identity;
        const std::vector<std::int16_t> oversized(4097,9);
        require(!world.replace_person_data(1,oversized),"default bank limit rejects before publication");
        unchanged(world,1,before_reject,words);
        for (const auto id : {0,-1,99}) {
            require(!world.replace_person_data(static_cast<std::int16_t>(id),expected),
                    "invalid, negative or absent actor rejected");
            unchanged(world,1,before_reject,words);
        }
        require(!world.replace_person_data(2,expected) &&
                !world.replace_person_data(2,{}),"object rejects nonempty and empty person banks");
        unchanged(world,2,parent,words);

        odh::World limited({4,6,5});
        require(limited.create(1,odh::EntityKind::avatar,{1,3,0,1}) &&
                limited.declare_object_word(1,0,17),"aggregate budget initially full");
        require(limited.replace_person_data(1,{1,0,-1}) && limited.word_count() == 6,
                "full budget permits equal bank replacement");
        auto limited_before = *limited.find(1);
        require(!limited.replace_person_data(1,{1,2,3,4}),"aggregate budget rejects growth");
        unchanged(limited,1,limited_before,6);
        require(limited.replace_person_data(1,{7}) && limited.word_count() == 4,
                "shrink releases aggregate words");
        require(limited.create(2,odh::EntityKind::object,{2,0,0}) &&
                limited.word_count() == 6,"released budget reusable by another entity");
        limited_before = *limited.find(1);
        require(!limited.replace_person_data(1,{1,2}),"other entity consumes freed space");
        unchanged(limited,1,limited_before,6);
        require(limited.replace_person_data(1,{}) && limited.word_count() == 5 &&
                limited.find(1)->person_data.empty() && same_unrelated(*limited.find(1),limited_before) &&
                !limited.read(frame(1),18,0),"empty replacement valid and releases final bank word");
        require(limited.replace_person_data(1,{}) && limited.word_count() == 5,
                "repeated empty replacement preserves accounting");
        require(limited.replace_person_data(1,{-32768}) && limited.word_count() == 6,
                "empty bank can regrow to exact remaining aggregate boundary");
        require(limited.remove(1) && limited.word_count() == 2 &&
                limited.remove(2) && limited.word_count() == 0,"removal accounts resized bank exactly");

        odh::World custom({1,20,2});
        require(custom.create(1,odh::EntityKind::avatar,{0,1,0}) &&
                custom.replace_person_data(1,{4,5}),"custom bank boundary accepts");
        const auto custom_before = *custom.find(1);
        require(!custom.replace_person_data(1,{4,5,6}),"custom bank boundary rejects");
        unchanged(custom,1,custom_before,2);

        odh::World index_limit({1,std::numeric_limits<std::size_t>::max(),
                               std::numeric_limits<std::size_t>::max()});
        require(index_limit.create(1,odh::EntityKind::avatar),"large custom limits fixture");
        std::vector<std::int16_t> indexed(32768,0);
        indexed.back() = -321;
        require(index_limit.replace_person_data(1,indexed) &&
                index_limit.word_count() == 32768 &&
                index_limit.read(frame(1),18,32767) == -321 &&
                !index_limit.read(frame(1),18,-1),"32768 signed-addressable bank boundary");
        const auto index_before = *index_limit.find(1);
        indexed.push_back(1);
        require(!index_limit.replace_person_data(1,indexed),"signed index bank limit rejects 32769");
        unchanged(index_limit,1,index_before,32768);

        odh::World zero_budget({1,0,0});
        require(zero_budget.create(1,odh::EntityKind::avatar) &&
                zero_budget.replace_person_data(1,{}) && zero_budget.word_count() == 0 &&
                !zero_budget.replace_person_data(1,{1}),"empty bank valid at zero limits");
        std::cout << checks << " person replacement checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Person replacement test failed: " << e.what() << '\n';
        return 1;
    }
}
