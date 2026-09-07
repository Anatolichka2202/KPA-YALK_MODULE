[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$StageDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$stage = (Resolve-Path -LiteralPath $StageDir).Path
$manifestPath = Join-Path $stage "release-manifest.json"
$manifestHashPath = Join-Path $stage "release-manifest.sha256"

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "release-manifest.json not found: $manifestPath"
}
if (-not (Test-Path -LiteralPath $manifestHashPath -PathType Leaf)) {
    throw "release-manifest.sha256 not found: $manifestHashPath"
}

$hashLine = (Get-Content -LiteralPath $manifestHashPath -Raw).Trim()
if ($hashLine -notmatch '^([a-fA-F0-9]{64})\s+release-manifest\.json$') {
    throw "Invalid release-manifest.sha256 format"
}
$expectedManifestHash = $Matches[1].ToLowerInvariant()
$actualManifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualManifestHash -ne $expectedManifestHash) {
    throw "Manifest hash mismatch: expected $expectedManifestHash, actual $actualManifestHash"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ([int]$manifest.schema_version -ne 1) {
    throw "Unsupported release manifest schema: $($manifest.schema_version)"
}

$expectedPaths = @{}
foreach ($entry in $manifest.files) {
    $relativePath = [string]$entry.path
    if ([string]::IsNullOrWhiteSpace($relativePath)) {
        throw "Manifest contains an empty file path"
    }
    if ([IO.Path]::IsPathRooted($relativePath)) {
        throw "Manifest path must be relative: $relativePath"
    }
    if (($relativePath -split '/') -contains '..') {
        throw "Manifest path contains parent traversal: $relativePath"
    }
    if ($expectedPaths.ContainsKey($relativePath)) {
        throw "Manifest contains duplicate path: $relativePath"
    }
    $expectedPaths[$relativePath] = $true

    $nativeRelative = $relativePath.Replace([char]'/', [IO.Path]::DirectorySeparatorChar)
    $fullPath = Join-Path $stage $nativeRelative
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Payload file is missing: $relativePath"
    }

    $item = Get-Item -LiteralPath $fullPath
    if ([int64]$item.Length -ne [int64]$entry.size) {
        throw "Payload size mismatch for ${relativePath}: expected $($entry.size), actual $($item.Length)"
    }

    $actualHash = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $expectedHash = ([string]$entry.sha256).ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "Payload hash mismatch for ${relativePath}: expected $expectedHash, actual $actualHash"
    }
}

$trimChars = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$actualPayloadPaths = @(
    Get-ChildItem -LiteralPath $stage -Recurse -File |
        Where-Object {
            $_.FullName -ne $manifestPath -and $_.FullName -ne $manifestHashPath
        } |
        ForEach-Object {
            $relative = $_.FullName.Substring($stage.Length).TrimStart($trimChars)
            $relative.Replace([IO.Path]::DirectorySeparatorChar, [char]'/')
        }
)
foreach ($relativePath in $actualPayloadPaths) {
    if (-not $expectedPaths.ContainsKey($relativePath)) {
        throw "Untracked payload file found: $relativePath"
    }
}

Write-Output ("OK {0} {1} ({2} files)" -f $manifest.package_id, $manifest.version, $expectedPaths.Count)
