#!/usr/bin/env pwsh
# Script to test OTA locally using Python HTTP server

param(
    [string]$TestVersion = "2.0.4",
    [int]$Port = 8000
)

Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "OTA Local Test Server" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan

# Create test directory
$TestDir = Join-Path $PSScriptRoot "ota_test"
if (Test-Path $TestDir) {
    Remove-Item -Recurse -Force $TestDir
}
New-Item -ItemType Directory -Path $TestDir | Out-Null

# Copy firmware from build directory
$BuildDir = Join-Path $PSScriptRoot "build"
Copy-Item "$BuildDir\xiaozhi.bin" "$TestDir\xiaozhi-$TestVersion.bin"
Write-Host "✓ Firmware copied to test directory" -ForegroundColor Green

# Calculate SHA256
$Hash = Get-FileHash "$TestDir\xiaozhi-$TestVersion.bin" -Algorithm SHA256
$Sha256 = $Hash.Hash.ToLower()
$FileSize = (Get-Item "$TestDir\xiaozhi-$TestVersion.bin").Length

Write-Host "✓ SHA256: $Sha256" -ForegroundColor Gray
Write-Host "✓ Size: $FileSize bytes" -ForegroundColor Gray

# Create version.json for testing
$VersionJson = @{
    version = $TestVersion
    firmware_url = "http://localhost:$Port/xiaozhi-$TestVersion.bin"
    size = $FileSize
    sha256 = $Sha256
    release_date = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
    changelog = @(
        "Test OTA update to version $TestVersion",
        "This is a local test"
    )
} | ConvertTo-Json -Depth 10

$VersionJson | Out-File -FilePath "$TestDir\version.json" -Encoding UTF8
Write-Host "✓ version.json created" -ForegroundColor Green

# Display version.json content
Write-Host "`nversion.json content:" -ForegroundColor Yellow
Write-Host $VersionJson -ForegroundColor Gray

Write-Host "`n=====================================" -ForegroundColor Cyan
Write-Host "Starting HTTP Server on port $Port..." -ForegroundColor Green
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "Test URL: http://localhost:$Port/version.json" -ForegroundColor Yellow
Write-Host "Firmware URL: http://localhost:$Port/xiaozhi-$TestVersion.bin" -ForegroundColor Yellow
Write-Host "`nPress Ctrl+C to stop the server" -ForegroundColor Gray
Write-Host "=====================================" -ForegroundColor Cyan

# Start Python HTTP server
Push-Location $TestDir
try {
    python -m http.server $Port
} finally {
    Pop-Location
}
