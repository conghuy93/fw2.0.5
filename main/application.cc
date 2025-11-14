#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
#include "ota.h"
// For Otto GIF/text emoji mode toggling
#include "boards/otto-robot/otto_emoji_display.h"
// For Otto movement actions from voice
#include "boards/otto-robot/otto_webserver.h"

#include <cstring>
#include <esp_log.h>
#include <esp_netif.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>
#include <nvs_flash.h>

#define TAG "Application"


static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveMode(false);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveMode(true);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota.HasNewVersion()) {
            if (UpgradeFirmware(ota)) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation (don't break, just fall through)
        }

        // No new version, mark the current version as valid
        ota.MarkCurrentVersionValid();
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // Lock emotion to force "winking" and prevent MCP from overriding
    emotion_locked_ = true;
    ESP_LOGI(TAG, "🔒 Emotion LOCKED for QR code display (winking)");

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    // Use "winking" emotion instead of "link" to show playful face when displaying QR code
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "winking", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
    
    // Unlock emotion after QR code is displayed (15 seconds should be enough)
    xTaskCreate([](void* arg) {
        vTaskDelay(pdMS_TO_TICKS(15000));  // Keep winking for 15 seconds
        Application* app = static_cast<Application*>(arg);
        app->Schedule([app]() {
            app->emotion_locked_ = false;
            ESP_LOGI("Application", "🔓 Emotion UNLOCKED after QR code display");
        });
        vTaskDelete(NULL);
    }, "qr_emotion_unlock", 2048, this, 1, NULL);
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Start the main event loop task with priority 3
    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version or get the MQTT broker address
    Ota ota;
    CheckNewVersion(ota);

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (device_state_ == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            // Don't clear chat message on audio channel close to preserve conversation history
            // display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (device_state_ == kDeviceStateSpeaking) {
                        // Clear chat message when TTS stops
                        auto display = Board::GetInstance().GetDisplay();
                        display->SetChatMessage("", "");
                        
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    
                    std::string assistant_msg = text->valuestring;
                    
                    // Display message immediately (Gemini check happens at TTS stop)
                    Schedule([this, display, message = assistant_msg]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                std::string message = text->valuestring;
                
                // Skip only truly empty messages
                if (message.empty()) {
                    ESP_LOGI(TAG, "Ignoring empty STT message from server");
                    return;
                }
                
                // Skip old-style placeholder wake words (for backward compatibility)
                if (message == "web_ui" || message == "text_input" || message == "web_input" || message == "text input") {
                    ESP_LOGI(TAG, "Ignoring legacy placeholder STT message from server: %s", message.c_str());
                    return;
                }
                
                // Skip echo of wake word from web UI (server echoes back the wake word we sent)
                if (!last_web_wake_word_.empty() && message == last_web_wake_word_) {
                    ESP_LOGI(TAG, "Skipping echo of web wake word from server: %s", message.c_str());
                    last_web_wake_word_.clear();  // Clear after skipping once
                    return;
                }
                
                ESP_LOGI(TAG, ">> %s", message.c_str());
                
                // Voice command: special action sequence
                // Phrase example (Vietnamese): "súng nè", "bắn", "bang bang", "bùm"
                // Behavior: walk back 1 step (speed 15), sit down, then lie down slowly; show shocked emoji
                // Also accept unaccented form: "sung ne", "ban", "bang bang", "bum"
                std::string phrase = message;
                auto to_lower = [](std::string s) {
                    for (auto &ch : s) ch = (char)tolower((unsigned char)ch);
                    return s;
                };
                auto contains = [](const std::string &hay, const char* needle) {
                    return hay.find(needle) != std::string::npos;
                };
                std::string lower = to_lower(phrase);
                
                ESP_LOGI(TAG, "🎤 STT voice command check: original='%s' lower='%s'", phrase.c_str(), lower.c_str());

                bool shoot_seq =
                    // Shooting keywords (accented and unaccented)
                    contains(lower, "súng nè") ||
                    contains(lower, "sung ne") ||
                    contains(lower, "bắn") ||
                    contains(lower, "ban") ||
                    contains(lower, "bang bang") ||
                    contains(lower, "bùm") ||
                    contains(lower, "bum");
                    
                ESP_LOGI(TAG, "🎯 Shoot sequence match: %s", shoot_seq ? "YES ✅" : "NO ❌");
                
                // Check for instant action keywords
                bool walk_forward = contains(lower, "đi tới") || contains(lower, "di toi") || 
                                   contains(lower, "tiến lên") || contains(lower, "tien len");
                bool walk_back = contains(lower, "lùi lại") || contains(lower, "lui lai") || 
                                contains(lower, "đi lùi") || contains(lower, "di lui");
                bool turn_left = contains(lower, "quẹo trái") || contains(lower, "queo trai") || 
                                contains(lower, "rẽ trái") || contains(lower, "re trai");
                bool turn_right = contains(lower, "quẹo phải") || contains(lower, "queo phai") || 
                                 contains(lower, "rẽ phải") || contains(lower, "re phai");
                bool sit_down = contains(lower, "ngồi xuống") || contains(lower, "ngoi xuong") || 
                               contains(lower, "ngồi") || contains(lower, "ngoi");
                bool dance = contains(lower, "nhảy") || contains(lower, "nhay") || 
                            contains(lower, "múa") || contains(lower, "mua");
                bool bow = contains(lower, "cúi chào") || contains(lower, "cui chao") || 
                          contains(lower, "chào") || contains(lower, "chao");
                bool show_ip = contains(lower, "192168") || contains(lower, "một chín hai") || 
                              contains(lower, "mot chin hai") || contains(lower, "ip address");
                bool open_panel = contains(lower, "mở bảng điều khiển") || contains(lower, "mo bang dieu khien") ||
                                 contains(lower, "bảng điều khiển") || contains(lower, "bang dieu khien") ||
                                 contains(lower, "mở trang điều khiển") || contains(lower, "mo trang dieu khien") ||
                                 contains(lower, "mở web") || contains(lower, "mo web");
                bool show_qr = contains(lower, "mở qr") || contains(lower, "mo qr") || 
                              contains(lower, "mở mã qr") || contains(lower, "mo ma qr") ||
                              contains(lower, "hiển thị qr") || contains(lower, "hien thi qr") ||
                              contains(lower, "mở mạng qr") || contains(lower, "mo mang qr");

                // New voice pose triggers
                bool toilet_pose = contains(lower, "đi vệ sinh") || contains(lower, "di ve sinh") ||
                                   contains(lower, "đi toilet") || contains(lower, "di toilet");
                bool pushup_pose = contains(lower, "chống đẩy") || contains(lower, "chong day") ||
                                   contains(lower, "tập thể dục") || contains(lower, "tap the duc") ||
                                   contains(lower, "hít đất") || contains(lower, "hit dat");
                
                if (shoot_seq) {
                    ESP_LOGI(TAG, "🔫 EXECUTING shoot/defend sequence NOW! (No text display, only emoji)");
                    // Lock emotion IMMEDIATELY before Schedule
                    emotion_locked_ = true;
                    ESP_LOGI(TAG, "🔒 Emotion LOCKED for keyword sequence");
                    
                    Schedule([this]() {
                        auto disp = Board::GetInstance().GetDisplay();
                        // Set shocked emotion/icon immediately (NO text message)
                        disp->SetEmotion("shocked");

                        // Queue movement sequence
                        // 1) Walk back 1 step, speed delay 15 (smaller = faster per implementation)
                        otto_controller_queue_action(ACTION_DOG_WALK_BACK, 1, 15, 0, 0);
                        // 2) Sit down (3 seconds for complete motion)
                        otto_controller_queue_action(ACTION_DOG_SIT_DOWN, 1, 3000, 0, 0);
                        // 3) Lie down slowly
                        otto_controller_queue_action(ACTION_DOG_LIE_DOWN, 1, 1500, 0, 0);
                        // 4) After sequence, wait 3s then return home
                        otto_controller_queue_action(ACTION_DELAY, 0, 3000, 0, 0);
                        otto_controller_queue_action(ACTION_HOME, 1, 500, 0, 0);
                        
                        // Unlock emotion after sequence completes (total ~8s)
                        // Schedule unlock after action queue finishes
                        xTaskCreate([](void* arg) {
                            vTaskDelay(pdMS_TO_TICKS(9000)); // Wait for sequence to complete
                            Application* app = static_cast<Application*>(arg);
                            app->Schedule([app]() {
                                app->emotion_locked_ = false;
                                ESP_LOGI("Application", "🔓 Emotion UNLOCKED after keyword sequence");
                            });
                            vTaskDelete(NULL);
                        }, "emotion_unlock", 2048, this, 1, NULL);
                    });
                    ESP_LOGI(TAG, "✅ Shoot/defend sequence scheduled, returning now (no chat message)");
                    return; // handled - skip SetChatMessage
                }
                
                if (show_qr) {
                    ESP_LOGI(TAG, "📱 QR keyword detected: showing winking emoji for 15s (no movement, no IP, no activation code)");
                    // Lock emotion immediately so other actions cannot override during display period
                    emotion_locked_ = true;
                    ESP_LOGI(TAG, "🔒 Emotion LOCKED for QR winking display");
                    Schedule([this]() {
                        if (auto disp = Board::GetInstance().GetDisplay()) {
                            // Show only winking emoji, no chat/status text
                            disp->SetEmotion("winking");
                        }
                        // Unlock after 15 seconds
                        xTaskCreate([](void* arg) {
                            vTaskDelay(pdMS_TO_TICKS(15000)); // Changed from 30s to 15s
                            Application* app = static_cast<Application*>(arg);
                            app->Schedule([app]() {
                                app->emotion_locked_ = false;
                                ESP_LOGI("Application", "🔓 Emotion UNLOCKED after QR winking display (15s)");
                            });
                            vTaskDelete(NULL);
                        }, "qr_wink_unlock", 2048, this, 1, NULL);
                    });
                    return; // handled
                }

                if (pushup_pose) {
                    ESP_LOGI(TAG, "💪 Voice trigger: pushup exercise");
                    Schedule([this]() {
                        if (auto disp = Board::GetInstance().GetDisplay()) disp->SetEmotion("happy");
                        // Default 3 pushups speed 150
                        otto_controller_queue_action(ACTION_DOG_PUSHUP, 3, 150, 0, 0);
                    });
                    return; // handled
                }

                if (toilet_pose) {
                    ESP_LOGI(TAG, "🚽 Voice trigger: toilet squat pose");
                    Schedule([this]() {
                        if (auto disp = Board::GetInstance().GetDisplay()) disp->SetEmotion("embarrassed");
                        // Hold 3000 ms, speed base 150
                        otto_controller_queue_action(ACTION_DOG_TOILET, 3000, 150, 0, 0);
                    });
                    return; // handled
                }
                
                // Instant action commands - execute immediately without LLM
                if (walk_forward) {
                    ESP_LOGI(TAG, "⚡ INSTANT ACTION: Walk Forward");
                    Schedule([this]() {
                        auto disp = Board::GetInstance().GetDisplay();
                        disp->SetEmotion("happy");
                        otto_controller_queue_action(ACTION_DOG_WALK, 3, 150, 0, 0);
                    });
                    return;
                }
                if (walk_back) {
                    ESP_LOGI(TAG, "⚡ INSTANT ACTION: Walk Back");
                    Schedule([this]() {
                        auto disp = Board::GetInstance().GetDisplay();
                        disp->SetEmotion("neutral");
                        otto_controller_queue_action(ACTION_DOG_WALK_BACK, 3, 150, 0, 0);
                    });
                    return;
                }
                if (turn_left) {
                    ESP_LOGI(TAG, "⚡ INSTANT ACTION: Turn Left");
                    Schedule([this]() {
                        auto disp = Board::GetInstance().GetDisplay();
                        disp->SetEmotion("happy");
                        otto_controller_queue_action(ACTION_DOG_TURN_LEFT, 3, 150, 0, 0);
                    });
                    return;
                }
                if (turn_right) {
                    ESP_LOGI(TAG, "⚡ INSTANT ACTION: Turn Right");
                    Schedule([this]() {
                        auto disp = Board::GetInstance().GetDisplay();
                        disp->SetEmotion("happy");
                        otto_controller_queue_action(ACTION_DOG_TURN_RIGHT, 3, 150, 0, 0);
                    });
                    return;
                }
                if (sit_down) {
                    ESP_LOGI(TAG, "⚡ INSTANT ACTION: Sit Down");
                    Schedule([this]() {
                        auto disp = Board::GetInstance().GetDisplay();
                        disp->SetEmotion("sleepy");
                        otto_controller_queue_action(ACTION_DOG_SIT_DOWN, 1, 1000, 0, 0);
                    });
                    return;
                }
                if (dance) {
                    ESP_LOGI(TAG, "⚡ INSTANT ACTION: Dance 4 Feet");
                    Schedule([this]() {
                        auto disp = Board::GetInstance().GetDisplay();
                        disp->SetEmotion("happy");
                        otto_controller_queue_action(ACTION_DOG_DANCE_4_FEET, 3, 200, 0, 0);
                    });
                    return;
                }
                if (bow) {
                    ESP_LOGI(TAG, "⚡ INSTANT ACTION: Bow");
                    Schedule([this]() {
                        auto disp = Board::GetInstance().GetDisplay();
                        disp->SetEmotion("happy");
                        otto_controller_queue_action(ACTION_DOG_BOW, 1, 1500, 0, 0);
                    });
                    return;
                }
                if (show_ip) {
                    ESP_LOGI(TAG, "⚡ INSTANT ACTION: Show WiFi IP Address for 30s");
                    Schedule([this]() {
                        auto disp = Board::GetInstance().GetDisplay();
                        disp->SetEmotion("happy");
                        
                        // Get IP address and display it
                        esp_netif_ip_info_t ip_info;
                        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                            char ip_str[64];
                            snprintf(ip_str, sizeof(ip_str), "📱 IP: %d.%d.%d.%d", 
                                     IP2STR(&ip_info.ip));
                            ESP_LOGI("Application", "\033[1;33m🌟 Station IP: " IPSTR "\033[0m", 
                                     IP2STR(&ip_info.ip));
                            disp->SetChatMessage("system", ip_str);
                            // Keep display for 30 seconds
                            xTaskCreate([](void* arg) {
                                vTaskDelay(pdMS_TO_TICKS(30000));
                                auto d = Board::GetInstance().GetDisplay();
                                if (d) {
                                    d->SetEmotion("neutral");
                                    d->SetChatMessage("", "");
                                }
                                ESP_LOGI("Application", "🔓 IP display cleared after 30s");
                                vTaskDelete(NULL);
                            }, "ip_clear", 2048, nullptr, 1, NULL);
                        } else {
                            ESP_LOGE("Application", "❌ Failed to get IP info");
                            disp->SetChatMessage("system", "WiFi chưa kết nối!");
                        }
                    });
                    return;
                }
                if (open_panel) {
                    ESP_LOGI(TAG, "⚡ INSTANT ACTION: Open Control Panel (Start Webserver + Show IP)");
                    Schedule([this]() {
                        // Check if webserver is already running
                        extern bool webserver_enabled;
                        auto disp = Board::GetInstance().GetDisplay();
                        
                        if (!webserver_enabled) {
                            ESP_LOGI(TAG, "🌐 Starting webserver for control panel access");
                            otto_start_webserver();
                        } else {
                            ESP_LOGI(TAG, "🌐 Webserver already running");
                        }
                        
                        // Display IP address with happy emoji for 15 seconds
                        if (disp) {
                            disp->SetEmotion("happy");
                            
                            // Get and display IP address
                            esp_netif_ip_info_t ip_info;
                            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                            if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                                char ip_str[64];
                                snprintf(ip_str, sizeof(ip_str), "📱 IP: %d.%d.%d.%d", 
                                         IP2STR(&ip_info.ip));
                                ESP_LOGI("Application", "🌟 Station IP: " IPSTR, IP2STR(&ip_info.ip));
                                disp->SetChatMessage("system", ip_str);
                                
                                // Keep display for 15 seconds
                                xTaskCreate([](void* arg) {
                                    vTaskDelay(pdMS_TO_TICKS(15000));
                                    auto d = Board::GetInstance().GetDisplay();
                                    if (d) {
                                        d->SetEmotion("neutral");
                                        d->SetChatMessage("", "");
                                    }
                                    ESP_LOGI("Application", "🔓 IP display cleared after 15s");
                                    vTaskDelete(NULL);
                                }, "panel_ip_clear", 2048, nullptr, 1, NULL);
                            } else {
                                ESP_LOGE("Application", "❌ Failed to get IP info");
                                disp->SetChatMessage("system", "✅ Web server đã khởi động!");
                            }
                        }
                    });
                    return;
                }
                
                // Show user's recognized speech (only if NOT a keyword trigger)
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });

                // Voice commands: Toggle between Otto GIF emoji mode and default text emoji mode
                // Keywords (Vietnamese):
                //   - "emoji chính"  => switch to Otto GIF mode (primary/animated)
                //   - "emoji mặc định" => switch to default text mode
                // Also accept unaccented forms: "emoji chinh", "emoji mac dinh"
                // Note: helpers defined above

                bool ask_otto = false;
                bool ask_default = false;

                // Exact keywords only
                if (contains(lower, "emoji chính") || contains(lower, "emoji chinh")) {
                    ask_otto = true;
                }
                if (contains(lower, "emoji mặc định") || contains(lower, "emoji mac dinh")) {
                    ask_default = true;
                }

                if (ask_otto || ask_default) {
                    Schedule([this, ask_otto, ask_default]() {
                        auto disp = Board::GetInstance().GetDisplay();
                        // Try OttoEmojiDisplay specific API when available
                        if (auto otto = dynamic_cast<OttoEmojiDisplay*>(disp)) {
                            if (ask_otto && !ask_default) {
                                ESP_LOGI(TAG, "🎙 Voice cmd: switch to Otto GIF emoji mode");
                                otto->SetEmojiMode(true);
                                otto->SetEmotion("neutral");
                                otto->ShowNotification("Chế độ emoji: Otto GIF", 2000);
                            } else if (ask_default && !ask_otto) {
                                ESP_LOGI(TAG, "🎙 Voice cmd: switch to Default text emoji mode");
                                otto->SetEmojiMode(false);
                                otto->SetEmotion("neutral");
                                otto->ShowNotification("Chế độ emoji: Mặc định", 2000);
                            } else {
                                // If both detected, prefer explicit default unless phrase clearly says otto
                                ESP_LOGI(TAG, "🎙 Voice cmd ambiguous; defaulting to text mode");
                                otto->SetEmojiMode(false);
                                otto->SetEmotion("neutral");
                                otto->ShowNotification("Chế độ emoji: Mặc định", 2000);
                            }
                        } else {
                            // Fallback: use base display only (no Otto-specific toggling)
                            ESP_LOGW(TAG, "Voice emoji mode toggle requested but Otto display not available");
                            disp->ShowNotification("Không hỗ trợ đổi emoji trên màn hình hiện tại", 2500);
                        }
                    });
                }
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    // Skip emotion change if locked (keyword sequence in progress)
                    if (emotion_locked_) {
                        ESP_LOGW(TAG, "⛔ Ignoring LLM emotion '%s' (emotion locked for keyword)", emotion_str.c_str());
                        return;
                    }
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    bool protocol_started = protocol_->Start();

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota.HasServerTime();
    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            // Print the debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
                // SystemInfo::PrintTaskList();
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::OnWakeWordDetected() {
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        // Wake up display immediately when wake word is detected
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            display->SetPowerSaveMode(false);
        }
        auto backlight = Board::GetInstance().GetBacklight();
        if (backlight) {
            backlight->RestoreBrightness();
        }
        ESP_LOGI(TAG, "🔆 Display turned on by wake word detection");
        
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
        
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // Send the state change event
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // Make sure the audio processor is running
            if (!audio_service_.IsAudioProcessorRunning()) {
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(Ota& ota, const std::string& url) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    // Use provided URL or get from OTA object
    std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
    std::string version_info = url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";
    
    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());
    
    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);
    
    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveMode(false);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = ota.StartUpgradeFromUrl(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveMode(true); // Restore power save mode
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    if (protocol_ == nullptr) {
        return;
    }

    // Make sure you are using main thread to send MCP message
    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
        protocol_->SendMcpMessage(payload);
    } else {
        Schedule([this, payload = std::move(payload)]() {
            protocol_->SendMcpMessage(payload);
        });
    }
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

