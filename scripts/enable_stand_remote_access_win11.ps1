#Requires -RunAsAdministrator

[CmdletBinding()]
param(
    [string]$UserName = $env:USERNAME,
    [string]$AllowedClientAddress = '192.168.0.171'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

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
        throw "icacls завершился с кодом $LASTEXITCODE"
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

Write-Host 'Установка OpenSSH Server...'
$capability = Get-WindowsCapability -Online -Name 'OpenSSH.Server~~~~0.0.1.0'
if ($capability.State -ne 'Installed') {
    Add-WindowsCapability -Online -Name 'OpenSSH.Server~~~~0.0.1.0' | Out-Host
}

$sshdService = Get-Service -Name sshd -ErrorAction Stop
Set-Service -Name sshd -StartupType Automatic

$profile = Get-CimInstance Win32_UserProfile |
    Where-Object { $_.SID -eq $userSid } |
    Select-Object -First 1
if (-not $profile -or -not $profile.LocalPath) {
    throw "Не найден профиль пользователя $localUserName"
}

Write-Host "Установка ключа для $localUserName..."
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

# Стандартная конфигурация Windows OpenSSH направляет администраторов в этот
# общий файл. Тот же ключ кладётся сюда, чтобы вход работал независимо от того,
# входит ли оператор стенда в локальную группу администраторов.
$adminAuthorizedKeys = Join-Path $env:ProgramData 'ssh\administrators_authorized_keys'
Add-AuthorizedKey -Path $adminAuthorizedKeys
Invoke-Icacls -Arguments @(
    $adminAuthorizedKeys,
    '/inheritance:r',
    '/grant:r',
    "*$systemSid`:F",
    "*$administratorsSid`:F"
)

Write-Host "Ограничение TCP/22 адресом $AllowedClientAddress..."
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

Write-Host 'Подготовка рабочего каталога C:\Orbita...'
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
        throw 'Проверка конфигурации sshd завершилась ошибкой'
    }
}

Start-Service -Name sshd
Restart-Service -Name sshd

$listener = Get-NetTCPConnection -State Listen -LocalPort 22 -ErrorAction Stop
Write-Host ''
Write-Host 'Готово.' -ForegroundColor Green
Write-Host "Пользователь: $localUserName"
Write-Host "Разрешённый клиент: $AllowedClientAddress"
Write-Host "Слушатель: $($listener.LocalAddress):22"
Write-Host 'Рабочий каталог: C:\Orbita'

