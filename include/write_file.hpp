#pragma once

#include <string>
#include <string_view>

struct WriteResult {
    bool ok;
    size_t bytes_written;
    std::string abs_path;
    std::string err;
};

/**
 * @brief Safely write a file preventing Symlink/TOCTOU escapes.
 * 
 * @param workspace_abs The absolute path to the workspace root.
 * @param rel_path The relative path representing the target file.
 * @param content The content payload to write.
 * @param max_write_bytes Upper bound for payload limit (defaults to 4MB).
 * @return WriteResult Struct including operation success, bytes written, and errors.
 */
WriteResult write_file_safe(const std::string& workspace_abs,
                            const std::string& rel_path,
                            std::string_view content,
                            size_t max_write_bytes = 4 * 1024 * 1024);
