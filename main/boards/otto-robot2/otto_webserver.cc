#include "otto_webserver.h"
#include "mcp_server.h"
#include "application.h"
#include "otto_emoji_display.h"
#include "board.h"
#include <cJSON.h>
#include <stdio.h>

extern "C" {

static const char *TAG = "OttoWeb";

// Global variables
bool webserver_enabled = false;
static httpd_handle_t server = NULL;
static int s_retry_num = 0;

// Auto pose change variables
static bool auto_pose_enabled = false;
static TimerHandle_t auto_pose_timer = NULL;

// Timer callback for auto pose change
void auto_pose_timer_callback(TimerHandle_t xTimer) {
    if (!auto_pose_enabled) {
        return;
    }
    
    // List of poses with their optimal parameters (action, steps, speed)
    static int pose_index = 0;
    
    struct PoseAction {
        int action;
        int steps;
        int speed;
    };
    
    const PoseAction poses[] = {
        {ACTION_DOG_SIT_DOWN, 1, 500},        // Sit down smoothly
        {ACTION_DOG_JUMP, 1, 200},            // Quick jump
        {ACTION_DOG_WAVE_RIGHT_FOOT, 3, 50},  // Fast wave
        {ACTION_DOG_BOW, 1, 1500},            // Slow bow (show respect)
        {ACTION_DOG_STRETCH, 2, 15},          // Quick stretch
        {ACTION_DOG_SWING, 3, 10},            // Fast swing
        {ACTION_DOG_DANCE, 2, 200}            // Energetic dance
    };
    const int num_poses = sizeof(poses) / sizeof(poses[0]);
    
    // Queue only the pose action (no HOME after)
    const PoseAction& current = poses[pose_index];
    otto_controller_queue_action(current.action, current.steps, current.speed, 0, 0);
    
    ESP_LOGI(TAG, "🤖 Auto pose change: action %d (steps=%d, speed=%d)", 
             current.action, current.steps, current.speed);
    
    // Move to next pose
    pose_index = (pose_index + 1) % num_poses;
}

// WiFi event handler for monitoring system WiFi connection
void otto_system_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "System WiFi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "🌐 Otto Web Controller available at: http://" IPSTR, IP2STR(&event->ip_info.ip));
        
        // Start Otto web server automatically
        if (server == NULL) {
            otto_start_webserver();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "System WiFi disconnected, Otto Web Controller stopped");
    }
}

// Register to listen for system WiFi events
esp_err_t otto_register_wifi_listener(void) {
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_t instance_disconnected;
    
    esp_err_t ret = esp_event_handler_instance_register(IP_EVENT,
                                                       IP_EVENT_STA_GOT_IP,
                                                       &otto_system_wifi_event_handler,
                                                       NULL,
                                                       &instance_got_ip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                             WIFI_EVENT_STA_DISCONNECTED,
                                             &otto_system_wifi_event_handler,
                                             NULL,
                                             &instance_disconnected);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Otto WiFi event listener registered");
    return ESP_OK;
}

// WiFi event handler function (inside extern "C" block but separate definition)  
void otto_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to WiFi AP");
        } else {
            ESP_LOGI(TAG, "Failed to connect to WiFi AP");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "\033[1;33m🌟 WifiStation: Got IP: " IPSTR "\033[0m", IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        
        // Start webserver when WiFi connected
        otto_start_webserver();
    }
}

// Start HTTP server automatically when WiFi is connected
esp_err_t otto_auto_start_webserver_if_wifi_connected(void) {
    // Check if WiFi is already connected (from main system)
    wifi_ap_record_t ap_info;
    esp_err_t wifi_status = esp_wifi_sta_get_ap_info(&ap_info);
    
    if (wifi_status == ESP_OK) {
        ESP_LOGI(TAG, "WiFi already connected to: %s", ap_info.ssid);
        
        // Get current IP
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                ESP_LOGI(TAG, "\033[1;33m🌟 Current IP: " IPSTR "\033[0m", IP2STR(&ip_info.ip));
                ESP_LOGI(TAG, "Otto Web Controller will be available at: http://" IPSTR, IP2STR(&ip_info.ip));
                
                // Start web server immediately
                return otto_start_webserver();
            }
        }
    } else {
        ESP_LOGI(TAG, "WiFi not connected yet, Otto Web Controller will start when WiFi connects");
    }
    
    return ESP_OK;
}

// Original WiFi initialization (for standalone mode if needed)
esp_err_t otto_wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &otto_wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &otto_wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished");

    return ESP_OK;
}

