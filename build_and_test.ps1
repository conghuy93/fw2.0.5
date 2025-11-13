# Build and Test Otto UDP Drawing
# Run this from ESP-IDF PowerShell or CMD

Write-Host "🔨 Building firmware..." -ForegroundColor Cyan
idf.py -B build_otto -D BOARD_TYPE=otto-robot build

if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Build successful!" -ForegroundColor Green
    Write-Host ""
    Write-Host "🔥 Flashing to COM31..." -ForegroundColor Yellow
    idf.py -B build_otto -p COM31 flash
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✅ Flash successful!" -ForegroundColor Green
        Write-Host ""
        Write-Host "📡 Waiting for device to boot..." -ForegroundColor Cyan
        Start-Sleep -Seconds 5
        
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Magenta
        Write-Host "  UDP DRAWING TEST INSTRUCTIONS" -ForegroundColor Magenta
        Write-Host "========================================" -ForegroundColor Magenta
        Write-Host ""
        Write-Host "Monitor output will show IP address like:" -ForegroundColor White
        Write-Host "  I (5613) WifiStation: Got IP: 192.168.0.45" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "Test Methods:" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "1️⃣  Web Browser:" -ForegroundColor Green
        Write-Host "   - Open: http://[IP]/draw" -ForegroundColor White
        Write-Host "   - Enable drawing mode" -ForegroundColor White
        Write-Host "   - Draw on canvas" -ForegroundColor White
        Write-Host ""
        Write-Host "2️⃣  PowerShell UDP Test:" -ForegroundColor Green
        Write-Host "   Run these commands (replace IP):" -ForegroundColor White
        Write-Host '   $ip = "192.168.0.45"' -ForegroundColor Yellow
        Write-Host '   $client = New-Object System.Net.Sockets.UdpClient' -ForegroundColor Yellow
        Write-Host '   $client.Connect($ip, 12345)' -ForegroundColor Yellow
        Write-Host '   # Draw white pixels' -ForegroundColor Yellow
        Write-Host '   $client.Send([Text.Encoding]::ASCII.GetBytes("120,140,1"), 9)' -ForegroundColor Yellow
        Write-Host '   $client.Send([Text.Encoding]::ASCII.GetBytes("121,140,1"), 9)' -ForegroundColor Yellow
        Write-Host '   $client.Send([Text.Encoding]::ASCII.GetBytes("122,140,1"), 9)' -ForegroundColor Yellow
        Write-Host '   # Erase pixel' -ForegroundColor Yellow
        Write-Host '   $client.Send([Text.Encoding]::ASCII.GetBytes("121,140,0"), 9)' -ForegroundColor Yellow
        Write-Host '   $client.Close()' -ForegroundColor Yellow
        Write-Host ""
        Write-Host "3️⃣  Web API:" -ForegroundColor Green
        Write-Host "   - Enable: http://[IP]/drawing_mode?enable=1" -ForegroundColor White
        Write-Host "   - Draw:   http://[IP]/drawing_pixel?x=120&y=140&state=1" -ForegroundColor White
        Write-Host "   - Erase:  http://[IP]/drawing_pixel?x=120&y=140&state=0" -ForegroundColor White
        Write-Host "   - Clear:  http://[IP]/drawing_clear" -ForegroundColor White
        Write-Host "   - Status: http://[IP]/drawing_status" -ForegroundColor White
        Write-Host "   - Disable: http://[IP]/drawing_mode?enable=0" -ForegroundColor White
        Write-Host ""
        Write-Host "Expected Results:" -ForegroundColor Cyan
        Write-Host "  ✅ White pixels appear on transparent canvas" -ForegroundColor White
        Write-Host "  ✅ Otto emoji visible underneath canvas" -ForegroundColor White
        Write-Host "  ✅ Can draw (state=1) and erase (state=0)" -ForegroundColor White
        Write-Host "  ✅ Disable drawing mode returns to normal UI" -ForegroundColor White
        Write-Host ""
        Write-Host "========================================" -ForegroundColor Magenta
        Write-Host ""
        Write-Host "Press Ctrl+] to exit monitor" -ForegroundColor Yellow
        Write-Host ""
        
        # Start monitor
        idf.py -B build_otto -p COM31 monitor
    } else {
        Write-Host "❌ Flash failed!" -ForegroundColor Red
    }
} else {
    Write-Host "❌ Build failed!" -ForegroundColor Red
}
