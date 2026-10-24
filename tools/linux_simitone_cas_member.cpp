#include "linux_simitone_cas_member.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace odh_host {
namespace {
bool identifier(std::string_view value, std::size_t minimum, std::size_t maximum) {
    if (value.size() < minimum || value.size() > maximum) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
    });
}

void append(std::vector<odh::TemplateBodyString>& strings, std::uint16_t index,
            std::string value) {
    strings.push_back({index, std::move(value)});
}

CasRandomBelow make_default_random() {
    auto engine = std::make_shared<std::mt19937>(std::random_device{}());
    return [engine](std::uint32_t upper) {
        if (upper == 0) throw std::invalid_argument("CAS random range is empty");
        return std::uniform_int_distribution<std::uint32_t>(0, upper - 1)(*engine);
    };
}

std::uint32_t draw(CasRandomBelow& random_below, std::uint32_t upper) {
    const auto value = random_below(upper);
    if (value >= upper) throw std::invalid_argument("CAS random source returned an out-of-range value");
    return value;
}

} // namespace

LinuxPrivateFamilyMemberDraft make_simitone_cas_member(
        const SimitoneCasPersonInput& input, CasRandomBelow random_below) {
    if (input.language < 1 || input.language > 20)
        throw std::invalid_argument("CAS language code is outside 1..20");
    if (!identifier(input.body_type, 3, 3))
        throw std::invalid_argument("CAS body type must be a three-character asset code");
    for (const auto* asset : {&input.body_appearance, &input.body_texture,
                              &input.head_appearance, &input.head_texture,
                              &input.handgroup_texture})
        if (!identifier(*asset, 1, 128))
            throw std::invalid_argument("CAS appearance asset name is invalid");
    if (input.skin != "lgt" && input.skin != "med" && input.skin != "drk")
        throw std::invalid_argument("CAS skin must be lgt, med, or drk");
    const auto point_total = std::accumulate(input.personality_points.begin(),
        input.personality_points.end(), 0U);
    if (point_total > 25 || std::any_of(input.personality_points.begin(),
            input.personality_points.end(), [](std::uint8_t points) { return points > 10; }))
        throw std::invalid_argument("CAS personality exceeds the 25-point, 0..10 limits");
    if (!random_below) random_below = make_default_random();

    LinuxPrivateFamilyMemberDraft result;
    result.name_utf8 = input.name_utf8;
    result.biography_utf8 = input.biography_utf8;
    result.language = input.language;

    auto& strings = result.body_strings;
    append(strings, 0, input.child ? "child" : "adult");
    append(strings, 1, input.body_appearance + ",BODY=" + input.body_texture);
    append(strings, 2, input.head_appearance + ",HEAD-HEAD=" + input.head_texture);
    append(strings, 12, input.female ? "female" : "male");
    append(strings, 13, input.child ? "9" : "27");
    append(strings, 14, input.skin);
    const std::string hand = input.child ? "u" : (input.female ? "f" : "m");
    append(strings, 17, "H" + hand + "LO,HAND=huao" + input.handgroup_texture);
    append(strings, 18, "H" + hand + "RO,HAND=huao" + input.handgroup_texture);
    append(strings, 19, "H" + hand + "LP,HAND=huao" + input.handgroup_texture);
    append(strings, 20, "H" + hand + "RP,HAND=huao" + input.handgroup_texture);
    append(strings, 21, "H" + hand + "LO,HAND=huao" + input.handgroup_texture);
    append(strings, 22, "H" + hand + "RC,HAND=huao" + input.handgroup_texture);

    result.person_data.assign(88, 0);
    constexpr std::array<std::size_t, 5> personality_words{2, 3, 5, 6, 7};
    for (std::size_t i = 0; i < personality_words.size(); ++i)
        result.person_data[personality_words[i]] = static_cast<std::int16_t>(input.personality_points[i] * 100);
    result.person_data[13] = static_cast<std::int16_t>(500 + draw(random_below, 5) * 100);
    result.person_data[14] = static_cast<std::int16_t>(500 + draw(random_below, 5) * 100);
    result.person_data[16] = 600;
    result.person_data[26] = 600;
    std::array<unsigned, 4> remaining{1, 3, 3, 3};
    for (std::size_t word = 46; word < 56; ++word) {
        bool accepted = false;
        for (unsigned attempt = 0; attempt < 64 && !accepted; ++attempt) {
            const auto value = draw(random_below, 10);
            const auto category = value == 0 ? 0U : (value < 4 ? 1U : (value < 7 ? 2U : 3U));
            if (remaining[category] == 0) continue;
            --remaining[category];
            result.person_data[word] = static_cast<std::int16_t>(value * 100);
            accepted = true;
        }
        if (!accepted) throw std::runtime_error("CAS random source could not complete the ten personality traits");
    }
    const auto skin = input.skin == "lgt" ? 0 : (input.skin == "med" ? 1 : 2);
    result.person_data[58] = input.child ? 9 : 27;
    result.person_data[60] = static_cast<std::int16_t>(skin);
    result.person_data[65] = input.female ? 1 : 0;
    return result;
}

} // namespace odh_host
