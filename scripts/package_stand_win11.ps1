param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\build\deploy'),
    [string]$PackageName = ('orbita_desktop_ubsi_priority_{0}' -f (Get-Date -Format 'yyyyMMdd_HHmm'))
)

$ErrorActionPreference = 'Stop'

$buildRoot = [System.IO.Path]::GetFullPath($BuildDirectory)
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$runtimeRoot = Join-Path $buildRoot 'desktop_orbita'
$application = Join-Path $runtimeRoot 'OrbitaDesktop.exe'
$packageRoot = Join-Path $outputRoot $PackageName
$archivePath = "$packageRoot.zip"

if (-not (Test-Path -LiteralPath $application -PathType Leaf)) {
    throw "OrbitaDesktop.exe not found: $application"
}
if ((Test-Path -LiteralPath $packageRoot) -or (Test-Path -LiteralPath $archivePath)) {
    throw "Package already exists: $PackageName"
}

$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
$qtDirectoryLine = Get-Content -LiteralPath $cachePath |
    Where-Object { $_ -like 'Qt6_DIR:PATH=*' } |
    Select-Object -First 1
if (-not $qtDirectoryLine) {
    throw "Qt6_DIR is not recorded in $cachePath"
}
$qtCmakeDirectory = $qtDirectoryLine.Substring('Qt6_DIR:PATH='.Length)
$qtRoot = [System.IO.Path]::GetFullPath((Join-Path $qtCmakeDirectory '..\..\..'))
$deployTool = Join-Path $qtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf)) {
    throw "windeployqt.exe not found: $deployTool"
}

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null

$runtimeFiles = @(
    'OrbitaDesktop.exe',
    'Lusbapi64.dll',
    # windeployqt sees dependencies of OrbitaDesktop.exe, but not dependencies
    # imported only by equipment DLLs. SerialPort remains a low-level runtime
    # dependency of stand adapters and must travel with the release explicitly.
    'Qt6SerialPort.dll',
    'parameters.db',
    'stand.ini'
)
foreach ($name in $runtimeFiles) {
    $source = Join-Path $runtimeRoot $name
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        Copy-Item -LiteralPath $source -Destination $packageRoot
    }
}

$standRuntime = Join-Path $buildRoot 'stand'
$diagnosticFiles = @(
    'orbita_equipment_probe.exe',
    'orbita_telemetry_probe.exe',
    'orbita_ubsi_udp_probe.exe',
    'orbita_ytp_rokt_probe.exe',
    'yalk_timing_probe.exe',
    'yalk_full_probe.exe',
    'visa_discover.exe'
)
foreach ($name in $diagnosticFiles) {
    $source = Join-Path $standRuntime $name
    if (Test-Path -LiteralPath $source -PathType Leaf) {
        Copy-Item -LiteralPath $source -Destination $packageRoot
    }
}

$runtimeDirectories = @('address', 'catalog', 'profiles', 'scenarios')
foreach ($name in $runtimeDirectories) {
    $source = Join-Path $runtimeRoot $name
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Required runtime directory not found: $source"
    }
    Copy-Item -LiteralPath $source -Destination $packageRoot -Recurse
}

$pluginSource = Join-Path $runtimeRoot 'plugins'
$pluginTarget = Join-Path $packageRoot 'plugins'
New-Item -ItemType Directory -Path $pluginTarget -Force | Out-Null
$pluginAllowList = @(
    'orbita_plugin_ktma_adapter_udp.dll',
    'orbita_plugin_isd_http.dll',
    'orbita_plugin_v7_visa.dll',
    'orbita_plugin_akip_1160.dll',
    'orbita_plugin_rigol_generator.dll',
    'orbita_plugin_rigol_dho8xx.dll'
)
foreach ($name in $pluginAllowList) {
    $source = Join-Path $pluginSource $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required equipment plugin not found: $source"
    }
    Copy-Item -LiteralPath $source -Destination $pluginTarget
}

New-Item -ItemType Directory -Path (Join-Path $packageRoot 'records') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $packageRoot 'runs') -Force | Out-Null

& $deployTool --release --no-translations --dir $packageRoot $application
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath -CompressionLevel Optimal
$hash = Get-FileHash -LiteralPath $archivePath -Algorithm SHA256
$hashLine = '{0}  {1}' -f $hash.Hash.ToLowerInvariant(), (Split-Path -Leaf $archivePath)
Set-Content -LiteralPath "$archivePath.sha256.txt" -Value $hashLine -Encoding utf8

[pscustomobject]@{
    Package = $packageRoot
    Archive = $archivePath
    Sha256 = $hash.Hash.ToLowerInvariant()
}
