#include "odh/lot.hpp"
#include <iostream>
#include <sstream>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void put(std::string& bytes, std::size_t offset, std::uint32_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i)
        bytes.at(offset+i) = static_cast<char>((value >> (8*i)) & 255);
}
odh::Simi parse(const std::string& bytes) {
    std::istringstream stream(bytes, std::ios::binary);
    return odh::read_simi(odh::View(stream, bytes.size()));
}
void rejects(const std::string& bytes) {
    bool failed = false;
    try { parse(bytes); } catch (const odh::FormatError&) { failed = true; }
    require(failed, "malformed/truncated SIMI must fail");
}
std::string fixture(unsigned version, unsigned mask) {
    const std::size_t count = version <= 0x3f ? 32 : 64;
    std::string bytes(12 + count*2 + 34, '\0');
    put(bytes, 0, 0xaabbccdd); put(bytes, 4, version); bytes.replace(8, 4, "IMIS");
    for (std::size_t i = 0; i < count; ++i) put(bytes, 12+i*2, static_cast<unsigned>(i+1), 2);
    put(bytes, 12, 0xffff, 2); put(bytes, 14, 0x8000, 2);
    const auto fields = 12 + count*2;
    put(bytes, fields, 0xffff, 2);
    for (unsigned i = 0; i < 8; ++i) put(bytes, fields+2+i*4, 0xfffffff0+i);
    for (unsigned day = 0; day < 6; ++day) {
        const auto offset = bytes.size();
        const bool valid = (mask & (1U << day)) != 0;
        bytes.resize(offset + (valid ? 36 : 4), '\0');
        // Preserve raw validity; every nonzero value includes eight amounts.
        put(bytes, offset, valid ? (day == 2 ? 0xffffffff : day+1) : 0);
        if (valid) for (unsigned i = 0; i < 8; ++i)
            put(bytes, offset+4+i*4, i == 0 ? 0x80000000 : day*100+i);
    }
    return bytes;
}
}
int main() {
    try {
        for (const unsigned version : {0x33U,0x38U,0x3eU,0x3fU,0x45U,0x48U,0x49U}) {
            for (const unsigned mask : {0U, 0x15U, 0x3fU}) {
                const auto bytes = fixture(version, mask);
                const auto lot = parse(bytes);
                const std::size_t count = version <= 0x3f ? 32 : 64;
                require(lot.reserved == 0xaabbccdd && lot.version == version && lot.remaining_bytes == 0,
                        "raw pad, version, consumed extent");
                require(lot.global_data.size() == count && lot.global_data[0] == -1 &&
                        lot.global_data[1] == -32768 && lot.global_data.back() == static_cast<int>(count),
                        "only serialized globals; signed words and final global preserved");
                require(lot.unknown1 == -1 && lot.unknown2 == -16 && lot.unknown3 == -15 &&
                        lot.guid1 == -14 && lot.guid2 == -13 && lot.unknown4 == -12 &&
                        lot.lot_value == -11 && lot.objects_value == -10 && lot.architecture_value == -9,
                        "all valuation/unknown fields retain raw signed values");
                for (unsigned day = 0; day < 6; ++day) {
                    const bool valid = (mask & (1U << day)) != 0;
                    const auto& budget = lot.budget_days[day];
                    require(budget.amounts.has_value() == valid &&
                            budget.valid == (valid ? (day == 2 ? -1 : static_cast<int>(day+1)) : 0),
                            "variable records preserve validity and absence");
                    if (!valid) continue;
                    const auto& a = *budget.amounts;
                    require(a.misc_income == (-2147483647-1) && a.job_income == static_cast<int>(day*100+1) &&
                            a.service_expense == static_cast<int>(day*100+2) &&
                            a.food_expense == static_cast<int>(day*100+3) &&
                            a.bills_expense == static_cast<int>(day*100+4) &&
                            a.misc_expense == static_cast<int>(day*100+5) &&
                            a.household_expense == static_cast<int>(day*100+6) &&
                            a.architecture_expense == static_cast<int>(day*100+7),
                            "eight distinct budget fields including misc expense");
                }
                for (std::size_t n = 0; n < bytes.size(); ++n) rejects(bytes.substr(0, n));
            }
        }
        auto bytes = fixture(0x33, 0); bytes.append(168, '\xff');
        require(parse(bytes).remaining_bytes == 168, "legacy unknown tail extent retained");
        bytes = fixture(0x49, 0x3f); bytes += "tail";
        require(parse(bytes).remaining_bytes == 4, "new profile trailing extent retained");
        for (const unsigned version : {0U,0x32U,0x34U,0x39U,0x40U,0x44U,0x46U,0x4aU,0xffffffffU}) {
            bytes = fixture(0x3e, 0); put(bytes, 4, version); rejects(bytes);
        }
        bytes = fixture(0x3e, 0); bytes[8] = 'X'; rejects(bytes);
        std::cout << checks << " SIMI checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SIMI test failed: " << error.what() << '\n'; return 1;
    }
}
