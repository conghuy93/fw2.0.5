#include "gemini_client.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <string.h>

static const char* TAG = "GeminiClient";
static const char* GEMINI_API_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-exp:generateContent";

GeminiClient::GeminiClient(const char* api_key) 
    : api_key_(api_key) {
    
    // Default system instruction for Otto
    system_instruction_ = 
        "Bạn là Otto, một robot chó thông minh và dễ thương. "
        "Bạn có thể di chuyển, nhảy múa, thể hiện cảm xúc qua biểu tượng emoji. "
        "Trả lời ngắn gọn, thân thiện bằng tiếng Việt. "
        "Khi cần thực hiện hành động, hãy gọi function tương ứng.";
    
    RegisterOttoFunctions();
}

GeminiClient::~GeminiClient() {
}

void GeminiClient::RegisterOttoFunctions() {
    // Define Otto functions for Gemini Function Calling
    const char* functions_schema = R"(
    {
        "function_declarations": [
            {
                "name": "otto_walk",
                "description": "Make Otto walk forward or backward",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "direction": {
                            "type": "string",
                            "enum": ["forward", "backward"],
                            "description": "Direction to walk"
                        },
                        "steps": {
                            "type": "integer",
                            "description": "Number of steps (1-10)"
                        }
                    },
                    "required": ["direction", "steps"]
                }
            },
            {
                "name": "otto_turn",
                "description": "Make Otto turn left or right",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "direction": {
                            "type": "string",
                            "enum": ["left", "right"],
                            "description": "Direction to turn"
                        }
                    },
                    "required": ["direction"]
                }
            },
            {
                "name": "otto_dance",
                "description": "Make Otto dance",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "duration": {
                            "type": "integer",
                            "description": "Dance duration in seconds (1-10)"
                        }
                    },
                    "required": ["duration"]
                }
            },
            {
                "name": "otto_emotion",
                "description": "Set Otto's emotion/expression",
                "parameters": {
                    "type": "object",
                    "properties": {
                        "emotion": {
                            "type": "string",
                            "enum": ["happy", "sad", "angry", "shocked", "love", "sleepy", "cool", "wink", "neutral"],
                            "description": "Emotion to display"
                        }
                    },
                    "required": ["emotion"]
                }
            },
            {
                "name": "otto_sit",
                "description": "Make Otto sit down",
                "parameters": {
                    "type": "object",
                    "properties": {}
                }
            },
            {
                "name": "otto_bow",
                "description": "Make Otto bow (greet)",
                "parameters": {
                    "type": "object",
                    "properties": {}
                }
            }
        ]
    })";
    
    functions_json_ = functions_schema;
}

void GeminiClient::SetSystemInstruction(const std::string& instruction) {
    system_instruction_ = instruction;
}

void GeminiClient::ClearHistory() {
    conversation_history_.clear();
}

std::string GeminiClient::BuildRequestPayload(const std::string& user_message,
                                             const std::vector<Message>& history) {
    cJSON* root = cJSON_CreateObject();
    
    // System instruction
    cJSON* system_instruction = cJSON_CreateObject();
    cJSON* parts_array = cJSON_CreateArray();
    cJSON* text_part = cJSON_CreateObject();
    cJSON_AddStringToObject(text_part, "text", system_instruction_.c_str());
    cJSON_AddItemToArray(parts_array, text_part);
    cJSON_AddItemToObject(system_instruction, "parts", parts_array);
    cJSON_AddItemToObject(root, "system_instruction", system_instruction);
    
    // Contents (conversation history + new message)
    cJSON* contents = cJSON_CreateArray();
    
    // Add history
    for (const auto& msg : history) {
        cJSON* content = cJSON_CreateObject();
        cJSON_AddStringToObject(content, "role", msg.role.c_str());
        cJSON* msg_parts = cJSON_CreateArray();
        cJSON* msg_text = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_text, "text", msg.content.c_str());
        cJSON_AddItemToArray(msg_parts, msg_text);
        cJSON_AddItemToObject(content, "parts", msg_parts);
        cJSON_AddItemToArray(contents, content);
    }
    
    // Add new user message
    cJSON* user_content = cJSON_CreateObject();
    cJSON_AddStringToObject(user_content, "role", "user");
    cJSON* user_parts = cJSON_CreateArray();
    cJSON* user_text = cJSON_CreateObject();
    cJSON_AddStringToObject(user_text, "text", user_message.c_str());
    cJSON_AddItemToArray(user_parts, user_text);
    cJSON_AddItemToObject(user_content, "parts", user_parts);
    cJSON_AddItemToArray(contents, user_content);
    
    cJSON_AddItemToObject(root, "contents", contents);
    
    // Add function declarations
    cJSON* tools = cJSON_Parse(functions_json_.c_str());
    if (tools) {
        cJSON* tools_array = cJSON_CreateArray();
        cJSON_AddItemToArray(tools_array, tools);
        cJSON_AddItemToObject(root, "tools", tools_array);
    }
    
    char* json_str = cJSON_PrintUnformatted(root);
    std::string payload(json_str);
    free(json_str);
    cJSON_Delete(root);
    
    return payload;
}

