#include "saved_thread_plan_audit.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
unsigned check_count = 0;
void require(bool value, const char* message) {
    ++check_count;
    if (!value) throw std::runtime_error(message);
}
void put(std::string& bytes, std::size_t at, std::uint32_t value, unsigned count,
         bool big_endian = false) {
    for (unsigned i = 0; i < count; ++i) {
        const auto shift = (big_endian ? count - 1U - i : i) * 8U;
        bytes.at(at + i) = static_cast<char>((value >> shift) & 0xffU);
    }
}
std::string chunk(const char* type, std::uint16_t id, const std::string& payload) {
    std::string bytes(76, '\0');
    bytes.replace(0, 4, type);
    put(bytes, 4, static_cast<std::uint32_t>(bytes.size() + payload.size()), 4, true);
    put(bytes, 8, id, 2, true);
    return bytes + payload;
}
std::string iff(const std::string& chunks) {
    std::string bytes = "IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    bytes.resize(64, '\0');
    return bytes + chunks;
}
std::string bhav() {
    std::string bytes(24, '\0');
    put(bytes, 0, 0x8002, 2);
    put(bytes, 2, 1, 2);
    bytes[5] = 4;
    put(bytes, 12, 2, 2);
    bytes[14] = static_cast<char>(0xfe);
    bytes[15] = static_cast<char>(0xff);
    return bytes;
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("could not write temporary IFF fixture");
}
void write_far(const std::filesystem::path& path,
               const std::vector<std::pair<std::string, std::string>>& assets) {
    std::string bytes = "FAR!byAZ";
    bytes.resize(16, '\0');
    put(bytes, 8, 1, 4);
    std::vector<std::uint32_t> offsets;
    offsets.reserve(assets.size());
    for (const auto& asset : assets) {
        offsets.push_back(static_cast<std::uint32_t>(bytes.size()));
        bytes += asset.second;
    }
    const auto index = bytes.size();
    put(bytes, 12, static_cast<std::uint32_t>(index), 4);
    bytes.resize(index + 4, '\0');
    put(bytes, index, static_cast<std::uint32_t>(assets.size()), 4);
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const auto at = bytes.size();
        bytes.resize(at + 16, '\0');
        const auto size = static_cast<std::uint32_t>(assets[i].second.size());
        put(bytes, at, size, 4);
        put(bytes, at + 4, size, 4);
        put(bytes, at + 8, offsets[i], 4);
        put(bytes, at + 12, static_cast<std::uint32_t>(assets[i].first.size()), 4);
        bytes += assets[i].first;
    }
    write(path, bytes);
}
struct TempDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("odh-saved-thread-plan-audit-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDirectory() { std::filesystem::create_directory(path); }
    ~TempDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
}

int main() {
    try {
        TempDirectory temp;
        const auto owner_a = temp.path / "owner-a.iff";
        const auto owner_b = temp.path / "owner-b.iff";
        write(owner_a, iff(chunk("BHAV", 4096, bhav())));
        write(owner_b, iff(chunk("BHAV", 4097, bhav())));
        const auto shared_entries = temp.path / "shared-two.far";
        write_far(shared_entries, {
            {"OwnerA.iff", iff(chunk("BHAV", 8192, bhav()))},
            {"Global.iff", iff(chunk("BHAV", 100, bhav()))},
        });

        odh::SimitoneSavedThreadPlan plan{};
        plan.entity_id = 11;
        auto frame = [](std::uint16_t routine, std::uint32_t owner) {
            odh::SimitoneSavedFramePlan result{};
            result.routine_id = routine;
            result.code_owner_guid = owner;
            result.caller = static_cast<std::int16_t>(owner);
            result.callee = static_cast<std::int16_t>(owner);
            result.return_state = odh::SimitoneSavedReturnState::normal;
            return result;
        };
        plan.frames = {
            frame(4096, 11), frame(8192, 11), frame(4097, 22), frame(100, 22),
        };
        const std::vector<SavedThreadCodeCheck> checks{
            {"private", owner_a.string(), "", 1},
            {"semiglobal", shared_entries.string(), "OwnerA.iff", 1},
            {"private", owner_b.string(), "", 1},
            {"global", shared_entries.string(), "Global.iff", 1},
        };
        const auto banks = load_saved_thread_routine_banks(plan, checks);
        require(banks.global.count(odh::routine_key(100)) == 1,
                "global frame code comes from the explicit global bank");
        require(banks.owners.size() == 2 &&
                banks.owners.at(11).count(odh::routine_key(4096)) == 1 &&
                banks.owners.at(11).count(odh::routine_key(8192)) == 1 &&
                banks.owners.at(22).count(odh::routine_key(4097)) == 1,
                "private and semiglobal routines stay grouped by saved resource owner");
        odh::Vm vm(banks.global, {}, {}, {}, banks.owners);
        odh::restore_simitone_saved_thread_plan(vm, plan);
        require(vm.status() == odh::VmStatus::dormant && vm.stack().size() == 4 &&
                vm.stack()[0].context.code_owner == 11 &&
                vm.stack()[1].context.code_owner == 11 &&
                vm.stack()[2].context.code_owner == 22 &&
                vm.stack()[3].context.code_owner == 22,
                "resource-backed plan import validates mixed-scope frames and remains dormant");

        auto missing_plan = plan;
        missing_plan.frames[0].routine_id = 4098;
        bool missing_rejected = false;
        try { (void)load_saved_thread_routine_banks(missing_plan, checks); }
        catch (const odh::FormatError&) { missing_rejected = true; }
        require(missing_rejected, "routine absent from an audited source is rejected");
        std::cout << "OK (" << check_count << " resource-backed dormant import checks)\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