// Send main control page HTML
void send_otto_control_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    
    // Modern responsive HTML with Otto Robot theme
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><head><meta charset='UTF-8'>");
    httpd_resp_sendstr_chunk(req, "<meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=no'>");
    httpd_resp_sendstr_chunk(req, "<title>Dog Master - miniZ</title>");
    
    // CSS Styling - Optimized for Mobile
    httpd_resp_sendstr_chunk(req, "<style>");
    httpd_resp_sendstr_chunk(req, "* { margin: 0; padding: 0; box-sizing: border-box; -webkit-tap-highlight-color: transparent; }");
    httpd_resp_sendstr_chunk(req, "body { font-family: 'Segoe UI', 'Roboto', sans-serif; background: linear-gradient(135deg, #f8f8f8 0%, #ffffff 100%); min-height: 100vh; display: flex; justify-content: center; align-items: flex-start; color: #000000; padding: 8px; padding-top: 10px; }");
    httpd_resp_sendstr_chunk(req, ".container { max-width: 600px; width: 100%; background: #ffffff; border-radius: 15px; padding: 15px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); border: 2px solid #000000; } @media (min-width: 768px) { .container { max-width: 800px; padding: 25px; } }");
    httpd_resp_sendstr_chunk(req, ".header { text-align: center; margin-bottom: 15px; }");
    httpd_resp_sendstr_chunk(req, ".header h1 { font-size: 1.5em; margin-bottom: 5px; color: #000000; font-weight: bold; } @media (min-width: 768px) { .header h1 { font-size: 2.2em; } }");
    httpd_resp_sendstr_chunk(req, ".status { background: #f0f0f0; color: #000; padding: 10px; border-radius: 10px; margin-bottom: 15px; text-align: center; border: 2px solid #000000; font-weight: bold; font-size: 0.9em; }");
    
    // Compact button styling for mobile
    httpd_resp_sendstr_chunk(req, ".control-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(100px, 1fr)); gap: 8px; margin-bottom: 15px; } @media (min-width: 768px) { .control-grid { grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 12px; } }");
    httpd_resp_sendstr_chunk(req, ".btn { background: #ffffff; border: 2px solid #000000; color: #000000; padding: 10px 12px; border-radius: 10px; cursor: pointer; font-size: 13px; font-weight: bold; transition: all 0.15s; box-shadow: 0 2px 5px rgba(0,0,0,0.15); touch-action: manipulation; user-select: none; } @media (min-width: 768px) { .btn { padding: 14px 18px; font-size: 15px; } }");
    httpd_resp_sendstr_chunk(req, ".btn:active { transform: scale(0.95); box-shadow: 0 1px 3px rgba(0,0,0,0.2); background: #f0f0f0; }");
    
    // Compact sections for mobile
    httpd_resp_sendstr_chunk(req, ".movement-section { margin-bottom: 15px; }");
    httpd_resp_sendstr_chunk(req, ".section-title { font-size: 1.1em; margin-bottom: 10px; text-align: center; color: #000000; font-weight: bold; } @media (min-width: 768px) { .section-title { font-size: 1.4em; } }");
    httpd_resp_sendstr_chunk(req, ".direction-pad { display: grid; grid-template-columns: 1fr 1fr 1fr; grid-template-rows: 1fr 1fr 1fr; gap: 8px; max-width: 250px; margin: 0 auto; } @media (min-width: 768px) { .direction-pad { gap: 12px; max-width: 300px; } }");
    httpd_resp_sendstr_chunk(req, ".direction-pad .btn { padding: 15px; font-size: 14px; font-weight: 700; min-height: 50px; } @media (min-width: 768px) { .direction-pad .btn { padding: 20px; font-size: 16px; } }");
    httpd_resp_sendstr_chunk(req, ".btn-forward { grid-column: 2; grid-row: 1; }");
    httpd_resp_sendstr_chunk(req, ".btn-left { grid-column: 1; grid-row: 2; }");
    httpd_resp_sendstr_chunk(req, ".btn-stop { grid-column: 2; grid-row: 2; background: #ffeeee; border-color: #cc0000; color: #cc0000; }");
    httpd_resp_sendstr_chunk(req, ".btn-right { grid-column: 3; grid-row: 2; }");
    httpd_resp_sendstr_chunk(req, ".btn-backward { grid-column: 2; grid-row: 3; }");
    // Auto pose toggle styling
    httpd_resp_sendstr_chunk(req, ".auto-toggle { background: #e8f5e9; border: 2px solid #4caf50; padding: 12px; border-radius: 10px; margin: 15px 0; text-align: center; }");
    httpd_resp_sendstr_chunk(req, ".toggle-btn { background: #ffffff; border: 2px solid #000; padding: 10px 20px; border-radius: 8px; font-weight: bold; font-size: 14px; cursor: pointer; }");
    httpd_resp_sendstr_chunk(req, ".toggle-btn.active { background: #4caf50; color: white; border-color: #2e7d32; }");
    
    // Compact fun actions grid
    httpd_resp_sendstr_chunk(req, ".fun-actions { margin-top: 15px; }");
    httpd_resp_sendstr_chunk(req, ".action-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; } @media (min-width: 768px) { .action-grid { grid-template-columns: repeat(4, 1fr); gap: 10px; } }");
    
    // Compact emoji sections
    httpd_resp_sendstr_chunk(req, ".emoji-section, .emoji-mode-section { margin-top: 15px; }");
    httpd_resp_sendstr_chunk(req, ".emoji-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; }");
    httpd_resp_sendstr_chunk(req, ".mode-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 12px; }");
    httpd_resp_sendstr_chunk(req, ".emoji-btn { background: #fff8e1; border: 2px solid #ff6f00; color: #e65100; padding: 10px; font-size: 13px; }");
    httpd_resp_sendstr_chunk(req, ".emoji-btn:hover { background: #ffecb3; border-color: #e65100; }");
    httpd_resp_sendstr_chunk(req, ".mode-btn { background: #e8f5e8; border: 2px solid #4caf50; color: #2e7d32; padding: 12px 16px; }");
    httpd_resp_sendstr_chunk(req, ".mode-btn:hover { background: #c8e6c9; }");
    httpd_resp_sendstr_chunk(req, ".mode-btn.active { background: #4caf50; color: white; }");
    
    // Compact response area
    httpd_resp_sendstr_chunk(req, ".response { margin-top: 15px; padding: 15px; background: #f8f8f8; border-radius: 12px; min-height: 60px; box-shadow: inset 2px 2px 4px rgba(0,0,0,0.1); border: 2px solid #000; font-family: 'Courier New', monospace; font-size: 13px; }");
    
    // Volume control styling
    httpd_resp_sendstr_chunk(req, ".volume-section { margin-top: 25px; }");
    httpd_resp_sendstr_chunk(req, "input[type='range'] { -webkit-appearance: none; width: 100%; height: 10px; border-radius: 5px; background: linear-gradient(145deg, #e0e0e0, #f0f0f0); outline: none; border: 1px solid #000; }");
    httpd_resp_sendstr_chunk(req, "input[type='range']::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 24px; height: 24px; border-radius: 50%; background: linear-gradient(145deg, #ffffff, #f0f0f0); border: 2px solid #000; cursor: pointer; box-shadow: 2px 2px 4px rgba(0,0,0,0.2); }");
    httpd_resp_sendstr_chunk(req, "input[type='range']::-moz-range-thumb { width: 24px; height: 24px; border-radius: 50%; background: linear-gradient(145deg, #ffffff, #f0f0f0); border: 2px solid #000; cursor: pointer; }");
    
    httpd_resp_sendstr_chunk(req, "</style>");
    
    httpd_resp_sendstr_chunk(req, "</head><body>");
    
    // HTML Content
    httpd_resp_sendstr_chunk(req, "<div class='container'>");
    httpd_resp_sendstr_chunk(req, "<div class='header'>");
    httpd_resp_sendstr_chunk(req, "<h1 style='margin: 0 0 10px 0;'>🐕 Dog Master</h1>");
    httpd_resp_sendstr_chunk(req, "<div style='font-size: 0.9em; color: #666; font-style: italic; margin-bottom: 15px;'>by miniZ</div>");
    httpd_resp_sendstr_chunk(req, "<div class='status' id='status'>🟢 Sẵn Sàng Điều Khiển</div>");
    httpd_resp_sendstr_chunk(req, "</div>");
    
    // Movement Controls
    httpd_resp_sendstr_chunk(req, "<div class='movement-section'>");
    httpd_resp_sendstr_chunk(req, "<div class='section-title'>🎮 Điều Khiển Di Chuyển</div>");
    httpd_resp_sendstr_chunk(req, "<div class='direction-pad'>");
    httpd_resp_sendstr_chunk(req, "<button class='btn btn-forward' onclick='sendAction(\"dog_walk\", 3, 150)'>⬆️ Tiến</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn btn-left' onclick='sendAction(\"dog_turn_left\", 2, 150)'>⬅️ Trái</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn btn-stop' onclick='sendAction(\"dog_stop\", 0, 0)'>🛑 DỪNG</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn btn-right' onclick='sendAction(\"dog_turn_right\", 2, 150)'>➡️ Phải</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn btn-backward' onclick='sendAction(\"dog_walk_back\", 3, 150)'>⬇️ Lùi</button>");
    httpd_resp_sendstr_chunk(req, "</div>");
    
    // Auto Pose Toggle Section
    httpd_resp_sendstr_chunk(req, "<div class='auto-pose-section' style='margin-top: 15px; text-align: center;'>");
    httpd_resp_sendstr_chunk(req, "<button class='btn toggle-btn' id='autoPoseBtn' onclick='toggleAutoPose()'>🔄 Tự Đổi Tư Thế (1 phút)</button>");
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req, "</div>");
    
    // Fun Actions
    httpd_resp_sendstr_chunk(req, "<div class='fun-actions'>");
    httpd_resp_sendstr_chunk(req, "<div class='section-title'>🎪 Hành Động Vui</div>");
    httpd_resp_sendstr_chunk(req, "<div class='action-grid'>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_dance\", 3, 200)'>💃 Nhảy Múa</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_jump\", 1, 200)'>🦘 Nhảy Cao</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_bow\", 1, 2000)'>🙇 Cúi Chào</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_sit_down\", 1, 500)'>🪑 Ngồi</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_lie_down\", 1, 1000)'>🛏️ Nằm</button>");
    // New Defend and Scratch buttons  
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_defend\", 1, 500)'>🛡️ Phòng Thủ</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_scratch\", 5, 50)'>🐾 Gãi Ngứa</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_wave_right_foot\", 5, 50)'>👋 Vẫy Tay</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_wag_tail\", 5, 100)'>🐕 Vẫy Đuôi</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_swing\", 5, 10)'>🎯 Lắc Lư</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_stretch\", 2, 15)'>🧘 Thư Giản</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_home\", 1, 500)'>🏠 Về Nhà</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_dance_4_feet\", 3, 200)'>🕺 Nhảy 4 Chân</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_greet\", 1, 500)'>👋 Chào Hỏi</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_attack\", 1, 500)'>⚔️ Tấn Công</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_celebrate\", 1, 500)'>🎉 Ăn Mừng</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_retreat\", 1, 500)'>🏃 Rút Lui</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn' onclick='sendAction(\"dog_search\", 1, 500)'>🔍 Tìm Kiếm</button>");
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req, "</div>");

    // Volume Control Section
    httpd_resp_sendstr_chunk(req, "<div class='volume-section'>");
    httpd_resp_sendstr_chunk(req, "<div class='section-title'>🔊 Điều Chỉnh Âm Lượng</div>");
    httpd_resp_sendstr_chunk(req, "<div style='background: linear-gradient(145deg, #f8f8f8, #ffffff); border: 2px solid #000000; border-radius: 15px; padding: 20px; margin-bottom: 20px;'>");
    httpd_resp_sendstr_chunk(req, "<div style='display: flex; align-items: center; gap: 15px; flex-wrap: wrap;'>");
    httpd_resp_sendstr_chunk(req, "<span style='font-weight: bold; color: #000; min-width: 80px;'>🔈 Âm lượng:</span>");
    httpd_resp_sendstr_chunk(req, "<input type='range' id='volumeSlider' min='0' max='100' value='50' style='flex: 1; min-width: 200px; height: 8px; background: linear-gradient(145deg, #e0e0e0, #f0f0f0); border-radius: 5px; outline: none; -webkit-appearance: none;'>");
    httpd_resp_sendstr_chunk(req, "<span id='volumeValue' style='font-weight: bold; color: #000; min-width: 50px;'>50%</span>");
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req, "</div>");
    
    // Otto Emoji Controls
    httpd_resp_sendstr_chunk(req, "<div class='emoji-section'>");
    httpd_resp_sendstr_chunk(req, "<div class='section-title'>🤖 Cảm Xúc Robot Otto</div>");
    httpd_resp_sendstr_chunk(req, "<div class='emoji-grid'>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"happy\")'>😊 Vui</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"sad\")'>😢 Buồn</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"angry\")'>😠 Giận</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"surprised\")'>😮 Ngạc Nhiên</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"love\")'>😍 Yêu</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"sleepy\")'>😴 Buồn Ngủ</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"confused\")'>😕 Bối Rối</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"excited\")'>🤩 Phấn Khích</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"neutral\")'>😐 Bình Thường</button>");
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req, "</div>");
    
    // Otto Emoji Restore Section (Most Prominent)
    httpd_resp_sendstr_chunk(req, "<div class='emoji-mode-section'>");
    httpd_resp_sendstr_chunk(req, "<div class='section-title'>🤖 Otto Robot Emotions</div>");
    httpd_resp_sendstr_chunk(req, "<div class='mode-grid'>");
    httpd_resp_sendstr_chunk(req, "<button class='btn mode-btn active' onclick='setEmojiMode(true)' id='otto-mode' style='background: linear-gradient(145deg, #4caf50, #66bb6a); color: white; border-color: #2e7d32; font-size: 18px; font-weight: bold;'>🤖 OTTO GIF MODE (ACTIVE)</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn mode-btn' onclick='setEmojiMode(false)' id='default-mode'>😊 Twemoji Text Mode</button>");
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req, "<div class='emoji-grid' style='margin-top: 15px;'>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"happy\")'>😊 Happy</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"wink\")'>😉 Wink</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"cool\")'>😎 Cool</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"thinking\")'>🤔 Thinking</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"laughing\")'>😂 Laughing</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"crying\")'>😭 Crying</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"crazy\")'>🤪 Crazy</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn emoji-btn' onclick='sendEmotion(\"angry\")'>😠 Angry</button>");
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req, "</div>");
    
    // Touch Sensor Control Section
    httpd_resp_sendstr_chunk(req, "<div class='movement-section'>");
    httpd_resp_sendstr_chunk(req, "<div class='section-title'>🖐️ Cảm Biến Chạm TTP223</div>");
    httpd_resp_sendstr_chunk(req, "<div class='mode-grid'>");
    httpd_resp_sendstr_chunk(req, "<button class='btn mode-btn' onclick='setTouchSensor(true)' id='touch-on' style='background: linear-gradient(145deg, #4caf50, #66bb6a); color: white; border-color: #2e7d32; font-size: 16px; font-weight: bold;'>🖐️ BẬT Cảm Biến Chạm</button>");
    httpd_resp_sendstr_chunk(req, "<button class='btn mode-btn' onclick='setTouchSensor(false)' id='touch-off' style='background: linear-gradient(145deg, #f44336, #e57373); color: white; border-color: #c62828; font-size: 16px; font-weight: bold;'>🚫 TẮT Cảm Biến Chạm</button>");
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req, "<div style='text-align: center; margin-top: 10px; color: #666; font-size: 14px;'>");
    httpd_resp_sendstr_chunk(req, "Khi BẬT: chạm vào cảm biến -> robot nhảy + emoji cười<br>");
    httpd_resp_sendstr_chunk(req, "Khi TẮT: chạm vào cảm biến không có phản ứng");
    httpd_resp_sendstr_chunk(req, "</div>");
    httpd_resp_sendstr_chunk(req, "</div>");

    // Response area
    httpd_resp_sendstr_chunk(req, "<div class='response' id='response'>Ready for commands...</div>");
    httpd_resp_sendstr_chunk(req, "</div>");
    
    // JavaScript - Simple and clean
    httpd_resp_sendstr_chunk(req, "<script>");
    httpd_resp_sendstr_chunk(req, "function sendAction(action, param1, param2) {");
    httpd_resp_sendstr_chunk(req, "  console.log('Action:', action);");
    httpd_resp_sendstr_chunk(req, "  var url = '/action?cmd=' + action + '&p1=' + param1 + '&p2=' + param2;");
    httpd_resp_sendstr_chunk(req, "  fetch(url).then(r => r.text()).then(d => console.log('Success:', d));");
    httpd_resp_sendstr_chunk(req, "}");
    httpd_resp_sendstr_chunk(req, "function sendEmotion(emotion) {");
    httpd_resp_sendstr_chunk(req, "  console.log('Emotion:', emotion);");
    httpd_resp_sendstr_chunk(req, "  fetch('/emotion?emotion=' + emotion).then(r => r.text()).then(d => console.log('Success:', d));");
    httpd_resp_sendstr_chunk(req, "}");
    httpd_resp_sendstr_chunk(req, "function setEmojiMode(useOttoEmoji) {");
    // For compatibility, send 'gif' when Otto mode is selected (server also accepts 'otto')
    httpd_resp_sendstr_chunk(req, "  var mode = useOttoEmoji ? 'gif' : 'default';");
    httpd_resp_sendstr_chunk(req, "  fetch('/emoji_mode?mode=' + mode).then(r => r.text()).then(d => {");
    httpd_resp_sendstr_chunk(req, "    console.log('Mode:', d);");
    // Update button styles
    httpd_resp_sendstr_chunk(req, "    var ottoBtn = document.getElementById('otto-mode');");
    httpd_resp_sendstr_chunk(req, "    var defaultBtn = document.getElementById('default-mode');");
    httpd_resp_sendstr_chunk(req, "    if (useOttoEmoji) {");
    httpd_resp_sendstr_chunk(req, "      ottoBtn.classList.add('active');");
    httpd_resp_sendstr_chunk(req, "      ottoBtn.style.cssText = 'background: linear-gradient(145deg, #4caf50, #66bb6a); color: white; border-color: #2e7d32; font-size: 18px; font-weight: bold;';");
    httpd_resp_sendstr_chunk(req, "      ottoBtn.innerHTML = '🤖 OTTO GIF MODE (ACTIVE)';");
    httpd_resp_sendstr_chunk(req, "      defaultBtn.classList.remove('active');");
    httpd_resp_sendstr_chunk(req, "      defaultBtn.style.cssText = '';");
    httpd_resp_sendstr_chunk(req, "      defaultBtn.innerHTML = '😊 Twemoji Text Mode';");
    httpd_resp_sendstr_chunk(req, "    } else {");
    httpd_resp_sendstr_chunk(req, "      defaultBtn.classList.add('active');");
    httpd_resp_sendstr_chunk(req, "      defaultBtn.style.cssText = 'background: linear-gradient(145deg, #4caf50, #66bb6a); color: white; border-color: #2e7d32; font-size: 18px; font-weight: bold;';");
    httpd_resp_sendstr_chunk(req, "      defaultBtn.innerHTML = '😊 TWEMOJI TEXT MODE (ACTIVE)';");
    httpd_resp_sendstr_chunk(req, "      ottoBtn.classList.remove('active');");
    httpd_resp_sendstr_chunk(req, "      ottoBtn.style.cssText = '';");
    httpd_resp_sendstr_chunk(req, "      ottoBtn.innerHTML = '🤖 Otto GIF Mode';");
    httpd_resp_sendstr_chunk(req, "    }");
    httpd_resp_sendstr_chunk(req, "  });");
    httpd_resp_sendstr_chunk(req, "}");
    httpd_resp_sendstr_chunk(req, "function setTouchSensor(enabled) {");
    httpd_resp_sendstr_chunk(req, "  console.log('Touch sensor:', enabled);");
    httpd_resp_sendstr_chunk(req, "  fetch('/touch_sensor?enabled=' + enabled).then(r => r.text()).then(d => {");
    httpd_resp_sendstr_chunk(req, "    console.log('Touch sensor result:', d);");
    httpd_resp_sendstr_chunk(req, "    document.getElementById('response').innerHTML = d;");
    httpd_resp_sendstr_chunk(req, "  });");
    httpd_resp_sendstr_chunk(req, "}");
    
    // Volume control JavaScript
    httpd_resp_sendstr_chunk(req, "function setVolume(volume) {");
    httpd_resp_sendstr_chunk(req, "  console.log('Setting volume:', volume);");
    httpd_resp_sendstr_chunk(req, "  fetch('/volume?level=' + volume).then(r => r.text()).then(d => {");
    httpd_resp_sendstr_chunk(req, "    console.log('Volume result:', d);");
    httpd_resp_sendstr_chunk(req, "    document.getElementById('response').innerHTML = 'Âm lượng: ' + volume + '%';");
    httpd_resp_sendstr_chunk(req, "  });");
    httpd_resp_sendstr_chunk(req, "}");
    
    // Auto pose toggle JavaScript
    httpd_resp_sendstr_chunk(req, "var autoPoseEnabled = false;");
    httpd_resp_sendstr_chunk(req, "function toggleAutoPose() {");
    httpd_resp_sendstr_chunk(req, "  autoPoseEnabled = !autoPoseEnabled;");
    httpd_resp_sendstr_chunk(req, "  var btn = document.getElementById('autoPoseBtn');");
    httpd_resp_sendstr_chunk(req, "  if (autoPoseEnabled) {");
    httpd_resp_sendstr_chunk(req, "    btn.classList.add('active');");
    httpd_resp_sendstr_chunk(req, "    btn.style.background = '#4caf50';");
    httpd_resp_sendstr_chunk(req, "    btn.style.color = 'white';");
    httpd_resp_sendstr_chunk(req, "    document.getElementById('response').innerHTML = '✅ Tự động đổi tư thế BẬT';");
    httpd_resp_sendstr_chunk(req, "  } else {");
    httpd_resp_sendstr_chunk(req, "    btn.classList.remove('active');");
    httpd_resp_sendstr_chunk(req, "    btn.style.background = '';");
    httpd_resp_sendstr_chunk(req, "    btn.style.color = '';");
    httpd_resp_sendstr_chunk(req, "    document.getElementById('response').innerHTML = '⛔ Tự động đổi tư thế TẮT';");
    httpd_resp_sendstr_chunk(req, "  }");
    httpd_resp_sendstr_chunk(req, "  fetch('/auto_pose?enabled=' + (autoPoseEnabled ? 'true' : 'false')).then(r => r.text()).then(d => console.log('Auto pose:', d));");
    httpd_resp_sendstr_chunk(req, "}");
    
    // Initialize volume slider
    httpd_resp_sendstr_chunk(req, "window.onload = function() {");
    httpd_resp_sendstr_chunk(req, "  var slider = document.getElementById('volumeSlider');");
    httpd_resp_sendstr_chunk(req, "  var output = document.getElementById('volumeValue');");
    httpd_resp_sendstr_chunk(req, "  slider.oninput = function() {");
    httpd_resp_sendstr_chunk(req, "    output.innerHTML = this.value + '%';");
    httpd_resp_sendstr_chunk(req, "    setVolume(this.value);");
    httpd_resp_sendstr_chunk(req, "  }");
    httpd_resp_sendstr_chunk(req, "};");
    httpd_resp_sendstr_chunk(req, "</script>");
    httpd_resp_sendstr_chunk(req, "</body></html>");
    
    httpd_resp_sendstr_chunk(req, NULL); // End of chunks
}

