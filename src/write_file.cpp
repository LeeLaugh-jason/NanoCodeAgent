#include "write_file.hpp"
#include "workspace.hpp"
#include "config.hpp"

#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <system_error>
#include <cstring>
#include <vector>

namespace fs = std::filesystem;

WriteResult write_file_safe(const std::string& workspace_abs,
                            const std::string& rel_path,
                            std::string_view content,
                            size_t max_write_bytes) {
    // 1) Validate via workspace_resolve to prevent trivial ".." escapes
    AgentConfig dummy_cfg;
    dummy_cfg.workspace_abs = workspace_abs;
    
    std::string safe_abs_path;
    std::string err_msg;
    if (!workspace_resolve(dummy_cfg, rel_path, &safe_abs_path, &err_msg)) {
        return {false, 0, "", "Path resolution failed: " + err_msg};
    }

    // 2) Enforce strict upper bound limit on writes (DoD Requirement)
    if (content.size() > max_write_bytes) {
        return {false, 0, "", "Write size (" + std::to_string(content.size()) + 
                              " bytes) exceeded MAX_WRITE_BYTES limit (" + 
                              std::to_string(max_write_bytes) + ")"};
    }

    // 3) TOCTOU safe traversal preventing any symlink components
    int dir_fd = open(workspace_abs.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd < 0) {
        return {false, 0, "", std::string("Failed to open workspace root: ") + strerror(errno)};
    }

    fs::path rel_p(rel_path);
    rel_p = rel_p.lexically_normal(); 

    // Extract directory components and the final filename correctly
    std::vector<std::string> components;
    for (const auto& p : rel_p) {
        if (p.string() != "." && p.string() != "/") { 
            components.push_back(p.string());
        }
    }

    if (components.empty()) {
        close(dir_fd);
        return {false, 0, "", "Invalid empty path target"};
    }

    std::string filename = components.back();
    components.pop_back();

    // Iterate through directory components safely
    for (const auto& comp : components) {
        // Attempt to open the component checking it is strictly a directory without following symlinks
        int next_fd = openat(dir_fd, comp.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        
        if (next_fd < 0) {
            if (errno == ENOENT) {
                // If the path component does not exist, automatically attempt to create it (0755)
                if (mkdirat(dir_fd, comp.c_str(), 0755) < 0) {
                    std::string e = strerror(errno);
                    close(dir_fd);
                    return {false, 0, "", "Failed to create directory component '" + comp + "': " + e};
                }
                
                // Re-open after creation
                next_fd = openat(dir_fd, comp.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            }
            
            // If it still fails (or failed due to being a symlink -> ELOOP / ENOTDIR), reject entirely!
            if (next_fd < 0) {
                std::string e = strerror(errno);
                close(dir_fd);
                return {false, 0, "", "Failed to securely traverse directory '" + comp + "': " + e};
            }
        }
        
        // Progress down the tree securely
        close(dir_fd);
        dir_fd = next_fd;
    }

    // Now dir_fd strictly points safely to the innermost directory without any intermediate symlinks traversed.
    // Ensure final file is securely opened without following symlinks and truncated if existing.
    int fd = openat(dir_fd, filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        std::string e = strerror(errno);
        close(dir_fd);
        return {false, 0, "", "Failed to securely open target file '" + filename + "': " + e};
    }

    // Final POSIX defense: check fd is strictly a regular file (e.g., rejecting FIFOs/char devices hiding inside workspace)
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        close(dir_fd);
        return {false, 0, "", "Target is not a statistically regular file type"};
    }

    // Write content buffer correctly handling partial writes and EINTR
    size_t total_written = 0;
    const char* ptr = content.data();
    size_t left = content.size();
    
    while (left > 0) {
        ssize_t written = write(fd, ptr, left);
        if (written < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            std::string e = strerror(errno);
            close(fd);
            close(dir_fd);
            return {false, total_written, "", "Filesystem IO error during write: " + e};
        }
        if (written == 0) {
            // Unlikely for regular files but fail-safe against infinite loops
            std::string e = "Unexpected explicit zero-byte write loop";
            close(fd);
            close(dir_fd);
            return {false, total_written, "", "Filesystem IO error: " + e};
        }
        ptr += written;
        left -= written;
        total_written += written;
    }

    close(fd);
    close(dir_fd);

    if (total_written != content.size()) {
        return {false, total_written, "", "Incomplete file write (short write aborted)"};
    }

    return {true, total_written, safe_abs_path, ""};
}
