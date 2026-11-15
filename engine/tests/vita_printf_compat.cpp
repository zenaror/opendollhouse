/* SPDX-License-Identifier: MPL-2.0 */
#include "../platform/vita/PrintfFormatCompat.h"
#ifdef NDEBUG
#undef NDEBUG // Contract checks must execute in Release builds too.
#endif
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
extern "C" void* __real_malloc(size_t);
namespace { bool failAllocation = false; size_t allocationCalls = 0; }
extern "C" void* __wrap_malloc(size_t size) {
    ++allocationCalls;
    return failAllocation ? nullptr : __real_malloc(size);
}
static int ThroughVsnprintf(char* out, size_t capacity, const char* format, ...) {
    va_list args; va_start(args, format);
    const int result = vsnprintf(out, capacity, format, args);
    va_end(args); return result;
}
static int ThroughVsprintf(char* out, const char* format, ...) {
    va_list args; va_start(args, format);
    const int result = vsprintf(out, format, args);
    va_end(args); return result;
}
static int ThroughVfprintf(FILE* stream, const char* format, ...) {
    va_list args; va_start(args, format);
    const int result = vfprintf(stream, format, args);
    va_end(args); return result;
}
static int ThroughVprintf(const char* format, ...) {
    va_list args; va_start(args, format);
    const int result = vprintf(format, args);
    va_end(args); return result;
}
static std::string Contents(FILE* stream) {
    assert(fflush(stream) == 0); rewind(stream);
    char bytes[512] = {};
    const size_t size = fread(bytes, 1, sizeof(bytes), stream);
    return std::string(bytes, size);
}
static void CopiedLists(const char* format, ...) {
    va_list original, first, second;
    va_start(original, format); va_copy(first, original); va_copy(second, original);
    char one[64], two[64];
    const int a = vsnprintf(one, sizeof(one), format, first);
    const int b = vsnprintf(two, sizeof(two), format, second);
    va_end(first); va_end(second); va_end(original);
    assert(a == b && strcmp(one, two) == 0 && strcmp(one, "2:665:arrival") == 0);
}
int main() {
    using SignedSize = typename std::make_signed<size_t>::type;
    using UnsignedDiff = typename std::make_unsigned<ptrdiff_t>::type;
    char buffer[1024];
    const char* family = "house %d: family #%d '%s', %zu members, budget %d, arrival point %s";
    const char* expected = "house 7: family #1 'Novato', 2 members, budget 665, arrival point door";
    const int count = snprintf(buffer, sizeof(buffer), family, 7, 1, "Novato", size_t(2), 665, "door");
    assert(count == int(strlen(expected)) && strcmp(buffer, expected) == 0);
    // All eight public entry points, with following arguments and %n counters.
    const char* format = "%zu|%jd|%td|%s%n";
    const char* answer = "2|-7|-9|ok";
    int consumed = -1; int calls = 0;
    assert(snprintf(buffer, sizeof(buffer), format, size_t(2), intmax_t(-7), ptrdiff_t(-9), "ok", &consumed) == 10);
    assert(consumed == 10 && strcmp(buffer, answer) == 0); ++calls;
    consumed = -1;
    assert(sprintf(buffer, format, size_t(2), intmax_t(-7), ptrdiff_t(-9), "ok", &consumed) == 10);
    assert(consumed == 10 && strcmp(buffer, answer) == 0); ++calls;
    consumed = -1;
    assert(ThroughVsnprintf(buffer, sizeof(buffer), format, size_t(2), intmax_t(-7), ptrdiff_t(-9), "ok", &consumed) == 10);
    assert(consumed == 10 && strcmp(buffer, answer) == 0); ++calls;
    consumed = -1;
    assert(ThroughVsprintf(buffer, format, size_t(2), intmax_t(-7), ptrdiff_t(-9), "ok", &consumed) == 10);
    assert(consumed == 10 && strcmp(buffer, answer) == 0); ++calls;
    FILE* stream = tmpfile(); assert(stream); consumed = -1;
    assert(fprintf(stream, format, size_t(2), intmax_t(-7), ptrdiff_t(-9), "ok", &consumed) == 10);
    assert(consumed == 10 && Contents(stream) == answer); ++calls; fclose(stream);
    stream = tmpfile(); assert(stream); consumed = -1;
    assert(ThroughVfprintf(stream, format, size_t(2), intmax_t(-7), ptrdiff_t(-9), "ok", &consumed) == 10);
    assert(consumed == 10 && Contents(stream) == answer); ++calls; fclose(stream);
    consumed = -1;
    assert(printf(format, size_t(2), intmax_t(-7), ptrdiff_t(-9), "ok", &consumed) == 10);
    assert(consumed == 10); ++calls; consumed = -1;
    assert(ThroughVprintf(format, size_t(2), intmax_t(-7), ptrdiff_t(-9), "ok", &consumed) == 10);
    assert(consumed == 10); ++calls; assert(calls == 8);
    SignedSize zn = -1; intmax_t jn = -1; ptrdiff_t tn = -1;
    assert(snprintf(buffer, sizeof(buffer), "abc%zn:%jn!%tn", &zn, &jn, &tn) == 5);
    assert(strcmp(buffer, "abc:!") == 0 && zn == 3 && jn == 4 && tn == 5);
    assert(snprintf(buffer, sizeof(buffer), "%zd:%zi:%jd:%ji:%td:%ti", SignedSize(-3), SignedSize(-4), intmax_t(-5), intmax_t(-6), ptrdiff_t(-7), ptrdiff_t(-8)) == 17);
    assert(strcmp(buffer, "-3:-4:-5:-6:-7:-8") == 0);
    assert(snprintf(buffer, sizeof(buffer), "%zu:%zo:%zx:%zX:%ju:%jo:%jx:%jX:%tu:%to:%tx:%tX", size_t(31), size_t(31), size_t(31), size_t(31), uintmax_t(31), uintmax_t(31), uintmax_t(31), uintmax_t(31), UnsignedDiff(31), UnsignedDiff(31), UnsignedDiff(31), UnsignedDiff(31)) == 35);
    assert(strcmp(buffer, "31:37:1f:1F:31:37:1f:1F:31:37:1f:1F") == 0);
    assert(snprintf(buffer, sizeof(buffer), "%#08zx|%+*.*jd|%-*zu|%.*td", size_t(42), 8, 4, intmax_t(-7), 4, size_t(2), 3, ptrdiff_t(-9)) == 27);
    assert(strcmp(buffer, "0x00002a|   -0007|2   |-009") == 0);
    assert(snprintf(buffer, sizeof(buffer), "%%zu|%%%zu|%%%%|%s", size_t(2), "Ol\xc3\xa1 \xe6\x97\xa5\xe6\x9c\xac %zu") > 0);
    assert(strcmp(buffer, "%zu|%2|%%|Ol\xc3\xa1 \xe6\x97\xa5\xe6\x9c\xac %zu") == 0);
    assert(ThroughVsnprintf(nullptr, 0, family, 7, 1, "Novato", size_t(2), 665, "door") == count);
    assert(snprintf(buffer, 4, "%zu-%s", size_t(12345), "x") == 7 && strcmp(buffer, "123") == 0);
    CopiedLists("%zu:%jd:%s", size_t(2), intmax_t(665), "arrival");
    // Short transformed and long unchanged formats never allocate.
    allocationCalls = 0; failAllocation = true;
    assert(snprintf(buffer, sizeof(buffer), "%zu %s", size_t(2), "ok") == 4);
    assert(allocationCalls == 0); failAllocation = false;
    const std::string longPlain = std::string(4096, 'x') + "%s";
    failAllocation = true; allocationCalls = 0;
    assert(snprintf(buffer, sizeof(buffer), longPlain.c_str(), "end") == 4099);
    assert(allocationCalls == 0); failAllocation = false;
    const std::string longConverted = std::string(4096, 'x') + "%zu:%s";
    allocationCalls = 0;
    assert(snprintf(buffer, sizeof(buffer), longConverted.c_str(), size_t(2), "end") == 4101);
    assert(allocationCalls == 1); failAllocation = true; allocationCalls = 0; errno = 0;
    strcpy(buffer, "unchanged");
    assert(snprintf(buffer, sizeof(buffer), longConverted.c_str(), size_t(2), "end") == -1);
    assert(errno == ENOMEM && allocationCalls == 1 && strcmp(buffer, "unchanged") == 0);
    failAllocation = false;
    const size_t maximum = std::numeric_limits<size_t>::max();
    assert(snprintf(buffer, sizeof(buffer), "%zu", maximum) > 0);
    assert(std::string(buffer) == std::to_string(maximum));
    puts("\nVita printf compatibility: eight entry points passed");
}
