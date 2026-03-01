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
