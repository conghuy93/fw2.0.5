#!/usr/bin/env pwsh
# Script to switch OTA URL between local test and GitHub Pages

param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("local", "github")]
    [string]$Mode,
    
    [string]$LocalUrl = "http://192.168.0.45:8000/version.json"
)

$OtaFile = "main\ota.cc"

Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "OTA URL Switcher" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan

if ($Mode -eq "local") {
    Write-Host "Switching to LOCAL test mode..." -ForegroundColor Yellow
    Write-Host "Local URL: $LocalUrl" -ForegroundColor Gray
    
    # Read the file
    $content = Get-Content $OtaFile -Raw
    
    # Replace GitHub URL with local URL
    $newContent = $content -replace 'https://conghuy93\.github\.io/otanew/version\.json', $LocalUrl
    
    # Save the file
    $newContent | Out-File -FilePath $OtaFile -Encoding UTF8 -NoNewline
    
    Write-Host "✓ OTA URL switched to: $LocalUrl" -ForegroundColor Green
    Write-Host "`nNext steps:" -ForegroundColor Cyan
    Write-Host "1. Run: .\test_ota_local.ps1" -ForegroundColor Gray
    Write-Host "2. Build and flash firmware: idf.py build flash" -ForegroundColor Gray
    Write-Host "3. Monitor logs: idf.py monitor" -ForegroundColor Gray
    
} elseif ($Mode -eq "github") {
    Write-Host "Switching to GITHUB Pages mode..." -ForegroundColor Yellow
    
    # Read the file
    $content = Get-Content $OtaFile -Raw
    
    # Replace local URL back to GitHub URL
    $newContent = $content -replace 'http://[^"]+/version\.json', 'https://conghuy93.github.io/otanew/version.json'
    
    # Save the file
    $newContent | Out-File -FilePath $OtaFile -Encoding UTF8 -NoNewline
    
    Write-Host "✓ OTA URL switched back to GitHub Pages" -ForegroundColor Green
    Write-Host "`nNext steps:" -ForegroundColor Cyan
    Write-Host "1. Build and flash firmware: idf.py build flash" -ForegroundColor Gray
}

Write-Host "=====================================" -ForegroundColor Cyan
