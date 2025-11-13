# UDP Drawing Test Guide

## 🚀 Quick Start

### 1. Build & Flash
```powershell
# Open ESP-IDF 5.5 PowerShell, then:
cd C:\Users\congh\Downloads\Compressed\xiaozhi-esp32-2.0.3otto2\xiaozhi-esp32-2.0.3
.\build_and_test.ps1
```

Or just double-click: `START_BUILD.bat`

### 2. Get Otto's IP Address
Monitor output will show:
```
I (5613) WifiStation: Got IP: 192.168.0.45
I (5613) OttoRobot: ✅ UDP Drawing Service started on port 12345
```

### 3. Enable Drawing Mode
Open browser: `http://192.168.0.45/drawing_mode?enable=1`

Or use web UI: `http://192.168.0.45/draw`

## 🎨 Test Methods

### Method 1: PowerShell UDP Test (Automated)
```powershell
# Quick test with default IP
.\test_udp_drawing.ps1

# Or specify IP
.\test_udp_drawing.ps1 -IP 192.168.0.45
```

This will draw:
- ✅ Horizontal line
- ✅ Vertical line  
- ✅ Box outline
- ✅ Erase test (gap in line)
- ✅ Random dots

### Method 2: Manual UDP Commands
```powershell
$ip = "192.168.0.45"
$client = New-Object System.Net.Sockets.UdpClient
$client.Connect($ip, 12345)

# Draw white pixel at (120, 140)
$client.Send([Text.Encoding]::ASCII.GetBytes("120,140,1"), 9)

# Draw more pixels
$client.Send([Text.Encoding]::ASCII.GetBytes("121,140,1"), 9)
$client.Send([Text.Encoding]::ASCII.GetBytes("122,140,1"), 9)

# Erase pixel at (121, 140)
$client.Send([Text.Encoding]::ASCII.GetBytes("121,140,0"), 9)

$client.Close()
```

### Method 3: Web API
```bash
# Enable drawing mode
curl http://192.168.0.45/drawing_mode?enable=1

# Draw pixel (white)
curl "http://192.168.0.45/drawing_pixel?x=120&y=140&state=1"

# Erase pixel (transparent)
curl "http://192.168.0.45/drawing_pixel?x=120&y=140&state=0"

# Clear canvas
curl http://192.168.0.45/drawing_clear

# Get statistics
curl http://192.168.0.45/drawing_status

# Disable drawing mode (return to normal UI)
curl http://192.168.0.45/drawing_mode?enable=0
```

### Method 4: Web Browser UI
1. Open: `http://192.168.0.45/draw`
2. Click "Enable Drawing"
3. Draw on canvas with mouse
4. Click "Clear" to erase
5. Click "Disable" to return to emoji display

### Method 5: Android App
1. Install **Draw_on_OLED** app from GitHub
   - https://github.com/BenchRobotics/Draw_on_OLED
2. Configure:
   - IP: `192.168.0.45`
   - Port: `12345`
3. Draw on phone screen
4. See real-time display on Otto LCD

## ✅ Expected Results

### Visual Check
- ✅ **White pixels visible** on Otto LCD
- ✅ **Otto emoji still visible** underneath transparent canvas
- ✅ **Can draw** (state=1) - white pixels appear
- ✅ **Can erase** (state=0) - pixels become transparent
- ✅ **Clear works** - all pixels erased, emoji visible
- ✅ **Disable mode** - canvas disappears, back to normal UI

### Performance Check
```json
// GET http://192.168.0.45/drawing_status
{
  "enabled": true,
  "running": true,
  "packets_received": 1523,
  "packets_processed": 1520,
  "pixels_drawn": 1450,
  "errors": 3
}
```

## 🐛 Troubleshooting

### Issue: No pixels visible
**Check:**
1. Drawing mode enabled? `curl http://[IP]/drawing_status`
2. Canvas initialized? Check serial monitor for: `✅ Canvas initialized: 240x280`
3. Coordinates valid? (0-239 for x, 0-279 for y)

**Fix:**
```bash
# Disable and re-enable
curl http://[IP]/drawing_mode?enable=0
curl http://[IP]/drawing_mode?enable=1
```

### Issue: Canvas blocks emoji
**Problem:** Using old RGB565 chroma key instead of ARGB8888 alpha

