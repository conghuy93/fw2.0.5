# Quick Build Script for OTA
# Run this from ESP-IDF PowerShell

Write-Host "🔨 Building firmware for GitHub Pages OTA..." -ForegroundColor Cyan
Write-Host ""

# Build firmware
idf.py -B build_otto -D BOARD_TYPE=otto-robot build

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "✅ Build successful!" -ForegroundColor Green
    Write-Host ""
    
    # Create merged binary for OTA
    Write-Host "📦 Creating merged binary..." -ForegroundColor Yellow
    
    $buildDir = "build_otto"
    $projectName = (Get-Content CMakeLists.txt | Select-String 'project\((.*?)\)' | ForEach-Object { $_.Matches.Groups[1].Value }).Trim()
    
    if ([string]::IsNullOrEmpty($projectName)) {
        $projectName = "xiaozhi-esp32"
    }
    
    $binFile = "$buildDir\$projectName.bin"
    $mergedBin = "docs\firmware.bin"
    
    if (Test-Path $binFile) {
        # Copy to docs for GitHub Pages
        Copy-Item $binFile $mergedBin -Force
        
        $size = (Get-Item $mergedBin).Length
        $sizeMB = [math]::Round($size / 1MB, 2)
        
        Write-Host ""
        Write-Host "✅ Merged binary created: $mergedBin" -ForegroundColor Green
        Write-Host "   Size: $sizeMB MB ($size bytes)" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "📤 Ready to upload to GitHub:" -ForegroundColor Yellow
        Write-Host "   1. Update version in docs/version.json" -ForegroundColor White
        Write-Host "   2. git add docs/" -ForegroundColor White
        Write-Host '   3. git commit -m "Update firmware vX.X.X"' -ForegroundColor White
        Write-Host "   4. git push origin main" -ForegroundColor White
        Write-Host "   5. Enable GitHub Pages (Settings > Pages > /docs folder)" -ForegroundColor White
        Write-Host ""
        Write-Host "🌐 Your OTA URL will be:" -ForegroundColor Cyan
        Write-Host "   https://nguyenconghuy2904-source.github.io/xiaozhi-esp32-otto-robot/version.json" -ForegroundColor White
        Write-Host ""
    } else {
        Write-Host "❌ Binary not found: $binFile" -ForegroundColor Red
        Write-Host "   Available files:" -ForegroundColor Yellow
        Get-ChildItem $buildDir\*.bin | Select-Object Name, Length
    }
} else {
    Write-Host ""
    Write-Host "❌ Build failed!" -ForegroundColor Red
}
