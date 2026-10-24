#pragma once

#include "odh/vm.hpp"

namespace odh {

using TuningTables = std::map<std::uint16_t, Bcon>;
enum class TuningSemiState { unknown, absent, loaded };
struct TuningOwner {
    TuningTables private_tables, semiglobal_tables;
    TuningSemiState semiglobal_state = TuningSemiState::unknown;
};
// Attach a resolved semiglobal bank to one code owner. Each owner keeps its
// own copy even when several resources share the same semiglobal asset.
void set_semiglobal_tuning(TuningOwner&, TuningTables);
struct TuningResources {
    TuningTables globals;
    std::map<CodeOwner, TuningOwner> owners;
};
struct TuningAddress {
    RoutineScope scope;
    std::uint16_t table;
    std::uint8_t index;
};
struct TuningValue {
    std::int16_t value;
    TuningAddress address; // Query address; fallback retains the semiglobal address.
    CodeOwner owner; // Calling code owner, also retained for shared globals.
    bool semiglobal_fallback;
};
struct TuningLimits {
    std::size_t owners = 64;
    std::size_t tables = 1024;
    std::size_t constants = 65536;
};

// BCON-only snapshot. No callee replacement or OTF tuning is implied.
// Missing data is unsupported (nullopt), distinct from a present zero word.
class BconTuning {
public:
    explicit BconTuning(TuningResources resources, TuningLimits limits = {});
    static TuningAddress address(std::int16_t query);
    std::optional<TuningValue> read(CodeOwner owner, std::int16_t query) const;
private:
    const TuningResources resources_;
};

} // namespace odh
