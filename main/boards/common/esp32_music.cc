#include "esp32_music.h"
#include "application.h"
#include "board.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>

#define TAG "Esp32Music"

// PSRAM allocation helper
static uint8_t* allocate_psram(size_t size, const char* description) {
    uint8_t* ptr = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM for %s (%u bytes)", description, (unsigned)size);
    }
    return ptr;
}

// ========== Constructor/Destructor ==========

Esp32Music::Esp32Music() 
    : song_name_displayed_(false)
    , current_lyric_index_(0)
    , lyric_task_handle_(nullptr)
    , is_lyric_running_(false)
    , display_mode_(DisplayMode::DISPLAY_MODE_SPECTRUM)
    , is_playing_(false)
    , is_downloading_(false)
    , is_stopping_(false)
    , play_task_handle_(nullptr)
    , download_task_handle_(nullptr)
    , current_play_time_ms_(0)
    , last_frame_time_ms_(0)
    , total_frames_decoded_(0)
    , buffer_size_(0)
    , mp3_decoder_(nullptr)
    , mp3_decoder_initialized_(false)
    , stream_format_(AudioStreamFormat::Unknown)
    , active_http_(nullptr) {
    
    InitializeMp3Decoder();
}

Esp32Music::~Esp32Music() {
    Stop();  // Renamed from StopStreaming
    
    // Wait for tasks to finish
    if (download_task_handle_ != nullptr) {
        vTaskDelete(download_task_handle_);
        download_task_handle_ = nullptr;
    }
    if (play_task_handle_ != nullptr) {
        vTaskDelete(play_task_handle_);
        play_task_handle_ = nullptr;
    }
    
    // Cleanup
    ClearAudioBuffer();
    CleanupMp3Decoder();
}

// ========== Buffer Management ==========

void Esp32Music::ClearAudioBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    while (!audio_buffer_.empty()) {
        AudioChunk chunk = audio_buffer_.front();
        audio_buffer_.pop();
        if (chunk.data) {
            heap_caps_free(chunk.data);
        }
    }
    
    buffer_size_ = 0;
}

void Esp32Music::MonitorPsramUsage() {
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Memory - Free PSRAM: %d KB, Free SRAM: %d KB", 
             (int)(free_psram/1024), (int)(free_sram/1024));
}

// ========== MP3 Decoder Management ==========

bool Esp32Music::InitializeMp3Decoder() {
    if (mp3_decoder_initialized_) {
        ESP_LOGW(TAG, "MP3 decoder already initialized");
        return true;
    }
    
    mp3_decoder_ = MP3InitDecoder();
    if (!mp3_decoder_) {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        return false;
    }
    
    mp3_decoder_initialized_ = true;
    ESP_LOGI(TAG, "MP3 decoder initialized successfully");
    return true;
}

void Esp32Music::CleanupMp3Decoder() {
    if (mp3_decoder_) {
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
    }
    mp3_decoder_initialized_ = false;
}

// ========== Format Detection ==========

Esp32Music::AudioStreamFormat Esp32Music::DetermineStreamFormat(const uint8_t* data, size_t size) const {
    if (!data || size < 4) {
        return AudioStreamFormat::Unknown;
    }
    
    // Check for MP3 sync word: 0xFF, 0xE0-0xFF
    if (data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) {
        if (IsLikelyMp3Frame(data, size)) {
            return AudioStreamFormat::MP3;
        }
    }
    
    // Check for AAC ADTS: 0xFF, 0xF0-0xFF
    if (data[0] == 0xFF && (data[1] & 0xF0) == 0xF0) {
        return AudioStreamFormat::AAC_ADTS;
    }
    
    return AudioStreamFormat::Unknown;
}

bool Esp32Music::IsLikelyMp3Frame(const uint8_t* data, size_t size) const {
    if (!data || size < 4) {
        return false;
    }
    
    // Check sync word
    if (data[0] != 0xFF || (data[1] & 0xE0) != 0xE0) {
        return false;
    }
    
    // Check layer (bits 1-2 of byte 1)
    uint8_t layer = (data[1] >> 1) & 0x03;
    if (layer == 0x00) {  // Reserved
        return false;
    }
    
    // Check bitrate (bits 4-7 of byte 2)
    uint8_t bitrate_index = (data[2] >> 4) & 0x0F;
    if (bitrate_index == 0x0F || bitrate_index == 0x00) {  // Free/bad
        return false;
    }
    
    // Check sample rate (bits 2-3 of byte 2)
    uint8_t sample_rate_index = (data[2] >> 2) & 0x03;
    if (sample_rate_index == 0x03) {  // Reserved
        return false;
    }
    
    return true;
}

