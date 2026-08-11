#include "hftp/logging/logger.h"
#include "hftp/ui/progress.h"

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "[FAIL] " << __FILE__ << ':' << __LINE__ \
                      << " -> " #condition "\n"; \
            return false; \
        } \
    } while (false)

namespace {

bool test_progress_tracker() {
    hftp::ui::ProgressTracker progress(10);
    progress.advance(4);
    auto state = progress.snapshot();
    TEST_CHECK(state.transferred == 4);
    TEST_CHECK(state.total == 10);
    TEST_CHECK(state.percentage == 40);
    TEST_CHECK(!state.complete);
    TEST_CHECK(progress.render(10) == "[====      ] 40% (4/10 bytes)");

    progress.advance(100);
    state = progress.snapshot();
    TEST_CHECK(state.transferred == 10);
    TEST_CHECK(state.percentage == 100);
    TEST_CHECK(state.complete);

    progress.reset(1'000);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&progress] {
            for (int count = 0; count < 250; ++count) {
                progress.advance(1);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    TEST_CHECK(progress.snapshot().complete);
    TEST_CHECK(progress.snapshot().transferred == 1'000);

    std::cout << "[PASS] Progress calculation, saturation and thread safety\n";
    return true;
}

bool test_logger() {
    std::ostringstream output;
    hftp::logging::Logger logger(output, false);
    logger.write(hftp::logging::Level::info, "connected", 42);
    logger.write(hftp::logging::Level::warning, "bad\r\nforged line");
    logger.write(hftp::logging::Level::error, "transfer failed");

    const auto text = output.str();
    TEST_CHECK(text.find("[INFO] [session=42] connected\n") != std::string::npos);
    TEST_CHECK(text.find("[WARN] bad  forged line\n") != std::string::npos);
    TEST_CHECK(text.find("[ERROR] transfer failed\n") != std::string::npos);
    TEST_CHECK(text.find("\r") == std::string::npos);
    TEST_CHECK(hftp::logging::Logger::sanitize("a\nb") == "a b");

    std::cout << "[PASS] Structured logger and log-forging protection\n";
    return true;
}

} // namespace

int main() {
    if (!test_progress_tracker() || !test_logger()) {
        return 1;
    }
    std::cout << "==> ALL PROGRESS/LOGGING TESTS PASSED! <==\n";
    return 0;
}
