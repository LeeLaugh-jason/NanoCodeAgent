#include <gtest/gtest.h>
#include "http.hpp"
#include <string>

class StreamLimitsTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
};

TEST_F(StreamLimitsTest, StreamWriteCallbackNormal) {
    HttpOptions options;
    options.max_stream_bytes = 1000;

    std::string accumulated;
    HttpStreamContext ctx;
    ctx.options = &options;
    ctx.on_chunk = [&](const std::string& chunk) {
        accumulated += chunk;
        return true;
    };

    std::string data1 = "Hello";
    std::string data2 = " World";

    size_t res1 = http_stream_write_callback(data1.data(), 1, data1.size(), &ctx);
    EXPECT_EQ(res1, data1.size());
    EXPECT_EQ(ctx.stream_size_bytes, data1.size());
    EXPECT_FALSE(ctx.limit_exceeded);
    EXPECT_FALSE(ctx.aborted_by_user);

    size_t res2 = http_stream_write_callback(data2.data(), 1, data2.size(), &ctx);
    EXPECT_EQ(res2, data2.size());
    EXPECT_EQ(ctx.stream_size_bytes, data1.size() + data2.size());

    EXPECT_EQ(accumulated, "Hello World");
}

TEST_F(StreamLimitsTest, StreamWriteCallbackExceedsMax) {
    HttpOptions options;
    options.max_stream_bytes = 10;

    HttpStreamContext ctx;
    ctx.options = &options;
    std::string buffer = "abcdefghijklmnopqrs";
    
    // Total size 19 which is > 10
    size_t res = http_stream_write_callback(buffer.data(), 1, buffer.size(), &ctx);
    
    EXPECT_EQ(res, 0); // should abort
    EXPECT_TRUE(ctx.limit_exceeded);
    EXPECT_EQ(ctx.stream_size_bytes, 0); // Doesn't accumulate
}

TEST_F(StreamLimitsTest, StreamWriteCallbackUserAbort) {
    HttpOptions options;
    options.max_stream_bytes = 1000;

    HttpStreamContext ctx;
    ctx.options = &options;
    ctx.on_chunk = [&](const std::string& chunk) {
        return false; // User triggers stream abort
    };

    std::string buffer = "test data";
    size_t res = http_stream_write_callback(buffer.data(), 1, buffer.size(), &ctx);

    EXPECT_EQ(res, 0); // aborted
    EXPECT_TRUE(ctx.aborted_by_user);
    EXPECT_FALSE(ctx.limit_exceeded);
    EXPECT_EQ(ctx.stream_size_bytes, buffer.size()); // Size mapped before user returned false
}
