#!/usr/bin/env pwsh
# Script to build firmware and create OTA package with version.json

param(
    [string]$Version = "1.0.0",
    [string]$OutputDir = "ota_package"
)

Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "Building OTA Package" -ForegroundColor Cyan
Write-Host "Version: $Version" -ForegroundColor Yellow
Write-Host "=====================================" -ForegroundColor Cyan

# Step 1: Activate ESP-IDF environment
Write-Host "`n[1/5] Activating ESP-IDF environment..." -ForegroundColor Green
. C:\Espressif\frameworks\esp-idf-v5.5\export.ps1

# Step 2: Build firmware
Write-Host "`n[2/5] Building firmware..." -ForegroundColor Green
idf.py build
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

# Step 3: Create output directory
Write-Host "`n[3/5] Creating OTA package directory..." -ForegroundColor Green
$OtaDir = Join-Path $PSScriptRoot $OutputDir
if (Test-Path $OtaDir) {
    Remove-Item -Recurse -Force $OtaDir
}
New-Item -ItemType Directory -Path $OtaDir | Out-Null

# Step 4: Copy firmware files
Write-Host "`n[4/5] Copying firmware files..." -ForegroundColor Green
$BuildDir = Join-Path $PSScriptRoot "build"

# Copy main firmware binary
Copy-Item "$BuildDir\xiaozhi.bin" "$OtaDir\xiaozhi-$Version.bin"
Write-Host "  ✓ Copied xiaozhi.bin -> xiaozhi-$Version.bin" -ForegroundColor Gray

# Copy all necessary files for full flash
Copy-Item "$BuildDir\bootloader\bootloader.bin" "$OtaDir\"
Copy-Item "$BuildDir\partition_table\partition-table.bin" "$OtaDir\"
Copy-Item "$BuildDir\ota_data_initial.bin" "$OtaDir\"
Copy-Item "$BuildDir\generated_assets.bin" "$OtaDir\"
Write-Host "  ✓ Copied bootloader and partition files" -ForegroundColor Gray

# Step 5: Create version.json for GitHub Pages OTA
Write-Host "`n[5/5] Creating version.json..." -ForegroundColor Green

# Get file size
$FirmwareFile = Get-Item "$OtaDir\xiaozhi-$Version.bin"
$FileSize = $FirmwareFile.Length

# Calculate SHA256 hash
$Hash = Get-FileHash "$OtaDir\xiaozhi-$Version.bin" -Algorithm SHA256
$Sha256 = $Hash.Hash.ToLower()

# Create version.json with GitHub Pages format
$VersionJson = @{
    version = $Version
    firmware_url = "https://github.com/nguyenconghuy2904-source/xiaozhi-esp32-otto-robot/releases/download/v$Version/xiaozhi-$Version.bin"
    size = $FileSize
    sha256 = $Sha256
    release_date = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
    changelog = @(
        "Update to version $Version",
        "Bug fixes and improvements"
    )
} | ConvertTo-Json -Depth 10

$VersionJson | Out-File -FilePath "$OtaDir\version.json" -Encoding UTF8
Write-Host "  ✓ Created version.json" -ForegroundColor Gray

# Create README with instructions
$ReadmeContent = @"
# OTA Package for Version $Version

## Files Included:
- **xiaozhi-$Version.bin** - Main firmware for OTA updates (size: $FileSize bytes)
- **version.json** - Version metadata for OTA system
- **bootloader.bin** - Bootloader (for full flash only)
- **partition-table.bin** - Partition table (for full flash only)
- **ota_data_initial.bin** - OTA data (for full flash only)
- **generated_assets.bin** - Assets (for full flash only)

## SHA256 Hash:
``````
$Sha256
``````

## How to Deploy OTA:

### Method 1: GitHub Releases (Recommended)
1. Create a new release on GitHub with tag: v$Version
2. Upload **xiaozhi-$Version.bin** to the release
3. Update your GitHub Pages with the **version.json** file
4. Device will automatically detect and download the update

### Method 2: Manual Flash via USB
``````powershell
python -m esptool --chip esp32s3 -p COM31 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0xd000 ota_data_initial.bin 0x20000 xiaozhi-$Version.bin 0x800000 generated_assets.bin
``````

## version.json Content:
``````json
$VersionJson
``````

---
Generated on: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
"@

$ReadmeContent | Out-File -FilePath "$OtaDir\README.md" -Encoding UTF8

# Create GitHub upload script
$GithubScript = @"
#!/usr/bin/env pwsh
# Script to upload OTA package to GitHub

# Set your GitHub token as environment variable:
# `$env:GITHUB_TOKEN = "your_token_here"

`$Version = "$Version"
`$Repo = "nguyenconghuy2904-source/xiaozhi-esp32-otto-robot"
`$Tag = "v`$Version"

Write-Host "Uploading OTA package to GitHub..." -ForegroundColor Cyan

# Check if gh CLI is installed
if (!(Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Host "GitHub CLI (gh) is not installed!" -ForegroundColor Red
    Write-Host "Install from: https://cli.github.com/" -ForegroundColor Yellow
    exit 1
}

# Create release
Write-Host "Creating release `$Tag..." -ForegroundColor Green
gh release create `$Tag ``
    --repo `$Repo ``
    --title "Version `$Version" ``
    --notes "OTA Update - Version `$Version``n``nSHA256: $Sha256" ``
    xiaozhi-`$Version.bin

if (`$LASTEXITCODE -eq 0) {
    Write-Host "`n✓ Release created successfully!" -ForegroundColor Green
    Write-Host "Download URL: https://github.com/`$Repo/releases/download/`$Tag/xiaozhi-`$Version.bin" -ForegroundColor Cyan
    Write-Host "`nNext steps:" -ForegroundColor Yellow
    Write-Host "1. Update GitHub Pages with version.json" -ForegroundColor Gray
    Write-Host "2. Commit and push the version.json file" -ForegroundColor Gray
} else {
    Write-Host "Failed to create release!" -ForegroundColor Red
}
"@

$GithubScript | Out-File -FilePath "$OtaDir\upload_to_github.ps1" -Encoding UTF8

# Summary
Write-Host "`n=====================================" -ForegroundColor Cyan
Write-Host "OTA Package Created Successfully!" -ForegroundColor Green
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "Output directory: $OtaDir" -ForegroundColor Yellow
Write-Host "Firmware file: xiaozhi-$Version.bin" -ForegroundColor Yellow
Write-Host "File size: $FileSize bytes" -ForegroundColor Yellow
Write-Host "SHA256: $Sha256" -ForegroundColor Yellow
Write-Host "`nNext steps:" -ForegroundColor Cyan
Write-Host "1. Review files in: $OtaDir" -ForegroundColor Gray
Write-Host "2. Run upload_to_github.ps1 to create GitHub release" -ForegroundColor Gray
Write-Host "3. Update GitHub Pages with version.json" -ForegroundColor Gray
Write-Host "4. Device will auto-update from: https://conghuy93.github.io/otanew/version.json" -ForegroundColor Gray
Write-Host "`n=====================================" -ForegroundColor Cyan