void Application::SendSttMessage(const std::string& text) {
    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    // Validate text length (max 1500 chars)
    std::string validated_text = text;
    if (validated_text.length() > 1500) {
        ESP_LOGW(TAG, "Text too long, truncating to 1500 chars");
        validated_text = validated_text.substr(0, 1500);
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            display->ShowNotification("Text quá dài, đã cắt bớt");
        }
    }

    if (validated_text.empty()) {
        ESP_LOGE(TAG, "Empty text");
        return;
    }

    ESP_LOGI(TAG, "SendSttMessage: %s", validated_text.c_str());

    // Check for special keywords that should be handled locally (not sent to server)
    auto to_lower = [](std::string s) {
        for (auto &ch : s) ch = (char)tolower((unsigned char)ch);
        return s;
    };
    auto contains = [](const std::string &hay, const char* needle) {
        return hay.find(needle) != std::string::npos;
    };
    std::string lower = to_lower(validated_text);
    
    // Check for QR code display keywords
    bool show_qr = contains(lower, "mở qr") || contains(lower, "mo qr") || 
                  contains(lower, "mở mã qr") || contains(lower, "mo ma qr") ||
                  contains(lower, "hiển thị qr") || contains(lower, "hien thi qr") ||
                  contains(lower, "mở mạng qr") || contains(lower, "mo mang qr");
    
    // Check for birthday celebration keywords
    bool birthday_celebration = contains(lower, "chúc mừng sinh nhật") || contains(lower, "chuc mung sinh nhat") ||
                               contains(lower, "happy birthday") || contains(lower, "sinh nhật") ||
                               contains(lower, "sinh nhat") || contains(lower, "chúc mừng") ||
                               contains(lower, "chuc mung");
    
    if (show_qr) {
        ESP_LOGI(TAG, "🔒 QR CODE keyword detected - handling locally (no server send)");
        // Lock emotion IMMEDIATELY
        emotion_locked_ = true;
        ESP_LOGI(TAG, "🔒 Emotion LOCKED for QR code display (winking)");
        
        Schedule([this]() {
            auto disp = Board::GetInstance().GetDisplay();
            // Display user message first
            disp->SetChatMessage("user", "Mở mã QR");
            
            // Get Station IP address instead of activation code
            esp_netif_ip_info_t ip_info;
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            
            if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
                ESP_LOGW(TAG, "⚠️ No WiFi connection - cannot show IP QR");
                disp->SetEmotion("sad");
                disp->SetChatMessage("system", "WiFi chưa kết nối!");
                emotion_locked_ = false;
            } else {
                // Display winking emotion with IP address
                disp->SetEmotion("winking");
                
                char ip_url[128];
                snprintf(ip_url, sizeof(ip_url), "🌐 http://%d.%d.%d.%d", IP2STR(&ip_info.ip));
                disp->SetChatMessage("system", ip_url);
                
                ESP_LOGI(TAG, "📱 Displaying IP QR with winking: " IPSTR, IP2STR(&ip_info.ip));
                
                // Unlock emotion after 15 seconds
                xTaskCreate([](void* arg) {
                    vTaskDelay(pdMS_TO_TICKS(15000));
                    Application* app = static_cast<Application*>(arg);
                    app->Schedule([app]() {
                        app->emotion_locked_ = false;
                        ESP_LOGI("Application", "🔓 Emotion UNLOCKED after IP QR display");
                    });
                    vTaskDelete(NULL);
                }, "qr_unlock", 2048, this, 1, NULL);
            }
        });
        return; // Skip sending to server
    }

    // Handle birthday celebration keywords
    if (birthday_celebration) {
        ESP_LOGI(TAG, "🎂 BIRTHDAY keyword detected - showing silly emoji for 15s");
        // Lock emotion IMMEDIATELY
        emotion_locked_ = true;
        ESP_LOGI(TAG, "🔒 Emotion LOCKED for birthday celebration (silly)");
        
        Schedule([this]() {
            auto disp = Board::GetInstance().GetDisplay();
            // Display user message first
            disp->SetChatMessage("user", "Chúc mừng sinh nhật!");
            
            // Display silly emotion for birthday celebration
            disp->SetEmotion("silly");
            disp->SetChatMessage("system", "🎂 Chúc mừng sinh nhật! 🎂");
            
            ESP_LOGI(TAG, "🎂 Displaying Silly emoji for birthday celebration");
            
            // Unlock emotion after 15 seconds
            xTaskCreate([](void* arg) {
                vTaskDelay(pdMS_TO_TICKS(15000)); // 15 seconds display duration
                Application* app = static_cast<Application*>(arg);
                app->Schedule([app]() {
                    app->emotion_locked_ = false;
                    ESP_LOGI("Application", "🔓 Emotion UNLOCKED after birthday celebration");
                });
                vTaskDelete(NULL);
            }, "birthday_unlock", 2048, this, 1, NULL);
        });
        return; // Skip sending to server
    }

    // Display the user's text message on screen
    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        display->SetChatMessage("user", validated_text.c_str());
    }

    // Open audio channel if not already open
    if (!protocol_->IsAudioChannelOpened()) {
        SetDeviceState(kDeviceStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
            ESP_LOGE(TAG, "Failed to open audio channel");
            SetDeviceState(kDeviceStateIdle);
            return;
        }
    }
    
    // Save current device state to restore later if server doesn't respond
    DeviceState previous_state = device_state_;
    bool was_voice_processing = audio_service_.IsAudioProcessorRunning();
    bool was_wake_word_detection = audio_service_.IsWakeWordRunning();
    
    // Set device to listening state
    SetDeviceState(kDeviceStateListening);

    // Determine wake word: use actual text if short enough, otherwise use first 32 chars
    // Server needs wake word to establish context, so sending actual user text is better than "web_ui"
    std::string wake_word_to_send;
    if (validated_text.length() <= 32) {
        // Text is short enough to use as wake word
        wake_word_to_send = validated_text;
    } else {
        // Text is too long, use first 32 chars (break at word boundary if possible)
        size_t break_pos = 32;
        for (size_t i = 31; i > 0; i--) {
            if (validated_text[i] == ' ' || validated_text[i] == '\n' || validated_text[i] == '\t') {
                break_pos = i;
                break;
            }
        }
        wake_word_to_send = validated_text.substr(0, break_pos);
        // Trim trailing whitespace
        while (!wake_word_to_send.empty() && 
               (wake_word_to_send.back() == ' ' || wake_word_to_send.back() == '\n' || wake_word_to_send.back() == '\t')) {
            wake_word_to_send.pop_back();
        }
    }
    
    // Send wake word detected message with actual user text (or excerpt)
    protocol_->SendWakeWordDetected(wake_word_to_send);
    ESP_LOGI(TAG, "Sent wake word: %s", wake_word_to_send.c_str());
    
    // Store wake word to skip echo from server
    last_web_wake_word_ = wake_word_to_send;

    // Split text into chunks (max 32 chars OR max 10 words per chunk)
    std::vector<std::string> chunks;
    size_t pos = 0;
    
    while (pos < validated_text.length()) {
        size_t chunk_end = pos + 32; // Max 32 chars
        
        // If we're not at the end, try to break at word boundary
        if (chunk_end < validated_text.length()) {
            // Count words in this chunk
            int word_count = 0;
            size_t word_break = pos;
            for (size_t i = pos; i < chunk_end && i < validated_text.length(); ++i) {
                if (validated_text[i] == ' ' || validated_text[i] == '\n' || validated_text[i] == '\t') {
                    word_count++;
                    word_break = i + 1;
                    if (word_count >= 10) {
                        chunk_end = word_break;
                        break;
                    }
                }
            }
            
            // If no space found, use char limit
            if (word_break == pos) {
                // Find last space before chunk_end
                for (size_t i = chunk_end; i > pos; --i) {
                    if (validated_text[i] == ' ' || validated_text[i] == '\n' || validated_text[i] == '\t') {
                        chunk_end = i + 1;
                        break;
                    }
                }
            }
        }
        
        chunk_end = std::min(chunk_end, validated_text.length());
        std::string chunk = validated_text.substr(pos, chunk_end - pos);
        
        // Trim whitespace
        size_t start = chunk.find_first_not_of(" \t\n\r");
        size_t end = chunk.find_last_not_of(" \t\n\r");
        if (start != std::string::npos && end != std::string::npos) {
            chunks.push_back(chunk.substr(start, end - start + 1));
        }
        
        pos = chunk_end;
    }

    // Send STT messages for each chunk
    int chunk_index = 0;
    bool is_chunk = chunks.size() > 1;
    for (const auto& chunk : chunks) {
        // Escape special characters for JSON
        std::string escaped_text;
        escaped_text.reserve(chunk.length() * 2);
        for (char c : chunk) {
            switch (c) {
                case '"':  escaped_text += "\\\""; break;
                case '\\': escaped_text += "\\\\"; break;
                case '\b': escaped_text += "\\b"; break;
                case '\f': escaped_text += "\\f"; break;
                case '\n': escaped_text += "\\n"; break;
                case '\r': escaped_text += "\\r"; break;
                case '\t': escaped_text += "\\t"; break;
                default:
                    if (c < 32) {
                        char buf[7];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        escaped_text += buf;
                    } else {
                        escaped_text += c;
                    }
                    break;
            }
        }

        ESP_LOGI(TAG, "Sending STT chunk %d: %s", chunk_index, escaped_text.c_str());
        protocol_->SendUserText(escaped_text, is_chunk, chunk_index);
        chunk_index++;
        
        // Wait 5.5 seconds between chunks (except for the last chunk)
        // This prevents server timeout and allows AI to process each chunk
        if (chunk_index < (int)chunks.size()) {
            ESP_LOGI(TAG, "Waiting 5.5 seconds before sending next chunk...");
            vTaskDelay(pdMS_TO_TICKS(5500));
        }
    }

    ESP_LOGI(TAG, "Sent %zu STT chunks", chunks.size());
    
    // Send stop listening after all chunks
    vTaskDelay(pdMS_TO_TICKS(500));
    protocol_->SendStopListening();
    ESP_LOGI(TAG, "Sent stop listening signal");
    
    // Schedule timeout handler: if server doesn't respond in 8s, restore previous state
    Schedule([this, previous_state, was_voice_processing, was_wake_word_detection]() {
        vTaskDelay(pdMS_TO_TICKS(8000));  // Wait 8 seconds for server response
        if (device_state_ == kDeviceStateListening) {
            // No response from server, reset to previous state
            ESP_LOGI(TAG, "⚠️ No server response after 8s, resetting to %s state", 
                     previous_state == kDeviceStateIdle ? "idle" : 
                     previous_state == kDeviceStateSpeaking ? "speaking" : "previous");
            SetDeviceState(previous_state == kDeviceStateSpeaking ? kDeviceStateIdle : previous_state);
            if (device_state_ != kDeviceStateListening) {
                audio_service_.EnableVoiceProcessing(was_voice_processing);
                audio_service_.EnableWakeWordDetection(was_wake_word_detection);
            }
        }
    });
}

