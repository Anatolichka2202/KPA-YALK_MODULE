[CmdletBinding()]
param(
    [string]$BuildDirectory = '',
    [string]$OutputDirectory = '',
    [string]$PackageName = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $PSScriptRoot '..\build\Desktop_Qt_6_8_0_MinGW_64_bit-Release'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $PSScriptRoot '..\build\deploy'
}
if ([string]::IsNullOrWhiteSpace($PackageName)) {
    $shortSha = ''
    try {
        $shortSha = (& git -C (Join-Path $PSScriptRoot '..') rev-parse --short=7 HEAD 2>$null).Trim()
    } catch {}
    if ([string]::IsNullOrWhiteSpace($shortSha)) {
        $shortSha = Get-Date -Format 'yyyyMMdd-HHmmss'
    }
    $PackageName = "MilTechStation-KTMA-2.0.0-pilot-$shortSha"
}

$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$runtimeRoot = Join-Path $buildRoot 'apps\desktop'
$application = Join-Path $runtimeRoot 'MilTechStation.exe'
if (-not (Test-Path -LiteralPath $application -PathType Leaf)) {
    throw "MilTechStation.exe not found: $application"
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$packageRoot = Join-Path $outputRoot $PackageName
$archivePath = "$packageRoot.zip"
if ((Test-Path -LiteralPath $packageRoot) -or (Test-Path -LiteralPath $archivePath)) {
    throw "Package already exists: $PackageName"
}

$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
$qtDirectoryLine = Get-Content -LiteralPath $cachePath |
    Where-Object { $_ -match '^Qt6_DIR:[^=]+=' } | Select-Object -First 1
if (-not $qtDirectoryLine) { throw "Qt6_DIR is not recorded in $cachePath" }
$qtCmakeDirectory = $qtDirectoryLine.Substring($qtDirectoryLine.IndexOf('=') + 1)
$qtRoot = [IO.Path]::GetFullPath((Join-Path $qtCmakeDirectory '..\..\..'))
$deployTool = Join-Path $qtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf)) {
    throw "windeployqt.exe not found: $deployTool"
}

New-Item -ItemType Directory -Path $packageRoot | Out-Null
foreach ($name in @(
    'MilTechStation.exe', 'Lusbapi64.dll', 'Qt6SerialPort.dll',
    'parameters.db', 'stand.ini',
    'orbita_equipment_probe.exe', 'orbita_telemetry_probe.exe',
    'orbita_ubsi_udp_probe.exe', 'orbita_yvp_rokt_probe.exe',
    'visa_discover.exe'
)) {
    $source = Join-Path $runtimeRoot $name
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        Copy-Item -LiteralPath $source -Destination $packageRoot
    }
}

foreach ($name in @('address', 'catalog', 'profiles', 'scenarios', 'plugins')) {
    $source = Join-Path $runtimeRoot $name
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Required runtime directory not found: $source"
    }
    Copy-Item -LiteralPath $source -Destination $packageRoot -Recurse
}
New-Item -ItemType Directory -Path (Join-Path $packageRoot 'records') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $packageRoot 'runs') | Out-Null

Set-Content -LiteralPath (Join-Path $packageRoot 'BUILD_INFO.txt') -Encoding ascii -Value @(
    "package=$PackageName"
    "created_utc=$([DateTime]::UtcNow.ToString('o'))"
)

& $deployTool --release --no-translations --compiler-runtime --dir $packageRoot $application
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

foreach ($plugin in Get-ChildItem -LiteralPath (Join-Path $packageRoot 'plugins') -Filter '*.dll' -File) {
    & $deployTool --release --no-translations --no-plugins --compiler-runtime `
        --dir $packageRoot $plugin.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed for equipment plugin $($plugin.Name)"
    }
}

$required = @(
    'MilTechStation.exe', 'parameters.db', 'profiles\stand_ktma.yaml',
    'scenarios\ubsi_production_full.yaml', 'scenarios\ubsi_production_power.yaml',
    'scenarios\ubsi_production_yalk.yaml', 'scenarios\ubsi_production_ytp.yaml',
    'scenarios\ubsi_production_yvp.yaml',
    'plugins\orbita_plugin_akip_1160.dll',
    'plugins\orbita_plugin_isd_http.dll',
    'plugins\orbita_plugin_ktma_adapter_udp.dll',
    'plugins\orbita_plugin_v7_visa.dll',
    'orbita_yvp_rokt_probe.exe',
    'platforms\qwindows.dll', 'Qt6SerialPort.dll'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageRoot $relative) -PathType Leaf)) {
        throw "Release is incomplete; missing $relative"
    }
}

Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath -CompressionLevel Optimal
$hash = Get-FileHash -LiteralPath $archivePath -Algorithm SHA256
Set-Content -LiteralPath "$archivePath.sha256.txt" -Encoding ascii `
    -Value ("{0}  {1}" -f $hash.Hash.ToLowerInvariant(), (Split-Path -Leaf $archivePath))

[pscustomobject]@{
    Package = $packageRoot
    Archive = $archivePath
    Sha256 = $hash.Hash.ToLowerInvariant()
}
