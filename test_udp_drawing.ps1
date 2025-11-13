# Quick UDP Drawing Test Script
# Usage: Set $IP to your Otto's IP address and run this script

param(
    [Parameter(Mandatory=$false)]
    [string]$IP = "192.168.0.45",
    
    [Parameter(Mandatory=$false)]
    [int]$Port = 12345
)

Write-Host "🎨 Otto UDP Drawing Test" -ForegroundColor Cyan
Write-Host "Target: $IP`:$Port" -ForegroundColor Yellow
Write-Host ""

# Create UDP client
$client = New-Object System.Net.Sockets.UdpClient
$client.Connect($IP, $Port)

Write-Host "✅ Connected to Otto" -ForegroundColor Green
Write-Host ""

# Test 1: Draw a horizontal line
Write-Host "Test 1: Drawing horizontal line (y=140, x=100-150)" -ForegroundColor Cyan
for ($x = 100; $x -le 150; $x++) {
    $packet = "$x,140,1"
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($packet)
    $client.Send($bytes, $bytes.Length) | Out-Null
    Start-Sleep -Milliseconds 10
}
Write-Host "✅ Line drawn" -ForegroundColor Green
Start-Sleep -Seconds 1

# Test 2: Draw a vertical line
Write-Host "Test 2: Drawing vertical line (x=120, y=100-150)" -ForegroundColor Cyan
for ($y = 100; $y -le 150; $y++) {
    $packet = "120,$y,1"
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($packet)
    $client.Send($bytes, $bytes.Length) | Out-Null
    Start-Sleep -Milliseconds 10
}
Write-Host "✅ Line drawn" -ForegroundColor Green
Start-Sleep -Seconds 1

# Test 3: Draw a box
Write-Host "Test 3: Drawing box (50x50 at 50,50)" -ForegroundColor Cyan
# Top edge
for ($x = 50; $x -le 100; $x++) {
    $client.Send([Text.Encoding]::ASCII.GetBytes("$x,50,1"), "$x,50,1".Length) | Out-Null
}
# Bottom edge
for ($x = 50; $x -le 100; $x++) {
    $client.Send([Text.Encoding]::ASCII.GetBytes("$x,100,1"), "$x,100,1".Length) | Out-Null
}
# Left edge
for ($y = 50; $y -le 100; $y++) {
    $client.Send([Text.Encoding]::ASCII.GetBytes("50,$y,1"), "50,$y,1".Length) | Out-Null
}
# Right edge
for ($y = 50; $y -le 100; $y++) {
    $client.Send([Text.Encoding]::ASCII.GetBytes("100,$y,1"), "100,$y,1".Length) | Out-Null
}
Write-Host "✅ Box drawn" -ForegroundColor Green
Start-Sleep -Seconds 2

# Test 4: Erase part of the horizontal line
Write-Host "Test 4: Erasing part of horizontal line (x=110-130)" -ForegroundColor Cyan
for ($x = 110; $x -le 130; $x++) {
    $packet = "$x,140,0"
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($packet)
    $client.Send($bytes, $bytes.Length) | Out-Null
    Start-Sleep -Milliseconds 10
}
Write-Host "✅ Erased" -ForegroundColor Green
Start-Sleep -Seconds 1

# Test 5: Random dots
Write-Host "Test 5: Random dots (50 pixels)" -ForegroundColor Cyan
for ($i = 0; $i -lt 50; $i++) {
    $x = Get-Random -Minimum 0 -Maximum 240
    $y = Get-Random -Minimum 0 -Maximum 280
    $packet = "$x,$y,1"
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($packet)
    $client.Send($bytes, $bytes.Length) | Out-Null
    Start-Sleep -Milliseconds 20
}
Write-Host "✅ Dots drawn" -ForegroundColor Green

$client.Close()

Write-Host ""
Write-Host "========================================" -ForegroundColor Magenta
Write-Host "Test Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Magenta
Write-Host ""
Write-Host "Check Otto display for:" -ForegroundColor Cyan
Write-Host "  ✅ White lines and box" -ForegroundColor White
Write-Host "  ✅ Gap in horizontal line (erased pixels)" -ForegroundColor White
Write-Host "  ✅ Random dots scattered" -ForegroundColor White
Write-Host "  ✅ Otto emoji still visible underneath" -ForegroundColor White
Write-Host ""
Write-Host "To clear the canvas, visit:" -ForegroundColor Yellow
Write-Host "  http://$IP/drawing_clear" -ForegroundColor White
Write-Host ""
Write-Host "To disable drawing mode:" -ForegroundColor Yellow
Write-Host "  http://$IP/drawing_mode?enable=0" -ForegroundColor White
Write-Host ""
