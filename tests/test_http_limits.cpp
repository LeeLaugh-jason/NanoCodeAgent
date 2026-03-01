#include <gtest/gtest.h>
#include "http.hpp"

// We must redeclare HttpContext shape to test callbacks unless we extract it to a testable header.
// It's just a struct in http.cpp. Let's redefine here for direct callback test. 
struct MockHttpContext {
    std::string* buffer;
    size_t limit;
    size_t current_size;
    bool* limit_exceeded;
};

TEST(HttpLimitsTest, WriteCallbackEnforcesLimit) {
    std::string buf;
    bool exceeded = false;
    MockHttpContext ctx{&buf, 10, 0, &exceeded};
    
    char data1[] = "12345";
    size_t res1 = http_write_callback(data1, 1, 5, &ctx);
    EXPECT_EQ(res1, 5);
    EXPECT_FALSE(exceeded);
    EXPECT_EQ(ctx.current_size, 5);
    
    char data2[] = "678901"; // length 6, total 11 > 10
    size_t res2 = http_write_callback(data2, 1, 6, &ctx);
    
    EXPECT_EQ(res2, 0); // Should abort
    EXPECT_TRUE(exceeded);
    EXPECT_EQ(ctx.current_size, 5); // size didn't increase
    EXPECT_EQ(buf, "12345"); // content not appended
}

TEST(HttpLimitsTest, HeaderCallbackEnforcesLimit) {
    std::string buf;
    bool exceeded = false;
    MockHttpContext ctx{&buf, 5, 0, &exceeded};
    
    char data1[] = "abc";
    size_t res1 = http_header_callback(data1, 1, 3, &ctx);
    EXPECT_EQ(res1, 3);
    EXPECT_FALSE(exceeded);
    
    char data2[] = "def"; // total 6 > 5
    size_t res2 = http_header_callback(data2, 1, 3, &ctx);
    
    EXPECT_EQ(res2, 0); // abort
    EXPECT_TRUE(exceeded);
}
