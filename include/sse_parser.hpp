#pragma once

#include <string>
#include <vector>
#include <stdexcept>

class SseParser {
public:
    static constexpr size_t MAX_REMAINDER_BYTES = 256 * 1024; // 256KB limit

    SseParser() = default;

    std::vector<std::string> feed(const std::string& chunk);
    bool is_done() const { return done_; }

private:
    std::string remainder_;
    bool done_{false};
};
