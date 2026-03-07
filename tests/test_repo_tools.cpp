#include <gtest/gtest.h>

#include "agent_tools.hpp"
#include "repo_tools.hpp"
#include "tool_call.hpp"
#include "config.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string shell_escape_single_quotes(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

int run_bash(const std::string& command) {
    const std::string wrapped = "bash -lc '" + shell_escape_single_quotes(command) + "'";
    return std::system(wrapped.c_str());
}

} // namespace

class RepoToolsTest : public ::testing::Test {
protected:
    std::string test_workspace;

    void SetUp() override {
        auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        test_workspace = (fs::temp_directory_path() / ("nano_repo_tools_" + std::to_string(tick))).string();
        fs::create_directories(test_workspace);
    }

    void TearDown() override {
        clear_rg_binary_for_testing();
        fs::remove_all(test_workspace);
    }

    void create_file(const std::string& rel_path, const std::string& content) {
        fs::path path = fs::path(test_workspace) / rel_path;
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    std::string create_fake_rg_script() {
        const fs::path script_path = fs::path(test_workspace) / "fake-rg.sh";
        std::ofstream out(script_path);
        out << "#!/bin/sh\n";
        // Args: rg --json --line-number --color never -e <query> <dir>
        // Positional: $0=rg_binary $1=--json $2=--line-number $3=--color $4=never
        //             $5=-e $6=<query> $7=<dir>
        out << "query=\"$6\"\n";
        out << "target=\"$7\"\n";
        out << "if [ \"$query\" = \"needle\" ] || [ \"$query\" = \"-dash-query\" ]; then\n";
        out << "  grep -R -n -- \"needle\" \"$target\" | while IFS=: read -r file line rest; do\n";
        out << "    printf '{\"type\":\"match\",\"data\":{\"path\":{\"text\":\"%s\"},\"line_number\":%s,\"submatches\":[{\"start\":0}],\"lines\":{\"text\":\"match:%s\\\\n\"}}}\\n' \"$file\" \"$line\" \"$query\"\n";
        out << "  done\n";
        out << "  exit 0\n";
        out << "fi\n";
        out << "exit 1\n";
        out.close();
        fs::permissions(script_path,
                        fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                        fs::perms::group_exec | fs::perms::group_read |
                        fs::perms::others_exec | fs::perms::others_read);
        return script_path.string();
    }
};

TEST_F(RepoToolsTest, ListFilesBoundedRespectsLimitAndTruncation) {
    create_file("a.cpp", "int a = 1;\n");
    create_file("b.txt", "skip\n");
    create_file(".git/config", "[core]\n");
    create_file("src/c.hpp", "#pragma once\n");
    create_file("src/d.cpp", "int d = 1;\n");

    const auto result = list_files_bounded(test_workspace, "", {".cpp", ".hpp"}, 2);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_TRUE(result["truncated"].get<bool>());
    EXPECT_EQ(result["returned"].get<size_t>(), 2U);
    ASSERT_EQ(result["files"].size(), 2);
    EXPECT_EQ(result["files"][0]["path"], "a.cpp");
    EXPECT_EQ(result["files"][1]["path"], "src/c.hpp");
    for (const auto& file : result["files"]) {
        EXPECT_FALSE(file["path"].get<std::string>().starts_with(".git/"));
    }
}

TEST_F(RepoToolsTest, RgSearchFindsMatchesWithInjectedBinary) {
    create_file("src/sample.cpp", "int needle = 42;\n");
    set_rg_binary_for_testing(create_fake_rg_script());

    const auto result = rg_search(test_workspace, "needle", "src", 10, 20);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_EQ(result["directory"], "src");
    EXPECT_EQ(result["returned"].get<size_t>(), 1U);
    ASSERT_EQ(result["matches"].size(), 1);
    EXPECT_EQ(result["matches"][0]["file"], "src/sample.cpp");
    EXPECT_EQ(result["matches"][0]["line"], 1);
    EXPECT_NE(result["matches"][0]["snippet"].get<std::string>().find("needle"), std::string::npos);
}

TEST_F(RepoToolsTest, RgSearchReturnsReadableErrorWhenBinaryCannotExecute) {
    set_rg_binary_for_testing((fs::path(test_workspace) / "missing-rg").string());

    const auto result = rg_search(test_workspace, "needle", "", 10, 40);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("failed to exec"), std::string::npos);
}

TEST_F(RepoToolsTest, GitStatusFailsGracefullyOutsideGitRepo) {
    const auto result = git_status(test_workspace, 0);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("git repository"), std::string::npos);
}

