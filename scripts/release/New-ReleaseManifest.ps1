[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$StageDir,

    [string]$PackageId = "miltech-station.ktma"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$stage = (Resolve-Path -LiteralPath $StageDir).Path
$buildInfoPath = Join-Path $stage "app\build-info.json"
if (-not (Test-Path -LiteralPath $buildInfoPath -PathType Leaf)) {
    throw "build-info.json not found: $buildInfoPath"
}

$buildInfo = Get-Content -LiteralPath $buildInfoPath -Raw | ConvertFrom-Json
foreach ($requiredProperty in @("version", "git_sha", "build_utc", "release_channel")) {
    if (-not ($buildInfo.PSObject.Properties.Name -contains $requiredProperty)) {
        throw "build-info.json is missing required property '$requiredProperty'"
    }
}

$manifestPath = Join-Path $stage "release-manifest.json"
$manifestHashPath = Join-Path $stage "release-manifest.sha256"

$payloadFiles = @(
    Get-ChildItem -LiteralPath $stage -Recurse -File |
        Where-Object {
            $_.FullName -ne $manifestPath -and $_.FullName -ne $manifestHashPath
        } |
        Sort-Object FullName
)

$trimChars = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$files = @()
foreach ($file in $payloadFiles) {
    $relativePath = $file.FullName.Substring($stage.Length).TrimStart($trimChars)
    $relativePath = $relativePath.Replace([IO.Path]::DirectorySeparatorChar, [char]'/')
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()

    $files += [ordered]@{
        path   = $relativePath
        size   = [int64]$file.Length
        sha256 = $hash
    }
}

$manifest = [ordered]@{
    schema_version = 1
    package_id     = $PackageId
    version        = [string]$buildInfo.version
    channel        = [string]$buildInfo.release_channel
    git_sha        = [string]$buildInfo.git_sha
    generated_utc  = [string]$buildInfo.build_utc
    payload_root   = "app"
    files          = $files
}

$manifestJson = $manifest | ConvertTo-Json -Depth 8
Set-Content -LiteralPath $manifestPath -Value $manifestJson -Encoding UTF8

$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath $manifestHashPath -Value ("{0}  release-manifest.json" -f $manifestHash) -Encoding ASCII

Write-Output $manifestPath
