#include "tool_call_assembler.hpp"
#include "logger.hpp"
#include <sstream>

ToolCallAssembler::ToolCallAssembler(size_t max_args_bytes_per_call)
    : max_args_bytes_(max_args_bytes_per_call) {}

bool ToolCallAssembler::ingest_delta(const nlohmann::json& delta, std::string* err) {
    // delta is a single element from choices[0].delta.tool_calls[]
    if (!delta.contains("index") || !delta["index"].is_number_integer()) {
        // Ignore malformed delta without index
        return true;
    }
    int idx = delta["index"].get<int>();

    // Guard: max number of distinct tool calls
    if (buffers_.find(idx) == buffers_.end()) {
        if (static_cast<int>(buffers_.size()) >= MAX_TOOL_CALLS) {
            if (err) *err = "ToolCallAssembler: exceeded MAX_TOOL_CALLS (" +
                            std::to_string(MAX_TOOL_CALLS) + ") at index " + std::to_string(idx);
            return false;
        }
        buffers_[idx] = ToolCallBuffer{};
    }

    ToolCallBuffer& buf = buffers_[idx];

    // Accumulate id (only the first non-empty value wins)
    if (buf.id.empty() && delta.contains("id") && delta["id"].is_string()) {
        buf.id = delta["id"].get<std::string>();
    }

    // Accumulate function name
    if (delta.contains("function")) {
        const auto& fn = delta["function"];
        if (buf.name.empty() && fn.contains("name") && fn["name"].is_string()) {
            buf.name = fn["name"].get<std::string>();
        }
        // Accumulate arguments fragment
        if (fn.contains("arguments") && fn["arguments"].is_string()) {
            const std::string& frag = fn["arguments"].get<std::string>();
            // Enforce per-call hard limit BEFORE appending
            if (buf.bytes_count + frag.size() > max_args_bytes_) {
                if (err) *err = "ToolCallAssembler: arguments buffer exceeded limit (" +
                                std::to_string(max_args_bytes_) + " bytes) for tool_call index " +
                                std::to_string(idx);
                return false;
            }
            buf.arguments_buffer += frag;
            buf.bytes_count += frag.size();
        }
    }

    return true;
}

bool ToolCallAssembler::finalize(std::vector<ToolCall>* out, std::string* err) const {
    if (!out) return true;
    out->clear();

    for (const auto& [idx, buf] : buffers_) {
        ToolCall tc;
        tc.index = idx;
        tc.id = buf.id.empty() ? ("call_" + std::to_string(idx)) : buf.id;
        tc.name = buf.name;
        tc.raw_arguments = buf.arguments_buffer;

        if (!buf.arguments_buffer.empty()) {
            try {
                tc.arguments = nlohmann::json::parse(buf.arguments_buffer);
            } catch (const nlohmann::json::exception& e) {
                // Build readable error with tail snippet (last 64 chars)
                const std::string& raw = buf.arguments_buffer;
                std::string tail = raw.size() > 64 ? raw.substr(raw.size() - 64) : raw;
                if (err) *err = "ToolCallAssembler: JSON parse failed for tool_call index " +
                                std::to_string(idx) + " (" + std::string(e.what()) +
                                "). Tail: \"" + tail + "\"";
                return false;
            }
        } else {
            tc.arguments = nlohmann::json::object();
        }

        out->push_back(std::move(tc));
    }

    return true;
}

void ToolCallAssembler::reset() {
    buffers_.clear();
}
