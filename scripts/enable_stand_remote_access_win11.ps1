#Requires -RunAsAdministrator

[CmdletBinding()]
param(
    [string]$UserName = $env:USERNAME,
    [string]$AllowedClientAddress = '192.168.0.171'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Keep this bootstrap file ASCII-only. Windows PowerShell 5.1 treats UTF-8
# files without a BOM as the current ANSI code page.
$publicKey = 'ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIEOwOVo6KbdNKKGgbEW9f/RkqYLxF2CB23TghKnGQRKD orbita-dev-to-stand-20260824'
$localUserName = ($UserName -split '\\')[-1]
$localUser = Get-LocalUser -Name $localUserName -ErrorAction Stop
$userSid = $localUser.SID.Value
$systemSid = 'S-1-5-18'
$administratorsSid = 'S-1-5-32-544'

function Invoke-Icacls {
    param(
        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    & "$env:SystemRoot\System32\icacls.exe" @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "icacls failed with exit code $LASTEXITCODE"
    }
}

function Add-AuthorizedKey {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType File -Path $Path | Out-Null
    }

    $existing = @(Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue)
    if ($existing -notcontains $publicKey) {
        Add-Content -LiteralPath $Path -Value $publicKey -Encoding ascii
    }
}

Write-Host 'Installing Windows OpenSSH Server...'
$capabilityName = 'OpenSSH.Server~~~~0.0.1.0'
$capability = Get-WindowsCapability -Online -Name $capabilityName
if ($capability.State -ne 'Installed') {
    Add-WindowsCapability -Online -Name $capabilityName | Out-Host
}

$capability = Get-WindowsCapability -Online -Name $capabilityName
if ($capability.State -ne 'Installed') {
    throw "OpenSSH Server installation did not complete. State: $($capability.State)"
}

$sshdService = Get-Service -Name sshd -ErrorAction SilentlyContinue
if (-not $sshdService) {
    throw 'OpenSSH Server capability is installed, but the sshd service is missing. Reboot Windows and run this script again.'
}
Set-Service -Name sshd -StartupType Automatic

$profile = Get-CimInstance Win32_UserProfile |
    Where-Object { $_.SID -eq $userSid } |
    Select-Object -First 1
if (-not $profile -or -not $profile.LocalPath) {
    throw "Windows profile was not found for user $localUserName"
}

Write-Host "Installing the SSH public key for $localUserName..."
$userSshDirectory = Join-Path $profile.LocalPath '.ssh'
$userAuthorizedKeys = Join-Path $userSshDirectory 'authorized_keys'
Add-AuthorizedKey -Path $userAuthorizedKeys
Invoke-Icacls -Arguments @(
    $userSshDirectory,
    '/inheritance:r',
    '/grant:r',
    "*$systemSid`:(OI)(CI)F",
    "*$userSid`:(OI)(CI)F"
)
Invoke-Icacls -Arguments @(
    $userAuthorizedKeys,
    '/inheritance:r',
    '/grant:r',
    "*$systemSid`:F",
    "*$userSid`:F"
)

# The default Windows sshd_config uses this file for administrator accounts.
$adminAuthorizedKeys = Join-Path $env:ProgramData 'ssh\administrators_authorized_keys'
Add-AuthorizedKey -Path $adminAuthorizedKeys
Invoke-Icacls -Arguments @(
    $adminAuthorizedKeys,
    '/inheritance:r',
    '/grant:r',
    "*$systemSid`:F",
    "*$administratorsSid`:F"
)

Write-Host "Restricting TCP/22 to client $AllowedClientAddress..."
$defaultRule = Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue
if ($defaultRule) {
    $defaultRule | Disable-NetFirewallRule | Out-Null
}

$ruleName = 'Orbita-Stand-SSH-In'
$rule = Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue
if (-not $rule) {
    New-NetFirewallRule `
        -Name $ruleName `
        -DisplayName 'Orbita stand SSH from developer workstation' `
        -Enabled True `
        -Direction Inbound `
        -Protocol TCP `
        -Action Allow `
        -LocalPort 22 `
        -RemoteAddress $AllowedClientAddress | Out-Null
} else {
    $rule | Set-NetFirewallRule -Enabled True -Direction Inbound -Action Allow | Out-Null
    $rule | Get-NetFirewallAddressFilter |
        Set-NetFirewallAddressFilter -RemoteAddress $AllowedClientAddress | Out-Null
}

Write-Host 'Preparing C:\Orbita...'
$orbitaDirectories = @(
    'C:\Orbita',
    'C:\Orbita\incoming',
    'C:\Orbita\releases',
    'C:\Orbita\logs',
    'C:\Orbita\records'
)
foreach ($directory in $orbitaDirectories) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
Invoke-Icacls -Arguments @(
    'C:\Orbita',
    '/grant',
    "*$userSid`:(OI)(CI)M",
    '/T'
)

$sshdPath = Join-Path $env:SystemRoot 'System32\OpenSSH\sshd.exe'
if (Test-Path -LiteralPath $sshdPath) {
    & $sshdPath -t
    if ($LASTEXITCODE -ne 0) {
        throw 'sshd configuration validation failed'
    }
}

Start-Service -Name sshd
Restart-Service -Name sshd

$listener = Get-NetTCPConnection -State Listen -LocalPort 22 -ErrorAction Stop
Write-Host ''
Write-Host 'READY' -ForegroundColor Green
Write-Host "User: $localUserName"
Write-Host "Allowed client: $AllowedClientAddress"
Write-Host "Listener: $($listener.LocalAddress):22"
Write-Host 'Workspace: C:\Orbita'
