#include "qr_code_display.h"
#include <esp_log.h>
#include <qrcode.h>
#include <cstring>

static const char* TAG = "QrCodeDisplay";

// Static member to store current instance for callback
static QrCodeDisplay* s_current_instance = nullptr;

QrCodeDisplay::~QrCodeDisplay() {
    Cleanup();
}

// Static callback function for esp_qrcode_generate
static void qr_display_callback(esp_qrcode_handle_t qrcode) {
    if (s_current_instance) {
        s_current_instance->RenderQrCode(qrcode);
    }
}

bool QrCodeDisplay::Show(const char* text, lv_obj_t* parent) {
    if (!text || strlen(text) == 0) {
        ESP_LOGE(TAG, "Invalid text");
        return false;
    }

    // Clean up previous QR code if exists
    Cleanup();

    // Save parent for canvas creation
    parent_ = parent ? parent : lv_screen_active();

    // Set current instance for callback
    s_current_instance = this;

    // Configure QR code generation with our callback
    esp_qrcode_config_t config = {
        .display_func = qr_display_callback,
        .max_qrcode_version = 10,
        .qrcode_ecc_level = ESP_QRCODE_ECC_LOW,
    };

    // Generate QR code (will call our callback)
    esp_err_t err = esp_qrcode_generate(&config, text);
    
    // Clear current instance
    s_current_instance = nullptr;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "QR code generation failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "QR code displayed successfully");
    return true;
}

void QrCodeDisplay::RenderQrCode(esp_qrcode_handle_t qrcode) {
    // Get QR code size
    int qr_size = esp_qrcode_get_size(qrcode);
    if (qr_size <= 0) {
        ESP_LOGE(TAG, "Invalid QR code size: %d", qr_size);
        return;
    }

    // Create canvas to display QR code
    if (!CreateCanvas(qr_size)) {
        ESP_LOGE(TAG, "Failed to create canvas");
        return;
    }

    // Draw QR code on canvas
    DrawQrCode(qrcode, qr_size);

    ESP_LOGI(TAG, "QR code rendered (size: %d)", qr_size);
}

void QrCodeDisplay::Hide() {
    Cleanup();
}

bool QrCodeDisplay::CreateCanvas(int qr_size) {
    // Calculate canvas size with 4x scaling
    const int scale = 4;
    const int padding = 8;  // White padding around QR code
    int canvas_width = (qr_size * scale) + (padding * 2);
    int canvas_height = canvas_width;

    // Create canvas
    qr_canvas_ = lv_canvas_create(parent_);
    if (!qr_canvas_) {
        ESP_LOGE(TAG, "Failed to create canvas object");
        return false;
    }

    // Allocate canvas buffer (LVGL 9.x uses LV_CANVAS_BUF_SIZE)
    size_t buf_size = lv_canvas_buf_size(canvas_width, canvas_height, LV_COLOR_FORMAT_RGB565, 1);
    uint8_t* canvas_buf = (uint8_t*)malloc(buf_size);
    if (!canvas_buf) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer");
        lv_obj_del(qr_canvas_);
        qr_canvas_ = nullptr;
        return false;
    }

    // Set canvas buffer
    lv_canvas_set_buffer(qr_canvas_, canvas_buf, canvas_width, canvas_height, LV_COLOR_FORMAT_RGB565);
    
    // Fill canvas with white background
    lv_canvas_fill_bg(qr_canvas_, lv_color_white(), LV_OPA_COVER);

    // Center canvas on screen
    lv_obj_center(qr_canvas_);

    // Create label for instruction text
    qr_label_ = lv_label_create(parent_);
    if (qr_label_) {
        lv_label_set_text(qr_label_, "Scan QR Code");
        lv_obj_align_to(qr_label_, qr_canvas_, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    }

    return true;
}

void QrCodeDisplay::DrawQrCode(esp_qrcode_handle_t qrcode, int qr_size) {
    if (!qr_canvas_) {
        return;
    }

    const int scale = 4;
    const int padding = 8;

    // Draw QR code modules on canvas
    for (int y = 0; y < qr_size; y++) {
        for (int x = 0; x < qr_size; x++) {
            bool is_black = esp_qrcode_get_module(qrcode, x, y);
            lv_color_t color = is_black ? lv_color_black() : lv_color_white();

            // Draw scaled pixel (scale x scale block)
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    int px = padding + (x * scale) + dx;
                    int py = padding + (y * scale) + dy;
                    lv_canvas_set_px(qr_canvas_, px, py, color, LV_OPA_COVER);
                }
            }
        }
    }
}

void QrCodeDisplay::Cleanup() {
    if (qr_canvas_) {
        // Free canvas buffer
        const void* buf = lv_canvas_get_buf(qr_canvas_);
        if (buf) {
            free(const_cast<void*>(buf));
        }
        lv_obj_del(qr_canvas_);
        qr_canvas_ = nullptr;
    }

    if (qr_label_) {
        lv_obj_del(qr_label_);
        qr_label_ = nullptr;
    }
}
