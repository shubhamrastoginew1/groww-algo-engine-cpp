#include "ge/core/logger.hpp"

#include <atomic>
#include <cstdio>
#include <ctime>

namespace ge {
namespace {
std::atomic<LogLevel> g_level{LogLevel::Info};
std::mutex g_mu;
const char* kNames[] = {"DEBUG", "INFO", "WARNING", "ERROR"};
}  // namespace

void set_log_level(LogLevel level) { g_level = level; }

void log_write(LogLevel level, std::string_view name, std::string_view msg) {
    if (level < g_level.load()) return;
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char ts[16];
    std::strftime(ts, sizeof ts, "%H:%M:%S", &tm);
    std::lock_guard lk(g_mu);
    std::fprintf(stderr, "%s | %-7s | %-10.*s | %.*s\n", ts, kNames[static_cast<int>(level)],
                 static_cast<int>(name.size()), name.data(), static_cast<int>(msg.size()), msg.data());
}

}  // namespace ge
