#ifndef ODH_STATE_CASES_HPP
#define ODH_STATE_CASES_HPP
#include "odh_thread_state.h"
#include <stdint.h>
#include <stdio.h>
#include <limits.h>

// Identical synthetic bank/lifetime/Expression suite on host and target probes.
namespace odh_state_cases {
inline uint32_t last_checks = 0;
struct Runner {
    uint32_t checks = 0, failures = 0;
    void check(bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; printf("FAIL state: %s\n", label); }
    }
};
struct State {
    odh_thread_state* value = nullptr;
    explicit State(uint32_t initialize = 1) { odh_state_create_v1(initialize, &value); }
    ~State() { if (value) odh_state_release_v1(value); }
    State(const State&) = delete;
    State& operator=(const State&) = delete;
};
struct Bank {
    odh_register_bank* value = nullptr;
    Bank(odh_thread_state* state, uint32_t kind) { odh_state_get_bank_v1(state, kind, &value); }
    explicit Bank(uint32_t kind) { odh_bank_create_v1(kind, &value); }
    ~Bank() { if (value) odh_bank_release_v1(value); }
    Bank(const Bank&) = delete;
    Bank& operator=(const Bank&) = delete;
};
inline int32_t get(odh_register_bank* bank, int32_t index) {
    int32_t value = INT32_MIN; odh_bank_read_v1(bank, index, &value); return value;
}
struct Counts { uint32_t branch = 99, nr = 99, nw = 99, hr = 99, hw = 99, sr = 99, neg = 99; };
struct Host {
    odh_thread_state *current = nullptr, *stack = nullptr, *next_stack = nullptr, *third_stack = nullptr;
    odh_register_bank* replace = nullptr;
    int32_t value = 9;
    uint32_t accepted = 1, resolve_calls = 0, reads = 0, writes = 0;
    bool fail_read = false, fail_write = false, fail_resolve = false, has_levels = false, fail_levels = false;
    bool replace_on_read = false, nest_on_read = false, nested_ok = false, drop_current = false, drop_stack = false;
};
inline int32_t execute(odh_thread_state*, uint8_t, uint8_t, int16_t, uint8_t, int16_t, Counts&, Host* = nullptr);
inline int32_t ODH_SIM_CALL read(void* opaque, uint8_t scope, int16_t index, int32_t* value) {
    auto& host = *static_cast<Host*>(opaque); ++host.reads;
    if (host.replace_on_read) {
        host.replace_on_read = false;
        odh_state_bind_bank_v1(host.current, ODH_BANK_TEMP16, host.replace);
    }
    if (host.nest_on_read) {
        host.nest_on_read = false; Counts nested;
        host.nested_ok = execute(host.current, 5, 8, 2, 8, 1, nested) == 0 && nested.nr == 1 && nested.nw == 1;
    }
    if (host.drop_current) {
        host.drop_current = false;
        odh_state_release_v1(host.current); // executor must keep the holder alive
    }
    if (host.drop_stack) {
        host.drop_stack = false;
        odh_state_release_v1(host.stack);
    }
    if (host.fail_read) return 91;
    *value = scope == 7 ? index : host.value;
    return 0;
}
inline int32_t ODH_SIM_CALL write(void* opaque, uint8_t, int16_t, int32_t value, uint32_t* accepted) {
    auto& host = *static_cast<Host*>(opaque); ++host.writes;
    if (host.fail_write) return 92;
    host.value = value; *accepted = host.accepted; return 0;
}
inline int32_t ODH_SIM_CALL resolve(void* opaque, odh_thread_state** state) {
    auto& host = *static_cast<Host*>(opaque); ++host.resolve_calls;
    if (host.fail_resolve) return 93;
    *state = host.stack;
    if (host.next_stack) { host.stack = host.next_stack; host.next_stack = host.third_stack; host.third_stack = nullptr; }
    return 0;
}
inline int32_t ODH_SIM_CALL levels(void* opaque, int32_t* caller, int32_t* callee, int32_t* offset) {
    auto& host = *static_cast<Host*>(opaque);
    if (host.fail_levels) return 94;
    *caller = 2; *callee = 3; *offset = 0; return 0;
}
inline void operand(uint8_t (&bytes)[8], uint8_t op, uint8_t ls, int16_t li, uint8_t rs, int16_t ri) {
    const auto left = static_cast<uint16_t>(li), right = static_cast<uint16_t>(ri);
    bytes[0] = static_cast<uint8_t>(left); bytes[1] = static_cast<uint8_t>(left >> 8);
    bytes[2] = static_cast<uint8_t>(right); bytes[3] = static_cast<uint8_t>(right >> 8);
    bytes[4] = 0; bytes[5] = op; bytes[6] = ls; bytes[7] = rs;
}
inline int32_t execute(odh_thread_state* state, uint8_t op, uint8_t ls, int16_t li, uint8_t rs, int16_t ri,
    Counts& count, Host* host) {
    uint8_t bytes[8]; operand(bytes, op, ls, li, rs, ri);
    return odh_expression_execute_ts1_state_v1(bytes, 8, state, host,
        host ? read : nullptr, host ? write : nullptr, host && host->has_levels ? levels : nullptr, host ? resolve : nullptr,
        &count.branch, &count.nr, &count.nw, &count.hr, &count.hw, &count.sr, &count.neg);
}
inline int run() {
    Runner r;
    r.check(odh_state_abi_version() == 1, "state ABI1");
    {
        State current, other(0); Bank temps(current.value, 1), xl(current.value, 2);
        uint32_t kind = 0, count = 0, refs = 0; uint64_t bytes = 0;
        r.check(current.value && temps.value && xl.value, "zero state creates both domains");
        r.check(get(temps.value, 19) == 0 && get(xl.value, 1) == 0, "zero initialization");
        r.check(odh_bank_info_v1(temps.value, &kind, &count, &refs, &bytes) == 0 &&
            kind == 1 && count == 20 && refs == 2 && bytes >= 40, "TEMP size and owned view");
        r.check(odh_bank_info_v1(xl.value, &kind, &count, &refs, &bytes) == 0 &&
            kind == 2 && count == 2 && refs == 2 && bytes >= 8, "XL size and owned view");
        r.check(odh_bank_write_v1(temps.value, 0, 65535) == 0 && get(temps.value, 0) == -1,
            "TEMP signed low16 wrap");
        r.check(odh_bank_write_v1(xl.value, 0, INT32_MIN) == 0 && get(xl.value, 0) == INT32_MIN,
            "XL preserves int32");
        int32_t out = 99;
        r.check(odh_bank_read_v1(temps.value, -1, &out) == 2 && out == 0 &&
            odh_bank_write_v1(temps.value, 20, 2) == 2 && odh_bank_write_v1(xl.value, 2, 1) == 2,
            "signed strict bounds");
        int16_t data[20]{}; data[19] = -234;
        r.check(odh_bank_import16_v1(temps.value, data, 19) == 1 && get(temps.value, 0) == -1,
            "invalid import leaves bank intact");
        r.check(odh_bank_import16_v1(temps.value, data, 20) == 0 && get(temps.value, 19) == -234,
            "TEMP exact import");
        data[0] = 123;
        r.check(odh_bank_export16_v1(temps.value, data, 19) == 1 && data[0] == 123,
            "invalid export leaves destination intact");
        r.check(odh_bank_export16_v1(temps.value, data, 20) == 0 && data[19] == -234 && data[0] == 0,
            "TEMP export");
        int32_t wide[2]{INT32_MIN, INT32_MAX};
        r.check(odh_bank_import32_v1(xl.value, wide, 2) == 0 && odh_bank_export32_v1(xl.value, wide, 2) == 0 &&
            wide[0] == INT32_MIN && wide[1] == INT32_MAX, "XL exact copy");
        r.check(odh_bank_import32_v1(temps.value, wide, 2) == 3 &&
            odh_bank_export16_v1(xl.value, data, 20) == 3, "typed copies reject wrong kind");
        r.check(odh_state_bind_bank_v1(other.value, 1, temps.value) == 0 &&
            odh_state_bind_bank_v1(other.value, 2, xl.value) == 0, "share independent domains");
        odh_register_bank* clone = nullptr;
        r.check(odh_bank_clone_v1(temps.value, &clone) == 0, "clone creates owned bank");
        r.check(odh_bank_write_v1(clone, 19, 444) == 0 && get(temps.value, 19) == -234,
            "clone contents independent");
        r.check(odh_state_bind_bank_v1(current.value, 1, clone) == 0, "replace only TEMP");
        Bank other_temps(other.value, 1), current_xl(current.value, 2);
        r.check(other_temps.value == temps.value && get(other_temps.value, 19) == -234 && current_xl.value == xl.value,
            "replacement preserves shared old domain and XL identity");
        r.check(odh_state_bind_bank_v1(current.value, 1, clone) == 0 &&
            odh_state_bind_bank_v1(current.value, 1, xl.value) == 3, "same bind and wrong kind");
        odh_bank_release_v1(clone);
        r.check(odh_state_bind_bank_v1(other.value, 1, nullptr) == 0 && get(temps.value, 19) == -234,
            "clear preserves retained old view");
        odh_register_bank* absent = reinterpret_cast<odh_register_bank*>(1);
        r.check(odh_state_get_bank_v1(other.value, 1, &absent) == 4 && absent == nullptr,
            "missing domain never recreated");
    }
    {
        // Every operator executes against native XL using the same shared body.
        struct Case { uint8_t op; int32_t after; uint32_t branch, writes; };
        const Case cases[] = {{0,6,1,0},{1,6,0,0},{2,6,0,0},{3,8,1,1},{4,4,1,1},
            {5,2,1,1},{6,12,1,1},{7,3,1,1},{8,6,1,0},{9,6,1,1},{10,4,1,1},
            {11,7,0,1},{12,0,1,1},{13,2,1,1},{14,6,1,0},{15,6,0,0},
            {16,6,1,0},{17,5,1,1},{18,6,1,1},{19,4,1,1},{20,1,1,1}};
        State state; Bank xl(state.value, 2);
        for (const auto& test : cases) {
            odh_bank_write_v1(xl.value, 0, 6); odh_bank_write_v1(xl.value, 1, 2); Counts c;
            const auto status = execute(state.value, test.op, 42, 0, 42, 1, c);
            r.check(status == 0 && get(xl.value, 0) == test.after && c.branch == test.branch &&
                c.nw == test.writes && c.nr == (test.op == 5 || test.op == 20 ? 1u : 2u) &&
                c.hr == 0 && c.hw == 0 && c.sr == 0 && c.neg == 0, "all21 native operators");
        }
        Counts c; odh_bank_write_v1(xl.value, 0, INT32_MIN); odh_bank_write_v1(xl.value, 1, -1);
        r.check(execute(state.value, 7, 42, 0, 42, 1, c) == 4 && c.nw == 0 && c.branch == 0,
            "division overflow unchanged");
        odh_bank_write_v1(xl.value, 1, -4);
        r.check(execute(state.value, 20, 42, 0, 42, 1, c) == 0 && c.neg == 1 && get(xl.value, 0) == 0,
            "negative sqrt explicit signal");
    }
    {
        State state; Bank temps(state.value, 1); Counts c; Host host; host.current = state.value;
        odh_bank_write_v1(temps.value, 0, 19); odh_bank_write_v1(temps.value, 19, 987);
        r.check(execute(state.value, 5, 8, 1, 11, 0, c) == 0 && get(temps.value, 1) == 987 &&
            c.nr == 2 && c.nw == 1, "TempByTemp two physical reads");
        host.value = 456;
        r.check(execute(state.value, 5, 11, 0, 6, 0, c, &host) == 0 && get(temps.value, 19) == 456 &&
            c.nr == 1 && c.nw == 1 && c.hr == 1, "TempByTemp setter follows index");
        odh_bank_write_v1(temps.value, 0, -1);
        r.check(execute(state.value, 5, 8, 1, 11, 0, c) == 6 && c.nr == 1 && c.nw == 0,
            "indirect negative target bounds");
        r.check(execute(state.value, 5, 8, 1, 8, 20, c) == 6 && c.nr == 0, "direct upper bounds");
        r.check(execute(state.value, 5, 8, -1, 8, 19, c) == 6 && c.nr == 1, "write bounds after RHS");
        r.check(execute(state.value, 5, 8, 1, 6, 1, c) == 1 && c.hr == 0, "missing host read is lazy argument error");
        r.check(execute(nullptr, 2, 7, 3, 7, 3, c, &host) == 0 && c.hr == 2 && c.branch == 1,
            "null current works for host only");
        r.check(execute(nullptr, 5, 8, 0, 7, 3, c, &host) == 7 && c.hr == 1,
            "null current fails only upon native access");
        host.fail_read = true;
        r.check(execute(state.value, 11, 8, 1, 6, 0, c, &host) == 2 && c.nr == 1 && c.nw == 1 && c.hr == 0,
            "Inc preserves write before failed RHS");
        host.fail_read = false; host.accepted = 0; host.value = -4;
        r.check(execute(state.value, 20, 6, 0, 6, 0, c, &host) == 0 && c.branch == 0 && c.neg == 1 && c.hw == 1,
            "successful host false setter counted");
        host.value = -4; host.fail_write = true;
        r.check(execute(state.value, 20, 6, 0, 6, 0, c, &host) == 2 && c.neg == 1 && c.hw == 0,
            "sqrt signal survives setter failure");
        host.fail_write = false; host.accepted = 2;
        r.check(execute(state.value, 5, 6, 0, 8, 1, c, &host) == 2 && c.hw == 1 && c.branch == 0,
            "invalid host accepted rejected after successful write");
        r.check(execute(state.value, 1, 3, 29, 7, 1024, c, &host) == 1 && c.hr == 2,
            "room levels callback required only after both reads");
        host.has_levels = true;
        r.check(execute(state.value, 1, 3, 29, 7, 1024, c, &host) == 0 && c.branch == 1 && c.hr == 2,
            "room levels bridge preserves caller context");
        host.fail_levels = true;
        r.check(execute(state.value, 14, 3, 29, 7, 1024, c, &host) == 2 && c.branch == 0,
            "room levels bridge failure");
        odh_state_bind_bank_v1(state.value, 1, nullptr);
        r.check(execute(state.value, 5, 8, 1, 8, 0, c) == 7 && c.nr == 0,
            "cleared holder domain unavailable");
        r.check(execute(state.value, 5, 42, 1, 42, 0, c) == 0, "cleared TEMP preserves XL");
    }
    {
        State state, a, b; Bank old(state.value, 1), replacement(1), at(a.value, 1), bt(b.value, 1);
        Counts c; Host host; host.current = state.value; host.replace = replacement.value;
        host.replace_on_read = true; host.value = 17;
        odh_bank_write_v1(old.value, 0, 5); odh_bank_write_v1(replacement.value, 0, 99);
        r.check(execute(state.value, 3, 8, 0, 6, 0, c, &host) == 0 && get(old.value, 0) == 5 &&
            get(replacement.value, 0) == 22, "current domain reread after host replacement");
        host.stack = a.value; host.next_stack = b.value;
        odh_bank_write_v1(at.value, 0, 31); odh_bank_write_v1(bt.value, 0, 72);
        r.check(execute(state.value, 5, 13, 0, 13, 0, c, &host) == 0 && c.sr == 2 &&
            c.nr == 1 && c.nw == 1 && get(at.value, 0) == 31 && get(bt.value, 0) == 31,
            "scope13 resolves separately for RHS and setter");
        State third; Bank third_temps(third.value, 1);
        host.stack = a.value; host.next_stack = b.value; host.third_stack = third.value;
        odh_bank_write_v1(third_temps.value, 0, 90);
        r.check(execute(state.value, 11, 13, 0, 13, 0, c, &host) == 0 && c.sr == 3 &&
            get(at.value, 0) == 31 && get(bt.value, 0) == 32 && c.branch == 1,
            "scope13 Inc resolves LHS read setter and RHS separately");
        host.stack = nullptr;
        r.check(execute(state.value, 5, 8, 0, 13, 0, c, &host) == 7 && c.sr == 1,
            "missing stack holder unavailable");
        host.fail_resolve = true;
        r.check(execute(state.value, 5, 8, 0, 13, 0, c, &host) == 2 && c.sr == 1,
            "resolver failure status");
        r.check(execute(state.value, 5, 8, 0, 13, 0, c) == 7 && c.sr == 0,
            "missing resolver unavailable without invocation");
        host.fail_resolve = false; host.stack = a.value; host.drop_stack = true; host.value = 4;
        a.value = nullptr; // host owner released during RHS callback, after LHS scope13 read
        r.check(execute(state.value, 3, 13, 0, 6, 0, c, &host) == 0 && c.sr == 2 &&
            get(at.value, 0) == 35, "resolved holder retained until return across host release");
        host.stack = nullptr;
        host.nest_on_read = true; host.value = 6;
        odh_bank_write_v1(replacement.value, 1, 135);
        r.check(execute(state.value, 5, 8, 0, 6, 0, c, &host) == 0 && host.nested_ok &&
            get(replacement.value, 2) == 135 && c.nr == 0 && c.hr == 1 && c.nw == 1,
            "host callback reentry with separate counters");
        host.drop_current = true;
        odh_thread_state* borrowed = state.value; state.value = nullptr;
        r.check(execute(borrowed, 5, 8, 0, 6, 0, c, &host) == 0 && get(replacement.value, 0) == 6,
            "holder retained over host release callback");
    }
    {
        State state; Counts c; uint8_t bytes[8]{}; operand(bytes, 99, 8, 0, 8, 0);
        r.check(odh_expression_execute_ts1_state_v1(bytes, 8, state.value, nullptr, nullptr, nullptr, nullptr, nullptr,
            &c.branch,&c.nr,&c.nw,&c.hr,&c.hw,&c.sr,&c.neg) == 3 && c.branch == 0 && c.nr == 0 && c.neg == 0,
            "unknown op zero outputs");
        c = Counts{};
        r.check(odh_expression_execute_ts1_state_v1(bytes, 8, state.value, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr,&c.nr,&c.nw,&c.hr,&c.hw,&c.sr,&c.neg) == 1 && c.nr == 0 && c.neg == 0,
            "required outputs validated and present outputs zeroed");
        odh_thread_state* invalid = reinterpret_cast<odh_thread_state*>(1);
        r.check(odh_state_create_v1(2, &invalid) == 1 && invalid == nullptr, "invalid initialize clears output");
        c = Counts{};
        r.check(odh_expression_execute_ts1_state_v1(bytes, 7, state.value, nullptr, nullptr, nullptr, nullptr, nullptr,
            &c.branch,&c.nr,&c.nw,&c.hr,&c.hw,&c.sr,&c.neg) == 1 && c.branch == 0 && c.nr == 0,
            "invalid operand length before decode");
        r.check(odh_bank_read_v1(nullptr, 0, nullptr) == 1 && odh_state_retain_v1(nullptr) == 1 &&
            odh_state_release_v1(nullptr) == 1 && odh_bank_release_v1(nullptr) == 1, "null lifetime APIs rejected");
    }
    last_checks = r.checks;
    printf("state checks=%u failures=%u\n", static_cast<unsigned>(r.checks), static_cast<unsigned>(r.failures));
    return r.failures ? 1 : 0;
}
}
inline int odh_state_run_cases() { return odh_state_cases::run(); }
inline uint32_t odh_state_last_check_count() { return odh_state_cases::last_checks; }
#endif
