#ifndef HFTP_LOGGING_LOGGER_H
#define HFTP_LOGGING_LOGGER_H

#include <cstdint>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace hftp::logging {

enum class Level { debug, info, warning, error };

class Logger {
public:
    explicit Logger(std::ostream& output, bool include_timestamp = true)
        : output_(output), include_timestamp_(include_timestamp) {}

    void write(Level level, std::string_view message,
               std::optional<std::uint64_t> session_id = std::nullopt);

    [[nodiscard]] static std::string sanitize(std::string_view message);
    [[nodiscard]] static std::string_view level_name(Level level) noexcept;

private:
    std::ostream& output_;
    bool include_timestamp_{true};
    std::mutex mutex_;
};

} // namespace hftp::logging

#endif // HFTP_LOGGING_LOGGER_H
