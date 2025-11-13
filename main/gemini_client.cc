#include "gemini_client.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <string.h>

static const char* TAG = "GeminiClient";

// Gemini API endpoint
static const char* GEMINI_API_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent";

GeminiClient::GeminiClient() : initialized_(false) {}

GeminiClient::~GeminiClient() {}

bool GeminiClient::Initialize(const std::string& api_key) {
    if (api_key.empty()) {
        ESP_LOGE(TAG, "❌ API key is empty");
        return false;
    }
    
    api_key_ = api_key;
    initialized_ = true;
    ESP_LOGI(TAG, "✅ Gemini client initialized with API key: %.8s***", api_key_.c_str());
    return true;
}

std::string GeminiClient::BuildRequestBody(const std::string& prompt, 
                                           const std::vector<std::pair<std::string, std::string>>* history) {
    // Build JSON request for Gemini API
    // Format: {"contents":[{"parts":[{"text":"prompt"}]}]}
    
    std::string json = "{\"contents\":[";
    
    // Add history if provided
    if (history && !history->empty()) {
        for (const auto& msg : *history) {
            const std::string& role = msg.first;  // "user" or "model"
            const std::string& text = msg.second;
            
            json += "{\"role\":\"" + role + "\",\"parts\":[{\"text\":\"" + text + "\"}]},";
        }
    }
    
    // Add current prompt
    json += "{\"role\":\"user\",\"parts\":[{\"text\":\"" + prompt + "\"}]}";
    json += "]}";
    
    return json;
}

std::string GeminiClient::ParseResponse(const std::string& json_response) {
    // Parse JSON response from Gemini
    // Format: {"candidates":[{"content":{"parts":[{"text":"response"}]}}]}
    
    cJSON* root = cJSON_Parse(json_response.c_str());
    if (!root) {
        ESP_LOGE(TAG, "❌ Failed to parse JSON response");
        return "";
    }
    
    std::string result;
    
    cJSON* candidates = cJSON_GetObjectItem(root, "candidates");
    if (cJSON_IsArray(candidates) && cJSON_GetArraySize(candidates) > 0) {
        cJSON* first_candidate = cJSON_GetArrayItem(candidates, 0);
        cJSON* content = cJSON_GetObjectItem(first_candidate, "content");
        cJSON* parts = cJSON_GetObjectItem(content, "parts");
        
        if (cJSON_IsArray(parts) && cJSON_GetArraySize(parts) > 0) {
            cJSON* first_part = cJSON_GetArrayItem(parts, 0);
            cJSON* text = cJSON_GetObjectItem(first_part, "text");
            
            if (cJSON_IsString(text)) {
                result = text->valuestring;
            }
        }
    }
    
    cJSON_Delete(root);
    return result;
}

// HTTP event handler - collect response data
esp_err_t GeminiClient::HttpEventHandler(esp_http_client_event_t *evt) {
    static std::string* response_buffer = nullptr;
    
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!response_buffer) {
                response_buffer = new std::string();
            }
            response_buffer->append((char*)evt->data, evt->data_len);
            break;
            
        case HTTP_EVENT_ON_FINISH:
            if (response_buffer && evt->user_data) {
                // Copy response to user_data
                std::string* output = (std::string*)evt->user_data;
                *output = *response_buffer;
                delete response_buffer;
                response_buffer = nullptr;
            }
            break;
            
        case HTTP_EVENT_ERROR:
        case HTTP_EVENT_DISCONNECTED:
            if (response_buffer) {
                delete response_buffer;
                response_buffer = nullptr;
            }
            break;
            
        default:
            break;
    }
    return ESP_OK;
}

void GeminiClient::SendPrompt(const std::string& prompt, ResponseCallback callback) {
    std::vector<std::pair<std::string, std::string>> empty_history;
    SendPromptWithHistory(prompt, empty_history, callback);
}

void GeminiClient::SendPromptWithHistory(const std::string& prompt, 
                                         const std::vector<std::pair<std::string, std::string>>& history,
                                         ResponseCallback callback) {
    if (!initialized_) {
        ESP_LOGE(TAG, "❌ Gemini client not initialized");
        if (callback) callback("", false);
        return;
    }
    
    if (prompt.empty()) {
        ESP_LOGE(TAG, "❌ Prompt is empty");
        if (callback) callback("", false);
        return;
    }
    
    ESP_LOGI(TAG, "🤖 Sending prompt to Gemini: %.50s...", prompt.c_str());
    
    // Build request body
    std::string request_body = BuildRequestBody(prompt, &history);
    
    // Build URL with API key
    std::string url = std::string(GEMINI_API_URL) + "?key=" + api_key_;
    
    // Prepare HTTP client
    std::string response_data;
    
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.event_handler = HttpEventHandler;
    config.user_data = &response_data;
    config.timeout_ms = 30000;  // 30 second timeout
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    // Set POST data
    esp_http_client_set_post_field(client, request_body.c_str(), request_body.length());
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "✅ HTTP POST Status = %d, content_length = %lld",
                status_code, esp_http_client_get_content_length(client));
        
        if (status_code == 200) {
            // Parse response
            std::string result = ParseResponse(response_data);
            
            if (!result.empty()) {
                ESP_LOGI(TAG, "🎉 Gemini response: %.100s...", result.c_str());
                if (callback) callback(result, true);
            } else {
                ESP_LOGE(TAG, "❌ Failed to parse Gemini response");
                if (callback) callback("", false);
            }
        } else {
            ESP_LOGE(TAG, "❌ HTTP error: %d", status_code);
            ESP_LOGE(TAG, "Response: %s", response_data.c_str());
            if (callback) callback("", false);
        }
    } else {
        ESP_LOGE(TAG, "❌ HTTP POST request failed: %s", esp_err_to_name(err));
        if (callback) callback("", false);
    }
    
    esp_http_client_cleanup(client);
}
