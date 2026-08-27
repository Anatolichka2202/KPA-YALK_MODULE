[CmdletBinding()]
param(
    [string]$AdapterAddress = '192.168.0.115',
    [string]$LocalAddress = '192.168.0.50',
    [int[]]$Modes = @(0, 1, 2, 6, 7, 8, 11),
    [int]$CommandPort = 1001,
    [int]$AcknowledgementPort = 1101,
    [int]$PassiveSeconds = 5,
    [int]$ReceiveMilliseconds = 1500,
    [string]$OutputDirectory = (Join-Path $env:PUBLIC ('OrbitaDiag\\ubsi_adapter_' + (Get-Date -Format 'yyyyMMdd_HHmmss')))
)

$ErrorActionPreference = 'Stop'

function New-UdpReceiver([System.Net.IPAddress]$address, [int]$port) {
    $socket = [System.Net.Sockets.UdpClient]::new(
        [System.Net.IPEndPoint]::new($address, $port))
    $socket.Client.Blocking = $false
    return $socket
}

function Receive-Available(
    [System.Net.Sockets.UdpClient]$socket,
    [string]$kind,
    [int]$mode,
    [string]$outputDirectory,
    [System.Collections.Generic.List[string]]$events
) {
    while ($socket.Available -gt 0) {
        $remote = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
        $bytes = $socket.Receive([ref]$remote)
        $stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
        $fileName = '{0}_{1}_mode{2}_{3}_{4}_{5}.bin' -f $stamp, $kind, $mode,
            $remote.Address, $remote.Port, $bytes.Length
        $path = Join-Path $outputDirectory $fileName
        [System.IO.File]::WriteAllBytes($path, $bytes)
        $events.Add(('{0};{1};{2};{3};{4};{5}' -f (Get-Date -Format o), $kind,
            $mode, $remote.Address, $remote.Port, $bytes.Length))
        Write-Output ('RX kind={0} mode={1} from={2}:{3} bytes={4} file={5}' -f
            $kind, $mode, $remote.Address, $remote.Port, $bytes.Length, $fileName)
    }
}

function Wait-And-Drain(
    [int]$milliseconds,
    [System.Net.Sockets.UdpClient]$dataSocket,
    [System.Net.Sockets.UdpClient]$ackSocket,
    [int]$mode,
    [string]$outputDirectory,
    [System.Collections.Generic.List[string]]$events
) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($milliseconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        Receive-Available $dataSocket 'data' $mode $outputDirectory $events
        Receive-Available $ackSocket 'ack' $mode $outputDirectory $events
        Start-Sleep -Milliseconds 20
    }
    Receive-Available $dataSocket 'data' $mode $outputDirectory $events
    Receive-Available $ackSocket 'ack' $mode $outputDirectory $events
}

$local = [System.Net.IPAddress]::Parse($LocalAddress)
$adapter = [System.Net.IPAddress]::Parse($AdapterAddress)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$events = [System.Collections.Generic.List[string]]::new()
$events.Add('time;kind;mode;source_ip;source_port;bytes')

$dataSocket = New-UdpReceiver $local $CommandPort
$ackSocket = New-UdpReceiver $local $AcknowledgementPort
$sender = [System.Net.Sockets.UdpClient]::new([System.Net.IPEndPoint]::new($local, 0))

try {
    Write-Output "LISTEN data=$LocalAddress`:$CommandPort ack=$LocalAddress`:$AcknowledgementPort adapter=$AdapterAddress"
    Wait-And-Drain ($PassiveSeconds * 1000) $dataSocket $ackSocket -1 $OutputDirectory $events

    foreach ($mode in $Modes) {
        if ($mode -lt 0 -or $mode -gt 255) { throw "Invalid mode: $mode" }
        [byte[]]$command = 0x44, 0x01, [byte]$mode
        [void]$sender.Send($command, $command.Length,
            [System.Net.IPEndPoint]::new($adapter, $CommandPort))
        $events.Add(('{0};tx;{1};{2};{3};{4}' -f (Get-Date -Format o), $mode,
            $adapter, $CommandPort, $command.Length))
        Write-Output ('TX mode={0} to={1}:{2} hex={3}' -f $mode, $adapter,
            $CommandPort, ([BitConverter]::ToString($command)))
        Wait-And-Drain $ReceiveMilliseconds $dataSocket $ackSocket $mode $OutputDirectory $events
    }
} finally {
    $sender.Dispose()
    $dataSocket.Dispose()
    $ackSocket.Dispose()
    $eventsPath = Join-Path $OutputDirectory 'events.csv'
    [System.IO.File]::WriteAllLines($eventsPath, $events, [System.Text.Encoding]::UTF8)
    Write-Output "REPORT=$OutputDirectory"
}
