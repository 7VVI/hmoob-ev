#pragma once
#include <cstdio>
#include <windows.h>

// These macros return false/nullptr; use only in bool/pointer-returning functions.
#define HMMOB_HR_CHECK(expr)                                                   \
    do {                                                                       \
        HRESULT hmmob_hr_ = (expr);                                            \
        if (FAILED(hmmob_hr_)) {                                               \
            std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__, \
                         static_cast<unsigned long>(hmmob_hr_));               \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define HMMOB_HR_CHECK_PTR(expr)                                               \
    do {                                                                       \
        HRESULT hmmob_hr_ = (expr);                                            \
        if (FAILED(hmmob_hr_)) {                                               \
            std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__, \
                         static_cast<unsigned long>(hmmob_hr_));               \
            return nullptr;                                                    \
        }                                                                      \
    } while (0)
