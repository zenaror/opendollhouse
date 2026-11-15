/* SPDX-License-Identifier: MPL-2.0 */
#include "PrintfFormatCompat.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>

extern "C" {
int __real_vprintf(const char*, va_list);
int __real_vfprintf(FILE*, const char*, va_list);
int __real_vsprintf(char*, const char*, va_list);
int __real_vsnprintf(char*, size_t, const char*, va_list);
}

namespace {

template<class T> constexpr const char* IntegerLength() {
    static_assert(std::is_same<T, int>::value || std::is_same<T, long>::value ||
                  std::is_same<T, long long>::value,
                  "Unsupported printf integer representation");
    return std::is_same<T, int>::value ? "" :
           std::is_same<T, long>::value ? "l" : "ll";
}

const char* Replacement(char length) {
    switch (length) {
    case 'z': return IntegerLength<typename std::make_signed<size_t>::type>();
    case 'j': return IntegerLength<intmax_t>();
    case 't': return IntegerLength<ptrdiff_t>();
    default: return nullptr;
    }
}

bool Digit(char c) { return c >= '0' && c <= '9'; }
bool Flag(char c) {
    return c == '-' || c == '+' || c == ' ' || c == '#' || c == '0' || c == '\'';
}
bool IntegerConversion(char c) {
    return c == 'd' || c == 'i' || c == 'o' || c == 'u' || c == 'x' ||
           c == 'X' || c == 'n';
}

// Locate the complete specifier without touching its argument list. Positional
// syntax is preserved too, although the installed Vita libc does not support it.
struct Specifier {
    const char* end;
    const char* length;
    const char* replacement;
};
Specifier Parse(const char* percent) {
    const char* p = percent + 1;
    if (*p == '%') return {p + 1, nullptr, nullptr};
    const char* initial = p;
    while (Digit(*p)) ++p;
    if (*p == '$') ++p;
    else p = initial;
    while (Flag(*p)) ++p;
    if (*p == '*') {
        ++p;
        while (Digit(*p)) ++p;
        if (*p == '$') ++p;
    } else {
        while (Digit(*p)) ++p;
    }
    if (*p == '.') {
        ++p;
        if (*p == '*') {
            ++p;
            while (Digit(*p)) ++p;
            if (*p == '$') ++p;
        } else {
            while (Digit(*p)) ++p;
        }
    }
    const char* length = p;
    const char* replacement = Replacement(*p);
    if (replacement) ++p;
    else if (*p == 'h' || *p == 'l') {
        const char first = *p++;
        if (*p == first) ++p;
    } else if (*p == 'L') ++p;
    if (!IntegerConversion(*p)) replacement = nullptr;
    return {*p ? p + 1 : p, length, replacement};
}

// First pass measures; second pass writes. Every copied byte contributes to the
// checked count, including the terminator. No precision/width or argument moves.
bool Transform(const char* input, char* output, size_t& size, bool& changed) {
    size_t used = 0;
    const auto put = [&](char c) {
        if (used == std::numeric_limits<size_t>::max()) return false;
        if (output) output[used] = c;
        ++used;
        return true;
    };
    const char* p = input;
    changed = false;
    while (*p) {
        if (*p != '%') {
            if (!put(*p++)) return false;
            continue;
        }
        const auto spec = Parse(p);
        if (!spec.replacement) {
            while (p != spec.end) if (!put(*p++)) return false;
            continue;
        }
        changed = true;
        while (p != spec.length) if (!put(*p++)) return false;
        for (const char* r = spec.replacement; *r; ++r) if (!put(*r)) return false;
        ++p; // The original single-character z/j/t modifier.
        while (p != spec.end) if (!put(*p++)) return false;
    }
    if (!put('\0')) return false;
    size = used;
    return true;
}

class Format {
public:
    explicit Format(const char* original) : text_(original) {
        size_t size = 0;
        bool changed = false;
        if (!Transform(original, nullptr, size, changed)) {
            errno = EOVERFLOW;
            text_ = nullptr;
            return;
        }
        if (!changed) return;
        char* destination = local_;
        if (size > sizeof(local_)) {
            allocated_ = static_cast<char*>(std::malloc(size));
            if (!allocated_) {
                errno = ENOMEM;
                text_ = nullptr;
                return;
            }
            destination = allocated_;
        }
        if (!Transform(original, destination, size, changed)) {
            errno = EOVERFLOW;
            text_ = nullptr;
            return;
        }
        text_ = destination;
    }
    ~Format() {
        const int savedError = errno;
        std::free(allocated_);
        errno = savedError;
    }
    Format(const Format&) = delete;
    Format& operator=(const Format&) = delete;
    const char* get() const { return text_; }
private:
    const char* text_;
    char* allocated_ = nullptr;
    char local_[512];
};

} // namespace

extern "C" int __wrap_vprintf(const char* format, va_list args) {
    const Format compatible(format);
    return compatible.get() ? __real_vprintf(compatible.get(), args) : -1;
}
extern "C" int __wrap_vfprintf(FILE* stream, const char* format, va_list args) {
    const Format compatible(format);
    return compatible.get() ? __real_vfprintf(stream, compatible.get(), args) : -1;
}
extern "C" int __wrap_vsprintf(char* buffer, const char* format, va_list args) {
    const Format compatible(format);
    return compatible.get() ? __real_vsprintf(buffer, compatible.get(), args) : -1;
}
extern "C" int __wrap_vsnprintf(char* buffer, size_t size, const char* format, va_list args) {
    const Format compatible(format);
    return compatible.get() ? __real_vsnprintf(buffer, size, compatible.get(), args) : -1;
}
extern "C" int __wrap_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int result = __wrap_vprintf(format, args);
    va_end(args);
    return result;
}
extern "C" int __wrap_fprintf(FILE* stream, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int result = __wrap_vfprintf(stream, format, args);
    va_end(args);
    return result;
}
extern "C" int __wrap_sprintf(char* buffer, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int result = __wrap_vsprintf(buffer, format, args);
    va_end(args);
    return result;
}
extern "C" int __wrap_snprintf(char* buffer, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int result = __wrap_vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}
