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
odh::VmFrame frame(std::int16_t caller=1, std::int16_t object=2) {
    return {odh::routine_key(4096),0,{caller,object,object},{},{}};
}
bool same_other(const odh::Entity& a,const odh::Entity& b) {
    const bool attachment = a.attachment.has_value() == b.attachment.has_value() &&
        (!a.attachment || (a.attachment->container == b.attachment->container &&
         a.attachment->slot == b.attachment->slot &&
         a.attachment->positioned == b.attachment->positioned));
    const bool definition = a.definition.has_value() == b.definition.has_value() &&
        (!a.definition || (a.definition->guid == b.definition->guid &&
         a.definition->master_guid == b.definition->master_guid));
    return a.id == b.id && a.kind == b.kind && a.generation == b.generation &&
        a.person_data == b.person_data && a.slots == b.slots && a.motives == b.motives &&
        a.object_words == b.object_words && a.definition_words == b.definition_words &&
        a.position == b.position && attachment && definition;
}
void unchanged(const odh::World& w, std::int16_t id, const odh::Entity& before,
               std::size_t words) {
    const auto* after = w.find(id);
    require(after && same_other(*after,before) && after->attributes == before.attributes &&
            w.word_count() == words,"rejected growth leaves state and accounting unchanged");
}
}
int main() {
    try {
        odh::World fixed;
        require(fixed.create(1,odh::EntityKind::object,{1,0,0}) &&
                fixed.create(2,odh::EntityKind::avatar),"default fixed fixture");
        require(fixed.write(frame(),0,0,12) && fixed.read(frame(),0,0) == 12 &&
                !fixed.read(frame(),0,1) && !fixed.write(frame(),0,1,7) &&
                !fixed.read(frame(),1,0) && !fixed.write(frame(),1,0,7) &&
                fixed.word_count() == 1,"legacy fixed reads and writes remain strict");
        odh::World explicit_fixed({},odh::AttributePolicy::fixed);
        require(explicit_fixed.create(1,odh::EntityKind::object) &&
                !explicit_fixed.read(frame(1,1),0,0) &&
                !explicit_fixed.write(frame(1,1),1,0,1),"explicit fixed policy remains strict");

        odh::World w({},odh::AttributePolicy::ts1_bounded);
        require(w.create(1,odh::EntityKind::avatar,{1,2,1,2}) &&
                w.create(2,odh::EntityKind::object,{0,0,1}) &&
                w.create(3,odh::EntityKind::object),"bounded fixture");
        require(w.define_type(1,0x12345678U,0x87654321U) &&
                w.declare_object_word(1,0,-40) && w.declare_definition_word(1,9,2) &&
                w.set_person_data(1,0,-17) && w.set_motive(1,1,55) &&
                w.set_position(1,{10,20,1,3}) && w.attach(2,0,1) && w.attach(1,0,3),
                "identity and unrelated banks initialized");
        const auto before = *w.find(1);
        const auto parent = *w.find(2);
        const auto child = *w.find(3);
        const auto* identity = w.find(1);
        const auto words = w.word_count();
        require(w.read(frame(),0,0) == 0 && w.read(frame(),0,2) == 0 &&
                w.read(frame(),1,0) == 0 && w.read(frame(),1,4095) == 0 &&
                w.word_count() == words && w.find(1)->attributes.size() == 1 &&
                w.find(2)->attributes.empty(),"absent bounded words read zero without allocation");
        require(w.write(frame(),0,3,-32768) && w.find(1) == identity &&
                identity->attributes == std::vector<std::int16_t>({0,0,0,-32768}) &&
                same_other(*identity,before) && w.word_count() == words+3,
                "caller growth zero extends and preserves identity and all unrelated state");
        require(same_other(*w.find(2),parent) && w.find(2)->attributes == parent.attributes &&
                same_other(*w.find(3),child),"parent and child unaffected by growth");
        require(w.write(frame(),1,1,32767) && w.read(frame(),1,1) == 32767 &&
                w.read(frame(),0,1) == 0 && w.find(2)->attributes[0] == 0 &&
                w.word_count() == words+5,"stack object growth independent of caller");
        require(w.write(frame(),0,3,-1) && w.read(frame(),0,3) == -1 &&
                w.write(frame(),0,0,0) && w.word_count() == words+5,
                "existing signed attribute writes consume no words");
        const auto reject_before = *identity;
        for (const auto index : {-1,4096,32767}) {
            require(!w.read(frame(),0,static_cast<std::int16_t>(index)) &&
                    !w.write(frame(),0,static_cast<std::int16_t>(index),1),
                    "negative and configured bank boundary unsupported");
            unchanged(w,1,reject_before,words+5);
        }
        for (const auto id : {0,-1,99}) {
            require(!w.read(frame(static_cast<std::int16_t>(id),static_cast<std::int16_t>(id)),0,0) &&
                    !w.write(frame(static_cast<std::int16_t>(id),static_cast<std::int16_t>(id)),1,0,1),
                    "missing or invalid entity never becomes implicit zero");
            unchanged(w,1,reject_before,words+5);
        }
        require(!w.read(frame(),2,0) && !w.write(frame(),2,0,1) &&
                !w.read(frame(),255,0) && !w.write(frame(),255,0,1),
                "unsupported scopes do not inherit attribute policy");
        unchanged(w,1,reject_before,words+5);

        odh::World budget({2,6,8},odh::AttributePolicy::ts1_bounded);
        require(budget.create(1,odh::EntityKind::avatar,{1,2,0,1}) &&
                budget.create(2,odh::EntityKind::object,{1,0,0}) &&
                budget.declare_object_word(1,0,9),"shared budget full fixture");
        auto budget_before = *budget.find(1);
        require(budget.read(frame(),0,7) == 0 && budget.word_count() == 6 &&
                !budget.write(frame(),0,1,42),"implicit zero independent of allocation budget");
        unchanged(budget,1,budget_before,6);
        require(budget.write(frame(),0,0,-8) && budget.word_count() == 6,
                "full budget permits existing writes");
        require(budget.remove(2) && budget.write(frame(1,1),1,1,42) &&
                budget.word_count() == 6 && budget.read(frame(1,1),0,1) == 42,
                "released shared word permits exact boundary growth");
        budget_before = *budget.find(1);
        require(!budget.write(frame(),0,2,99),"growth over exact shared boundary rejected");
        unchanged(budget,1,budget_before,6);
        require(budget.remove(1) && budget.word_count() == 0,"grown attributes removed with exact accounting");

        odh::World custom({1,10,3},odh::AttributePolicy::ts1_bounded);
        require(custom.create(1,odh::EntityKind::object) &&
                custom.read(frame(1,1),0,2) == 0 &&
                custom.write(frame(1,1),0,2,-9) && custom.word_count() == 3 &&
                !custom.read(frame(1,1),0,3) && !custom.write(frame(1,1),0,3,1),
                "custom bank limit accepts final index and rejects boundary");
        odh::World wide({1,std::numeric_limits<std::size_t>::max(),
                         std::numeric_limits<std::size_t>::max()},odh::AttributePolicy::ts1_bounded);
        require(wide.create(1,odh::EntityKind::object) &&
                wide.read(frame(1,1),1,32767) == 0 && wide.word_count() == 0 &&
                wide.write(frame(1,1),1,32767,-321) &&
                wide.find(1)->attributes.size() == 32768 && wide.word_count() == 32768 &&
                wide.read(frame(1,1),0,32767) == -321 &&
                wide.read(frame(1,1),0,32766) == 0,"signed-index boundary is 32768 words");
        const auto wide_before = *wide.find(1);
        require(!wide.read(frame(1,1),0,std::numeric_limits<std::int16_t>::min()) &&
                !wide.write(frame(1,1),0,std::numeric_limits<std::int16_t>::min(),1),
                "negative operand does not alias reference unsigned 32768");
        unchanged(wide,1,wide_before,32768);
        odh::World zero({1,0,0},odh::AttributePolicy::ts1_bounded);
        require(zero.create(1,odh::EntityKind::object) && !zero.read(frame(1,1),0,0) &&
                !zero.write(frame(1,1),0,0,1) && zero.word_count() == 0,
                "zero configured bank supports no implicit attribute words");
        std::cout << checks << " attribute profile checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Attribute profile test failed: " << e.what() << '\n';
        return 1;
    }
}
