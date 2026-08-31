param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\runs\diagnostics'),
    [string]$IsdIp = '192.168.0.101',
    [string]$AdapterIp = '192.168.0.115',
    [string]$EquipmentIp = '192.168.0.50',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$probe = Join-Path $BuildDir 'stand\yalk_full_probe.exe'
if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) {
    throw "Probe not found: $probe. Run: cmake --build build --target yalk_full_probe"
}

$cache = Join-Path $BuildDir 'CMakeCache.txt'
$qtLine = Get-Content -LiteralPath $cache |
    Where-Object { $_ -like 'Qt6_DIR:PATH=*' } |
    Select-Object -First 1
if (-not $qtLine) { throw "Qt6_DIR not found in $cache" }
$qtCmake = $qtLine.Substring('Qt6_DIR:PATH='.Length)
$qtBin = [System.IO.Path]::GetFullPath((Join-Path $qtCmake '..\..\..\bin'))
$env:PATH = "$qtBin;$env:PATH"

$output = [System.IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$consoleLog = Join-Path $output "yalk_full_console_$stamp.log"

Write-Host 'DIAGNOSTIC YALK: 80 addresses x 0.00/3.10/6.20 V.' -ForegroundColor Yellow
Write-Host 'REAL ISD OUTPUTS. Acceptance OK is not produced.' -ForegroundColor Yellow
Write-Host "ISD=$IsdIp; adapter=$AdapterIp; equipment_ip=$EquipmentIp"

if (-not $Force) {
    $answer = Read-Host 'Type START to confirm stand readiness'
    if ($answer -cne 'START') {
        throw 'Start cancelled by operator'
    }
}

& $probe $IsdIp $AdapterIp $EquipmentIp $output 2>&1 |
    Tee-Object -FilePath $consoleLog
$exitCode = $LASTEXITCODE

if ($exitCode -eq 4) {
    Write-Warning "Completed with failed points. See $consoleLog and channels.csv."
    exit 4
}
if ($exitCode -ne 0) {
    throw "Full test error $exitCode. Cleanup requested; see $consoleLog"
}

Write-Host "Full diagnostic completed: $consoleLog" -ForegroundColor Green
