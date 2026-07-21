#pragma once

#include <cstdint>
#include <source_location>
#include <string>

namespace qstorage::infra {
enum class LogLevel : uint8_t {
    kTrace = 0,
    kDebug = 1,
    kInfo = 2,
    kWarn = 3,
    kError = 4,
    kFatal = 5,
};

constexpr std::string_view levelName(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::kTrace:
            return "TRACE";
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
        case LogLevel::kFatal:
            return "FATAL";
    }
    return "UNKNOWN";
}

class Logger {
   public:
    explicit Logger(std::string_view name);
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void trace(std::string_view msg,
               std::source_location loc = std::source_location::current());
    void debug(std::string_view msg,
               std::source_location loc = std::source_location::current());
    void info(std::string_view msg,
              std::source_location loc = std::source_location::current());
    void warn(std::string_view msg,
              std::source_location loc = std::source_location::current());
    void error(std::string_view msg,
               std::source_location loc = std::source_location::current());
    void fatal(std::string_view msg,
               std::source_location loc = std::source_location::current());
};

}  // namespace qstorage::infra