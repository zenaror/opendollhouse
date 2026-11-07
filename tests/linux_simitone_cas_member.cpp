#include "linux_simitone_cas_member.hpp"
#include "linux_cas_draft.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void need(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

odh_host::SimitoneCasPersonInput adult() {
    odh_host::SimitoneCasPersonInput input;
    input.name_utf8 = "Ana";
    input.biography_utf8 = "Criativa";
    input.language = 14;
    input.female = true;
    input.skin = "med";
    input.body_type = "fat";
    input.body_appearance = "b001mafat";
    input.body_texture = "b001mafatmed_01";
    input.head_appearance = "c001ma";
    input.head_texture = "c001mamed_01";
    input.handgroup_texture = "bear1";
    input.personality_points = {10, 5, 4, 3, 3};
    return input;
}

} // namespace

int main() {
    try {
        const std::array<std::uint32_t, 12> random_values{0, 4, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::size_t draw_index = 0;
        auto draft = odh_host::make_simitone_cas_member(adult(), [&](std::uint32_t upper) {
            need(draw_index < random_values.size(), "CAS requested more random values than the fixture provides");
            const auto value = random_values[draw_index++];
            need(value < upper, "fixture random value exceeds requested range");
            return value;
        });
        need(draft.name_utf8 == "Ana" && draft.biography_utf8 == "Criativa" && draft.language == 14,
            "CAS text and language were not preserved for the private writer");
        need(draft.body_strings.size() == 12, "CAS produced an unexpected body-string replacement count");
        auto body = [&](std::uint16_t index) -> const std::string& {
            for (const auto& entry : draft.body_strings) if (entry.index == index) return entry.bytes;
            throw std::runtime_error("expected body-string replacement is missing");
        };
        need(body(0) == "adult" && body(1) == "b001mafat,BODY=b001mafatmed_01" &&
             body(2) == "c001ma,HEAD-HEAD=c001mamed_01", "CAS body and head choices were not carried over");
        need(body(12) == "female" && body(13) == "27" && body(14) == "med",
            "adult gender, age, or skin choices were not carried over");
        need(body(17) == "HfLO,HAND=huaobear1" && body(22) == "HfRC,HAND=huaobear1",
            "CAS hand choices were not converted");
        for (const auto& replacement : draft.body_strings)
            need(replacement.index < 27, "CAS wrote outside the original TemplatePerson string table");
        need(draft.person_data.size() == 88 && draft.person_data[2] == 1000 &&
             draft.person_data[3] == 500 && draft.person_data[5] == 400 &&
             draft.person_data[6] == 300 && draft.person_data[7] == 300,
            "CAS personality sliders were stored in the wrong PersonData words");
        need(draft.person_data[13] == 500 && draft.person_data[14] == 900 &&
             draft.person_data[16] == 600 && draft.person_data[26] == 600,
            "CAS initial needs differ from the observed Simitone profile");
        need(draft.person_data[46] == 0 && draft.person_data[47] == 100 &&
             draft.person_data[48] == 200 && draft.person_data[49] == 300 &&
             draft.person_data[50] == 400 && draft.person_data[51] == 500 &&
             draft.person_data[52] == 600 && draft.person_data[53] == 700 &&
             draft.person_data[54] == 800 && draft.person_data[55] == 900,
            "CAS randomized trait buckets differ from Simitone's TS1 mapping");
        need(draft.person_data[58] == 27 && draft.person_data[60] == 1 && draft.person_data[65] == 1,
            "CAS age, skin tone, or gender was stored in the wrong PersonData word");
        need(draw_index == random_values.size(), "CAS consumed an unexpected number of random values");

        auto child = adult();
        child.female = false;
        child.child = true;
        child.skin = "drk";
        draw_index = 0;
        draft = odh_host::make_simitone_cas_member(child, [&](std::uint32_t upper) {
            const auto value = random_values[draw_index++];
            need(value < upper, "child fixture random value exceeds requested range");
            return value;
        });
        need(body(0) == "child" && body(12) == "male" && body(13) == "9" && body(14) == "drk" &&
             draft.person_data[58] == 9 && draft.person_data[60] == 2 && draft.person_data[65] == 0,
            "child CAS choices were not converted according to the TS1 profile");

        auto invalid = adult();
        invalid.personality_points = {10, 10, 10, 0, 0};
        bool rejected = false;
        try { (void)odh_host::make_simitone_cas_member(invalid, [](std::uint32_t) { return 0U; }); }
        catch (const std::invalid_argument&) { rejected = true; }
        need(rejected, "CAS accepted more than 25 personality points");
        invalid = adult();
        invalid.body_appearance = "../outside";
        rejected = false;
        try { (void)odh_host::make_simitone_cas_member(invalid, [](std::uint32_t) { return 0U; }); }
        catch (const std::invalid_argument&) { rejected = true; }
        need(rejected, "CAS accepted a path-like appearance asset name");

        // EditIndex replaces an existing WIP member, including at capacity;
        // selection/cancel must not publish the unfinished editor contents.
        odh_host::LinuxCasDraftMembers family;
        family.accept(adult());
        family.accept(child);
        auto changed=family.select(0);
        changed.name_utf8="Ágata";
        changed.biography_utf8="Nome revisto";
        changed.child=true;
        changed.female=false;
        changed.skin="drk";
        changed.body_appearance="b002mcchd";
        changed.body_texture="b002mcchddrk_01";
        changed.head_appearance="c002mc";
        changed.head_texture="c002mcdrk_01";
        changed.personality_points={1,2,3,4,5};
        need(family.members()[0].name_utf8=="Ana", "merely opening/editing form changed accepted member");
        family.begin_new();
        need(family.members()[0].name_utf8=="Ana", "canceling member edit changed the family");
        (void)family.select(0);
        family.accept(changed);
        need(family.members().size()==2&&family.members()[0].name_utf8=="Ágata"&&
            family.members()[1].skin=="drk"&&family.members()[1].name_utf8==child.name_utf8,
            "editing first member appended, reordered, or changed the other member");
        draw_index=0;
        const auto converted=odh_host::make_simitone_cas_member(family.members()[0],
            [&](std::uint32_t upper) {
                need(draw_index<random_values.size(),"edited CAS fixture exhausted its valid trait sequence");
                const auto value=random_values[draw_index++];
                need(value<upper,"edited CAS fixture value exceeds requested range");
                return value;
            });
        need(converted.name_utf8=="Ágata"&&converted.biography_utf8=="Nome revisto"&&
            converted.person_data[2]==100&&converted.person_data[3]==200&&
            converted.person_data[5]==300&&converted.person_data[6]==400&&
            converted.person_data[7]==500&&converted.person_data[58]==9&&
            converted.person_data[60]==2&&converted.person_data[65]==0,
            "edited text, personality, age/gender/skin did not reach the private-character consumer");
        bool edited_body=false,edited_head=false;
        for(const auto& replacement:converted.body_strings) {
            if(replacement.index==1)edited_body=replacement.bytes=="b002mcchd,BODY=b002mcchddrk_01";
            if(replacement.index==2)edited_head=replacement.bytes=="c002mc,HEAD-HEAD=c002mcdrk_01";
        }
        need(edited_body&&edited_head,"edited draft appearance did not reach character body strings");
        while(family.members().size()<8)family.accept(adult());
        (void)family.select(0);family.accept(changed);
        need(family.members().size()==8,"editing a full household consumed another place");
        rejected=false;
        try {family.accept(adult());}catch(const std::invalid_argument&){rejected=true;}
        need(rejected&&family.members().size()==8,"ninth member changed the draft");
        (void)family.select(0);
        need(family.erase_selected()&&family.members().size()==7&&
            family.members()[0].name_utf8==child.name_utf8,"removing draft member damaged roster order");
        need(!family.erase_selected(),"removing without a selection changed another member");
        family.accept(changed);
        need(family.members().size()==8&&family.members().back().name_utf8=="Ágata",
            "removal did not release a place for a new member");
        rejected=false;
        try {(void)family.select(8);}catch(const std::out_of_range&){rejected=true;}
        need(rejected&&!family.editing(),"invalid selection changed active edit state");

        std::cout << "Simitone TS1 CAS member conversion checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
