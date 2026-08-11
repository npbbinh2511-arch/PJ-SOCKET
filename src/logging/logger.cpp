#include "hftp/logging/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>

namespace hftp::logging {

std::string Logger::sanitize(std::string_view message) {
    std::string clean;
    clean.reserve(message.size());
    for (const char character : message) {
        clean.push_back(character == '\r' || character == '\n' ? ' ' : character);
    }
    return clean;
}

std::string_view Logger::level_name(Level level) noexcept {
    switch (level) {
    case Level::debug: return "DEBUG";
    case Level::info: return "INFO";
    case Level::warning: return "WARN";
    case Level::error: return "ERROR";
    }
    return "UNKNOWN";
}

void Logger::write(Level level, std::string_view message,
                   std::optional<std::uint64_t> session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (include_timestamp_) {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm local_time{};
#if defined(_WIN32)
        localtime_s(&local_time, &time);
#else
        localtime_r(&time, &local_time);
#endif
        output_ << '[' << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << "] ";
    }

    output_ << '[' << level_name(level) << ']';
    if (session_id.has_value()) {
        output_ << " [session=" << *session_id << ']';
    }
    output_ << ' ' << sanitize(message) << '\n';
    output_.flush();
}

} // namespace hftp::logging
