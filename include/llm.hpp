#pragma once
#include "config.hpp"
#include <string>
#include <functional>

class SseParser;
class ToolCallAssembler;

// Build a Chat Completion JSON request string
std::string llm_build_request(const AgentConfig& cfg, const std::string& user_prompt);

// Given the API JSON response, parse out the assistant's context
bool llm_parse_response(const std::string& json_resp, std::string* out_text, std::string* err);

// Make the actual network call to the LLM backend
bool llm_chat_completion(const AgentConfig& cfg, const std::string& prompt, std::string* out_text, std::string* err);

// Exposed for Mock Tests: Parse chunk and call the content callback.
// tool_asm is optional (pass nullptr to skip tool_call tracking).
bool llm_stream_process_chunk(const std::string& chunk, SseParser& parser, const std::function<bool(const std::string&)>& on_content_delta, ToolCallAssembler* tool_asm, std::string* err);

// Day 4 SSE Streaming API
bool llm_chat_completion_stream(const AgentConfig& cfg, const std::string& prompt, std::function<bool(const std::string&)> on_content_delta, std::string* err);
