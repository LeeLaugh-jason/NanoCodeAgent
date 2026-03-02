#include "llm.hpp"
#include "http.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

using json = nlohmann::json;

std::string llm_build_request(const AgentConfig& cfg, const std::string& user_prompt) {
    json j;
    j["model"] = cfg.model;
    
    json system_msg = {
        {"role", "system"},
        {"content", "You are a helpful coding agent."}
    };
    
    json user_msg = {
        {"role", "user"},
        {"content", user_prompt}
    };
    
    j["messages"] = json::array({system_msg, user_msg});
    
    return j.dump(); // Avoid formatting to save space
}

bool llm_parse_response(const std::string& json_resp, std::string* out_text, std::string* err) {
    try {
        json j = json::parse(json_resp);
        
        if (j.contains("error")) {
            if (err) {
                *err = "API Error: ";
                if (j["error"].contains("message") && j["error"]["message"].is_string()) {
                    *err += j["error"]["message"].get<std::string>();
                } else {
                    *err += j["error"].dump();
                }
            }
            return false;
        }
        
        if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
            if (err) *err = "Response missing 'choices' array";
            return false;
        }
        
        auto& choice = j["choices"][0];
        if (!choice.contains("message") || !choice["message"].contains("content")) {
            if (err) *err = "Response missing 'message.content'";
            return false;
        }
        
        if (out_text) {
            *out_text = choice["message"]["content"].get<std::string>();
        }
        return true;
        
    } catch (const json::parse_error& e) {
        if (err) *err = "JSON Parse Error: " + std::string(e.what());
        return false;
    } catch (const json::exception& e) {
        if (err) *err = "JSON Error: " + std::string(e.what());
        return false;
    }
}

bool llm_chat_completion(const AgentConfig& cfg, const std::string& prompt, std::string* out_text, std::string* err) {
    std::string endpoint = cfg.base_url;
    // ensure endpoint ends without trailing slash and proper /v1/chat/completions suffix
    if (endpoint.back() == '/') {
        endpoint.pop_back();
    }
    if (!endpoint.ends_with("/chat/completions")) {
        if (!endpoint.ends_with("/v1")) {
            endpoint += "/v1";
        }
        endpoint += "/chat/completions";
    }

    std::vector<std::string> headers = {
        "Content-Type: application/json"
    };
    if (cfg.api_key.has_value() && !cfg.api_key.value().empty()) {
        headers.push_back("Authorization: Bearer " + cfg.api_key.value());
    }

    std::string req_body = llm_build_request(cfg, prompt);
    
    HttpOptions opts; 
    // Use defaults
    
    HttpResponse resp;
    if (!http_post_json(endpoint, headers, req_body, opts, &resp, err)) {
        return false;
    }

    if (resp.status_code < 200 || resp.status_code >= 300) {
        if (err) {
            *err = "HTTP Error " + std::to_string(resp.status_code) + "\nBody: " + resp.body;
        }
        return false;
    }

    return llm_parse_response(resp.body, out_text, err);
}

#include "sse_parser.hpp"
#include "tool_call_assembler.hpp"
#include "logger.hpp"

bool llm_stream_process_chunk(const std::string& chunk, SseParser& parser, const std::function<bool(const std::string&)>& on_content_delta, ToolCallAssembler* tool_asm, std::string* err) {
    auto events = parser.feed(chunk);
    for (const auto& ev : events) {
        if (ev == "[DONE]") {
            break;
        }

        try {
            nlohmann::json j = nlohmann::json::parse(ev);

            // Top-level error from API
            if (j.contains("error")) {
                if (err) {
                    *err = "API Error: ";
                    if (j["error"].contains("message") && j["error"]["message"].is_string()) {
                        *err += j["error"]["message"].get<std::string>();
                    } else {
                        *err += j["error"].dump();
                    }
                }
                return false;
            }

            if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
                const auto& delta = j["choices"][0].value("delta", nlohmann::json::object());

                // content delta -> text streaming callback
                if (delta.contains("content") && delta["content"].is_string()) {
                    std::string content = delta["content"].get<std::string>();
                    if (!on_content_delta(content)) {
                        return false;
                    }
                }

                // tool_calls delta -> assemble
                if (tool_asm && delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                    for (const auto& tc_delta : delta["tool_calls"]) {
                        if (!tool_asm->ingest_delta(tc_delta, err)) {
                            return false;
                        }
                    }
                }
            }
        } catch (const nlohmann::json::exception& e) {
            if (err) {
                *err = "Stream JSON Error: " + std::string(e.what()) + "\nRaw event: " + ev;
            }
            return false;
        }
    }
    return true;
}

// 主干调用实现
bool llm_chat_completion_stream(const AgentConfig& cfg, const std::string& prompt, std::function<bool(const std::string&)> on_content_delta, std::string* err) {
    if (!cfg.api_key.has_value() || cfg.api_key.value().empty()) {
        if (err) *err = "API key is required for LLM connection.";
        return false;
    }
    
    std::string url = cfg.base_url;
    if (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    if (!url.ends_with("/chat/completions")) {
        if (!url.ends_with("/v1")) {
            url += "/v1";
        }
        url += "/chat/completions";
    }

    std::vector<std::string> headers = {
        "Content-Type: application/json",
        "Authorization: Bearer " + cfg.api_key.value()
    };

    std::string base_req = llm_build_request(cfg, prompt);
    
    // 反序列化并注入 streaming 参数 & 真实系统提示词
    nlohmann::json req_json = nlohmann::json::parse(base_req);
    req_json["stream"] = true;
    std::string final_body = req_json.dump();

    HttpOptions opts;
    SseParser parser;
    ToolCallAssembler tool_asm;
    bool stream_ok = true;

    bool http_success = http_post_json_stream(url, headers, final_body, opts,
        [&](const std::string& chunk) -> bool {
            bool chunk_res = llm_stream_process_chunk(chunk, parser, on_content_delta, &tool_asm, err);
            if (!chunk_res) {
                stream_ok = false;
            }
            return chunk_res;
        }, err);

    if (!stream_ok) {
        return false;
    }

    // Finalize tool calls (if any were accumulated)
    std::vector<ToolCall> tool_calls;
    std::string tc_err;
    if (!tool_asm.finalize(&tool_calls, &tc_err)) {
        LOG_ERROR("ToolCall finalize error: {}", tc_err);
        // Non-fatal for content-only responses; only fail if tool_calls were started
        if (!tool_calls.empty() || !tc_err.empty()) {
            if (err) *err = tc_err;
            return false;
        }
    }
    for (const auto& tc : tool_calls) {
        LOG_DEBUG("tool_call[{}] id={} name={} args={}", tc.index, tc.id, tc.name, tc.raw_arguments);
    }

    return http_success;
}
