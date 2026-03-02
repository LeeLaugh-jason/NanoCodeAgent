#pragma once

#include "tool_call.hpp"
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

class ToolCallAssembler {
public:
    static constexpr size_t MAX_TOOL_ARGS_BYTES = 1 * 1024 * 1024; // 1 MB per call
    static constexpr int    MAX_TOOL_CALLS      = 32;              // upper bound on number of calls

    explicit ToolCallAssembler(size_t max_args_bytes_per_call = MAX_TOOL_ARGS_BYTES);

    // Consume a single element of delta.tool_calls[].
    // Returns false and fills *err if any limit is exceeded.
    bool ingest_delta(const nlohmann::json& tool_call_delta, std::string* err);

    // Called once after stream ends (DONE / finish_reason tool_calls).
    // Parses each buffer's raw_arguments string; on JSON error fills *err with
    // a readable message including index + tail snippet.
    bool finalize(std::vector<ToolCall>* out, std::string* err) const;

    void reset();

private:
    struct ToolCallBuffer {
        std::string id;
        std::string name;
        std::string arguments_buffer;
        size_t      bytes_count = 0;
    };

    size_t                     max_args_bytes_;
    std::map<int, ToolCallBuffer> buffers_;
};
