#pragma once

// clang-format off
#ifdef xlog_STATIC
    #define xlog_API
#else
    #if defined(_WIN32) || defined(_WIN64)
        #ifdef xlog_EXPORTS
            #define xlog_API __declspec(dllexport)
        #else
            #define xlog_API __declspec(dllimport)
        #endif
    #else
        #define xlog_API
    #endif
#endif
// clang-format on
