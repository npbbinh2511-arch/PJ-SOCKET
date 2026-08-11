#include "hftp/ui/progress.h"

#include <algorithm>
#include <limits>
#include <sstream>

namespace hftp::ui {

ProgressTracker::ProgressTracker(std::uint64_t total_bytes)
    : total_(total_bytes) {}

void ProgressTracker::reset(std::uint64_t total_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    transferred_ = 0;
    total_ = total_bytes;
}

void ProgressTracker::advance(std::uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bytes > std::numeric_limits<std::uint64_t>::max() - transferred_) {
        transferred_ = std::numeric_limits<std::uint64_t>::max();
    } else {
        transferred_ += bytes;
    }
    if (total_ > 0) {
        transferred_ = std::min(transferred_, total_);
    }
}

ProgressSnapshot ProgressTracker::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ProgressSnapshot result;
    result.transferred = transferred_;
    result.total = total_;
    result.complete = total_ == 0 ? transferred_ == 0 : transferred_ >= total_;
    result.percentage = total_ == 0
        ? (result.complete ? 100U : 0U)
        : static_cast<unsigned int>(
              (static_cast<long double>(transferred_) * 100.0L) /
              static_cast<long double>(total_));
    return result;
}

std::string ProgressTracker::render(std::size_t width) const {
    const auto state = snapshot();
    const auto filled = width * state.percentage / 100U;

    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < width; ++index) {
        output << (index < filled ? '=' : ' ');
    }
    output << "] " << state.percentage << "% ("
           << state.transferred << '/' << state.total << " bytes)";
    return output.str();
}

} // namespace hftp::ui
