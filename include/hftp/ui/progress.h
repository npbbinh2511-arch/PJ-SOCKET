#ifndef HFTP_UI_PROGRESS_H
#define HFTP_UI_PROGRESS_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace hftp::ui {

struct ProgressSnapshot {
    std::uint64_t transferred{0};
    std::uint64_t total{0};
    unsigned int percentage{0};
    bool complete{false};
};

class ProgressTracker {
public:
    explicit ProgressTracker(std::uint64_t total_bytes = 0);

    void reset(std::uint64_t total_bytes);
    void advance(std::uint64_t bytes);
    [[nodiscard]] ProgressSnapshot snapshot() const;
    [[nodiscard]] std::string render(std::size_t width = 30) const;

private:
    mutable std::mutex mutex_;
    std::uint64_t transferred_{0};
    std::uint64_t total_{0};
};

} // namespace hftp::ui

#endif // HFTP_UI_PROGRESS_H
