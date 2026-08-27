#requires -version 5.1

<##
.SYNOPSIS
    Read-only inventory and connectivity diagnostics for the Orbita test stand.

.DESCRIPTION
    Collects Windows, network, driver, PnP, VISA, LCard/E20-10 and SSH data.
    It pings the known Ethernet/RS-485 adapter and can passively listen for one
    UDP datagram. If a deployed Orbita package is supplied, its probe utility
    performs read-only plugin checks (*IDN?, GETD or the device equivalent).
##>

[CmdletBinding()]
param(
    [string]$OutputDirectory = (Get-Location).Path,
    [string]$StandAddress = '192.168.0.50',
    [string]$AdapterAddress = '192.168.0.101',
    [string]$AdapterMac = '00-35-65-03-74-01',
    [ValidateRange(1, 30)]
    [int]$PassiveListenSeconds = 3,
    [switch]$SkipPassiveUdpCapture,
    [string]$OrbitaDirectory = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$safeComputerName = ($env:COMPUTERNAME -replace '[^A-Za-z0-9_.-]', '_')
if ([string]::IsNullOrWhiteSpace($safeComputerName)) {
    $safeComputerName = 'unknown-host'
}

$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $outputRoot)) {
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
}

$reportDirectory = Join-Path $outputRoot "orbita_stand_diag_${safeComputerName}_${timestamp}"
New-Item -ItemType Directory -Path $reportDirectory | Out-Null

function Save-Report {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Body
    )

    $path = Join-Path $reportDirectory $Name
    $header = @(
        "Report: $Name"
        "Computer: $env:COMPUTERNAME"
        "Collected: $(Get-Date -Format o)"
        ('=' * 78)
    )
    $header | Out-File -LiteralPath $path -Encoding UTF8

    try {
        & $Body *>&1 | Out-String -Width 4096 | Add-Content -LiteralPath $path -Encoding UTF8
    }
    catch {
        "ERROR: $($_.Exception.Message)" | Add-Content -LiteralPath $path -Encoding UTF8
        $_ | Format-List * -Force | Out-String -Width 4096 | Add-Content -LiteralPath $path -Encoding UTF8
    }
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-RegistryPrograms {
    $locations = @(
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )

    foreach ($location in $locations) {
        Get-ItemProperty -Path $location -ErrorAction SilentlyContinue |
            Select-Object DisplayName, DisplayVersion, Publisher, InstallDate, InstallLocation |
            Where-Object { $_.DisplayName }
    }
}

function Get-CommandInventory {
    $names = @(
        'cmake', 'ninja', 'qmake', 'qtpaths', 'windeployqt',
        'cl', 'gcc', 'g++', 'clang', 'clang++',
        'git', 'ssh', 'scp', 'python', 'py', 'powershell', 'pwsh'
    )

    foreach ($name in $names) {
        $commands = @(Get-Command $name -All -ErrorAction SilentlyContinue)
        if ($commands.Count -eq 0) {
            [pscustomobject]@{ Name = $name; Found = $false; Source = $null; Version = $null }
            continue
        }
        foreach ($command in $commands) {
            [pscustomobject]@{
                Name = $name
                Found = $true
                Source = $command.Source
                Version = if ($command.Version) { $command.Version.ToString() } else { $null }
            }
        }
    }
}

function Receive-PassiveUdpPacket {
    param([int]$Port, [int]$TimeoutSeconds)

    $occupied = @(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)
    if ($occupied.Count -gt 0) {
        return [pscustomobject]@{
            Port = $Port
            Result = 'SKIPPED_PORT_ALREADY_IN_USE'
            Remote = $null
            Length = $null
            FirstBytesHex = $null
        }
    }

    $client = $null
    try {
        $client = New-Object System.Net.Sockets.UdpClient($Port)
        $client.Client.ReceiveTimeout = $TimeoutSeconds * 1000
        $remote = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
        $bytes = $client.Receive([ref]$remote)
        $previewLength = [Math]::Min(64, $bytes.Length)
        $preview = if ($previewLength -gt 0) {
            [BitConverter]::ToString($bytes, 0, $previewLength)
        }
        else {
            ''
        }
        return [pscustomobject]@{
            Port = $Port
            Result = 'RECEIVED'
            Remote = $remote.ToString()
            Length = $bytes.Length
            FirstBytesHex = $preview
        }
    }
    catch [System.Net.Sockets.SocketException] {
        if ($_.Exception.SocketErrorCode -eq [System.Net.Sockets.SocketError]::TimedOut) {
            return [pscustomobject]@{
                Port = $Port
                Result = 'NO_PACKET_DURING_PASSIVE_WINDOW'
                Remote = $null
                Length = $null
                FirstBytesHex = $null
            }
        }
        return [pscustomobject]@{
            Port = $Port
            Result = "SOCKET_ERROR: $($_.Exception.Message)"
            Remote = $null
            Length = $null
            FirstBytesHex = $null
        }
    }
    finally {
        if ($null -ne $client) { $client.Dispose() }
    }
}

