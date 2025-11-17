#ifndef QR_CODE_DISPLAY_H
#define QR_CODE_DISPLAY_H

#include <string>
#include <lvgl.h>
#include <qrcode.h>

class QrCodeDisplay {
public:
    QrCodeDisplay() = default;
    ~QrCodeDisplay();

    // Generate and display QR code on LVGL canvas
    // Returns true if successful
    bool Show(const char* text, lv_obj_t* parent = nullptr);
    
    // Hide the QR code
    void Hide();
    
    // Check if QR code is currently displayed
    bool IsVisible() const { return qr_canvas_ != nullptr; }

    // Called by the QR code generation callback
    void RenderQrCode(esp_qrcode_handle_t qrcode);

private:
    lv_obj_t* qr_canvas_ = nullptr;
    lv_obj_t* qr_label_ = nullptr;
    lv_obj_t* parent_ = nullptr;
    
    bool CreateCanvas(int qr_size);
    void DrawQrCode(esp_qrcode_handle_t qrcode, int qr_size);
    void Cleanup();
};

#endif // QR_CODE_DISPLAY_H
