#requires -version 5.1
<##
.SYNOPSIS
    Read-only identity and OpenSSH check for the Windows 11 stand computer.

.DESCRIPTION
    Run locally on the computer that is believed to be the stand PC. The script
    does not install, start, stop or reconfigure anything. It prints the exact
    host identity, IPv4/MAC pairs, state of OpenSSH Server, TCP/22 listeners and
    firewall rules. Save the console output and return it to the developer.
##>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'

Write-Host '=== ORBITA STAND ACCESS CHECK ==='
Write-Host "Time: $(Get-Date -Format o)"
Write-Host "Computer: $env:COMPUTERNAME"
Write-Host "User: $([Security.Principal.WindowsIdentity]::GetCurrent().Name)"

Write-Host "`n--- Windows ---"
Get-CimInstance Win32_OperatingSystem |
    Select-Object Caption, Version, BuildNumber, CSName, OSArchitecture |
    Format-List

Write-Host "`n--- Active network interfaces ---"
Get-NetAdapter -Physical -ErrorAction SilentlyContinue |
    Where-Object { $_.Status -eq 'Up' } |
    ForEach-Object {
        $adapter = $_
        $addresses = Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { $_.IPAddress -notlike '169.254.*' } |
            Select-Object -ExpandProperty IPAddress
        [pscustomobject]@{
            Name = $adapter.Name
            InterfaceIndex = $adapter.ifIndex
            MAC = $adapter.MacAddress
            IPv4 = ($addresses -join ', ')
            LinkSpeed = $adapter.LinkSpeed
        }
    } | Format-Table -AutoSize

Write-Host "`n--- OpenSSH Server capability/service ---"
if (Get-Command Get-WindowsCapability -ErrorAction SilentlyContinue) {
    try {
        Get-WindowsCapability -Online -Name 'OpenSSH.Server*' -ErrorAction Stop |
            Select-Object Name, State | Format-Table -AutoSize
    }
    catch {
        Write-Host 'OpenSSH capability state requires an elevated PowerShell window.'
    }
}
Get-Service -Name sshd -ErrorAction SilentlyContinue |
    Select-Object Name, Status, StartType | Format-Table -AutoSize

Write-Host "`n--- TCP/22 listener ---"
Get-NetTCPConnection -State Listen -LocalPort 22 -ErrorAction SilentlyContinue |
    Select-Object LocalAddress, LocalPort, OwningProcess | Format-Table -AutoSize

Write-Host "`n--- SSH firewall rules ---"
Get-NetFirewallRule -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match 'OpenSSH|sshd|Orbita-Stand-SSH' -or $_.DisplayName -match 'OpenSSH|sshd|Orbita' } |
    ForEach-Object {
        $rule = $_
        $addresses = $rule | Get-NetFirewallAddressFilter -ErrorAction SilentlyContinue
        [pscustomobject]@{
            Name = $rule.Name
            Enabled = $rule.Enabled
            Direction = $rule.Direction
            Action = $rule.Action
            RemoteAddress = ($addresses.RemoteAddress -join ', ')
        }
    } | Format-Table -AutoSize

Write-Host "`n--- Verdict ---"
$listener = @(Get-NetTCPConnection -State Listen -LocalPort 22 -ErrorAction SilentlyContinue)
$service = Get-Service -Name sshd -ErrorAction SilentlyContinue
if ($service -and $service.Status -eq 'Running' -and $listener.Count -gt 0) {
    Write-Host 'SSH_READY: sshd is running and TCP/22 is listening.' -ForegroundColor Green
} else {
    Write-Host 'SSH_NOT_READY: run enable_stand_remote_access_win11.ps1 as Administrator on this exact PC.' -ForegroundColor Yellow
}