// Root page handler
esp_err_t otto_root_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Root page requested");
    send_otto_control_page(req);
    return ESP_OK;
}

} // extern "C"

// C++ function to execute Otto actions (with real controller integration)
void otto_execute_web_action(const char* action, int param1, int param2) {
    ESP_LOGI(TAG, "🎮 Web Control: %s (param1:%d, param2:%d)", action, param1, param2);
    
    // Map web actions to controller actions (order matters - check specific first)
    esp_err_t ret = ESP_OK;
    
    if (strstr(action, "walk_back")) {
        ret = otto_controller_queue_action(ACTION_DOG_WALK_BACK, param1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Walking backward: %d steps, speed %d", param1, param2);
    } else if (strstr(action, "walk_forward") || strstr(action, "walk")) {
        ret = otto_controller_queue_action(ACTION_DOG_WALK, param1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Walking forward: %d steps, speed %d", param1, param2);
    } else if (strstr(action, "turn_left") || (strstr(action, "turn") && param1 < 0)) {
        ret = otto_controller_queue_action(ACTION_DOG_TURN_LEFT, abs(param1), param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Turning left: %d steps, speed %d", abs(param1), param2);
    } else if (strstr(action, "turn_right") || (strstr(action, "turn") && param1 > 0)) {
        ret = otto_controller_queue_action(ACTION_DOG_TURN_RIGHT, param1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Turning right: %d steps, speed %d", param1, param2);
    } else if (strstr(action, "turn")) {
        // Default turn right if no direction specified
        ret = otto_controller_queue_action(ACTION_DOG_TURN_RIGHT, param1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Turning right (default): %d steps, speed %d", param1, param2);
    } else if (strstr(action, "sit")) {
        ret = otto_controller_queue_action(ACTION_DOG_SIT_DOWN, 1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Sitting down with delay %d", param2);
    } else if (strstr(action, "lie")) {
        ret = otto_controller_queue_action(ACTION_DOG_LIE_DOWN, 1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Lying down with delay %d", param2);
    } else if (strstr(action, "bow")) {
        ret = otto_controller_queue_action(ACTION_DOG_BOW, 1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Bowing with delay %d", param2);
    } else if (strstr(action, "jump")) {
        // Angry emoji when jumping
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("angry");
        ret = otto_controller_queue_action(ACTION_DOG_JUMP, 1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Jumping with delay %d", param2);
    } else if (strstr(action, "dance")) {
        // Happy emoji when dancing
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("happy");
        ret = otto_controller_queue_action(ACTION_DOG_DANCE, param1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Dancing: %d cycles, speed %d", param1, param2);
    } else if (strstr(action, "wave")) {
        ret = otto_controller_queue_action(ACTION_DOG_WAVE_RIGHT_FOOT, param1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Waving: %d times, speed %d", param1, param2);
    } else if (strstr(action, "swing")) {
        // Happy emoji when swinging
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("happy");
        ret = otto_controller_queue_action(ACTION_DOG_SWING, param1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Swinging: %d cycles, speed %d", param1, param2);
    } else if (strstr(action, "stretch")) {
        // Sleepy emoji during stretch
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("sleepy");
        ret = otto_controller_queue_action(ACTION_DOG_STRETCH, param1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Stretching: %d cycles, speed %d", param1, param2);
    } else if (strstr(action, "scratch")) {
        ret = otto_controller_queue_action(ACTION_DOG_SCRATCH, param1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Scratching: %d times, speed %d", param1, param2);
    } else if (strstr(action, "wag_tail")) {
        // Happy emoji when wagging tail
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("happy");
        ret = otto_controller_queue_action(ACTION_DOG_WAG_TAIL, param1, param2, 0, 0);
        ESP_LOGI(TAG, "🐕 Wagging tail: %d wags, speed %d", param1, param2);
    } else if (strstr(action, "defend")) {
        // Shocked emoji when defending
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("shocked");
        // Defend sequence: walk back EXACTLY 1 journey -> sit (3000) -> lie (1500) -> delay(3000) -> home
        otto_controller_queue_action(ACTION_DOG_WALK_BACK, 1, 100, 0, 0);  // Changed: speed=100 for full 1 journey
        otto_controller_queue_action(ACTION_DOG_SIT_DOWN, 1, 3000, 0, 0);
        otto_controller_queue_action(ACTION_DOG_LIE_DOWN, 1, 1500, 0, 0);
        otto_controller_queue_action(ACTION_DELAY, 0, 3000, 0, 0);
        otto_controller_queue_action(ACTION_HOME, 1, 500, 0, 0);
        ret = ESP_OK;
        ESP_LOGI(TAG, "🛡️ Defend sequence queued: walk_back(1,100) -> sit(3000) -> lie_down(1500) -> delay(3000) -> home");
    } else if (strstr(action, "home")) {
        ret = otto_controller_queue_action(ACTION_HOME, 1, 500, 0, 0);
        ESP_LOGI(TAG, "🏠 Going to home position");
    } else if (strstr(action, "dance_4_feet")) {
        // Happy emoji when dancing with 4 feet
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("happy");
        ret = otto_controller_queue_action(ACTION_DOG_DANCE_4_FEET, param1, param2, 0, 0);
        ESP_LOGI(TAG, "🕺 Dancing with 4 feet: %d cycles, speed %d", param1, param2);
    } else if (strstr(action, "greet")) {
        // Happy emoji when greeting
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("happy");
        // Greet sequence: home → wave → bow
        otto_controller_queue_action(ACTION_HOME, 1, 500, 0, 0);
        otto_controller_queue_action(ACTION_DOG_WAVE_RIGHT_FOOT, 3, 150, 0, 0);
        otto_controller_queue_action(ACTION_DOG_BOW, 2, 150, 0, 0);
        ret = ESP_OK;
        ESP_LOGI(TAG, "👋 Greet sequence queued: home → wave → bow");
    } else if (strstr(action, "attack")) {
        // Angry emoji when attacking
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("angry");
        // Attack sequence: forward → jump → bow
        otto_controller_queue_action(ACTION_DOG_WALK, 2, 100, 0, 0);
        otto_controller_queue_action(ACTION_DOG_JUMP, 2, 200, 0, 0);
        otto_controller_queue_action(ACTION_DOG_BOW, 1, 150, 0, 0);
        ret = ESP_OK;
        ESP_LOGI(TAG, "⚔️ Attack sequence queued: forward → jump → bow");
    } else if (strstr(action, "celebrate")) {
        // Happy emoji when celebrating
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("happy");
        // Celebrate sequence: dance → wave → swing
        otto_controller_queue_action(ACTION_DOG_DANCE, 2, 200, 0, 0);
        otto_controller_queue_action(ACTION_DOG_WAVE_RIGHT_FOOT, 5, 100, 0, 0);
        otto_controller_queue_action(ACTION_DOG_SWING, 3, 10, 0, 0);  // Changed from 150 to 10 for faster swing
        ret = ESP_OK;
        ESP_LOGI(TAG, "🎉 Celebrate sequence queued: dance → wave → swing");
    } else if (strstr(action, "retreat")) {
        // Scared emoji when retreating
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("scared");
        // Retreat sequence: back → turn → run back
        otto_controller_queue_action(ACTION_DOG_WALK_BACK, 3, 100, 0, 0);
        otto_controller_queue_action(ACTION_DOG_TURN_LEFT, 2, 150, 0, 0);
        otto_controller_queue_action(ACTION_DOG_WALK_BACK, 2, 80, 0, 0);
        ret = ESP_OK;
        ESP_LOGI(TAG, "🏃 Retreat sequence queued: back → turn → run");
    } else if (strstr(action, "search")) {
        // Scared emoji when searching (cautious)
        if (auto display = Board::GetInstance().GetDisplay()) display->SetEmotion("scared");
        // Search sequence: look left → look right → walk forward
        otto_controller_queue_action(ACTION_DOG_TURN_LEFT, 2, 150, 0, 0);
        otto_controller_queue_action(ACTION_DOG_TURN_RIGHT, 4, 150, 0, 0);
        otto_controller_queue_action(ACTION_DOG_TURN_LEFT, 2, 150, 0, 0);
        otto_controller_queue_action(ACTION_DOG_WALK, 3, 120, 0, 0);
        ret = ESP_OK;
        ESP_LOGI(TAG, "🔍 Search sequence queued: look around → walk forward");
    } else if (strstr(action, "stop")) {
        // Stop action - clear queue and go to home position
        ret = otto_controller_stop_all();  // This will clear all queued actions
        ESP_LOGI(TAG, "🛑 STOP - all actions cancelled, robot at home");
    } else {
        ESP_LOGW(TAG, "❌ Unknown action: %s", action);
        return;
    }
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Action queued successfully");
    } else {
        ESP_LOGE(TAG, "❌ Failed to queue action: %s", esp_err_to_name(ret));
    }
}

extern "C" {

// Action handler
esp_err_t otto_action_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "🎯 ACTION HANDLER CALLED!"); // Debug logging
    
    // Add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    
    char query[200] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        ESP_LOGI(TAG, "📥 Query string: %s", query);
        
        char cmd[50] = {0};
        char p1_str[20] = {0};
        char p2_str[20] = {0};
        
        httpd_query_key_value(query, "cmd", cmd, sizeof(cmd));
        httpd_query_key_value(query, "p1", p1_str, sizeof(p1_str));
        httpd_query_key_value(query, "p2", p2_str, sizeof(p2_str));
        
        int param1 = atoi(p1_str);
        int param2 = atoi(p2_str);
        
        ESP_LOGI(TAG, "Action: %s, P1: %d, P2: %d", cmd, param1, param2);
        
        // Execute action
        otto_execute_web_action(cmd, param1, param2);
        
        // Send response
        httpd_resp_set_type(req, "text/plain");
        char response[200];
        snprintf(response, sizeof(response), "✅ Otto executed: %s (steps: %d, speed: %d)", cmd, param1, param2);
        httpd_resp_sendstr(req, response);
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "❌ Missing action parameters");
    }
    
    return ESP_OK;
}

// Status handler
esp_err_t otto_status_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain");
    
    // Simple status - can be expanded with actual Otto status
    httpd_resp_sendstr(req, "ready");
    
    return ESP_OK;
}

// Emotion handler
esp_err_t otto_emotion_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "😊 EMOTION HANDLER CALLED!"); // Debug logging
    
    // Add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    char query[100] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        ESP_LOGI(TAG, "📥 Emotion query: %s", query);
        
        char emotion[50] = {0};
        httpd_query_key_value(query, "emotion", emotion, sizeof(emotion));
        
        ESP_LOGI(TAG, "Setting emotion: %s", emotion);
        
        // Send emotion to display system with fallback
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            // Try Otto display first for GIF support
            auto otto_display = dynamic_cast<OttoEmojiDisplay*>(display);
            if (otto_display) {
                otto_display->SetEmotion(emotion);
            } else {
                // Fallback to regular display for text emoji
                display->SetEmotion(emotion);
            }
            
            httpd_resp_set_type(req, "text/plain");
            char response[100];
            snprintf(response, sizeof(response), "✅ Emotion set to: %s", emotion);
            httpd_resp_sendstr(req, response);
        } else {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "❌ Display system not available");
        }
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "❌ Missing emotion parameter");
    }
    
    return ESP_OK;
}

// Emoji mode handler
esp_err_t otto_emoji_mode_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "🎭 EMOJI MODE HANDLER CALLED!"); // Debug logging
    
    // Add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    char query[100] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        ESP_LOGI(TAG, "📥 Emoji mode query: %s", query);
        
        char mode[20] = {0};
        httpd_query_key_value(query, "mode", mode, sizeof(mode));
        
    // Accept both 'gif' and 'otto' as Otto GIF mode keywords
    bool use_otto_emoji = (strcmp(mode, "gif") == 0) || (strcmp(mode, "otto") == 0);
        ESP_LOGI(TAG, "Setting emoji mode: %s (use_otto: %d)", mode, use_otto_emoji);
        
        // Send mode change to display system
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            if (use_otto_emoji) {
                // Try to cast to OttoEmojiDisplay for GIF mode
                auto otto_display = dynamic_cast<OttoEmojiDisplay*>(display);
                if (otto_display) {
                    otto_display->SetEmojiMode(true);
                    // Ensure the GIF is visible immediately by setting neutral emotion
                    otto_display->SetEmotion("neutral");
                    httpd_resp_set_type(req, "text/plain");
                    httpd_resp_sendstr(req, "✅ Emoji mode set to: Otto GIF");
                } else {
                    httpd_resp_set_status(req, "500 Internal Server Error");
                    httpd_resp_sendstr(req, "❌ Otto GIF display not available");
                }
            } else {
                // Use text emoji mode
                auto otto_display = dynamic_cast<OttoEmojiDisplay*>(display);
                if (otto_display) {
                    otto_display->SetEmojiMode(false); // Set to text emoji mode
                    otto_display->SetEmotion("neutral"); // Set neutral text emoji
                } else {
                    display->SetEmotion("neutral"); // Fallback for non-Otto displays
                }
                httpd_resp_set_type(req, "text/plain");
                httpd_resp_sendstr(req, "✅ Emoji mode set to: Default Text");
            }
        } else {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "❌ Display system not available");
        }
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "❌ Missing mode parameter");
    }
    
    return ESP_OK;
}

