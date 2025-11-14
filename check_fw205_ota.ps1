# Đợi GitHub Pages kích hoạt
Write-Host ' Đang đợi GitHub Pages kích hoạt (30 giây)...' -ForegroundColor Yellow
Start-Sleep -Seconds 30

# Kiểm tra version.json
Write-Host '
 Kiểm tra version.json...' -ForegroundColor Cyan
try {
    $version = Invoke-WebRequest -Uri 'https://conghuy93.github.io/fw2.0.5/version.json' -UseBasicParsing | Select-Object -ExpandProperty Content
    Write-Host $version -ForegroundColor Green
    $versionObj = $version | ConvertFrom-Json
    Write-Host '
 Version:' $versionObj.version -ForegroundColor Green
    Write-Host ' Firmware URL:' $versionObj.firmware_url -ForegroundColor Green
} catch {
    Write-Host ' Không thể truy cập version.json. Vui lòng kiểm tra GitHub Pages settings!' -ForegroundColor Red
    exit 1
}

# Kiểm tra firmware.bin
Write-Host '
� Kiểm tra firmware.bin...' -ForegroundColor Cyan
try {
    $response = Invoke-WebRequest -Uri 'https://conghuy93.github.io/fw2.0.5/firmware.bin' -Method Head -UseBasicParsing
    Write-Host ' Firmware.bin accessible: HTTP' $response.StatusCode -ForegroundColor Green
    Write-Host ' Size:' ($response.Headers.'Content-Length' / 1MB).ToString('N2') 'MB' -ForegroundColor Green
} catch {
    Write-Host ' Không thể truy cập firmware.bin!' -ForegroundColor Red
    exit 1
}

Write-Host '
 Tất cả files đã sẵn sàng! ESP32 sẽ tự động update khi khởi động lại.' -ForegroundColor Green
Write-Host ' URL kiểm tra: https://conghuy93.github.io/fw2.0.5/version.json' -ForegroundColor Cyan