TEST_F(RepoToolsTest, GitStatusParsesRepositoryStateAndTruncates) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);

    create_file("tracked.txt", "hello\n");
    create_file("untracked.txt", "world\n");

    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add tracked.txt >/dev/null 2>&1"), 0);

    const auto full = git_status(test_workspace, 10);
    ASSERT_TRUE(full["ok"].get<bool>()) << full["error"];
    EXPECT_EQ(full["branch"], "main");
    EXPECT_TRUE(full["has_changes"].get<bool>());
    ASSERT_GE(full["entries"].size(), 2U);

    const auto limited = git_status(test_workspace, 1);
    ASSERT_TRUE(limited["ok"].get<bool>()) << limited["error"];
    EXPECT_TRUE(limited["truncated"].get<bool>());
    ASSERT_EQ(limited["entries"].size(), 1U);
}

TEST_F(RepoToolsTest, FindExecutableResolvesFromProcessPath) {
    // Create a fake rg binary named exactly "rg" in a custom directory so that
    // find_executable_in_path must locate it through the PATH env var rather
    // than the hardcoded fallback directories.
    const fs::path custom_bin = fs::path(test_workspace) / "custom-bin";
    fs::create_directories(custom_bin);
    const fs::path fake_rg = custom_bin / "rg";
    {
        std::ofstream out(fake_rg);
        out << "#!/bin/sh\n";
        // Emit one rg-format match line regardless of arguments.
        // Using echo with single-quoted JSON avoids shell escape complexity.
        out << "echo '{\"type\":\"match\",\"data\":{\"path\":{\"text\":\"found.cpp\"},"
               "\"line_number\":1,\"submatches\":[{\"start\":0}],"
               "\"lines\":{\"text\":\"line\"}}}' \n";
        out << "exit 0\n";
    }
    fs::permissions(fake_rg,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_exec | fs::perms::group_read |
                    fs::perms::others_exec | fs::perms::others_read);

    // Save and override PATH so our custom-bin directory is searched first.
    const char* old_path_cstr = std::getenv("PATH");
    const std::string old_path = old_path_cstr ? old_path_cstr : "";
    const bool had_path = (old_path_cstr != nullptr);
    const std::string new_path = custom_bin.string() + (had_path ? ":" + old_path : "");
    setenv("PATH", new_path.c_str(), 1);

    // Ensure override is cleared so rg_search uses find_executable_in_path.
    clear_rg_binary_for_testing();
    const auto result = rg_search(test_workspace, "anything", "", 10, 40);

    // Restore PATH unconditionally before any assertions.
    if (had_path) {
        setenv("PATH", old_path.c_str(), 1);
    } else {
        unsetenv("PATH");
    }

    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_EQ(result["returned"].get<size_t>(), 1U);
}

TEST_F(RepoToolsTest, GitStatusDoesNotSplitFilenameContainingArrow) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);

    // A file whose name legitimately contains the " -> " substring.
    create_file("a -> b.txt", "content\n");

    const auto result = git_status(test_workspace, 10);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];

    bool found = false;
    for (const auto& entry : result["entries"]) {
        // Git may C-quote filenames containing spaces, so check with find().
        if (entry["path"].get<std::string>().find("a -> b.txt") != std::string::npos) {
            found = true;
            EXPECT_FALSE(entry.contains("orig_path"))
                << "Untracked file with arrow in name must not gain a fabricated orig_path";
        }
    }
    EXPECT_TRUE(found) << "Expected an entry for 'a -> b.txt' in git status output";
}

TEST_F(RepoToolsTest, RgSearchHandlesDashPrefixedQuery) {
    // A query starting with '-' must be passed through -e so rg does not
    // interpret it as a flag. The fake binary accepts "-dash-query" as a
    // valid pattern and returns a match for "needle" in the file.
    create_file("src/sample.cpp", "int needle = 42;\n");
    set_rg_binary_for_testing(create_fake_rg_script());

    const auto result = rg_search(test_workspace, "-dash-query", "src", 10, 40);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];
    EXPECT_EQ(result["returned"].get<size_t>(), 1U);
}

