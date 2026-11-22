#pragma once

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace nc {

inline std::mutex& logMutex() {
    static std::mutex mutex;
    return mutex;
}

inline void logRaw(const char* level, const char* fmt, va_list args) {
    std::lock_guard<std::mutex> lock(logMutex());
    std::fprintf(stderr, "[nativecraft][%s] ", level);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

inline void logInfo(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logRaw("info", fmt, args);
    va_end(args);
}

inline void logWarn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logRaw("warn", fmt, args);
    va_end(args);
}

inline void logError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logRaw("error", fmt, args);
    va_end(args);
}

}
