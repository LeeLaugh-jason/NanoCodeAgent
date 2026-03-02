#include <gtest/gtest.h>
#include "llm.hpp"
#include "sse_parser.hpp"

TEST(LlmStreamTest, NormalStreamingMultipleChunks) {
    SseParser parser;
    std::string err;
    std::string accumulated;

    auto callback = [&accumulated](const std::string& content) -> bool {
        accumulated += content;
        return true;
    };

    // First Payload Chunk
    std::string chunk1 = "data: {\"choices\":[{\"delta\":{\"content\":\"he\"}}]}\n\n";
    EXPECT_TRUE(llm_stream_process_chunk(chunk1, parser, callback, &err));
    EXPECT_EQ(accumulated, "he");

    // Second Payload Chunk
    std::string chunk2 = "data: {\"choices\":[{\"delta\":{\"content\":\"llo\"}}]}\n\n";
    EXPECT_TRUE(llm_stream_process_chunk(chunk2, parser, callback, &err));
    EXPECT_EQ(accumulated, "hello");

    // Finished flag Chunk
    std::string chunk3 = "data: [DONE]\n\n";
    EXPECT_TRUE(llm_stream_process_chunk(chunk3, parser, callback, &err));
}

TEST(LlmStreamTest, ParseErrorPayload) {
    SseParser parser;
    std::string err;
    auto callback = [](const std::string& content) -> bool { return true; };

    // API Error Payload
    std::string chunk = "data: {\"error\":{\"message\":\"API error limit\"}}\n\n";
    EXPECT_FALSE(llm_stream_process_chunk(chunk, parser, callback, &err));
    EXPECT_EQ(err, "API Error: API error limit");
}

TEST(LlmStreamTest, ParseJsonThrowError) {
    SseParser parser;
    std::string err;
    auto callback = [](const std::string& content) -> bool { return true; };

    // Bad Formatted JSON
    std::string chunk = "data: {invalid json}\n\n";
    EXPECT_FALSE(llm_stream_process_chunk(chunk, parser, callback, &err));
    EXPECT_NE(err.find("Stream JSON Error"), std::string::npos);
}

TEST(LlmStreamTest, IgnoreControlChunks) {
    SseParser parser;
    std::string err;
    std::string accumulated;
    auto callback = [&accumulated](const std::string& content) -> bool {
        accumulated += content;
        return true;
    };

    // Control chunk: Only tells role without content
    std::string chunk = "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\"}}]}\n\n";
    EXPECT_TRUE(llm_stream_process_chunk(chunk, parser, callback, &err));
    EXPECT_EQ(accumulated, ""); // Ignored successfully
}

TEST(LlmStreamTest, AbortFromUserCallback) {
    SseParser parser;
    std::string err;

    auto callback = [](const std::string& content) -> bool {
        return false; // Signal abort immediately
    };

    std::string chunk = "data: {\"choices\":[{\"delta\":{\"content\":\"hi\"}}]}\n\n";
    EXPECT_FALSE(llm_stream_process_chunk(chunk, parser, callback, &err));
}
