# OTA Update với GitHub Pages

Hướng dẫn cập nhật firmware ESP32 qua GitHub Pages (miễn phí).

## 📋 Tính năng

- ✅ Host firmware trên GitHub Pages (miễn phí, không giới hạn)
- ✅ Tự động kiểm tra version mới qua file `version.json`
- ✅ Tải và cập nhật firmware qua WiFi
- ✅ Tương thích với cả server OTA hiện tại và GitHub Pages
- ✅ Không cần authentication (phù hợp cho open source projects)

## 🚀 Quick Start

### 1️⃣ Tạo GitHub Repository và bật Pages

```bash
# Tạo thư mục docs/ cho GitHub Pages
mkdir -p docs

# Commit và push
git add docs/
git commit -m "Setup GitHub Pages for OTA"
git push origin main
```

**Bật GitHub Pages:**
- Vào repository → **Settings** → **Pages**
- Source: **Deploy from a branch**
- Branch: **main**, Folder: **/docs**
- Click **Save**

### 2️⃣ Tạo file version.json

Tạo file `docs/version.json` với nội dung:

```json
{
  "version": "1.0.1",
  "firmware_url": "https://<username>.github.io/<repo-name>/firmware.bin"
}
```

**Ví dụ:**
```json
{
  "version": "1.0.1",
  "firmware_url": "https://nguyenconghuy2904-source.github.io/xiaozhi-esp32-otto-robot/firmware.bin"
}
```

### 3️⃣ Upload firmware binary

```bash
# Copy firmware binary vào docs/
cp build/xiaozhi-esp32.bin docs/firmware.bin

# Commit và push
git add docs/firmware.bin docs/version.json
git commit -m "Release firmware v1.0.1"
git push origin main
```

### 4️⃣ Cấu hình ESP32

**Cách 1: Qua WiFi Config Portal**
- Kết nối WiFi của ESP32
- Vào trang cấu hình
- Nhập OTA URL: `https://<username>.github.io/<repo-name>/version.json`

**Cách 2: Code trực tiếp trong `sdkconfig.defaults`**
```ini
CONFIG_OTA_URL="https://nguyenconghuy2904-source.github.io/xiaozhi-esp32-otto-robot/version.json"
```

**Cách 3: Qua Settings API (runtime)**
```cpp
Settings settings("wifi", true);
settings.SetString("ota_url", "https://nguyenconghuy2904-source.github.io/xiaozhi-esp32-otto-robot/version.json");
```

## 📝 Format JSON

### GitHub Pages Format (Đơn giản)
```json
{
  "version": "1.0.1",
  "firmware_url": "https://username.github.io/repo/firmware.bin"
}
```

### Server Format (Phức tạp - backward compatible)
```json
{
  "firmware": {
    "version": "1.0.1",
    "url": "https://server.com/firmware.bin",
    "force": 0
  },
  "activation": {
    "code": "...",
    "challenge": "..."
  },
  "mqtt": {...},
  "websocket": {...}
}
```

Code tự động phát hiện format nào đang được sử dụng.

## 🔄 Quy trình update

1. **Build firmware mới**
   ```bash
   idf.py build
   ```

2. **Tăng version** trong code (file `version.txt` hoặc `CMakeLists.txt`)

3. **Copy firmware binary**
   ```bash
   cp build/xiaozhi-esp32.bin docs/firmware.bin
   ```

4. **Update version.json**
   ```json
   {
     "version": "1.0.2",
     "firmware_url": "https://..."
   }
   ```

5. **Push lên GitHub**
   ```bash
   git add docs/
   git commit -m "Release v1.0.2"
   git push origin main
   ```

6. **ESP32 tự động update** (kiểm tra mỗi 5 phút hoặc khi reboot)

## 🔍 Kiểm tra log

Mở Serial Monitor để xem quá trình OTA:

```
I (12345) Ota: Current version: 1.0.0
I (12350) Ota: Using GitHub Pages OTA format
I (12355) Ota: New version available: 1.0.1
I (12360) Ota: Upgrading firmware from https://...
I (12365) Ota: Progress: 10% (102400/1024000), Speed: 51200B/s
I (12370) Ota: Progress: 20% (204800/1024000), Speed: 51200B/s
...
I (12400) Ota: Firmware upgrade successful
I (12405) Ota: Restarting...
```

## ⚙️ Cấu hình nâng cao

### Tự động build và deploy với GitHub Actions

Tạo file `.github/workflows/build-and-deploy.yml`:

```yaml
name: Build and Deploy OTA

on:
  push:
    branches: [ main ]
    paths:
      - 'main/**'
      - 'CMakeLists.txt'

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup ESP-IDF
        uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.5
          target: esp32s3
      
      - name: Build firmware
        run: |
          idf.py build
          cp build/xiaozhi-esp32.bin docs/firmware.bin
      
      - name: Get version
        id: version
        run: echo "VERSION=$(cat version.txt)" >> $GITHUB_OUTPUT
      
      - name: Update version.json
        run: |
          echo "{\"version\":\"${{ steps.version.outputs.VERSION }}\",\"firmware_url\":\"https://${{ github.repository_owner }}.github.io/${{ github.event.repository.name }}/firmware.bin\"}" > docs/version.json
      
      - name: Deploy to GitHub Pages
        uses: peaceiris/actions-gh-pages@v3
        with:
          github_token: ${{ secrets.GITHUB_TOKEN }}
          publish_dir: ./docs
```

### Version management

Tạo file `version.txt` trong root:
```
1.0.1
```

Update trong `CMakeLists.txt`:
```cmake
file(READ "${CMAKE_SOURCE_DIR}/version.txt" PROJECT_VER)
string(STRIP "${PROJECT_VER}" PROJECT_VER)
project(xiaozhi-esp32 VERSION ${PROJECT_VER})
```

## 🔒 Bảo mật

**Lưu ý:** GitHub Pages là **public**, tất cả đều có thể tải firmware.

**Giải pháp nếu cần bảo mật:**
1. Sử dụng private server với authentication (giữ code hiện tại)
2. Mã hóa firmware binary
3. Sử dụng GitHub Releases với token authentication
4. Hybrid: GitHub Pages cho public releases, private server cho beta/internal

## ⚠️ Giới hạn

- **File size**: GitHub Pages không giới hạn size file
- **Bandwidth**: 100GB/tháng (soft limit)
- **Build time**: 1-2 phút để GitHub Pages update sau khi push
- **HTTPS only**: GitHub Pages chỉ hỗ trợ HTTPS (secure)

## 🐛 Troubleshooting

### Lỗi 404 Not Found
- Chờ 1-2 phút sau khi push để GitHub Pages build
- Kiểm tra URL đúng: `https://<username>.github.io/<repo-name>/version.json`
- Đảm bảo file nằm trong thư mục `docs/`

### Không tải được firmware
- Kiểm tra `firmware_url` trong `version.json` đúng
- Kiểm tra file `docs/firmware.bin` tồn tại trên GitHub
- Kiểm tra ESP32 có đủ free heap (ít nhất 200KB)

### Version không update
- So sánh version string theo semantic versioning (1.0.1 > 1.0.0)
- Kiểm tra ESP32 đã kết nối WiFi
- Xem log Serial Monitor để debug

## 📚 Tham khảo

- [GitHub Pages Documentation](https://docs.github.com/en/pages)
- [ESP-IDF OTA Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html)
- [Semantic Versioning](https://semver.org/)

## 📄 License

MIT License

---

**Tác giả:** nguyenconghuy2904-source  
**Repository:** https://github.com/nguyenconghuy2904-source/xiaozhi-esp32-otto-robot
