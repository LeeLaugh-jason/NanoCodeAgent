#include <gtest/gtest.h>
#include "write_file.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>

namespace fs = std::filesystem;

class WriteFileTest : public ::testing::Test {
protected:
    std::string test_workspace;

    void SetUp() override {
        test_workspace = fs::absolute(fs::path("test_ws_write")).string();
        fs::create_directories(test_workspace);
    }

    void TearDown() override {
        fs::remove_all(test_workspace);
    }

    // Helper to extract content
    std::string read_file_direct(const std::string& path) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return "";
        return std::string(std::istreambuf_iterator<char>(ifs), {});
    }
};

TEST_F(WriteFileTest, BasicWriteOk) {
    std::string content = "Hello Secure World!";
    auto res = write_file_safe(test_workspace, "hello.txt", content);
    
    EXPECT_TRUE(res.ok) << "Expected write to succeed: " << res.err;
    EXPECT_EQ(res.bytes_written, content.size());
    EXPECT_EQ(read_file_direct(res.abs_path), content);
    
    // Sub-directories auto creation (mkdirat)
    auto res_sub = write_file_safe(test_workspace, "sub_dir/inner/data.json", "{ \"a\": 1 }");
    EXPECT_TRUE(res_sub.ok);
    EXPECT_TRUE(fs::exists(res_sub.abs_path));
}

TEST_F(WriteFileTest, RejectsDotDotAndAbsolute) {
    // Escaping dot dots
    auto res1 = write_file_safe(test_workspace, "../evil.txt", "payload");
    EXPECT_FALSE(res1.ok);
    EXPECT_NE(res1.err.find("resolution failed"), std::string::npos); // Follows Day 2 rules

    // Rejects pure absolute
    auto res2 = write_file_safe(test_workspace, "/etc/passwd", "payload");
    EXPECT_FALSE(res2.ok);
}

TEST_F(WriteFileTest, RejectsSymlinkEscape) {
    // Create nested payload testing directory
    fs::path inner_path = fs::path(test_workspace) / "inner";
    fs::create_directories(inner_path);
    
    // Create actual symlink component locally to simulate TOCTOU/Traversal attacks
    std::string mock_symlink_dir = (fs::path(test_workspace) / "linked_dir").string();
    symlink(inner_path.c_str(), mock_symlink_dir.c_str());

    // Try to write through symlink path component: 'linked_dir/stolen.txt'
    auto res = write_file_safe(test_workspace, "linked_dir/stolen.txt", "bypass");
    
    // Expect failure since O_NOFOLLOW blocks navigating symbolic link directory structures
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.err.find("securely traverse directory"), std::string::npos);
    EXPECT_FALSE(fs::exists(inner_path / "stolen.txt"));

    // Attack 2: Target file itself is a symlink pointing outward
    std::string external_target = (fs::path(test_workspace) / "secret.txt").string();
    std::string sym_target = (inner_path / "evil_link").string();
    
    // Set dummy external data
    write_file_safe(test_workspace, "secret.txt", "SECRET_DATA");
    
    // Link local sym to secret
    symlink(external_target.c_str(), sym_target.c_str());
    
    // Rewrite sym target directly
    auto res2 = write_file_safe(test_workspace, "inner/evil_link", "OVERWRITE_ATTACK");
    EXPECT_FALSE(res2.ok);
    // Prevents overwriting target file and specifically fails due to O_NOFOLLOW catching symlink target.
    EXPECT_EQ(read_file_direct(external_target), "SECRET_DATA");
}

TEST_F(WriteFileTest, RejectsOversize) {
    size_t limit = 4 * 1024 * 1024; // 4MB
    
    // Construct payload larger than max
    std::string huge_payload(limit + 512, 'A');
    
    auto res = write_file_safe(test_workspace, "huge.txt", huge_payload);
    
    EXPECT_FALSE(res.ok);
    EXPECT_NE(res.err.find("exceeded"), std::string::npos);
    
    // Ensure file doesn't fall onto disk silently truncated
    EXPECT_FALSE(fs::exists(fs::path(test_workspace) / "huge.txt"));
}
