
#pragma once
#include "logger.h"

namespace qstorage::tools {

// siezeof(enum) =4 ,sizeof( enum: char)= 1
enum class InfoLogLevel : unsigned char {

    DEBUG_LEVEL = 0,
    INFO_LEVEL,
    WARN_LEVEL,
    ERROR_LEVEL,
    FATAL_LEVEL,
    HEADER_LEVEL,
    NUM_INFO_LOG_LEVELS,
};

void Log(const InfoLogLevel log_level, Logger& logger, const char* format,
         ...) {}

inline const char* LogShorterFileName(const char* file) {
    return file + (sizeof(__FILE__) > 18 ? sizeof(__FILE__) - 18 : 0);
}
}  // namespace qstorage::tools

#define LOG_TOSTRING(x) #x

#define LOG_PREPEND_FILE_LINE(fmt) ("[%s:" LOG_TOSTRING(__LINE__) "]" fmt)

#define LOG_LEVEL(logger, level, fmt, ...)             \
    Log((level), (logger), LOG_PREPEND_FILE_LINE(fmt), \
        LogShorterFileName(__FILE__), ##__VA_ARGS__)

#define LOG_WARN(logger, fmt, ...) \
    LOG_LEVEL(logger, InfoLogLevel::WARN_LEVEL, fmt, ##__VA_ARGS__)

#define LOG_INFO(logger, fmt, ...) \
    LOG_LEVEL(logger, InfoLogLevel::INFO_LEVEL, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(logger, fmt, ...) \
    LOG_LEVEL(logger, InfoLogLevel::DEBUG_LEVEL, fmt, ##__VA_ARGS__)

#define LOG_ERROR(logger, fmt, ...) \
    LOG_LEVEL(logger, InfoLogLevel::ERROR_LEVEL, fmt, ##__VA_ARGS__)

#define LOG_FATAL(logger, fmt, ...) \
    LOG_LEVEL(logger, InfoLogLevel::FATAL_LEVEL, fmt, ##__VA_ARGS__)

/*
    在 C++ 中，宏（#define 定义的标识符）确实没有传统意义上的作用域概念，它的生效范围是从定义点开始，到被 #undef 取消定义为止，
    或者到文件结束（以先到者为准）。    
    
    宏由预处理器处理，预处理器在编译阶段之前执行，不理解 C++ 的块作用域（{} 内）、函数作用域、类作用域等语法结构。
    即使在某个块内定义宏（例如 if 语句块、函数体内部），它的生效范围也不会被块限制，而是从定义点一直延续到 #undef 或文件结束。
    
    ```cpp
    void func() {
        #define M 10
        int a = M; // 有效
    }
    int b = M; // 仍然有效（除非在 func 内用 #undef 取消）
    ```
    
    */
