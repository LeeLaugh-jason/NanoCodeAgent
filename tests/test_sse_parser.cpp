#include <gtest/gtest.h>
#include "sse_parser.hpp"

TEST(SseParserTest, BasicParse) {
    SseParser parser;
    auto res = parser.feed("data: hello\n\n");
    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(res[0], "hello");
}

TEST(SseParserTest, ChunkedBoundary) {
    SseParser parser;
    auto res = parser.feed("data: hello\n");
    EXPECT_TRUE(res.empty());
    
    res = parser.feed("\n");
    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(res[0], "hello");
}

TEST(SseParserTest, MultilineData) {
    SseParser parser;
    auto res = parser.feed("data: line1\ndata: line2\n\n");
    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(res[0], "line1\nline2");
}

TEST(SseParserTest, IgnoreComments) {
    SseParser parser;
    auto res = parser.feed(": comment\ndata: value\n\n");
    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(res[0], "value");
}

TEST(SseParserTest, DoneSignal) {
    SseParser parser;
    auto res = parser.feed("data: value1\n\ndata: [DONE]\n\n");
    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(res[0], "value1");
    EXPECT_TRUE(parser.is_done());
}

TEST(SseParserTest, RNRNBoundary) {
    SseParser parser;
    auto res = parser.feed("data: test\r\n\r\n");
    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(res[0], "test");
}

TEST(SseParserTest, ExceedLimit) {
    SseParser parser;
    std::string large_chunk(300 * 1024, 'a');
    EXPECT_THROW(parser.feed(large_chunk), std::runtime_error);
}
