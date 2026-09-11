#pragma once

#ifdef _WIN32
#define AKIDASHAREDLIB_EXPORT __declspec(dllexport)
#elif (__GNUC__ || __clang__)
#define AKIDASHAREDLIB_EXPORT __attribute__((visibility("default")))
#else
#define AKIDASHAREDLIB_EXPORT
#endif
