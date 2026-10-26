#include "odh_thread_state.h"
#include "odh/temporary_registers.hpp"

#include <atomic>
#include <cstring>
#include <limits>
#include <new>

struct odh_register_bank {
    std::atomic<uint32_t> references{1};
    uint32_t kind;
    explicit odh_register_bank(uint32_t value) : kind(value) { }
};
namespace {
struct TempBank : odh_register_bank {
    odh::Ts1Temps values{};
    TempBank() : odh_register_bank(ODH_BANK_TEMP16) { }
};
struct XLBank : odh_register_bank {
    odh::Ts1TempXL values{};
    XLBank() : odh_register_bank(ODH_BANK_XL32) { }
};
static_assert(odh::ts1_temp_count == ODH_TEMP_COUNT, "TEMP contract");
static_assert(odh::ts1_temp_xl_count == ODH_TEMP_XL_COUNT, "XL contract");

int32_t retain(std::atomic<uint32_t>& references) {
    auto count = references.load(std::memory_order_relaxed);
    for (;;) {
        if (count == 0) return ODH_STATE_INVALID_ARGUMENT;
        if (count == std::numeric_limits<uint32_t>::max()) return ODH_STATE_REFCOUNT_OVERFLOW;
        if (references.compare_exchange_weak(count, count + 1, std::memory_order_relaxed)) return ODH_STATE_OK;
    }
}
bool drop(std::atomic<uint32_t>& references, bool& last) {
    auto count = references.load(std::memory_order_relaxed);
    for (;;) {
        if (count == 0) return false;
        if (references.compare_exchange_weak(count, count - 1, std::memory_order_acq_rel)) {
            last = count == 1;
            return true;
        }
    }
}
bool valid_kind(uint32_t kind) { return kind == ODH_BANK_TEMP16 || kind == ODH_BANK_XL32; }
int16_t narrow(int32_t value) {
    const auto bits = static_cast<uint16_t>(value);
    return bits <= UINT16_C(0x7fff) ? static_cast<int16_t>(bits)
        : static_cast<int16_t>(-32768 + static_cast<int32_t>(bits - UINT16_C(0x8000)));
}
}

struct odh_thread_state {
    std::atomic<uint32_t> references{1};
    odh_register_bank* temps = nullptr;
    odh_register_bank* xl = nullptr;
};

extern "C" uint32_t ODH_SIM_CALL odh_state_abi_version(void) { return 1; }

