# 🧪 Hướng dẫn Test OTA Local

## 📋 Chuẩn bị

Robot ESP32 đang chạy firmware hiện tại (version 2.0.3)
Máy tính và robot cùng WiFi (192.168.0.x)

## 🚀 Các bước test OTA

### Bước 1: Chuyển sang chế độ test local

```powershell
.\switch_ota_url.ps1 -Mode local -LocalUrl "http://192.168.0.216:8000/version.json"
```

### Bước 2: Build và flash firmware mới

```powershell
idf.py build flash
```

### Bước 3: Khởi động HTTP server test (Terminal 2)

```powershell
.\test_ota_local.ps1 -TestVersion "2.0.4"
```

Server sẽ chạy tại: `http://192.168.0.216:8000/version.json`

### Bước 4: Monitor logs để xem OTA update (Terminal 1)

```powershell
idf.py -p COM31 monitor
```

## ✅ Kết quả mong đợi

Robot sẽ:
1. ✓ Kết nối WiFi
2. ✓ Check version từ `http://192.168.0.216:8000/version.json`
3. ✓ Phát hiện version mới (2.0.4 > 2.0.3)
4. ✓ Download firmware từ `http://192.168.0.216:8000/xiaozhi-2.0.4.bin`
5. ✓ Cài đặt và reboot
6. ✓ Sau reboot sẽ hiển thị version 2.0.4

## 📝 Logs mong đợi

```
I (xxxx) Ota: Current version: 2.0.3
I (xxxx) Ota: Using GitHub Pages OTA (hardcoded): http://192.168.0.216:8000/version.json
I (xxxx) Ota: New version available: 2.0.4
I (xxxx) Ota: Starting OTA update...
I (xxxx) Ota: Downloading firmware from: http://192.168.0.216:8000/xiaozhi-2.0.4.bin
I (xxxx) Ota: OTA update successful!
```

## 🔄 Quay lại GitHub Pages sau khi test xong

```powershell
.\switch_ota_url.ps1 -Mode github
idf.py build flash
```

## 🐛 Troubleshooting

### Lỗi kết nối:
- Kiểm tra firewall cho phép port 8000
- Kiểm tra robot và PC cùng WiFi subnet

### Lỗi 404:
- Kiểm tra HTTP server đang chạy
- Kiểm tra file `xiaozhi-2.0.4.bin` tồn tại trong `ota_test/`

### Không phát hiện version mới:
- Kiểm tra version trong `version.json` phải > 2.0.3
- Kiểm tra URL trong code đã đúng chưa