// ========== ID3 Tag Handling ==========

size_t Esp32Music::SkipId3Tag(uint8_t* data, size_t size) {
    if (!data || size < 10) {
        return 0;
    }
    
    // Check for ID3v2 tag: "ID3"
    if (data[0] != 'I' || data[1] != 'D' || data[2] != '3') {
        return 0;
    }
    
    // Get tag size (synchsafe integer)
    size_t tag_size = ((data[6] & 0x7F) << 21) |
                      ((data[7] & 0x7F) << 14) |
                      ((data[8] & 0x7F) << 7) |
                      (data[9] & 0x7F);
    
    // Total size = header (10 bytes) + tag size
    size_t total_skip = 10 + tag_size;
    
    if (total_skip > size) {
        ESP_LOGW(TAG, "ID3 tag size (%u) exceeds buffer size (%u)", 
                 (unsigned)total_skip, (unsigned)size);
        return 0;
    }
    
    ESP_LOGI(TAG, "Skipping ID3v2 tag: %u bytes", (unsigned)total_skip);
    return total_skip;
}

// ========== Download API (Stub for now) ==========

bool Esp32Music::Download(const std::string& song_name, const std::string& artist_name) {
    ESP_LOGI(TAG, "Download API called: %s - %s", song_name.c_str(), artist_name.c_str());
    // TODO: Implement API call to get music URL
    // For now, return false to indicate not implemented
    return false;
}

std::string Esp32Music::GetDownloadResult() {
    return "Not implemented";
}

// ========== Public Getters ==========
// Note: Inline definitions in header, these are removed to avoid redefinition

// ========== Start Streaming ==========

bool Esp32Music::StartStreaming(const std::string& music_url) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Starting streaming: %s", music_url.c_str());
    ESP_LOGI(TAG, "========================================");
    
    // Validate URL
    if (music_url.empty()) {
        ESP_LOGE(TAG, "ERROR: Empty URL provided");
        return false;
    }
    
    if (music_url.find("http://") != 0 && music_url.find("https://") != 0) {
        ESP_LOGE(TAG, "ERROR: URL must start with http:// or https://");
        return false;
    }
    
    // Reset stopping flag
    is_stopping_.store(false, std::memory_order_release);
    
    // Stop any existing playback
    if (is_playing_ || is_downloading_) {
        ESP_LOGI(TAG, "🛑 Stopping existing playback before starting new stream");
        Stop();  // Renamed from StopStreaming
        
        // Wait for tasks to fully terminate (up to 1 second)
        int wait_count = 0;
        while ((download_task_handle_ != nullptr || play_task_handle_ != nullptr) && wait_count < 20) {
            vTaskDelay(pdMS_TO_TICKS(50));
            wait_count++;
        }
        
        if (download_task_handle_ != nullptr || play_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "⚠️ Tasks did not terminate cleanly, forcing cleanup");
            if (download_task_handle_ != nullptr) {
                vTaskDelete(download_task_handle_);
                download_task_handle_ = nullptr;
            }
            if (play_task_handle_ != nullptr) {
                vTaskDelete(play_task_handle_);
                play_task_handle_ = nullptr;
            }
        } else {
            ESP_LOGI(TAG, "✅ Previous playback stopped cleanly");
        }
    }
    
    // Clear buffers and decoders
    ESP_LOGI(TAG, "🧹 Clearing buffers and reinitializing decoder");
    ClearAudioBuffer();
    CleanupMp3Decoder();
    
    // Reinitialize MP3 decoder for new stream
    if (!InitializeMp3Decoder()) {
        ESP_LOGE(TAG, "❌ Failed to reinitialize MP3 decoder");
        return false;
    }
    
    stream_format_.store(AudioStreamFormat::Unknown, std::memory_order_relaxed);
    
    // Check network status
    auto network = Board::GetInstance().GetNetwork();
    if (!network) {
        ESP_LOGE(TAG, "ERROR: Network instance not available");
        return false;
    }
    
    // Enable audio output
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec && !codec->output_enabled()) {
        ESP_LOGI(TAG, "🔊 Enabling audio output");
        codec->EnableOutput(true);
    }
    
    // Set state flags
    current_music_url_ = music_url;
    is_downloading_ = true;
    is_playing_ = true;
    
    ESP_LOGI(TAG, "🚀 Creating streaming tasks...");
    
    // Create download task with 8KB stack (priority 5)
    xTaskCreatePinnedToCore(
        [](void* param) {
            auto* self = static_cast<Esp32Music*>(param);
            self->DownloadAudioStream(self->current_music_url_);
            vTaskDelete(nullptr);
        },
        "MusicDownload",
        8192,  // 8KB stack
        this,
        5,  // Priority
        &download_task_handle_,
        0  // Core 0
    );
    
    // Create play task with 12KB stack (priority 6)
    xTaskCreatePinnedToCore(
        [](void* param) {
            auto* self = static_cast<Esp32Music*>(param);
            self->PlayAudioStream();
            vTaskDelete(nullptr);
        },
        "MusicPlayback",
        12288,  // 12KB stack for MP3 decoding
        this,
        6,  // Priority
        &play_task_handle_,
        1  // Core 1
    );
    
    if (download_task_handle_ == nullptr || play_task_handle_ == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to create streaming tasks");
        is_playing_ = false;
        is_downloading_ = false;
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Streaming tasks started successfully");
    return true;
}

