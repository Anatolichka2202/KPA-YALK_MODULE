[CmdletBinding()]
param(
    [ValidateSet('Status', 'Shell', 'Upload', 'Download', 'Run', 'V7Probe')]
    [string]$Action = 'Status',
    [string]$UserName = 'Azerty',
    [string]$StandAddress = '192.168.0.50',
    [string]$Path,
    [string]$Command
)

$ErrorActionPreference = 'Stop'
$keyPath = Join-Path $env:USERPROFILE '.ssh\id_ed25519_orbita_stand'
if (-not (Test-Path -LiteralPath $keyPath)) {
    throw "Не найден ключ $keyPath"
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
        throw "$Program завершился с кодом $LASTEXITCODE"
    }
}

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
        if (-not $Path) { throw 'Для Upload укажите -Path' }
        $resolved = (Resolve-Path -LiteralPath $Path).Path
        Invoke-Checked -Program 'scp.exe' -Arguments (
            $sshCommon + @('-r', $resolved, "${target}:C:/Orbita/incoming/")
        )
    }
    'Download' {
        if (-not $Path) { throw 'Для Download укажите локальный -Path' }
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
        Invoke-Checked -Program 'scp.exe' -Arguments (
            $sshCommon + @('-r', "${target}:C:/Orbita/records/.", $Path)
        )
    }
    'Run' {
        if (-not $Command) { throw 'Для Run укажите -Command' }
        Invoke-Checked -Program 'ssh.exe' -Arguments (
            $sshCommon + @($target, "powershell.exe -NoProfile -Command `"$Command`"")
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
}
