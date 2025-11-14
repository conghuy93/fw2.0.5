# OTA Package for Version 1.0.1

## Files Included:
- **xiaozhi-1.0.1.bin** - Main firmware for OTA updates (size: 3847712 bytes)
- **version.json** - Version metadata for OTA system
- **bootloader.bin** - Bootloader (for full flash only)
- **partition-table.bin** - Partition table (for full flash only)
- **ota_data_initial.bin** - OTA data (for full flash only)
- **generated_assets.bin** - Assets (for full flash only)

## SHA256 Hash:
```
48cffc3b090d4a2bd8f16f234271d2737a9ba6933f1ec4cf664bd37f5e37e17f
```

## How to Deploy OTA:

### Method 1: GitHub Releases (Recommended)
1. Create a new release on GitHub with tag: v1.0.1
2. Upload **xiaozhi-1.0.1.bin** to the release
3. Update your GitHub Pages with the **version.json** file
4. Device will automatically detect and download the update

### Method 2: Manual Flash via USB
```powershell
python -m esptool --chip esp32s3 -p COM31 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0xd000 ota_data_initial.bin 0x20000 xiaozhi-1.0.1.bin 0x800000 generated_assets.bin
```

## version.json Content:
```json
{
    "firmware_url":  "https://github.com/nguyenconghuy2904-source/xiaozhi-esp32-otto-robot/releases/download/v1.0.1/xiaozhi-1.0.1.bin",
    "version":  "1.0.1",
    "sha256":  "48cffc3b090d4a2bd8f16f234271d2737a9ba6933f1ec4cf664bd37f5e37e17f",
    "release_date":  "2025-11-14T06:01:28Z",
    "changelog":  [
                      "Update to version 1.0.1",
                      "Bug fixes and improvements"
                  ],
    "size":  3847712
}
```

---
Generated on: 2025-11-14 06:01:28