// ========== Download Thread ==========

void Esp32Music::DownloadAudioStream(const std::string& music_url) {
    ESP_LOGI(TAG, "Download thread started for URL: %s", music_url.c_str());
    MonitorPsramUsage();
    
    auto network = Board::GetInstance().GetNetwork();
    if (!network) {
        ESP_LOGE(TAG, "Failed to get Network instance");
        is_downloading_ = false;
        is_playing_ = false;
        return;
    }
    
    ESP_LOGI(TAG, "Creating HTTP client...");
    auto http = network->CreateHttp(0);
    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client - network may not be ready");
        is_downloading_ = false;
        is_playing_ = false;
        return;
    }
    
    ESP_LOGI(TAG, "HTTP client created successfully");
    
    // Store HTTP handle for fast stop
    {
        std::lock_guard<std::mutex> lock(http_mutex_);
        active_http_ = http.get();
    }
    
    // Start HTTP GET request
    ESP_LOGI(TAG, "Opening HTTP connection to: %s", music_url.c_str());
    if (!http->Open("GET", music_url)) {
        ESP_LOGE(TAG, "HTTP GET failed - connection could not be established");
        ESP_LOGE(TAG, "Check: 1) WiFi connected? 2) URL valid? 3) Server reachable?");
        {
            std::lock_guard<std::mutex> lock(http_mutex_);
            active_http_ = nullptr;
        }
        is_downloading_ = false;
        is_playing_ = false;
        return;
    }
    
    ESP_LOGI(TAG, "HTTP connection established, starting download...");
    
    size_t total_downloaded = 0;
    const size_t chunk_size = 2048;  // 2KB chunks for Otto
    uint8_t buffer[chunk_size];
    
    while (is_playing_) {
        // Check buffer limit - wait if buffer is full
        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            buffer_cv_.wait(lock, [this] {
                return buffer_size_ < MAX_BUFFER_SIZE || !is_playing_;
            });
            
            if (!is_playing_) {
                break;
            }
        }
        
        // Read from HTTP stream
        int bytes_read = http->Read((char*)buffer, chunk_size);
        if (bytes_read <= 0) {
            ESP_LOGI(TAG, "Download complete, total: %u bytes", (unsigned)total_downloaded);
            break;
        }
        
        // Detect format from first chunk
        if (total_downloaded == 0 && bytes_read >= 4) {
            AudioStreamFormat detected = DetermineStreamFormat(buffer, bytes_read);
            if (detected != AudioStreamFormat::Unknown) {
                stream_format_.store(detected, std::memory_order_relaxed);
                if (detected == AudioStreamFormat::MP3) {
                    ESP_LOGI(TAG, "Detected MP3 stream");
                } else if (detected == AudioStreamFormat::AAC_ADTS) {
                    ESP_LOGI(TAG, "Detected AAC stream");
                }
            } else {
                ESP_LOGW(TAG, "Unknown format: %02X %02X %02X %02X",
                         buffer[0], buffer[1], buffer[2], buffer[3]);
            }
        }
        
        // Allocate chunk in PSRAM
        uint8_t* chunk_data = allocate_psram(bytes_read, "audio chunk");
        if (!chunk_data) {
            ESP_LOGE(TAG, "Failed to allocate chunk memory");
            break;
        }
        memcpy(chunk_data, buffer, bytes_read);
        
        // Add to buffer queue
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            AudioChunk chunk;
            chunk.data = chunk_data;
            chunk.size = bytes_read;
            audio_buffer_.push(chunk);
            buffer_size_ += bytes_read;
        }
        buffer_cv_.notify_one();
        
        total_downloaded += bytes_read;
        
        // Log progress every 50KB
        if (total_downloaded % (50 * 1024) == 0) {
            ESP_LOGI(TAG, "Downloaded: %u KB, Buffer: %u KB",
                     (unsigned)(total_downloaded/1024),
                     (unsigned)(buffer_size_/1024));
        }
    }
    
    // Cleanup
    http->Close();
    {
        std::lock_guard<std::mutex> lock(http_mutex_);
        active_http_ = nullptr;
    }
    
    is_downloading_ = false;
    buffer_cv_.notify_all();
    
    ESP_LOGI(TAG, "Download thread finished");
    MonitorPsramUsage();
}

