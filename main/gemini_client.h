#pragma once

#include <string>
#include <functional>
#include <memory>
#include "esp_http_client.h"
#include "cJSON.h"

class GeminiClient {
public:
    using ResponseCallback = std::function<void(const std::string& response, bool success)>;
    
    GeminiClient();
    ~GeminiClient();
    
    // Initialize with API key
    bool Initialize(const std::string& api_key);
    
    // Send text prompt to Gemini
    void SendPrompt(const std::string& prompt, ResponseCallback callback);
    
    // Send prompt with conversation history
    void SendPromptWithHistory(const std::string& prompt, 
                               const std::vector<std::pair<std::string, std::string>>& history,
                               ResponseCallback callback);
    
    // Check if initialized
    bool IsInitialized() const { return initialized_; }
    
private:
    std::string api_key_;
    bool initialized_;
    
    // HTTP response handler
    static esp_err_t HttpEventHandler(esp_http_client_event_t *evt);
    
    // Build JSON request body
    std::string BuildRequestBody(const std::string& prompt,
                                 const std::vector<std::pair<std::string, std::string>>* history = nullptr);
    
    // Parse Gemini response
    std::string ParseResponse(const std::string& json_response);
};
