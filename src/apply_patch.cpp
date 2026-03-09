/// src/apply_patch.cpp
///
/// Exact-text replacement primitive used by the apply_patch tool.
///
/// Design notes
/// ─────────────
/// • Uses read_file_safe / write_file_safe for all I/O, inheriting their
///   workspace-boundary enforcement (workspace_resolve + O_NOFOLLOW).
/// • count_occurrences_with_overlap advances by 1 per iteration so that
///   overlapping occurrences (e.g. "aa" in "aaa") are counted correctly.
/// • Truncated or binary reads are rejected immediately; no patch is applied.
/// • Batch mode applies all patches in memory first; the file is written back
///   only if every patch succeeds.  Note: write_file_safe does NOT perform a
///   filesystem-atomic rename — no temp-file / fsync / rename is used.

#include "apply_patch.hpp"
#include "read_file.hpp"
#include "write_file.hpp"

#include <limits>
#include <string>
#include <vector>

namespace {

/// Count how many times needle appears in haystack, including overlapping
/// occurrences.  Advances the search position by 1 on every hit so that
/// "aa" in "aaa" is counted as 2, not 1.
static int count_occurrences_with_overlap(const std::string& haystack,
                                          const std::string& needle) {
    if (needle.empty()) {
        return 0;
    }
    int count = 0;
    std::string::size_type pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += 1;  // advance by 1, NOT by needle.size(), to detect overlaps
    }
    return count;
}

/// Apply a single (old_text → new_text) replacement on an in-memory string.
/// Returns {ok, match_count, err}.  On success, content is modified in place.
/// On failure, content is left unchanged.
static ApplyPatchResult apply_single_on_content(std::string& content,
                                                const std::string& old_text,
                                                const std::string& new_text) {
    if (old_text.empty()) {
        return {false, 0, "old_text must not be empty"};
    }

    const int count = count_occurrences_with_overlap(content, old_text);

    if (count == 0) {
        return {false, 0, "old_text not found in file content"};
    }
    if (count > 1) {
        return {false, count,
                "old_text occurs " + std::to_string(count) +
                    " times in file; it must be unique"};
    }

    // Exactly one match: replace it.
    const std::string::size_type pos = content.find(old_text);
    content.replace(pos, old_text.size(), new_text);
    return {true, 1, ""};
}

}  // namespace

ApplyPatchResult apply_patch_single(const std::string& workspace_abs,
                                    const std::string& rel_path,
                                    const std::string& old_text,
                                    const std::string& new_text) {
    if (old_text.empty()) {
        return {false, 0, "old_text must not be empty"};
    }

    const auto rr = read_file_safe(workspace_abs, rel_path);
    if (rr.is_binary) {
        return {false, 0,
                "file appears to be binary; refusing to patch binary content"};
    }
    if (rr.truncated) {
        return {false, 0,
                "file was truncated during read; refusing to patch truncated content"};
    }
    if (!rr.ok) {
        return {false, 0, "failed to read file: " + rr.err};
    }

    std::string content = rr.content;
    ApplyPatchResult result = apply_single_on_content(content, old_text, new_text);
    if (!result.ok) {
        return result;
    }

    const auto wr = write_file_safe(workspace_abs, rel_path, content);
    if (!wr.ok) {
        return {false, 1,
                "patch applied in memory but write-back failed: " + wr.err};
    }

    return {true, 1, ""};
}

ApplyPatchResult apply_patch_batch(const std::string& workspace_abs,
                                   const std::string& rel_path,
                                   const std::vector<PatchEntry>& patches) {
    if (patches.empty()) {
        return {false, 0, "'patches' array must not be empty"};
    }
    if (patches.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {false, 0, "'patches' array is too large (exceeds INT_MAX entries)"};
    }

    const auto rr = read_file_safe(workspace_abs, rel_path);
    if (rr.is_binary) {
        return {false, 0,
                "file appears to be binary; refusing to patch binary content"};
    }
    if (rr.truncated) {
        return {false, 0,
                "file was truncated during read; refusing to patch truncated content"};
    }
    if (!rr.ok) {
        return {false, 0, "failed to read file: " + rr.err};
    }

    // Apply all patches in memory before touching the file.
    std::string content = rr.content;
    for (std::size_t i = 0; i < patches.size(); ++i) {
        ApplyPatchResult step =
            apply_single_on_content(content, patches[i].old_text, patches[i].new_text);
        if (!step.ok) {
            return {false, static_cast<int>(i),
                    "patch[" + std::to_string(i) + "]: " + step.err};
        }
    }

    // All patches succeeded in memory — write back once.
    // Note: write_file_safe is not filesystem-atomic (no temp-file+fsync+rename).
    const auto wr = write_file_safe(workspace_abs, rel_path, content);
    if (!wr.ok) {
        return {false, static_cast<int>(patches.size()),
                "patches applied in memory but write-back failed: " + wr.err};
    }

    return {true, static_cast<int>(patches.size()), ""};
}
