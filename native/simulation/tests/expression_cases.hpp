#ifndef ODH_EXPRESSION_CASES_HPP
#define ODH_EXPRESSION_CASES_HPP

#include "odh_expression.h"
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <initializer_list>

// Shared desktop/ARM source suite: no containers, heap allocation or platform APIs.
// API frozen: run returns 0 on success; getter returns the last run's checks.
namespace odh_expression_cases {
inline uint32_t last_checks = 0;
struct Runner {
    uint32_t checks = 0, failures = 0;
    void check(bool condition, const char* label) {
        ++checks;
        if (!condition) { ++failures; printf("FAIL expression: %s\n", label); }
    }
};
struct Bank {
    int32_t lhs = 0, rhs = 0;
    uint8_t lhs_scope = 6, rhs_scope = 7;
    int16_t lhs_index = 11, rhs_index = 12;
    uint32_t accepted = 1;
    int32_t caller = 0, callee = 0, offset = 0;
    uint32_t calls = 0, fail_at = 0, writes = 0, levels = 0;
    bool alias = false, nested = false, nested_ok = false, narrow_write = false;
    char trace[16] = {};
    uint32_t trace_length = 0;
    int32_t written = 0;
    bool event(char ch) {
        if (trace_length < 15) { trace[trace_length++] = ch; trace[trace_length] = 0; }
        ++calls;
        return fail_at != 0 && calls == fail_at;
    }
};
inline bool equal(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
inline void operand(uint8_t (&bytes)[8], const Bank& bank, uint8_t op, uint8_t signed_flag = 0) {
    const auto lhs = static_cast<uint16_t>(bank.lhs_index);
    const auto rhs = static_cast<uint16_t>(bank.rhs_index);
    bytes[0] = static_cast<uint8_t>(lhs); bytes[1] = static_cast<uint8_t>(lhs >> 8);
    bytes[2] = static_cast<uint8_t>(rhs); bytes[3] = static_cast<uint8_t>(rhs >> 8);
    bytes[4] = signed_flag; bytes[5] = op; bytes[6] = bank.lhs_scope; bytes[7] = bank.rhs_scope;
}
inline int32_t ODH_SIM_CALL read(void*, uint8_t, int16_t, int32_t*);
inline int32_t ODH_SIM_CALL write(void*, uint8_t, int16_t, int32_t, uint32_t*);
inline int32_t ODH_SIM_CALL levels(void*, int32_t*, int32_t*, int32_t*);
inline int32_t execute(Bank& bank, uint8_t op, uint32_t& result) {
    uint8_t bytes[8]; operand(bytes, bank, op);
    return odh_expression_execute_ts1_v1(bytes, 8, &bank, read, write, levels, &result);
}
inline int32_t ODH_SIM_CALL read(void* context, uint8_t scope, int16_t index, int32_t* value) {
    auto& bank = *static_cast<Bank*>(context);
    const bool lhs = scope == bank.lhs_scope && index == bank.lhs_index;
    const bool rhs = scope == bank.rhs_scope && index == bank.rhs_index;
    if (!lhs && !rhs) return 90;
    if (bank.event(lhs ? 'L' : 'R')) return 91;
    if (bank.nested) {
        bank.nested = false;
        Bank inner; inner.lhs = INT32_MAX; inner.rhs = 1;
        uint32_t result = 7;
        bank.nested_ok = execute(inner, 3, result) == 0 && inner.lhs == INT32_MIN && result == 1;
    }
    *value = lhs || bank.alias ? bank.lhs : bank.rhs;
    return 0;
}
inline int32_t ODH_SIM_CALL write(void* context, uint8_t scope, int16_t index, int32_t value, uint32_t* accepted) {
    auto& bank = *static_cast<Bank*>(context);
    if (scope != bank.lhs_scope || index != bank.lhs_index) return 92;
    if (bank.event('W')) return 93;
    bank.written = value;
    if (bank.narrow_write) {
        const auto bits = static_cast<uint16_t>(value);
        bank.lhs = bits <= UINT16_C(0x7fff) ? static_cast<int32_t>(bits)
            : -32768 + static_cast<int32_t>(bits - UINT16_C(0x8000));
    } else bank.lhs = value;
    ++bank.writes; *accepted = bank.accepted;
    return 0;
}
inline int32_t ODH_SIM_CALL levels(void* context, int32_t* caller, int32_t* callee, int32_t* offset) {
    auto& bank = *static_cast<Bank*>(context);
    ++bank.levels;
    if (bank.event('V')) return 94;
    *caller = bank.caller; *callee = bank.callee; *offset = bank.offset;
    return 0;
}
struct Case { uint8_t op; int32_t lhs, rhs, after; uint32_t result; const char* trace; };

inline int run() {
    Runner runner;
    runner.check(odh_expression_abi_version() == 1, "ABI1");
    const Case all[] = {
        {0,10,3,10,1,"LR"}, {1,-10,3,-10,1,"LR"}, {2,3,3,3,1,"LR"},
        {3,INT32_MAX,1,INT32_MIN,1,"LRW"}, {4,INT32_MIN,1,INT32_MAX,1,"LRW"},
        {5,10,-20,-20,1,"RW"}, {6,INT32_MAX,2,-2,1,"LRW"}, {7,-7,2,-3,1,"LRW"},
        {8,1,1,1,1,"LR"}, {9,0,32,INT32_MIN,1,"LRW"}, {10,-1,32,INT32_MAX,1,"LRW"},
        {11,INT32_MAX,0,INT32_MIN,1,"LWR"}, {12,-1,INT32_MIN,INT32_MAX,1,"LRW"},
        {13,0x56,0x33,0x12,1,"LRW"}, {14,3,3,3,1,"LR"}, {15,-10,3,-10,1,"LR"},
        {16,3,4,3,1,"LR"}, {17,INT32_MIN,0,INT32_MAX,1,"LWR"},
        {18,0x50,3,0x53,1,"LRW"}, {19,0x55,0xff,0xaa,1,"LRW"},
        {20,123,INT32_MAX,-19196,1,"RW"}
    };
    for (const auto& item : all) {
        Bank bank; bank.lhs = item.lhs; bank.rhs = item.rhs;
        uint32_t result = 99;
        runner.check(execute(bank, item.op, result) == 0, "all21 status");
        runner.check(result == item.result, "all21 boolean result");
        runner.check(bank.lhs == item.after, "all21 memory write");
        runner.check(equal(bank.trace, item.trace), "all21 effect order");
    }
    const Case false_cases[] = {
        {0,1,2,1,0,"LR"}, {1,2,1,2,0,"LR"}, {2,2,1,2,0,"LR"},
        {8,-1,32,-1,0,"LR"}, {14,1,2,1,0,"LR"}, {15,2,1,2,0,"LR"}, {16,1,1,1,0,"LR"}
    };
    for (const auto& item : false_cases) {
        Bank bank; bank.lhs = item.lhs; bank.rhs = item.rhs; uint32_t result = 99;
        runner.check(execute(bank, item.op, result) == 0 && result == 0 && bank.writes == 0, "false compare/signed flag result");
    }
    // Numeric cases are bound to measured .NET 9/10 actual-expression oracle.
    const Case numeric[] = {
        {7,123,0,-1,1,"LRW"}, {12,123,0,123,1,"LRW"},
        {12,INT32_MAX-1,INT32_MAX,-3,1,"LRW"},
        {12,INT32_MIN,-1073741825,-1073741823,1,"LRW"},
        {12,-7,3,2,1,"LRW"}, {12,7,-3,-2,1,"LRW"},
        {20,7,-1,0,1,"RW"}, {20,7,INT32_MIN,0,1,"RW"},
        {20,7,1073676288,32766,1,"RW"}, {20,7,1073676289,32767,1,"RW"},
        {20,7,1073741823,32767,1,"RW"}, {20,7,1073741824,-32768,1,"RW"},
        {20,7,1073741825,-32768,1,"RW"}, {20,7,0,0,1,"RW"}
    };
    for (const auto& item : numeric) {
        Bank bank; bank.lhs = item.lhs; bank.rhs = item.rhs; uint32_t result = 99;
        runner.check(execute(bank, item.op, result) == 0 && result == 1 && bank.lhs == item.after, "numeric oracle result");
        runner.check(equal(bank.trace, item.trace), "numeric oracle effect order");
    }
    for (const uint8_t op : {uint8_t(7), uint8_t(12)}) {
        Bank bank; bank.lhs = INT32_MIN; bank.rhs = -1; uint32_t result = 99;
        runner.check(execute(bank, op, result) == ODH_EXPRESSION_OVERFLOW && result == 0, "MIN/-1 overflow status");
        runner.check(bank.lhs == INT32_MIN && bank.writes == 0 && equal(bank.trace,"LR"), "overflow no write");
    }
    const int32_t shifts[] = { INT32_MIN, -33, -1, 0, 1, 2, 31, 32, 33, 64, INT32_MAX };
    for (const int32_t shift : shifts) {
        const uint32_t mask = UINT32_C(1) << ((static_cast<uint32_t>(shift)-1)&31);
        Bank bank; bank.lhs = -1; bank.rhs = shift; uint32_t result = 99;
        runner.check(execute(bank,8,result) == 0 && result == (mask == UINT32_C(0x80000000) ? 0U : 1U), "C# shift mask and signed flag test");
    }
    // Assignment, flags and sqrt observe setter boolean; arithmetic ignores it.
    for (const uint8_t op : {uint8_t(5),uint8_t(9),uint8_t(10),uint8_t(20)}) {
        Bank bank; bank.lhs = 10; bank.rhs = 2; bank.accepted = 0; uint32_t result = 99;
        runner.check(execute(bank,op,result) == 0 && result == 0 && bank.writes == 1, "observed false setter");
        Bank invalid; invalid.lhs = 10; invalid.rhs = 2; invalid.accepted = 2;
        runner.check(execute(invalid,op,result) == ODH_EXPRESSION_CALLBACK_FAILED && result == 0, "invalid observed setter boolean");
    }
    for (const uint8_t op : {uint8_t(3),uint8_t(4),uint8_t(6),uint8_t(7),uint8_t(11),uint8_t(12),uint8_t(13),uint8_t(17),uint8_t(18),uint8_t(19)}) {
        Bank bank; bank.lhs = 10; bank.rhs = 2; bank.accepted = 0; uint32_t result = 99;
        runner.check(execute(bank,op,result) == 0 && bank.writes == 1, "ignored setter boolean status");
        runner.check(op == 11 || op == 17 || result == 1, "arithmetic true despite rejected setter");
    }
    for (const uint8_t op : {uint8_t(11),uint8_t(17)}) {
        Bank bank; bank.lhs = 5; bank.alias = true; uint32_t result = 99;
        runner.check(execute(bank,op,result) == 0 && result == 0 && equal(bank.trace,"LWR"), "Inc/Dec write before aliased RHS");
        Bank failure; failure.lhs = 5; failure.rhs = 8; failure.fail_at = 3;
        runner.check(execute(failure,op,result) == ODH_EXPRESSION_CALLBACK_FAILED && result == 0, "Inc/Dec RHS failure");
        runner.check(failure.lhs == (op == 11 ? 6 : 4) && failure.writes == 1, "prior write preserved on RHS failure");
        Bank narrowed; narrowed.lhs = op == 11 ? 32767 : -32768; narrowed.rhs = 0; narrowed.narrow_write = true;
        runner.check(execute(narrowed,op,result) == 0 && result == 0, "Inc/Dec compare retains local int32 after narrow setter");
        runner.check(narrowed.written == (op == 11 ? 32768 : -32769) && narrowed.lhs == (op == 11 ? -32768 : 32767), "setter narrowing is host behavior");
    }
    for (const auto& item : all) {
        for (uint32_t fail = 1; fail <= 3; ++fail) {
            Bank bank; bank.lhs = item.lhs; bank.rhs = item.rhs; bank.fail_at = fail; uint32_t result = 99;
            const int32_t status = execute(bank,item.op,result);
            if (fail <= bank.calls) runner.check(status == ODH_EXPRESSION_CALLBACK_FAILED && result == 0, "callback failure status/output");
            else runner.check(status == 0, "unreached callback failure ignored");
        }
    }
    // Lazy special Room path; levels are unsigned-offset values widened to int32.
    for (const uint8_t op : {uint8_t(1),uint8_t(14)}) {
        Bank bank; bank.lhs_scope = 3; bank.lhs_index = 29; bank.lhs = -7; bank.rhs = 1024;
        bank.caller = 1; bank.callee = 2; bank.offset = 65535; uint32_t result = 99;
        runner.check(execute(bank,op,result) == 0 && result == (op == 1 ? 0U : 1U), "Room unsigned LevelOffset");
        runner.check(bank.levels == 1 && equal(bank.trace,"LRV"), "Room levels lazy after reads");
        uint8_t bytes[8]; operand(bytes,bank,op); result = 99;
        runner.check(odh_expression_execute_ts1_v1(bytes,8,&bank,read,write,nullptr,&result) == ODH_EXPRESSION_INVALID_ARGUMENT && result == 0, "missing needed levels error");
        Bank failed; failed.lhs_scope = 3; failed.lhs_index = 29; failed.rhs = 1024; failed.fail_at = 3;
        runner.check(execute(failed,op,result) == ODH_EXPRESSION_CALLBACK_FAILED && result == 0 && equal(failed.trace,"LRV"), "levels failure propagates");
        for (uint32_t fail = 1; fail <= 2; ++fail) {
            Bank early; early.lhs_scope = 3; early.lhs_index = 29; early.rhs = 1024; early.fail_at = fail;
            runner.check(execute(early,op,result) == ODH_EXPRESSION_CALLBACK_FAILED && result == 0 && early.levels == 0,
                "Room memory-read errors precede lazy levels access");
        }
    }
    for (uint8_t op = 0; op <= 20; ++op) {
        Bank bank; bank.lhs_scope = 3; bank.lhs_index = 29; bank.lhs = 1; bank.rhs = 1023;
        uint8_t bytes[8]; operand(bytes,bank,op); uint32_t result = 99;
        runner.check(odh_expression_execute_ts1_v1(bytes,8,&bank,read,write,nullptr,&result) == 0 && bank.levels == 0, "unrelated path does not need levels");
    }
    for (uint8_t op = 0; op <= 20; ++op) {
        if (op == 1 || op == 14) continue;
        Bank bank; bank.lhs_scope = 3; bank.lhs_index = 29; bank.lhs = 1; bank.rhs = 1024;
        uint8_t bytes[8]; operand(bytes,bank,op); uint32_t result = 99;
        runner.check(odh_expression_execute_ts1_v1(bytes,8,&bank,read,write,nullptr,&result) == 0 && bank.levels == 0,
            "only Less/GreaterEqual use Room special case");
    }
    { Bank bank; bank.nested = true; bank.lhs = 1; bank.rhs = 2; uint32_t result = 99;
      runner.check(execute(bank,3,result) == 0 && result == 1 && bank.lhs == 3 && bank.nested_ok, "stateless callback reentry"); }
    { Bank bank; bank.lhs_scope = 255; bank.rhs_scope = 254; bank.lhs_index = INT16_MIN; bank.rhs_index = INT16_MAX;
      bank.lhs = 2; bank.rhs = 3; uint32_t result = 99;
      runner.check(execute(bank,3,result) == 0 && bank.lhs == 5, "full signed indices and scope bytes"); }
    for (uint32_t flag = 0; flag < 256; ++flag) {
        Bank bank; bank.lhs = 5; bank.rhs = 3; uint8_t bytes[8]; operand(bytes,bank,3,static_cast<uint8_t>(flag)); uint32_t result = 99;
        runner.check(odh_expression_execute_ts1_v1(bytes,8,&bank,read,write,nullptr,&result) == 0 && result == 1 && bank.lhs == 8, "unused IsSigned byte preserved/ignored");
    }
    for (uint32_t op = 21; op < 256; ++op) {
        Bank bank; uint8_t bytes[8]; operand(bytes,bank,static_cast<uint8_t>(op)); uint32_t result = 99;
        runner.check(odh_expression_execute_ts1_v1(bytes,8,&bank,read,write,nullptr,&result) == ODH_EXPRESSION_UNKNOWN_OPERATOR && result == 0 && bank.calls == 0, "unknown op before effects");
    }
    { Bank bank; uint8_t bytes[8]; operand(bytes,bank,3); uint8_t short_input[1] = {0}; uint32_t result = 99;
      const uint32_t bad_lengths[] = {0,1,2,7,9,UINT32_MAX};
      for (const auto length : bad_lengths) {
          result = 99;
          runner.check(odh_expression_execute_ts1_v1(short_input,length,&bank,read,write,nullptr,&result) == ODH_EXPRESSION_INVALID_ARGUMENT && result == 0 && bank.calls == 0, "length validated before operand access");
      }
      runner.check(odh_expression_execute_ts1_v1(nullptr,8,&bank,read,write,nullptr,&result) == ODH_EXPRESSION_INVALID_ARGUMENT, "null operand");
      runner.check(odh_expression_execute_ts1_v1(bytes,8,&bank,nullptr,write,nullptr,&result) == ODH_EXPRESSION_INVALID_ARGUMENT, "null read callback");
      runner.check(odh_expression_execute_ts1_v1(bytes,8,&bank,read,nullptr,nullptr,&result) == ODH_EXPRESSION_INVALID_ARGUMENT, "null write callback");
      runner.check(odh_expression_execute_ts1_v1(bytes,8,&bank,read,write,nullptr,nullptr) == ODH_EXPRESSION_INVALID_ARGUMENT, "null output");
      runner.check(bank.calls == 0, "invalid arguments no effects"); }
    last_checks = runner.checks;
    printf("Expression checks: %lu; failures: %lu\n",
        static_cast<unsigned long>(runner.checks), static_cast<unsigned long>(runner.failures));
    return runner.failures == 0 ? 0 : 1;
}
}
inline int odh_expression_run_cases() { return odh_expression_cases::run(); }
inline uint32_t odh_expression_case_check_count() { return odh_expression_cases::last_checks; }

#endif
