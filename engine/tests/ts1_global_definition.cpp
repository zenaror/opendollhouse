// SPDX-License-Identifier: MPL-2.0
#include "content/TS1GlobalDefinition.h"

#include <array>
#include <cstdint>
#include <optional>

int main()
{
    using FSO::Content::IsTS1GlobalDefinition;
    using FSO::Files::Formats::IFF::Chunks::OBJDType;

    constexpr std::array<OBJDType, 4> types = {
        OBJDType::Normal, OBJDType::SimType, OBJDType::Internal, OBJDType::GiftToken
    };
    constexpr std::array<std::optional<uint16_t>, 5> globals = {
        std::nullopt, uint16_t{0}, uint16_t{1}, uint16_t{2}, uint16_t{0xffff}
    };

    // Literal source membership matrix: absent/zero are not candidates;
    // the three nonzero words are candidates only for types 7 and 12.
    constexpr bool expected[4][5] = {
        {false,false,false,false,false},
        {false,false,true,true,true},
        {false,false,false,false,false},
        {false,false,true,true,true}
    };
    static_assert(uint16_t(OBJDType::SimType)==7 && uint16_t(OBJDType::GiftToken)==12);
    for (size_t typeIndex = 0; typeIndex < types.size(); ++typeIndex) {
        for (size_t globalIndex = 0; globalIndex < globals.size(); ++globalIndex) {
            if (IsTS1GlobalDefinition(types[typeIndex], globals[globalIndex]) !=
                expected[typeIndex][globalIndex]) return 1;
        }
    }

    // Lock the source's explicit type membership independently of enum order.
    if (!IsTS1GlobalDefinition(OBJDType::SimType, uint16_t{1})) return 1;
    if (!IsTS1GlobalDefinition(OBJDType::GiftToken, uint16_t{1})) return 1;
    if (IsTS1GlobalDefinition(OBJDType::Normal, uint16_t{1})) return 1;
    if (IsTS1GlobalDefinition(OBJDType::Internal, uint16_t{1})) return 1;
    return 0;
}
