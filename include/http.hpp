#pragma once

#include <string>
#include <vector>
#include <cstddef>

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
};

// Extracted callback functions for testing purposes
size_t http_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
size_t http_header_callback(char* buffer, size_t size, size_t nitems, void* userdata);

// Core HTTP POST JSON execution
bool http_post_json(const std::string& url, 
                    const std::vector<std::string>& custom_headers, 
                    const std::string& json_body, 
                    const HttpOptions& options,
                    HttpResponse* out_resp, 
                    std::string* err);
