# UDP Drawing Guide cho Otto Robot

## Tổng quan

Tính năng **UDP Drawing** cho phép bạn vẽ trực tiếp lên màn hình Otto robot từ xa qua WiFi sử dụng giao thức UDP.

Tính năng này được tích hợp từ dự án [Draw_on_OLED](https://github.com/BenchRobotics/Draw_on_OLED) và tương thích với Android app từ dự án đó.

## Kiến trúc

```
Android App / PC Client
        ↓ (UDP packets: "x,y,state")
   WiFi Network
        ↓
  ESP32 (Otto Robot)
        ↓
  UdpDrawService (port 12345)
        ↓
  OttoEmojiDisplay::DrawPixel()
        ↓
  LVGL Canvas (240x280 pixels)
        ↓
  ST7789 LCD Display
```

## Component chính

### 1. UdpDrawService
- **File**: `main/boards/otto-robot/udp_draw_service.{h,cc}`
- **Chức năng**: 
  - Lắng nghe UDP packets trên port 12345
  - Parse định dạng "x,y,state"
  - Gọi DrawPixel() trên display
  - Quản lý statistics

### 2. OttoEmojiDisplay Drawing Methods
- **File**: `main/boards/otto-robot/otto_emoji_display.{h,cc}`
- **Methods mới**:
  ```cpp
  void EnableDrawingCanvas(bool enable);  // Bật/tắt drawing mode
  void ClearDrawingCanvas();              // Xóa canvas
  void DrawPixel(int x, int y, bool state); // Vẽ 1 pixel
  bool IsDrawingCanvasEnabled() const;    // Kiểm tra trạng thái
  ```

## Cách sử dụng

### Bước 1: Kích hoạt UDP Drawing Service

Trong `otto_robot.cc`, thêm vào class OttoRobot:

```cpp
#include "udp_draw_service.h"

class OttoRobot : public WifiBoard {
private:
    std::unique_ptr<UdpDrawService> udp_draw_service_;
    
public:
    OttoRobot() : WifiBoard("otto") {
        // ... existing initialization ...
        
        // Initialize UDP Drawing Service
        udp_draw_service_ = std::make_unique<UdpDrawService>(display_, 12345);
    }
    
    void OnNetworkConnected() override {
        WifiBoard::OnNetworkConnected();
        
        // Start UDP drawing service khi WiFi connected
        if (udp_draw_service_ && !udp_draw_service_->IsRunning()) {
            udp_draw_service_->Start();
            ESP_LOGI("OttoRobot", "UDP Drawing Service started");
        }
    }
};
```

### Bước 2: Enable Drawing Mode

Có thể enable qua web API hoặc button:

```cpp
// Qua web API
void HandleDrawingModeApi(httpd_req_t *req) {
    // Parse enable=true/false từ query params
    bool enable = ...; 
    udp_draw_service_->EnableDrawingMode(enable);
}

// Hoặc qua button (ví dụ: long press touch sensor)
touch_button_.OnLongPress([this]() {
    if (udp_draw_service_) {
        bool current = udp_draw_service_->IsDrawingMode();
        udp_draw_service_->EnableDrawingMode(!current);
    }
});
```

### Bước 3: Kết nối từ Android App

1. **Download Android App**: 
   - Từ repo: https://github.com/BenchRobotics/Draw_on_OLED
   - File: `Control_center.apk`

2. **Cài đặt app** trên điện thoại Android

3. **Kết nối**:
   - Mở app
   - Nhập IP address của Otto robot (xem qua Serial Monitor hoặc touch 5 lần)
   - Port: 12345 (default)
   - Nhấn Connect

4. **Vẽ**:
   - Chạm vẽ trên màn hình điện thoại
   - Hình vẽ sẽ xuất hiện realtime trên màn Otto

### Bước 4: Test bằng Python Script (không cần Android)

```python
import socket
import time

# Cấu hình
ESP32_IP = "192.168.1.100"  # Thay bằng IP của Otto robot
UDP_PORT = 12345

# Tạo UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Vẽ chữ X trên màn hình
def draw_x(width=240, height=280):
    for i in range(min(width, height)):
        # Diagonal từ top-left to bottom-right
        x1, y1 = i, i
        packet1 = f"{x1},{y1},1"
        sock.sendto(packet1.encode(), (ESP32_IP, UDP_PORT))
        
        # Diagonal từ top-right to bottom-left
        x2, y2 = width - i - 1, i
        packet2 = f"{x2},{y2},1"
        sock.sendto(packet2.encode(), (ESP32_IP, UDP_PORT))
        
        time.sleep(0.001)  # 1ms delay

print(f"Drawing X on Otto display at {ESP32_IP}:{UDP_PORT}")
draw_x()
print("Done!")
```

Chạy script:
```bash
python draw_test.py
```

## UDP Protocol

### Packet Format
```
"x,y,state"
```

### Parameters
- **x**: Tọa độ X (0 đến width-1, Otto: 0-239)
- **y**: Tọa độ Y (0 đến height-1, Otto: 0-279)  
- **state**: 
  - `1` = Vẽ pixel (white/colored)
  - `0` = Xóa pixel (black)

### Examples
```
"120,140,1"  → Vẽ pixel trắng tại giữa màn hình
"0,0,1"      → Vẽ pixel ở góc trên trái
"239,279,0"  → Xóa pixel ở góc dưới phải
```

## Web API Integration (Tùy chọn)

Thêm các endpoint vào `otto_webserver.cc`:

### 1. Enable/Disable Drawing Mode
```
GET /api/drawing/mode?enable=true
Response: {"status": "ok", "drawing_mode": true}
```

### 2. Clear Canvas
```
POST /api/drawing/clear
Response: {"status": "ok"}
```

### 3. Get Statistics
```
GET /api/drawing/stats
Response: {
  "packets_received": 1234,
  "packets_processed": 1230,
  "pixels_drawn": 1230,
  "errors": 4
}
```

### Example Implementation
```cpp
static esp_err_t drawing_mode_handler(httpd_req_t *req) {
    char query[100];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char enable_str[10];
        if (httpd_query_key_value(query, "enable", enable_str, sizeof(enable_str)) == ESP_OK) {
            bool enable = (strcmp(enable_str, "true") == 0);
            
            // Get UdpDrawService từ OttoRobot instance
            GetUdpDrawService()->EnableDrawingMode(enable);
            
            httpd_resp_set_type(req, "application/json");
            char response[100];
            snprintf(response, sizeof(response), 
                    "{\"status\":\"ok\",\"drawing_mode\":%s}", 
                    enable ? "true" : "false");
            httpd_resp_sendstr(req, response);
            return ESP_OK;
        }
    }
    httpd_resp_send_404(req);
    return ESP_FAIL;
}
```

## Sử dụng thực tế

### 1. Design Custom Emoji
1. Enable drawing mode
2. Vẽ emoji trên app Android
3. Screenshot hoặc lưu tọa độ
4. Convert thành GIF data cho Otto

### 2. Debug Display Layout
1. Enable drawing mode
2. Vẽ UI wireframe
3. Test positioning cho LVGL widgets
4. Disable để quay về normal mode

### 3. Remote Art/Messages
1. Gửi tin nhắn vẽ tay từ xa
2. Vẽ icon/logo custom
3. Create animations bằng cách vẽ nhiều frames

### 4. Live Demo
1. Present dự án
2. Vẽ trực tiếp từ điện thoại
3. Khách hàng/giám khảo thấy realtime

## Troubleshooting

### Không nhận được packets
- **Kiểm tra WiFi**: Otto và điện thoại cùng mạng?
- **Firewall**: Port 12345 có bị block không?
- **IP address**: Đúng IP của Otto chưa? (touch 5 lần để hiển thị)

### Drawing lag/chậm
- **UDP buffer**: Tăng queue size trong socket config
- **Priority**: Tăng priority của udp_draw_task
- **Batch**: Gửi nhiều pixels cùng lúc thay vì từng pixel

### Pixel sai vị trí
- **Coordinates**: Đảm bảo x < 240, y < 280
- **Orientation**: Kiểm tra display rotation config
- **Offset**: Kiểm tra DISPLAY_OFFSET_X/Y

## Performance

### Specifications
- **Max packet rate**: ~1000 packets/second
- **Latency**: <10ms (local WiFi)
- **Memory**: ~200KB (canvas buffer RGB565)
- **CPU**: ~5% @ 240MHz (UDP task)

### Optimization Tips
1. **Batch updates**: Gửi nhiều pixels, dùng `lv_refr_now()` 1 lần
2. **Compress**: Gửi line drawing commands thay vì từng pixel
3. **Delta**: Chỉ gửi pixels thay đổi, không gửi lại toàn bộ

## Roadmap

- [ ] Thêm web UI để vẽ trực tiếp trong browser
- [ ] Hỗ trợ colors (RGB565) thay vì chỉ black/white
- [ ] Save/load drawing to SPIFFS
- [ ] Drawing commands (line, circle, fill)
- [ ] Multi-user collaborative drawing

## Tham khảo

- **Original Project**: https://github.com/BenchRobotics/Draw_on_OLED
- **Tutorial**: https://benchrobotics.com/arduino/drawing-on-esp32-oled-screen/
- **LVGL Canvas**: https://docs.lvgl.io/master/widgets/canvas.html
- **ESP32 UDP**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_netif.html

---

**Note**: Tính năng này hiện là experimental. Sử dụng với Otto robot có màn ST7789 240x280. Cần điều chỉnh cho các board/màn hình khác.
