#pragma once
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

enum class LogLevel { Info, Warn, Error };

struct LogEntry {
    LogLevel level;
    std::string text;
};

// Simple global logger. Keeps a ring buffer so the editor UI can show a console panel.
class Log {
public:
    static void info(const char* fmt, ...)  { va_list a; va_start(a, fmt); write(LogLevel::Info,  fmt, a); va_end(a); }
    static void warn(const char* fmt, ...)  { va_list a; va_start(a, fmt); write(LogLevel::Warn,  fmt, a); va_end(a); }
    static void error(const char* fmt, ...) { va_list a; va_start(a, fmt); write(LogLevel::Error, fmt, a); va_end(a); }

    static std::vector<LogEntry> snapshot() {
        std::lock_guard<std::mutex> lock(mutex());
        return entries();
    }
    static void clear() {
        std::lock_guard<std::mutex> lock(mutex());
        entries().clear();
    }

private:
    static std::mutex& mutex() { static std::mutex m; return m; }
    static std::vector<LogEntry>& entries() { static std::vector<LogEntry> e; return e; }

    static void write(LogLevel level, const char* fmt, va_list args) {
        char buf[2048];
        vsnprintf(buf, sizeof(buf), fmt, args);

        const char* tag = level == LogLevel::Info ? "INFO" : level == LogLevel::Warn ? "WARN" : "ERROR";
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char stamp[16];
        std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm);
        fprintf(level == LogLevel::Error ? stderr : stdout, "[%s][%s] %s\n", stamp, tag, buf);

        std::lock_guard<std::mutex> lock(mutex());
        auto& e = entries();
        e.push_back({level, std::string("[") + stamp + "] " + buf});
        if (e.size() > 2000) e.erase(e.begin(), e.begin() + 1000);
    }
};
