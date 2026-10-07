#pragma once
// Minimal thread-safe logger (stderr) with a level filter.
#include <mutex>
#include <string>
#include <string_view>

namespace ge {

enum class LogLevel { Debug = 0, Info, Warning, Error };

void set_log_level(LogLevel level);
void log_write(LogLevel level, std::string_view name, std::string_view msg);

class Logger {
public:
    explicit Logger(std::string name) : name_(std::move(name)) {}
    void debug(std::string_view m) const { log_write(LogLevel::Debug, name_, m); }
    void info(std::string_view m) const { log_write(LogLevel::Info, name_, m); }
    void warn(std::string_view m) const { log_write(LogLevel::Warning, name_, m); }
    void error(std::string_view m) const { log_write(LogLevel::Error, name_, m); }

private:
    std::string name_;
};

}  // namespace ge