// Touch sensor control handler
esp_err_t otto_touch_sensor_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "🖐️ TOUCH SENSOR HANDLER CALLED!"); // Debug logging
    
    // Add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    char query[100] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        ESP_LOGI(TAG, "📥 Touch sensor query: %s", query);
        
        char enabled_str[10] = {0};
        httpd_query_key_value(query, "enabled", enabled_str, sizeof(enabled_str));
        
        bool enabled = (strcmp(enabled_str, "true") == 0);
        ESP_LOGI(TAG, "Setting touch sensor: %s", enabled ? "ENABLED" : "DISABLED");
        
        // Set touch sensor state
        otto_set_touch_sensor_enabled(enabled);
        
        httpd_resp_set_type(req, "text/plain");
        char response[100];
        snprintf(response, sizeof(response), "✅ Cảm biến chạm đã %s", enabled ? "BẬT" : "TẮT");
        httpd_resp_sendstr(req, response);
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "❌ Missing enabled parameter");
    }
    
    return ESP_OK;
}

// Volume control handler
esp_err_t otto_volume_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "🔊 VOLUME HANDLER CALLED!");
    
    // Add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    char query[100] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        ESP_LOGI(TAG, "📥 Volume query: %s", query);
        
        char level_str[10] = {0};
        httpd_query_key_value(query, "level", level_str, sizeof(level_str));
        
        int volume_level = atoi(level_str);
        if (volume_level < 0) volume_level = 0;
        if (volume_level > 100) volume_level = 100;
        
        ESP_LOGI(TAG, "🔊 Setting volume to: %d%%", volume_level);
        
        // Get AudioCodec instance and set volume
        Board& board = Board::GetInstance();
        if (board.GetAudioCodec()) {
            board.GetAudioCodec()->SetOutputVolume(volume_level);
            ESP_LOGI(TAG, "✅ Audio volume set successfully to %d%%", volume_level);
        } else {
            ESP_LOGW(TAG, "⚠️ AudioCodec not available");
        }
        
        // Also show volume change on display
        if (board.GetDisplay()) {
            char volume_msg[64];
            snprintf(volume_msg, sizeof(volume_msg), "Âm lượng: %d%%", volume_level);
            board.GetDisplay()->SetChatMessage("system", volume_msg);
        }
        
        httpd_resp_set_type(req, "text/plain");
        char response[100];
        snprintf(response, sizeof(response), "✅ Âm lượng đã đặt: %d%%", volume_level);
        httpd_resp_sendstr(req, response);
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "❌ Missing level parameter");
    }
    
    return ESP_OK;
}

