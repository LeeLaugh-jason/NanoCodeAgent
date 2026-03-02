#include "http.hpp"
#include <curl/curl.h>

struct HttpContext {
    std::string* buffer;
    size_t limit;
    size_t current_size;
    bool* limit_exceeded;
};

size_t http_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t realsize = size * nmemb;
    HttpContext* ctx = static_cast<HttpContext*>(userdata);
    
    if (ctx->current_size + realsize > ctx->limit) {
        if (ctx->limit_exceeded) *(ctx->limit_exceeded) = true;
        return 0; // Abort transfer
    }
    
    ctx->buffer->append(ptr, realsize);
    ctx->current_size += realsize;
    
    return realsize;
}

size_t http_header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t realsize = size * nitems;
    HttpContext* ctx = static_cast<HttpContext*>(userdata);
    
    if (ctx->current_size + realsize > ctx->limit) {
        if (ctx->limit_exceeded) *(ctx->limit_exceeded) = true;
        return 0; // Abort transfer
    }
    
    ctx->buffer->append(buffer, realsize);
    ctx->current_size += realsize;
    
    return realsize;
}

bool http_post_json(const std::string& url, 
                    const std::vector<std::string>& custom_headers, 
                    const std::string& json_body, 
                    const HttpOptions& options,
                    HttpResponse* out_resp, 
                    std::string* err) 
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (err) *err = "Failed to initialize CURL";
        return false;
    }

    if (out_resp) {
        out_resp->body.clear();
        out_resp->headers.clear();
        out_resp->status_code = 0;
    }

    bool body_limit_exceeded = false;
    bool header_limit_exceeded = false;

    HttpContext body_ctx{out_resp ? &out_resp->body : nullptr, options.max_body_size, 0, &body_limit_exceeded};
    HttpContext header_ctx{out_resp ? &out_resp->headers : nullptr, options.max_header_size, 0, &header_limit_exceeded};

    curl_slist* chunk = nullptr;
    for (const auto& h : custom_headers) {
        chunk = curl_slist_append(chunk, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(json_body.length()));

    if (out_resp) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body_ctx);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, http_header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_ctx);
    }

    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, options.timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, options.connect_timeout_ms);
    // Be robust with modern environments
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    
    if (out_resp) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        out_resp->status_code = static_cast<int>(http_code);
    }

    curl_slist_free_all(chunk);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        if (err) {
            if (body_limit_exceeded) {
                *err = "HTTP Request aborted: Response body exceeded maximum allowed size (" + std::to_string(options.max_body_size) + " bytes)";
            } else if (header_limit_exceeded) {
                *err = "HTTP Request aborted: Response header exceeded maximum allowed size";
            } else {
                *err = "CURL errored: " + std::string(curl_easy_strerror(res));
            }
        }
        return false;
    }

    return true;
}

size_t http_stream_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t realsize = size * nmemb;
    HttpStreamContext* ctx = static_cast<HttpStreamContext*>(userdata);

    if (ctx->stream_size_bytes + realsize > ctx->options->max_stream_bytes) {
        ctx->limit_exceeded = true;
        return 0; // Abort transfer
    }

    ctx->stream_size_bytes += realsize;

    std::string chunk(ptr, realsize);
    if (ctx->on_chunk && !ctx->on_chunk(chunk)) {
        ctx->aborted_by_user = true;
        return 0; // Abort transfer
    }

    return realsize;
}

bool http_post_json_stream(const std::string& url, 
                           const std::vector<std::string>& custom_headers, 
                           const std::string& json_body, 
                           const HttpOptions& options,
                           std::function<bool(const std::string&)> on_chunk,
                           std::string* err)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (err) *err = "Failed to initialize CURL";
        return false;
    }

    HttpStreamContext ctx;
    ctx.options = &options;
    ctx.on_chunk = on_chunk;
    ctx.stream_size_bytes = 0;
    ctx.aborted_by_user = false;
    ctx.limit_exceeded = false;

    curl_slist* chunk_headers = nullptr;
    for (const auto& h : custom_headers) {
        chunk_headers = curl_slist_append(chunk_headers, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk_headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(json_body.length()));

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_stream_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, options.timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, options.connect_timeout_ms);
    
    // low speed limits
    if (options.idle_timeout_ms > 0) {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, options.idle_timeout_ms / 1000);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    }

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    
    // We can also extract status code if needed, but stream is real-time.
    // For simplicity, we just rely on the res.
    
    curl_slist_free_all(chunk_headers);
    curl_easy_cleanup(curl);

    if (ctx.limit_exceeded) {
        if (err) *err = "Stream exceeded max_stream_bytes limit";
        return false;
    }

    if (ctx.aborted_by_user) {
        if (err) *err = "Stream aborted by user";
        return false;
    }

    if (res != CURLE_OK) {
        if (err) *err = std::string("CURL streaming error: ") + curl_easy_strerror(res);
        return false;
    }

    return true;
}
