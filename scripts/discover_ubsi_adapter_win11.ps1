[CmdletBinding()]
param(
    [string]$LocalAddress = '192.168.0.50',
    [string]$TargetAddress = '192.168.0.255',
    [int]$DataPort = 1001,
    [int]$AcknowledgementPort = 1101,
    [ValidateRange(0, 255)]
    [int]$Mode = 6,
    [ValidateRange(1, 60)]
    [int]$ListenSeconds = 8,
    [switch]$Active
)

$ErrorActionPreference = 'Stop'

function New-BoundUdpClient {
    param([int]$Port)

    $client = [System.Net.Sockets.UdpClient]::new()
    $client.Client.SetSocketOption(
        [System.Net.Sockets.SocketOptionLevel]::Socket,
        [System.Net.Sockets.SocketOptionName]::ReuseAddress,
        $true)
    $endpoint = [System.Net.IPEndPoint]::new(
        [System.Net.IPAddress]::Parse($LocalAddress), $Port)
    $client.Client.Bind($endpoint)
    return $client
}

function Format-HexPreview {
    param([byte[]]$Bytes)

    $length = [Math]::Min($Bytes.Length, 64)
    return (($Bytes[0..($length - 1)] | ForEach-Object { $_.ToString('X2') }) -join '')
}

$dataClient = $null
$ackClient = $null
$sender = $null

try {
    $dataClient = New-BoundUdpClient -Port $DataPort
    $ackClient = New-BoundUdpClient -Port $AcknowledgementPort

    Write-Output "UBSI_DISCOVERY local=$LocalAddress target=$TargetAddress data_port=$DataPort ack_port=$AcknowledgementPort"
    if ($Active) {
        $senderEndpoint = [System.Net.IPEndPoint]::new(
            [System.Net.IPAddress]::Parse($LocalAddress), 0)
        $sender = [System.Net.Sockets.UdpClient]::new($senderEndpoint)
        $sender.EnableBroadcast = $true
        [byte[]]$command = @(0x44, 0x01, $Mode)
        $target = [System.Net.IPEndPoint]::new(
            [System.Net.IPAddress]::Parse($TargetAddress), $DataPort)
        $sent = $sender.Send($command, $command.Length, $target)
        Write-Output "ACTIVE_SELECT mode=$Mode command=4401$($Mode.ToString('X2')) bytes=$sent"
    } else {
        Write-Output 'PASSIVE no_command_sent=true'
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($ListenSeconds)
    $packets = 0
    $senders = [System.Collections.Generic.HashSet[string]]::new()
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($item in @(
                @{ Name = 'DATA'; Client = $dataClient },
                @{ Name = 'ACK'; Client = $ackClient })) {
            while ($item.Client.Available -gt 0) {
                $remote = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
                [byte[]]$bytes = $item.Client.Receive([ref]$remote)
                # Windows delivers our own broadcast back to the data socket.
                # It proves only that the command left this process, not that
                # the adapter answered, so do not count it as a device packet.
                if ($remote.Address.ToString() -eq $LocalAddress) {
                    Write-Output "LOCAL_ECHO kind=$($item.Name) from=$remote size=$($bytes.Length)"
                    continue
                }
                $packets++
                [void]$senders.Add($remote.Address.ToString())
                $preview = Format-HexPreview -Bytes $bytes
                Write-Output "PACKET kind=$($item.Name) from=$remote size=$($bytes.Length) hex=$preview"
            }
        }
        Start-Sleep -Milliseconds 20
    }

    $senderList = ($senders | Sort-Object) -join ','
    Write-Output "RESULT packets=$packets senders=$senderList"
    if ($packets -eq 0) { exit 3 }
} finally {
    if ($sender) { $sender.Dispose() }
    if ($ackClient) { $ackClient.Dispose() }
    if ($dataClient) { $dataClient.Dispose() }
}
