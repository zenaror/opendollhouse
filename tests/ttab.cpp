#include "odh/formats.hpp"

#include <iostream>
#include <sstream>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F operation, const char* message) {
    bool failed = false;
    try { operation(); } catch (const odh::FormatError&) { failed = true; }
    require(failed, message);
}
odh::Ttab parse(const std::string& bytes, const odh::Limits& limits = {}) {
    std::istringstream stream(bytes, std::ios::binary);
    return odh::read_ttab(odh::View(stream, bytes.size()), limits);
}
void put(std::string& bytes, std::size_t position, std::uint32_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        bytes.at(position + i) = static_cast<char>((value >> (8 * i)) & 255);
}
std::string binary(std::uint32_t value, unsigned width) {
    std::string result;
    for (unsigned i = width; i > 0; --i) result += ((value >> (i - 1)) & 1) ? '1' : '0';
    return result;
}
// Fixture construction uses explicit width selectors, never automatic selection.
std::string encoded(const std::string& fields) {
    std::string bytes(5 + (fields.size() + 7) / 8, '\0');
    put(bytes, 0, 1, 2); put(bytes, 2, 9, 2); bytes[4] = 1;
    for (std::size_t i = 0; i < fields.size(); ++i)
        if (fields[i] == '1') bytes[5 + i / 8] = static_cast<char>(
            static_cast<unsigned char>(bytes[5 + i / 8]) | (1U << (7 - i % 8)));
    return bytes;
}
std::string raw() {
    std::string b(42, '\0');
    put(b, 0, 1, 2); put(b, 2, 8, 2);
    put(b, 4, 0x1001, 2); put(b, 6, 0x1002, 2); put(b, 8, 1, 4);
    put(b, 12, 0x80000001, 4); put(b, 20, 4, 4); put(b, 24, 0x3F800000, 4);
    put(b, 28, 50, 4); put(b, 32, 0xFFFFFFFF, 4);
    put(b, 36, 0xFFFC, 2); put(b, 38, 20, 2); put(b, 40, 0xFFFF, 2);
    return b;
}
} // namespace

int main() {
    try {
        const auto plain = raw();
        const auto compressed = encoded(
            "111" + binary(0x1001, 16) + "111" + binary(0x1002, 16) +
            "100000001" + "111" + binary(0x80000001, 32) + "0" + "100000100" +
            "111" + binary(0x3F800000, 32) + "101" + binary(50, 11) + "100111111" +
            "10011100" + "10100010100" + "10011111");
        for (const auto& bytes : {plain, compressed}) {
            const auto table = parse(bytes);
            require(table.status == odh::TtabStatus::decoded && table.interactions.size() == 1,
                    "TTAB decoded status");
            const auto& row = table.interactions[0];
            require(row.action == 0x1001 && row.test == 0x1002, "action and condition IDs");
            require(row.flags == 0x80000001 && row.attenuation_bits == 0x3F800000,
                    "preserve 32-bit sign/float patterns");
            require(row.label_index == 0 && row.attenuation_code == 4 && row.autonomy_threshold == 50,
                    "cross-byte fields");
            require(row.joining_index == -1, "32-bit signed field");
            require(row.motives.size() == 1 && row.motives[0].minimum == -4 &&
                    row.motives[0].delta == 20 && row.motives[0].personality_modifier == 65535,
                    "16-bit sign extension and unsigned truncation");
        }
        require(parse(compressed).remaining_bits == 7 && parse(plain).remaining_bits == 0,
                "padding coverage");
        for (const auto version : {4, 5, 6}) {
            auto b = plain.substr(0, 20) + plain.substr(24, 12) + plain.substr(38, 2);
            put(b, 2, static_cast<std::uint32_t>(version), 2);
            const auto table = parse(b);
            require(table.interactions[0].motives[0].delta == 20 &&
                    table.interactions[0].motives[0].minimum == 0 &&
                    table.interactions[0].attenuation_code == 0 && table.remaining_bits == 0,
                    "older raw layout omits attenuation code and motive min/modifier");
        }
        auto raw9 = plain.substr(0, 4) + std::string(1, '\0') + plain.substr(4);
        put(raw9, 2, 9, 2);
        require(!parse(raw9).compressed && parse(raw9).interactions[0].flags == 0x80000001,
                "version9 raw compression code0");
        for (const auto spec : {std::pair<unsigned, unsigned>{0, 5}, {1, 8}, {2, 13}, {3, 16}}) {
            const auto minimum = std::uint32_t{1} << (spec.second - 1);
            const auto bytes = encoded("1" + binary(spec.first, 2) + binary(minimum, spec.second) + "00000000");
            const auto expected = static_cast<std::uint16_t>(0U - minimum);
            require(parse(bytes).interactions[0].action == expected, "16-bit width selector/sign boundary");
        }
        for (const auto spec : {std::pair<unsigned, unsigned>{0, 6}, {1, 11}, {2, 21}, {3, 32}}) {
            const auto minimum = std::uint32_t{1} << (spec.second - 1);
            const auto bytes = encoded("0001" + binary(spec.first, 2) + binary(minimum, spec.second) + "00000");
            require(parse(bytes).interactions[0].flags == 0U - minimum, "32-bit width selector/sign boundary");
        }
        require(parse(encoded("000000000")).interactions[0].flags == 0, "compressed zero fields");
        require(parse("").status == odh::TtabStatus::absent_payload, "absent is explicit");
        require(parse(std::string(2, '\0')).status == odh::TtabStatus::empty, "empty count");
        for (const auto version : {2, 3}) {
            auto b = plain; put(b, 2, static_cast<std::uint32_t>(version), 2);
            const auto table = parse(b);
            require(table.status == odh::TtabStatus::legacy_unsupported && table.declared_count == 1 &&
                    table.interactions.empty() && table.remaining_bits == 304, "legacy is not decoded empty");
        }
        auto b = plain; put(b, 8, 0xFFFFFFFF, 4);
        rejects([&] { parse(b); }, "motive allocation bound");
        b = plain; put(b, 0, 65535, 2);
        rejects([&] { parse(b); }, "interaction allocation bound");
        b = compressed; b[4] = 2;
        rejects([&] { parse(b); }, "unknown compression");
        b = plain; put(b, 2, 10, 2);
        rejects([&] { parse(b); }, "TSO version not guessed");
        odh::Limits limits;
        limits.ttab_motives = 0;
        rejects([&] { parse(compressed, limits); }, "configured motive budget");
        limits = {}; limits.ttab_interactions = 0;
        rejects([&] { parse(compressed, limits); }, "configured interaction budget");
        for (const auto& bytes : {plain, compressed})
            for (std::size_t i = 1; i < bytes.size(); ++i)
                rejects([&] { parse(bytes.substr(0, i)); }, "truncated table/field must not invent zero");
        std::cout << checks << " TTAB checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TTAB test failed: " << error.what() << '\n';
        return 1;
    }
}
