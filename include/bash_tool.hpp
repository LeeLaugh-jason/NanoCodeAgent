#pragma once

#include <string>

struct BashResult {
    bool ok;
    int exit_code;
    std::string out_tail;
    std::string err_tail;
    bool truncated;
    bool timed_out;
    size_t out_bytes;
    size_t err_bytes;
    std::string err;
};

/**
 * @brief Safely executes raw bash commands inside a secured, resource-limited boundary context.
 * 
 * @param workspace_abs The strict absolute trajectory this process will be locked inside.
 * @param command The bash command payload string to parse and execute via generic POSIX shell.
 * @param timeout_ms Global ceiling limit timer for process execution (default: 20 seconds).
 * @param max_out Absolute standard output overflow threshold (default 1MB).
 * @param max_err Absolute stderr boundary barrier limitation (default 1MB).
 * @return BashResult Complete trace reporting statuses on outputs, bounds, and terminal signals.
 */
BashResult bash_execute_safe(const std::string& workspace_abs,
                             const std::string& command,
                             int timeout_ms = 20000,
                             size_t max_out = 1024 * 1024,
                             size_t max_err = 1024 * 1024);
