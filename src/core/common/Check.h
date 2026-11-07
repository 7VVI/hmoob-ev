#pragma once
#include <cstdio>
#include <windows.h>

#define HMMOB_HR_CHECK(expr)                                                   \
    do {                                                                       \
        HRESULT hr__ = (expr);                                                 \
        if (FAILED(hr__)) {                                                    \
            std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__, \
                         static_cast<unsigned long>(hr__));                    \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define HMMOB_HR_CHECK_PTR(expr)                                               \
    do {                                                                       \
        HRESULT hr__ = (expr);                                                 \
        if (FAILED(hr__)) {                                                    \
            std::fprintf(stderr, "%s:%d HRESULT=0x%08lx\n", __FILE__, __LINE__, \
                         static_cast<unsigned long>(hr__));                    \
            return nullptr;                                                    \
        }                                                                      \
    } while (0)
