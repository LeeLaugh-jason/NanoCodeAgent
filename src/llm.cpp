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

// 新增 Chunk 流式处理解析器
bool llm_stream_process_chunk(const std::string& chunk, SseParser& parser, const std::function<bool(const std::string&)>& on_content_delta, std::string* err) {
    auto events = parser.feed(chunk);
    for (const auto& ev : events) {
        if (ev == "[DONE]") {
            break; // 结束事件循环，随后继续返回 true
        }
        
        try {
            nlohmann::json j = nlohmann::json::parse(ev);
            if (j.contains("error")) {
                if (err) {
                    *err = "API Error: ";
                    if (j["error"].contains("message") && j["error"]["message"].is_string()) {
                        *err += j["error"]["message"].get<std::string>();
                    } else {
                        *err += j["error"].dump();
                    }
                }
                return false; // 中止流
            }
            
            if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
                auto& choice = j["choices"][0];
                if (choice.contains("delta") && choice["delta"].contains("content") && choice["delta"]["content"].is_string()) {
                    std::string content = choice["delta"]["content"].get<std::string>();
                    if (!on_content_delta(content)) {
                        return false; // 因用户操作中止流
                    }
                }
            }
            // 弹性设计：包含 choices 但没有 content 或 delta 视作普通的 control chunk 继续循环
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
    bool stream_ok = true;

    bool http_success = http_post_json_stream(url, headers, final_body, opts, 
        [&](const std::string& chunk) -> bool {
            bool chunk_res = llm_stream_process_chunk(chunk, parser, on_content_delta, err);
            if (!chunk_res) {
                stream_ok = false;
            }
            return chunk_res;
        }, err);

    if (!stream_ok) {
        return false;
    }

    return http_success;
}
