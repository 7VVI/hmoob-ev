#pragma once
#include <cstdio>
#include <windows.h>

// These macros return false/nullptr; use only in bool/pointer-returning functions.
#define HMMOB_HR_CHECK(expr)                                                   \
    do {                                                                       \
        HRESULT hr = (expr);                                                   \
        if (FAILED(hr)) {                                                      \
            std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__, \
                         static_cast<unsigned long>(hr));                      \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define HMMOB_HR_CHECK_PTR(expr)                                               \
    do {                                                                       \
        HRESULT hr = (expr);                                                   \
        if (FAILED(hr)) {                                                      \
            std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__, \
                         static_cast<unsigned long>(hr));                      \
            return nullptr;                                                    \
        }                                                                      \
    } while (0)