$isAdministrator = Test-IsAdministrator

Save-Report '00_README.txt' {
    @"
This archive was created by stand_full_diagnostics_win11.ps1.

Safety:
- no UDP control command was sent to the Ethernet/RS-485 adapter;
- the optional equipment probe only sends documented identification/state queries;
- no source output, route, generator, resistance or operating mode was changed;
- passive UDP capture, when enabled, only waited for an already transmitted packet;
- the script did not change drivers, firewall, network settings or services.

Expected stand address: $StandAddress
Known adapter firmware address: $AdapterAddress
Known adapter firmware MAC: $AdapterMac
Known adapter UDP ports: data/control 1001, acknowledgements 1101, raw bridge 999.
Run as administrator for the most complete driver, firewall and event-log data.
Running as administrator: $isAdministrator
"@
}

Save-Report '01_system.txt' {
    '--- Operating system ---'
    Get-CimInstance Win32_OperatingSystem |
        Select-Object Caption, Version, BuildNumber, OSArchitecture, InstallDate, LastBootUpTime,
            WindowsDirectory, SystemDirectory, Locale | Format-List
    '--- Computer ---'
    Get-CimInstance Win32_ComputerSystem |
        Select-Object Manufacturer, Model, SystemType, TotalPhysicalMemory, Domain,
            PartOfDomain, UserName | Format-List
    '--- BIOS ---'
    Get-CimInstance Win32_BIOS |
        Select-Object Manufacturer, SMBIOSBIOSVersion, ReleaseDate, SerialNumber | Format-List
    '--- CPU ---'
    Get-CimInstance Win32_Processor |
        Select-Object Name, Manufacturer, NumberOfCores, NumberOfLogicalProcessors,
            MaxClockSpeed, AddressWidth | Format-List
    '--- Memory modules ---'
    Get-CimInstance Win32_PhysicalMemory |
        Select-Object Manufacturer, PartNumber, SerialNumber, Capacity, Speed | Format-Table -AutoSize
    '--- Time zone ---'
    Get-TimeZone | Format-List *
}

Save-Report '02_security_and_powershell.txt' {
    "Administrator: $isAdministrator"
    "User: $([Security.Principal.WindowsIdentity]::GetCurrent().Name)"
    "PowerShell: $($PSVersionTable.PSVersion)"
    "Edition: $($PSVersionTable.PSEdition)"
    "CLR: $($PSVersionTable.CLRVersion)"
    "Execution policy: $(Get-ExecutionPolicy)"
    '--- UAC ---'
    Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System' -ErrorAction SilentlyContinue |
        Select-Object EnableLUA, ConsentPromptBehaviorAdmin, PromptOnSecureDesktop | Format-List
    '--- Defender ---'
    if (Get-Command Get-MpComputerStatus -ErrorAction SilentlyContinue) {
        Get-MpComputerStatus |
            Select-Object AntivirusEnabled, AntispywareEnabled, RealTimeProtectionEnabled,
                IoavProtectionEnabled, NISEnabled, AntivirusSignatureVersion,
                AntivirusSignatureLastUpdated | Format-List
    }
}

Save-Report '03_storage.txt' {
    '--- Volumes ---'
    Get-CimInstance Win32_LogicalDisk |
        Select-Object DeviceID, DriveType, VolumeName, FileSystem, Size, FreeSpace | Format-Table -AutoSize
    '--- Physical disks ---'
    Get-CimInstance Win32_DiskDrive |
        Select-Object Model, InterfaceType, MediaType, Size, Status, SerialNumber | Format-Table -AutoSize
}

