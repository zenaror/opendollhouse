/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

// VitaSDK's libc omits C99 z/j/t length modifiers. Link the Vita application
// with --wrap for all eight functions below; other platforms use libc directly.
#ifdef __cplusplus
extern "C" {
#endif
int __wrap_printf(const char* format, ...);
int __wrap_fprintf(FILE* stream, const char* format, ...);
int __wrap_sprintf(char* buffer, const char* format, ...);
int __wrap_snprintf(char* buffer, size_t size, const char* format, ...);
int __wrap_vprintf(const char* format, va_list args);
int __wrap_vfprintf(FILE* stream, const char* format, va_list args);
int __wrap_vsprintf(char* buffer, const char* format, va_list args);
int __wrap_vsnprintf(char* buffer, size_t size, const char* format, va_list args);
#ifdef __cplusplus
}
#endif
