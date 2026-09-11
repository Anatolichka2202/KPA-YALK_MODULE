[CmdletBinding()]
param(
    [string]$ReleaseRoot = 'C:\Orbita\releases',
    [string]$BaseUrl = 'https://github.com/Anatolichka2202/KPA-YALK_MODULE/releases/download/new-dis-latest'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$archiveName = 'MilTechStation-KTMA-new-dis.zip'
$hashName = "$archiveName.sha256.txt"
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("ktma-release-" + [guid]::NewGuid().ToString('N'))
$archivePath = Join-Path $tempRoot $archiveName
$hashPath = Join-Path $tempRoot $hashName
$extractRoot = Join-Path $tempRoot 'extract'

try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $extractRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $ReleaseRoot -Force | Out-Null

    Write-Host "Downloading $archiveName ..."
    Invoke-WebRequest -UseBasicParsing -Uri "$BaseUrl/$archiveName" -OutFile $archivePath
    Invoke-WebRequest -UseBasicParsing -Uri "$BaseUrl/$hashName" -OutFile $hashPath

    $expectedLine = (Get-Content -LiteralPath $hashPath -Raw).Trim()
    $expectedHash = ($expectedLine -split '\s+')[0].ToLowerInvariant()
    if ($expectedHash -notmatch '^[0-9a-f]{64}$') {
        throw "Invalid SHA-256 file: $hashPath"
    }

    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "SHA-256 mismatch. expected=$expectedHash actual=$actualHash"
    }
    Write-Host "SHA-256 OK: $actualHash"

    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractRoot
    $roots = @(Get-ChildItem -LiteralPath $extractRoot -Directory)
    $looseFiles = @(Get-ChildItem -LiteralPath $extractRoot -File)
    if ($roots.Count -ne 1 -or $looseFiles.Count -ne 0) {
        throw 'Release archive must contain exactly one top-level release directory'
    }

    $releaseName = $roots[0].Name
    if ($releaseName -notmatch '^MilTechStation-KTMA-2\.0\.0-pilot-[0-9a-f]{7}$') {
        throw "Unexpected release directory name: $releaseName"
    }

    $target = Join-Path $ReleaseRoot $releaseName
    if (Test-Path -LiteralPath $target) {
        throw "Release already exists and will not be overwritten: $target"
    }

    Move-Item -LiteralPath $roots[0].FullName -Destination $target
    Write-Host "Installed: $target"
    Write-Host "Run from the stand desktop session:"
    Write-Host "  $target\MilTechStation.exe"
    Write-Output $target
}
finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