Save-Report '10_network_adapters.txt' {
    '--- Net adapters ---'
    Get-NetAdapter -IncludeHidden |
        Sort-Object ifIndex |
        Select-Object ifIndex, Name, InterfaceDescription, Status, LinkSpeed, MacAddress,
            DriverInformation, DriverFileName | Format-Table -AutoSize
    '--- IP configuration ---'
    Get-NetIPConfiguration -Detailed | Format-List *
    '--- IP addresses ---'
    Get-NetIPAddress |
        Sort-Object InterfaceIndex, AddressFamily |
        Select-Object InterfaceIndex, InterfaceAlias, AddressFamily, IPAddress, PrefixLength,
            PrefixOrigin, SuffixOrigin, AddressState | Format-Table -AutoSize
}

Save-Report '11_network_native.txt' {
    '--- ipconfig /all ---'
    ipconfig /all
    '--- route print ---'
    route print
    '--- arp -a ---'
    arp -a
    '--- netstat -ano ---'
    netstat -ano
}

Save-Report '12_routes_neighbors_ports.txt' {
    '--- Routes ---'
    Get-NetRoute |
        Sort-Object AddressFamily, RouteMetric |
        Select-Object AddressFamily, DestinationPrefix, NextHop, InterfaceAlias,
            RouteMetric, State | Format-Table -AutoSize
    '--- Neighbors ---'
    Get-NetNeighbor |
        Sort-Object InterfaceIndex, IPAddress |
        Select-Object InterfaceIndex, InterfaceAlias, IPAddress, LinkLayerAddress, State |
        Format-Table -AutoSize
    '--- TCP listeners ---'
    Get-NetTCPConnection -State Listen |
        Sort-Object LocalPort |
        Select-Object LocalAddress, LocalPort, OwningProcess | Format-Table -AutoSize
    '--- UDP endpoints ---'
    Get-NetUDPEndpoint |
        Sort-Object LocalPort |
        Select-Object LocalAddress, LocalPort, OwningProcess | Format-Table -AutoSize
}

Save-Report '13_known_addresses.txt' {
    "Expected stand address: $StandAddress"
    "Ethernet/RS-485 adapter address from firmware: $AdapterAddress"
    "Ethernet/RS-485 adapter MAC from firmware: $AdapterMac"
    '--- Local address match ---'
    Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -eq $StandAddress } |
        Format-List InterfaceAlias, InterfaceIndex, IPAddress, PrefixLength, AddressState
    '--- Ping adapter (ICMP only) ---'
    $ping = Test-Connection -ComputerName $AdapterAddress -Count 2 -Quiet -ErrorAction SilentlyContinue
    "Ping result: $ping"
    '--- Adapter neighbor record ---'
    $neighbors = @(Get-NetNeighbor -IPAddress $AdapterAddress -ErrorAction SilentlyContinue)
    $neighbors | Format-List *
    if ($neighbors.Count -gt 0) {
        $expected = $AdapterMac.Replace(':', '-').ToUpperInvariant()
        $matched = @($neighbors | Where-Object {
            $_.LinkLayerAddress -and $_.LinkLayerAddress.Replace(':', '-').ToUpperInvariant() -eq $expected
        }).Count -gt 0
        "Firmware MAC match: $matched"
    }
    '--- Known UDP port ownership ---'
    Get-NetUDPEndpoint -ErrorAction SilentlyContinue |
        Where-Object { $_.LocalPort -in @(999, 1001, 1101) } |
        Format-Table LocalAddress, LocalPort, OwningProcess -AutoSize
}

Save-Report '14_passive_udp_capture.txt' {
    if ($SkipPassiveUdpCapture) {
        'Passive capture was disabled by -SkipPassiveUdpCapture.'
    }
    else {
        "Waiting up to $PassiveListenSeconds second(s) per port. No packet is transmitted."
        Receive-PassiveUdpPacket -Port 1001 -TimeoutSeconds $PassiveListenSeconds | Format-List
        Receive-PassiveUdpPacket -Port 1101 -TimeoutSeconds $PassiveListenSeconds | Format-List
    }
}

Save-Report '20_pnp_all.txt' {
    if (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue) {
        Get-PnpDevice -PresentOnly |
            Sort-Object Class, FriendlyName |
            Select-Object Status, Class, FriendlyName, InstanceId, Problem, ConfigManagerErrorCode |
            Format-Table -AutoSize
    }
    else {
        Get-CimInstance Win32_PnPEntity |
            Sort-Object PNPClass, Name |
            Select-Object Status, PNPClass, Name, DeviceID, ConfigManagerErrorCode |
            Format-Table -AutoSize
    }
}

