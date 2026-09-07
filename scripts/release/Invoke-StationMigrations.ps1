[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DataRoot,

    [Parameter(Mandatory = $true)]
    [string]$CurrentVersion,

    [Parameter(Mandatory = $true)]
    [string]$TargetVersion,

    [string]$MigrationRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-SafeChildPath {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$RelativePath
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        throw "Migration path must not be empty"
    }
    if ([IO.Path]::IsPathRooted($RelativePath)) {
        throw "Migration path must be relative: $RelativePath"
    }

    $trimChars = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd($trimChars) + [IO.Path]::DirectorySeparatorChar
    $candidate = [IO.Path]::GetFullPath((Join-Path $Root $RelativePath))
    if (-not $candidate.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Migration path escapes its root: $RelativePath"
    }
    return $candidate
}

$current = [version]$CurrentVersion
$target = [version]$TargetVersion
if ($target -lt $current) {
    throw "Downgrade migrations are not supported by this runner: $current -> $target"
}

if (-not (Test-Path -LiteralPath $DataRoot)) {
    New-Item -ItemType Directory -Path $DataRoot -Force | Out-Null
}
$dataRootFull = (Resolve-Path -LiteralPath $DataRoot).Path

if ([string]::IsNullOrWhiteSpace($MigrationRoot)) {
    $MigrationRoot = Join-Path $PSScriptRoot "..\..\release\migrations"
}
$migrationRootFull = (Resolve-Path -LiteralPath $MigrationRoot).Path
$indexPath = Join-Path $migrationRootFull "index.json"
if (-not (Test-Path -LiteralPath $indexPath -PathType Leaf)) {
    throw "Migration index not found: $indexPath"
}

$index = Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json
if ([int]$index.schema_version -ne 1) {
    throw "Unsupported migration index schema: $($index.schema_version)"
}

$applied = @()
while ($current -lt $target) {
    $candidates = @(
        $index.migrations | Where-Object {
            ([version]$_.from_version -eq $current) -and ([version]$_.to_version -le $target)
        }
    )

    if ($candidates.Count -eq 0) {
        throw "No migration path from $current to $target"
    }
    if ($candidates.Count -gt 1) {
        $ids = ($candidates | ForEach-Object { $_.id }) -join ", "
        throw "Ambiguous migration path from ${current}: $ids"
    }

    $migration = $candidates[0]
    foreach ($requiredProperty in @("id", "from_version", "to_version", "script", "backup_paths")) {
        if (-not ($migration.PSObject.Properties.Name -contains $requiredProperty)) {
            throw "Migration entry is missing required property '$requiredProperty'"
        }
    }

    $migrationId = [string]$migration.id
    if ($migrationId -notmatch '^[A-Za-z0-9._-]+$') {
        throw "Unsafe migration id: $migrationId"
    }

    $nextVersion = [version]$migration.to_version
    if ($nextVersion -le $current) {
        throw "Migration '$migrationId' does not advance version: $current -> $nextVersion"
    }

    $scriptRelative = [string]$migration.script
    $scriptPath = Get-SafeChildPath -Root $migrationRootFull -RelativePath $scriptRelative
    if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
        throw "Migration script not found: $scriptPath"
    }

    $stamp = (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ")
    $backupRoot = Join-Path $dataRootFull ("backups\release\{0}-{1}" -f $stamp, $migrationId)
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null

    $backupEntries = @()
    foreach ($relative in @($migration.backup_paths)) {
        $relativePath = [string]$relative
        $segments = $relativePath.Replace([IO.Path]::AltDirectorySeparatorChar, [IO.Path]::DirectorySeparatorChar).Split([IO.Path]::DirectorySeparatorChar)
        if ($segments.Count -gt 0 -and $segments[0].Equals("backups", [StringComparison]::OrdinalIgnoreCase)) {
            throw "Migration backup_paths must not include the runner-owned backups tree: $relativePath"
        }

        $sourcePath = Get-SafeChildPath -Root $dataRootFull -RelativePath $relativePath
        $backupPath = Get-SafeChildPath -Root $backupRoot -RelativePath $relativePath
        $existed = Test-Path -LiteralPath $sourcePath

        if ($existed) {
            $backupParent = Split-Path -Parent $backupPath
            if (-not (Test-Path -LiteralPath $backupParent)) {
                New-Item -ItemType Directory -Path $backupParent -Force | Out-Null
            }
            Copy-Item -LiteralPath $sourcePath -Destination $backupPath -Recurse -Force
        }

        $backupEntries += [pscustomobject]@{
            RelativePath = $relativePath
            SourcePath   = $sourcePath
            BackupPath   = $backupPath
            Existed      = $existed
        }
    }

    try {
        & $scriptPath -DataRoot $dataRootFull -FromVersion $current.ToString() -ToVersion $nextVersion.ToString()
        if (-not $?) {
            throw "Migration script returned failure: $migrationId"
        }
    }
    catch {
        foreach ($entry in $backupEntries) {
            if (Test-Path -LiteralPath $entry.SourcePath) {
                Remove-Item -LiteralPath $entry.SourcePath -Recurse -Force
            }
            if ($entry.Existed -and (Test-Path -LiteralPath $entry.BackupPath)) {
                $parent = Split-Path -Parent $entry.SourcePath
                if (-not (Test-Path -LiteralPath $parent)) {
                    New-Item -ItemType Directory -Path $parent -Force | Out-Null
                }
                Copy-Item -LiteralPath $entry.BackupPath -Destination $entry.SourcePath -Recurse -Force
            }
        }
        throw "Migration '$migrationId' failed and declared backup paths were restored. $($_.Exception.Message)"
    }

    $applied += $migrationId
    $current = $nextVersion
}

$state = [ordered]@{
    schema_version = 1
    version        = $target.ToString()
    migrated_utc   = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    applied        = $applied
}
$statePath = Join-Path $dataRootFull ".miltech-release-state.json"
$tempStatePath = "$statePath.tmp"
$state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $tempStatePath -Encoding UTF8
Move-Item -LiteralPath $tempStatePath -Destination $statePath -Force

Write-Output ("OK {0} -> {1}; applied: {2}" -f $CurrentVersion, $TargetVersion, ($applied -join ", "))