extern "C" int32_t ODH_SIM_CALL odh_bank_create_v1(uint32_t kind, odh_register_bank** out) {
    if (out) *out = nullptr;
    if (!out || !valid_kind(kind)) return ODH_STATE_INVALID_ARGUMENT;
    odh_register_bank* bank = kind == ODH_BANK_TEMP16 ? static_cast<odh_register_bank*>(new(std::nothrow) TempBank)
        : static_cast<odh_register_bank*>(new(std::nothrow) XLBank);
    if (!bank) return ODH_STATE_NO_MEMORY;
    *out = bank;
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_bank_retain_v1(odh_register_bank* bank) {
    if (!bank) return ODH_STATE_INVALID_ARGUMENT;
    return retain(bank->references);
}
extern "C" int32_t ODH_SIM_CALL odh_bank_release_v1(odh_register_bank* bank) {
    if (!bank) return ODH_STATE_INVALID_ARGUMENT;
    if (!valid_kind(bank->kind)) return ODH_STATE_WRONG_KIND;
    bool last = false;
    if (!drop(bank->references, last)) return ODH_STATE_INVALID_ARGUMENT;
    if (last) {
        if (bank->kind == ODH_BANK_TEMP16) delete static_cast<TempBank*>(bank);
        else delete static_cast<XLBank*>(bank);
    }
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_bank_clone_v1(const odh_register_bank* bank, odh_register_bank** out) {
    if (out) *out = nullptr;
    if (!bank || !out) return ODH_STATE_INVALID_ARGUMENT;
    int32_t status = odh_bank_create_v1(bank->kind, out);
    if (status != ODH_STATE_OK) return status;
    if (bank->kind == ODH_BANK_TEMP16) static_cast<TempBank*>(*out)->values = static_cast<const TempBank*>(bank)->values;
    else static_cast<XLBank*>(*out)->values = static_cast<const XLBank*>(bank)->values;
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_bank_read_v1(const odh_register_bank* bank, int32_t index, int32_t* value) {
    if (value) *value = 0;
    if (!bank || !value) return ODH_STATE_INVALID_ARGUMENT;
    if (!valid_kind(bank->kind)) return ODH_STATE_WRONG_KIND;
    const auto count = bank->kind == ODH_BANK_TEMP16 ? ODH_TEMP_COUNT : ODH_TEMP_XL_COUNT;
    if (index < 0 || index >= count) return ODH_STATE_BOUNDS;
    *value = bank->kind == ODH_BANK_TEMP16 ? static_cast<const TempBank*>(bank)->values[static_cast<uint32_t>(index)]
        : static_cast<const XLBank*>(bank)->values[static_cast<uint32_t>(index)];
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_bank_write_v1(odh_register_bank* bank, int32_t index, int32_t value) {
    if (!bank) return ODH_STATE_INVALID_ARGUMENT;
    if (!valid_kind(bank->kind)) return ODH_STATE_WRONG_KIND;
    const auto count = bank->kind == ODH_BANK_TEMP16 ? ODH_TEMP_COUNT : ODH_TEMP_XL_COUNT;
    if (index < 0 || index >= count) return ODH_STATE_BOUNDS;
    if (bank->kind == ODH_BANK_TEMP16) static_cast<TempBank*>(bank)->values[static_cast<uint32_t>(index)] = narrow(value);
    else static_cast<XLBank*>(bank)->values[static_cast<uint32_t>(index)] = value;
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_bank_import16_v1(odh_register_bank* bank, const int16_t* values, uint32_t count) {
    if (!bank || !values || count != ODH_TEMP_COUNT) return ODH_STATE_INVALID_ARGUMENT;
    if (bank->kind != ODH_BANK_TEMP16) return ODH_STATE_WRONG_KIND;
    std::memmove(static_cast<TempBank*>(bank)->values.data(), values, sizeof(odh::Ts1Temps));
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_bank_export16_v1(const odh_register_bank* bank, int16_t* values, uint32_t count) {
    if (!bank || !values || count != ODH_TEMP_COUNT) return ODH_STATE_INVALID_ARGUMENT;
    if (bank->kind != ODH_BANK_TEMP16) return ODH_STATE_WRONG_KIND;
    std::memmove(values, static_cast<const TempBank*>(bank)->values.data(), sizeof(odh::Ts1Temps));
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_bank_import32_v1(odh_register_bank* bank, const int32_t* values, uint32_t count) {
    if (!bank || !values || count != ODH_TEMP_XL_COUNT) return ODH_STATE_INVALID_ARGUMENT;
    if (bank->kind != ODH_BANK_XL32) return ODH_STATE_WRONG_KIND;
    std::memmove(static_cast<XLBank*>(bank)->values.data(), values, sizeof(odh::Ts1TempXL));
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_bank_export32_v1(const odh_register_bank* bank, int32_t* values, uint32_t count) {
    if (!bank || !values || count != ODH_TEMP_XL_COUNT) return ODH_STATE_INVALID_ARGUMENT;
    if (bank->kind != ODH_BANK_XL32) return ODH_STATE_WRONG_KIND;
    std::memmove(values, static_cast<const XLBank*>(bank)->values.data(), sizeof(odh::Ts1TempXL));
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_bank_info_v1(const odh_register_bank* bank,
    uint32_t* kind, uint32_t* count, uint32_t* references, uint64_t* bytes) {
    if (kind) *kind = 0;
    if (count) *count = 0;
    if (references) *references = 0;
    if (bytes) *bytes = 0;
    if (!bank || !kind || !count || !references || !bytes) return ODH_STATE_INVALID_ARGUMENT;
    if (!valid_kind(bank->kind)) return ODH_STATE_WRONG_KIND;
    *kind = bank->kind;
    *count = bank->kind == ODH_BANK_TEMP16 ? ODH_TEMP_COUNT : ODH_TEMP_XL_COUNT;
    *references = bank->references.load(std::memory_order_relaxed);
    *bytes = bank->kind == ODH_BANK_TEMP16 ? sizeof(TempBank) : sizeof(XLBank);
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_state_create_v1(uint32_t initialize, odh_thread_state** out) {
    if (out) *out = nullptr;
    if (!out || initialize > 1) return ODH_STATE_INVALID_ARGUMENT;
    auto* state = new(std::nothrow) odh_thread_state;
    if (!state) return ODH_STATE_NO_MEMORY;
    if (initialize != 0) {
        int32_t status = odh_bank_create_v1(ODH_BANK_TEMP16, &state->temps);
        if (status == ODH_STATE_OK) status = odh_bank_create_v1(ODH_BANK_XL32, &state->xl);
        if (status != ODH_STATE_OK) { odh_state_release_v1(state); return status; }
    }
    *out = state;
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_state_retain_v1(odh_thread_state* state) {
    if (!state) return ODH_STATE_INVALID_ARGUMENT;
    return retain(state->references);
}
extern "C" int32_t ODH_SIM_CALL odh_state_release_v1(odh_thread_state* state) {
    if (!state) return ODH_STATE_INVALID_ARGUMENT;
    bool last = false;
    if (!drop(state->references, last)) return ODH_STATE_INVALID_ARGUMENT;
    if (last) {
        if (state->temps) odh_bank_release_v1(state->temps);
        if (state->xl) odh_bank_release_v1(state->xl);
        delete state;
    }
    return ODH_STATE_OK;
}
extern "C" int32_t ODH_SIM_CALL odh_state_get_bank_v1(const odh_thread_state* state, uint32_t kind, odh_register_bank** out) {
    if (out) *out = nullptr;
    if (!state || !out || !valid_kind(kind)) return ODH_STATE_INVALID_ARGUMENT;
    auto* bank = kind == ODH_BANK_TEMP16 ? state->temps : state->xl;
    if (!bank) return ODH_STATE_MISSING_BANK;
    const auto status = odh_bank_retain_v1(bank);
    if (status == ODH_STATE_OK) *out = bank;
    return status;
}
extern "C" int32_t ODH_SIM_CALL odh_state_bind_bank_v1(odh_thread_state* state, uint32_t kind, odh_register_bank* bank) {
    if (!state || !valid_kind(kind)) return ODH_STATE_INVALID_ARGUMENT;
    if (bank && bank->kind != kind) return ODH_STATE_WRONG_KIND;
    auto*& slot = kind == ODH_BANK_TEMP16 ? state->temps : state->xl;
    if (slot == bank) return ODH_STATE_OK;
    if (bank) {
        const auto status = odh_bank_retain_v1(bank);
        if (status != ODH_STATE_OK) return status;
    }
    auto* previous = slot;
    slot = bank;
    if (previous) odh_bank_release_v1(previous);
    return ODH_STATE_OK;
}