Save-Report '21_pnp_errors.txt' {
    if (Get-Command Get-PnpDevice -ErrorAction SilentlyContinue) {
        Get-PnpDevice -PresentOnly |
            Where-Object { $_.Status -ne 'OK' -or $_.Problem -ne 0 } |
            Select-Object Status, Class, FriendlyName, InstanceId, Problem, ConfigManagerErrorCode |
            Format-Table -AutoSize
    }
    Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.ConfigManagerErrorCode -ne 0 } |
        Select-Object Name, PNPClass, DeviceID, ConfigManagerErrorCode, Status |
        Format-Table -AutoSize
}

Save-Report '22_usb_and_serial.txt' {
    '--- COM ports ---'
    Get-CimInstance Win32_SerialPort |
        Select-Object DeviceID, Name, Description, PNPDeviceID, ProviderType, Status |
        Format-Table -AutoSize
    '--- USB controllers ---'
    Get-CimInstance Win32_USBController |
        Select-Object Name, DeviceID, Status | Format-Table -AutoSize
    '--- USB hubs ---'
    Get-CimInstance Win32_USBHub |
        Select-Object Name, DeviceID, PNPDeviceID, Status | Format-Table -AutoSize
    '--- USB PnP devices ---'
    Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.PNPDeviceID -like 'USB*' } |
        Sort-Object Name |
        Select-Object Name, PNPClass, Manufacturer, PNPDeviceID, Status, ConfigManagerErrorCode |
        Format-Table -AutoSize
}

Save-Report '23_relevant_equipment.txt' {
    $v7Cyrillic = ([char]0x0412).ToString() + '7[- ]?78'
    $ya2mCyrillic = ([char]0x042F).ToString() + '2' + ([char]0x041C).ToString()
    $mb26Cyrillic = ([char]0x041C).ToString() + ([char]0x0411).ToString() + '26'
    $pattern = "LCard|E20[- ]?10|LUSB|VISA|GPIB|V7[- ]?78|$v7Cyrillic|YA2M|$ya2mCyrillic|MB26|$mb26Cyrillic|RS[- ]?485|Serial|COM Port"
    '--- Matching PnP devices ---'
    Get-CimInstance Win32_PnPEntity |
        Where-Object { ($_.Name -match $pattern) -or ($_.Manufacturer -match $pattern) -or ($_.PNPDeviceID -match $pattern) } |
        Select-Object Name, PNPClass, Manufacturer, PNPDeviceID, Status, ConfigManagerErrorCode |
        Format-List
    '--- Matching services and drivers ---'
    Get-CimInstance Win32_SystemDriver |
        Where-Object { ($_.Name -match $pattern) -or ($_.DisplayName -match $pattern) -or ($_.PathName -match $pattern) } |
        Select-Object Name, DisplayName, State, StartMode, PathName, ServiceType |
        Format-List
    Get-Service |
        Where-Object { ($_.Name -match $pattern) -or ($_.DisplayName -match $pattern) } |
        Select-Object Status, Name, DisplayName, StartType | Format-Table -AutoSize
}

Save-Report '24_drivers.txt' {
    '--- Signed PnP drivers ---'
    Get-CimInstance Win32_PnPSignedDriver |
        Sort-Object DeviceName |
        Select-Object DeviceName, Manufacturer, DriverProviderName, DriverVersion,
            DriverDate, InfName, IsSigned, DeviceID | Format-Table -AutoSize
    '--- Native driverquery ---'
    driverquery /v /fo csv
}

Save-Report '25_lcard_files.txt' {
    $roots = @(
        $PSScriptRoot,
        'C:\LCard',
        'C:\Program Files\LCard',
        'C:\Program Files (x86)\LCard',
        'C:\Program Files\L-CARD',
        'C:\Program Files (x86)\L-CARD'
    ) | Select-Object -Unique

    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        "--- $root ---"
        Get-ChildItem -LiteralPath $root -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match 'Lusbapi|E2010|E20-10|lcard' } |
            ForEach-Object {
                $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256 -ErrorAction SilentlyContinue
                $signature = Get-AuthenticodeSignature -LiteralPath $_.FullName -ErrorAction SilentlyContinue
                [pscustomobject]@{
                    Path = $_.FullName
                    Size = $_.Length
                    Version = $_.VersionInfo.FileVersion
                    SHA256 = $hash.Hash
                    Signature = $signature.Status
                }
            } | Format-List
    }
}