GeminiClient::Response GeminiClient::ParseGeminiResponse(const std::string& json_response) {
    Response response;
    response.success = false;
    
    cJSON* root = cJSON_Parse(json_response.c_str());
    if (!root) {
        response.error_message = "Failed to parse JSON response";
        return response;
    }
    
    // Check for error
    cJSON* error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON* message = cJSON_GetObjectItem(error, "message");
        if (message && cJSON_IsString(message)) {
            response.error_message = message->valuestring;
        }
        cJSON_Delete(root);
        return response;
    }
    
    // Get candidates
    cJSON* candidates = cJSON_GetObjectItem(root, "candidates");
    if (!candidates || !cJSON_IsArray(candidates) || cJSON_GetArraySize(candidates) == 0) {
        response.error_message = "No candidates in response";
        cJSON_Delete(root);
        return response;
    }
    
    cJSON* candidate = cJSON_GetArrayItem(candidates, 0);
    cJSON* content = cJSON_GetObjectItem(candidate, "content");
    cJSON* parts = cJSON_GetObjectItem(content, "parts");
    
    if (!parts || !cJSON_IsArray(parts)) {
        response.error_message = "No parts in response";
        cJSON_Delete(root);
        return response;
    }
    
    // Parse parts (text and function calls)
    for (int i = 0; i < cJSON_GetArraySize(parts); i++) {
        cJSON* part = cJSON_GetArrayItem(parts, i);
        
        // Check for text
        cJSON* text = cJSON_GetObjectItem(part, "text");
        if (text && cJSON_IsString(text)) {
            response.text += text->valuestring;
        }
        
        // Check for function call
        cJSON* function_call = cJSON_GetObjectItem(part, "functionCall");
        if (function_call) {
            FunctionCall fc;
            cJSON* name = cJSON_GetObjectItem(function_call, "name");
            cJSON* args = cJSON_GetObjectItem(function_call, "args");
            
            if (name && cJSON_IsString(name)) {
                fc.name = name->valuestring;
                fc.arguments = cJSON_Duplicate(args, true);
                response.function_calls.push_back(fc);
            }
        }
    }
    
    response.success = true;
    cJSON_Delete(root);
    return response;
}

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static std::string* response_buffer = nullptr;
    
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (response_buffer) {
                response_buffer->append((char*)evt->data, evt->data_len);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            break;
        case HTTP_EVENT_DISCONNECTED:
            break;
        default:
            break;
    }
    return ESP_OK;
}

GeminiClient::Response GeminiClient::SendMessage(const std::string& user_message) {
    return SendMessageWithHistory(user_message, conversation_history_);
}

GeminiClient::Response GeminiClient::SendMessageWithHistory(
    const std::string& user_message,
    const std::vector<Message>& history) {
    
    Response response;
    response.success = false;
    
    // Build request payload
    std::string payload = BuildRequestPayload(user_message, history);
    
    ESP_LOGI(TAG, "📤 Sending to Gemini: %s", user_message.c_str());
    ESP_LOGD(TAG, "Payload: %s", payload.c_str());
    
    // Build URL with API key
    std::string url = std::string(GEMINI_API_URL) + "?key=" + api_key_;
    
    // HTTP client configuration
    std::string response_buffer;
    
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_POST;
    config.event_handler = http_event_handler;
    config.user_data = &response_buffer;
    config.timeout_ms = 30000;  // 30 second timeout
    config.crt_bundle_attach = esp_crt_bundle_attach;  // Use ESP certificate bundle for HTTPS
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload.c_str(), payload.length());
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "📥 Gemini response status: %d", status_code);
        
        if (status_code == 200) {
            response = ParseGeminiResponse(response_buffer);
            
            if (response.success) {
                ESP_LOGI(TAG, "✅ Gemini response: %s", response.text.c_str());
                
                // Update conversation history
                conversation_history_.push_back({.role = "user", .content = user_message});
                conversation_history_.push_back({.role = "model", .content = response.text});
                
                // Log function calls
                for (const auto& fc : response.function_calls) {
                    char* args_str = cJSON_PrintUnformatted(fc.arguments);
                    ESP_LOGI(TAG, "🔧 Function call: %s(%s)", fc.name.c_str(), args_str);
                    free(args_str);
                }
            } else {
                ESP_LOGE(TAG, "❌ Gemini error: %s", response.error_message.c_str());
            }
        } else {
            response.error_message = "HTTP error: " + std::to_string(status_code);
            ESP_LOGE(TAG, "❌ %s", response.error_message.c_str());
        }
    } else {
        response.error_message = "HTTP request failed: " + std::string(esp_err_to_name(err));
        ESP_LOGE(TAG, "❌ %s", response.error_message.c_str());
    }
    
    esp_http_client_cleanup(client);
    return response;
}
