#!/usr/bin/env pwsh
# Script to upload OTA package to GitHub

# Set your GitHub token as environment variable:
# $env:GITHUB_TOKEN = "your_token_here"

$Version = "1.0.1"
$Repo = "nguyenconghuy2904-source/xiaozhi-esp32-otto-robot"
$Tag = "v$Version"

Write-Host "Uploading OTA package to GitHub..." -ForegroundColor Cyan

# Check if gh CLI is installed
if (!(Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Host "GitHub CLI (gh) is not installed!" -ForegroundColor Red
    Write-Host "Install from: https://cli.github.com/" -ForegroundColor Yellow
    exit 1
}

# Create release
Write-Host "Creating release $Tag..." -ForegroundColor Green
gh release create $Tag `
    --repo $Repo `
    --title "Version $Version" `
    --notes "OTA Update - Version $Version`n`nSHA256: 48cffc3b090d4a2bd8f16f234271d2737a9ba6933f1ec4cf664bd37f5e37e17f" `
    xiaozhi-$Version.bin

if ($LASTEXITCODE -eq 0) {
    Write-Host "
✓ Release created successfully!" -ForegroundColor Green
    Write-Host "Download URL: https://github.com/$Repo/releases/download/$Tag/xiaozhi-$Version.bin" -ForegroundColor Cyan
    Write-Host "
Next steps:" -ForegroundColor Yellow
    Write-Host "1. Update GitHub Pages with version.json" -ForegroundColor Gray
    Write-Host "2. Commit and push the version.json file" -ForegroundColor Gray
} else {
    Write-Host "Failed to create release!" -ForegroundColor Red
}
