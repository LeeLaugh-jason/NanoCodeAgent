#pragma once

#include <string>
#include <nlohmann/json.hpp>

struct ToolCall {
    int index = 0;
    std::string id;        // may be empty if not provided; fallback: "call_<index>"
    std::string name;
    nlohmann::json arguments; // parsed JSON object after finalize
    std::string raw_arguments; // the concatenated raw string before parsing
};