void Application::OpenControlPanel() {
    auto disp = Board::GetInstance().GetDisplay();
    
    // Get Station IP address
    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    
    if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to get IP - cannot open control panel");
        disp->SetEmotion("sad");
        disp->SetChatMessage("system", "WiFi chưa kết nối!");
        return;
    }
    
    // Start webserver if not already running
    if (!webserver_enabled) {
        ESP_LOGI(TAG, "🌐 Starting webserver for control panel");
        otto_start_webserver();
    }
    
    // Format IP address
    char ip_str[64];
    snprintf(ip_str, sizeof(ip_str), "🌐 http://%d.%d.%d.%d", IP2STR(&ip_info.ip));
    
    ESP_LOGI(TAG, "\033[1;32m🌟 Opening Control Panel: " IPSTR "\033[0m", IP2STR(&ip_info.ip));
    
    // Display IP on screen
    disp->SetEmotion("happy");
    disp->SetChatMessage("system", ip_str);
    
    // Cancel existing timer if any
    if (control_panel_timer_handle_ != nullptr) {
        esp_timer_stop(control_panel_timer_handle_);
        esp_timer_delete(control_panel_timer_handle_);
        control_panel_timer_handle_ = nullptr;
        ESP_LOGI(TAG, "🔄 Cancelled previous control panel timer");
    }
    
    // Create 5-minute auto-close timer
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            auto app = static_cast<Application*>(arg);
            app->Schedule([app]() {
                app->CloseControlPanel();
            });
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "control_panel_timer",
        .skip_unhandled_events = false
    };
    
    esp_err_t err = esp_timer_create(&timer_args, &control_panel_timer_handle_);
    if (err == ESP_OK) {
        // Start timer for 5 minutes (300,000,000 microseconds)
        err = esp_timer_start_once(control_panel_timer_handle_, 300000000ULL);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "⏰ Control panel will auto-close in 5 minutes");
        } else {
            ESP_LOGE(TAG, "❌ Failed to start control panel timer");
        }
    } else {
        ESP_LOGE(TAG, "❌ Failed to create control panel timer");
    }
}

void Application::CloseControlPanel() {
    ESP_LOGI(TAG, "🔒 Closing control panel (5 minutes timeout)");
    
    // Stop webserver to save power
    if (webserver_enabled) {
        ESP_LOGI(TAG, "🌐 Stopping webserver to save power");
        otto_stop_webserver();
    }
    
    auto disp = Board::GetInstance().GetDisplay();
    disp->SetEmotion("neutral");
    disp->SetChatMessage("system", "Bảng điều khiển đã đóng");
    
    // Clean up timer
    if (control_panel_timer_handle_ != nullptr) {
        esp_timer_stop(control_panel_timer_handle_);
        esp_timer_delete(control_panel_timer_handle_);
        control_panel_timer_handle_ = nullptr;
    }
    
    // Return to idle state
    vTaskDelay(pdMS_TO_TICKS(2000));  // Show message for 2 seconds
    SetDeviceState(kDeviceStateIdle);
}