**Check serial log:**
```
✅ Canvas initialized: 240x280, buffer=268800 bytes (ARGB8888 transparent overlay)
```

If you see RGB565, rebuild with latest code.

### Issue: Pixels don't erase
**UDP Format:**
- Draw: `"x,y,1"` - white opaque pixel
- Erase: `"x,y,0"` - transparent pixel

**Example:**
```powershell
# Draw then erase
$client.Send([Text.Encoding]::ASCII.GetBytes("100,100,1"), 9)
Start-Sleep -Seconds 1
$client.Send([Text.Encoding]::ASCII.GetBytes("100,100,0"), 9)
```

### Issue: UDP packets not received
**Check network:**
```powershell
# Test UDP connectivity
Test-NetConnection -ComputerName 192.168.0.45 -Port 12345 -InformationLevel Detailed

# Or use telnet
telnet 192.168.0.45 12345
```

**Check firewall:**
- Allow UDP port 12345
- Otto and PC on same network

## 📊 Drawing Specifications

### Display
- **Size**: 240x280 pixels
- **Format**: ARGB8888 (32-bit with alpha)
- **Coordinate System**:
  - X: 0 (left) to 239 (right)
  - Y: 0 (top) to 279 (bottom)

### UDP Protocol
- **Port**: 12345
- **Format**: `"x,y,state"` (ASCII string)
- **Examples**:
  - `"0,0,1"` - top-left corner, white
  - `"239,279,1"` - bottom-right corner, white
  - `"120,140,0"` - center, erase (transparent)

### Colors
- **Draw (state=1)**: White (0xFFFFFF) with full opacity
- **Erase (state=0)**: Transparent (alpha=0)
- Background: Otto emoji/UI visible through transparency

### Performance
- **Buffer**: ~269KB PSRAM (240x280x4 bytes)
- **Max rate**: ~1000 pixels/second
- **Latency**: <10ms per pixel

## 🎯 Test Scenarios

### Scenario 1: Basic Drawing
1. Enable drawing mode
2. Send 10 pixels in a line
3. **Expected**: White line visible, emoji underneath
4. Clear canvas
5. **Expected**: Line disappears, emoji visible

### Scenario 2: Erase Test
1. Draw a filled box (100 pixels)
2. Erase center pixels
3. **Expected**: Hollow box, emoji visible in center

### Scenario 3: Mode Toggle
1. Enable drawing, draw something
2. Disable drawing mode
3. **Expected**: Canvas disappears, emoji visible
4. Re-enable drawing
5. **Expected**: Canvas reappears (empty), ready to draw

### Scenario 4: Persistence
1. Draw complex pattern
2. Don't clear
3. Disable then re-enable drawing mode
4. **Expected**: Canvas cleared on disable (new instance)

### Scenario 5: Android App
1. Connect Android app
2. Draw on phone in real-time
3. **Expected**: Drawings appear instantly on Otto LCD
4. Emoji still visible underneath

## 📝 Technical Details

### Architecture
```
UdpDrawService (port 12345)
    ↓ receives UDP packets
    ↓ parses "x,y,state"
    ↓
DrawingDisplay (LVGL canvas)
    ↓ ARGB8888 buffer in PSRAM
    ↓ lv_canvas_set_px() with alpha
    ↓
LcdDisplay (ST7789)
    ↓ composite: emoji + canvas overlay
    ↓
LCD Panel (240x280)
```

### Memory Usage
- **Canvas Buffer**: 268,800 bytes (240×280×4) in PSRAM
- **LVGL Object**: ~200 bytes
- **UDP Buffer**: 512 bytes
- **Total**: ~270 KB

### Code Changes from Original
1. **RGB565 → ARGB8888**: Proper alpha channel support
2. **Chroma Key → Alpha Blending**: Smooth transparency
3. **Canvas Lifecycle**: Enable/disable properly cleans up
4. **Invalidation**: Pixel updates trigger immediate redraw
5. **Web Integration**: Full API for remote control

## 🔗 Resources

- **Original Project**: https://github.com/BenchRobotics/Draw_on_OLED
- **LVGL Canvas Docs**: https://docs.lvgl.io/master/widgets/canvas.html
- **ESP32 UDP**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_netif.html
