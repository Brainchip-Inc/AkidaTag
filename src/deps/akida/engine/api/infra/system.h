#pragma once

#if defined(__cplusplus)
extern "C" { /* C-declarations in C++ programs */
#endif

#include <cstdint>

#include "infra/exports.h"

/**
 * @brief wait for a given duration
 * @param duration: how long should be waited, in milliseconds
 */
AKIDASHAREDLIB_EXPORT void msleep(uint32_t duration);

/**
 * @brief return monotone time in milliseconds
 */
AKIDASHAREDLIB_EXPORT int64_t time_ms();

/**
 * @brief signal watchdog while the library is busy during long processing.
 */
AKIDASHAREDLIB_EXPORT void kick_watchdog();

/**
 * @brief generate an unrecoverable error (exception), whose description is
 * formatted as a string.
 */
AKIDASHAREDLIB_EXPORT void panic [[noreturn]] (const char* format, ...);

#if defined(__cplusplus)
} /* C-declarations in C++ programs */
#endif