Save-Report '30_visa.txt' {
    '--- VISA-related installed software ---'
    Get-RegistryPrograms |
        Where-Object { $_.DisplayName -match 'VISA|NI[- ]|National Instruments|Keysight|Agilent|Rohde|R&S' } |
        Sort-Object DisplayName | Format-Table -AutoSize

    '--- VISA registry roots ---'
    $registryRoots = @(
        'HKLM:\SOFTWARE\IVI Foundation',
        'HKLM:\SOFTWARE\WOW6432Node\IVI Foundation',
        'HKLM:\SOFTWARE\National Instruments',
        'HKLM:\SOFTWARE\WOW6432Node\National Instruments',
        'HKLM:\SOFTWARE\Keysight',
        'HKLM:\SOFTWARE\WOW6432Node\Keysight'
    )
    foreach ($root in $registryRoots) {
        "${root}: $(Test-Path -LiteralPath $root)"
        if (Test-Path -LiteralPath $root) {
            Get-ChildItem -LiteralPath $root -ErrorAction SilentlyContinue |
                Select-Object Name, Property | Format-List
        }
    }

    '--- VISA files and environment ---'
    foreach ($name in @('VXIPNPPATH', 'VXIPNPPATH64', 'IVIROOTDIR32', 'IVIROOTDIR64', 'NIEXTCCOMPILERSUPP')) {
        [pscustomobject]@{ Name = $name; Value = [Environment]::GetEnvironmentVariable($name) }
    }
    @(
        foreach ($path in @(
        'C:\Windows\System32\visa32.dll',
        'C:\Windows\SysWOW64\visa32.dll',
        'C:\Windows\System32\visa64.dll'
        )) {
            if (Test-Path -LiteralPath $path) {
                $item = Get-Item -LiteralPath $path
                $hash = Get-FileHash -LiteralPath $path -Algorithm SHA256
                [pscustomobject]@{
                    Path = $item.FullName
                    Version = $item.VersionInfo.FileVersion
                    SHA256 = $hash.Hash
                    Signature = (Get-AuthenticodeSignature -LiteralPath $path).Status
                }
            }
            else {
                [pscustomobject]@{ Path = $path; Version = $null; SHA256 = $null; Signature = 'NOT_FOUND' }
            }
        }
    ) | Format-List
}

Save-Report '34_orbita_plugins_and_safe_probes.txt' {
    if ([string]::IsNullOrWhiteSpace($OrbitaDirectory)) {
        'SKIPPED: pass -OrbitaDirectory C:\Orbita\releases\<version> to run plugin probes.'
        return
    }
    $orbitaRoot = [System.IO.Path]::GetFullPath($OrbitaDirectory)
    $probe = Join-Path $orbitaRoot 'orbita_equipment_probe.exe'
    $profile = Join-Path $orbitaRoot 'profiles\stand_ktma.yaml'
    $plugins = Join-Path $orbitaRoot 'plugins'
    "Orbita directory: $orbitaRoot"
    "Probe: $(Test-Path -LiteralPath $probe)"
    "Profile: $(Test-Path -LiteralPath $profile)"
    "Plugins: $(Test-Path -LiteralPath $plugins)"
    if (-not (Test-Path -LiteralPath $probe)) { return }
    if (-not (Test-Path -LiteralPath $profile)) { return }
    if (-not (Test-Path -LiteralPath $plugins)) { return }

    '--- Plugin DLL versions and hashes ---'
    Get-ChildItem -LiteralPath $plugins -Filter '*.dll' -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            [pscustomobject]@{
                Name = $_.Name
                Version = $_.VersionInfo.FileVersion
                SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
            }
        } | Format-Table -AutoSize

    '--- Safe equipment probes ---'
    & $probe $profile $plugins 2>&1
    "Exit code: $LASTEXITCODE"
}

Save-Report '31_installed_software.txt' {
    Get-RegistryPrograms |
        Sort-Object DisplayName, DisplayVersion -Unique |
        Format-Table -AutoSize
}

