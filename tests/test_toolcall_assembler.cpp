#include <gtest/gtest.h>
#include "tool_call_assembler.hpp"
#include "sse_parser.hpp"
#include "llm.hpp"

// ──────────────────────────────────────────────────────────────────────────────
// Helper: build a single-element delta JSON for a tool_call fragment
// ──────────────────────────────────────────────────────────────────────────────
static nlohmann::json make_tc_delta(int index, const std::string& id,
                                     const std::string& name,
                                     const std::string& args_frag) {
    nlohmann::json tc;
    tc["index"] = index;
    if (!id.empty()) tc["id"] = id;
    if (!name.empty()) tc["function"]["name"] = name;
    if (!args_frag.empty()) tc["function"]["arguments"] = args_frag;
    return tc;
}

// ──────────────────────────────────────────────────────────────────────────────
// 1) Single tool_call – arguments arrive in 3 fragments
// ──────────────────────────────────────────────────────────────────────────────
TEST(ToolCallAssemblerTest, SingleToolCallFragmented) {
    ToolCallAssembler asm_;
    std::string err;

    // Fragment 1: establish id + name + first arg chunk
    ASSERT_TRUE(asm_.ingest_delta(make_tc_delta(0, "call_abc", "read_file", "{\"path\":"), &err)) << err;
    // Fragment 2: middle
    ASSERT_TRUE(asm_.ingest_delta(make_tc_delta(0, "", "", "\"/home/user"), &err)) << err;
    // Fragment 3: closing
    ASSERT_TRUE(asm_.ingest_delta(make_tc_delta(0, "", "", "/readme.md\"}"), &err)) << err;

    std::vector<ToolCall> out;
    ASSERT_TRUE(asm_.finalize(&out, &err)) << err;
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].id,   "call_abc");
    EXPECT_EQ(out[0].name, "read_file");
    EXPECT_EQ(out[0].arguments["path"].get<std::string>(), "/home/user/readme.md");
}

// ──────────────────────────────────────────────────────────────────────────────
// 2) Two tool_calls with interleaved delta fragments (index 0 and 1)
// ──────────────────────────────────────────────────────────────────────────────
TEST(ToolCallAssemblerTest, DualIndexInterleaved) {
    ToolCallAssembler asm_;
    std::string err;

    // Index 0 – name arrives first
    ASSERT_TRUE(asm_.ingest_delta(make_tc_delta(0, "c0", "write_file", "{\"file\":"), &err)) << err;
    // Index 1 – interleaved
    ASSERT_TRUE(asm_.ingest_delta(make_tc_delta(1, "c1", "bash",       "{\"cmd\":"), &err)) << err;
    // Index 0 continues
    ASSERT_TRUE(asm_.ingest_delta(make_tc_delta(0, "",   "",            "\"out.txt\"}"), &err)) << err;
    // Index 1 continues
    ASSERT_TRUE(asm_.ingest_delta(make_tc_delta(1, "",   "",            "\"ls\"}"), &err)) << err;

    std::vector<ToolCall> out;
    ASSERT_TRUE(asm_.finalize(&out, &err)) << err;
    ASSERT_EQ(out.size(), 2u);

    // finalize returns sorted by index (map order)
    EXPECT_EQ(out[0].index, 0);
    EXPECT_EQ(out[0].name,  "write_file");
    EXPECT_EQ(out[0].arguments["file"].get<std::string>(), "out.txt");

    EXPECT_EQ(out[1].index, 1);
    EXPECT_EQ(out[1].name,  "bash");
    EXPECT_EQ(out[1].arguments["cmd"].get<std::string>(), "ls");
}

// ──────────────────────────────────────────────────────────────────────────────
// 3) Arguments buffer exceeds per-call limit → abort
// ──────────────────────────────────────────────────────────────────────────────
TEST(ToolCallAssemblerTest, ArgsBytesExceeded) {
    // Set a tiny limit of 16 bytes per call for testing
    ToolCallAssembler asm_(16);
    std::string err;

    ASSERT_TRUE(asm_.ingest_delta(make_tc_delta(0, "cx", "fn", "{\"x\":"), &err)) << err;
    // This fragment pushes past the 16-byte limit
    const std::string big_frag = "\"" + std::string(20, 'a') + "\"}";
    bool ok = asm_.ingest_delta(make_tc_delta(0, "", "", big_frag), &err);
    EXPECT_FALSE(ok);
    EXPECT_NE(err.find("exceeded"), std::string::npos) << "err was: " << err;
}

// ──────────────────────────────────────────────────────────────────────────────
// 4) Truncated / malformed JSON – finalize returns false but doesn't crash
// ──────────────────────────────────────────────────────────────────────────────
TEST(ToolCallAssemblerTest, MalformedArguments) {
    ToolCallAssembler asm_;
    std::string err;

    // Truncated JSON: missing closing brace
    ASSERT_TRUE(asm_.ingest_delta(make_tc_delta(0, "cx", "fn", "{\"key\": \"val\""), &err)) << err;

    std::vector<ToolCall> out;
    bool ok = asm_.finalize(&out, &err);
    EXPECT_FALSE(ok);
    // Error must mention the index
    EXPECT_NE(err.find("0"), std::string::npos) << "err was: " << err;
    // Error must contain a tail snippet (not just a generic message)
    EXPECT_NE(err.find("Tail"), std::string::npos) << "err was: " << err;
}

// ──────────────────────────────────────────────────────────────────────────────
// 5) Integration: llm_stream_process_chunk routes tool_calls into assembler
// ──────────────────────────────────────────────────────────────────────────────
TEST(ToolCallAssemblerTest, ChunkRoutingViaLlmStreamProcess) {
    SseParser parser;
    ToolCallAssembler asm_;
    std::string err;
    auto no_content = [](const std::string&) { return true; };

    // Simulate two SSE events that together form a complete tool_call
    const std::string chunk1 =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"cX\",\"function\":{\"name\":\"greet\",\"arguments\":\"{\\\"name\\\":\"}}]}}]}\n\n";
    const std::string chunk2 =
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"\\\"world\\\"}\"}}]}}]}\n\n";

    ASSERT_TRUE(llm_stream_process_chunk(chunk1, parser, no_content, &asm_, &err)) << err;
    ASSERT_TRUE(llm_stream_process_chunk(chunk2, parser, no_content, &asm_, &err)) << err;

    std::vector<ToolCall> out;
    ASSERT_TRUE(asm_.finalize(&out, &err)) << err;
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].name, "greet");
    EXPECT_EQ(out[0].arguments["name"].get<std::string>(), "world");
}
