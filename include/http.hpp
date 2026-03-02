#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include <functional>

struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::string headers;
};

struct HttpOptions {
    size_t max_body_size = 10 * 1024 * 1024;   // 10 MB default
    size_t max_header_size = 1024 * 1024;      // 1 MB default
    long timeout_ms = 60000;                   // 60 sec total timeout
    long connect_timeout_ms = 10000;           // 10 sec connect timeout
    size_t max_stream_bytes = 100 * 1024 * 1024; // 100 MB default
    long idle_timeout_ms = 30000;              // 30 sec idle timeout
};

struct HttpStreamContext {
    size_t stream_size_bytes = 0;
    const HttpOptions* options = nullptr;
    std::function<bool(const std::string&)> on_chunk;
    bool aborted_by_user = false;
    bool limit_exceeded = false;
};

// Extracted callback functions for testing purposes
size_t http_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
size_t http_header_callback(char* buffer, size_t size, size_t nitems, void* userdata);
size_t http_stream_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

// Core HTTP POST JSON execution
bool http_post_json(const std::string& url, 
                    const std::vector<std::string>& custom_headers, 
                    const std::string& json_body, 
                    const HttpOptions& options,
                    HttpResponse* out_resp, 
                    std::string* err);

// Core HTTP POST JSON Stream execution
bool http_post_json_stream(const std::string& url, 
                           const std::vector<std::string>& custom_headers, 
                           const std::string& json_body, 
                           const HttpOptions& options,
                           std::function<bool(const std::string&)> on_chunk,
                           std::string* err);
