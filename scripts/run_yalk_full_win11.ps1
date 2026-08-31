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
    throw "Не найден $probe. Выполните: cmake --build build --target yalk_full_probe"
}

$output = [System.IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$consoleLog = Join-Path $output "yalk_full_console_$stamp.log"

Write-Host 'ДИАГНОСТИЧЕСКИЙ полный цикл ЯЛК: 80 адресов x 0,00/3,10/6,20 В.' -ForegroundColor Yellow
Write-Host 'Будут последовательно включаться реальные выходы ИСД. Приёмочный OK не формируется.' -ForegroundColor Yellow
Write-Host "ИСД=$IsdIp; адаптер=$AdapterIp; Ethernet ПЭВМ=$EquipmentIp"

if (-not $Force) {
    $answer = Read-Host 'Подтвердите готовность стенда словом ЗАПУСК'
    if ($answer -cne 'ЗАПУСК') {
        throw 'Запуск отменён оператором'
    }
}

& $probe $IsdIp $AdapterIp $EquipmentIp $output 2>&1 |
    Tee-Object -FilePath $consoleLog
$exitCode = $LASTEXITCODE

if ($exitCode -eq 4) {
    Write-Warning "Цикл завершён, есть точки вне допуска. Смотрите $consoleLog и channels.csv."
    exit 4
}
if ($exitCode -ne 0) {
    throw "Ошибка полного контура, код $exitCode. Cleanup запрошен; смотрите $consoleLog"
}

Write-Host "Полный диагностический цикл завершён: $consoleLog" -ForegroundColor Green
