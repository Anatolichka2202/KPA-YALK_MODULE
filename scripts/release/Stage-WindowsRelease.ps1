[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [string]$OutputRoot,

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",

    [string]$QtBinDir = "",

    [string]$PackageId = "miltech-station.ktma",

    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
$buildRoot = (Resolve-Path -LiteralPath $BuildDir).Path

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $sourceRoot "out\release"
}

if (-not (Test-Path -LiteralPath $OutputRoot)) {
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
}
$OutputRoot = (Resolve-Path -LiteralPath $OutputRoot).Path

$temporaryStage = Join-Path $OutputRoot (".stage-{0}" -f [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporaryStage -Force | Out-Null

try {
    & cmake --install $buildRoot --config $Configuration --prefix $temporaryStage
    if ($LASTEXITCODE -ne 0) {
        throw "cmake --install failed with exit code $LASTEXITCODE"
    }

    $buildInfoPath = Join-Path $temporaryStage "app\build-info.json"
    if (-not (Test-Path -LiteralPath $buildInfoPath -PathType Leaf)) {
        throw "Release install did not produce app\build-info.json. Reconfigure the build with the repository root CMakeLists.txt."
    }

    $buildInfo = Get-Content -LiteralPath $buildInfoPath -Raw | ConvertFrom-Json
    $version = [string]$buildInfo.version
    $channel = [string]$buildInfo.release_channel
    if ([string]::IsNullOrWhiteSpace($version) -or [string]::IsNullOrWhiteSpace($channel)) {
        throw "build-info.json does not contain version/release_channel"
    }

    $artifactName = "MilTechStation-KTMA-{0}-{1}" -f $version, $channel
    $finalStage = Join-Path $OutputRoot $artifactName
    if ((Test-Path -LiteralPath $finalStage) -and (-not $Force)) {
        throw "Release stage already exists: $finalStage. Use -Force to replace it."
    }

    $exePath = Join-Path $temporaryStage "app\OrbitaDesktop.exe"
    if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
        throw "OrbitaDesktop.exe not found in installed payload: $exePath"
    }

    if ([string]::IsNullOrWhiteSpace($QtBinDir)) {
        $windeployqtCommand = Get-Command "windeployqt.exe" -ErrorAction SilentlyContinue
        if ($null -eq $windeployqtCommand) {
            throw "windeployqt.exe not found in PATH. Pass -QtBinDir <Qt bin directory>."
        }
        $windeployqt = $windeployqtCommand.Source
    }
    else {
        $windeployqt = Join-Path $QtBinDir "windeployqt.exe"
        if (-not (Test-Path -LiteralPath $windeployqt -PathType Leaf)) {
            throw "windeployqt.exe not found: $windeployqt"
        }
    }

    if ($Configuration -eq "Debug") {
        $deployMode = "--debug"
    }
    else {
        $deployMode = "--release"
    }

    & $windeployqt $deployMode --no-translations --compiler-runtime $exePath
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }

    $manifestScript = Join-Path $PSScriptRoot "New-ReleaseManifest.ps1"
    & $manifestScript -StageDir $temporaryStage -PackageId $PackageId | Out-Null

    $verifyScript = Join-Path $PSScriptRoot "Test-ReleaseManifest.ps1"
    & $verifyScript -StageDir $temporaryStage | Out-Null

    if (Test-Path -LiteralPath $finalStage) {
        Remove-Item -LiteralPath $finalStage -Recurse -Force
    }
    Move-Item -LiteralPath $temporaryStage -Destination $finalStage
    $temporaryStage = $null

    Write-Output $finalStage
}
finally {
    if ($null -ne $temporaryStage -and (Test-Path -LiteralPath $temporaryStage)) {
        Remove-Item -LiteralPath $temporaryStage -Recurse -Force -ErrorAction SilentlyContinue
    }
}
