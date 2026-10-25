#ifndef ODH_DATA_H
#define ODH_DATA_H

#include <stdint.h>

#if defined(_WIN32) && !defined(ODH_DATA_STATIC)
#  if defined(ODH_DATA_SHARED_BUILD)
#    define ODH_DATA_API __declspec(dllexport)
#  else
#    define ODH_DATA_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && !defined(ODH_DATA_STATIC)
#  define ODH_DATA_API __attribute__((visibility("default")))
#else
#  define ODH_DATA_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ODH_DATA_OK = 0,
    ODH_DATA_INVALID_ARGUMENT = 1,
    ODH_DATA_INVALID_FORMAT = 2,
    ODH_DATA_BUFFER_TOO_SMALL = 3,
    ODH_DATA_INTERNAL_ERROR = 4
};

ODH_DATA_API uint32_t odh_data_abi_version(void);

/* BCON: byte count, byte flags, then count little-endian 16-bit words.
 * All three metadata outputs are required and reset to zero on entry.
 * Null data is invalid even when len is zero. Null words is valid only with
 * capacity zero. On invalid input or internal failure, words is unchanged.
 * For a valid undersized output buffer, required metadata is returned but
 * words remains unchanged. Caller owns all buffers; no pointer is retained.
 * Input and output ranges must refer to valid storage and must not overlap
 * each other. Metadata and words pointers must be naturally aligned for their
 * C types. Decoded uint16_t words use the host's native in-memory byte order;
 * the BCON input bytes are decoded explicitly as little-endian values.
 */
ODH_DATA_API int32_t odh_bcon_decode_v1(
    const uint8_t* data, uint32_t len,
    uint16_t* words, uint32_t capacity,
    uint32_t* count, uint32_t* flags, uint32_t* remaining);

#ifdef __cplusplus
}
#endif

#endif
