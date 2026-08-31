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
    throw "Probe not found: $probe. Run: cmake --build build --target yalk_timing_probe"
}

$cache = Join-Path $BuildDir 'CMakeCache.txt'
$qtLine = Get-Content -LiteralPath $cache |
    Where-Object { $_ -like 'Qt6_DIR:PATH=*' } |
    Select-Object -First 1
if (-not $qtLine) { throw "Qt6_DIR not found in $cache" }
$qtCmake = $qtLine.Substring('Qt6_DIR:PATH='.Length)
$qtBin = [System.IO.Path]::GetFullPath((Join-Path $qtCmake '..\..\..\bin'))
$env:PATH = "$qtBin;$env:PATH"

$logDir = Join-Path $PSScriptRoot '..\runs\diagnostics'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$log = Join-Path $logDir "yalk_channel1_$stamp.log"

Write-Host 'REAL OUTPUTS: ISD 0.00 / 3.10 / 6.20 V.' -ForegroundColor Yellow
Write-Host "ISD=$IsdIp; adapter=$AdapterIp; equipment_ip=$EquipmentIp; channel=$IsdChannel"
Write-Host "Log: $log"

& $probe $IsdIp $AdapterIp $EquipmentIp $IsdChannel 2>&1 |
    Tee-Object -FilePath $log

if ($LASTEXITCODE -ne 0) {
    throw "Test failed with exit code $LASTEXITCODE. See $log"
}

Write-Host 'Single YALK channel completed; cleanup completed.' -ForegroundColor Green
