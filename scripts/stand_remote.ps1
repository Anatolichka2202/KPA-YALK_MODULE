[CmdletBinding()]
param(
    [ValidateSet('Status', 'Shell', 'Upload', 'Download', 'Run', 'V7Probe', 'DeployOrbita')]
    [string]$Action = 'Status',
    [string]$UserName = 'Azerty',
    # No default is intentional: the currently confirmed SSH address is .31,
    # but it is assigned to Wi-Fi and may change. Supplying it explicitly and
    # verifying the hostname prevents commands reaching another Windows PC.
    [Parameter(Mandatory = $true)]
    [string]$StandAddress,
    [string]$ExpectedComputerName = 'DESKTOP-5EO9J5A',
    [string]$Path,
    [string]$Command
)

$ErrorActionPreference = 'Stop'
$keyPath = Join-Path $env:USERPROFILE '.ssh\id_ed25519_orbita_stand'
if (-not (Test-Path -LiteralPath $keyPath)) {
    throw "SSH key not found: $keyPath"
}

$target = "$UserName@$StandAddress"
$sshCommon = @(
    '-i', $keyPath,
    '-o', 'IdentitiesOnly=yes',
    '-o', 'ConnectTimeout=5'
)

function Invoke-Checked {
    param(
        [Parameter(Mandatory)]
        [string]$Program,
        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program exited with code $LASTEXITCODE"
    }
}

function Assert-StandIdentity {
    $actual = & ssh.exe @sshCommon $target 'hostname'
    if ($LASTEXITCODE -ne 0) {
        throw "Cannot verify stand identity at $StandAddress"
    }
    $actualName = ($actual | Out-String).Trim()
    if ($actualName -ne $ExpectedComputerName) {
        throw "SSH target mismatch: expected $ExpectedComputerName, received $actualName"
    }
    Write-Host "Verified stand: $actualName ($StandAddress)"
}

Assert-StandIdentity

switch ($Action) {
    'Status' {
        Invoke-Checked -Program 'ssh.exe' -Arguments (
            $sshCommon + @($target, 'whoami && hostname && powershell.exe -NoProfile -Command "Get-Date; Get-Service sshd"')
        )
    }
    'Shell' {
        Invoke-Checked -Program 'ssh.exe' -Arguments ($sshCommon + @('-t', $target, 'powershell.exe -NoProfile'))
    }
    'Upload' {
        if (-not $Path) { throw 'Upload requires -Path' }
        $resolved = (Resolve-Path -LiteralPath $Path).Path
        Invoke-Checked -Program 'scp.exe' -Arguments (
            $sshCommon + @('-r', $resolved, "${target}:C:/Orbita/incoming/")
        )
    }
    'Download' {
        if (-not $Path) { throw 'Download requires a local -Path' }
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
        Invoke-Checked -Program 'scp.exe' -Arguments (
            $sshCommon + @('-r', "${target}:C:/Orbita/records/.", $Path)
        )
    }
    'Run' {
        if (-not $Command) { throw 'Run requires -Command' }
        # Windows sshd starts the command through cmd.exe. Passing a script
        # after -Command loses pipes, braces and non-ASCII text there; the
        # encoded form is parsed only by the target PowerShell process.
        $encodedCommand = [Convert]::ToBase64String(
            [System.Text.Encoding]::Unicode.GetBytes($Command))
        Invoke-Checked -Program 'ssh.exe' -Arguments (
            # The bypass applies only to this remote command and does not
            # change the stand computer execution policy.
            $sshCommon + @($target, "powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand $encodedCommand")
        )
    }
    'V7Probe' {
        $archive = Join-Path $PSScriptRoot '..\build\deploy\v7_probe_win11_20260824_1535.zip'
        $resolvedArchive = (Resolve-Path -LiteralPath $archive).Path
        Invoke-Checked -Program 'scp.exe' -Arguments (
            $sshCommon + @($resolvedArchive, "${target}:C:/Orbita/incoming/v7_probe.zip")
        )
        $remoteCommand = "Expand-Archive -LiteralPath 'C:\Orbita\incoming\v7_probe.zip' -DestinationPath 'C:\Orbita\releases\v7_probe' -Force; & 'C:\Orbita\releases\v7_probe\v7_probe.exe'"
        Invoke-Checked -Program 'ssh.exe' -Arguments (
            $sshCommon + @($target, "powershell.exe -NoProfile -Command `"$remoteCommand`"")
        )
    }
    'DeployOrbita' {
        if ($Path) {
            $archive = $Path
        } else {
            $archive = Get-ChildItem (Join-Path $PSScriptRoot '..\build\deploy') `
                -Filter 'orbita_desktop_ubsi_priority_*.zip' -File |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1 -ExpandProperty FullName
        }
        if (-not $archive) {
            throw 'No orbita_desktop_ubsi_priority_*.zip release found; run package_stand_win11.ps1 first'
        }
        $resolvedArchive = (Resolve-Path -LiteralPath $archive).Path
        $archiveName = Split-Path -Leaf $resolvedArchive
        $releaseName = [System.IO.Path]::GetFileNameWithoutExtension($archiveName)
        Invoke-Checked -Program 'scp.exe' -Arguments (
            $sshCommon + @($resolvedArchive, "${target}:C:/Orbita/incoming/$archiveName")
        )
        $remoteCommand = "`$destination = 'C:\Orbita\releases\$releaseName'; if (Test-Path -LiteralPath `$destination) { throw 'Release already exists: ' + `$destination }; Expand-Archive -LiteralPath 'C:\Orbita\incoming\$archiveName' -DestinationPath 'C:\Orbita\releases'; Get-Item -LiteralPath (`$destination + '\OrbitaDesktop.exe') | Select-Object FullName,Length,LastWriteTime"
        $encodedCommand = [Convert]::ToBase64String(
            [System.Text.Encoding]::Unicode.GetBytes($remoteCommand))
        Invoke-Checked -Program 'ssh.exe' -Arguments (
            $sshCommon + @($target, "powershell.exe -NoProfile -EncodedCommand $encodedCommand")
        )
    }
}