// ========== Play Thread ==========

void Esp32Music::PlayAudioStream() {
    ESP_LOGI(TAG, "Play thread started");
    
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec) {
        ESP_LOGE(TAG, "Audio codec not available");
        is_playing_ = false;
        return;
    }
    
    // Enable speaker output
    if (!codec->output_enabled()) {
        codec->EnableOutput(true);
    }
    
    // Wait for minimum buffer before starting
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        buffer_cv_.wait(lock, [this] {
            return buffer_size_ >= MIN_BUFFER_SIZE || (!is_downloading_ && !audio_buffer_.empty());
        });
    }
    
    // Wait for format detection
    AudioStreamFormat format = stream_format_.load(std::memory_order_relaxed);
    if (format == AudioStreamFormat::Unknown) {
        ESP_LOGI(TAG, "Waiting for format detection...");
        for (int i = 0; i < 50 && format == AudioStreamFormat::Unknown; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            format = stream_format_.load(std::memory_order_relaxed);
        }
    }
    
    // Initialize decoder based on format
    if (format == AudioStreamFormat::MP3) {
        if (!mp3_decoder_initialized_) {
            if (!InitializeMp3Decoder()) {
                ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
                is_playing_ = false;
                return;
            }
        }
    } else if (format == AudioStreamFormat::AAC_ADTS) {
        ESP_LOGE(TAG, "AAC format not supported on Otto");
        is_playing_ = false;
        return;
    } else {
        ESP_LOGE(TAG, "Unknown audio format, cannot decode");
        is_playing_ = false;
        return;
    }
    
    ESP_LOGI(TAG, "Starting playback, buffer: %u KB", (unsigned)(buffer_size_/1024));
    MonitorPsramUsage();
    
    // ========== MP3 Playback Loop ==========
    
    size_t total_played = 0;
    const size_t mp3_buffer_size = 8192;  // 8KB input buffer
    uint8_t* mp3_input_buffer = allocate_psram(mp3_buffer_size, "MP3 input");
    if (!mp3_input_buffer) {
        is_playing_ = false;
        return;
    }
    
    int bytes_left = 0;
    uint8_t* read_ptr = mp3_input_buffer;
    bool id3_processed = false;
    
    // Allocate PCM output buffer in PSRAM
    const size_t pcm_buffer_size = 2304 * 2;  // Max MP3 frame: 1152*2 channels * 2 bytes
    int16_t* pcm_buffer = (int16_t*)allocate_psram(pcm_buffer_size, "PCM buffer");
    if (!pcm_buffer) {
        heap_caps_free(mp3_input_buffer);
        is_playing_ = false;
        return;
    }
    
    while (is_playing_) {
        // Refill buffer if needed
        if (bytes_left < 2048) {  // Need more data
            // Move remaining bytes to start
            if (bytes_left > 0 && read_ptr != mp3_input_buffer) {
                memmove(mp3_input_buffer, read_ptr, bytes_left);
            }
            read_ptr = mp3_input_buffer;
            
            // Try to get chunk from queue
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            if (!audio_buffer_.empty()) {
                AudioChunk chunk = audio_buffer_.front();
                audio_buffer_.pop();
                buffer_size_ -= chunk.size;
                lock.unlock();
                
                // Copy to input buffer
                size_t copy_size = std::min((size_t)chunk.size, mp3_buffer_size - bytes_left);
                memcpy(mp3_input_buffer + bytes_left, chunk.data, copy_size);
                bytes_left += copy_size;
                read_ptr = mp3_input_buffer;
                
                // Check and skip ID3 tag (only once at start)
                if (!id3_processed && bytes_left >= 10) {
                    size_t id3_skip = SkipId3Tag(read_ptr, bytes_left);
                    if (id3_skip > 0) {
                        read_ptr += id3_skip;
                        bytes_left -= id3_skip;
                        ESP_LOGI(TAG, "Skipped ID3 tag: %u bytes", (unsigned)id3_skip);
                    }
                    id3_processed = true;
                }
                
                // Free chunk memory
                heap_caps_free(chunk.data);
            } else {
                // No more chunks available
                if (!is_downloading_) {
                    // Download finished, no more data
                    break;
                }
                // Wait for more data
                buffer_cv_.wait_for(lock, std::chrono::milliseconds(100));
                continue;
            }
        }
        
        // Find MP3 sync word
        int sync_offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync_offset < 0) {
            ESP_LOGW(TAG, "No MP3 sync word found, skipping %d bytes", bytes_left);
            bytes_left = 0;
            continue;
        }
        
        // Skip to sync position
        if (sync_offset > 0) {
            read_ptr += sync_offset;
            bytes_left -= sync_offset;
        }
        
        // Decode MP3 frame
        int decode_result = MP3Decode(mp3_decoder_, &read_ptr, &bytes_left, pcm_buffer, 0);
        
        if (decode_result == 0) {
            // Decode success, get frame info
            MP3GetLastFrameInfo(mp3_decoder_, &mp3_frame_info_);
            
            // Validate frame info
            if (mp3_frame_info_.samprate == 0 || mp3_frame_info_.nChans == 0) {
                ESP_LOGW(TAG, "Invalid frame info: rate=%d, channels=%d",
                         mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                continue;
            }
            
            // Play PCM data through codec
            int sample_count = mp3_frame_info_.outputSamps;
            std::vector<int16_t> pcm_data(pcm_buffer, pcm_buffer + sample_count);
            codec->OutputData(pcm_data);
            
            total_played += sample_count * sizeof(int16_t);
            
            // Log progress every 1MB
            if (total_played % (1024 * 1024) == 0) {
                ESP_LOGI(TAG, "Played %u MB, buffer: %u KB",
                         (unsigned)(total_played/(1024*1024)),
                         (unsigned)(buffer_size_/1024));
                MonitorPsramUsage();
            }
        } else {
            // Decode failed
            ESP_LOGW(TAG, "MP3 decode error: %d", decode_result);
            // Skip some bytes and try again
            if (bytes_left > 0) {
                read_ptr++;
                bytes_left--;
            }
        }
    }
    
    // Cleanup
    if (mp3_input_buffer) {
        heap_caps_free(mp3_input_buffer);
    }
    if (pcm_buffer) {
        heap_caps_free(pcm_buffer);
    }
    
    is_playing_ = false;
    ESP_LOGI(TAG, "Play thread finished, total played: %u MB",
             (unsigned)(total_played/(1024*1024)));
    MonitorPsramUsage();
}