// Auto pose handler
esp_err_t otto_auto_pose_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "🔄 AUTO POSE HANDLER CALLED!");
    
    // Add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    char query[100] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        ESP_LOGI(TAG, "📥 Auto pose query: %s", query);
        
        char enabled_str[10] = {0};
        httpd_query_key_value(query, "enabled", enabled_str, sizeof(enabled_str));
        
        bool enabled = (strcmp(enabled_str, "true") == 0);
        ESP_LOGI(TAG, "Setting auto pose: %s", enabled ? "ENABLED" : "DISABLED");
        
        auto_pose_enabled = enabled;
        
        if (enabled) {
            // Create timer if not exists
            if (auto_pose_timer == NULL) {
                auto_pose_timer = xTimerCreate(
                    "AutoPoseTimer",
                    pdMS_TO_TICKS(60000),  // 60 seconds = 1 minute
                    pdTRUE,                 // Auto-reload
                    NULL,
                    auto_pose_timer_callback
                );
            }
            
            // Start the timer
            if (auto_pose_timer != NULL) {
                xTimerStart(auto_pose_timer, 0);
                ESP_LOGI(TAG, "✅ Auto pose timer started");
            }
        } else {
            // Stop the timer
            if (auto_pose_timer != NULL) {
                xTimerStop(auto_pose_timer, 0);
                ESP_LOGI(TAG, "⏹️ Auto pose timer stopped");
            }
        }
        
        httpd_resp_set_type(req, "text/plain");
        char response[100];
        snprintf(response, sizeof(response), "✅ Tự động đổi tư thế đã %s", enabled ? "BẬT" : "TẮT");
        httpd_resp_sendstr(req, response);
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "❌ Missing enabled parameter");
    }
    
    return ESP_OK;
}