Save-Report '32_services.txt' {
    Get-Service |
        Sort-Object Status, DisplayName |
        Select-Object Status, StartType, Name, DisplayName | Format-Table -AutoSize
}

Save-Report '33_processes.txt' {
    Get-Process -ErrorAction SilentlyContinue |
        Sort-Object ProcessName |
        Select-Object ProcessName, Id, Path, Company, ProductVersion |
        Format-Table -AutoSize
}

Save-Report '40_toolchain.txt' {
    Get-CommandInventory | Format-Table -AutoSize
    '--- Selected environment variables ---'
    @(
        foreach ($name in @('PATH', 'QTDIR', 'Qt6_DIR', 'CMAKE_PREFIX_PATH', 'CC', 'CXX', 'VCPKG_ROOT')) {
            [pscustomobject]@{ Name = $name; Value = [Environment]::GetEnvironmentVariable($name) }
        }
    ) | Format-List
}

Save-Report '41_openssh.txt' {
    '--- OpenSSH Windows capabilities ---'
    if (Get-Command Get-WindowsCapability -ErrorAction SilentlyContinue) {
        try {
            Get-WindowsCapability -Online -Name 'OpenSSH*' -ErrorAction Stop |
                Select-Object Name, State | Format-Table -AutoSize
        }
        catch {
            "Capability query unavailable without elevation: $($_.Exception.Message)"
        }
    }
    '--- sshd service ---'
    Get-Service sshd -ErrorAction SilentlyContinue | Format-List *
    '--- Port 22 listener ---'
    Get-NetTCPConnection -State Listen -LocalPort 22 -ErrorAction SilentlyContinue |
        Format-Table LocalAddress, LocalPort, OwningProcess -AutoSize
    '--- OpenSSH firewall rules ---'
    if (Get-Command Get-NetFirewallRule -ErrorAction SilentlyContinue) {
        Get-NetFirewallRule -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match 'OpenSSH|sshd' -or $_.DisplayName -match 'OpenSSH|sshd' } |
            Format-List Name, DisplayName, Enabled, Direction, Action, Profile
    }
}

Save-Report '50_recent_system_events.txt' {
    $start = (Get-Date).AddDays(-7)
    Get-WinEvent -FilterHashtable @{ LogName = 'System'; StartTime = $start; Level = 1, 2, 3 } -MaxEvents 500 -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -match 'Kernel-PnP|DriverFrameworks|Service Control|Disk|Ntfs|WHEA|USB|Network|TCPIP' } |
        Select-Object TimeCreated, LevelDisplayName, ProviderName, Id, Message |
        Format-List
}

Save-Report '51_device_install_events.txt' {
    $logs = @(
        'Microsoft-Windows-DeviceSetupManager/Admin',
        'Microsoft-Windows-Kernel-PnP/Configuration'
    )
    foreach ($log in $logs) {
        "--- $log ---"
        Get-WinEvent -FilterHashtable @{ LogName = $log; StartTime = (Get-Date).AddDays(-30) } -MaxEvents 300 -ErrorAction SilentlyContinue |
            Select-Object TimeCreated, LevelDisplayName, ProviderName, Id, Message |
            Format-List
    }
}

$manifestPath = Join-Path $reportDirectory 'SHA256SUMS.txt'
Get-ChildItem -LiteralPath $reportDirectory -File |
    Where-Object { $_.FullName -ne $manifestPath } |
    Sort-Object Name |
    ForEach-Object {
        $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
        "{0} *{1}" -f $hash.Hash, $_.Name
    } | Out-File -LiteralPath $manifestPath -Encoding ASCII

$zipPath = "$reportDirectory.zip"
Compress-Archive -Path (Join-Path $reportDirectory '*') -DestinationPath $zipPath -CompressionLevel Optimal
$zipHash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
$zipHashPath = "$zipPath.sha256.txt"
"{0} *{1}" -f $zipHash.Hash, ([System.IO.Path]::GetFileName($zipPath)) |
    Out-File -LiteralPath $zipHashPath -Encoding ASCII

Write-Host ''
Write-Host 'Diagnostics complete.'
Write-Host "Report directory: $reportDirectory"
Write-Host "Archive:          $zipPath"
Write-Host "Archive SHA-256:  $($zipHash.Hash)"
Write-Host 'Return the ZIP archive and its .sha256.txt file.'
