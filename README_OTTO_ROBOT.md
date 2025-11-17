# 🤖 Xiaozhi ESP32 Otto Robot - Enhanced Version

An enhanced ESP32-S3 firmware for Otto-style quadruped robot with improved movements, web controller, and Model Context Protocol (MCP) server integration.

## ✨ Key Features

### 🐾 Enhanced Robot Movements
- **Softer Walking Motion**: Reduced angles from 30°/150° to 35°/145° for gentler movement
- **Sitting Wave Action**: Wave hand gesture performed in proper sitting posture
- **23 MCP Robot Tools**: Complete set of dog-style movements and actions

### 🌐 Web Controller
- Built-in WiFi Access Point: `Xiaozhi-ECF9`
- Real-time robot control via web interface
- Emoji display system with happy expressions

### 🔧 Technical Specifications
- **Platform**: ESP32-S3 (QFN56) with 16MB Flash, 8MB PSRAM
- **Servo Configuration**: 4-servo quadruped setup
- **GPIO Mapping**: ESP32-S3 compatible pin assignments
- **Flash Usage**: Optimized with 11% free space (3.6MB used)

## 🎯 Recent Improvements (Otto Version)

### Movement Enhancements
- ✅ **Gentler Walking**: 5° angle reduction for smoother motion
- ✅ **Proper Sitting Wave**: LF=90° standing, RF waves, LB/RB=30° sitting
- ✅ **Code Optimization**: Removed news functionality, saved ~164KB

### Hardware Compatibility
- ✅ **GPIO Fix**: Corrected pin assignments for ESP32-S3
- ✅ **Servo Reliability**: All 4 servos initialize successfully
- ✅ **Display Support**: 240x240 ST7789 LCD with LVGL

## 🚀 Quick Start

### Prerequisites
- ESP-IDF v5.5
- ESP32-S3 development board
- 4x SG90 servo motors
- 240x240 ST7789 display

### Build & Flash
```bash
# Setup ESP-IDF environment
. $IDF_PATH/export.sh

# Build firmware
idf.py -B build_otto build

# Create merged binary
esptool.py --chip esp32s3 merge_bin -o build_otto/xiaozhi-merged.bin \\
  --flash_mode dio --flash_freq 80m --flash_size 16MB \\
  0x0 build_otto/bootloader/bootloader.bin \\
  0x8000 build_otto/partition_table/partition-table.bin \\
  0xd000 build_otto/ota_data_initial.bin \\
  0x20000 build_otto/xiaozhi.bin \\
  0x800000 build_otto/generated_assets.bin

# Flash to ESP32-S3
esptool.py --chip esp32s3 -p COM24 -b 460800 write_flash 0x0 build_otto/xiaozhi-merged.bin
```

### Configuration
Edit `main/boards/otto-robot/config.h` for your hardware setup:
```cpp
#define DOG_LEFT_FRONT_PIN GPIO_NUM_17   // Left Front leg
#define DOG_RIGHT_FRONT_PIN GPIO_NUM_18  // Right Front leg  
#define DOG_LEFT_BACK_PIN GPIO_NUM_12    // Left Back leg
#define DOG_RIGHT_BACK_PIN GPIO_NUM_38   // Right Back leg
```

## 🎮 Usage

### WiFi Connection
1. Power on the robot
2. Connect to WiFi: `Xiaozhi-ECF9`
3. Open browser: `http://192.168.4.1`
4. Control robot via web interface

### Available Commands
- **Walking**: "đi tiến", "đi lùi"
- **Turning**: "quay trái", "quay phải"  
- **Actions**: "vẫy tay", "ngồi", "nhảy"
- **Emotions**: Various emoji expressions

## 📁 Project Structure

```
├── main/
│   ├── boards/otto-robot/          # Otto robot board configuration
│   │   ├── config.h                # GPIO pin definitions
│   │   ├── otto_controller.cc      # MCP server & robot control
│   │   ├── otto_movements.cc       # Movement functions
│   │   └── otto_webserver.cc       # Web controller
│   ├── display/                    # Display drivers
│   ├── audio/                      # Audio processing
│   └── protocols/                  # Communication protocols
├── docs/                           # Documentation
└── scripts/                        # Build utilities
```

## 🔨 Development

### Movement Customization
Edit `otto_movements.cc` to modify:
- Walking angles and speeds
- Custom gesture sequences
- Servo calibration values

### Web Interface
Customize `otto_webserver.cc` for:
- Additional control buttons
- Real-time sensor data
- Custom styling

### MCP Tools
Add new robot capabilities in `otto_controller.cc`:
- Custom movement patterns
- Sensor integrations
- External API connections

## 🔒 OTA Security Configuration

### Default Behavior (v2.0.8+)
Firmware no longer hardcodes GitHub OTA URLs. Instead, uses a priority system:

1. **NVS Settings** (Highest Priority) - Custom URL stored in device
2. **Kconfig** - Compile-time configuration
3. **Default** - Fallback to tenclass API

### Setting Custom OTA URL

**Option 1: Quick Script (Recommended)**
```bash
# Windows
.\set_ota_url.ps1 -Url "https://your-server.com/ota/version.json"

# Linux/Mac
chmod +x set_ota_url.sh
./set_ota_url.sh https://your-server.com/ota/version.json
```

**Option 2: Via Serial Monitor**
```bash
idf.py -p COM31 monitor

# In monitor console:
settings set ota_url "https://your-github.io/repo/version.json"
settings save
```

**Option 3: Compile-Time Config**
```bash
idf.py menuconfig
# Navigate: Xiaozhi Assistant → Default OTA URL
# Change URL and save
```

### Verify Current URL
Check device logs for:
```
📡 Using custom OTA URL from NVS: https://...
⚙️ Using OTA URL from config: https://...
🔧 Using default OTA URL: https://...
```

📚 **Full Security Guide**: [docs/SECURE_OTA_SETUP.md](docs/SECURE_OTA_SETUP.md)

## 📊 Performance

- **Boot Time**: ~2 seconds to WiFi ready
- **Response Time**: <100ms for movement commands
- **Battery Life**: ~2 hours continuous operation
- **Range**: 100m WiFi coverage

## 🛠️ Troubleshooting

### Common Issues
- **Servo jitter**: Check power supply capacity
- **WiFi connection**: Verify antenna connection
- **Build errors**: Ensure ESP-IDF v5.5 compatibility

### GPIO Conflicts
If you see "GPIO is not usable" warnings:
- Check pin assignments in `config.h`
- Avoid pins used by display/audio
- Use multimeter to verify connections

## 📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

## 🤝 Contributing

1. Fork the repository
2. Create feature branch (`git checkout -b feature/amazing-movement`)
3. Commit changes (`git commit -m 'Add amazing movement'`)
4. Push to branch (`git push origin feature/amazing-movement`)
5. Open Pull Request

## 🙏 Acknowledgments

- Original Xiaozhi ESP32 project
- ESP-IDF framework team
- Otto robot community
- MCP protocol developers

---

**Happy Robot Building! 🤖✨**