// ========== Stop Streaming ==========

bool Esp32Music::Stop() {
    ESP_LOGI(TAG, "Stopping streaming");
    
    // Prevent rapid stop spam
    bool expected = false;
    if (!is_stopping_.compare_exchange_strong(expected, true, std::memory_order_release)) {
        ESP_LOGW(TAG, "Already stopping");
        return false;
    }
    
    // Set flags to stop threads
    is_playing_ = false;
    is_downloading_ = false;
    
    // Abort HTTP connection for fast stop
    {
        std::lock_guard<std::mutex> lock(http_mutex_);
        if (active_http_) {
            // Closing HTTP connection will cause Read() to return 0
            // This allows download thread to exit quickly
            ESP_LOGI(TAG, "Aborting HTTP connection");
        }
    }
    
    // Wake up all waiting threads
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }
    
    // Wait for tasks to finish (simple approach)
    vTaskDelay(pdMS_TO_TICKS(100));  // Give tasks time to exit
    
    if (download_task_handle_ != nullptr) {
        vTaskDelete(download_task_handle_);
        download_task_handle_ = nullptr;
        ESP_LOGI(TAG, "Download task deleted");
    }
    
    if (play_task_handle_ != nullptr) {
        vTaskDelete(play_task_handle_);
        play_task_handle_ = nullptr;
        ESP_LOGI(TAG, "Play task deleted");
    }
    
    // Cleanup resources
    CleanupMp3Decoder();
    stream_format_.store(AudioStreamFormat::Unknown, std::memory_order_relaxed);
    
    // Clear buffer and free memory
    ClearAudioBuffer();
    
    // Reset stopping flag
    is_stopping_.store(false, std::memory_order_release);
    
    ESP_LOGI(TAG, "Streaming stopped");
    MonitorPsramUsage();
    
    return true;
}