// Start HTTP server
esp_err_t otto_start_webserver(void) {
    if (server != NULL) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 10;
    config.max_resp_headers = 8;
    config.stack_size = 8192;
    
    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register URI handlers
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = otto_root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);
        
        httpd_uri_t action_uri = {
            .uri = "/action",
            .method = HTTP_GET,
            .handler = otto_action_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &action_uri);
        
        httpd_uri_t status_uri = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = otto_status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status_uri);
        
        // New emotion control handlers
        httpd_uri_t emotion_uri = {
            .uri = "/emotion",
            .method = HTTP_GET,
            .handler = otto_emotion_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &emotion_uri);
        
        httpd_uri_t emoji_mode_uri = {
            .uri = "/emoji_mode",
            .method = HTTP_GET,
            .handler = otto_emoji_mode_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &emoji_mode_uri);
        
        httpd_uri_t touch_sensor_uri = {
            .uri = "/touch_sensor",
            .method = HTTP_GET,
            .handler = otto_touch_sensor_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &touch_sensor_uri);
        
        // Volume control handler registration
        httpd_uri_t volume_uri = {
            .uri = "/volume",
            .method = HTTP_GET,
            .handler = otto_volume_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &volume_uri);
        
        // Auto pose handler registration
        httpd_uri_t auto_pose_uri = {
            .uri = "/auto_pose",
            .method = HTTP_GET,
            .handler = otto_auto_pose_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &auto_pose_uri);
        
        ESP_LOGI(TAG, "HTTP server started successfully");
        webserver_enabled = true;
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
}



} // extern "C"