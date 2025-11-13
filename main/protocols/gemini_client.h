#ifndef GEMINI_CLIENT_H
#define GEMINI_CLIENT_H

#include <string>
#include <vector>
#include <functional>
#include <cJSON.h>

class GeminiClient {
public:
    struct Message {
        std::string role;      // "user" or "model"
        std::string content;
    };

    struct FunctionCall {
        std::string name;
        cJSON* arguments;      // JSON object with function arguments
    };

    struct Response {
        bool success;
        std::string text;
        std::vector<FunctionCall> function_calls;
        std::string error_message;
    };

    GeminiClient(const char* api_key);
    ~GeminiClient();

    // Send text message to Gemini and get response
    Response SendMessage(const std::string& user_message);
    
    // Send message with conversation history
    Response SendMessageWithHistory(const std::string& user_message, 
                                   const std::vector<Message>& history);
    
    // Add Otto-specific function declarations for function calling
    void RegisterOttoFunctions();
    
    // Clear conversation history
    void ClearHistory();
    
    // Set system instruction
    void SetSystemInstruction(const std::string& instruction);

private:
    std::string api_key_;
    std::string system_instruction_;
    std::vector<Message> conversation_history_;
    std::string functions_json_;  // JSON schema for Otto functions
    
    std::string BuildRequestPayload(const std::string& user_message,
                                   const std::vector<Message>& history);
    Response ParseGeminiResponse(const std::string& json_response);
};

#endif // GEMINI_CLIENT_H
