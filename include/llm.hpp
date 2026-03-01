#pragma once
#include "config.hpp"
#include <string>

// Build a Chat Completion JSON request string
std::string llm_build_request(const AgentConfig& cfg, const std::string& user_prompt);

// Given the API JSON response, parse out the assistant's context
bool llm_parse_response(const std::string& json_resp, std::string* out_text, std::string* err);

// Make the actual network call to the LLM backend
bool llm_chat_completion(const AgentConfig& cfg, const std::string& prompt, std::string* out_text, std::string* err);
