#include "sse_parser.hpp"
#include "logger.hpp"

std::vector<std::string> SseParser::feed(const std::string& chunk) {
    if (done_) {
        return {};
    }

    remainder_ += chunk;

    if (remainder_.size() > MAX_REMAINDER_BYTES) {
        LOG_ERROR("SSE remainder exceeded MAX_REMAINDER_BYTES");
        throw std::runtime_error("SSE remainder exceeded MAX_REMAINDER_BYTES");
    }

    std::vector<std::string> payloads;

    while (!done_) {
        size_t event_end_pos = std::string::npos;
        size_t copy_skip = 0;

        size_t nn_pos = remainder_.find("\n\n");
        size_t rnrn_pos = remainder_.find("\r\n\r\n");

        if (nn_pos != std::string::npos && (rnrn_pos == std::string::npos || nn_pos < rnrn_pos)) {
            event_end_pos = nn_pos;
            copy_skip = 2;
        } else if (rnrn_pos != std::string::npos) {
            event_end_pos = rnrn_pos;
            copy_skip = 4;
        } else {
            break;
        }

        std::string event_str = remainder_.substr(0, event_end_pos);
        remainder_.erase(0, event_end_pos + copy_skip);

        std::string data;
        bool has_data = false;
        size_t line_start = 0;

        while (line_start < event_str.size()) {
            size_t line_end = event_str.find('\n', line_start);
            if (line_end == std::string::npos) {
                line_end = event_str.size();
            }

            size_t actual_end = line_end;
            if (actual_end > line_start && event_str[actual_end - 1] == '\r') {
                actual_end--;
            }

            std::string line = event_str.substr(line_start, actual_end - line_start);

            if (!line.empty() && line[0] != ':') {
                if (line.starts_with("data:")) {
                    size_t val_start = 5;
                    if (val_start < line.size() && line[val_start] == ' ') {
                        val_start++;
                    }
                    if (has_data) {
                        data += '\n'; // multiline data
                    } else {
                        has_data = true;
                    }
                    data += line.substr(val_start);
                }
            }

            line_start = line_end + 1;
        }

        if (has_data) {
            if (data == "[DONE]") {
                LOG_DEBUG("Received [DONE] signal");
                done_ = true;
                break;
            }
            payloads.push_back(data);
        }
    }

    return payloads;
}