TEST_F(RepoToolsTest, GitStatusRenameWithArrowInOrigPath) {
    // Machine-safe porcelain parsing must preserve the original path even when
    // the source filename itself contains the arrow substring.
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);

    // Simulate a rename where old path itself has " -> " in the name.
    // We parse the line directly through git_status by staging a rename.
    create_file("a -> b.txt", "hello\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add . >/dev/null 2>&1"), 0);
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git -c user.email=t@t.com -c user.name=T commit -m init >/dev/null 2>&1"), 0);
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git mv 'a -> b.txt' c.txt >/dev/null 2>&1"), 0);

    const auto result = git_status(test_workspace, 10);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];

    bool found = false;
    for (const auto& entry : result["entries"]) {
        if (entry["path"].get<std::string>().find("c.txt") != std::string::npos) {
            found = true;
            // orig_path must end in "a -> b.txt", not an intermediate split
            ASSERT_TRUE(entry.contains("orig_path"))
                << "Rename entry must carry orig_path";
            EXPECT_NE(entry["orig_path"].get<std::string>().find("a -> b.txt"), std::string::npos)
                << "orig_path should contain the original filename 'a -> b.txt'";
        }
    }
    EXPECT_TRUE(found) << "Expected a rename entry to c.txt";
}

TEST_F(RepoToolsTest, GitStatusRenameDestHasArrow) {
    // Machine-safe porcelain parsing must preserve the destination path even
    // when the target filename contains the arrow substring.
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);

    create_file("old_plain.txt", "content\n");
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git add . >/dev/null 2>&1"), 0);
    ASSERT_EQ(run_bash("cd '" + test_workspace +
                       "' && git -c user.email=t@t.com -c user.name=T commit -m init >/dev/null 2>&1"), 0);
    // Rename: original path is plain, destination contains ' -> '.
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git mv 'old_plain.txt' 'new -> name.txt' >/dev/null 2>&1"), 0);

    const auto result = git_status(test_workspace, 10);
    ASSERT_TRUE(result["ok"].get<bool>()) << result["error"];

    bool found = false;
    for (const auto& entry : result["entries"]) {
        const std::string path = entry["path"].get<std::string>();
        if (path.find("new -> name.txt") != std::string::npos ||
            path.find("new") != std::string::npos) {
            found = true;
            ASSERT_TRUE(entry.contains("orig_path")) << "Rename entry must carry orig_path";
            EXPECT_EQ(entry["orig_path"].get<std::string>(), "old_plain.txt")
                << "orig_path must be the original filename without the dest arrow";
        }
    }
    EXPECT_TRUE(found) << "Expected a rename entry for the destination with ' -> '";
}

TEST_F(RepoToolsTest, ListFilesBoundedExecutorEnforcesOutputLimit) {
    // When the ToolRegistry executor for list_files_bounded is invoked with a
    // tiny max_tool_output_bytes, the repo tool must stop appending entries and
    // return valid truncated JSON.
    // Creates enough files that the raw JSON would exceed 100 bytes.
    for (int i = 0; i < 10; ++i) {
        create_file("file" + std::to_string(i) + ".cpp", "int x = " + std::to_string(i) + ";\n");
    }

    ToolCall call;
    call.name = "list_files_bounded";
    call.arguments = nlohmann::json::object();

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_tool_output_bytes = 100; // forces truncation

    const std::string raw = get_default_tool_registry().execute(call, config);
    const auto result = nlohmann::json::parse(raw);
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>())
        << "Expected truncated=true when output_limit is tiny; got: " << result.dump();
}

TEST_F(RepoToolsTest, RgSearchExecutorEnforcesOutputLimit) {
    for (int i = 0; i < 8; ++i) {
        create_file("src/file" + std::to_string(i) + ".cpp", "int needle = 42;\n");
    }
    set_rg_binary_for_testing(create_fake_rg_script());

    ToolCall call;
    call.name = "rg_search";
    call.arguments = {
        {"query", "needle"},
        {"directory", "src"}
    };

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_tool_output_bytes = 150;

    const std::string raw = get_default_tool_registry().execute(call, config);
    const auto result = nlohmann::json::parse(raw);
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_TRUE(result.contains("matches"));
    EXPECT_TRUE(result["matches"].is_array());
    EXPECT_EQ(result["returned"].get<size_t>(), result["matches"].size());
}

TEST_F(RepoToolsTest, GitStatusExecutorEnforcesOutputLimit) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);
    for (int i = 0; i < 8; ++i) {
        create_file("file" + std::to_string(i) + ".txt", "content\n");
    }

    ToolCall call;
    call.name = "git_status";
    call.arguments = nlohmann::json::object();

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_tool_output_bytes = 170;

    const std::string raw = get_default_tool_registry().execute(call, config);
    const auto result = nlohmann::json::parse(raw);
    ASSERT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_TRUE(result["truncated"].get<bool>()) << result.dump();
    EXPECT_TRUE(result.contains("branch"));
    EXPECT_TRUE(result.contains("entries"));
    EXPECT_TRUE(result["entries"].is_array());
    EXPECT_EQ(result["returned"].get<size_t>(), result["entries"].size());
}
