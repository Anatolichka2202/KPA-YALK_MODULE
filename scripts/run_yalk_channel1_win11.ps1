param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build'),
    [string]$IsdIp = '192.168.0.101',
    [string]$AdapterIp = '192.168.0.115',
    [string]$EquipmentIp = '192.168.0.50',
    [int]$IsdChannel = 1
)

$ErrorActionPreference = 'Stop'
$probe = Join-Path $BuildDir 'stand\yalk_timing_probe.exe'
if (-not (Test-Path -LiteralPath $probe)) {
    throw "Не найден $probe. Сначала выполните: cmake --build build --target yalk_timing_probe"
}

$logDir = Join-Path $PSScriptRoot '..\runs\diagnostics'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$log = Join-Path $logDir "yalk_channel1_$stamp.log"

Write-Host 'Проверка использует реальные воздействия ИСД 0.00 / 3.10 / 6.20 В.' -ForegroundColor Yellow
Write-Host "ИСД=$IsdIp; адаптер=$AdapterIp; Ethernet ПЭВМ=$EquipmentIp; канал=$IsdChannel"
Write-Host "Лог: $log"

& $probe $IsdIp $AdapterIp $EquipmentIp $IsdChannel 2>&1 |
    Tee-Object -FilePath $log

if ($LASTEXITCODE -ne 0) {
    throw "Проверка завершилась с кодом $LASTEXITCODE. Смотрите $log"
}

Write-Host 'Один канал ЯЛК пройден, cleanup выполнен.' -ForegroundColor Green
