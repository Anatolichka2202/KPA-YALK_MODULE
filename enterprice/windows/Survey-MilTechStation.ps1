# MilTech Station Windows survey -- READ ONLY. PowerShell 5.1+/Windows 10+
# Does not change IP addresses, routing, firewall, VLAN, COM devices, or reserved ports.
[CmdletBinding()]
param([string]$Output = "$env:TEMP\miltech-station-survey.json")
$ErrorActionPreference = 'Stop'
$state = [ordered]@{
    CapturedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
    Host = $env:COMPUTERNAME
    OS = (Get-CimInstance Win32_OperatingSystem).Caption
    Adapters = @(Get-NetAdapter | Select-Object Name, InterfaceDescription, Status, MacAddress, ifIndex, LinkSpeed)
    Addresses = @(Get-NetIPAddress -AddressFamily IPv4 | Select-Object InterfaceAlias, InterfaceIndex, IPAddress, PrefixLength, SkipAsSource, PrefixOrigin)
    Routes = @(Get-NetRoute -AddressFamily IPv4 | Select-Object InterfaceAlias, DestinationPrefix, NextHop, RouteMetric, PolicyStore)
    TcpListeners = @(Get-NetTCPConnection -State Listen | Select-Object LocalAddress, LocalPort, OwningProcess)
    UdpEndpoints = @(Get-NetUDPEndpoint | Select-Object LocalAddress, LocalPort, OwningProcess)
    SerialPorts = @(Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Name, PNPDeviceID)
    TcpExcludedRanges = @(netsh interface ipv4 show excludedportrange protocol=tcp)
    UdpExcludedRanges = @(netsh interface ipv4 show excludedportrange protocol=udp)
}
$state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Output -Encoding UTF8
Write-Host "Inventory saved to $Output"
Write-Host "NOTE: inventory may contain internal addresses and device identifiers; handle as restricted operational data."
